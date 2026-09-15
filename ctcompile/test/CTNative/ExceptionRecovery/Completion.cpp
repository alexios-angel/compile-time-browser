#include "Tests.h"

namespace ctcompile::test::exception_recovery {

int failures = 0;

bool check(bool condition, llvm::StringRef label) {
    if (!condition) {
        llvm::errs() << "FAIL " << label << '\n';
        ++failures;
    }
    return condition;
}

std::string printed(mlir::Operation * operation) {
    std::string text;
    llvm::raw_string_ostream stream(text);
    operation->print(stream);
    return text;
}

ctjs::FuncOp guarded(mlir::ModuleOp module) {
    for (auto function : module.getOps<ctjs::FuncOp>()) {
        if (function.getSymName().starts_with("guarded$")) { return function; }
    }
    return {};
}

ctjs::CallDirectOp firstCall(ctjs::FuncOp function) {
    ctjs::CallDirectOp found;
    function.walk([&](ctjs::CallDirectOp call) {
        if (!found) { found = call; }
    });
    return found;
}

unsigned countChecks(ctjs::FuncOp function) {
    unsigned count = 0;
    function.walk([&](ctjs::CheckOp) { ++count; });
    return count;
}

mlir::OwningOpRef<mlir::ModuleOp> import(mlir::MLIRContext & context, llvm::StringRef source,
                                         bool resolve) {
    auto program = ctbrowser::script::compiler::compile(source.str());
    if (!check(program.ok, "source compiles")) { return {}; }
    auto imported = ctcompile::js::import_program(program, "invocation-recovery", &context);
    if (!check(static_cast<bool>(imported.module), "source imports")) { return {}; }
    if (resolve) {
        mlir::PassManager passes(&context);
        passes.addPass(ctjs::createCTJSResolveGlobals());
        passes.addPass(ctjs::createCTJSLiftToSCF());
        if (!check(mlir::succeeded(passes.run(*imported.module)), "source prepares")) { return {}; }
    }
    return std::move(imported.module);
}

// This checks the selected completion's SSA wiring. It supplies fixed helper
// outcomes, independently of helper implementation/type inference: every normal
// call returns 20; one selected call throws the fixture's known payload. Poison
// is never a number and cannot satisfy a selected completion observation.
struct completionTrace {
    ctjs::InvokeOp failing;
    double payload;
    unsigned remaining = 4096;

    std::optional<double> value(mlir::Value value) {
        if (remaining == 0) { return {}; }
        --remaining;
        if (auto literal = value.getDefiningOp<ctjs::ConstantOp>()) {
            auto attr = literal->getAttr("value");
            if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(attr)) {
                return std::bit_cast<double>(number.getBits());
            }
            if (auto boolean = llvm::dyn_cast<ctjs::BooleanAttr>(attr)) {
                return boolean.getValue() ? 1 : 0;
            }
        }
        if (auto literal = value.getDefiningOp<mlir::arith::ConstantOp>()) {
            if (auto integer = llvm::dyn_cast<mlir::IntegerAttr>(literal.getValue())) {
                if (integer.getType().isInteger(1)) { return integer.getValue().isOne() ? 1 : 0; }
                return static_cast<double>(integer.getInt());
            }
        }
        if (auto truthy = value.getDefiningOp<ctjs::TruthyOp>()) {
            return this->value(truthy->getOperand(0));
        }
        if (auto comparison = value.getDefiningOp<mlir::arith::CmpIOp>()) {
            auto left = this->value(comparison.getLhs());
            auto right = this->value(comparison.getRhs());
            if (left && right && comparison.getPredicate() == mlir::arith::CmpIPredicate::eq) {
                return *left == *right ? 1 : 0;
            }
            return {};
        }
        if (llvm::isa_and_nonnull<ctjs::FromBoolOp, mlir::arith::IndexCastUIOp,
                                  mlir::arith::ExtUIOp, mlir::arith::TruncIOp>(
                value.getDefiningOp())) {
            return this->value(value.getDefiningOp()->getOperand(0));
        }
        if (auto invocation = value.getDefiningOp<ctjs::InvokeOp>()) {
            const unsigned index = llvm::cast<mlir::OpResult>(value).getResultNumber();
            const bool failed = invocation == failing;
            if (index == 0) { return failed ? 1 : 0; }
            if (index == 1) { return failed ? std::optional<double>{} : 20; }
            if (!failed) { return {}; }
            if (index == 2) { return payload; }
            auto exit =
                llvm::cast<ctjs::InvokeExitOp>(invocation.getBody().front().getTerminator());
            return this->value(exit.getState()[index - 3]);
        }
        if (auto branch = value.getDefiningOp<mlir::scf::IfOp>()) {
            auto condition = this->value(branch.getCondition());
            if (!condition) { return {}; }
            auto & region = *condition != 0 ? branch.getThenRegion() : branch.getElseRegion();
            return this->value(region.front().getTerminator()->getOperand(
                llvm::cast<mlir::OpResult>(value).getResultNumber()));
        }
        return {};
    }
};

