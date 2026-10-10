//===- Guards.cpp - Exception-recovery refusal tests ------------*- C++ -*-===//
//
// Checks that recoverPrimitiveExceptionRegion refuses, leaving the IR
// byte-identical, whenever a mutation breaks a fact its proof relies on, and
// that every step budget below the exact cost refuses atomically. Forged
// ctnative.* proof attributes are added before most refusals to show that the
// proof is rebuilt from the live IR rather than read back from attributes.
//
//===----------------------------------------------------------------------===//

#include "Tests.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "ctcompile/CTNative/Transforms/Passes.h"
#include "llvm/ADT/DenseSet.h"

namespace ctcompile::test::exception_recovery {

/// Applies eight edits after the protected call, one at a time, and checks that
/// CheckedInvocations recovery refuses with the IR unchanged:
/// 0/1 rewire the catch edge's first operand (to the call result, to a function
/// argument), 2 replaces a continuation operand with the call result, 3 adds a
/// constant, 4 publishes the result to a global, 5 adds a global load before the
/// call, 6 duplicates the call, 7 replaces the result in the continuation with a
/// block argument. When `resolve` is false, it also checks that an ordinary call
/// gets no exception or native-call permission from other modes.
void testMutations(mlir::MLIRContext & context, llvm::StringRef source, bool resolve) {
    for (unsigned mutation = 0; mutation != 8; ++mutation) {
        auto module = import(context, source, resolve);
        if (!module) { return; }
        auto function = guarded(*module);
        mlir::Operation * call = nullptr;
        function.walk([&](mlir::Operation * operation) {
            if (!call && llvm::isa<ctjs::CallDirectOp, ctjs::CallOp>(operation)) {
                call = operation;
            }
        });
        if (!check(call != nullptr, "mutation retains its original source call")) { return; }
        auto status = llvm::cast<ctjs::CheckOp>(call->getBlock()->getTerminator());
        const unsigned width = static_cast<unsigned>(status.getContOperands().size());
        mlir::OpBuilder at(status);
        if (mutation == 0) {
            status->setOperand(width, call->getResult(0));
        } else if (mutation == 1) {
            status->setOperand(width, function.getBody().front().getArgument(3));
        } else if (mutation == 2) {
            status->setOperand(0, call->getResult(0));
        } else if (mutation == 3) {
            ctjs::ConstantOp::create(at, call->getLoc(), ctjs::ValueType::get(&context),
                                     ctjs::BooleanAttr::get(&context, true));
        } else if (mutation == 4) {
            mlir::OperationState store(call->getLoc(), "ctjs.store_global");
            store.addAttribute("name", at.getStringAttr("published"));
            store.addOperands(call->getResult(0));
            at.create(store);
        } else if (mutation == 5) {
            at.setInsertionPoint(call);
            ctjs::LoadGlobalOp::create(at, call->getLoc(), "falliblePrefix");
        } else if (mutation == 6) {
            at.setInsertionPoint(call);
            at.clone(*call);
        } else {
            for (auto [index, value] : llvm::enumerate(status.getContOperands())) {
                if (value == call->getResult(0)) {
                    status->setOperand(static_cast<unsigned>(index),
                                       call->getBlock()->getArgument(static_cast<unsigned>(index)));
                    break;
                }
            }
        }
        const auto before = printed(function);
        auto result = recoverPrimitiveExceptionRegion(function, 100000,
                                                      ExceptionRecoveryMode::CheckedInvocations);
        check(!result.recovered && result.refusal.find("invocation") != std::string::npos &&
                  printed(function) == before,
              "late call/state/publication mutation refuses without changing the source");
    }
    if (resolve) { return; }
    auto unresolved = import(context, source, false);
    if (!unresolved) { return; }
    auto function = guarded(*unresolved);
    const auto before = printed(function);
    for (auto mode :
         {ExceptionRecoveryMode::ExplicitThrows, ExceptionRecoveryMode::EffectCheckedInvocations}) {
        auto result = recoverPrimitiveExceptionRegion(function, 100000, mode);
        check(!result.recovered && printed(function) == before,
              "an ordinary source call supplies no generic exception or effect permission");
    }
    auto recovered = recoverPrimitiveExceptionRegion(function, 100000,
                                                     ExceptionRecoveryMode::CheckedInvocations);
    if (!check(recovered.recovered, "ordinary native refusal starts from structural recovery")) {
        return;
    }
    const auto structured = printed(function);
    mlir::PassManager passes(&context);
    passes.addPass(ctnative::createCTNativeLowerToEmitC());
    if (!check(mlir::succeeded(passes.run(*unresolved)), "generic native lowering can refuse")) {
        return;
    }
    check(function->hasAttr("ctnative.not_native"),
          "structural ordinary invocation recovery supplies no native call admission");
    function.walk([](mlir::Operation * operation) {
        ctnative::removeAttrsWithPrefix(operation, "ctnative.");
    });
    check(printed(function) == structured,
          "generic native refusal preserves every ordinary invocation continuation");
}

/// A try whose body does checked arithmetic on a possibly unknown value, then
/// calls `leaf`, which always throws 32. `#ctjs.number<N>` holds the IEEE-754
/// bit pattern of a double: 4629700416936869888 is 32.0 and
/// 4607182418800017408 is 1.0.
constexpr llvm::StringLiteral effectFixture = R"mlir(
module {
  ctjs.func private @leaf(%r: !ctjs.value, %nt: !ctjs.value,
                           %c: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %payload = ctjs.constant #ctjs.number<4629700416936869888>
    ctjs.throw %payload
  }
  ctjs.func @guarded$effects(%r: !ctjs.value, %nt: !ctjs.value,
                              %c: !ctjs.value, %unknown: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32, ctjs.not_structured = "preserved"} {
    %frame = ctjs.frame_enter 2
    %zero = ctjs.constant #ctjs.number<0>
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.push_handler ^body(%zero, %zero : !ctjs.value, !ctjs.value)
      catch ^handler(%zero, %zero : !ctjs.value, !ctjs.value)
  ^body(%a: !ctjs.value, %b: !ctjs.value):
    %condition = ctjs.truthy %unknown
    cf.cond_br %condition, ^arithmetic(%a, %b : !ctjs.value, !ctjs.value),
      ^arithmetic(%one, %b : !ctjs.value, !ctjs.value)
  ^arithmetic(%left: !ctjs.value, %scratch: !ctjs.value):
    %sum = ctjs.binary add %left, %one
    ctjs.check ^call(%sum, %scratch : !ctjs.value, !ctjs.value)
      caught ^handler(%left, %scratch : !ctjs.value, !ctjs.value)
  ^call(%saved: !ctjs.value, %old: !ctjs.value):
    %called = ctjs.call_direct @leaf(%r, %nt, %c)
    ctjs.check ^done(%saved, %called : !ctjs.value, !ctjs.value)
      caught ^handler(%saved, %old : !ctjs.value, !ctjs.value)
  ^done(%state: !ctjs.value, %returned: !ctjs.value):
    ctjs.pop_handler
    ctjs.frame_exit %frame
    ctjs.return %returned
  ^handler(%before: !ctjs.value, %oldScratch: !ctjs.value):
    %pad, %thrown = ctjs.catch_land
    %caught = ctjs.binary add %before, %one
    ctjs.frame_exit %frame
    ctjs.return %caught
  }
}
)mlir";

