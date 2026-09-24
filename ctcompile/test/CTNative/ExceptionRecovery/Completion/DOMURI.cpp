#include "DOMURI.hpp"
#include "../../../../lib/CTNative/HostContract/Preparation.h"
#include "ClassTransactions.hpp"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "mlir/IR/Dominance.h"

#include "check.hpp"

namespace ctcompile::test::exception_recovery {

static void testObservedIteratorRecovery(mlir::MLIRContext & context) {
    const std::string getter = R"js(function customElements(anchor) {
  const values = {
    [Symbol.iterator]() { return this; },
    next() {
      const done = anchor.hasAttribute('data-yielded');
      anchor.setAttribute('data-next', done);
      anchor.setAttribute('data-yielded', 'yes');
      return {done: done, value: anchor};
    },
    get return() {
      anchor.setAttribute('data-closed', 'yes');
      throw anchor;
    }
  };
  try {
  for (const node of values) {
    return 1;
    if (anchor.hasAttribute('stop')) break;
  }
  } catch (error) { return error === anchor; }
  return anchor.hasAttribute('data-visited');
}
)js";
    for (bool method : {false, true}) {
        auto source = getter;
        if (method) { source.replace(source.find("get return()"), 12, "return()"); }
        auto module = import(context, source, false);
        if (!module) { continue; }
        auto function = module->lookupSymbol<ctjs::FuncOp>("customElements$1");
        if (!check(static_cast<bool>(function), "original iterator entry survives raw import")) {
            continue;
        }
        mlir::OwningOpRef<ctjs::FuncOp> original(llvm::cast<ctjs::FuncOp>(function->clone()));
        const auto before = printed(*original);
        const auto checks = countChecks(function);
        mlir::DominanceInfo dominance(function);
        llvm::SmallVector<ctjs::CallOp> originalCalls;
        function.walk([&](ctjs::CallOp call) {
            if (dominance.isReachableFromEntry(call->getBlock()) &&
                llvm::isa_and_nonnull<ctjs::CheckOp>(call->getNextNode())) {
                originalCalls.push_back(call);
            }
        });
        check(originalCalls.size() == 4 && checks == 33,
              "original iterator keeps four checked calls and every non-call check");
        auto recovered = recoverPrimitiveExceptionRegion(function, 100000,
                                                         ExceptionRecoveryMode::CheckedInvocations);
        if (!check(recovered.recovered, "original observing iterator retains checked calls")) {
            llvm::errs() << recovered.refusal << '\n';
            continue;
        }
        check(recovered.original && printed(*recovered.original) == before &&
                  countChecks(*recovered.original) == checks &&
                  mlir::succeeded(mlir::verify(*module)),
              "iterator recovery retains the complete original snapshot and verifies");
        unsigned calls = 0;
        function.walk([&](ctjs::InvokeOp invocation) {
            ++calls;
            auto call = llvm::cast<ctjs::CallOp>(invocation.getBody().front().front());
            auto dispatch = llvm::cast<ctjs::InvokeExitOp>(invocation.getBody().front().back());
            check(dispatch.getState().size() == 15,
                  "each iterator call retains its complete pre-call register vector");
            const auto source = llvm::find_if(originalCalls, [&](ctjs::CallOp originalCall) {
                return originalCall.getLoc() == call.getLoc();
            });
            if (!check(source != originalCalls.end(), "recovered call keeps its original site")) {
                return;
            }
            check(call->getNumOperands() == (*source)->getNumOperands(),
                  "iterator invocation retains every original argument");
            for (auto [value, input] : llvm::zip(call->getOperands(), (*source)->getOperands())) {
                if (auto slot = llvm::dyn_cast<mlir::BlockArgument>(input)) {
                    check(value == dispatch.getState()[slot.getArgNumber()],
                          "iterator callee, receiver and inputs use their original saved slots");
                } else {
                    auto literal = input.getDefiningOp<ctjs::ConstantOp>();
                    auto copied = value.getDefiningOp<ctjs::ConstantOp>();
                    check(literal && copied && literal.getValue() == copied.getValue(),
                          "iterator invocation keeps its original literal operands");
                }
            }
            originalCalls.erase(source);
            check(dispatch.getNormalResult() == call.getResult() &&
                      llvm::all_of(dispatch.getState(),
                                   [&](mlir::Value state) {
                                       return !invocation.getBody().isAncestor(
                                           state.getParentRegion());
                                   }),
                  "a failed iterator result cannot enter the pre-call snapshot");
            auto unwind =
                llvm::cast<ctjs::InvokeYieldOp>(invocation.getUnwindBody().front().back());
            check(llvm::equal(unwind.getValues().drop_front(2),
                              invocation.getUnwindBody().front().getArguments()),
                  "each iterator failure forwards its own payload and saved state together");
        });
        check(calls == 4 && originalCalls.empty(),
              "open, next and both normal closes have distinct completions");
        for (unsigned control = 0; control < 3; ++control) {
            mlir::OwningOpRef<ctjs::FuncOp> broken(llvm::cast<ctjs::FuncOp>(original->clone()));
            ctjs::CallOp call;
            broken->walk([&](ctjs::CallOp found) {
                if (!call && llvm::isa_and_nonnull<ctjs::CheckOp>(found->getNextNode())) {
                    call = found;
                }
            });
            auto checked = llvm::cast<ctjs::CheckOp>(call->getNextNode());
            if (control == 0) {
                checked->setOperand(static_cast<unsigned>(checked.getContOperands().size()),
                                    call.getResult());
            } else if (control == 1) {
                // Register zero is not this invocation's result scratch slot.
                checked->setOperand(0, checked.getHandlerOperands()[1]);
            }
            const auto snapshot = printed(*broken);
            auto refused = recoverPrimitiveExceptionRegion(
                *broken, control == 2 ? 0 : 100000, ExceptionRecoveryMode::CheckedInvocations);
            check(
                !refused.recovered && !refused.original && printed(*broken) == snapshot &&
                    countChecks(*broken) == checks &&
                    refused.refusal.find(control == 2 ? "budget" : "invocation") !=
                        std::string::npos,
                "bad iterator failure state, changed normal state and zero budget preserve source");
        }
        llvm::outs() << "observing iterator recovery: " << calls << " calls, " << checks
                     << " original checks, " << recovered.steps << " steps\n";
    }
}