void checkCompletion(ctjs::TryOp attempt, ctjs::InvokeOp failing, double payload,
                     double expectedState) {
    auto exit = llvm::cast<ctjs::TryExitOp>(attempt.getBody().front().getTerminator());
    completionTrace trace{failing, payload};
    check(trace.value(exit.getIsThrow()) == (failing ? 1 : 0),
          "enclosing try selects call completion");
    if (!failing) {
        check(trace.value(exit.getNormalResult()) == 20, "normal return publishes the call result");
        return;
    }
    auto & handler = attempt.getCatchBody().front();
    ctjs::BinaryOp add;
    for (auto binary : handler.getOps<ctjs::BinaryOp>()) { add = binary; }
    if (!check(static_cast<bool>(add), "fixture catch keeps its state-plus-payload addition")) {
        return;
    }
    auto saved = llvm::dyn_cast<mlir::BlockArgument>(add->getOperand(0));
    auto thrown = llvm::dyn_cast<mlir::BlockArgument>(add->getOperand(1));
    if (!check(saved && thrown && saved.getOwner() == &handler && thrown.getArgNumber() == 0,
               "catch reads its own payload and saved state")) {
        return;
    }
    check(trace.value(exit.getCaughtValues()[saved.getArgNumber()]) == expectedState,
          "catch receives state immediately before the failed call");
    check(trace.value(exit.getCaughtValues()[0]) == payload,
          "catch receives the invocation's thrown payload");
}