/// Undoes a successful recovery by putting back the original body and
/// attributes saved in `result.original`.
void restore(ctjs::FuncOp function,
             ctcompile::ctnative::lowering_detail::ExceptionRecoveryResult & result) {
    function.getBody().takeBody(result.original->getBody());
    function->setAttrs((*result.original)->getAttrs());
}

/// Checks that every budget below `complete` refuses with the IR unchanged and
/// that exactly `complete` proves again; the successful run is rolled back.
static bool checkBudgets(ctjs::FuncOp function, unsigned complete, ExceptionRecoveryMode mode,
                         llvm::function_ref<bool()> unchanged, llvm::StringRef prefix,
                         llvm::StringRef incomplete, llvm::StringRef exactLabel) {
    for (unsigned budget = 0; budget < complete; ++budget) {
        auto limited = recoverPrimitiveExceptionRegion(function, budget, mode);
        if (!check(!limited.recovered &&
                       limited.refusal.find("budget exhausted") != std::string::npos && unchanged(),
                   incomplete)) {
            llvm::errs() << prefix << budget << ": " << limited.refusal << '\n';
            return false;
        }
    }
    auto exact = recoverPrimitiveExceptionRegion(function, complete, mode);
    if (!check(exact.recovered && exact.steps == complete, exactLabel)) { return false; }
    restore(function, exact);
    return true;
}