void testDOMURITransaction(mlir::MLIRContext & context) {
    testObservedIteratorRecovery(context);
    for (unsigned control = 0; control < 40; ++control) {
        std::string source = "function guarded(element) { try { throw element; } "
                             "catch (error) { return error === element; } }";
        if (control == 1) {
            source.replace(source.find("error === element"), 17,
                           "error.hasAttribute('data-closed')");
        }
        if (control == 2) {
            source.insert(source.find("throw element"), "element.hasAttribute('data-before'); ");
        }
        if (control == 3) {
            source.replace(source.find("return error === element;"), 25, "throw error;");
        }
        if (control == 4) {
            source.replace(source.find("return error === element;"), 25, "return error;");
        }
        if (control == 5) { source.insert(source.find("throw element"), "if (element) "); }
        if (control >= 7) {
            source = "function guarded(element) { "
                     "const flag = element.hasAttribute('flag'); let saved = false; "
                     "try { if (flag) { saved = true; throw element; } "
                     "else { throw element; } } catch (error) { "
                     "return error === element && saved; } }";
            if (control == 8) {
                source.replace(source.find("error === element && saved"), 26,
                               "saved && error.hasAttribute('flag')");
            }
            if (control == 9) {
                source.insert(source.find("saved = true"), "element.hasAttribute('inside'); ");
            }
            if (control == 10) {
                source.replace(source.find("return error === element && saved;"), 34,
                               "throw error;");
            }
            if (control == 11) {
                source.replace(source.find("return error === element && saved;"), 34,
                               "return error;");
            }
        }
        if (control >= 12) {
            source = "function guarded(element) { "
                     "const flag = element.hasAttribute('flag'); let saved = false; "
                     "try { if (flag) { saved = true; throw element; } "
                     "else { saved = true; } } catch (error) { "
                     "return error === element && saved; } return !saved; }";
            if (control == 13) { source.replace(source.find("if (flag)"), 9, "if (!flag)"); }
            if (control == 14) {
                source.replace(source.find("throw element;"), 14,
                               "if (flag) { throw element; } return false;");
            }
            if (control == 15) {
                source.insert(source.find("else { saved") + 7, "element.hasAttribute('inside'); ");
            }
            if (control == 16) {
                source.replace(source.find("return error === element && saved;"), 34,
                               "throw error;");
            }
            if (control == 17) {
                source.replace(source.find("return error === element && saved;"), 34,
                               "return error;");
            }
            if (control == 18) {
                source.replace(source.find("return !saved;"), 14, "return element;");
            }
        }
        if (control >= 20) {
            source = "function guarded(element) { "
                     "const flag = element.hasAttribute('flag'); "
                     "const inner = element.hasAttribute('inner'); let saved = false; "
                     "try { if (flag) { saved = true; if (inner) { throw element; } } "
                     "else { saved = true; } } catch (error) { "
                     "return error === element && saved; } return !saved; }";
            if (control == 21) { source.replace(source.find("if (inner)"), 10, "if (!inner)"); }
            if (control == 22) {
                source.replace(source.find("error === element && saved"), 26,
                               "saved && error.hasAttribute('flag')");
            }
            if (control == 23) {
                source.insert(source.find("else { saved") + 7, "element.hasAttribute('inside'); ");
            }
            if (control == 24) {
                source.replace(source.find("return error === element && saved;"), 34,
                               "return error;");
            }
            if (control == 25) {
                source.replace(source.find("return error === element && saved;"), 34,
                               "throw error;");
            }
        }
        if (control >= 28) {
            source = "function guarded(element) { let saved = false; try { "
                     "saved = element.hasAttribute('flag'); if (saved) throw element; "
                     "} catch (error) { return error === element && saved; } return saved; }";
            if (control == 29) {
                source.replace(source.find("element.hasAttribute('flag')"), 28,
                               "({hasAttribute() { throw false; }}).hasAttribute('flag')");
            }
            if (control == 30) { source.replace(source.find("'flag'"), 6, "42"); }
            if (control == 31) {
                source.replace(source.find("element.hasAttribute('flag')"), 28,
                               "decodeURIComponent('%')");
            }
        }
        if (control >= 33) {
            source = "function guarded(element) { let saved = false; try { "
                     "saved = element.hasAttribute('flag'); return saved; "
                     "} catch (error) { return error === element && saved; } }";
            if (control == 34) {
                source.replace(source.find("return saved;"), 13,
                               "return saved && element.hasAttribute('second');");
            }
            if (control == 35) {
                source.replace(source.find("saved = element"), 15, "if (element) saved = element");
            }
            if (control == 36) { source.replace(source.find("'flag'"), 6, "42"); }
            if (control == 37) {
                source.replace(source.find("element.hasAttribute('flag')"), 28,
                               "decodeURIComponent('%')");
            }
        }
        auto candidate = import(context, source, true);
        if (!candidate) { continue; }
        const auto original = printed(*candidate);
        ctnative::HostContract request;
        request.provider = ctnative::HostContract::Provider::ctbrowserDOM;
        request.entry = guarded(*candidate).getSymName().str();
        request.elementParameters = {0};
        if (control == 32 || control == 38) { request.elementParameters.clear(); }
        request.moduleSha256 = ctnative::hostContractFingerprint(*candidate);
        const auto fingerprint = request.moduleSha256;
        auto error = ctnative::prepareDOMEntry(
            *candidate, request,
            control == 6 || control == 19 || control == 26 || control == 39 ? 0
            : control == 27                                                 ? 500
                                                                            : 100000);
        if (control >= 3 && control != 5 && control != 7 && control != 8 && control != 9 &&
            !(control >= 12 && control <= 15) && !(control >= 20 && control <= 23) &&
            control != 28 && !(control >= 33 && control <= 35)) {
            check(static_cast<bool>(error), "confined catch needs complete effects and lifetime");
            llvm::consumeError(std::move(error));
            check(printed(*candidate) == original && request.moduleSha256 == fingerprint,
                  "refused caught-node preparation preserves source and contract");
        } else {
            if (error) { llvm::errs() << llvm::toString(std::move(error)) << '\n'; }
            check(ctnative::DOMEntryAnalysis(*candidate, request).proved() &&
                      mlir::succeeded(mlir::verify(*candidate)),
                  "caught node keeps its identity through complete DOM reproof");
            bool exception = false;
            candidate->walk([&](mlir::Operation * operation) {
                exception |= llvm::isa<ctjs::ThrowOp, ctjs::TryOp, ctjs::PushHandlerOp>(operation);
            });
            check(!exception, "local catch needs no escaping borrowed exception carrier");
        }
    }
    if (!testClassTransactions(context)) { return; }
    for (const auto provider : {ctnative::HostContract::Provider::ctbrowserDOM,
                                ctnative::HostContract::Provider::ctbrowserDOMSession}) {
        for (unsigned control = 0; control < 12; ++control) {
            std::string source = R"js(function guarded(element) {
                function F(t) { return t.replace(/[A-Z]/g, t => `-${t.toLowerCase()}`); }
                const H = {
                    first(t, key) { return t.getAttribute(F(key)); },
                    second(t, key) { return t.getAttribute(F(key)); }
                };
                class Shape { constructor() { this.key = 'x'; } }
                const shape = new Shape();
                const saved = H.first(element, 'x');
                return saved === H.second(element, 'x') && element.getAttribute(shape.key) === null;
            })js";
            if (control == 1) {
                source.replace(source.find("H.second(element, 'x')"), 22, "H.second(element, 'X')");
            }
            if (control == 2) {
                source.replace(source.find("H.second(element, 'x')"), 22,
                               "H.second(element, element.getAttribute('key'))");
            }
            if (control == 3) { source.insert(source.find("return saved"), "element.unknown(); "); }
            if (control >= 6) {
                source.replace(source.find("H.first(element, 'x')"), 21, "H.first(element, 'ÉAZ')");
            }
            if (control == 7) {
                source.replace(source.find("t.toLowerCase()"), 15, "t.toUpperCase()");
            }
            if (control == 8) { source.replace(source.find("t.toLowerCase()"), 15, "unknown(t)"); }
            if (control == 9) { source.replace(source.find("t.toLowerCase()"), 15, "t.length"); }
            if (control == 10) {
                source.replace(source.find("t.toLowerCase()"), 15, "arguments[0]");
            }
            auto candidate = import(context, source, true);
            if (!candidate) { continue; }
            const auto original = printed(*candidate);
            ctnative::HostContract request;
            request.provider = provider;
            request.entry = guarded(*candidate).getSymName().str();
            request.elementParameters = {0};
            request.initialIntrinsics = {"__ctbrowser_class_defined"};
            if (control != 4) { request.initialIntrinsics.push_back("__ctbrowser_regexp"); }
            request.moduleSha256 = ctnative::hostContractFingerprint(*candidate);
            const auto before = request;
            auto error = ctnative::prepareDOMEntry(*candidate, request,
                                                   control == 5 || control == 11 ? 0 : 1000000);
            if (control != 0 && control != 1 && control != 6) {
                check(static_cast<bool>(error), "every sibling input and effect needs proof");
                llvm::consumeError(std::move(error));
                check(printed(*candidate) == original &&
                          request.moduleSha256 == before.moduleSha256 &&
                          request.initialIntrinsics == before.initialIntrinsics &&
                          request.elementParameters == before.elementParameters &&
                          request.provider == before.provider && request.entry == before.entry,
                      "sibling-call refusal preserves the original source and contract");
            } else {
                if (error) { llvm::errs() << llvm::toString(std::move(error)) << '\n'; }
                const ctnative::DOMEntryAnalysis proof(*candidate, request);
                check(proof.proved() && mlir::succeeded(mlir::verify(*candidate)),
                      "sibling actuals jointly prove their original shared String helper");
            }
        }
    }
    for (const auto provider : {ctnative::HostContract::Provider::ctbrowserDOM,
                                ctnative::HostContract::Provider::ctbrowserDOMSession}) {
        for (unsigned control = 0; control < 8; ++control) {
            std::string source = R"js(function guarded(element) {
                function F(t) { return t.replace(/[A-Z]/g, t => `-${t.toLowerCase()}`); }
                var result = '';
                for (const key of Object.keys(element.dataset)) result = result + F(key);
                return result;
            })js";
            if (control == 1) {
                source.replace(source.find("t.toLowerCase()"), 15, "t.toUpperCase()");
            }
            if (control == 2) {
                source.insert(source.find("return result;"), "F(element.getAttribute('key')); ");
            }
            if (control == 3) {
                source.insert(source.find("var result"), "String.prototype.toLowerCase = F; ");
            }
            if (control == 6) { source.replace(source.find("/[A-Z]/g"), 8, "/[A-Z]/i"); }
            if (control == 7) {
                source.replace(source.find("t => `-${t.toLowerCase()}`"), 26,
                               "t => { unknown(t); return `-${t.toLowerCase()}`; }");
            }
            auto candidate = import(context, source, true);
            if (!candidate) { continue; }
            const auto original = printed(*candidate);
            ctnative::HostContract request;
            request.provider = provider;
            request.entry = guarded(*candidate).getSymName().str();
            request.elementParameters = request.datasetParameters = {0};
            request.initialIntrinsics = {"Object",
                                         "Array",
                                         "RegExp",
                                         "__ctbrowser_regexp",
                                         "__ctbrowser_for_of_open",
                                         "__ctbrowser_iter_next",
                                         "__ctbrowser_iter_close"};
            if (control != 4) { request.initialIntrinsics.push_back("String"); }
            request.moduleSha256 = ctnative::hostContractFingerprint(*candidate);
            const auto before = request;
            auto error = ctnative::prepareDOMEntry(*candidate, request, control == 5 ? 0 : 1000000);
            if (control) {
                check(static_cast<bool>(error), "dynamic replacement requires its complete proof");
                llvm::consumeError(std::move(error));
                check(printed(*candidate) == original &&
                          request.moduleSha256 == before.moduleSha256 &&
                          request.initialIntrinsics == before.initialIntrinsics &&
                          request.elementParameters == before.elementParameters &&
                          request.datasetParameters == before.datasetParameters &&
                          request.provider == before.provider && request.entry == before.entry,
                      "dynamic replacement refusal preserves source and contract");
            } else {
                if (error) { llvm::errs() << llvm::toString(std::move(error)) << '\n'; }
                const ctnative::DOMEntryAnalysis proof(*candidate, request);
                check(proof.proved() && mlir::succeeded(mlir::verify(*candidate)),
                      "dynamic replacement publishes a complete fresh DOM proof");
                if (!proof.proved()) { continue; }
                for (unsigned mutation = 0; mutation < 3; ++mutation) {
                    mlir::OwningOpRef<mlir::ModuleOp> forged(candidate->clone());
                    ctjs::CallOp replacement, lowercase;
                    forged->walk([&](ctjs::CallOp call) {
                        auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                        if (!read) { return; }
                        if (ctjs::constantKey(read.getKey()) == "replace") { replacement = call; }
                        if (ctjs::constantKey(read.getKey()) == "toLowerCase") { lowercase = call; }
                    });
                    if (!check(replacement && lowercase, "dynamic proof retains original calls")) {
                        continue;
                    }
                    auto callback = replacement.getArgs()[1].getDefiningOp<ctjs::CreateClosureOp>();
                    if (!check(static_cast<bool>(callback), "dynamic proof retains its callback")) {
                        continue;
                    }
                    if (mutation == 0) {
                        callback.getEnclosingClosureMutable().assign(
                            callback->getParentOfType<ctjs::FuncOp>().getBody().front().getArgument(
                                ctjs::arg_receiver));
                    } else if (mutation == 1) {
                        replacement->setOperand(1, callback.getEnclosingThis());
                    } else {
                        lowercase->setOperand(1, lowercase->getParentOfType<ctjs::FuncOp>()
                                                     .getBody()
                                                     .front()
                                                     .getArgument(ctjs::arg_receiver));
                    }
                    auto fresh = request;
                    fresh.moduleSha256 = ctnative::hostContractFingerprint(*forged);
                    const ctnative::DOMEntryAnalysis rejected(*forged, fresh);
                    check(!rejected.proved(),
                          "fresh dynamic proof rejects forged callback provenance");
                }
            }
        }
    }
    using ctnative::lowering_detail::inspectSingleInvocationRegion;
    using ctnative::lowering_detail::normalizeDOMURI;
    auto module = import(context, R"js(
function guarded(element) {
    var text = element.hasAttribute("x") ? "%41" : "%";
    var saved = "before";
    try { saved = decodeURIComponent(text); }
    catch (ignored) { return saved; }
    return saved;
}
)js",
                         false);
    if (!module) { return; }
    auto function = guarded(*module);
    const auto original = printed(*module);
    const unsigned checks = countChecks(function);
    ctnative::HostContract contract;
    contract.provider = ctnative::HostContract::Provider::ctbrowserDOM;
    contract.entry = function.getSymName().str();
    contract.elementParameters = {0};
    contract.initialIntrinsics = {"decodeURIComponent"};
    contract.moduleSha256 = ctnative::hostContractFingerprint(*module);
    for (const auto provider : {ctnative::HostContract::Provider::ctbrowserDOM,
                                ctnative::HostContract::Provider::ctbrowserDOMSession}) {
        for (unsigned control = 0; control < 4; ++control) {
            mlir::OwningOpRef<mlir::ModuleOp> candidate(module->clone());
            auto request = contract;
            request.provider = provider;
            if (control == 1) { request.moduleSha256 = "stale"; }
            if (control == 2) { request.initialIntrinsics.clear(); }
            const auto fingerprint = request.moduleSha256;
            auto error = ctnative::prepareDOMEntry(*candidate, request, control == 3 ? 0 : 100000);
            if (control) {
                check(static_cast<bool>(error), "unproved DOM preparation refuses");
                llvm::consumeError(std::move(error));
                check(printed(*candidate) == original && request.moduleSha256 == fingerprint &&
                          request.provider == provider && request.entry == contract.entry &&
                          request.elementParameters == contract.elementParameters &&
                          request.initialIntrinsics == (control == 2 ? std::vector<std::string>{}
                                                                     : contract.initialIntrinsics),
                      "DOM preparation refusal preserves source and host request together");
            } else {
                if (error) { llvm::errs() << llvm::toString(std::move(error)) << '\n'; }
                const ctnative::DOMEntryAnalysis prepared(*candidate, request);
                check(mlir::succeeded(mlir::verify(*candidate)) && prepared.proved() &&
                          !prepared.wrapper() && prepared.entry().isPublic() &&
                          request.moduleSha256 == ctnative::hostContractFingerprint(*candidate) &&
                          request.moduleSha256 != fingerprint,
                      "DOM preparation publishes the normalized entry with its fresh proof");
            }
        }
    }
    mlir::OwningOpRef<mlir::ModuleOp> normalized(module->clone());
    if (auto error = normalizeDOMURI(*normalized, contract)) {
        check(false, "original URI source normalizes with unused payload and saved String");
        llvm::errs() << llvm::toString(std::move(error)) << '\n';
        return;
    }
    check(mlir::succeeded(mlir::verify(*normalized)), "normalized original URI source verifies");
    auto fresh = contract;
    fresh.moduleSha256 = ctnative::hostContractFingerprint(*normalized);
    const ctnative::DOMEntryAnalysis proof(*normalized, fresh);
    if (!check(proof.proved(), "normalized original URI source passes fresh complete DOM proof")) {
        llvm::errs() << proof.reason() << '\n';
        return;
    }
    unsigned invocations = 0;
    normalized->walk([&](ctjs::InvokeOp invocation) {
        ++invocations;
        auto & normal = invocation.getNormalBody().front();
        auto & caught = invocation.getUnwindBody().front();
        auto exit = llvm::cast<ctjs::InvokeExitOp>(invocation.getBody().front().back());
        auto success = llvm::cast<ctjs::InvokeYieldOp>(normal.back());
        auto failure = llvm::cast<ctjs::InvokeYieldOp>(caught.back());
        auto saved = failure.getValues().front().getDefiningOp<ctjs::ConstantOp>();
        auto string =
            saved ? llvm::dyn_cast<ctjs::StringAttr>(saved.getValue()) : ctjs::StringAttr{};
        check(proof.invocation(invocation) && exit.getState().empty() &&
                  caught.getNumArguments() == 1 && caught.getArgument(0).use_empty() &&
                  success.getValues().front() == normal.getArgument(0) && string &&
                  string.getValue() == "before",
              "URI normal result and original pre-call catch String remain separate");
    });
    check(invocations == 2 && countChecks(guarded(*normalized)) == 0,
          "both dynamic prefix arms retain their URI completion and discharge proved checks");
    const auto attempt = [&](unsigned budget) {
        mlir::OwningOpRef<mlir::ModuleOp> candidate(module->clone());
        const auto before = printed(*candidate);
        auto error = normalizeDOMURI(*candidate, contract, budget);
        if (!error) { return true; }
        const auto reason = llvm::toString(std::move(error));
        check(reason.find("budget exhausted") != std::string::npos &&
                  printed(*candidate) == before && countChecks(guarded(*candidate)) == checks,
              "incomplete URI normalization preserves the complete source and all checks");
        return false;
    };
    unsigned lower = 0, upper = 100000;
    while (lower < upper) {
        const unsigned middle = lower + (upper - lower) / 2;
        if (attempt(middle)) {
            upper = middle;
        } else {
            lower = middle + 1;
        }
    }
    check(attempt(lower), "the first complete URI normalization budget succeeds");
    for (unsigned budget = 0; budget < lower; ++budget) {
        if (!check(!attempt(budget), "every smaller URI normalization budget refuses")) { break; }
    }
    for (bool payload : {false, true}) {
        mlir::OwningOpRef<mlir::ModuleOp> candidate(module->clone());
        auto source = inspectSingleInvocationRegion(guarded(*candidate));
        if (!check(source.proved(), "URI mutation starts from the exact original snapshot")) {
            return;
        }
        if (payload) {
            auto returned =
                llvm::dyn_cast<ctjs::ReturnOp>(source.landing->getBlock()->getTerminator());
            if (!check(static_cast<bool>(returned), "URI fixture catch returns its saved state")) {
                return;
            }
            returned->setOperand(0, source.landing.getThrown());
        } else {
            source.check->setOperand(static_cast<unsigned>(source.check.getContOperands().size()),
                                     source.call->getResult(0));
        }
        auto changedContract = contract;
        changedContract.moduleSha256 = ctnative::hostContractFingerprint(*candidate);
        const auto before = printed(*candidate);
        auto error = normalizeDOMURI(*candidate, changedContract);
        if (!check(static_cast<bool>(error), "mutated URI snapshot or observed payload refuses")) {
            continue;
        }
        const auto reason = llvm::toString(std::move(error));
        check(reason.find(payload ? "semantic error payload" : "invocation") != std::string::npos &&
                  printed(*candidate) == before && countChecks(guarded(*candidate)) == checks,
              "fresh URI refusal retains the exact mutated source and every status edge");
    }
    check(printed(*module) == original,
          "URI transaction tests retain their untouched source oracle");
    llvm::outs() << "DOM URI normalization: " << lower
                 << " steps, every incomplete budget preserves " << checks << " checks\n";
}

// Original M's protected body: JSON.parse is looked up first, then
// decodeURIComponent runs, then parse consumes its result. Both calls become
// nested invokes on one success path; either failure reaches the same catch.
} // namespace ctcompile::test::exception_recovery