void testSource(mlir::MLIRContext & context, llvm::StringRef name, llvm::StringRef source,
                unsigned expectedCalls, double saved, double payload, ExceptionRecoveryMode mode,
                bool resolve) {
    auto module = import(context, source, resolve);
    if (!module) { return; }
    auto function = guarded(*module);
    if (!check(static_cast<bool>(function), "guarded source function survives")) { return; }
    const auto original = printed(function);
    mlir::OwningOpRef<ctjs::FuncOp> detached(llvm::cast<ctjs::FuncOp>(function->clone()));
    const auto snapshot = printed(*detached);
    const auto checks = countChecks(function);
    // Successful recovery keeps these original values alive in its rollback
    // snapshot. Calls also carry the importer's literal undefined receiver.
    llvm::SmallVector<llvm::SmallVector<mlir::Value>> callInputs;
    function.walk([&](ctjs::CallOp call) { callInputs.emplace_back(call->getOperands()); });
    auto ordinary = recoverPrimitiveExceptionRegion(function);
    check(!ordinary.recovered &&
              ordinary.refusal ==
                  "native try/catch needs an explicit throw in its active handler" &&
              printed(function) == original,
          "ordinary native recovery keeps its exact throwing-call refusal and source graph");

    auto recovered = recoverPrimitiveExceptionRegion(function, 100000, mode);
    if (!check(recovered.recovered, ("checked invocation recovery succeeds for " + name).str())) {
        llvm::errs() << recovered.refusal << '\n';
        return;
    }
    check(mlir::succeeded(mlir::verify(*module)), "recovered completion IR verifies");
    check(recovered.original && printed(*recovered.original) == snapshot &&
              countChecks(*recovered.original) == checks && checks > expectedCalls,
          "rollback retains every original call and non-call status edge");
    llvm::SmallVector<ctjs::InvokeOp> invocations;
    ctjs::TryOp attempt;
    function.walk<mlir::WalkOrder::PreOrder>(
        [&](ctjs::InvokeOp invocation) { invocations.push_back(invocation); });
    function.walk([&](ctjs::TryOp found) { attempt = found; });
    if (!check(invocations.size() == expectedCalls && attempt,
               "one invocation per source call reaches the enclosing try")) {
        return;
    }
    for (auto [index, invocation] : llvm::enumerate(invocations)) {
        auto * call = &invocation.getBody().front().front();
        auto dispatch = llvm::cast<ctjs::InvokeExitOp>(invocation.getBody().front().back());
        check(resolve ? llvm::isa<ctjs::CallDirectOp>(call) : llvm::isa<ctjs::CallOp>(call),
              "recovery preserves the original call kind without inventing a direct target");
        check(call->getResult(0).hasOneUse() && dispatch.getNormalResult() == call->getResult(0),
              "call result has only its normal dispatch use");
        if (!resolve &&
            check(index < callInputs.size() && callInputs[index].size() == call->getNumOperands(),
                  "ordinary invocation preserves every original call operand")) {
            for (auto [operand, original] : llvm::zip(call->getOperands(), callInputs[index])) {
                if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(original)) {
                    const unsigned slot = argument.getArgNumber();
                    check(slot < dispatch.getState().size() && operand == dispatch.getState()[slot],
                          "callee, receiver and arguments retain exact pre-call register slots");
                } else {
                    auto literal = original.getDefiningOp<ctjs::ConstantOp>();
                    auto copied = operand.getDefiningOp<ctjs::ConstantOp>();
                    check(literal && copied && literal.getValue() == copied.getValue(),
                          "ordinary invocation preserves the original literal operand");
                }
            }
        }
        check(llvm::all_of(dispatch.getState(),
                           [&](mlir::Value state) {
                               return !invocation.getBody().isAncestor(state.getParentRegion());
                           }),
              "every saved register is defined before the call body");
        auto normal = llvm::cast<ctjs::InvokeYieldOp>(invocation.getNormalBody().front().back());
        auto unwind = llvm::cast<ctjs::InvokeYieldOp>(invocation.getUnwindBody().front().back());
        check(
            normal.getValues()[1] == invocation.getNormalBody().front().getArgument(0) &&
                llvm::isa_and_nonnull<mlir::ub::PoisonOp>(unwind.getValues()[1].getDefiningOp()) &&
                llvm::equal(unwind.getValues().drop_front(2),
                            invocation.getUnwindBody().front().getArguments()),
            "normal publication and unwind payload/state stay separate");
    }
    checkCompletion(attempt, invocations.back(), payload, saved);
    checkCompletion(attempt, {}, payload, 0);
    if (expectedCalls == 2) { checkCompletion(attempt, invocations.front(), payload, 0); }
    // The first argument-free handler cannot be recovered a second time. A
    // failed rerun must not consume its existing continuations.
    const auto structured = printed(function);
    auto rerun = recoverPrimitiveExceptionRegion(function, 100000, mode);
    check(!rerun.recovered && printed(function) == structured,
          "recovery rerun preserves the existing invocation graph");

    // Restore exactly as the native transaction does after admission refuses.
    function.getBody().takeBody(recovered.original->getBody());
    function->setAttrs((*recovered.original)->getAttrs());
    check(printed(function) == original, "source rollback is byte-identical");
    for (unsigned budget : {0u, 1u, 64u, 256u}) {
        auto limited = recoverPrimitiveExceptionRegion(function, budget, mode);
        check(!limited.recovered && limited.refusal.find("budget exhausted") != std::string::npos &&
                  printed(function) == original,
              "bounded recovery leaves all original status edges on failure");
    }
    if (!resolve) {
        for (unsigned budget = 0; budget < recovered.steps; ++budget) {
            auto limited = recoverPrimitiveExceptionRegion(function, budget, mode);
            if (!check(!limited.recovered &&
                           limited.refusal.find("budget exhausted") != std::string::npos &&
                           printed(function) == original && countChecks(function) == checks,
                       "every incomplete ordinary-call budget preserves source and checks")) {
                llvm::errs() << "budget " << budget << ": " << limited.refusal << '\n';
                return;
            }
        }
        auto exact = recoverPrimitiveExceptionRegion(function, recovered.steps, mode);
        if (!check(exact.recovered && exact.steps == recovered.steps,
                   "the complete ordinary-call budget succeeds exactly")) {
            return;
        }
        function.getBody().takeBody(exact.original->getBody());
        function->setAttrs((*exact.original)->getAttrs());
        check(printed(function) == original,
              "exact-budget ordinary call rollback is byte-identical");
    }
    llvm::outs() << name << ": " << expectedCalls << " checked invocation(s), " << checks
                 << " original checks retained for admission rollback, " << recovered.steps
                 << " steps, effects=" << (mode == ExceptionRecoveryMode::EffectCheckedInvocations)
                 << '\n';
}