/// Checks effect-checked recovery against the global binding of the helper
/// `choose`: the exact budget, nineteen mutations that must refuse, and
/// uncalled helpers whose throw payload is or is not a known primitive.
void testBindings(mlir::MLIRContext & context, llvm::StringRef source) {
    auto module = import(context, source);
    if (!module) { return; }
    auto function = guarded(*module);
    const auto before = printed(*module);
    const auto checks = countChecks(function);
    auto result = recoverPrimitiveExceptionRegion(function, 100000,
                                                  ExceptionRecoveryMode::EffectCheckedInvocations);
    if (!check(result.recovered, "source bindings first prove without stored annotations")) {
        llvm::errs() << result.refusal << '\n';
        return;
    }
    const unsigned complete = result.steps;
    restore(function, result);
    check(printed(*module) == before, "source binding proof rollback restores the whole module");
    if (!checkBudgets(
            function, complete, ExceptionRecoveryMode::EffectCheckedInvocations,
            [&] { return printed(*module) == before && countChecks(function) == checks; },
            "budget ", "every incomplete source binding/effect budget preserves all original IR",
            "the complete source binding/effect budget succeeds exactly")) {
        return;
    }
    check(printed(*module) == before, "exact source budget permits a fresh proof after rollback");
    llvm::outs() << "source binding recovery: " << complete
                 << " steps, every incomplete budget preserves " << checks << " checks\n";

    // Each of the 19 mutations changes the global binding, callee identity,
    // helper body or published state that the proof relies on. Forged
    // `ctnative.*` proof attributes are then added, and the proof must still
    // refuse: it must be rebuilt from the live IR, not trusted from attributes.
    // 0-2 duplicate, replace or move the `choose` declaration; 3-4 change the
    // call's target or operand 2; 5 renames the loaded global; 6-7 write or
    // read globalThis.choose; 8 marks the module as having skipped functions;
    // 9 adds an unknown op; 10-14 make the helper publish, coerce or throw its
    // argument, duplicate it, or return another argument; 15 publishes the
    // callee; 16 drops the loaded value from the continuation; 17 changes the
    // closure's operand; 18 makes the helper read a property (a possible
    // getter call).
    for (unsigned mutation = 0; mutation != 19; ++mutation) {
        auto current = import(context, source);
        if (!current) { return; }
        auto candidate = guarded(*current);
        auto prior = recoverPrimitiveExceptionRegion(
            candidate, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
        if (!check(prior.recovered, "binding mutation first proves the unchanged module")) {
            return;
        }
        restore(candidate, prior);
        auto call = firstCall(candidate);
        ctjs::FuncOp helper;
        ctjs::StoreGlobalOp declaration;
        ctjs::LoadGlobalOp load;
        for (auto body : current->getOps<ctjs::FuncOp>()) {
            if (body.getSymName() == call.getCallee()) { helper = body; }
        }
        current->walk([&](ctjs::StoreGlobalOp store) {
            if (store.getName() == "choose") { declaration = store; }
        });
        candidate.walk([&](ctjs::LoadGlobalOp found) {
            if (!load) { load = found; }
        });
        if (!check(helper && declaration && load, "binding controls find live source operands")) {
            return;
        }
        auto entry = declaration->getParentOfType<ctjs::FuncOp>();
        auto closure = declaration.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        auto type = ctjs::ValueType::get(&context);
        mlir::OpBuilder at(entry.getBody().front().getTerminator());
        auto where = call.getLoc();
        if (mutation == 0) {
            at.clone(*declaration.getOperation());
        } else if (mutation == 1) {
            declaration->setOperand(0, entry.getBody().front().getArgument(0));
        } else if (mutation == 2) {
            declaration->moveBefore(entry.getBody().front().getTerminator());
        } else if (mutation == 3) {
            call.setCalleeAttr(mlir::FlatSymbolRefAttr::get(&context, candidate.getSymName()));
        } else if (mutation == 4) {
            call->setOperand(2, candidate.getBody().front().getArgument(0));
        } else if (mutation == 5) {
            load.setNameAttr(at.getStringAttr("hostAlternative"));
        } else if (mutation == 6 || mutation == 7) {
            auto global = ctjs::LoadGlobalOp::create(at, where, "globalThis");
            auto key = ctjs::ConstantOp::create(at, where, type,
                                                ctjs::StringAttr::get(&context, "choose"));
            if (mutation == 6) {
                ctjs::SetPropertyOp::create(at, where, global, key, declaration.getValue());
            } else {
                ctjs::GetPropertyOp::create(at, where, type, global, key);
            }
        } else if (mutation == 8) {
            (*current)->setAttr("ctjs.skipped", at.getArrayAttr({at.getDictionaryAttr({})}));
        } else if (mutation == 9) {
            mlir::OperationState unknown(where, "ctjs.unproved_binding_effect");
            at.create(unknown);
        } else if (mutation == 10) {
            at.setInsertionPoint(helper.getBody().front().getTerminator());
            ctjs::StoreGlobalOp::create(at, where, "unrelated",
                                        helper.getBody().front().getArgument(3));
        } else if (mutation == 11) {
            at.setInsertionPoint(helper.getBody().front().getTerminator());
            ctjs::UnaryOp::create(at, where, type,
                                  ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Plus),
                                  helper.getBody().front().getArgument(3));
        } else if (mutation == 12) {
            helper.walk([&](ctjs::ThrowOp thrown) {
                thrown->setOperand(0, helper.getBody().front().getArgument(3));
            });
        } else if (mutation == 13) {
            auto copy = llvm::cast<ctjs::FuncOp>(helper->clone());
            copy.setSymName("duplicate$1");
            current->getBody()->push_back(copy);
        } else if (mutation == 14) {
            helper.walk([&](ctjs::ReturnOp returned) {
                returned->setOperand(0, helper.getBody().front().getArgument(2));
            });
        } else if (mutation == 15) {
            at.setInsertionPoint(call);
            ctjs::StoreGlobalOp::create(at, where, "escapedCallee", call.getCalleeValue());
        } else if (mutation == 16) {
            auto status = llvm::cast<ctjs::CheckOp>(load->getBlock()->getTerminator());
            for (auto [index, operand] : llvm::enumerate(status.getContOperands())) {
                if (operand == load.getResult()) {
                    status->setOperand(static_cast<unsigned>(index),
                                       candidate.getBody().front().getArgument(0));
                }
            }
        } else if (mutation == 17) {
            closure->setOperand(0, entry.getBody().front().getArgument(0));
        } else {
            at.setInsertionPoint(helper.getBody().front().getTerminator());
            auto key = ctjs::ConstantOp::create(at, where, type,
                                                ctjs::StringAttr::get(&context, "getter"));
            ctjs::GetPropertyOp::create(at, where, type, helper.getBody().front().getArgument(0),
                                        key);
        }
        if (mutation == 11 || mutation == 12) {
            // These mutations make the helper read its boolean parameter.
            // Pass an unknown value for it at the call site, because a single
            // caller with a known value must not prove a fact about every caller.
            entry.walk([&](ctjs::CallDirectOp actual) {
                if (actual.getCallee() == candidate.getSymName()) {
                    actual->setOperand(3, entry.getBody().front().getArgument(0));
                }
            });
        }
        // The proof must be rebuilt after the IR changes, even when forged
        // summary attributes agree with its earlier successful result.
        load->setAttr("ctnative.nothrow", at.getUnitAttr());
        helper->setAttr("ctnative.nonthrowing", at.getUnitAttr());
        (*current)->setAttr("ctnative.global_bindings_complete", at.getUnitAttr());
        const auto changed = printed(*current);
        auto refused = recoverPrimitiveExceptionRegion(
            candidate, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
        if (!check(
                !refused.recovered && !refused.refusal.empty() && printed(*current) == changed &&
                    countChecks(candidate) == checks,
                "live binding, identity, getter and completion mutations preserve source checks")) {
            llvm::errs() << "binding mutation " << mutation << ": " << refused.refusal << '\n';
        }
    }
    for (bool knownPayload : {true, false}) {
        auto suffix = knownPayload ? "\nfunction unused(value) { throw 1; }\n"
                                   : "\nfunction unused(value) { throw value; }\n";
        auto current = import(context, source.str() + suffix);
        if (!current) { return; }
        auto candidate = guarded(*current);
        const auto unchanged = printed(*current);
        auto checked = recoverPrimitiveExceptionRegion(
            candidate, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
        check(checked.recovered == knownPayload,
              "uncalled helpers need independent primitive throw payloads for binding stability");
        if (checked.recovered) { restore(candidate, checked); }
        check(printed(*current) == unchanged,
              "uncalled throw effect proof preserves every source function and operation");
    }
}

/// Rewrites `source` so that `guarded` reaches `leaf` through a chain of
/// `depth` functions (choose -> forward1 -> ... -> leaf). With `payload`, each
/// link forwards `(flag, payload)` instead of `(flag)`. A depth below 2
/// returns `source` unchanged.
std::string nestedSource(llvm::StringRef source, unsigned depth, bool payload) {
    std::string text = source.str();
    if (depth < 2) { return text; }
    text.replace(text.find("function choose("), std::string("function choose").size(),
                 "function leaf");
    const auto name = [](unsigned index) {
        return index == 0 ? std::string("choose") : "forward" + std::to_string(index);
    };
    const std::string parameters = payload ? "flag, payload" : "flag";
    for (unsigned index = 0; index < depth - 1; ++index) {
        const auto target = index == depth - 2 ? std::string("leaf") : name(index + 1);
        text += "\nfunction " + name(index) + "(" + parameters + ") { return " + target + "(" +
                parameters + "); }\n";
    }
    return text;
}

/// Returns the function whose symbol is `prefix` followed by `$` and an
/// importer-assigned suffix, or null.
ctjs::FuncOp named(mlir::ModuleOp module, llvm::StringRef prefix) {
    for (auto function : module.getOps<ctjs::FuncOp>()) {
        if (function.getSymName().starts_with((prefix + "$").str())) { return function; }
    }
    return {};
}

/// Checks recovery through a three-link call chain: the exact budget, eight
/// mutations that must refuse, and the call-depth limit of 32.
void testTransitive(mlir::MLIRContext & context, llvm::StringRef source) {
    const auto nested = nestedSource(source, 3, true);
    auto module = import(context, nested);
    if (!module) { return; }
    auto function = guarded(*module);
    const auto before = printed(*module);
    const unsigned checks = countChecks(function);
    auto result = recoverPrimitiveExceptionRegion(function, 100000,
                                                  ExceptionRecoveryMode::EffectCheckedInvocations);
    if (!check(result.recovered, "the complete transitive source family proves")) {
        llvm::errs() << result.refusal << '\n';
        return;
    }
    check(mlir::succeeded(mlir::verify(*module)), "transitive invocation recovery verifies");
    const unsigned complete = result.steps;
    restore(function, result);
    check(printed(*module) == before, "transitive rollback preserves every family body");
    if (!checkBudgets(
            function, complete, ExceptionRecoveryMode::EffectCheckedInvocations,
            [&] { return printed(*module) == before && countChecks(function) == checks; },
            "transitive budget ",
            "every transitive proof cutoff preserves the complete source graph",
            "the exact transitive budget completes")) {
        return;
    }
    check(printed(*module) == before, "exact transitive rollback retains all source operations");
    llvm::outs() << "transitive recovery: " << complete
                 << " steps, every incomplete budget preserves " << checks << " checks\n";

    // 0 and 1 make forward1 call itself or `choose` (recursion), 2 adds an
    // unused self-recursive function, 3 adds an uncalled sibling caller of
    // leaf, 4 makes leaf read a property, 5 duplicates leaf's declaration,
    // 6 changes the nested call's operand 2, and 7 marks the module as having
    // skipped functions.
    for (unsigned mutation = 0; mutation != 8; ++mutation) {
        const auto suffix =
            mutation == 2   ? "\nfunction unused(flag, payload) { return unused(flag, payload); }"
            : mutation == 3 ? "\nfunction sibling(payload) { return leaf(false, payload); }"
                            : "";
        auto current = import(context, nested + suffix);
        if (!current) { return; }
        auto candidate = guarded(*current);
        if (mutation != 2 && mutation != 3) {
            auto prior = recoverPrimitiveExceptionRegion(
                candidate, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
            if (!check(prior.recovered, "transitive mutation first proves the live family")) {
                return;
            }
            restore(candidate, prior);
        }
        auto forward = named(*current, "forward1");
        auto leaf = named(*current, "leaf");
        auto nestedCall = firstCall(forward);
        ctjs::LoadGlobalOp load;
        forward.walk([&](ctjs::LoadGlobalOp found) { load = found; });
        if (!check(forward && leaf && nestedCall && load, "transitive controls find their edges")) {
            return;
        }
        mlir::OpBuilder at(nestedCall);
        if (mutation == 0 || mutation == 1) {
            // Change both the loaded global name and the direct callee, so the
            // result is real recursion rather than a name/target mismatch.
            auto target = mutation == 0 ? forward : named(*current, "choose");
            load.setNameAttr(at.getStringAttr(mutation == 0 ? "forward1" : "choose"));
            nestedCall.setCalleeAttr(mlir::FlatSymbolRefAttr::get(&context, target.getSymName()));
        } else if (mutation == 4) {
            at.setInsertionPoint(leaf.getBody().front().getTerminator());
            auto key = ctjs::ConstantOp::create(at, leaf.getLoc(), ctjs::ValueType::get(&context),
                                                ctjs::StringAttr::get(&context, "getter"));
            ctjs::GetPropertyOp::create(at, leaf.getLoc(), ctjs::ValueType::get(&context),
                                        leaf.getBody().front().getArgument(0), key);
        } else if (mutation == 5) {
            ctjs::StoreGlobalOp declaration;
            current->walk([&](ctjs::StoreGlobalOp store) {
                if (store.getName() == "leaf") { declaration = store; }
            });
            at.setInsertionPointAfter(declaration);
            at.clone(*declaration.getOperation());
        } else if (mutation == 6) {
            nestedCall->setOperand(2, forward.getBody().front().getArgument(0));
        } else if (mutation == 7) {
            (*current)->setAttr("ctjs.skipped", at.getArrayAttr({at.getDictionaryAttr({})}));
        }
        (*current)->setAttr("ctnative.global_bindings_complete", at.getUnitAttr());
        leaf->setAttr("ctnative.nonthrowing", at.getUnitAttr());
        nestedCall->setAttr("ctnative.completions_proved", at.getUnitAttr());
        const auto changed = printed(*current);
        auto refused = recoverPrimitiveExceptionRegion(
            candidate, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
        check(!refused.recovered && !refused.refusal.empty() && printed(*current) == changed &&
                  countChecks(candidate) == checks,
              "late transitive identity, effect, actual and recursive alternatives refuse");
        if (mutation <= 2) {
            check(refused.refusal.find("recursive") != std::string::npos,
                  "recursive source components fail the complete live call census");
        }
    }
    for (unsigned depth : {32u, 33u}) {
        auto current = import(context, nestedSource(source, depth, true));
        if (!current) { return; }
        auto candidate = guarded(*current);
        const auto unchanged = printed(*current);
        auto checked = recoverPrimitiveExceptionRegion(
            candidate, 1000000, ExceptionRecoveryMode::EffectCheckedInvocations);
        check(checked.recovered == (depth == 32), "transitive completion depth is bounded at 32");
        if (checked.recovered) {
            check(mlir::succeeded(mlir::verify(*current)), "depth-boundary recovery verifies");
            restore(candidate, checked);
        } else {
            check(checked.refusal.find("depth exceeds 32") != std::string::npos,
                  "an excessive family reports the independent depth limit");
        }
        check(printed(*current) == unchanged, "depth-boundary proof retains exact rollback");
    }
}

/// Checks that the catch's saved state is proved primitive only from the
/// arguments of the protected calls themselves, not from other callers of
/// the same helpers.
void testSelectedActuals(mlir::MLIRContext & context) {
    constexpr llvm::StringLiteral source = R"js(
function leaf(flag, payload) {
    if (flag) { throw 32; }
    return payload;
}
function forward(flag, payload) { return leaf(flag, payload); }
function choose(flag, payload) { return forward(flag, payload); }
function guarded(flag, payload) {
    var mark = 0;
    try {
        mark = choose(false, payload);
        mark = choose(flag, payload);
    } catch (value) { return mark + value; }
    return mark;
}
var caught52 = guarded(true, 20);
var normal20 = guarded(false, 20);
function sibling(payload) { return leaf(false, payload); }
)js";
    // `sibling` is never called, so `leaf`'s normal return through it is
    // unknown. (It does not use the top level's `this`: --ctjs-resolve-globals
    // treats that as the global object, and passing it to leaf would leave
    // every name unresolved.) Its effects and constant throw payload are still
    // proved, but its result is not the saved state of the first protected
    // call. Only that call's own chain of arguments may prove the catch
    // arithmetic's operand is primitive.
    testSource(context, "transitive selected actuals", source, 2, 20, 32,
               ExceptionRecoveryMode::EffectCheckedInvocations);
    auto module = import(context, source);
    if (!module) { return; }
    auto function = guarded(*module);
    auto prior = recoverPrimitiveExceptionRegion(function, 100000,
                                                 ExceptionRecoveryMode::EffectCheckedInvocations);
    if (!check(prior.recovered, "selected actuals first prove independently of other returns")) {
        return;
    }
    restore(function, prior);
    auto entry = *module->getOps<ctjs::FuncOp>().begin();
    bool changed = false;
    entry.walk([&](ctjs::CallDirectOp call) {
        if (!changed && call.getCallee() == function.getSymName()) {
            call->setOperand(4, entry.getBody().front().getArgument(0));
            changed = true;
        }
    });
    check(changed, "selected-actual mutation finds a current root invocation");
    function->setAttr("ctnative.completions_proved", mlir::UnitAttr::get(&context));
    const auto before = printed(*module);
    auto refused = recoverPrimitiveExceptionRegion(function, 100000,
                                                   ExceptionRecoveryMode::EffectCheckedInvocations);
    check(!refused.recovered && !refused.refusal.empty() && printed(*module) == before,
          "one unknown selected actual cannot borrow a sibling caller's primitive result");
}

/// Checks effect-checked recovery on effectFixture: an ordinary ctjs.call is
/// refused, the fixture proves with an exact budget, and thirteen mutations
/// that add a possibly throwing or observable operation must refuse.
void testEffects(mlir::MLIRContext & context) {
    {
        auto source = effectFixture.str();
        constexpr llvm::StringLiteral direct = "ctjs.call_direct @leaf(%r, %nt, %c)";
        replaceOnce(source, direct, "ctjs.call %c(%r)");
        auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
        if (!check(static_cast<bool>(module), "ordinary call effect fixture parses")) { return; }
        auto function = guarded(*module);
        const auto before = printed(*module);
        auto result = recoverPrimitiveExceptionRegion(
            function, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
        check(!result.recovered && result.refusal.find("`ctjs.call`") != std::string::npos &&
                  printed(*module) == before,
              "an ordinary call without global loads still needs an independent effect proof");
    }
    auto module = mlir::parseSourceString<mlir::ModuleOp>(effectFixture, &context);
    if (!check(static_cast<bool>(module), "closed primitive effect fixture parses")) { return; }
    auto function = guarded(*module);
    const auto before = printed(function);
    const unsigned checks = countChecks(function);
    auto result = recoverPrimitiveExceptionRegion(function, 100000,
                                                  ExceptionRecoveryMode::EffectCheckedInvocations);
    if (!check(result.recovered, "live primitive status and continuation effects discharge")) {
        llvm::errs() << result.refusal << '\n';
        return;
    }
    check(mlir::succeeded(mlir::verify(*module)), "effect-checked invocation IR verifies");
    check(checks == 2 && countChecks(function) == 0 && countChecks(*result.original) == checks,
          "a proved arithmetic check disappears only with the complete recovery transaction");
    unsigned calls = 0;
    function.walk([&](ctjs::InvokeOp) { ++calls; });
    check(calls == 1, "the throwing call keeps its separate invocation completion");
    const auto firstSteps = result.steps;
    restore(function, result);
    check(printed(function) == before,
          "effect-checked rollback restores exact source and attributes");

    // Every incomplete budget must fail before the result is adopted,
    // including cutoffs inside the predecessor walks, the effect scan and
    // structuring.
    if (!checkBudgets(
            function, firstSteps, ExceptionRecoveryMode::EffectCheckedInvocations,
            [&] { return printed(function) == before && countChecks(function) == checks; },
            "budget ", "every incomplete effect/recovery budget preserves the original graph",
            "the first complete effect/recovery budget succeeds exactly")) {
        return;
    }
    check(printed(function) == before, "exact-budget rollback permits a fresh proof");
    llvm::outs() << "effect recovery: " << firstSteps
                 << " steps, every incomplete budget preserves " << checks << " checks\n";

    // 0, 3, 4 add an unknown global load before the arithmetic, on the normal
    // path or in the catch; 9 adds one with forged nothrow/type attributes.
    // 1 allocates an object, 2 publishes a value to a global, 5 and 6 feed an
    // unknown value into the arithmetic, 7 converts undefined to an object
    // (which throws), 8 turns the add into a concat, 10 reads a property of an
    // unknown value, 11 duplicates the call, and 12 coerces an unknown value
    // to a number.
    for (unsigned mutation = 0; mutation != 13; ++mutation) {
        auto current = mlir::parseSourceString<mlir::ModuleOp>(effectFixture, &context);
        if (!current) { return; }
        auto candidate = guarded(*current);
        // Prove and roll back before every mutation: neither that earlier
        // success nor forged nothrow/type attributes may count as proof for
        // the mutated IR.
        auto prior = recoverPrimitiveExceptionRegion(
            candidate, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
        if (!check(prior.recovered, "mutation fixture first proves unchanged effects")) { return; }
        restore(candidate, prior);
        auto & entry = candidate.getBody().front();
        ctjs::BinaryOp arithmetic, caught;
        mlir::cf::CondBranchOp branch;
        ctjs::PopHandlerOp normal;
        candidate.walk([&](ctjs::BinaryOp binary) {
            if (!arithmetic) {
                arithmetic = binary;
            } else {
                caught = binary;
            }
        });
        candidate.walk([&](mlir::cf::CondBranchOp found) { branch = found; });
        candidate.walk([&](ctjs::PopHandlerOp found) { normal = found; });
        mlir::OpBuilder at(arithmetic);
        const auto where = arithmetic.getLoc();
        const auto unknown = entry.getArgument(3);
        const auto type = ctjs::ValueType::get(&context);
        static const llvm::DenseSet<unsigned> globalLoadMutations{0, 3, 4, 9};
        if (globalLoadMutations.contains(mutation)) {
            if (mutation == 3) { at.setInsertionPoint(normal); }
            if (mutation == 4) { at.setInsertionPoint(caught); }
            auto load = ctjs::LoadGlobalOp::create(at, where, "unprovedGetter");
            if (mutation == 9) {
                load->setAttr("ctnative.nothrow", at.getUnitAttr());
                load->setAttr("ctnative.type", at.getStringAttr("num<i32>"));
                candidate->setAttr("ctnative.nonthrowing", at.getUnitAttr());
            }
        } else if (mutation == 1) {
            mlir::OperationState allocation(where, "ctjs.create_object");
            allocation.addTypes(type);
            at.create(allocation);
        } else if (mutation == 2) {
            mlir::OperationState store(where, "ctjs.store_global");
            store.addAttribute("name", at.getStringAttr("published"));
            store.addOperands(arithmetic->getOperand(0));
            at.create(store);
        } else if (mutation == 5) {
            // Both edges go to the same block. Checking only the first edge's
            // operand would wrongly keep the value's primitive fact.
            branch->setOperand(1u + static_cast<unsigned>(branch.getTrueDestOperands().size()),
                               unknown);
        } else if (mutation == 6) {
            arithmetic->setOperand(1, unknown);
        } else if (mutation == 7) {
            auto absent =
                ctjs::ConstantOp::create(at, where, type, ctjs::UndefinedAttr::get(&context));
            ctjs::ConvertOp::create(
                at, where, type, ctjs::ConvertKindAttr::get(&context, ctjs::ConvertKind::ToObject),
                absent);
        } else if (mutation == 8) {
            arithmetic.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Concat));
            arithmetic->setOperand(1, unknown);
        } else if (mutation == 10) {
            auto key =
                ctjs::ConstantOp::create(at, where, type, ctjs::StringAttr::get(&context, "x"));
            ctjs::GetPropertyOp::create(at, where, type, unknown, key);
        } else if (mutation == 11) {
            at.setInsertionPoint(normal);
            at.clone(*firstCall(candidate).getOperation());
        } else {
            // Unary plus always produces a number, but coercing the input can
            // run valueOf or throw. A known result type does not make it safe.
            auto numeric = ctjs::UnaryOp::create(
                at, where, type, ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Plus),
                unknown);
            check(llvm::isa<ctcompile::ctnative::NumType>(
                      ctcompile::ctnative::staticResultType(numeric)),
                  "throwing coercion has an unconditional numeric normal-result fact");
        }
        const auto changed = printed(candidate);
        auto refused = recoverPrimitiveExceptionRegion(
            candidate, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
        check(!refused.recovered && refused.refusal.find("nonthrowing") != std::string::npos &&
                  printed(candidate) == changed && countChecks(candidate) == checks,
              "live status/continuation effect mutation preserves all original edges");
    }
}

/// A function whose try is installed after an early return and a fallible
/// global load, so only the tail of the function can be recovered. Its catch
/// compares the saved state with the payload. 4607182418800017408 is 1.0.
constexpr llvm::StringLiteral guardedTailFixture = R"mlir(
module {
  ctjs.func @guarded$tail(%r: !ctjs.value, %nt: !ctjs.value,
                         %c: !ctjs.value, %flag: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32, ctjs.not_structured = "preserved"} {
    %frame = ctjs.frame_enter 2
    %zero = ctjs.constant #ctjs.number<0>
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %earlyCondition = ctjs.truthy %flag
    cf.cond_br %earlyCondition, ^early(%zero, %one : !ctjs.value, !ctjs.value),
      ^prepare(%zero, %one : !ctjs.value, !ctjs.value)
  ^early(%earlyState: !ctjs.value, %earlyPayload: !ctjs.value):
    ctjs.frame_exit %frame
    ctjs.return %earlyState
  ^prepare(%saved: !ctjs.value, %scratch: !ctjs.value):
    %prefix = ctjs.load_global "preTry"
    ctjs.check ^install(%saved, %prefix : !ctjs.value, !ctjs.value)
      caught ^uncaught(%saved, %scratch : !ctjs.value, !ctjs.value)
  ^uncaught(%uncaughtState: !ctjs.value, %uncaughtPayload: !ctjs.value):
    ctjs.throw %uncaughtPayload
  ^install(%mark: !ctjs.value, %value: !ctjs.value):
    %condition = ctjs.truthy %value
    ctjs.push_handler ^body(%mark, %value : !ctjs.value, !ctjs.value)
      catch ^handler(%mark, %value : !ctjs.value, !ctjs.value)
  ^body(%state: !ctjs.value, %payload: !ctjs.value):
    cf.cond_br %condition, ^throw(%state, %payload : !ctjs.value, !ctjs.value),
      ^normal(%state, %payload : !ctjs.value, !ctjs.value)
  ^throw(%throwState: !ctjs.value, %throwPayload: !ctjs.value):
    ctjs.throw %throwPayload
  ^normal(%normalState: !ctjs.value, %normalPayload: !ctjs.value):
    ctjs.pop_handler
    ctjs.frame_exit %frame
    ctjs.return %normalState
  ^handler(%before: !ctjs.value, %oldScratch: !ctjs.value):
    %pad, %thrown = ctjs.catch_land
    %same = ctjs.compare strict_eq %before, %oldScratch
    %caught = ctjs.compare strict_eq %same, %thrown
    ctjs.frame_exit %frame
    ctjs.return %caught
  }
}
)mlir";

/// Prints every operation before the try is installed, without the function's
/// attributes. Recovery must keep this prefix's block order, SSA uses and both
/// check successors unchanged.
std::string guardedPrefix(ctjs::FuncOp function) {
    std::string text;
    llvm::raw_string_ostream into(text);
    for (auto & block : function.getBody()) {
        for (auto & operation : block) {
            if (llvm::isa<ctjs::PushHandlerOp, ctjs::TryOp>(operation)) { return text; }
            operation.print(into);
            into << '\n';
        }
    }
    return text;
}

/// Checks recovery of a try that starts after an unstructured prefix, in all
/// three modes, then eleven malformations of the prefix, handler or register
/// vectors that must refuse (except an unreachable block, which must not
/// prevent recovery).
void testGuardedTail(mlir::MLIRContext & context) {
    for (auto mode :
         {ExceptionRecoveryMode::ExplicitThrows, ExceptionRecoveryMode::CheckedInvocations,
          ExceptionRecoveryMode::EffectCheckedInvocations}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(guardedTailFixture, &context);
        if (!check(static_cast<bool>(module), "guarded-tail fixture parses")) { return; }
        auto function = guarded(*module);
        const auto before = printed(*module);
        const auto prefix = guardedPrefix(function);
        mlir::OwningOpRef<ctjs::FuncOp> detached(llvm::cast<ctjs::FuncOp>(function->clone()));
        const auto snapshot = printed(*detached);
        auto result = recoverPrimitiveExceptionRegion(function, 100000, mode);
        if (!check(result.recovered, "non-entry handler recovers its guarded tail")) {
            llvm::errs() << result.refusal << '\n';
            return;
        }
        check(mlir::succeeded(mlir::verify(*module)), "guarded-tail completion verifies");
        check(result.original && printed(*result.original) == snapshot,
              "adoption retains the complete source function and diagnostic in its snapshot");
        check(guardedPrefix(function) == prefix,
              "early return, fallible prefix, status edges and uncaught throw remain verbatim");
        check(function.getBody().getBlocks().size() == 5,
              "only collected normal and catch tail blocks are erased");
        check(function->getAttrOfType<mlir::StringAttr>("ctjs.not_structured") ==
                  mlir::StringAttr::get(&context, "preserved"),
              "partial recovery retains the original unstructured-prefix diagnostic");
        ctjs::TryOp attempt;
        ctjs::CheckOp status;
        ctjs::LoadGlobalOp load;
        unsigned throws = 0;
        function.walk([&](ctjs::TryOp found) { attempt = found; });
        function.walk([&](ctjs::CheckOp found) { status = found; });
        function.walk([&](ctjs::LoadGlobalOp found) { load = found; });
        function.walk([&](ctjs::ThrowOp thrown) {
            ++throws;
            check(thrown->getParentRegion() == &function.getBody(),
                  "pre-try throw remains outside both recovered regions");
        });
        if (!check(attempt && status && load && throws == 1 && countChecks(function) == 1,
                   "guarded-tail recovery retains the sole prefix failure edge")) {
            return;
        }
        check(attempt->getBlock() == &function.getBody().back() &&
                  attempt->getBlock() != &function.getBody().front() &&
                  status.getCont() == attempt->getBlock() &&
                  llvm::isa<ctjs::ThrowOp>(status.getHandler()->getTerminator()) &&
                  load->getParentRegion() == &function.getBody(),
              "try stays at its original installation site after the pre-try check");
        auto returned = llvm::dyn_cast<ctjs::ReturnOp>(attempt->getBlock()->getTerminator());
        check(returned && returned.getValue() == attempt.getResult() &&
                  llvm::isa_and_nonnull<ctjs::FrameExitOp>(returned->getPrevNode()),
              "the recovered tail exits its frame and returns at the installation site");
        check(attempt.getCatchBody().front().getNumArguments() == 3,
              "catch keeps payload and both live saved register slots");
        const auto complete = result.steps;
        restore(function, result);
        check(printed(*module) == before, "guarded-tail rollback restores the complete source");
        if (!checkBudgets(
                function, complete, mode, [&] { return printed(*module) == before; }, "budget ",
                "every incomplete prefix/tail budget preserves source and status edges",
                "the complete prefix/tail budget succeeds exactly")) {
            return;
        }
        check(printed(*module) == before, "exact-budget tail recovery rolls back verbatim");
        llvm::outs() << "guarded tail: " << complete
                     << " steps, all incomplete budgets preserve prefix and suffix, mode="
                     << static_cast<unsigned>(mode) << '\n';
    }
    {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(guardedTailFixture, &context);
        if (!module) { return; }
        auto function = guarded(*module);
        function->removeAttr("ctjs.not_structured");
        const auto before = printed(*module);
        auto result = recoverPrimitiveExceptionRegion(function);
        if (!check(result.recovered && function->hasAttr("ctjs.not_structured"),
                   "partial recovery records a missing unstructured-prefix diagnostic")) {
            return;
        }
        restore(function, result);
        check(printed(*module) == before, "rollback removes only the synthesized diagnostic");
    }
    {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(guardedTailFixture, &context);
        if (!module) { return; }
        auto function = guarded(*module);
        function->removeAttr("ctjs.not_structured");
        const auto source = printed(function);
        mlir::PassManager passes(&context);
        passes.addPass(ctnative::createCTNativeLowerToEmitC());
        if (!check(mlir::succeeded(passes.run(*module)), "partial native recovery can refuse")) {
            return;
        }
        check(function->hasAttr("ctnative.not_native"), "an unproved prefix remains non-native");
        function.walk([](mlir::Operation * operation) {
            ctnative::removeAttrsWithPrefix(operation, "ctnative.");
        });
        check(printed(function) == source,
              "native admission rollback restores a source without a diagnostic marker");
    }
    // 0 makes the check continue to itself, 1 jumps from the entry into the try
    // body, 2 jumps from the body back to the prefix, 3 and 4 add an extra
    // pop_handler, 5 changes a push_handler operand, 6 changes the frame's
    // register count, 7 drops a continuation operand, 8 adds an unreachable
    // block, 9 makes the body loop to itself, and 10 passes the frame token
    // as a register.
    for (unsigned mutation = 0; mutation != 11; ++mutation) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(guardedTailFixture, &context);
        if (!module) { return; }
        auto function = guarded(*module);
        auto prior = recoverPrimitiveExceptionRegion(function);
        if (!check(prior.recovered, "tail mutation first proves the unchanged source")) { return; }
        restore(function, prior);
        ctjs::PushHandlerOp push;
        ctjs::CheckOp status;
        ctjs::PopHandlerOp pop;
        ctjs::FrameEnterOp frame;
        function.walk([&](ctjs::PushHandlerOp found) { push = found; });
        function.walk([&](ctjs::CheckOp found) { status = found; });
        function.walk([&](ctjs::PopHandlerOp found) { pop = found; });
        function.walk([&](ctjs::FrameEnterOp found) { frame = found; });
        auto entry = llvm::cast<mlir::cf::CondBranchOp>(function.getBody().front().getTerminator());
        auto body = llvm::cast<mlir::cf::CondBranchOp>(push.getBody()->getTerminator());
        mlir::OpBuilder at(status);
        if (mutation == 0) {
            status->setSuccessor(status->getBlock(), 0);
        } else if (mutation == 1) {
            entry->setSuccessor(push.getBody(), 0);
        } else if (mutation == 2) {
            body->setSuccessor(status->getBlock(), 0);
        } else if (mutation == 3) {
            ctjs::PopHandlerOp::create(at, status.getLoc());
        } else if (mutation == 4) {
            at.setInsertionPoint(pop);
            ctjs::PopHandlerOp::create(at, pop.getLoc());
        } else if (mutation == 5) {
            push->setOperand(2, function.getBody().front().getArgument(0));
        } else if (mutation == 6) {
            frame->setAttr("reg_count", at.getI32IntegerAttr(3));
        } else if (mutation == 7) {
            status.getContOperandsMutable().erase(0);
        } else if (mutation == 8) {
            auto * unreachable = new mlir::Block;
            function.getBody().push_back(unreachable);
            unreachable->addArgument(ctjs::ValueType::get(&context), push.getLoc());
            unreachable->addArgument(ctjs::ValueType::get(&context), push.getLoc());
            at.setInsertionPointToEnd(unreachable);
            ctjs::ReturnOp::create(at, push.getLoc(), unreachable->getArgument(0));
        } else if (mutation == 9) {
            body->setSuccessor(push.getBody(), 0);
        } else {
            status->setOperand(0, frame.getResult());
        }
        function->setAttr("ctnative.completions_proved", at.getUnitAttr());
        const auto changed = printed(*module);
        for (auto mode :
             {ExceptionRecoveryMode::ExplicitThrows, ExceptionRecoveryMode::CheckedInvocations,
              ExceptionRecoveryMode::EffectCheckedInvocations}) {
            auto refused = recoverPrimitiveExceptionRegion(function, 100000, mode);
            if (mutation == 8) {
                if (!check(refused.recovered && mlir::succeeded(mlir::verify(*module)),
                           "a source block proved unreachable needs no executable clone")) {
                    return;
                }
                restore(function, refused);
                check(printed(*module) == changed,
                      "unreachable source remains in the complete rollback snapshot");
                continue;
            }
            if (!check(!refused.recovered && !refused.refusal.empty() &&
                           printed(*module) == changed,
                       "malformed prefix, suffix or register vectors refuse atomically")) {
                llvm::errs() << "mutation " << mutation << ": " << refused.refusal << '\n';
            }
        }
    }
}

} // namespace ctcompile::test::exception_recovery