void checkCompletionTypes(mlir::ModuleOp module, ctjs::FuncOp function, llvm::StringRef normalType,
                          llvm::StringRef stateType,
                          llvm::StringRef payloadType = "!ctnative.boxed") {
    mlir::DataFlowSolver solver;
    solver.load<mlir::dataflow::DeadCodeAnalysis>();
    solver.load<mlir::dataflow::SparseConstantPropagation>();
    solver.load<ctcompile::ctnative::TypeInference>();
    const auto before = printed(module);
    if (!check(mlir::succeeded(solver.initializeAndRun(module)),
               "recovered source completion inference converges")) {
        return;
    }
    const auto expect = [&](mlir::Value value, llvm::StringRef wanted, llvm::StringRef label) {
        const auto * lattice = solver.lookupState<ctcompile::ctnative::TypeLattice>(value);
        std::string type;
        if (lattice) {
            llvm::raw_string_ostream into(type);
            lattice->getValue().print(into);
        }
        if (!check(type == wanted, label)) {
            llvm::errs() << "  wanted " << wanted << ", found " << type << '\n';
        }
    };
    unsigned invokes = 0, states = 0;
    function.walk([&](ctjs::InvokeOp invocation) {
        ++invokes;
        auto * call = &invocation.getBody().front().front();
        expect(call->getResult(0), normalType, "successful source call has its own normal type");
        expect(invocation.getNormalBody().front().getArgument(0), normalType,
               "normal continuation receives only the successful returned value");
        expect(invocation.getUnwindBody().front().getArgument(0), payloadType,
               "unwind inference preserves the imported frame's failure alternative");
    });
    function.walk([&](ctjs::TryOp attempt) {
        auto & caught = attempt.getCatchBody().front();
        expect(caught.getArgument(0), payloadType,
               "enclosing catch cannot narrow an unproved frame failure");
        for (auto saved : caught.getArguments().drop_front()) {
            ++states;
            expect(saved, stateType, "pre-call catch state is independent of the payload type");
        }
    });
    check(invokes != 0 && states != 0, "source type witness includes invocation and saved state");
    check(printed(module) == before, "completion inference does not change source operations");
}

void testOrdinaryCompletionTypes(mlir::MLIRContext & context) {
    auto module = import(context, R"js(
function guarded(text) {
    if (text === "skip") { return "prefix"; }
    var mark = "before";
    try { mark = decodeURIComponent(text); }
    catch (ignored) { return mark; }
    return mark;
}
)js",
                         false);
    if (!module) { return; }
    auto function = guarded(*module);
    const auto before = printed(*module);
    const auto prefix = guardedPrefix(function);
    auto result = recoverPrimitiveExceptionRegion(function, 100000,
                                                  ExceptionRecoveryMode::CheckedInvocations);
    if (!check(result.recovered, "original URI call recovers structurally with unused payload")) {
        llvm::errs() << result.refusal << '\n';
        return;
    }
    check(mlir::succeeded(mlir::verify(*module)), "ordinary URI invocation IR verifies");
    check(guardedPrefix(function) == prefix && function->hasAttr("ctjs.not_structured"),
          "original URI guard and early return remain outside the recovered try");
    function.walk([&](ctjs::TryOp attempt) {
        auto payload = attempt.getCatchBody().front().getArgument(0);
        check(llvm::all_of(
                  payload.getUsers(),
                  [](mlir::Operation * operation) { return llvm::isa<ctjs::RootOp>(operation); }),
              "unused URI payload remains represented without inventing a primitive value");
    });
    checkCompletionTypes(*module, function, "!ctnative.boxed", "!ctnative.str<utf8>");
    function.getBody().takeBody(result.original->getBody());
    function->setAttrs((*result.original)->getAttrs());
    check(printed(*module) == before, "URI structural rollback restores the complete source");
    auto effects = recoverPrimitiveExceptionRegion(function, 100000,
                                                   ExceptionRecoveryMode::EffectCheckedInvocations);
    check(!effects.recovered && printed(*module) == before,
          "an unused URI payload supplies no host identity or effect proof");
}

void testSourceCompletionTypes(mlir::MLIRContext & context, llvm::StringRef name,
                               llvm::StringRef source, llvm::StringRef normalType,
                               llvm::StringRef stateType) {
    auto module = import(context, source);
    if (!module) { return; }
    auto function = guarded(*module);
    const auto original = printed(*module);
    auto recovered = recoverPrimitiveExceptionRegion(
        function, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
    if (!check(recovered.recovered, "source type witness passes independent effect recovery")) {
        llvm::errs() << recovered.refusal << '\n';
        return;
    }
    checkCompletionTypes(*module, function, normalType, stateType);
    checkCompletionTypes(*module, function, normalType, stateType);
    function.getBody().takeBody(recovered.original->getBody());
    function->setAttrs((*recovered.original)->getAttrs());
    check(printed(*module) == original, "type inference retains exact source rollback");
    auto ordinary = recoverPrimitiveExceptionRegion(function);
    check(!ordinary.recovered && printed(*module) == original,
          "normal type precision does not admit a native throwing source call");
    llvm::outs() << name << ": source normal/state types " << normalType << "/" << stateType
                 << ", unwind remains boxed across frame_enter\n";
}

void testSourceCompletionMutations(mlir::MLIRContext & context, llvm::StringRef source) {
    for (unsigned mutation = 0; mutation != 10; ++mutation) {
        auto module = import(context, source);
        if (!module) { return; }
        auto function = guarded(*module);
        auto recovered = recoverPrimitiveExceptionRegion(
            function, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
        if (!check(recovered.recovered, "live type control starts with checked source recovery")) {
            return;
        }
        checkCompletionTypes(*module, function, "!ctnative.num<i32>", "!ctnative.num<i32>");
        auto call = firstCall(function);
        auto helper =
            mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, call.getCalleeAttr());
        ctjs::ReturnOp returned;
        ctjs::ThrowOp thrown;
        helper.walk([&](ctjs::ReturnOp operation) { returned = operation; });
        helper.walk([&](ctjs::ThrowOp operation) { thrown = operation; });
        if (!check(returned && thrown, "source type control retains both helper completions")) {
            return;
        }
        mlir::OpBuilder at(returned);
        const auto where = returned.getLoc();
        const auto type = ctjs::ValueType::get(&context);
        helper->setAttr("ctnative.nothrow", at.getUnitAttr());
        helper->setAttr("ctnative.exception_effects", at.getUnitAttr());
        call->setAttr("ctnative.type", at.getStringAttr("num<i32>"));
        llvm::StringRef expected = "!ctnative.boxed";
        llvm::StringRef payload = "!ctnative.boxed";
        llvm::SmallVector<mlir::Operation *> added;
        if (mutation < 3) {
            mlir::Attribute literal =
                mutation == 0   ? mlir::Attribute(ctjs::StringAttr::get(&context, "live"))
                : mutation == 1 ? mlir::Attribute(ctjs::BooleanAttr::get(&context, true))
                                : mlir::Attribute(ctjs::NumberAttr::get(
                                      &context, std::bit_cast<uint64_t>(-0.0)));
            auto replacement = ctjs::ConstantOp::create(at, where, type, literal);
            returned->setOperand(0, replacement);
            expected = mutation == 0   ? "!ctnative.str<utf8>"
                       : mutation == 1 ? "!ctnative.bool"
                                       : "!ctnative.num<f64>";
        } else if (mutation == 3) {
            at.setInsertionPoint(thrown);
            auto replacement = ctjs::ConstantOp::create(at, where, type,
                                                        ctjs::StringAttr::get(&context, "failure"));
            thrown->setOperand(0, replacement);
            expected = "!ctnative.num<i32>";
        } else if (mutation == 4) {
            added.push_back(ctjs::StoreGlobalOp::create(at, where, "late", returned.getValue()));
        } else if (mutation == 5) {
            auto key = ctjs::ConstantOp::create(at, where, type,
                                                ctjs::StringAttr::get(&context, "property"));
            ctjs::GetPropertyOp::create(at, where, type, helper.getBody().front().getArgument(0),
                                        key);
        } else if (mutation == 6) {
            auto arguments = helper.getBody().front().getArguments();
            ctjs::CallDirectOp::create(at, where, type, call.getCalleeAttr(), arguments[0],
                                       arguments[1], arguments[2], arguments.drop_front(3),
                                       mlir::ArrayAttr{}, mlir::ArrayAttr{});
        } else if (mutation == 7) {
            mlir::SymbolTable::setSymbolVisibility(helper, mlir::SymbolTable::Visibility::Public);
        } else if (mutation == 8) {
            unsigned operations = 0;
            helper.getBody().walk([&](mlir::Operation *) { ++operations; });
            if (!check(operations < 4096, "source completion leaves room for exact work limit")) {
                return;
            }
            for (unsigned index = operations; index != 4096; ++index) {
                added.push_back(ctjs::ConstantOp::create(at, where, type,
                                                         ctjs::BooleanAttr::get(&context, true)));
            }
            checkCompletionTypes(*module, function, "!ctnative.num<i32>", "!ctnative.num<i32>");
            added.push_back(
                ctjs::ConstantOp::create(at, where, type, ctjs::BooleanAttr::get(&context, true)));
        } else {
            // A test-only frame-free counterpart establishes that the boxed
            // payload above is a real frame-failure obligation. This erasure
            // is not a source transform or evidence that native may do it.
            llvm::SmallVector<mlir::Operation *> bookkeeping;
            helper.walk([&](mlir::Operation * operation) {
                if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(operation)) {
                    bookkeeping.push_back(operation);
                }
            });
            for (auto * operation : llvm::reverse(bookkeeping)) { operation->erase(); }
            expected = "!ctnative.num<i32>";
            payload = "!ctnative.num<i32>";
        }
        checkCompletionTypes(*module, function, expected, "!ctnative.num<i32>", payload);
        checkCompletionTypes(*module, function, expected, "!ctnative.num<i32>", payload);
        if (!added.empty()) {
            for (auto * operation : llvm::reverse(added)) { operation->erase(); }
            checkCompletionTypes(*module, function, "!ctnative.num<i32>", "!ctnative.num<i32>");
        }
    }
    llvm::outs() << "source completion types: ten live mutations, fresh solves and reruns\n";
}

} // namespace ctcompile::test::exception_recovery
