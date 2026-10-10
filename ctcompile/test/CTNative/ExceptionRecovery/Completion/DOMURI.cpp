#include "DOMURI.hpp"
#include "../../../../lib/CTNative/HostContract/Analysis.h"
#include "../../../../lib/CTNative/HostContract/DOM/Source/Proof.hpp"
#include "../../../../lib/CTNative/HostContract/Preparation.h"
#include "ClassTransactions.hpp"
#include "DOMObservedState.hpp"
#include "ctbrowser/core/symbol.hpp"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"

#include "check.hpp"

#include <array>
#include <bit>

namespace ctcompile::test::exception_recovery {

using ctcompile::ctnative::host_detail::projectInvocationContinuation;

// A refused preparation leaves every contract field it could have written alone.
static bool sameContract(const ctcompile::ctnative::HostContract & request,
                         const ctcompile::ctnative::HostContract & before) {
    return request.moduleSha256 == before.moduleSha256 &&
           request.initialIntrinsics == before.initialIntrinsics &&
           request.elementParameters == before.elementParameters &&
           request.provider == before.provider && request.entry == before.entry;
}

static void checkRefused(llvm::Error error, bool unchanged, llvm::StringRef label) {
    check(static_cast<bool>(error) && unchanged, label);
    if (error) { llvm::consumeError(std::move(error)); }
}

static void testStructuredSourceOrdering(mlir::ModuleOp original) {
    auto module = mlir::OwningOpRef<mlir::ModuleOp>(original.clone());
    auto entry = module->lookupSymbol<ctjs::FuncOp>("entry$0");
    auto & body = entry.getBody().front();
    auto closure = *body.getOps<ctjs::CreateClosureOp>().begin();
    auto store = *body.getOps<ctjs::SetPropertyOp>().begin();
    auto attempt = *body.getOps<ctjs::TryOp>().begin();
    auto invocation = *attempt.getBody().front().getOps<ctjs::InvokeOp>().begin();
    auto & normal = invocation.getNormalBody().front();
    auto & unwind = invocation.getUnwindBody().front();
    auto normalRead = *normal.getOps<ctjs::GetPropertyOp>().begin();
    auto normalCall = *normal.getOps<ctjs::CallOp>().begin();
    auto unwindCall = *unwind.getOps<ctjs::CallOp>().begin();
    auto caughtCall = *attempt.getCatchBody().front().getOps<ctjs::CallOp>().begin();
    mlir::OpBuilder at(normalCall);
    auto yes = mlir::arith::ConstantIntOp::create(at, closure.getLoc(), 1, 1);
    auto branch = mlir::scf::IfOp::create(at, closure.getLoc(), yes, false);
    at.setInsertionPoint(branch.getThenRegion().front().getTerminator());
    auto * nestedCall = at.clone(*normalCall);
    at.setInsertionPointToStart(&unwind);
    auto scope = mlir::scf::ExecuteRegionOp::create(at, closure.getLoc(), mlir::TypeRange{});
    auto & unknown = scope.getRegion().emplaceBlock();
    at.setInsertionPointToEnd(&unknown);
    auto * unknownRead = at.clone(*normalRead);
    mlir::scf::YieldOp::create(at, closure.getLoc());
    at.setInsertionPointAfter(attempt);
    auto * lateStore = at.clone(*store);
    if (!check(mlir::succeeded(mlir::verify(*module)), "source ordering fixture verifies")) {
        return;
    }
    const auto before = printed(*module);
    const auto ordered = [&](mlir::Operation * definition, mlir::Operation * use, bool expected,
                             llvm::StringRef message) {
        ctcompile::ctnative::dom_source_detail::DOMSource proof(100000);
        check(proof.precedesInStructuredBody(definition, use) == expected, message);
        const unsigned used = 100000 - proof.remaining;
        if (expected && used) {
            for (unsigned budget : {used, used - 1, 0u}) {
                ctcompile::ctnative::dom_source_detail::DOMSource limited(budget);
                check(limited.precedesInStructuredBody(definition, use) == (budget == used),
                      "source ordering charges each ancestor and refuses incomplete proofs");
            }
        }
        check(printed(*module) == before, "source ordering and budget refusal preserve all IR");
    };
    ordered(closure, &invocation.getBody().front().front(), true,
            "closure precedes exact protected call");
    ordered(closure, normalCall, true, "closure precedes normal continuation call");
    ordered(closure, unwindCall, true, "closure precedes unwind continuation call");
    ordered(closure, caughtCall, true, "closure precedes caught continuation call");
    ordered(store, nestedCall, true, "assignment precedes whole nested continuation");
    ordered(normalRead, nestedCall, true, "local definition precedes its nested selection");
    ordered(lateStore, normalCall, false, "late assignment cannot reach normal continuation");
    ordered(lateStore, unwindCall, false, "late assignment cannot reach unwind continuation");
    ordered(lateStore, caughtCall, false, "late assignment cannot reach caught continuation");
    ordered(normalRead, unwindCall, false, "normal definition cannot reach sibling unwind");
    ordered(&attempt.getBody().front().front(), caughtCall, false,
            "try-body definition cannot reach sibling catch");
    ordered(closure, unknownRead, false, "unknown region ancestry remains refused");
    ordered(closure, &invocation.getBody().front().back(), false,
            "protected body admits only the exact call, not state transport");
    ordered(store, store, false, "an assignment cannot precede itself");
}

static void testInvocationCallableReads(mlir::MLIRContext & context) {
    const std::string source = R"MLIR(module {
  ctjs.func @entry$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"method">
    %holder = ctjs.create_object
    %helper = ctjs.create_closure %callee[1] this %u
    ctjs.set_property %holder[%key], %helper
    %answer = ctjs.try {
      %method = ctjs.get_property %holder[%key]
      %result = "ctjs.invoke"() ({
        %called = ctjs.call %method(%holder)
        ctjs.invoke_exit %called state(%u)
      }, {
      ^normal(%value: !ctjs.value):
        %read = ctjs.get_property %holder[%key]
        %called = ctjs.call %read(%holder)
        "ctjs.invoke_yield"(%called) : (!ctjs.value) -> ()
      }, {
      ^unwind(%error: !ctjs.value, %saved: !ctjs.value):
        %read = ctjs.get_property %holder[%key]
        %called = ctjs.call %read(%holder)
        "ctjs.invoke_yield"(%called) : (!ctjs.value) -> ()
      }) : () -> !ctjs.value
      %failed = arith.constant false
      ctjs.try_exit %failed normal %result caught(%u)
    } catch {
    ^catch(%error: !ctjs.value):
      %read = ctjs.get_property %holder[%key]
      %called = ctjs.call %read(%holder)
      ctjs.try_yield %called
    } : !ctjs.value
    ctjs.return %answer
  }
  ctjs.func private @method$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
})MLIR";
    unsigned required = 0;
    for (unsigned control = 0; control != 13; ++control) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
        if (!check(static_cast<bool>(module), "immutable invocation callable fixture parses")) {
            return;
        }
        if (control == 0) { testStructuredSourceOrdering(*module); }
        auto entry = module->lookupSymbol<ctjs::FuncOp>("entry$0");
        auto & body = entry.getBody().front();
        auto holder = *body.getOps<ctjs::CreateObjectOp>().begin();
        auto store = *body.getOps<ctjs::SetPropertyOp>().begin();
        auto closure = *body.getOps<ctjs::CreateClosureOp>().begin();
        auto attempt = *body.getOps<ctjs::TryOp>().begin();
        auto invocation = *attempt.getBody().front().getOps<ctjs::InvokeOp>().begin();
        auto & normal = invocation.getNormalBody().front();
        auto & unwind = invocation.getUnwindBody().front();
        mlir::OpBuilder at(attempt);
        if (control == 1) {
            store->moveAfter(attempt);
        } else if (control == 2 || control == 3) {
            if (control == 2) { at.setInsertionPointToStart(&normal); }
            at.clone(*store);
        } else if (control == 4) {
            at.setInsertionPointToStart(&unwind);
            ctjs::StoreGlobalOp::create(at, holder.getLoc(), "published", holder);
        } else if (control == 5) {
            auto exit = llvm::cast<ctjs::InvokeExitOp>(invocation.getBody().front().back());
            exit.getStateMutable().assign(holder.getResult());
        } else if (control == 6) {
            attempt.getCatchBody().front().back().setOperand(0, holder.getResult());
        } else if (control == 7 || control == 10) {
            at.setInsertionPointToStart(&normal);
            if (control == 7) {
                auto scope =
                    mlir::scf::ExecuteRegionOp::create(at, holder.getLoc(), mlir::TypeRange{});
                auto & nested = scope.getRegion().emplaceBlock();
                at.setInsertionPointToEnd(&nested);
                ctjs::GetPropertyOp::create(at, holder.getLoc(), holder, store.getKey());
                mlir::scf::YieldOp::create(at, holder.getLoc());
            } else {
                auto yes = mlir::arith::ConstantIntOp::create(at, holder.getLoc(), 1, 1);
                auto branch = mlir::scf::IfOp::create(at, holder.getLoc(), yes, false);
                at.setInsertionPoint(branch.getThenRegion().front().getTerminator());
                ctjs::GetPropertyOp::create(at, holder.getLoc(), holder, store.getKey());
            }
        } else if (control == 8) {
            auto read = *normal.getOps<ctjs::GetPropertyOp>().begin();
            read.getObjectMutable().assign(body.getArgument(ctjs::arg_receiver));
        } else if (control == 9) {
            at.setInsertionPoint(store);
            ctjs::GetPropertyOp::create(at, holder.getLoc(), holder, store.getKey());
        }
        if (!check(mlir::succeeded(mlir::verify(*module)),
                   "invocation callable control verifies")) {
            continue;
        }
        const auto before = printed(*module);
        const unsigned budget = control == 11 ? 0 : control == 12 ? required - 1 : 100000;
        unsigned remaining = budget;
        auto proof = ctcompile::ctnative::analyzeLocalCallableObject(holder, [&] {
            if (!remaining) { return false; }
            --remaining;
            return true;
        });
        const bool expected = control == 0 || control == 10;
        check(static_cast<bool>(proof) == expected,
              "only initialized immutable callable reads cross exception continuations");
        check(printed(*module) == before, "callable proof and budget refusal never change IR");
        if (!proof) {
            llvm::consumeError(proof.takeError());
            continue;
        }
        if (control == 0) { required = budget - remaining; }
        check(proof->stores.size() == 1 && proof->stores.front() == store &&
                  proof->calls.size() == 4 && proof->reads.size() == (control == 10 ? 5u : 4u),
              "callable proof retains every slot, read and exact receiver call");
        for (auto [read, target] : proof->reads) {
            check(target == closure && read.getObject() == holder.getResult(),
                  "all outcomes select the original initialized closure");
        }
    }
}

static void testInvocationCellReads(mlir::MLIRContext & context) {
    const std::string source = R"MLIR(module {
  ctjs.func @entry$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %initial: !ctjs.value, %replacement: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %frame = ctjs.frame_enter 1
    %cell = ctjs.create_cell %initial
    %result = "ctjs.invoke"() ({
      %called = ctjs.call %callee(%this)
      ctjs.invoke_exit %called state(%initial)
    }, {
    ^normal(%value: !ctjs.value):
      %read = ctjs.cell_get %cell
      ctjs.store_global "normal", %read
      "ctjs.invoke_yield"(%value) : (!ctjs.value) -> ()
    }, {
    ^unwind(%error: !ctjs.value, %saved: !ctjs.value):
      %read = ctjs.cell_get %cell
      ctjs.store_global "unwind", %read
      "ctjs.invoke_yield"(%error) : (!ctjs.value) -> ()
    }) : () -> !ctjs.value
    ctjs.frame_exit %frame
    ctjs.return %result
  }
})MLIR";
    unsigned required = 0;
    for (unsigned control = 0; control != 11; ++control) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
        if (!check(static_cast<bool>(module), "immutable invocation cell fixture parses")) {
            return;
        }
        auto entry = module->lookupSymbol<ctjs::FuncOp>("entry$0");
        auto & body = entry.getBody().front();
        auto cell = *body.getOps<ctjs::CreateCellOp>().begin();
        auto invocation = *body.getOps<ctjs::InvokeOp>().begin();
        const mlir::Value replacement = body.getArgument(4);
        mlir::OpBuilder at(invocation);
        if (control >= 1 && control <= 4) {
            if (control == 2) { at.setInsertionPointAfter(invocation); }
            if (control == 3) { at.setInsertionPointToStart(&invocation.getNormalBody().front()); }
            ctjs::CellSetOp::create(at, cell.getLoc(), cell, replacement);
            if (control == 4) { ctjs::CellSetOp::create(at, cell.getLoc(), cell, replacement); }
        } else if (control == 5) {
            ctjs::StoreGlobalOp::create(at, cell.getLoc(), "published", cell);
        } else if (control == 6) {
            auto exit = llvm::cast<ctjs::InvokeExitOp>(invocation.getBody().front().back());
            exit.getStateMutable().assign(cell.getResult());
        } else if (control == 7 || control == 8) {
            auto read = *invocation.getNormalBody().front().getOps<ctjs::CellGetOp>().begin();
            at.setInsertionPoint(read);
            if (control == 7) {
                auto scope =
                    mlir::scf::ExecuteRegionOp::create(at, cell.getLoc(), mlir::TypeRange{});
                auto & nested = scope.getRegion().emplaceBlock();
                read->moveBefore(&nested, nested.end());
                at.setInsertionPointToEnd(&nested);
                mlir::scf::YieldOp::create(at, cell.getLoc());
            } else {
                auto yes = mlir::arith::ConstantIntOp::create(at, cell.getLoc(), 1, 1);
                auto branch = mlir::scf::IfOp::create(at, cell.getLoc(), yes, false);
                read->moveBefore(branch.getThenRegion().front().getTerminator());
            }
            auto store = *invocation.getNormalBody().front().getOps<ctjs::StoreGlobalOp>().begin();
            store->moveAfter(read);
        }
        if (!check(mlir::succeeded(mlir::verify(*module)), "invocation cell control verifies")) {
            continue;
        }
        const auto before = printed(*module);
        const unsigned budget = control == 9 ? 0 : control == 10 ? required - 1 : 100000;
        ctcompile::ctnative::dom_source_detail::DOMSource proof(budget);
        const bool resolved = proof.resolveCell(cell);
        const bool expected = control == 0 || control == 1 || control == 8;
        check(resolved == expected, "only ordered immutable invocation cell reads resolve");
        if (!resolved) {
            check(printed(*module) == before && proof.cells.empty(),
                  "failed cell ordering, escape or budget proof preserves every read");
            continue;
        }
        if (control == 0) { required = budget - proof.remaining; }
        unsigned reads = 0, stores = 0;
        entry.walk([&](ctjs::CellGetOp) { ++reads; });
        entry.walk([&](ctjs::StoreGlobalOp store) {
            ++stores;
            check(store.getValue() == (control == 1 ? replacement : cell.getInitial()),
                  "both invocation outcomes observe the same initialized snapshot");
        });
        check(!reads && stores == 2 && mlir::succeeded(mlir::verify(*module)),
              "immutable snapshot substitution preserves both continuation effects");
    }
}

static void testInertHelperCompletion(mlir::MLIRContext & context) {
    const std::string source = R"MLIR(module {
  ctjs.func @entry$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %value: !ctjs.value, %saved: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %frame = ctjs.frame_enter 1
    %u = ctjs.constant #ctjs.undefined
    %tuple:3 = "ctjs.invoke"() ({
      %result = ctjs.call_direct @identity$1(%u, %u, %u, %value)
      ctjs.invoke_exit %result state(%saved, %value)
    }, {
    ^normal(%result: !ctjs.value):
      "ctjs.invoke_yield"(%result, %value, %saved) : (!ctjs.value, !ctjs.value, !ctjs.value) -> ()
    }, {
    ^unwind(%error: !ctjs.value, %oldSaved: !ctjs.value, %oldValue: !ctjs.value):
      "ctjs.invoke_yield"(%error, %oldSaved, %oldValue) : (!ctjs.value, !ctjs.value, !ctjs.value) -> ()
    }) : () -> (!ctjs.value, !ctjs.value, !ctjs.value)
    ctjs.store_global "payload", %tuple#0
    ctjs.store_global "value", %tuple#1
    ctjs.store_global "saved", %tuple#2
    ctjs.frame_exit %frame
    ctjs.return %tuple#0
  }
  ctjs.func private @identity$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %frame = ctjs.frame_enter 1
    ctjs.frame_exit %frame
    ctjs.return %value
  }
})MLIR";
    const auto replace = [](std::string text, llvm::StringRef from, llvm::StringRef to) {
        replaceOnce(text, from, to.str());
        return text;
    };
    auto original = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    if (!check(static_cast<bool>(original), "observed inert helper completion parses")) { return; }
    const auto snapshot = printed(*original);
    unsigned used = 0;
    auto input = mlir::OwningOpRef<mlir::ModuleOp>(original->clone());
    auto failure = ctcompile::ctnative::expandDOMHelpers(*input, "entry$0", 100000, &used);
    if (!check(!failure, "independently inert helper admits its observed normal tuple")) {
        llvm::errs() << llvm::toString(std::move(failure)) << '\n';
        return;
    }
    check(mlir::succeeded(mlir::verify(*input)), "observed helper expansion verifies");
    auto entry = input->lookupSymbol<ctjs::FuncOp>("entry$0");
    auto value = entry.getBody().front().getArgument(3);
    auto saved = entry.getBody().front().getArgument(4);
    unsigned stores = 0, calls = 0;
    entry.walk([&](ctjs::StoreGlobalOp store) {
        ++stores;
        check(store.getValue() == (store.getName() == "saved" ? saved : value),
              "normal payload and successful SSA retain their own slots, not unwind order");
    });
    entry.walk([&](mlir::Operation * operation) {
        calls += llvm::isa<ctjs::InvokeOp, ctjs::CallDirectOp, ctjs::CallOp>(operation);
    });
    check(stores == 3 && calls == 0 && !input->lookupSymbol<ctjs::FuncOp>("identity$1"),
          "observed inert helper expands once without retaining an invocation runtime");
    auto chainSource = replace(source, "    ctjs.return %value\n  }\n}", R"MLIR(
    %readKey = ctjs.constant #ctjs.string<"hasAttribute">
    %writeKey = ctjs.constant #ctjs.string<"setAttribute">
    %yielded = ctjs.constant #ctjs.string<"data-yielded">
    %next = ctjs.constant #ctjs.string<"data-next">
    %yes = ctjs.constant #ctjs.string<"yes">
    %read = ctjs.get_property %value[%readKey]
    %done = ctjs.call %read(%value, %yielded)
    %write = ctjs.get_property %value[%writeKey]
    %first = ctjs.call %write(%value, %next, %done)
    %second = ctjs.call %write(%value, %yielded, %yes)
    %record = ctjs.create_object
    %doneKey = ctjs.constant #ctjs.string<"done">
    %valueKey = ctjs.constant #ctjs.string<"value">
    ctjs.set_property %record[%doneKey], %done
    ctjs.set_property %record[%valueKey], %value
    ctjs.return %record
  }
})MLIR");
    chainSource = replace(
        chainSource,
        "    ^unwind(%error: !ctjs.value, %oldSaved: !ctjs.value, %oldValue: !ctjs.value):",
        "    ^unwind(%error: !ctjs.value, %oldSaved: !ctjs.value, %oldValue: !ctjs.value):\n"
        "      ctjs.store_global \"caught\", %error");
    auto chain = mlir::parseSourceString<mlir::ModuleOp>(chainSource, &context);
    auto chainError = ctcompile::ctnative::expandDOMHelpers(*chain, "entry$0", 100000);
    if (check(!chainError, "observed next helper retains its complete call chain")) {
        auto entry = chain->lookupSymbol<ctjs::FuncOp>("entry$0");
        auto saved = entry.getBody().front().getArgument(4);
        auto value = entry.getBody().front().getArgument(3);
        unsigned invocations = 0;
        entry.walk([&](ctjs::InvokeOp invocation) {
            ++invocations;
            auto exit = llvm::cast<ctjs::InvokeExitOp>(invocation.getBody().front().back());
            auto & unwind = invocation.getUnwindBody().front();
            auto effect = llvm::dyn_cast<ctjs::StoreGlobalOp>(unwind.front());
            auto yield = llvm::cast<ctjs::InvokeYieldOp>(unwind.back());
            check(exit.getState().size() == 2 && exit.getState()[0] == saved &&
                      exit.getState()[1] == value && effect &&
                      effect.getValue() == unwind.getArgument(0) &&
                      llvm::equal(yield.getValues(), unwind.getArguments()),
                  "every leaf retains caller saved state and exact failure payload/effects");
        });
        check(invocations == 3 && mlir::succeeded(mlir::verify(*chain)),
              "read and both writes remain separately protected before effect proof");
        chainError = ctcompile::ctnative::lowering_detail::normalizeDOMAttributeInvocations(
            entry, 100000, {0});
        if (check(!chainError, "independent attribute proof consumes the complete helper chain")) {
            llvm::SmallVector<ctjs::CallOp> leaves;
            entry.walk([&](ctjs::CallOp call) { leaves.push_back(call); });
            unsigned remainingInvocations = 0, observers = 0;
            entry.walk([&](ctjs::InvokeOp) { ++remainingInvocations; });
            entry.walk([&](ctjs::StoreGlobalOp store) {
                ++observers;
                check(
                    store.getName() == "payload"
                        ? static_cast<bool>(store.getValue().getDefiningOp<ctjs::CreateObjectOp>())
                        : store.getValue() == (store.getName() == "saved" ? saved : value),
                    "successful helper result and caller registers retain separate identities");
            });
            check(!remainingInvocations && observers == 3 && leaves.size() == 3 &&
                      leaves[1].getArgs().back() == leaves[0].getResult() &&
                      ctjs::constantKey(leaves[2].getArgs().back()) == "yes" &&
                      mlir::succeeded(mlir::verify(*chain)),
                  "next helper keeps original read snapshot, write order and result construction");
        }
    }
    if (chainError) { llvm::errs() << llvm::toString(std::move(chainError)) << '\n'; }
    auto queuedSource = replace(chainSource, "    ^normal(%result: !ctjs.value):",
                                "    ^normal(%result: !ctjs.value):\n"
                                "      %later = ctjs.call_direct @identity$1(%u, %u, %u, %value)");
    auto queued = mlir::parseSourceString<mlir::ModuleOp>(queuedSource, &context);
    auto queuedError = ctcompile::ctnative::expandDOMHelpers(*queued, "entry$0", 100000);
    if (check(!queuedError, "queued normal helper calls survive chain expansion")) {
        unsigned calls = 0, invocations = 0, direct = 0;
        queued->walk([&](ctjs::CallOp) { ++calls; });
        queued->walk([&](ctjs::CallDirectOp) { ++direct; });
        queued->walk([&](ctjs::InvokeOp) { ++invocations; });
        check(calls == 6 && invocations == 3 && !direct && mlir::succeeded(mlir::verify(*queued)),
              "later normal calls remain outside the preceding call's protection");
    }
    if (queuedError) { llvm::errs() << llvm::toString(std::move(queuedError)) << '\n'; }
    for (const std::string line : {"    ctjs.store_global \"payload\", %tuple#0\n",
                                   "    ctjs.store_global \"value\", %tuple#1\n",
                                   "    ctjs.store_global \"saved\", %tuple#2\n"}) {
        chainSource = replace(chainSource, line, "");
    }
    chainSource = replace(chainSource, "    ctjs.return %tuple#0",
                          "    %key = ctjs.constant #ctjs.string<\"done\">\n"
                          "    %done = ctjs.get_property %tuple#0[%key]\n"
                          "    ctjs.return %done");
    for (unsigned control = 0; control != 6; ++control) {
        auto text = chainSource;
        if (control == 1) {
            text =
                replace(text, "%first = ctjs.call %write(%value", "%first = ctjs.call %write(%new");
        }
        if (control == 2) {
            text = replace(text, "#ctjs.string<\"data-yielded\">", "#ctjs.string<\"bad name\">");
        }
        if (control == 3) {
            text = replace(
                text, "    %record = ctjs.create_object",
                "    ctjs.store_global \"leaked\", %value\n    %record = ctjs.create_object");
        }
        if (control == 5) {
            text = replace(text, "      ctjs.store_global \"caught\", %error",
                           "      %later = ctjs.call_direct @identity$1(%u, %u, %u, %value)\n"
                           "      ctjs.store_global \"caught\", %error");
        }
        auto candidate = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        const auto before = printed(*candidate);
        ctcompile::ctnative::HostContract contract;
        contract.provider = ctcompile::ctnative::HostContract::Provider::ctbrowserDOM;
        contract.entry = "entry$0";
        contract.elementParameters = {0, 1};
        contract.moduleSha256 = ctcompile::ctnative::hostContractFingerprint(*candidate);
        const auto fingerprint = contract.moduleSha256;
        auto error =
            ctcompile::ctnative::prepareDOMEntry(*candidate, contract, control == 4 ? 0 : 100000);
        if (control) {
            check(static_cast<bool>(error) && printed(*candidate) == before &&
                      contract.moduleSha256 == fingerprint,
                  "unproved chain effects, invalid names and budget exhaustion preserve source");
        } else {
            check(!error && mlir::succeeded(mlir::verify(*candidate)),
                  "observed next result passes complete DOM preparation");
        }
        if (error) { llvm::errs() << llvm::toString(std::move(error)) << '\n'; }
    }
    for (const std::string method : {"hasAttribute", "setAttribute"}) {
        auto leafSource = replace(
            source, "    ctjs.return %value\n  }\n}",
            "    %key = ctjs.constant #ctjs.string<\"" + method +
                "\">\n    %name = ctjs.constant #ctjs.string<\"data-written\">\n"
                "    %method = ctjs.get_property %value[%key]\n"
                "    %leaf = ctjs.call %method(%value, %name" +
                (method == "setAttribute" ? ", %name" : "") + ")\n    ctjs.return %leaf\n  }\n}");
        auto tupleSource = mlir::parseSourceString<mlir::ModuleOp>(leafSource, &context);
        auto tupleError = ctcompile::ctnative::expandDOMHelpers(*tupleSource, "entry$0", 100000);
        if (!check(!tupleError, "observed tuple helper expands before projection")) {
            llvm::consumeError(std::move(tupleError));
            continue;
        }
        auto tupleEntry = tupleSource->lookupSymbol<ctjs::FuncOp>("entry$0");
        const auto tupleSnapshot = printed(*tupleSource);
        auto budgetError = ctcompile::ctnative::lowering_detail::normalizeDOMAttributeInvocations(
            tupleEntry, 0, {0});
        check(static_cast<bool>(budgetError) && printed(*tupleSource) == tupleSnapshot,
              "attribute projection requires its own verification budget before mutation");
        if (budgetError) { llvm::consumeError(std::move(budgetError)); }
        tupleError = ctcompile::ctnative::lowering_detail::normalizeDOMAttributeInvocations(
            tupleEntry, 100000, {0});
        if (!check(!tupleError, "observed attribute tuple projects after helper binding")) {
            llvm::consumeError(std::move(tupleError));
            continue;
        }
        unsigned tupleStores = 0, tupleInvocations = 0;
        tupleEntry.walk([&](ctjs::InvokeOp) { ++tupleInvocations; });
        tupleEntry.walk([&](ctjs::StoreGlobalOp store) {
            ++tupleStores;
            check(store.getName() == "payload"
                      ? static_cast<bool>(store.getValue().getDefiningOp<ctjs::CallOp>())
                      : store.getValue() == tupleEntry.getBody().front().getArgument(
                                                store.getName() == "saved" ? 4 : 3),
                  "every normal payload and saved slot keeps its original source identity");
        });
        check(tupleStores == 3 && !tupleInvocations && mlir::succeeded(mlir::verify(*tupleSource)),
              "normal tuple transport disappears while all three observers remain");
        auto effectSource = replace(leafSource, "    ^normal(%result: !ctjs.value):",
                                    "    ^normal(%result: !ctjs.value):\n"
                                    "      ctjs.store_global \"normal\", %result");
        effectSource = replace(
            effectSource,
            "    ^unwind(%error: !ctjs.value, %oldSaved: !ctjs.value, %oldValue: !ctjs.value):",
            "    ^unwind(%error: !ctjs.value, %oldSaved: !ctjs.value, %oldValue: !ctjs.value):\n"
            "      ctjs.store_global \"unwind\", %error");
        auto effects = mlir::parseSourceString<mlir::ModuleOp>(effectSource, &context);
        auto effectError = ctcompile::ctnative::expandDOMHelpers(*effects, "entry$0", 100000);
        if (!check(!effectError, "effectful continuations retain their helper invocation")) {
            llvm::consumeError(std::move(effectError));
            continue;
        }
        auto effectEntry = effects->lookupSymbol<ctjs::FuncOp>("entry$0");
        effectError = ctcompile::ctnative::lowering_detail::normalizeDOMAttributeInvocations(
            effectEntry, 100000, {0});
        if (!check(!effectError, "nonthrowing attributes retain normal continuation effects")) {
            llvm::consumeError(std::move(effectError));
            continue;
        }
        unsigned effectsSeen = 0;
        effectEntry.walk([&](ctjs::StoreGlobalOp store) {
            ++effectsSeen;
            check(store.getName() != "unwind", "only independently impossible unwind disappears");
            if (store.getName() == "normal" || store.getName() == "payload") {
                auto called = store.getValue().getDefiningOp<ctjs::CallOp>();
                check(called && called->isBeforeInBlock(store),
                      "normal payload effects execute after the original call");
            } else {
                check(store.getValue() == effectEntry.getBody().front().getArgument(
                                              store.getName() == "saved" ? 4 : 3),
                      "effectful completion preserves successful saved slots");
            }
        });
        check(effectsSeen == 4 && mlir::succeeded(mlir::verify(*effects)),
              "all normal effects and tuple observers survive ordered projection");
        auto projectedSource =
            replace(leafSource, "    ctjs.return %leaf",
                    "    %savedReturn = ctjs.constant #ctjs.string<\"saved return\">\n"
                    "    ctjs.return %savedReturn");
        for (const std::string line : {"    ctjs.store_global \"payload\", %tuple#0\n",
                                       "    ctjs.store_global \"value\", %tuple#1\n",
                                       "    ctjs.store_global \"saved\", %tuple#2\n"}) {
            projectedSource = replace(projectedSource, line, "");
        }
        for (unsigned control = 0; control != 9; ++control) {
            auto text = projectedSource;
            if (control == 2) {
                text = replace(text, "    ^normal(%result: !ctjs.value):",
                               "    ^normal(%result: !ctjs.value):\n"
                               "      ctjs.store_global \"effect\", %result");
            }
            if (control == 3) {
                text = replace(text, "ctjs.call %method(%value", "ctjs.call %method(%new");
            }
            if (control == 4) {
                text = replace(text, "#ctjs.string<\"data-written\">", "#ctjs.number<42>");
            }
            if (control >= 6) {
                text = replace(text, "    ^normal(%result: !ctjs.value):", R"MLIR(
    ^normal(%result: !ctjs.value):
      %writeKey = ctjs.constant #ctjs.string<"setAttribute">
      %readKey = ctjs.constant #ctjs.string<"hasAttribute">
      %name = ctjs.constant #ctjs.string<"data-after">
      %write = ctjs.get_property %value[%writeKey]
      %read = ctjs.get_property %value[%readKey]
      %snapshot = "ctjs.invoke"() ({
        %called = ctjs.call %read(%value, %name)
        ctjs.invoke_exit %called state(%saved)
      }, {
      ^readNormal(%present: !ctjs.value):
        %written = ctjs.call %write(%value, %name, %present)
        "ctjs.invoke_yield"(%present) : (!ctjs.value) -> ()
      }, {
      ^readUnwind(%error: !ctjs.value, %oldSaved: !ctjs.value):
        ctjs.store_global "unreachable", %error
        "ctjs.invoke_yield"(%oldSaved) : (!ctjs.value) -> ()
      }) : () -> !ctjs.value
      %last = ctjs.call %write(%value, %name, %snapshot)
)MLIR");
                if (control == 7) {
                    text = replace(text, "%write = ctjs.get_property %value[%writeKey]",
                                   "%write = ctjs.get_property %new[%writeKey]");
                }
                if (control == 8) {
                    text = replace(text, "%last = ctjs.call %write(%value, %name, %snapshot)",
                                   "%last = ctjs.call %write(%value, %name, %saved)");
                }
            }
            auto projected = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
            if (!check(static_cast<bool>(projected), "observed helper preparation parses")) {
                continue;
            }
            const auto snapshot = printed(*projected);
            ctcompile::ctnative::HostContract contract;
            contract.provider = ctcompile::ctnative::HostContract::Provider::ctbrowserDOM;
            contract.entry = "entry$0";
            contract.elementParameters =
                control == 1 ? std::vector<unsigned>{} : std::vector<unsigned>{0, 1};
            contract.moduleSha256 = ctcompile::ctnative::hostContractFingerprint(*projected);
            const auto fingerprint = contract.moduleSha256;
            auto error = ctcompile::ctnative::prepareDOMEntry(*projected, contract,
                                                              control == 5 ? 0 : 100000);
            if (control && control != 6) {
                check(static_cast<bool>(error),
                      "observed helper needs proved effects, identity, arguments and budget");
                if (error) { llvm::consumeError(std::move(error)); }
                check(printed(*projected) == snapshot && contract.moduleSha256 == fingerprint,
                      "refused observed helper preserves source and contract");
                continue;
            }
            if (!check(!error, "observed helper return passes complete DOM preparation")) {
                llvm::errs() << llvm::toString(std::move(error)) << '\n';
                continue;
            }
            auto entry = projected->lookupSymbol<ctjs::FuncOp>("entry$0");
            unsigned invocations = 0, leaves = 0;
            entry.walk([&](ctjs::InvokeOp) { ++invocations; });
            entry.walk([&](ctjs::CallOp) { ++leaves; });
            auto returned = llvm::cast<ctjs::ReturnOp>(entry.getBody().front().back());
            check(
                !invocations && leaves == (control == 6 ? 4u : 1u) &&
                    ctjs::constantKey(returned.getValue()) == "saved return" &&
                    ctcompile::ctnative::DOMEntryAnalysis(*projected, contract).proved() &&
                    mlir::succeeded(mlir::verify(*projected)),
                "typed original call and independent return survive without invocation transport");
            if (control == 6) {
                llvm::SmallVector<ctjs::CallOp> calls;
                entry.walk([&](ctjs::CallOp call) { calls.push_back(call); });
                check(calls.size() == 4 && calls[2].getArgs().back() == calls[1].getResult() &&
                          calls[3].getArgs().back() == calls[1].getResult() &&
                          calls[0]->isBeforeInBlock(calls[1]) &&
                          calls[1]->isBeforeInBlock(calls[2]) &&
                          calls[2]->isBeforeInBlock(calls[3]),
                      "nested continuation reads and writes retain order and Boolean snapshot");
            }
        }
        // Both continuations remain observable. Expansion must preserve these
        // operations, not select one path from the helper's nominal return type.
        leafSource = replace(leafSource, "    ^normal(%result: !ctjs.value):",
                             "    ^normal(%result: !ctjs.value):\n"
                             "      ctjs.store_global \"normal\", %result");
        leafSource = replace(
            leafSource,
            "    ^unwind(%error: !ctjs.value, %oldSaved: !ctjs.value, %oldValue: !ctjs.value):",
            "    ^unwind(%error: !ctjs.value, %oldSaved: !ctjs.value, %oldValue: !ctjs.value):\n"
            "      ctjs.store_global \"unwind\", %error");
        auto candidate = mlir::parseSourceString<mlir::ModuleOp>(leafSource, &context);
        if (!check(static_cast<bool>(candidate), "observed DOM leaf forwarding parses")) {
            continue;
        }
        ctjs::InvokeOp invocation;
        candidate->walk([&](ctjs::InvokeOp found) { invocation = found; });
        auto normal = llvm::cast<ctjs::InvokeYieldOp>(invocation.getNormalBody().front().back());
        auto unwind = llvm::cast<ctjs::InvokeYieldOp>(invocation.getUnwindBody().front().back());
        auto dispatch = llvm::cast<ctjs::InvokeExitOp>(invocation.getBody().front().back());
        const llvm::SmallVector<mlir::Value> normalValues(normal.getValues());
        const llvm::SmallVector<mlir::Value> unwindValues(unwind.getValues());
        const llvm::SmallVector<mlir::Value> state(dispatch.getState());
        auto * normalEffect = &invocation.getNormalBody().front().front();
        auto * unwindEffect = &invocation.getUnwindBody().front().front();
        unsigned leafSteps = 0;
        auto failure =
            ctcompile::ctnative::expandDOMHelpers(*candidate, "entry$0", 100000, &leafSteps);
        if (!check(!failure, "observed DOM helper forwards its exact protected leaf")) {
            llvm::errs() << llvm::toString(std::move(failure)) << '\n';
            continue;
        }
        check(mlir::succeeded(mlir::verify(*candidate)), "forwarded invocation verifies");
        auto leaf = llvm::dyn_cast<ctjs::CallOp>(invocation.getBody().front().front());
        check(leaf && dispatch.getNormalResult() == leaf.getResult() &&
                  leaf->getNextNode() == dispatch &&
                  ctjs::constantKey(
                      leaf.getCallee().getDefiningOp<ctjs::GetPropertyOp>().getKey()) == method,
              "one original DOM leaf remains protected with its own result");
        check(llvm::equal(normal.getValues(), normalValues) &&
                  llvm::equal(unwind.getValues(), unwindValues) &&
                  llvm::equal(dispatch.getState(), state) &&
                  &invocation.getNormalBody().front().front() == normalEffect &&
                  &invocation.getUnwindBody().front().front() == unwindEffect,
              "normal and unwind payloads, saved registers and effects remain unchanged");
        auto exhausted = mlir::parseSourceString<mlir::ModuleOp>(leafSource, &context);
        auto refused = ctcompile::ctnative::expandDOMHelpers(*exhausted, "entry$0", leafSteps - 1);
        check(static_cast<bool>(refused), "incomplete leaf forwarding budget refuses");
        if (refused) { llvm::consumeError(std::move(refused)); }
        for (const bool suffix : {false, true}) {
            // The argument case preserves the former refusal body unchanged.
            // The suffix literal must move before Invoke to dominate success.
            const auto ownResult =
                replace(leafSource, "    ctjs.return %leaf",
                        suffix ? "    %suffix = ctjs.constant #ctjs.string<\"saved result\">\n"
                                 "    ctjs.return %suffix"
                               : "    ctjs.return %value");
            auto candidate = mlir::parseSourceString<mlir::ModuleOp>(ownResult, &context);
            if (!check(static_cast<bool>(candidate), "independent helper result parses")) {
                continue;
            }
            ctjs::InvokeOp invoke;
            candidate->walk([&](ctjs::InvokeOp found) { invoke = found; });
            auto exit = llvm::cast<ctjs::InvokeExitOp>(invoke.getBody().front().back());
            auto yes = llvm::cast<ctjs::InvokeYieldOp>(invoke.getNormalBody().front().back());
            auto no = llvm::cast<ctjs::InvokeYieldOp>(invoke.getUnwindBody().front().back());
            const llvm::SmallVector<mlir::Value> savedState(exit.getState());
            const llvm::SmallVector<mlir::Value> failedValues(no.getValues());
            const llvm::SmallVector<mlir::Value> successfulState(yes.getValues().drop_front());
            auto normalEffect =
                llvm::cast<ctjs::StoreGlobalOp>(invoke.getNormalBody().front().front());
            auto * failureEffect = &invoke.getUnwindBody().front().front();
            unsigned steps = 0;
            auto error =
                ctcompile::ctnative::expandDOMHelpers(*candidate, "entry$0", 100000, &steps);
            if (!check(!error, "helper return is independent of its protected call result")) {
                llvm::errs() << llvm::toString(std::move(error)) << '\n';
                continue;
            }
            check(mlir::succeeded(mlir::verify(*candidate)),
                  "helper result dominates its continuation");
            auto payload = yes.getValues().front();
            check(suffix ? ctjs::constantKey(payload) == "saved result"
                         : payload == candidate->lookupSymbol<ctjs::FuncOp>("entry$0")
                                          .getBody()
                                          .front()
                                          .getArgument(3),
                  "normal continuation observes the actual helper return");
            check(normalEffect.getValue() == payload &&
                      invoke.getNormalBody().front().getArgument(0).use_empty() &&
                      llvm::equal(yes.getValues().drop_front(), successfulState) &&
                      llvm::equal(no.getValues(), failedValues) &&
                      llvm::equal(exit.getState(), savedState) &&
                      &invoke.getUnwindBody().front().front() == failureEffect &&
                      exit.getNormalResult() == invoke.getBody().front().front().getResult(0),
                  "success effect sees its payload while call dispatch and failure stay unchanged");
            auto limited = mlir::parseSourceString<mlir::ModuleOp>(ownResult, &context);
            auto exhausted = ctcompile::ctnative::expandDOMHelpers(*limited, "entry$0", steps - 1);
            check(static_cast<bool>(exhausted), "independent result charges its complete rewrite");
            if (exhausted) { llvm::consumeError(std::move(exhausted)); }
        }
        unsigned control = 0;
        for (const auto & invalid :
             {replace(leafSource, "    ctjs.return %leaf", "    ctjs.return %method"),
              replace(leafSource, "    ctjs.return %leaf",
                      "    %second = ctjs.call %method(%value, %name)\n    ctjs.return %leaf"),
              replace(leafSource, "    ctjs.return %leaf",
                      "    ctjs.store_global \"extra\", %value\n    ctjs.return %leaf"),
              replace(leafSource, "    %method = ctjs.get_property",
                      "    %coerced = ctjs.unary plus %value\n"
                      "    %method = ctjs.get_property")}) {
            auto hostile = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
            if (!check(static_cast<bool>(hostile), "hostile leaf forwarding parses")) { continue; }
            auto error = ctcompile::ctnative::expandDOMHelpers(*hostile, "entry$0", 100000);
            if (control++ == 1) {
                unsigned invocations = 0;
                hostile->walk([&](ctjs::InvokeOp) { ++invocations; });
                check(!error && invocations == 2 && mlir::succeeded(mlir::verify(*hostile)),
                      "the original two-call control now retains both exceptional completions");
            } else {
                check(static_cast<bool>(error), "extra effects and method results refuse");
            }
            if (error) { llvm::consumeError(std::move(error)); }
        }
    }
    for (const auto & invalid :
         {replace(source, "    ctjs.return %value\n  }\n}",
                  "    %coerced = ctjs.unary plus %value\n    ctjs.return %coerced\n  }\n}"),
          replace(source, "    ctjs.return %value\n  }\n}",
                  "    ctjs.store_global \"effect\", %value\n    ctjs.return %value\n  }\n}"),
          replace(source, "    ctjs.return %value\n  }\n}", "    ctjs.throw %value\n  }\n}"),
          replace(
              source, "    ^normal(%result: !ctjs.value):",
              "    ^normal(%result: !ctjs.value):\n      ctjs.store_global \"effect\", %result"),
          replace(
              source,
              "    ^unwind(%error: !ctjs.value, %oldSaved: !ctjs.value, %oldValue: !ctjs.value):",
              "    ^unwind(%error: !ctjs.value, %oldSaved: !ctjs.value, %oldValue: !ctjs.value):\n "
              "     ctjs.store_global \"effect\", %error")}) {
        auto candidate = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        if (!check(static_cast<bool>(candidate), "hostile observed helper fixture parses")) {
            continue;
        }
        auto refused = ctcompile::ctnative::expandDOMHelpers(*candidate, "entry$0", 100000);
        check(static_cast<bool>(refused),
              "coercions, throws, body effects and effectful continuations refuse");
        if (refused) { llvm::consumeError(std::move(refused)); }
    }
    for (unsigned budget : {0u, used - 1}) {
        auto candidate = mlir::OwningOpRef<mlir::ModuleOp>(original->clone());
        auto refused = ctcompile::ctnative::expandDOMHelpers(*candidate, "entry$0", budget);
        check(static_cast<bool>(refused), "incomplete observed helper proof budget refuses");
        if (refused) { llvm::consumeError(std::move(refused)); }
    }
    check(printed(*original) == snapshot,
          "disposable helper attempts preserve the source snapshot");
}

static void testHelperCatchSource(mlir::MLIRContext & context) {
    const std::string source = R"js(function customElements(anchor) {
  function next() {
    const done = anchor.hasAttribute('data-yielded');
    anchor.setAttribute('data-next', done);
    anchor.setAttribute('data-yielded', 'yes');
    return {done: done, value: anchor};
  }
  try {
    const result = next();
    return result.done && result.value === anchor;
  } catch (error) { return error === anchor; }
}
)js";
    for (unsigned control = 0; control != 7; ++control) {
        auto text = source;
        if (control == 1) {
            replaceOnce(text, "'data-next'", "'bad name'");
        } else if (control == 2) {
            text.insert(text.find("    const done"), "    unknown();\n");
        } else if (control == 3) {
            replaceOnce(text, "result.done && result.value === anchor", "anchor.unproved");
        }
        auto module = import(context, text);
        if (!module) { continue; }
        if (control == 6) {
            auto function = module->lookupSymbol<ctjs::FuncOp>("customElements$1");
            auto at = mlir::OpBuilder::atBlockBegin(&function.getBody().front());
            mlir::ub::PoisonOp::create(at, function.getLoc(), ctjs::ValueType::get(&context));
        }
        const auto before = printed(*module);
        ctcompile::ctnative::HostContract contract;
        contract.provider = ctcompile::ctnative::HostContract::Provider::ctbrowserDOM;
        contract.entry = "customElements$1";
        if (control != 4) { contract.elementParameters = {0}; }
        contract.moduleSha256 = ctcompile::ctnative::hostContractFingerprint(*module);
        const auto fingerprint = contract.moduleSha256;
        auto error =
            ctcompile::ctnative::prepareDOMEntry(*module, contract, control == 5 ? 0 : 100000);
        if (control) {
            check(static_cast<bool>(error) && printed(*module) == before &&
                      contract.moduleSha256 == fingerprint,
                  "helper catch refuses unproved effects, receivers and budget without mutation");
        } else if (check(!error, "original next helper source completes its local catch")) {
            unsigned remaining = 0, calls = 0;
            module->walk([&](mlir::Operation * operation) {
                remaining += llvm::isa<ctjs::TryOp, ctjs::InvokeOp, ctjs::PushHandlerOp,
                                       ctjs::CheckOp, ctjs::CreateCellOp>(operation);
                calls += llvm::isa<ctjs::CallOp>(operation);
            });
            check(!remaining && calls == 3 && mlir::succeeded(mlir::verify(*module)),
                  "helper catch retains exactly its read and two ordered writes");
        }
        if (error) { llvm::errs() << llvm::toString(std::move(error)) << '\n'; }
    }
}

static void testRawIteratorHelpers(mlir::ModuleOp original,
                                   const ctcompile::ctnative::HostContract & contract) {
    mlir::OwningOpRef<mlir::ModuleOp> reference(original.clone());
    auto referenceProof = contract;
    auto referenceError = ctcompile::ctnative::prepareDOMEntry(*reference, referenceProof, 100000);
    if (!check(!referenceError,
               "original raw helpers establish exact prepared payload and effects")) {
        llvm::consumeError(std::move(referenceError));
        return;
    }
    const auto baseline = printed(*reference);
    auto * context = original.getContext();
    const std::array<std::pair<mlir::Attribute, bool>, 14> predicates{{
        {ctjs::NumberAttr::get(context, 0x0000000000000000ULL), false}, // +0
        {ctjs::NumberAttr::get(context, 0x8000000000000000ULL), false}, // -0
        {ctjs::NumberAttr::get(context, 0x7ff8000000000001ULL), false}, // quiet NaN
        {ctjs::NumberAttr::get(context, 0xfff0000000000001ULL), false}, // negative signaling NaN
        {ctjs::NumberAttr::get(context, 0x7ff0000000000000ULL), true},  // +Infinity
        {ctjs::NumberAttr::get(context, 0xfff0000000000000ULL), true},  // -Infinity
        {ctjs::NumberAttr::get(context, std::bit_cast<uint64_t>(1.0)), true},
        {ctjs::NumberAttr::get(context, std::bit_cast<uint64_t>(-1.0)), true},
        {ctjs::NumberAttr::get(context, 1), true}, // minimum positive subnormal
        {ctjs::StringAttr::get(context, ""), false},
        {ctjs::StringAttr::get(context, "0"), true},
        {ctjs::StringAttr::get(context, llvm::StringRef("\0", 1)), true},
        {ctjs::NullAttr::get(context), false},
        {ctjs::UndefinedAttr::get(context), false},
    }};
    const std::array<std::pair<mlir::Attribute, bool>, 9> negatedPredicates{{
        predicates[0],
        predicates[2],
        predicates[6],
        predicates[9],
        predicates[10],
        predicates[12],
        predicates[13],
        {ctjs::BooleanAttr::get(context, false), false},
        {ctjs::BooleanAttr::get(context, true), true},
    }};
    struct Comparison {
        ctjs::CompareKind kind;
        unsigned lhs;
        unsigned rhs;
        bool value;
    };
    const std::array<Comparison, 14> comparisons{{
        {ctjs::CompareKind::Eq, 0, 1, true},        // +0 == -0
        {ctjs::CompareKind::Eq, 6, 7, false},       // 1 == -1
        {ctjs::CompareKind::StrictEq, 0, 1, true},  // +0 === -0
        {ctjs::CompareKind::StrictEq, 2, 2, false}, // quiet NaN === quiet NaN
        {ctjs::CompareKind::Lt, 5, 8, true},        // -Infinity < minimum subnormal
        {ctjs::CompareKind::Lt, 3, 0, false},       // signaling NaN < +0
        {ctjs::CompareKind::Le, 1, 0, true},        // -0 <= +0
        {ctjs::CompareKind::Le, 2, 0, false},       // quiet NaN <= +0
        {ctjs::CompareKind::Gt, 8, 0, true},        // minimum subnormal > +0
        {ctjs::CompareKind::Gt, 0, 3, false},       // +0 > signaling NaN
        {ctjs::CompareKind::Ge, 4, 4, true},        // +Infinity >= +Infinity
        {ctjs::CompareKind::Ge, 0, 3, false},       // +0 >= signaling NaN
        {ctjs::CompareKind::Eq, 2, 0, false},       // quiet NaN == +0
        {ctjs::CompareKind::StrictEq, 8, 0, false}, // minimum subnormal === +0
    }};
    struct Arithmetic {
        ctjs::BinaryKind kind;
        uint64_t lhs;
        uint64_t rhs;
        uint64_t value;
        bool truth;
    };
    const std::array<Arithmetic, 12> arithmetic{{
        // Nearest-ties-even rounds both an even and an odd significand correctly.
        {ctjs::BinaryKind::Add, 0x3ff0000000000000ULL, 0x3ca0000000000000ULL, 0x3ff0000000000000ULL,
         true},
        {ctjs::BinaryKind::Add, 0x3ff0000000000001ULL, 0x3ca0000000000000ULL, 0x3ff0000000000002ULL,
         true},
        {ctjs::BinaryKind::Add, 0x8000000000000000ULL, 0x8000000000000000ULL, 0x8000000000000000ULL,
         false}, // -0 + -0
        {ctjs::BinaryKind::Sub, 0x3ff0000000000000ULL, 0x3ff0000000000000ULL, 0, false},
        {ctjs::BinaryKind::Mul, 0x8000000000000000ULL, 0x3ff0000000000000ULL, 0x8000000000000000ULL,
         false}, // -0 * 1
        {ctjs::BinaryKind::Mul, 0x7fefffffffffffffULL, 0x4000000000000000ULL, 0x7ff0000000000000ULL,
         true}, // finite overflow
        {ctjs::BinaryKind::Div, 0x3ff0000000000000ULL, 0x8000000000000000ULL, 0xfff0000000000000ULL,
         true}, // 1 / -0
        {ctjs::BinaryKind::Div, 0, 0, 0x7ff8000000000000ULL, false},
        {ctjs::BinaryKind::Div, 1, 0x4000000000000000ULL, 0, false}, // subnormal tie to zero
        {ctjs::BinaryKind::Mod, 0xc010000000000000ULL, 0x4000000000000000ULL, 0x8000000000000000ULL,
         false}, // -4 % 2
        {ctjs::BinaryKind::Mod, 0x4016000000000000ULL, 0x4000000000000000ULL, 0x3ff8000000000000ULL,
         true}, // 5.5 % 2 is 1.5, not IEEE remainder -0.5
        {ctjs::BinaryKind::Add, 0xfff0000000000001ULL, 0x3ff0000000000000ULL, 0xfff8000000000001ULL,
         false}, // signaling NaN becomes quiet, retaining its payload
    }};
    const std::array<Arithmetic, 30> powers{{
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(2.0), std::bit_cast<uint64_t>(3.0),
         std::bit_cast<uint64_t>(8.0), true},
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(-2.0), std::bit_cast<uint64_t>(3.0),
         std::bit_cast<uint64_t>(-8.0), true},
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(-2.0), std::bit_cast<uint64_t>(4.0),
         std::bit_cast<uint64_t>(16.0), true},
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(-2.0), std::bit_cast<uint64_t>(-3.0),
         std::bit_cast<uint64_t>(-0.125), true},
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(-4.0), std::bit_cast<uint64_t>(0.5),
         0x7ff8000000000000ULL, false},
        {ctjs::BinaryKind::Pow, 0x8000000000000000ULL, std::bit_cast<uint64_t>(3.0),
         0x8000000000000000ULL, false},
        {ctjs::BinaryKind::Pow, 0x8000000000000000ULL, std::bit_cast<uint64_t>(2.0), 0, false},
        {ctjs::BinaryKind::Pow, 0x8000000000000000ULL, std::bit_cast<uint64_t>(-3.0),
         0xfff0000000000000ULL, true},
        {ctjs::BinaryKind::Pow, 0x8000000000000000ULL, std::bit_cast<uint64_t>(-2.0),
         0x7ff0000000000000ULL, true},
        {ctjs::BinaryKind::Pow, 0xfff0000000000001ULL, 0, std::bit_cast<uint64_t>(1.0), true},
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(1.0), 0xfff0000000000001ULL,
         0x7ff8000000000000ULL, false},
        {ctjs::BinaryKind::Pow, 0xfff8000000000042ULL, std::bit_cast<uint64_t>(1.0),
         0x7ff8000000000000ULL, false},
        // JavaScript returns NaN for |base| == 1 with either infinite exponent.
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(1.0), 0x7ff0000000000000ULL,
         0x7ff8000000000000ULL, false},
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(-1.0), 0xfff0000000000000ULL,
         0x7ff8000000000000ULL, false},
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(2.0), 0x7ff0000000000000ULL,
         0x7ff0000000000000ULL, true},
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(2.0), 0xfff0000000000000ULL, 0, false},
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(0.5), 0x7ff0000000000000ULL, 0, false},
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(0.5), 0xfff0000000000000ULL,
         0x7ff0000000000000ULL, true},
        {ctjs::BinaryKind::Pow, 0xfff0000000000000ULL, std::bit_cast<uint64_t>(3.0),
         0xfff0000000000000ULL, true},
        {ctjs::BinaryKind::Pow, 0xfff0000000000000ULL, std::bit_cast<uint64_t>(2.0),
         0x7ff0000000000000ULL, true},
        {ctjs::BinaryKind::Pow, 0xfff0000000000000ULL, std::bit_cast<uint64_t>(-3.0),
         0x8000000000000000ULL, false},
        {ctjs::BinaryKind::Pow, 0xfff0000000000000ULL, std::bit_cast<uint64_t>(-2.0), 0, false},
        {ctjs::BinaryKind::Pow, 0xfff0000000000000ULL, std::bit_cast<uint64_t>(0.5),
         0x7ff0000000000000ULL, true},
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(2.0), std::bit_cast<uint64_t>(1024.0),
         0x7ff0000000000000ULL, true},
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(2.0), std::bit_cast<uint64_t>(-1074.0), 1,
         true},
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(2.0), std::bit_cast<uint64_t>(-1075.0), 0,
         false},
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(-1.0),
         std::bit_cast<uint64_t>(9007199254740991.0), std::bit_cast<uint64_t>(-1.0), true},
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(-1.0),
         std::bit_cast<uint64_t>(9007199254740992.0), std::bit_cast<uint64_t>(1.0), true},
        {ctjs::BinaryKind::Pow, std::bit_cast<uint64_t>(4.0), std::bit_cast<uint64_t>(0.5),
         std::bit_cast<uint64_t>(2.0), true},
        {ctjs::BinaryKind::Pow, 0, 0x8000000000000000ULL, std::bit_cast<uint64_t>(1.0), true},
    }};
    struct NumericUnary {
        ctjs::UnaryKind kind;
        uint64_t operand;
        uint64_t value;
        bool truth;
    };
    const std::array<NumericUnary, 12> numericUnaries{{
        {ctjs::UnaryKind::Plus, 0, 0, false},
        {ctjs::UnaryKind::Plus, 0x8000000000000000ULL, 0x8000000000000000ULL, false},
        {ctjs::UnaryKind::Plus, 0xfff8000000000042ULL, 0xfff8000000000042ULL, false},
        {ctjs::UnaryKind::Plus, 0xfff0000000000001ULL, 0xfff0000000000001ULL, false},
        {ctjs::UnaryKind::Plus, 0x7ff0000000000000ULL, 0x7ff0000000000000ULL, true},
        {ctjs::UnaryKind::Plus, 1, 1, true},
        {ctjs::UnaryKind::Neg, 0, 0x8000000000000000ULL, false},
        {ctjs::UnaryKind::Neg, 0x8000000000000000ULL, 0, false},
        {ctjs::UnaryKind::Neg, 0x7ff8000000000042ULL, 0xfff8000000000042ULL, false},
        {ctjs::UnaryKind::Neg, 0xfff0000000000001ULL, 0x7ff0000000000001ULL, false},
        {ctjs::UnaryKind::Neg, 0x7ff0000000000000ULL, 0xfff0000000000000ULL, true},
        {ctjs::UnaryKind::Neg, 0x8000000000000001ULL, 1, true},
    }};
    const std::array<NumericUnary, 12> bitwiseUnaries{{
        {ctjs::UnaryKind::BitNot, 0, std::bit_cast<uint64_t>(-1.0), true},
        {ctjs::UnaryKind::BitNot, 0x8000000000000000ULL, std::bit_cast<uint64_t>(-1.0), true},
        {ctjs::UnaryKind::BitNot, 0xfff0000000000001ULL, std::bit_cast<uint64_t>(-1.0), true},
        {ctjs::UnaryKind::BitNot, 0x7ff0000000000000ULL, std::bit_cast<uint64_t>(-1.0), true},
        {ctjs::UnaryKind::BitNot, 0xfff0000000000000ULL, std::bit_cast<uint64_t>(-1.0), true},
        {ctjs::UnaryKind::BitNot, 1, std::bit_cast<uint64_t>(-1.0), true},
        {ctjs::UnaryKind::BitNot, std::bit_cast<uint64_t>(-1.0), 0, false},
        {ctjs::UnaryKind::BitNot, std::bit_cast<uint64_t>(4294967296.0),
         std::bit_cast<uint64_t>(-1.0), true},
        {ctjs::UnaryKind::BitNot, std::bit_cast<uint64_t>(4294967295.0), 0, false},
        {ctjs::UnaryKind::BitNot, std::bit_cast<uint64_t>(2147483648.0),
         std::bit_cast<uint64_t>(2147483647.0), true},
        {ctjs::UnaryKind::BitNot, std::bit_cast<uint64_t>(3.9), std::bit_cast<uint64_t>(-4.0),
         true},
        {ctjs::UnaryKind::BitNot, std::bit_cast<uint64_t>(-3.9), std::bit_cast<uint64_t>(2.0),
         true},
    }};
    const std::array<Arithmetic, 18> staticArithmetic{{
        {ctjs::BinaryKind::Add, 0x3ff0000000000000ULL, 0x3ca0000000000000ULL, 0x3ff0000000000000ULL,
         true}, // static Add retains binary64 ties-to-even
        {ctjs::BinaryKind::BitAnd, std::bit_cast<uint64_t>(4294967295.0),
         std::bit_cast<uint64_t>(2147483648.0), std::bit_cast<uint64_t>(-2147483648.0), true},
        {ctjs::BinaryKind::BitAnd, std::bit_cast<uint64_t>(-1.9),
         std::bit_cast<uint64_t>(4294967298.9), std::bit_cast<uint64_t>(2.0), true},
        {ctjs::BinaryKind::BitOr, 0x8000000000000000ULL, 0x7ff8000000000001ULL, 0, false},
        {ctjs::BinaryKind::BitOr, std::bit_cast<uint64_t>(4294967296.0),
         std::bit_cast<uint64_t>(1.0), std::bit_cast<uint64_t>(1.0), true},
        {ctjs::BinaryKind::BitXor, std::bit_cast<uint64_t>(4294967295.0),
         std::bit_cast<uint64_t>(-1.0), 0, false},
        {ctjs::BinaryKind::BitXor, 0x7ff0000000000000ULL, std::bit_cast<uint64_t>(3.9),
         std::bit_cast<uint64_t>(3.0), true},
        {ctjs::BinaryKind::Shl, std::bit_cast<uint64_t>(2147483647.0), std::bit_cast<uint64_t>(1.0),
         std::bit_cast<uint64_t>(-2.0), true},
        {ctjs::BinaryKind::Shl, std::bit_cast<uint64_t>(1.0), std::bit_cast<uint64_t>(-1.0),
         std::bit_cast<uint64_t>(-2147483648.0), true},
        {ctjs::BinaryKind::Shr, std::bit_cast<uint64_t>(-2147483648.0),
         std::bit_cast<uint64_t>(33.0), std::bit_cast<uint64_t>(-1073741824.0), true},
        {ctjs::BinaryKind::Shr, std::bit_cast<uint64_t>(-3.9), std::bit_cast<uint64_t>(1.9),
         std::bit_cast<uint64_t>(-2.0), true},
        {ctjs::BinaryKind::UShr, std::bit_cast<uint64_t>(-1.0), 0,
         std::bit_cast<uint64_t>(4294967295.0), true},
        {ctjs::BinaryKind::UShr, std::bit_cast<uint64_t>(-1.0), std::bit_cast<uint64_t>(-1.0),
         std::bit_cast<uint64_t>(1.0), true},
        {ctjs::BinaryKind::Shl, std::bit_cast<uint64_t>(1.0), std::bit_cast<uint64_t>(32.0),
         std::bit_cast<uint64_t>(1.0), true},
        {ctjs::BinaryKind::Shr, std::bit_cast<uint64_t>(-1.0), 0x7ff8000000000001ULL,
         std::bit_cast<uint64_t>(-1.0), true},
        {ctjs::BinaryKind::UShr, std::bit_cast<uint64_t>(-1.0), 0x7ff0000000000000ULL,
         std::bit_cast<uint64_t>(4294967295.0), true},
        {ctjs::BinaryKind::Shl, std::bit_cast<uint64_t>(1.0), std::bit_cast<uint64_t>(4294967327.0),
         std::bit_cast<uint64_t>(-2147483648.0), true},
        {ctjs::BinaryKind::UShr, std::bit_cast<uint64_t>(4294967296.0),
         std::bit_cast<uint64_t>(1.0), 0, false},
    }};
    const std::array<mlir::Attribute, 99> equalityValues{{
        ctjs::BooleanAttr::get(context, false),
        ctjs::BooleanAttr::get(context, true),
        ctjs::NullAttr::get(context),
        ctjs::UndefinedAttr::get(context),
        ctjs::StringAttr::get(context, ""),
        ctjs::StringAttr::get(context, "0"),
        ctjs::StringAttr::get(context, llvm::StringRef("\0", 1)),
        ctjs::StringAttr::get(context, llvm::StringRef("a\0b", 3)),
        ctjs::StringAttr::get(context, "a"),
        ctjs::StringAttr::get(context, "\xc3\xa9"),
        ctjs::StringAttr::get(context, "e\xcc\x81"),
        predicates[0].first,
        predicates[6].first,
        predicates[2].first,
        ctjs::StringAttr::get(context, "\xf0\x9f\x98\x80"),
        ctjs::StringAttr::get(context, "\xf0\x9f\x98\x81"),
        predicates[1].first,
        predicates[7].first,
        predicates[4].first,
        predicates[8].first,
        ctjs::StringAttr::get(context, "\t\n\r \xc2\xa0\xe2\x80\xa8\xef\xbb\xbf"),
        ctjs::StringAttr::get(context, "0x10"),
        ctjs::StringAttr::get(context, "0O10"),
        ctjs::StringAttr::get(context, "0B11"),
        ctjs::StringAttr::get(context, "-0"),
        ctjs::StringAttr::get(context, ".5"),
        ctjs::StringAttr::get(context, "1e2"),
        ctjs::StringAttr::get(context, "NaN"),
        ctjs::StringAttr::get(context, "+Infinity"),
        ctjs::StringAttr::get(context, "garbage"),
        ctjs::StringAttr::get(context, "1"),
        ctjs::StringAttr::get(context, "-Infinity"),
        ctjs::StringAttr::get(context, "1e309"),
        ctjs::StringAttr::get(context, "1e-400"),
        ctjs::StringAttr::get(context, std::string(31, '0') + "1"),
        ctjs::StringAttr::get(context, "1junk"),
        ctjs::StringAttr::get(context, std::string(33, ' ')),
        ctjs::StringAttr::get(context, "0x10000000000000000"),
        ctjs::StringAttr::get(context, "nan"),
        ctjs::StringAttr::get(context, "."),
        ctjs::StringAttr::get(context, "+0x1"),
        ctjs::StringAttr::get(context, "1e2147483647"),
        ctjs::StringAttr::get(context, "1e-"),
        ctjs::StringAttr::get(context, "10"),
        ctjs::StringAttr::get(context, "2"),
        ctjs::NumberAttr::get(context, std::bit_cast<uint64_t>(16.0)),
        ctjs::NumberAttr::get(context, std::bit_cast<uint64_t>(8.0)),
        ctjs::NumberAttr::get(context, std::bit_cast<uint64_t>(3.0)),
        ctjs::NumberAttr::get(context, std::bit_cast<uint64_t>(0.5)),
        ctjs::NumberAttr::get(context, std::bit_cast<uint64_t>(100.0)),
        ctjs::NumberAttr::get(context, std::bit_cast<uint64_t>(17.0)),
        ctjs::StringAttr::get(context, "\x7f"),
        ctjs::StringAttr::get(context, "\xc2\x80"),
        ctjs::StringAttr::get(context, "\xc3\xa8"),
        ctjs::StringAttr::get(context, "\xef\xbf\xbf"),
        ctjs::StringAttr::get(context, "\xe0\xa0\x80"),
        ctjs::StringAttr::get(context, "\xef\xbf\xbd"),
        ctjs::StringAttr::get(context, "\xed\xa0\x80"),
        ctjs::StringAttr::get(context, "\xed\xb0\x80"),
        ctjs::StringAttr::get(context, "\xf0\x90\x80\x80"),
        ctjs::StringAttr::get(context, "ab"),
        ctjs::StringAttr::get(context, std::string(64, 'a')),
        ctjs::StringAttr::get(context, "b"),
        ctjs::StringAttr::get(context, "\xc0\x80"),
        ctjs::StringAttr::get(context, "\xc2"),
        ctjs::StringAttr::get(context, "\x80"),
        ctjs::StringAttr::get(context, "\xf4\x90\x80\x80"),
        ctjs::StringAttr::get(context, "\xed\xa0\x80\xed\xb0\x80"),
        ctjs::StringAttr::get(context, "\xe2\x28\xa1"),
        ctjs::StringAttr::get(context, "\xf0\x9f\x98"),
        ctjs::StringAttr::get(context, "\xe0\x80\x80"),
        ctjs::StringAttr::get(context, "\xf0\x80\x80\x80"),
        ctjs::StringAttr::get(context, "\xff"),
        ctjs::StringAttr::get(context, "\xe2\x82"),
        ctjs::StringAttr::get(context, "a\xf0\x90\x80\x80"),
        ctjs::StringAttr::get(context, "a\xef\xbf\xbf"),
        ctjs::StringAttr::get(context, std::string(64, '0') + "1"),
        ctjs::StringAttr::get(context, "0x" + std::string(64, '0') + "10"),
        ctjs::StringAttr::get(context, "\xc2\xa0" + std::string(64, ' ') + "+Infinity\xef\xbb\xbf"),
        ctjs::StringAttr::get(context, std::string(64, ' ')),
        ctjs::StringAttr::get(context, std::string(64, '0') + "1.e309"),
        ctjs::StringAttr::get(context, "-0." + std::string(64, '0') + "1e-400"),
        ctjs::StringAttr::get(context, "1" + std::string(64, '0') + "e-64"),
        ctjs::StringAttr::get(context, "0." + std::string(64, '0') + "1e65"),
        ctjs::StringAttr::get(context, "1" + std::string(64, '0') + "e2147483600"),
        ctjs::StringAttr::get(context, "0x" + std::string(64, 'f')),
        ctjs::StringAttr::get(context, std::string(64, '0') + "1junk"),
        ctjs::StringAttr::get(context, std::string("1\0", 2) + std::string(64, '0')),
        ctjs::StringAttr::get(context, "0b" + std::string(64, '0') + "1"),
        ctjs::StringAttr::get(context, std::string(64, ' ') + "NaN"),
        predicates[3].first,
        predicates[5].first,
        ctjs::NumberAttr::get(context, std::bit_cast<uint64_t>(1e-7)),
        ctjs::NumberAttr::get(context, std::bit_cast<uint64_t>(1e-6)),
        ctjs::NumberAttr::get(context, std::bit_cast<uint64_t>(1e20)),
        ctjs::NumberAttr::get(context, std::bit_cast<uint64_t>(1e21)),
        ctjs::NumberAttr::get(context, 0x7fefffffffffffffULL),
        ctjs::NumberAttr::get(context, std::bit_cast<uint64_t>(9007199254740992.0)),
        ctjs::NumberAttr::get(context, std::bit_cast<uint64_t>(-1.25)),
    }};
    const std::array<Comparison, 27> equalities{{
        {ctjs::CompareKind::StrictEq, 0, 0, true},    {ctjs::CompareKind::StrictEq, 1, 0, false},
        {ctjs::CompareKind::StrictEq, 2, 2, true},    {ctjs::CompareKind::StrictEq, 3, 3, true},
        {ctjs::CompareKind::StrictEq, 2, 3, false},   {ctjs::CompareKind::StrictEq, 3, 2, false},
        {ctjs::CompareKind::StrictEq, 4, 4, true},    {ctjs::CompareKind::StrictEq, 5, 4, false},
        {ctjs::CompareKind::StrictEq, 6, 6, true},    {ctjs::CompareKind::StrictEq, 6, 4, false},
        {ctjs::CompareKind::StrictEq, 7, 8, false},   {ctjs::CompareKind::StrictEq, 9, 9, true},
        {ctjs::CompareKind::StrictEq, 9, 10, false},  {ctjs::CompareKind::StrictEq, 14, 14, true},
        {ctjs::CompareKind::StrictEq, 14, 15, false}, {ctjs::CompareKind::StrictEq, 0, 11, false},
        {ctjs::CompareKind::StrictEq, 1, 12, false},  {ctjs::CompareKind::StrictEq, 4, 11, false},
        {ctjs::CompareKind::StrictEq, 5, 11, false},  {ctjs::CompareKind::StrictEq, 11, 2, false},
        {ctjs::CompareKind::StrictEq, 3, 13, false},  {ctjs::CompareKind::Eq, 4, 4, true},
        {ctjs::CompareKind::Eq, 5, 4, false},         {ctjs::CompareKind::Eq, 6, 6, true},
        {ctjs::CompareKind::Eq, 7, 8, false},         {ctjs::CompareKind::Eq, 9, 9, true},
        {ctjs::CompareKind::Eq, 9, 10, false},
    }};
    const std::array<Comparison, 28> looseEqualities{{
        {ctjs::CompareKind::Eq, 1, 1, true},   // true == true
        {ctjs::CompareKind::Eq, 1, 0, false},  // true == false
        {ctjs::CompareKind::Eq, 2, 2, true},   // null == null
        {ctjs::CompareKind::Eq, 3, 2, true},   // undefined == null
        {ctjs::CompareKind::Eq, 11, 0, true},  // +0 == false
        {ctjs::CompareKind::Eq, 1, 12, true},  // true == 1
        {ctjs::CompareKind::Eq, 12, 1, true},  // 1 == true
        {ctjs::CompareKind::Eq, 0, 16, true},  // false == -0
        {ctjs::CompareKind::Eq, 16, 0, true},  // -0 == false
        {ctjs::CompareKind::Eq, 1, 17, false}, // true == -1
        {ctjs::CompareKind::Eq, 17, 1, false}, // -1 == true
        {ctjs::CompareKind::Eq, 0, 13, false}, // false == NaN
        {ctjs::CompareKind::Eq, 13, 0, false}, // NaN == false
        {ctjs::CompareKind::Eq, 1, 18, false}, // true == Infinity
        {ctjs::CompareKind::Eq, 18, 1, false}, // Infinity == true
        {ctjs::CompareKind::Eq, 0, 19, false}, // false == minimum subnormal
        {ctjs::CompareKind::Eq, 19, 0, false}, // minimum subnormal == false
        {ctjs::CompareKind::Eq, 0, 2, false},  // false == null
        {ctjs::CompareKind::Eq, 2, 0, false},  // null == false
        {ctjs::CompareKind::Eq, 1, 3, false},  // true == undefined
        {ctjs::CompareKind::Eq, 3, 1, false},  // undefined == true
        {ctjs::CompareKind::Eq, 11, 2, false}, // +0 == null
        {ctjs::CompareKind::Eq, 2, 11, false}, // null == +0
        {ctjs::CompareKind::Eq, 11, 3, false}, // +0 == undefined
        {ctjs::CompareKind::Eq, 3, 11, false}, // undefined == +0
        {ctjs::CompareKind::Eq, 13, 2, false}, // NaN == null
        {ctjs::CompareKind::Eq, 2, 13, false}, // null == NaN
        {ctjs::CompareKind::Eq, 1, 11, false}, // true == +0
    }};
    const std::array<Comparison, 50> convertedComparisons{{
        {ctjs::CompareKind::Eq, 4, 11, true},   // empty String == +0
        {ctjs::CompareKind::Eq, 11, 4, true},   // +0 == empty String
        {ctjs::CompareKind::Eq, 20, 0, true},   // JS whitespace == false
        {ctjs::CompareKind::Eq, 0, 20, true},   // false == JS whitespace
        {ctjs::CompareKind::Eq, 21, 45, true},  // 0x10 == 16
        {ctjs::CompareKind::Eq, 45, 21, true},  // 16 == 0x10
        {ctjs::CompareKind::Eq, 22, 46, true},  // 0O10 == 8
        {ctjs::CompareKind::Eq, 47, 23, true},  // 3 == 0B11
        {ctjs::CompareKind::Eq, 24, 16, true},  // -0 String == -0 Number
        {ctjs::CompareKind::Eq, 16, 24, true},  // -0 Number == -0 String
        {ctjs::CompareKind::Eq, 25, 48, true},  // .5 == 0.5
        {ctjs::CompareKind::Eq, 49, 26, true},  // 100 == 1e2
        {ctjs::CompareKind::Eq, 27, 13, false}, // NaN String == NaN Number
        {ctjs::CompareKind::Eq, 13, 27, false}, // NaN Number == NaN String
        {ctjs::CompareKind::Eq, 28, 18, true},  // +Infinity String == Infinity
        {ctjs::CompareKind::Eq, 18, 28, true},  // Infinity == +Infinity String
        {ctjs::CompareKind::Eq, 29, 2, false},  // invalid numeric String == null needs no parse
        {ctjs::CompareKind::Eq, 3, 29, false},  // undefined == invalid numeric String
        {ctjs::CompareKind::Lt, 21, 50, true},  // 0x10 < 17
        {ctjs::CompareKind::Gt, 50, 21, true},  // 17 > 0x10
        {ctjs::CompareKind::Le, 26, 49, true},  // 1e2 <= 100
        {ctjs::CompareKind::Ge, 49, 26, true},  // 100 >= 1e2
        {ctjs::CompareKind::Gt, 1, 25, true},   // true > .5
        {ctjs::CompareKind::Lt, 25, 1, true},   // .5 < true
        {ctjs::CompareKind::Lt, 2, 30, true},   // null < 1 String
        {ctjs::CompareKind::Gt, 30, 2, true},   // 1 String > null
        {ctjs::CompareKind::Lt, 3, 30, false},  // undefined < 1 String
        {ctjs::CompareKind::Ge, 30, 3, false},  // 1 String >= undefined
        {ctjs::CompareKind::Ge, 0, 2, true},    // false >= null
        {ctjs::CompareKind::Le, 2, 0, true},    // null <= false
        {ctjs::CompareKind::Gt, 1, 0, true},    // true > false
        {ctjs::CompareKind::Lt, 0, 1, true},    // false < true
        {ctjs::CompareKind::Le, 27, 12, false}, // NaN String <= 1
        {ctjs::CompareKind::Ge, 12, 27, false}, // 1 >= NaN String
        {ctjs::CompareKind::Lt, 31, 0, true},   // -Infinity String < false
        {ctjs::CompareKind::Gt, 1, 31, true},   // true > -Infinity String
        {ctjs::CompareKind::Eq, 32, 18, true},  // decimal overflow == Infinity
        {ctjs::CompareKind::Eq, 33, 11, true},  // decimal underflow == +0
        {ctjs::CompareKind::Eq, 34, 1, true},   // exactly 32 source bytes == true
        {ctjs::CompareKind::Eq, 1, 34, true},   // true == exactly 32 source bytes
        // Invalid conversions refuse even if JS would give NaN. The whitespace
        // row deliberately selects the wrong result and must retain live effects.
        {ctjs::CompareKind::Eq, 35, 11, false}, // trailing junk
        {ctjs::CompareKind::Eq, 11, 36, false}, // 33 spaces convert to zero, contrary to this arm
        {ctjs::CompareKind::Eq, 37, 18, false}, // Core radix overflow
        {ctjs::CompareKind::Eq, 38, 13, false}, // noncanonical NaN spelling
        {ctjs::CompareKind::Gt, 39, 11, false}, // no decimal digits
        {ctjs::CompareKind::Lt, 11, 40, false}, // signed radix literal
        {ctjs::CompareKind::Ge, 41, 11, false}, // exponent beyond safe parser bound
        {ctjs::CompareKind::Le, 42, 11, false}, // incomplete exponent
        {ctjs::CompareKind::Eq, 6, 11, false},  // embedded NUL
        {ctjs::CompareKind::Eq, 9, 11, false},  // nonnumeric UTF-8
    }};
    const std::array<Comparison, 60> stringComparisons{{
        {ctjs::CompareKind::Lt, 43, 44, true}, // "10" < "2", lexical rather than numeric
        {ctjs::CompareKind::Le, 43, 44, true},
        {ctjs::CompareKind::Gt, 43, 44, false},
        {ctjs::CompareKind::Ge, 43, 44, false},
        {ctjs::CompareKind::Lt, 44, 43, false},
        {ctjs::CompareKind::Le, 44, 43, false},
        {ctjs::CompareKind::Gt, 44, 43, true},
        {ctjs::CompareKind::Ge, 44, 43, true},
        {ctjs::CompareKind::Lt, 4, 4, false}, // empty Strings
        {ctjs::CompareKind::Le, 4, 4, true},
        {ctjs::CompareKind::Gt, 8, 8, false}, // equal nonempty Strings
        {ctjs::CompareKind::Ge, 8, 8, true},
        {ctjs::CompareKind::Lt, 4, 8, true}, // empty prefix
        {ctjs::CompareKind::Gt, 8, 4, true},
        {ctjs::CompareKind::Lt, 8, 60, true}, // "a" is a prefix of "ab"
        {ctjs::CompareKind::Ge, 60, 8, true},
        {ctjs::CompareKind::Lt, 8, 7, true}, // "a" is a prefix of "a\0b"
        {ctjs::CompareKind::Gt, 7, 8, true},
        {ctjs::CompareKind::Lt, 6, 4, false}, // NUL is not an empty String
        {ctjs::CompareKind::Gt, 6, 4, true},
        {ctjs::CompareKind::Lt, 7, 60, true},  // embedded NUL precedes 'b'
        {ctjs::CompareKind::Lt, 51, 52, true}, // U+007F < U+0080, unsigned bytes
        {ctjs::CompareKind::Gt, 52, 51, true},
        {ctjs::CompareKind::Lt, 53, 9, true}, // U+00E8 < U+00E9
        {ctjs::CompareKind::Gt, 9, 53, true},
        {ctjs::CompareKind::Lt, 10, 9, true}, // no Unicode normalization
        {ctjs::CompareKind::Ge, 9, 10, true},
        {ctjs::CompareKind::Lt, 14, 15, true}, // supplementary code points agree
        {ctjs::CompareKind::Gt, 15, 14, true},
        {ctjs::CompareKind::Le, 14, 14, true},
        {ctjs::CompareKind::Ge, 14, 14, true},
        {ctjs::CompareKind::Lt, 55, 54, true}, // three-byte BMP code points
        {ctjs::CompareKind::Lt, 57, 58, true}, // lone high surrogate < lone low surrogate
        {ctjs::CompareKind::Ge, 58, 57, true},
        {ctjs::CompareKind::Lt, 57, 59, true}, // lone high surrogate prefixes U+10000
        {ctjs::CompareKind::Ge, 59, 57, true},
        {ctjs::CompareKind::Lt, 61, 62, true}, // String ordering has no 32-byte numeric limit
        {ctjs::CompareKind::Gt, 62, 61, true},
        {ctjs::CompareKind::Lt, 56, 54, true}, // literal replacement character is canonical
        {ctjs::CompareKind::Le, 7, 7, true},   // equality includes embedded NUL
        // Canonical inputs whose UTF-16 ordering disagrees with their byte order.
        {ctjs::CompareKind::Lt, 59, 54, true},
        {ctjs::CompareKind::Le, 59, 54, true},
        {ctjs::CompareKind::Gt, 59, 54, false},
        {ctjs::CompareKind::Ge, 59, 54, false},
        {ctjs::CompareKind::Lt, 54, 59, false},
        {ctjs::CompareKind::Gt, 54, 59, true},
        {ctjs::CompareKind::Lt, 58, 59, false},
        {ctjs::CompareKind::Lt, 74, 75, true}, // disagreement after a common ASCII prefix
        // The public decoder is total; a lossy/noncanonical roundtrip is refused.
        {ctjs::CompareKind::Lt, 63, 4, false},  // overlong NUL
        {ctjs::CompareKind::Ge, 8, 64, false},  // truncated two-byte rhs
        {ctjs::CompareKind::Gt, 65, 8, false},  // orphan continuation
        {ctjs::CompareKind::Le, 8, 66, false},  // code point above U+10FFFF
        {ctjs::CompareKind::Lt, 67, 59, false}, // separately encoded adjacent surrogates
        {ctjs::CompareKind::Le, 67, 67, true},  // even identical CESU-8 is noncanonical
        {ctjs::CompareKind::Ge, 68, 8, false},  // invalid continuation inside a sequence
        {ctjs::CompareKind::Lt, 69, 8, false},  // truncated four-byte sequence
        {ctjs::CompareKind::Gt, 70, 8, false},  // overlong three-byte sequence
        {ctjs::CompareKind::Le, 71, 8, false},  // overlong four-byte sequence
        {ctjs::CompareKind::Gt, 72, 8, false},  // invalid leading byte
        {ctjs::CompareKind::Lt, 8, 73, false},  // truncated three-byte rhs
    }};
    const std::array<Comparison, 16> wideComparisons{{
        {ctjs::CompareKind::Eq, 76, 1, true},   // long decimal == true
        {ctjs::CompareKind::Ge, 1, 76, true},   // reversed numeric conversion
        {ctjs::CompareKind::Eq, 77, 45, true},  // long hexadecimal == 16
        {ctjs::CompareKind::Eq, 78, 18, true},  // JS whitespace around Infinity
        {ctjs::CompareKind::Eq, 79, 0, true},   // whitespace-only == false
        {ctjs::CompareKind::Eq, 80, 18, true},  // trailing-point decimal overflow
        {ctjs::CompareKind::Eq, 81, 16, true},  // negative decimal underflow
        {ctjs::CompareKind::Eq, 82, 12, true},  // mantissa/exponent cancellation
        {ctjs::CompareKind::Eq, 83, 12, true},  // fractional/exponent cancellation
        {ctjs::CompareKind::Eq, 88, 1, true},   // long binary == true
        {ctjs::CompareKind::Ge, 89, 12, false}, // canonical NaN stays unordered
        {ctjs::CompareKind::Eq, 76, 2, false},  // absence needs no conversion
        // Complete grammar and Core arithmetic safety remain required even
        // when JavaScript's selected result happens to be predictable.
        {ctjs::CompareKind::Eq, 84, 11, false}, // exponent exceeds size-dependent bound
        {ctjs::CompareKind::Eq, 85, 11, false}, // Core radix overflow is not JS Number
        {ctjs::CompareKind::Eq, 86, 11, false}, // trailing junk
        {ctjs::CompareKind::Eq, 87, 11, false}, // embedded NUL
    }};
    struct CoercingUnary {
        ctjs::UnaryKind kind;
        unsigned operand;
        uint64_t value;
        bool truth;
    };
    const std::array<CoercingUnary, 26> coercingUnaries{{
        {ctjs::UnaryKind::Plus, 24, 0x8000000000000000ULL, false}, // String -0
        {ctjs::UnaryKind::Neg, 24, 0, false},
        {ctjs::UnaryKind::Plus, 4, 0, false},                    // empty String
        {ctjs::UnaryKind::Neg, 2, 0x8000000000000000ULL, false}, // null
        {ctjs::UnaryKind::Plus, 1, std::bit_cast<uint64_t>(1.0), true},
        {ctjs::UnaryKind::Neg, 1, std::bit_cast<uint64_t>(-1.0), true},
        {ctjs::UnaryKind::BitNot, 0, std::bit_cast<uint64_t>(-1.0), true},
        {ctjs::UnaryKind::Plus, 3, 0x7ff8000000000000ULL, false}, // undefined
        {ctjs::UnaryKind::Neg, 3, 0xfff8000000000000ULL, false},
        {ctjs::UnaryKind::BitNot, 3, std::bit_cast<uint64_t>(-1.0), true},
        {ctjs::UnaryKind::BitNot, 23, std::bit_cast<uint64_t>(-4.0), true}, // 0B11
        {ctjs::UnaryKind::BitNot, 27, std::bit_cast<uint64_t>(-1.0), true}, // NaN
        {ctjs::UnaryKind::BitNot, 32, std::bit_cast<uint64_t>(-1.0), true}, // overflow
        {ctjs::UnaryKind::BitNot, 88, std::bit_cast<uint64_t>(-2.0), true}, // long binary
        {ctjs::UnaryKind::Plus, 76, std::bit_cast<uint64_t>(1.0), true},    // long decimal
        {ctjs::UnaryKind::Neg, 78, 0xfff0000000000000ULL, true},            // JS space/Infinity
        {ctjs::UnaryKind::Plus, 81, 0x8000000000000000ULL, false},          // underflow
        {ctjs::UnaryKind::Plus, 22, std::bit_cast<uint64_t>(8.0), true},    // octal
        {ctjs::UnaryKind::BitNot, 25, std::bit_cast<uint64_t>(-1.0), true}, // fraction
        {ctjs::UnaryKind::Plus, 20, 0, false},                              // JS whitespace
        // Grammar and Core overflow refusals remain, even for predictable NaNs.
        {ctjs::UnaryKind::Plus, 29, 0, false},   // garbage
        {ctjs::UnaryKind::Neg, 35, 0, false},    // trailing junk
        {ctjs::UnaryKind::BitNot, 40, 0, false}, // signed radix
        {ctjs::UnaryKind::Plus, 85, 0, false},   // radix overflow
        {ctjs::UnaryKind::Neg, 84, 0, false},    // unsafe exponent
        {ctjs::UnaryKind::BitNot, 87, 0, false}, // embedded NUL
    }};
    struct CoercingBinary {
        ctjs::BinaryKind kind;
        unsigned lhs;
        unsigned rhs;
        uint64_t value;
        bool truth;
        bool staticForm = false;
    };
    const std::array<CoercingBinary, 27> coercingBinaries{{
        {ctjs::BinaryKind::Sub, 77, 1, std::bit_cast<uint64_t>(15.0), true}, // long hex - true
        {ctjs::BinaryKind::Sub, 2, 25, std::bit_cast<uint64_t>(-0.5), true}, // null - String .5
        {ctjs::BinaryKind::Mul, 24, 1, 0x8000000000000000ULL, false},        // String -0 * true
        {ctjs::BinaryKind::Mul, 1, 32, 0x7ff0000000000000ULL, true},         // true * overflow
        {ctjs::BinaryKind::Div, 1, 24, 0xfff0000000000000ULL, true},         // true / String -0
        {ctjs::BinaryKind::Div, 2, 0, 0x7ff8000000000000ULL, false},         // null / false
        {ctjs::BinaryKind::Mod, 22, 23, std::bit_cast<uint64_t>(2.0), true}, // octal % binary
        {ctjs::BinaryKind::Mod, 2, 1, 0, false},                             // null % true
        {ctjs::BinaryKind::Pow, 44, 23, std::bit_cast<uint64_t>(8.0), true}, // String 2 ** binary 3
        {ctjs::BinaryKind::Pow, 3, 0, std::bit_cast<uint64_t>(1.0), true},   // undefined ** false
        {ctjs::BinaryKind::Pow, 1, 28, 0x7ff8000000000000ULL, false},        // true ** Infinity
        {ctjs::BinaryKind::BitAnd, 23, 1, std::bit_cast<uint64_t>(1.0), true, true},
        {ctjs::BinaryKind::BitOr, 3, 21, std::bit_cast<uint64_t>(16.0), true, true},
        {ctjs::BinaryKind::BitXor, 23, 1, std::bit_cast<uint64_t>(2.0), true, true},
        {ctjs::BinaryKind::Shl, 1, 22, std::bit_cast<uint64_t>(256.0), true, true},
        {ctjs::BinaryKind::Shr, 31, 1, 0, false, true}, // String -Infinity >> true
        {ctjs::BinaryKind::UShr, 17, 2, std::bit_cast<uint64_t>(4294967295.0), true, true},
        {ctjs::BinaryKind::Add, 1, 2, std::bit_cast<uint64_t>(1.0), true}, // true + null
        {ctjs::BinaryKind::Add, 3, 1, 0x7ff8000000000000ULL, false},       // undefined + true
        {ctjs::BinaryKind::Add, 76, 1, std::bit_cast<uint64_t>(2.0), true,
         true}, // static is numeric
        {ctjs::BinaryKind::Shl, 30, 17, std::bit_cast<uint64_t>(-2147483648.0), true, true},
        {ctjs::BinaryKind::UShr, 17, 28, std::bit_cast<uint64_t>(4294967295.0), true, true},
        // Invalid grammar and unsafe exponents refuse on either operand. Generic
        // Add concatenates Strings; generic BitAnd has no numeric VM semantics.
        {ctjs::BinaryKind::Sub, 29, 1, 0, false},
        {ctjs::BinaryKind::Mod, 1, 84, 0, false},
        {ctjs::BinaryKind::Add, 76, 1, 0, false},
        {ctjs::BinaryKind::Add, 1, 76, 0, false},
        {ctjs::BinaryKind::BitAnd, 23, 1, 0, false},
    }};
    struct StringAdd {
        unsigned lhs;
        unsigned rhs;
        llvm::StringRef value;
        bool accepts = true;
    };
    const std::array<StringAdd, 38> stringAdds{{
        {4, 4, ""},
        {8, 62, "ab"},
        {5, 12, "01"},
        {12, 5, "10"},
        {8, 1, "atrue"},
        {0, 8, "falsea"},
        {8, 2, "anull"},
        {2, 8, "nulla"},
        {8, 3, "aundefined"},
        {3, 8, "undefineda"},
        {4, 16, "0"}, // either signed zero formats as 0
        {16, 4, "0"},
        {4, 13, "NaN"},
        {90, 4, "NaN"}, // negative signaling NaN loses sign and payload
        {4, 18, "Infinity"},
        {91, 4, "-Infinity"},
        {4, 19, "5e-324"},
        {92, 4, "1e-7"},
        {4, 93, "0.000001"},
        {94, 4, "100000000000000000000"},
        {4, 95, "1e+21"},
        {96, 4, "1.7976931348623157e+308"},
        {97, 4, "9007199254740992"},
        {4, 98, "-1.25"},
        {6, 8, llvm::StringRef("\0a", 2)},
        {7, 62, llvm::StringRef("a\0bb", 4)},
        {9, 10,
         "\xc3\xa9"
         "e\xcc\x81"},
        {57, 58, "\xf0\x90\x80\x80"}, // halves join only at the concatenation boundary
        {58, 57, "\xed\xb0\x80\xed\xa0\x80"},
        {8, 57, "a\xed\xa0\x80"},
        {58, 4, "\xed\xb0\x80"},
        {59, 14, "\xf0\x90\x80\x80\xf0\x9f\x98\x80"},
        {63, 4, "", false}, // overlong NUL
        {4, 64, "", false}, // truncated sequence
        {65, 8, "", false}, // lone continuation byte
        {8, 66, "", false}, // above U+10FFFF
        {67, 4, "", false}, // noncanonical paired surrogates in one operand
        {8, 68, "", false}, // invalid continuation
    }};
    const std::array<StringAdd, 18> concats{{
        {4, 4, ""},
        {8, 62, "ab"},
        {12, 12, "11"}, // Concat converts both Numbers; Add would produce 2
        {0, 1, "falsetrue"},
        {2, 3, "nullundefined"},
        {16, 11, "00"},
        {13, 90, "NaNNaN"},
        {18, 91, "Infinity-Infinity"},
        {19, 92, "5e-3241e-7"},
        {93, 95, "0.0000011e+21"},
        {6, 8, llvm::StringRef("\0a", 2)},
        {9, 10,
         "\xc3\xa9"
         "e\xcc\x81"},
        {57, 58, "\xf0\x90\x80\x80"},
        {58, 57, "\xed\xb0\x80\xed\xa0\x80"},
        {29, 35, "garbage1junk"}, // String concatenation needs no numeric grammar
        {63, 4, "", false},
        {4, 64, "", false},
        {67, 8, "", false},
    }};
    const std::array<std::pair<mlir::Attribute, llvm::StringRef>, 10> taggedUnaries{{
        {predicates[0].first, "number"}, // +0
        {predicates[1].first, "number"}, // -0
        {predicates[2].first, "number"}, // quiet NaN
        {predicates[3].first, "number"}, // negative signaling NaN
        {ctjs::BooleanAttr::get(context, false), "boolean"},
        {predicates[9].first, "string"},                        // empty String
        {ctjs::StringAttr::get(context, "\xc0\x80"), "string"}, // no byte decoding
        {ctjs::StringAttr::get(context, std::string(100002, 'a')), "string"},
        {predicates[12].first, "object"}, // null
        {predicates[13].first, "undefined"},
    }};
    for (unsigned control = 0; control != 1518; ++control) {
        auto expected = baseline;
        mlir::OwningOpRef<mlir::ModuleOp> candidate(original.clone());
        auto identity = candidate->lookupSymbol<ctjs::FuncOp>("fn$2");
        auto next = candidate->lookupSymbol<ctjs::FuncOp>("fn$3");
        auto close = candidate->lookupSymbol<ctjs::FuncOp>("fn$4");
        for (auto helper : {identity, next, close}) {
            check(std::distance(helper.getBody().begin(), helper.getBody().end()) == 2 &&
                      helper.getBody().back().hasNoPredecessors(),
                  "raw iterator helper retains its unreachable return tail");
        }
        auto & body = identity.getBody().front();
        mlir::OpBuilder at(body.getTerminator());
        if (control >= 536 && control < 560) {
            const unsigned row = control - 536;
            auto helper = row < 6 ? row / 2 == 0   ? identity
                                    : row / 2 == 1 ? next
                                                   : close
                                  : identity;
            auto & first = helper.getBody().front();
            auto * terminal = first.getTerminator();
            const auto where = helper.getLoc();
            const auto payload = terminal->getOperand(0);
            at.setInsertionPoint(terminal);
            auto load =
                ctjs::LoadGlobalOp::create(at, where, row == 14 || row == 15 ? "NaN" : "undefined");
            auto comparison = ctjs::CompareOp::create(at, where, payload.getType(),
                                                      ctjs::CompareKind::StrictEq, load, load);
            auto negated = ctjs::UnaryOp::create(
                at, where, payload.getType(),
                ctjs::UnaryKindAttr::get(context, ctjs::UnaryKind::Not), comparison);
            if (row < 6) {
                // Independent literal reference preserves every evaluated value,
                // including the global snapshot shared by both comparison inputs.
                mlir::IRMapping mapping;
                mlir::OwningOpRef<mlir::ModuleOp> evaluated(
                    llvm::cast<mlir::ModuleOp>((*candidate)->clone(mapping)));
                for (auto [value, attribute] :
                     {std::pair{load.getResult(),
                                mlir::Attribute(ctjs::UndefinedAttr::get(context))},
                      std::pair{comparison.getResult(),
                                mlir::Attribute(ctjs::BooleanAttr::get(context, true))},
                      std::pair{negated.getResult(),
                                mlir::Attribute(ctjs::BooleanAttr::get(context, false))}}) {
                    auto copy = mapping.lookup(value).getDefiningOp();
                    mlir::OpBuilder replace(copy);
                    auto result = ctjs::ConstantOp::create(replace, where, attribute);
                    copy->getResult(0).replaceAllUsesWith(result.getResult());
                    copy->erase();
                }
                auto evaluatedProof = contract;
                evaluatedProof.moduleSha256 =
                    ctcompile::ctnative::hostContractFingerprint(*evaluated);
                auto error =
                    ctcompile::ctnative::prepareDOMEntry(*evaluated, evaluatedProof, 100000);
                if (!check(!error, "literal undefined establishes the complete helper reference")) {
                    llvm::consumeError(std::move(error));
                    continue;
                }
                expected = printed(*evaluated);
            }
            if (row == 16) { load->removeAttr("name"); }
            if (row == 17) { load->getResult(0).setType(at.getI1Type()); }
            if (row == 22 || row == 23) {
                comparison->setOperand(1, first.getArgument(ctjs::arg_callee));
            }
            if (row == 6 || row == 7) {
                ctjs::StoreGlobalOp::create(at, where, "undefined", payload);
            } else if (row == 10 || row == 11) {
                auto wrapper = *candidate->getOps<ctjs::FuncOp>().begin();
                mlir::OpBuilder store(wrapper.getBody().front().getTerminator());
                ctjs::StoreGlobalOp::create(store, where, "undefined",
                                            wrapper.getBody().front().getArgument(0));
            } else if (row == 12 || row == 13) {
                mlir::OpBuilder store(next.getBody().back().getTerminator());
                ctjs::StoreGlobalOp::create(store, where, "undefined",
                                            next.getBody().front().getArgument(0));
            } else if (row == 20 || row == 21) {
                auto global = ctjs::LoadGlobalOp::create(at, where, "globalThis");
                auto key = ctjs::ConstantOp::create(at, where,
                                                    ctjs::StringAttr::get(context, "undefined"));
                ctjs::SetPropertyOp::create(at, where, global, key, payload);
            }
            auto flag = ctjs::TruthyOp::create(at, where, at.getI1Type(), negated);
            if (row % 2 == 0) {
                auto * continuation = first.splitBlock(terminal);
                auto * dead = &helper.getBody().emplaceBlock();
                at.setInsertionPointToEnd(&first);
                mlir::cf::CondBranchOp::create(at, where, flag, dead, continuation);
                at.setInsertionPointToEnd(dead);
                if (row == 8) { ctjs::StoreGlobalOp::create(at, where, "undefined", payload); }
                ctjs::ReturnOp::create(at, where, payload);
            } else {
                auto branch = mlir::scf::IfOp::create(at, where, payload.getType(), flag, true);
                for (auto [index, region] : llvm::enumerate(branch->getRegions())) {
                    at.setInsertionPointToEnd(&region.front());
                    if (row == 9 && index == 0) {
                        ctjs::StoreGlobalOp::create(at, where, "undefined", payload);
                    }
                    mlir::scf::YieldOp::create(at, where, mlir::ValueRange{payload});
                }
                terminal->setOperand(0, branch.getResult(0));
            }
        } else if (control == 0 || control == 1) {
            if (control == 0) { at.setInsertionPoint(identity.getBody().back().getTerminator()); }
            ctjs::StoreGlobalOp::create(at, identity.getLoc(), "identity-effect",
                                        body.getArgument(ctjs::arg_receiver));
        } else if (control >= 2 && control <= 4) {
            auto helper = control == 2 ? identity : control == 3 ? next : close;
            auto & first = helper.getBody().front();
            auto * terminal = first.getTerminator();
            auto * continuation = first.splitBlock(terminal);
            at.setInsertionPointToEnd(&first);
            mlir::cf::BranchOp::create(at, helper.getLoc(), continuation);
        } else if (control == 5 || control == 6) {
            auto receiver = close.getBody().front().getArgument(ctjs::arg_receiver);
            if (control == 5) {
                auto call = *close.getBody().front().getOps<ctjs::CallOp>().begin();
                call.getReceiverMutable().assign(receiver);
            } else {
                close.getBody().front().getTerminator()->setOperand(0, receiver);
            }
        } else if (control == 7) {
            for (auto literal : next.getBody().front().getOps<ctjs::ConstantOp>()) {
                if (ctjs::constantKey(literal) == "data-next") {
                    literal.setValueAttr(ctjs::StringAttr::get(original.getContext(), "bad name"));
                }
            }
        } else if (control == 8) {
            at.setInsertionPoint(identity.getBody().back().getTerminator());
            for (unsigned i = 0; i != 300; ++i) {
                ctjs::ConstantOp::create(at, identity.getLoc(),
                                         ctjs::UndefinedAttr::get(original.getContext()));
            }
        } else if (control == 9) {
            auto * continuation = body.splitBlock(body.getTerminator());
            at.setInsertionPointToEnd(&body);
            auto flag = mlir::arith::ConstantIntOp::create(at, identity.getLoc(), 0, 32);
            llvm::SmallVector<int32_t> cases;
            llvm::SmallVector<mlir::Block *> destinations;
            llvm::SmallVector<mlir::ValueRange> operands;
            for (int32_t i = 0; i != 400; ++i) {
                cases.push_back(i);
                destinations.push_back(continuation);
                operands.emplace_back();
            }
            mlir::cf::SwitchOp::create(at, identity.getLoc(), flag, continuation,
                                       mlir::ValueRange{}, cases, destinations, operands);
        } else if (control >= 76) {
            // Both representations must retain the close call and its thrown
            // identity, while discarding only the unselected payload/effects.
            close.getBody().back().erase();
            auto & first = close.getBody().front();
            auto * terminal = first.getTerminator();
            auto * moved = (*first.getOps<ctjs::CallOp>().begin()).getOperation();
            const auto where = close.getLoc();
            const auto payload = terminal->getOperand(0);
            const auto other = first.getArgument(ctjs::arg_receiver);
            const bool primitive = control < 104;
            const bool taggedUnary = control >= 1426;
            const unsigned taggedIndex = taggedUnary ? (control - 1426) / 4 : 0;
            const bool stringAdd = control >= 1248 && !taggedUnary;
            const bool concat = control >= 1358 && !taggedUnary;
            const unsigned addStart = concat ? 1358 : 1248;
            const auto strings = concat ? llvm::ArrayRef(concats) : llvm::ArrayRef(stringAdds);
            const auto stringCount = static_cast<unsigned>(strings.size());
            const unsigned addIndex = stringAdd ? (control - addStart) / 2 : 0;
            const bool coercingBinary = control >= 1166 && control < 1248;
            const unsigned binaryIndex = coercingBinary ? (control - 1166) / 2 : 0;
            const bool coercingUnary = control >= 1096 && control < 1166;
            const unsigned unaryIndex = coercingUnary ? (control - 1096) / 2 : 0;
            const bool wideComparison = control >= 1048 && control < 1096;
            const unsigned wideIndex = wideComparison ? (control - 1048) / 2 : 0;
            const bool stringComparison = control >= 896 && control < 1048;
            const unsigned stringIndex = stringComparison ? (control - 896) / 2 : 0;
            const bool convertedComparison = control >= 748 && control < 896;
            const unsigned convertedIndex = convertedComparison ? (control - 748) / 2 : 0;
            const bool looseEquality = control >= 644 && control < 748;
            const unsigned looseIndex = looseEquality ? (control - 644) / 2 : 0;
            const bool power = control >= 560 && control < 644;
            const unsigned powerIndex = power ? (control - 560) / 2 : 0;
            const bool equality = (control >= 434 && control < 536) || looseEquality ||
                                  convertedComparison || stringComparison || wideComparison;
            const bool bitwiseUnary = control >= 326 && control < 372;
            const bool numericUnary = (control >= 280 && control < 372) || coercingUnary;
            const bool staticCalculated = control >= 372 && !equality && !power && !numericUnary &&
                                          !coercingBinary && !stringAdd && !taggedUnary;
            const unsigned staticIndex = staticCalculated ? (control - 372) / 2 : 0;
            const bool calculated = control >= 232 && !numericUnary && !equality && !taggedUnary;
            const bool compared = control >= 176 && control < 232;
            const bool computed = control >= 116 && control < 176;
            const unsigned index =
                taggedUnary        ? 0
                : stringAdd        ? addIndex < stringCount ? strings[addIndex].accepts ? 0 : 18
                                                            : addIndex - stringCount + 12
                : coercingBinary   ? binaryIndex < 22    ? 0
                                     : binaryIndex < 27  ? 18
                                     : binaryIndex < 32  ? binaryIndex - 15
                                     : binaryIndex == 32 ? 17
                                     : binaryIndex == 34 ? 19
                                     : binaryIndex == 35 ? 20
                                                         : 18
                : coercingUnary    ? unaryIndex < 20    ? 0
                                     : unaryIndex < 26  ? 18
                                     : unaryIndex < 29  ? unaryIndex - 14
                                     : unaryIndex == 29 ? 17
                                     : unaryIndex < 32  ? unaryIndex - 11
                                     : unaryIndex == 32 ? 18
                                     : unaryIndex == 33 ? 15
                                                        : 16
                : wideComparison   ? wideIndex < wideComparisons.size() || wideIndex >= 23 ? 0
                                     : wideIndex < 22 ? wideIndex + 11
                                                      : 36
                : stringComparison ? stringIndex < stringComparisons.size() ? 0
                                     : stringIndex < 70                     ? stringIndex - 33
                                                                            : stringIndex - 25
                : convertedComparison
                    ? convertedIndex < convertedComparisons.size() ? 0 : convertedIndex - 23
                : looseEquality    ? looseIndex < looseEqualities.size() ? 0 : looseIndex - 1
                : power            ? powerIndex < powers.size() ? 0 : powerIndex - 18
                : equality         ? (control - 434) / 2
                : staticCalculated ? staticIndex < staticArithmetic.size() ? 0 : staticIndex - 6
                : bitwiseUnary     ? (control - 326) / 2
                : numericUnary     ? (control - 280) / 2
                : calculated       ? (control - 232) / 2
                : compared         ? (control - 176) / 2
                : computed         ? (control - 116) / 2
                                   : (control - 76) / 2;
            const bool transported = (taggedUnary && (taggedIndex == 10 || taggedIndex == 11 ||
                                                      taggedIndex == 20 || taggedIndex == 21)) ||
                                     (computed && control >= 156) ||
                                     (compared && (index == 14 || index == 15)) ||
                                     (calculated && (index == 12 || index == 13)) ||
                                     (numericUnary && (index == 12 || index == 13)) ||
                                     (equality && (index == 27 || index == 28));
            const unsigned transport = taggedUnary    ? taggedIndex == 11   ? 3
                                                        : taggedIndex == 20 ? 6
                                                        : taggedIndex == 21 ? 8
                                                                            : 0
                                       : equality     ? index == 28 ? 2 : 0
                                       : numericUnary ? index == 13 ? 2 : 0
                                       : calculated   ? index == 13 ? 2 : 0
                                       : compared     ? index == 15 ? 2 : 0
                                       : transported  ? index - 20
                                                      : 0;
            const unsigned mutation = taggedUnary    ? taggedIndex == 13   ? 0
                                                       : taggedIndex == 14 ? 1
                                                                           : 6
                                      : equality     ? index == 31   ? 0
                                                       : index == 32 ? 1
                                                                     : 6
                                      : numericUnary ? index == 15   ? 0
                                                       : index == 16 ? 1
                                                                     : 6
                                      : calculated   ? index == 15   ? 0
                                                       : index == 16 ? 1
                                                                     : 6
                                      : compared     ? index == 17   ? 0
                                                       : index == 18 ? 1
                                                                     : 6
                                      : computed     ? index == 12 || index == 27 ? 0
                                                       : index == 13              ? 1
                                                                                  : 6
                                      : primitive    ? 6
                                                     : (control - 104) / 2;
            bool selected = primitive ? predicates[index].second : true;
            at.setInsertionPoint(moved);
            mlir::Value input;
            llvm::SmallVector<std::pair<mlir::Operation *, mlir::Attribute>> evaluatedResults;
            if (taggedUnary) {
                const auto & row =
                    taggedUnaries[taggedIndex < taggedUnaries.size() ? taggedIndex : 0];
                const auto kind =
                    (control - 1426) % 4 < 2 ? ctjs::UnaryKind::TypeOf : ctjs::UnaryKind::Void;
                mlir::Value operand =
                    taggedIndex == 15 ? mlir::Value(other)
                    : taggedIndex == 16
                        ? ctjs::CreateObjectOp::create(at, where, payload.getType()).getResult()
                    : taggedIndex == 18
                        ? ctjs::LoadGlobalOp::create(at, where, "tagged-unary-input-effect")
                              .getResult()
                        : ctjs::ConstantOp::create(
                              at, where,
                              taggedIndex == 17
                                  ? mlir::Attribute(ctjs::BigIntAttr::get(context, "1"))
                                  : row.first)
                              .getResult();
                auto operation = ctjs::UnaryOp::create(
                    at, where, payload.getType(), ctjs::UnaryKindAttr::get(context, kind), operand);
                if (control == 1514) { operation->eraseOperand(0); }
                if (control == 1515) { operation->removeAttr("kind"); }
                if (control == 1516) { operand.getDefiningOp()->removeAttr("value"); }
                if (control == 1517) {
                    mlir::OpBuilder invalid(operation);
                    auto flag = mlir::arith::ConstantIntOp::create(invalid, where, 0, 1);
                    operation->setOperand(0, flag);
                }
                const mlir::Attribute value =
                    kind == ctjs::UnaryKind::TypeOf
                        ? mlir::Attribute(ctjs::StringAttr::get(context, row.second))
                        : ctjs::UndefinedAttr::get(context);
                evaluatedResults.emplace_back(operation.getOperation(), value);
                input = operation;
                selected = kind == ctjs::UnaryKind::TypeOf;
                if (taggedIndex >= 10 && taggedIndex <= 12) {
                    auto expectedValue = ctjs::ConstantOp::create(at, where, value);
                    auto comparison =
                        ctjs::CompareOp::create(at, where, payload.getType(),
                                                ctjs::CompareKind::StrictEq, input, expectedValue);
                    evaluatedResults.emplace_back(comparison.getOperation(),
                                                  ctjs::BooleanAttr::get(context, true));
                    auto negated = ctjs::UnaryOp::create(
                        at, where, payload.getType(),
                        ctjs::UnaryKindAttr::get(context, ctjs::UnaryKind::Not), comparison);
                    evaluatedResults.emplace_back(negated.getOperation(),
                                                  ctjs::BooleanAttr::get(context, false));
                    input = negated;
                    selected = false;
                    if (taggedIndex == 12) {
                        llvm::cast<ctjs::CallOp>(moved).getArgsMutable().slice(1, 1).assign(input);
                    }
                }
                if (taggedIndex == 19) {
                    ctjs::StoreGlobalOp::create(at, where, "tagged-unary-prefix-effect", other);
                }
            } else if (stringAdd) {
                const auto & row = strings[addIndex < stringCount ? addIndex : 1];
                const auto oversized = ctjs::StringAttr::get(context, std::string(100002, 'a'));
                mlir::Value lhs =
                    index == 17 ? mlir::Value(other)
                    : index == 18 && addIndex >= stringCount
                        ? ctjs::CreateObjectOp::create(at, where, payload.getType()).getResult()
                    : index == 20
                        ? ctjs::LoadGlobalOp::create(at, where, "string-add-input-effect")
                              .getResult()
                        : ctjs::ConstantOp::create(at, where,
                                                   index == 23 ? mlir::Attribute(oversized)
                                                               : equalityValues[row.lhs])
                              .getResult();
                auto rhs = ctjs::ConstantOp::create(
                    at, where,
                    index == 19   ? mlir::Attribute(ctjs::BigIntAttr::get(context, "1"))
                    : index == 24 ? mlir::Attribute(oversized)
                                  : equalityValues[row.rhs]);
                const auto kind = ctjs::BinaryKindAttr::get(context, concat || index == 28
                                                                         ? ctjs::BinaryKind::Concat
                                                                         : ctjs::BinaryKind::Add);
                auto * operation =
                    concat && index == 22
                        ? ctjs::BinaryStaticOp::create(at, where, payload.getType(), kind, lhs, rhs)
                              .getOperation()
                        : ctjs::BinaryOp::create(at, where, payload.getType(), kind, lhs, rhs)
                              .getOperation();
                const unsigned malformedIndex = control - (addStart + 2 * (stringCount + 13));
                if (malformedIndex == 0) { operation->eraseOperand(1); }
                if (malformedIndex == 1) { operation->removeAttr("kind"); }
                if (malformedIndex == 2) { lhs.getDefiningOp()->removeAttr("value"); }
                if (malformedIndex == 3) { rhs->removeAttr("value"); }
                if (malformedIndex == 4 || malformedIndex == 5) {
                    mlir::OpBuilder invalid(operation);
                    auto flag = mlir::arith::ConstantIntOp::create(invalid, where, 0, 1);
                    operation->setOperand(malformedIndex - 4, flag);
                }
                selected = !concat && index == 22 ? row.value.empty() : !row.value.empty();
                evaluatedResults.emplace_back(operation, ctjs::StringAttr::get(context, row.value));
                input = operation->getResult(0);
                if (index >= 12 && index <= 14) {
                    auto sum = ctjs::BinaryOp::create(
                        at, where, payload.getType(),
                        ctjs::BinaryKindAttr::get(context, concat ? ctjs::BinaryKind::Concat
                                                                  : ctjs::BinaryKind::Add),
                        input, input);
                    evaluatedResults.emplace_back(sum.getOperation(),
                                                  ctjs::StringAttr::get(context, "abab"));
                    auto comparison = ctjs::CompareOp::create(
                        at, where, payload.getType(), ctjs::CompareKind::StrictEq, sum, sum);
                    evaluatedResults.emplace_back(comparison.getOperation(),
                                                  ctjs::BooleanAttr::get(context, true));
                    input = comparison;
                    if (index == 14) {
                        llvm::cast<ctjs::CallOp>(moved).getArgsMutable().slice(1, 1).assign(sum);
                    } else {
                        auto negated = ctjs::UnaryOp::create(
                            at, where, payload.getType(),
                            ctjs::UnaryKindAttr::get(context, ctjs::UnaryKind::Not), input);
                        selected = false;
                        evaluatedResults.emplace_back(negated.getOperation(),
                                                      ctjs::BooleanAttr::get(context, false));
                        input = negated;
                    }
                }
                if (index == 21) {
                    ctjs::StoreGlobalOp::create(at, where, "string-add-prefix-effect", other);
                }
            } else if (equality) {
                auto row =
                    wideComparison
                        ? wideComparisons[wideIndex < wideComparisons.size() ? wideIndex : 0]
                    : stringComparison
                        ? stringComparisons[stringIndex < stringComparisons.size() ? stringIndex
                                                                                   : 36]
                    : convertedComparison
                        ? convertedComparisons[convertedIndex < convertedComparisons.size()
                                                   ? convertedIndex
                                                   : 4]
                    : looseEquality
                        ? looseEqualities[looseIndex < looseEqualities.size() ? looseIndex : 5]
                        : equalities[index < equalities.size() ? index : 0];
                if (convertedComparison && index >= 37 && index <= 40) {
                    row = {index == 37   ? ctjs::CompareKind::Lt
                           : index == 38 ? ctjs::CompareKind::Le
                           : index == 39 ? ctjs::CompareKind::Gt
                                         : ctjs::CompareKind::Ge,
                           43, 44, true};
                } else if (looseEquality && index >= 37 && index <= 40) {
                    row = index == 37   ? Comparison{ctjs::CompareKind::Eq, 5, 11, true}
                          : index == 38 ? Comparison{ctjs::CompareKind::Eq, 11, 5, true}
                          : index == 39 ? Comparison{ctjs::CompareKind::Eq, 5, 0, true}
                                        : Comparison{ctjs::CompareKind::Eq, 0, 5, true};
                } else if (index >= 37 && index <= 40) {
                    row = index == 37   ? Comparison{ctjs::CompareKind::Lt, 4, 5, true}
                          : index == 38 ? Comparison{ctjs::CompareKind::Le, 0, 1, true}
                          : index == 39 ? Comparison{ctjs::CompareKind::Gt, 2, 2, false}
                                        : Comparison{ctjs::CompareKind::Ge, 3, 3, false};
                } else if (convertedComparison && index >= 41 && index <= 44) {
                    row = convertedComparisons[index - 1];
                } else if (looseEquality && index >= 41 && index <= 44) {
                    row = index == 41   ? Comparison{ctjs::CompareKind::Eq, 5, 2, false}
                          : index == 42 ? Comparison{ctjs::CompareKind::Eq, 2, 5, false}
                          : index == 43 ? Comparison{ctjs::CompareKind::Eq, 4, 3, false}
                                        : Comparison{ctjs::CompareKind::Eq, 3, 4, false};
                } else if (index >= 41 && index <= 44) {
                    row = index == 41   ? Comparison{ctjs::CompareKind::Eq, 0, 11, true}
                          : index == 42 ? Comparison{ctjs::CompareKind::Eq, 0, 0, true}
                          : index == 43 ? Comparison{ctjs::CompareKind::Eq, 2, 3, true}
                                        : Comparison{ctjs::CompareKind::Eq, 3, 3, true};
                }
                mlir::Value lhs;
                if (index == 33) {
                    lhs = other;
                } else if (index == 34) {
                    lhs = ctjs::CreateObjectOp::create(at, where, payload.getType());
                } else if (index == 35) {
                    lhs = ctjs::LoadGlobalOp::create(at, where, "equality-input-effect");
                } else if (index == 47) {
                    lhs = ctjs::UnaryOp::create(
                        at, where, payload.getType(),
                        ctjs::UnaryKindAttr::get(context, ctjs::UnaryKind::Plus), other);
                } else {
                    const mlir::Attribute value =
                        control >= 1094 ? mlir::Attribute(ctjs::StringAttr::get(
                                              context, std::string(100001, '0') + "1"))
                        : index == 45   ? mlir::Attribute(ctjs::BigIntAttr::get(context, "0"))
                                        : equalityValues[row.lhs];
                    lhs = ctjs::ConstantOp::create(at, where, value);
                }
                const mlir::Attribute rhsValue =
                    index == 46 ? mlir::Attribute(ctjs::BigIntAttr::get(context, "0"))
                                : equalityValues[row.rhs];
                auto rhs = ctjs::ConstantOp::create(at, where, rhsValue);
                auto operation =
                    ctjs::CompareOp::create(at, where, payload.getType(), row.kind, lhs, rhs);
                if (control == 530 || control == 742 || control == 890 || control == 1042) {
                    operation->eraseOperand(1);
                }
                if (control == 531 || control == 743 || control == 891 || control == 1043) {
                    operation->removeAttr("kind");
                }
                if (control == 532 || control == 744 || control == 892 || control == 1044) {
                    lhs.getDefiningOp()->removeAttr("value");
                }
                if (control == 533 || control == 745 || control == 893 || control == 1045) {
                    rhs->removeAttr("value");
                }
                if (control == 534 || control == 535 || control == 746 || control == 747 ||
                    control == 894 || control == 895 || (control >= 1046 && control < 1048)) {
                    mlir::OpBuilder invalid(operation);
                    auto flag = mlir::arith::ConstantIntOp::create(invalid, where, 0, 1);
                    operation->setOperand(control % 2, flag);
                }
                selected = row.value;
                evaluatedResults.emplace_back(operation.getOperation(),
                                              ctjs::BooleanAttr::get(context, selected));
                input = operation;
                if (index == 27 || index == 28) {
                    auto negated = ctjs::UnaryOp::create(
                        at, where, payload.getType(),
                        ctjs::UnaryKindAttr::get(context, ctjs::UnaryKind::Not), input);
                    selected = !selected;
                    evaluatedResults.emplace_back(negated.getOperation(),
                                                  ctjs::BooleanAttr::get(context, selected));
                    input = negated;
                }
                if (index == 29 || index == 30) {
                    llvm::cast<ctjs::CallOp>(moved).getArgsMutable().slice(1, 1).assign(
                        index == 29 ? input : lhs);
                }
                if (index == 36) {
                    ctjs::StoreGlobalOp::create(at, where, "equality-prefix-effect", other);
                }
            } else if (numericUnary) {
                const auto & rows = bitwiseUnary ? bitwiseUnaries : numericUnaries;
                const auto & converted =
                    coercingUnaries[unaryIndex < coercingUnaries.size() ? unaryIndex : 4];
                const auto row =
                    coercingUnary
                        ? NumericUnary{converted.kind, 0, converted.value, converted.truth}
                    : index < rows.size() ? rows[index]
                    : bitwiseUnary
                        ? NumericUnary{ctjs::UnaryKind::BitNot, std::bit_cast<uint64_t>(-2.0),
                                       std::bit_cast<uint64_t>(1.0), true}
                        : NumericUnary{ctjs::UnaryKind::Plus, 0x3ff0000000000000ULL,
                                       0x3ff0000000000000ULL, true};
                const mlir::Attribute value =
                    coercingUnary && unaryIndex == 32
                        ? mlir::Attribute(
                              ctjs::StringAttr::get(context, std::string(100001, '0') + "1"))
                    : coercingUnary && (unaryIndex < 29 || unaryIndex > 32)
                        ? equalityValues[converted.operand]
                    : index == 18 ? mlir::Attribute(ctjs::StringAttr::get(context, "1"))
                    : index == 19 ? mlir::Attribute(ctjs::BigIntAttr::get(context, "1"))
                                  : ctjs::NumberAttr::get(context, row.operand);
                mlir::Value operand = index == 17
                                          ? mlir::Value(other)
                                          : ctjs::ConstantOp::create(at, where, value).getResult();
                if (index == 20) {
                    operand = ctjs::UnaryOp::create(
                        at, where, payload.getType(),
                        ctjs::UnaryKindAttr::get(context, ctjs::UnaryKind::Plus), other);
                }
                auto operation = ctjs::UnaryOp::create(
                    at, where, payload.getType(),
                    ctjs::UnaryKindAttr::get(
                        context, !bitwiseUnary && (index == 17 || index == 19 || index == 20)
                                     ? ctjs::UnaryKind::Neg
                                     : row.kind),
                    operand);
                const unsigned unaryControl = bitwiseUnary ? control - 46 : control;
                if (unaryControl == 322) { operation->eraseOperand(0); }
                if (unaryControl == 323) { operation->removeAttr("kind"); }
                if (unaryControl == 324) { operand.getDefiningOp()->removeAttr("value"); }
                if (unaryControl == 325) {
                    mlir::OpBuilder invalid(operation);
                    auto flag = mlir::arith::ConstantIntOp::create(invalid, where, 0, 1);
                    operation->setOperand(0, flag);
                }
                selected = row.truth;
                evaluatedResults.emplace_back(
                    operation.getOperation(),
                    ctjs::NumberAttr::get(context, bitwiseUnary && index == 18
                                                       ? std::bit_cast<uint64_t>(-2.0)
                                                       : row.value));
                input = operation;
                if (index == 12 || index == 13) {
                    auto negated = ctjs::UnaryOp::create(
                        at, where, payload.getType(),
                        ctjs::UnaryKindAttr::get(context, ctjs::UnaryKind::Neg), input);
                    evaluatedResults.emplace_back(
                        negated.getOperation(),
                        ctjs::NumberAttr::get(context, 0xbff0000000000000ULL));
                    // The result feeds both Neg and Add across transported edges.
                    auto sum = ctjs::BinaryOp::create(
                        at, where, payload.getType(),
                        ctjs::BinaryKindAttr::get(context, ctjs::BinaryKind::Add), negated, input);
                    evaluatedResults.emplace_back(sum.getOperation(),
                                                  ctjs::NumberAttr::get(context, 0));
                    input = sum;
                }
                if (index >= 12 && index <= 14) {
                    auto comparison = ctjs::CompareOp::create(
                        at, where, payload.getType(), ctjs::CompareKind::StrictEq, input, input);
                    selected = true;
                    evaluatedResults.emplace_back(comparison.getOperation(),
                                                  ctjs::BooleanAttr::get(context, selected));
                    input = comparison;
                    if (index == 14) {
                        llvm::cast<ctjs::CallOp>(moved).getArgsMutable().slice(1, 1).assign(input);
                    } else {
                        auto negated = ctjs::UnaryOp::create(
                            at, where, payload.getType(),
                            ctjs::UnaryKindAttr::get(context, ctjs::UnaryKind::Not), input);
                        selected = false;
                        evaluatedResults.emplace_back(negated.getOperation(),
                                                      ctjs::BooleanAttr::get(context, selected));
                        input = negated;
                    }
                }
            } else if (calculated) {
                const auto & converted =
                    coercingBinaries[binaryIndex < coercingBinaries.size() ? binaryIndex : 0];
                const auto row =
                    coercingBinary
                        ? Arithmetic{converted.kind, 0, 0, converted.value, converted.truth}
                    : power ? powers[powerIndex < powers.size() ? powerIndex : 0]
                    : staticCalculated
                        ? staticArithmetic[staticIndex < staticArithmetic.size() ? staticIndex : 1]
                        : arithmetic[index < arithmetic.size() ? index : 0];
                const mlir::Attribute lhsValue =
                    coercingBinary && binaryIndex == 37
                        ? mlir::Attribute(
                              ctjs::StringAttr::get(context, std::string(100001, '0') + "1"))
                    : coercingBinary ? equalityValues[converted.lhs]
                    : index == 18    ? mlir::Attribute(ctjs::StringAttr::get(context, "1"))
                                     : ctjs::NumberAttr::get(context, row.lhs);
                mlir::Value lhs = index == 17
                                      ? mlir::Value(other)
                                      : ctjs::ConstantOp::create(at, where, lhsValue).getResult();
                if (power && index == 20) {
                    lhs = ctjs::LoadGlobalOp::create(at, where, "power-input-effect");
                }
                if (coercingBinary && binaryIndex == 33) {
                    lhs = ctjs::CreateObjectOp::create(at, where, payload.getType());
                }
                if (coercingBinary && binaryIndex == 35) {
                    lhs = ctjs::LoadGlobalOp::create(at, where, "binary-input-effect");
                }
                const mlir::Attribute rhsValue =
                    index == 19      ? mlir::Attribute(ctjs::BigIntAttr::get(context, "1"))
                    : coercingBinary ? equalityValues[converted.rhs]
                                     : ctjs::NumberAttr::get(context, row.rhs);
                auto rhs = ctjs::ConstantOp::create(at, where, rhsValue);
                const auto kind = ctjs::BinaryKindAttr::get(
                    context,
                    control == 432 ? ctjs::BinaryKind::Sub
                    : control == 433 || (index == 20 && !staticCalculated && !coercingBinary)
                        ? ctjs::BinaryKind::Pow
                        : row.kind);
                auto * operation =
                    (coercingBinary ? converted.staticForm : staticCalculated && index != 20)
                        ? ctjs::BinaryStaticOp::create(at, where, payload.getType(), kind, lhs, rhs)
                              .getOperation()
                        : ctjs::BinaryOp::create(at, where, payload.getType(), kind, lhs, rhs)
                              .getOperation();
                const unsigned binaryControl = coercingBinary     ? control - 968
                                               : power            ? control - 364
                                               : staticCalculated ? control - 152
                                                                  : control;
                if (binaryControl == 274) { operation->eraseOperand(1); }
                if (binaryControl == 275) { operation->removeAttr("kind"); }
                if (binaryControl == 276) { lhs.getDefiningOp()->removeAttr("value"); }
                if (binaryControl == 277) { rhs->removeAttr("value"); }
                if (binaryControl == 278 || binaryControl == 279) {
                    mlir::OpBuilder invalid(operation);
                    auto flag = mlir::arith::ConstantIntOp::create(invalid, where, 0, 1);
                    operation->setOperand(binaryControl - 278, flag);
                }
                selected = coercingBinary && binaryIndex == 36 ? !row.truth : row.truth;
                evaluatedResults.emplace_back(
                    operation,
                    control == 268 || control == 269
                        ? mlir::Attribute(ctjs::StringAttr::get(context, "11.1102230246251565e-16"))
                        : ctjs::NumberAttr::get(context, power && index == 18
                                                             ? std::bit_cast<uint64_t>(1.0)
                                                             : row.value));
                input = operation->getResult(0);
                if (index >= 12 && index <= 14) {
                    auto comparison = ctjs::CompareOp::create(
                        at, where, payload.getType(), ctjs::CompareKind::StrictEq, input, input);
                    selected = true;
                    evaluatedResults.emplace_back(comparison.getOperation(),
                                                  ctjs::BooleanAttr::get(context, selected));
                    input = comparison;
                    if (index == 14) {
                        llvm::cast<ctjs::CallOp>(moved).getArgsMutable().slice(1, 1).assign(
                            coercingBinary ? lhs : input);
                    } else {
                        auto negated = ctjs::UnaryOp::create(
                            at, where, payload.getType(),
                            ctjs::UnaryKindAttr::get(context, ctjs::UnaryKind::Not), input);
                        selected = false;
                        evaluatedResults.emplace_back(negated.getOperation(),
                                                      ctjs::BooleanAttr::get(context, selected));
                        input = negated;
                    }
                }
            } else if (compared) {
                const auto comparison = comparisons[index < comparisons.size() ? index : 0];
                mlir::Value lhs;
                if (index == 19) {
                    lhs = other;
                } else if (index == 20) {
                    lhs = ctjs::CreateObjectOp::create(at, where, payload.getType());
                } else if (index == 23) {
                    lhs = ctjs::UnaryOp::create(
                        at, where, payload.getType(),
                        ctjs::UnaryKindAttr::get(context, ctjs::UnaryKind::Plus), other);
                } else if (index == 24) {
                    lhs = ctjs::LoadGlobalOp::create(at, where, "condition-effect");
                } else {
                    const mlir::Attribute value =
                        index == 21 ? mlir::Attribute(ctjs::StringAttr::get(context, "0"))
                                    : predicates[comparison.lhs].first;
                    lhs = ctjs::ConstantOp::create(at, where, value);
                }
                const mlir::Attribute rhsValue =
                    index == 22 ? mlir::Attribute(ctjs::BigIntAttr::get(context, "0"))
                                : predicates[comparison.rhs].first;
                auto rhs = ctjs::ConstantOp::create(at, where, rhsValue);
                auto operation = ctjs::CompareOp::create(at, where, payload.getType(),
                                                         comparison.kind, lhs, rhs);
                if (control == 226) { operation->eraseOperand(1); }
                if (control == 227) { operation->removeAttr("kind"); }
                if (control == 228) { lhs.getDefiningOp()->removeAttr("value"); }
                if (control == 229) { rhs->removeAttr("value"); }
                if (control >= 230) {
                    mlir::OpBuilder invalid(operation);
                    auto flag = mlir::arith::ConstantIntOp::create(invalid, where, 0, 1);
                    operation->setOperand(control - 230, flag);
                }
                selected = comparison.value;
                evaluatedResults.emplace_back(operation.getOperation(),
                                              ctjs::BooleanAttr::get(context, selected));
                input = operation;
                if (index == 14) {
                    auto negated = ctjs::UnaryOp::create(
                        at, where, payload.getType(),
                        ctjs::UnaryKindAttr::get(context, ctjs::UnaryKind::Not), input);
                    selected = !selected;
                    evaluatedResults.emplace_back(negated.getOperation(),
                                                  ctjs::BooleanAttr::get(context, selected));
                    input = negated;
                }
                if (index == 16) {
                    llvm::cast<ctjs::CallOp>(moved).getArgsMutable().slice(1, 1).assign(input);
                }
            } else if (computed) {
                const bool literal = index < negatedPredicates.size();
                bool truth = transported ? transport == 3
                             : literal   ? negatedPredicates[index].second
                                         : index != 9;
                if (index == 14 || index == 25) {
                    input = other;
                } else if (index == 15) {
                    input = ctjs::CreateObjectOp::create(at, where, payload.getType());
                } else if (index == 16) {
                    input = ctjs::LoadGlobalOp::create(at, where, "condition-effect");
                } else if (index == 18 || index == 29) {
                    input = ctjs::UnaryOp::create(
                        at, where, payload.getType(),
                        ctjs::UnaryKindAttr::get(context, ctjs::UnaryKind::Plus), other);
                } else {
                    const mlir::Attribute value =
                        transported && transport == 4
                            ? mlir::Attribute(ctjs::BooleanAttr::get(context, false))
                        : literal     ? negatedPredicates[index].first
                        : index == 17 ? mlir::Attribute(ctjs::BigIntAttr::get(context, "1"))
                                      : predicates[truth ? 6 : 0].first;
                    input = ctjs::ConstantOp::create(at, where, value);
                }
                if (index == 19) {
                    ctjs::StoreGlobalOp::create(at, where, "predicate-prefix-effect", other);
                }
                if (transported && transport == 4) {
                    llvm::cast<ctjs::CallOp>(moved).getArgsMutable().slice(1, 1).assign(input);
                }
                const unsigned depth = index == 9 ? 2 : index == 10 || index == 21 ? 3 : 1;
                for (unsigned i = 0; i != depth; ++i) {
                    auto operation = ctjs::UnaryOp::create(
                        at, where, payload.getType(),
                        ctjs::UnaryKindAttr::get(context, ctjs::UnaryKind::Not), input);
                    truth = !truth;
                    evaluatedResults.emplace_back(operation.getOperation(),
                                                  ctjs::BooleanAttr::get(context, truth));
                    input = operation;
                }
                selected = truth;
                if (index == 11) {
                    llvm::cast<ctjs::CallOp>(moved).getArgsMutable().slice(1, 1).assign(input);
                }
            } else if (mutation == 2) {
                input = ctjs::CreateObjectOp::create(at, where, payload.getType());
            } else if (mutation == 3) {
                input = ctjs::UnaryOp::create(
                    at, where, payload.getType(),
                    ctjs::UnaryKindAttr::get(context, ctjs::UnaryKind::Plus), other);
            } else if (mutation == 4) {
                input = ctjs::LoadGlobalOp::create(at, where, "condition-effect");
            } else {
                const mlir::Attribute value =
                    primitive       ? predicates[index].first
                    : mutation == 5 ? mlir::Attribute(ctjs::BigIntAttr::get(context, "1"))
                                    : predicates[6].first;
                input = ctjs::ConstantOp::create(at, where, value);
            }
            if ((taggedUnary && taggedIndex < 13) || primitive ||
                (computed && (index < 12 || (transported && transport < 5))) ||
                (compared && (index < 17 || index == 21)) || control == 268 || control == 269 ||
                control == 1356 || control == 1357 ||
                (calculated &&
                 (index < 15 || (!power && !coercingBinary && !stringAdd && index == 20) ||
                  (power && index == 18))) ||
                (numericUnary && (index < 15 || (!coercingUnary && index == 18))) ||
                (equality &&
                 ((stringComparison ? stringIndex < 48 || (index >= 27 && index < 31)
                                    : index < 31) ||
                  (!stringComparison && (convertedComparison ? index == 37 || index == 38
                                                             : index >= 37 && index <= 44))))) {
                // The input still evaluates after projection. Compare against
                // the same operands and known pure results without the edges.
                mlir::IRMapping mapping;
                mlir::OwningOpRef<mlir::ModuleOp> evaluated(
                    llvm::cast<mlir::ModuleOp>((*candidate)->clone(mapping)));
                for (auto [operation, value] : evaluatedResults) {
                    auto copy = mapping.lookup(operation->getResult(0)).getDefiningOp();
                    mlir::OpBuilder replace(copy);
                    auto result = ctjs::ConstantOp::create(replace, copy->getLoc(), value);
                    copy->getResult(0).replaceAllUsesWith(result.getResult());
                    copy->erase();
                }
                auto evaluatedProof = contract;
                evaluatedProof.moduleSha256 =
                    ctcompile::ctnative::hostContractFingerprint(*evaluated);
                auto error =
                    ctcompile::ctnative::prepareDOMEntry(*evaluated, evaluatedProof, 100000);
                if (!check(!error, "proved helper input establishes its branchless reference")) {
                    llvm::errs() << "raw iterator helper control " << control << ": "
                                 << llvm::toString(std::move(error)) << '\n';
                    continue;
                }
                expected = printed(*evaluated);
            }
            if (transported) {
                // The branchless reference above retains every evaluated input.
                // These edges must expose the operand before its predicate can fold.
                for (auto [operation, value] : evaluatedResults) {
                    (void)value;
                    const auto operand = operation->getOperand(0);
                    at.setInsertionPoint(operation);
                    if (transport == 2 || transport == 3 || transport == 6 || transport == 8) {
                        const bool selectedInput = transport != 3;
                        const mlir::Value condition =
                            transport == 6
                                ? mlir::Value(
                                      ctjs::TruthyOp::create(at, where, at.getI1Type(), other))
                                : mlir::Value(mlir::arith::ConstantIntOp::create(at, where,
                                                                                 selectedInput, 1));
                        auto branch =
                            mlir::scf::IfOp::create(at, where, operand.getType(), condition, true);
                        for (auto [armIndex, region] : llvm::enumerate(branch->getRegions())) {
                            at.setInsertionPointToEnd(&region.front());
                            mlir::Value yielded = operand;
                            if ((armIndex == 0) == selectedInput) {
                                if (transport == 3) {
                                    auto flag = mlir::arith::ConstantIntOp::create(at, where, 1, 1);
                                    auto nested = mlir::scf::IfOp::create(
                                        at, where, operand.getType(), flag, true);
                                    at.setInsertionPointToEnd(&nested.getThenRegion().front());
                                    mlir::scf::YieldOp::create(at, where,
                                                               mlir::ValueRange{operand});
                                    at.setInsertionPointToEnd(&nested.getElseRegion().front());
                                    ctjs::StoreGlobalOp::create(at, where, "nested-input-effect",
                                                                other);
                                    mlir::scf::YieldOp::create(at, where, mlir::ValueRange{other});
                                    at.setInsertionPointAfter(nested);
                                    yielded = nested.getResult(0);
                                }
                                if (transport == 8) {
                                    ctjs::StoreGlobalOp::create(at, where, "selected-input-effect",
                                                                other);
                                }
                            } else {
                                ctjs::StoreGlobalOp::create(at, where, "discarded-input-effect",
                                                            other);
                                yielded = transport == 6 ? mlir::Value(ctjs::ConstantOp::create(
                                                               at, where, predicates[6].first))
                                                         : mlir::Value(other);
                            }
                            mlir::scf::YieldOp::create(at, where, mlir::ValueRange{yielded});
                        }
                        operation->setOperand(0, branch.getResult(0));
                    } else {
                        auto * source = operation->getBlock();
                        auto * continuation = source->splitBlock(operation);
                        operation->setOperand(0,
                                              continuation->addArgument(operand.getType(), where));
                        at.setInsertionPointToEnd(source);
                        mlir::cf::BranchOp::create(at, where, continuation,
                                                   mlir::ValueRange{operand});
                    }
                }
                at.setInsertionPoint(moved);
            }
            auto flag = ctjs::TruthyOp::create(at, where, at.getI1Type(), input);
            const mlir::Value livePayload = mutation == 0 ? mlir::Value(other) : payload;
            auto & selectedBody = *moved->getBlock();
            if (control % 2 == 0) {
                auto * continuation = selectedBody.splitBlock(terminal);
                terminal->setOperand(0, continuation->addArgument(payload.getType(), where));
                auto * dead = &close.getBody().emplaceBlock();
                at.setInsertionPointToEnd(&selectedBody);
                mlir::cf::CondBranchOp::create(
                    at, where, flag, selected ? continuation : dead,
                    selected ? mlir::ValueRange{livePayload} : mlir::ValueRange{},
                    selected ? dead : continuation,
                    selected ? mlir::ValueRange{} : mlir::ValueRange{livePayload});
                at.setInsertionPointToEnd(dead);
                ctjs::StoreGlobalOp::create(at, where, "dead-arm-effect", other);
                ctjs::ThrowOp::create(at, where, other);
                if (mutation == 1) {
                    at.setInsertionPoint(moved);
                    ctjs::StoreGlobalOp::create(at, where, "live-arm-effect", other);
                }
            } else {
                auto branch = mlir::scf::IfOp::create(at, where, payload.getType(), flag, true);
                for (auto [armIndex, region] : llvm::enumerate(branch->getRegions())) {
                    auto & arm = region.front();
                    const bool live = (armIndex == 0) == selected;
                    if (live) {
                        arm.getOperations().splice(arm.end(), selectedBody.getOperations(),
                                                   moved->getIterator(), terminal->getIterator());
                    }
                    at.setInsertionPointToEnd(&arm);
                    if (!live || mutation == 1) {
                        ctjs::StoreGlobalOp::create(
                            at, where, live ? "live-arm-effect" : "dead-arm-effect", other);
                    }
                    mlir::scf::YieldOp::create(
                        at, where, mlir::ValueRange{live ? livePayload : mlir::Value(other)});
                }
                terminal->setOperand(0, branch.getResult(0));
            }
        } else if (control >= 56) {
            auto helper = control == 66 || control == 67 || control == 70 || control == 71 ? close
                          : control == 68 || control == 69 ? next
                                                           : identity;
            helper.getBody().back().erase();
            auto & first = helper.getBody().front();
            auto * terminal = first.getTerminator();
            mlir::Value payload = terminal->getOperand(0);
            auto receiver = first.getArgument(ctjs::arg_receiver);
            auto other = first.getArgument(ctjs::arg_callee);
            auto * moved = control == 67
                               ? (*first.getOps<ctjs::LoadUpvalueOp>().begin()).getOperation()
                           : control == 68 || control == 69 ? payload.getDefiningOp()
                                                            : nullptr;
            at.setInsertionPoint(moved ? moved : terminal);
            const bool selected = control != 57 && control != 59 && control != 63 &&
                                  control != 67 && control != 69 && control != 75;
            mlir::Value flag;
            if (control == 60 || control == 61 || control >= 73) {
                mlir::Value input = receiver;
                if (control == 73) {
                    input = ctjs::LoadGlobalOp::create(at, helper.getLoc(), "condition-effect");
                } else if (control >= 74) {
                    input = ctjs::ConstantOp::create(
                        at, helper.getLoc(),
                        ctjs::BooleanAttr::get(original.getContext(), selected));
                }
                flag = ctjs::TruthyOp::create(at, helper.getLoc(), at.getI1Type(), input);
            } else {
                flag = mlir::arith::ConstantIntOp::create(at, helper.getLoc(), selected, 1);
            }
            const bool tuple = control >= 62 && control <= 64;
            llvm::SmallVector<mlir::Type> types(tuple ? 2u : 1u, payload.getType());
            auto branch = mlir::scf::IfOp::create(at, helper.getLoc(), types, flag, true);
            for (auto [index, region] : llvm::enumerate(branch->getRegions())) {
                auto & arm = region.front();
                const bool live = (index == 0) == selected;
                mlir::Value yielded = live || control == 60 ? payload : mlir::Value(other);
                if (live && moved) {
                    // Move the complete producer suffix: captures, record fields
                    // and ordered effects must survive the selected arm together.
                    arm.getOperations().splice(arm.end(), first.getOperations(),
                                               moved->getIterator(), terminal->getIterator());
                }
                at.setInsertionPointToEnd(&arm);
                if (!live || control == 71) {
                    ctjs::StoreGlobalOp::create(at, helper.getLoc(),
                                                live ? "live-arm-effect" : "dead-arm-effect",
                                                receiver);
                    if (control == 72) {
                        for (unsigned i = 0; i != 300; ++i) {
                            ctjs::ConstantOp::create(
                                at, helper.getLoc(),
                                ctjs::UndefinedAttr::get(original.getContext()));
                        }
                    }
                }
                if (live && (control == 58 || control == 59 || control == 70)) { yielded = other; }
                if (live && (control == 65 || control == 67)) {
                    auto no = mlir::arith::ConstantIntOp::create(at, helper.getLoc(), 0, 1);
                    auto nested =
                        mlir::scf::IfOp::create(at, helper.getLoc(), payload.getType(), no, true);
                    at.setInsertionPointToEnd(&nested.getThenRegion().front());
                    mlir::scf::YieldOp::create(at, helper.getLoc(), mlir::ValueRange{other});
                    at.setInsertionPointToEnd(&nested.getElseRegion().front());
                    mlir::scf::YieldOp::create(at, helper.getLoc(), mlir::ValueRange{yielded});
                    yielded = nested.getResult(0);
                    at.setInsertionPointToEnd(&arm);
                }
                llvm::SmallVector<mlir::Value> values{yielded};
                if (tuple) {
                    values.push_back(other);
                    if (control == 63) { std::swap(values[0], values[1]); }
                }
                mlir::scf::YieldOp::create(at, helper.getLoc(), values);
            }
            terminal->setOperand(0, branch.getResult(control == 63 || control == 64 ? 1 : 0));
        } else if (control >= 37) {
            auto helper = (control >= 44 && control <= 46) || control == 48 ? close : identity;
            // Exercise the already-one-block path, without an imported dead tail
            // triggering CFG normalization on the helper's behalf.
            helper.getBody().back().erase();
            auto & first = helper.getBody().front();
            auto * terminal = first.getTerminator();
            auto * moved =
                helper == close ? (*first.getOps<ctjs::CallOp>().begin()).getOperation() : terminal;
            if (helper == close &&
                !check(moved->use_empty(), "structured close effect has no escaping result")) {
                continue;
            }
            at.setInsertionPoint(moved);
            auto receiver = first.getArgument(ctjs::arg_receiver);
            const bool selected =
                control != 38 && control != 40 && control != 41 && control != 45 && control != 52;
            mlir::Value flag;
            if (control == 39 || control == 40 || (control >= 49 && control <= 50) ||
                control == 52) {
                mlir::Value input = receiver;
                if (control == 50) {
                    input = ctjs::LoadGlobalOp::create(at, helper.getLoc(), "condition-effect");
                } else if (control == 52) {
                    input = ctjs::ConstantOp::create(
                        at, helper.getLoc(), ctjs::NumberAttr::get(original.getContext(), 0));
                } else if (control == 39 || control == 40) {
                    input = ctjs::ConstantOp::create(
                        at, helper.getLoc(),
                        ctjs::BooleanAttr::get(original.getContext(), selected));
                }
                flag = ctjs::TruthyOp::create(at, helper.getLoc(), at.getI1Type(), input);
            } else {
                flag = mlir::arith::ConstantIntOp::create(at, helper.getLoc(), selected, 1);
            }
            if (control == 54) {
                auto * continuation = first.splitBlock(terminal);
                at.setInsertionPointToEnd(&first);
                mlir::cf::BranchOp::create(at, helper.getLoc(), continuation,
                                           mlir::ValueRange{flag});
                flag = continuation->addArgument(at.getI1Type(), helper.getLoc());
                at.setInsertionPoint(terminal);
            }
            if (control == 51) {
                auto branch =
                    mlir::scf::IfOp::create(at, helper.getLoc(), receiver.getType(), flag, true);
                for (auto & region : branch->getRegions()) {
                    at.setInsertionPointToEnd(&region.front());
                    mlir::scf::YieldOp::create(at, helper.getLoc(), mlir::ValueRange{receiver});
                }
                terminal->setOperand(0, branch.getResult(0));
            } else {
                auto branch = mlir::scf::IfOp::create(at, helper.getLoc(), flag,
                                                      control != 41 && control != 42);
                auto & dead = selected ? branch.getElseRegion() : branch.getThenRegion();
                if (!dead.empty()) {
                    at.setInsertionPoint(dead.front().getTerminator());
                    ctjs::StoreGlobalOp::create(at, helper.getLoc(), "dead-arm-effect", receiver);
                    if (control == 53) {
                        for (unsigned i = 0; i != 300; ++i) {
                            ctjs::ConstantOp::create(
                                at, helper.getLoc(),
                                ctjs::UndefinedAttr::get(original.getContext()));
                        }
                    }
                }
                auto & live = selected ? branch.getThenRegion() : branch.getElseRegion();
                if (!live.empty()) {
                    auto * destination = &live.front();
                    if (control == 43 || control == 46 || control == 55) {
                        at.setInsertionPoint(destination->getTerminator());
                        mlir::Value nestedFlag = flag;
                        if (control != 55) {
                            nestedFlag =
                                mlir::arith::ConstantIntOp::create(at, helper.getLoc(), 0, 1);
                        }
                        auto nested =
                            mlir::scf::IfOp::create(at, helper.getLoc(), nestedFlag, true);
                        auto & discarded =
                            control == 55 ? nested.getElseRegion() : nested.getThenRegion();
                        at.setInsertionPoint(discarded.front().getTerminator());
                        ctjs::StoreGlobalOp::create(at, helper.getLoc(), "nested-dead-effect",
                                                    receiver);
                        destination =
                            &(control == 55 ? nested.getThenRegion() : nested.getElseRegion())
                                 .front();
                    }
                    if (helper == close) { moved->moveBefore(destination->getTerminator()); }
                    if (control == 47 || control == 48) {
                        at.setInsertionPoint(destination->getTerminator());
                        ctjs::StoreGlobalOp::create(at, helper.getLoc(), "live-arm-effect",
                                                    receiver);
                    }
                }
            }
        } else if (control >= 23) {
            auto helper = control == 28                                     ? close
                          : control == 27 || control == 33 || control >= 35 ? next
                                                                            : identity;
            auto & first = helper.getBody().front();
            auto * terminal = first.getTerminator();
            ctjs::CallOp call;
            if (helper != identity) { call = *first.getOps<ctjs::CallOp>().begin(); }
            auto * split = control == 36 ? call->getNextNode()
                           : call        ? call.getOperation()
                                         : terminal;
            auto * continuation = first.splitBlock(split);
            at.setInsertionPointToEnd(&first);
            auto receiver = first.getArgument(ctjs::arg_receiver);
            llvm::SmallVector<mlir::Value> payload;
            if (helper == identity) {
                payload.push_back(control == 34 ? first.getArgument(ctjs::arg_callee)
                                                : terminal->getOperand(0));
                terminal->setOperand(
                    0, continuation->addArgument(payload.front().getType(), helper.getLoc()));
            }
            if (control == 35) {
                ctjs::StoreGlobalOp::create(at, helper.getLoc(), "live-prefix-effect", receiver);
            }
            const bool dynamic = (control >= 29 && control <= 32) || control == 36;
            const bool selected = control != 24 && control != 26 && control != 28 && control != 34;
            mlir::Value flag;
            if (dynamic || control == 25 || control == 26) {
                mlir::Value input = control == 36 ? mlir::Value(call.getResult()) : receiver;
                if (control == 32) {
                    input = ctjs::LoadGlobalOp::create(at, helper.getLoc(), "condition-effect");
                } else if (!dynamic) {
                    input = ctjs::ConstantOp::create(
                        at, helper.getLoc(),
                        ctjs::BooleanAttr::get(original.getContext(), selected));
                }
                flag = ctjs::TruthyOp::create(at, helper.getLoc(), at.getI1Type(), input);
            } else {
                flag = mlir::arith::ConstantIntOp::create(at, helper.getLoc(), selected, 1);
            }
            if (dynamic && control != 31) {
                auto alternative = payload;
                if (control == 30) { alternative.front() = first.getArgument(ctjs::arg_callee); }
                mlir::cf::CondBranchOp::create(at, helper.getLoc(), flag, continuation, payload,
                                               continuation, alternative);
            } else {
                auto * alternative = &helper.getBody().emplaceBlock();
                mlir::cf::CondBranchOp::create(
                    at, helper.getLoc(), flag, selected ? continuation : alternative,
                    selected ? mlir::ValueRange(payload) : mlir::ValueRange{},
                    selected ? alternative : continuation,
                    selected ? mlir::ValueRange{} : mlir::ValueRange(payload));
                at.setInsertionPointToEnd(alternative);
                if (control != 31) {
                    ctjs::StoreGlobalOp::create(at, helper.getLoc(), "dead-arm-effect", receiver);
                }
                ctjs::ReturnOp::create(at, helper.getLoc(), receiver);
            }
            if (control == 33) {
                at.setInsertionPointToStart(continuation);
                ctjs::StoreGlobalOp::create(at, helper.getLoc(), "live-arm-effect", receiver);
            }
        } else if (control >= 20) {
            auto helper = control == 20 ? identity : control == 21 ? next : close;
            helper.eraseBody();
            helper.setPrivate();
        } else if (control >= 10) {
            auto helper = control == 10 || control == 17                    ? identity
                          : control == 11 || control == 16 || control == 19 ? next
                                                                            : close;
            auto & first = helper.getBody().front();
            auto * terminal = first.getTerminator();
            auto * split =
                control >= 18 ? (*first.getOps<ctjs::CallOp>().begin()).getOperation() : terminal;
            auto * continuation = first.splitBlock(split);
            at.setInsertionPointToEnd(&first);
            if (control <= 12 || control == 17) {
                mlir::Value payload = terminal->getOperand(0);
                if (control == 17) { payload = first.getArgument(ctjs::arg_callee); }
                auto transported = continuation->addArgument(payload.getType(), helper.getLoc());
                terminal->setOperand(0, transported);
                mlir::cf::BranchOp::create(at, helper.getLoc(), continuation,
                                           mlir::ValueRange{payload});
            } else if (control == 14 || control == 15) {
                auto * alternative = new mlir::Block;
                helper.getBody().push_back(alternative);
                if (control == 15) {
                    mlir::cf::BranchOp::create(at, helper.getLoc(), alternative);
                    at.setInsertionPointToEnd(alternative);
                }
                auto flag = ctjs::TruthyOp::create(at, helper.getLoc(), at.getI1Type(),
                                                   first.getArgument(ctjs::arg_receiver));
                mlir::cf::CondBranchOp::create(at, helper.getLoc(), flag, alternative,
                                               continuation);
                if (control == 14) {
                    at.setInsertionPointToEnd(alternative);
                    ctjs::ReturnOp::create(at, helper.getLoc(),
                                           first.getArgument(ctjs::arg_receiver));
                }
            } else {
                mlir::cf::BranchOp::create(at, helper.getLoc(), continuation);
                if (control == 13 || control == 16) {
                    at.setInsertionPoint(terminal);
                    ctjs::StoreGlobalOp::create(at, helper.getLoc(), "continuation-effect",
                                                terminal->getOperand(0));
                }
            }
        }
        const bool malformed =
            (control >= 226 && control < 232) || (control >= 274 && control < 280) ||
            (control >= 322 && control < 326) || (control >= 368 && control < 372) ||
            (control >= 426 && control < 434) || (control >= 530 && control < 536) ||
            control == 552 || control == 553 || (control >= 638 && control < 644) ||
            (control >= 742 && control < 748) || (control >= 890 && control < 896) ||
            (control >= 1042 && control < 1048) || (control >= 1242 && control < 1248) ||
            (control >= 1350 && control < 1356) || control == 1414 || control == 1415 ||
            (control >= 1420 && control < 1426) || control >= 1514;
        if (!check(mlir::succeeded(mlir::verify(*candidate)) != malformed,
                   "only intentionally malformed helper operations fail verification")) {
            continue;
        }
        auto proof = contract;
        proof.moduleSha256 = ctcompile::ctnative::hostContractFingerprint(*candidate);
        if (control == 554) { proof.moduleSha256 = "stale"; }
        if (control == 555) {
            proof.provider = ctcompile::ctnative::HostContract::Provider::closedSource;
        }
        const auto fingerprint = proof.moduleSha256;
        const auto before = printed(*candidate);
        const bool succeeds =
            control == 0 || (control >= 2 && control <= 4) || (control >= 10 && control <= 12) ||
            control == 18 || control == 19 || (control >= 23 && control <= 29) ||
            (control >= 36 && control <= 46) || control == 51 || control == 52 ||
            (control >= 54 && control <= 57) || control == 62 || control == 63 ||
            (control >= 65 && control <= 69) || (control >= 74 && control < 104) ||
            (control >= 116 && control < 140) || (control >= 156 && control < 166) ||
            (control >= 176 && control < 210) || (control >= 232 && control < 262) ||
            (control >= 280 && control < 310) || (control >= 326 && control < 356) ||
            (control >= 372 && control < 414) || (control >= 434 && control < 496) ||
            (control >= 536 && control < 542) || control == 272 || control == 273 ||
            (control >= 560 && control < 626) || (control >= 516 && control < 524) ||
            (control >= 644 && control < 708) || control == 218 || control == 219 ||
            (control >= 508 && control < 516) || (control >= 720 && control < 736) ||
            (control >= 748 && control < 828) || (control >= 848 && control < 856) ||
            (control >= 868 && control < 872) || (control >= 896 && control < 992) ||
            (control >= 1016 && control < 1024) || (control >= 1048 && control < 1072) ||
            (control >= 1080 && control < 1088) || (control >= 1096 && control < 1136) ||
            (control >= 1148 && control < 1154) || control == 316 || control == 317 ||
            control == 362 || control == 363 || control == 632 || control == 633 ||
            (control >= 1166 && control < 1210) || (control >= 1220 && control < 1226) ||
            control == 268 || control == 269 || (control >= 1248 && control < 1312) ||
            (control >= 1324 && control < 1330) || (control >= 1356 && control < 1388) ||
            (control >= 1394 && control < 1400) || (control >= 1426 && control < 1478) ||
            control == 424 || control == 425; // a bitwise ctjs.binary folds since e6babb265
        if (succeeds) {
            mlir::OwningOpRef<mlir::ModuleOp> limited(candidate->clone());
            auto limitedProof = proof;
            auto limitedError = ctcompile::ctnative::prepareDOMEntry(*limited, limitedProof, 0);
            check(static_cast<bool>(limitedError) && printed(*limited) == before &&
                      limitedProof.moduleSha256 == fingerprint,
                  "reachable helper budget refusal preserves the entire module and contract");
            if (limitedError) { llvm::consumeError(std::move(limitedError)); }
            if (control == 46 || control == 67 || control == 79 || control == 137 ||
                control == 165 || control == 209 || control == 259 || control == 307 ||
                control == 353 || control == 411 || control == 493 || control == 541 ||
                control == 623 || control == 703 || control == 851 || control == 1019 ||
                control == 1083 || control == 1151 || control == 1223 || control == 1327 ||
                control == 1397 || control == 1471 || control == 1473) {
                const auto attempt = [&](unsigned budget) {
                    mlir::OwningOpRef<mlir::ModuleOp> limited(candidate->clone());
                    auto limitedProof = proof;
                    auto error =
                        ctcompile::ctnative::prepareDOMEntry(*limited, limitedProof, budget);
                    if (!error) {
                        check(printed(*limited) == expected,
                              "complete structured helper budget retains exact effects");
                        return true;
                    }
                    llvm::consumeError(std::move(error));
                    check(printed(*limited) == before && limitedProof.moduleSha256 == fingerprint,
                          "partial structured helper projection rolls back all IR and proof");
                    return false;
                };
                unsigned lower = 1, upper = 100000;
                while (lower < upper) {
                    const unsigned middle = lower + (upper - lower) / 2;
                    if (attempt(middle)) {
                        upper = middle;
                    } else {
                        lower = middle + 1;
                    }
                }
                check(attempt(lower) && !attempt(lower - 1),
                      "structured helper needs its complete nonzero projection budget");
            }
        }
        auto error = ctcompile::ctnative::prepareDOMEntry(*candidate, proof, 100000);
        const auto reason = error ? llvm::toString(std::move(error)) : std::string{};
        if (succeeds) {
            check(reason.empty() && printed(*candidate) == expected &&
                      mlir::succeeded(mlir::verify(*candidate)) &&
                      ctcompile::ctnative::DOMEntryAnalysis(*candidate, proof).proved(),
                  "helper continuations preserve the exact original payload and ordered effects");
        } else {
            check(!reason.empty() && printed(*candidate) == before &&
                      proof.moduleSha256 == fingerprint,
                  "raw helper shape, effect and receiver refusals roll back the whole module");
            if (control == 8 || control == 9 || control == 53 || control == 72 ||
                (control >= 1094 && control < 1096) || (control >= 1160 && control < 1162) ||
                control == 1240 || control == 1241 || (control >= 1346 && control < 1350) ||
                (control >= 1416 && control < 1420)) {
                check(reason.find("budget") != std::string::npos,
                      "helper operations and successor edges consume the normalization budget");
            }
        }
        if (succeeds != reason.empty()) {
            llvm::errs() << "raw helper control " << control << ": " << reason << '\n';
        }
    }
}

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
    for (auto [method, structured] : {std::pair{false, false}, std::pair{true, false},
                                      std::pair{false, true}, std::pair{true, true}}) {
        auto source = getter;
        if (method) { replaceOnce(source, "get return()", "return()"); }
        auto module = import(context, source, structured);
        if (!module) { continue; }
        auto function = module->lookupSymbol<ctjs::FuncOp>("customElements$1");
        if (!check(static_cast<bool>(function), "original iterator entry survives raw import")) {
            continue;
        }
        mlir::OwningOpRef<ctjs::FuncOp> original(llvm::cast<ctjs::FuncOp>(function->clone()));
        const auto before = printed(*original);
        ctcompile::ctnative::HostContract contract;
        contract.provider = ctcompile::ctnative::HostContract::Provider::ctbrowserDOM;
        contract.entry = "customElements$1";
        contract.elementParameters = {0};
        contract.initialIntrinsics = {"Object",
                                      "Symbol",
                                      "__ctbrowser_for_of_open",
                                      "__ctbrowser_iter_next",
                                      "__ctbrowser_iter_close",
                                      "__ctbrowser_to_property_key",
                                      "__ctbrowser_set_function_name"};
        contract.moduleSha256 = ctcompile::ctnative::hostContractFingerprint(*module);
        if (!structured && !method) { testRawIteratorHelpers(*module, contract); }
        const auto originalModule = printed(*module);
        auto close = ctcompile::ctnative::normalizeDOMIteratorClose(*module, contract);
        check(close && !*close && printed(*module) == originalModule &&
                  contract.moduleSha256 == ctcompile::ctnative::hostContractFingerprint(*module),
              "original observing iterator defers suppression without changing any source state");
        if (!close) { llvm::consumeError(close.takeError()); }
        const auto boundary = method ? "DOM Symbol.iterator must be a closed identity method"
                                     : "DOM iterator getter requires a terminal throw";
        for (unsigned budget : {0u, 100000u}) {
            mlir::OwningOpRef<mlir::ModuleOp> candidate(module->clone());
            auto proof = contract;
            auto error = ctcompile::ctnative::prepareDOMEntry(*candidate, proof, budget);
            const auto reason = error ? llvm::toString(std::move(error)) : std::string{};
            if (budget) {
                check(reason.empty() && mlir::succeeded(mlir::verify(*candidate)) &&
                          proof.moduleSha256 ==
                              ctcompile::ctnative::hostContractFingerprint(*candidate) &&
                          ctcompile::ctnative::DOMEntryAnalysis(*candidate, proof).proved(),
                      "original iterator prepares and passes fresh complete DOM proof");
                if (!reason.empty()) { llvm::errs() << reason << '\n'; }
                continue;
            }
            check(reason.find("budget") != std::string::npos &&
                      printed(*candidate) == originalModule &&
                      proof.moduleSha256 == contract.moduleSha256,
                  "observing iterator budget refusal rolls back");
        }
        // Computed method setup is erasable only with both exact initial
        // helpers and no observer of its private closure or intermediate values.
        for (llvm::StringRef helperName :
             {"__ctbrowser_to_property_key", "__ctbrowser_set_function_name"}) {
            for (unsigned control = 0; control < 6; ++control) {
                mlir::OwningOpRef<mlir::ModuleOp> candidate(module->clone());
                auto entry = candidate->lookupSymbol<ctjs::FuncOp>(contract.entry);
                ctjs::CallOp setup;
                unsigned setupCalls = 0;
                entry.walk([&](ctjs::CallOp call) {
                    auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
                    if (load && load.getName() == helperName) {
                        setup = call;
                        ++setupCalls;
                    }
                });
                if (!check(setupCalls == 1, "computed iterator retains its original setup call")) {
                    continue;
                }
                auto request = contract;
                auto anchor = entry.getBody().front().getArgument(ctjs::implicit_arguments);
                const bool naming = helperName == "__ctbrowser_set_function_name";
                mlir::OpBuilder at(setup);
                if (control == 0) {
                    std::erase(request.initialIntrinsics, helperName.str());
                } else if (control == 1) {
                    ctjs::StoreGlobalOp::create(at, setup.getLoc(), helperName, anchor);
                } else if (control == 2) {
                    setup.getArgsMutable().slice(naming ? 1 : 0, 1).assign(anchor);
                } else if (control == 3) {
                    setup.getReceiverMutable().assign(anchor);
                } else {
                    at.setInsertionPointAfter(setup);
                    mlir::Value observed = setup.getResult();
                    if (control == 5) {
                        observed = setup.getCallee();
                        if (naming) {
                            auto name = ctjs::ConstantOp::create(
                                at, setup.getLoc(), ctjs::StringAttr::get(&context, "name"));
                            observed = ctjs::GetPropertyOp::create(at, setup.getLoc(),
                                                                   setup.getArgs().front(), name);
                        }
                    }
                    ctjs::StoreGlobalOp::create(at, setup.getLoc(), "setup-observer", observed);
                }
                check(mlir::succeeded(mlir::verify(*candidate)),
                      "computed iterator setup controls preserve valid source IR");
                request.moduleSha256 = ctcompile::ctnative::hostContractFingerprint(*candidate);
                const auto fingerprint = request.moduleSha256;
                const auto intrinsics = request.initialIntrinsics;
                const auto snapshot = printed(*candidate);
                auto failure = ctcompile::ctnative::prepareDOMEntry(*candidate, request, 100000);
                check(failure && printed(*candidate) == snapshot &&
                          request.moduleSha256 == fingerprint &&
                          request.initialIntrinsics == intrinsics,
                      "computed iterator rejects unproved setup without publishing "
                      "source edits");
                if (failure) { llvm::consumeError(std::move(failure)); }
            }
        }
        const auto checks = countChecks(function);
        mlir::DominanceInfo dominance(function);
        llvm::SmallVector<ctjs::CallOp> originalCalls;
        function.walk([&](ctjs::CallOp call) {
            if (dominance.isReachableFromEntry(call->getBlock()) &&
                llvm::isa_and_nonnull<ctjs::CheckOp>(call->getNextNode())) {
                originalCalls.push_back(call);
            }
        });
        // 27 since a2e5dbed3: `return 1` pops the try's handler before op::ret,
        // and the importer gives the unreachable code after it the handlers its
        // code-order walk has there - none - so that code has no status edges.
        check(originalCalls.size() == 4 && checks == 27,
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
        for (unsigned control = 0; control < (structured ? 43u : 7u); ++control) {
            mlir::OwningOpRef<mlir::ModuleOp> candidate(module->clone());
            auto entry = candidate->lookupSymbol<ctjs::FuncOp>(contract.entry);
            ctjs::InvokeOp invocation, opening;
            ctjs::CallOp openCall;
            llvm::SmallVector<ctjs::CallOp> closeCalls;
            entry.walk([&](ctjs::InvokeOp found) {
                auto call = llvm::cast<ctjs::CallOp>(found.getBody().front().front());
                auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
                if (load && load.getName() == "__ctbrowser_iter_close") {
                    if (!invocation) { invocation = found; }
                    closeCalls.push_back(call);
                }
                if (load && load.getName() == "__ctbrowser_for_of_open") {
                    opening = found;
                    openCall = call;
                }
            });
            if (!check(invocation && opening, "recovered calls retain their helpers")) { continue; }
            auto call = llvm::cast<ctjs::CallOp>(invocation.getBody().front().front());
            auto exit = llvm::cast<ctjs::InvokeExitOp>(invocation.getBody().front().back());
            auto holder = openCall.getArgs()[0];
            auto attempt = llvm::cast<ctjs::TryOp>(opening->getParentOp());
            mlir::OpResult recordAlias;
            entry.walk([&](mlir::scf::IfOp branch) {
                auto left = llvm::cast<mlir::scf::YieldOp>(branch.getThenRegion().front().back());
                auto right = llvm::cast<mlir::scf::YieldOp>(branch.getElseRegion().front().back());
                for (auto [result, a, b] :
                     llvm::zip(branch.getResults(), left.getOperands(), right.getOperands())) {
                    if (a == opening.getResult(1) && b == a) { recordAlias = result; }
                }
            });
            if (!check(static_cast<bool>(recordAlias),
                       "original successful record has a common alias")) {
                continue;
            }
            mlir::OpBuilder at(invocation);
            ctjs::StoreGlobalOp selectionEffect;
            if (control == 1) {
                call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>().setName("unknown");
            } else if (control == 2) {
                call.getArgsMutable().slice(1, 1).assign(call.getArgs()[0]);
            } else if (control == 3) {
                auto & normal = invocation.getNormalBody().front();
                at.setInsertionPoint(normal.getTerminator());
                ctjs::StoreGlobalOp::create(at, call.getLoc(), "observed", normal.getArgument(0));
            } else if (control == 4) {
                exit.getStateMutable().slice(0, 1).assign(call.getResult());
            } else if (control == 5) {
                call.getArgsMutable().append(entry.getBody().front().getArgument(3));
            } else if (control == 7 || control == 8) {
                at.setInsertionPoint(attempt);
                mlir::Value escaped = holder;
                if (control == 8) {
                    auto yes = mlir::arith::ConstantIntOp::create(at, call.getLoc(), 1, 1);
                    auto alias =
                        mlir::scf::IfOp::create(at, call.getLoc(), holder.getType(), yes, true);
                    for (auto & region : alias->getRegions()) {
                        auto & arm = region.front();
                        mlir::OpBuilder nested(&arm, arm.end());
                        mlir::scf::YieldOp::create(nested, call.getLoc(), holder);
                    }
                    escaped = alias.getResult(0);
                }
                ctjs::StoreGlobalOp::create(at, call.getLoc(), "holder", escaped);
            } else if (control == 9) {
                at.setInsertionPoint(attempt);
                auto key = ctjs::ConstantOp::create(
                    at, call.getLoc(),
                    ctjs::StringAttr::get(&context, CTBROWSER_SYMBOL_KEY("iterator")));
                auto number =
                    ctjs::ConstantOp::create(at, call.getLoc(), ctjs::NumberAttr::get(&context, 0));
                ctjs::SetPropertyOp::create(at, call.getLoc(), holder, key, number);
            } else if (control == 10) {
                auto identity = candidate->lookupSymbol<ctjs::FuncOp>("fn$2");
                if (!check(static_cast<bool>(identity), "iterator identity body is present")) {
                    continue;
                }
                at.setInsertionPoint(identity.getBody().front().getTerminator());
                ctjs::StoreGlobalOp::create(
                    at, call.getLoc(), "identity-effect",
                    identity.getBody().front().getArgument(ctjs::arg_receiver));
            } else if (control == 11) {
                at.setInsertionPointAfter(opening);
                ctjs::StoreGlobalOp::create(at, call.getLoc(), "holder", holder);
            } else if (control == 12 || control == 13) {
                auto branch = llvm::cast<mlir::scf::IfOp>(recordAlias.getOwner());
                auto yield = llvm::cast<mlir::scf::YieldOp>(branch.getElseRegion().front().back());
                at.setInsertionPoint(yield);
                mlir::Value other = holder;
                if (control == 13) {
                    other = mlir::ub::PoisonOp::create(at, call.getLoc(), holder.getType());
                }
                yield->setOperand(recordAlias.getResultNumber(), other);
            } else if (control == 14 || control == 15) {
                auto branch = llvm::cast<mlir::scf::IfOp>(recordAlias.getOwner());
                if (control == 14) {
                    at.setInsertionPointAfter(branch);
                } else {
                    at.setInsertionPoint(branch.getThenRegion().front().getTerminator());
                }
                selectionEffect =
                    ctjs::StoreGlobalOp::create(at, call.getLoc(), "selection",
                                                control == 14 ? mlir::Value(recordAlias) : holder);
            } else if (control == 16) {
                // The exhausted close saves done at slot 9. Observe its exact
                // unwind projection only after the original invocation.
                auto & unwind = invocation.getUnwindBody().front();
                auto yield = llvm::cast<ctjs::InvokeYieldOp>(unwind.back());
                unsigned observed = 0;
                at.setInsertionPointAfter(invocation);
                for (auto [result, value] : llvm::zip(invocation.getResults(), yield.getValues())) {
                    if (value == unwind.getArgument(10)) {
                        ctjs::StoreGlobalOp::create(at, call.getLoc(), "saved-done", result);
                        ++observed;
                    }
                }
                check(observed == 1, "saved done has one exact unwind result");
            }
            if (control >= 17 && control <= 19) {
                // Corrupt one selected leaf, not the other tag's inactive slot.
                // Every matching leaf must agree; an unknown tag cannot exclude it.
                mlir::scf::YieldOp leaf;
                entry.walk([&](mlir::scf::YieldOp yield) {
                    if (leaf || yield.getNumOperands() != 24 ||
                        yield.getOperand(4) != recordAlias) {
                        return;
                    }
                    auto tag = yield.getOperand(23).getDefiningOp<mlir::arith::ConstantOp>();
                    if (tag && llvm::cast<mlir::IntegerAttr>(tag.getValue()).getInt() == 0) {
                        leaf = yield;
                    }
                });
                if (!check(static_cast<bool>(leaf),
                           "close has an exact selected record/tag leaf")) {
                    continue;
                }
                at.setInsertionPoint(leaf);
                if (control == 18) {
                    leaf->setOperand(4, holder);
                } else {
                    const unsigned slot = control == 19 ? 23 : 4;
                    leaf->setOperand(slot, mlir::ub::PoisonOp::create(
                                               at, call.getLoc(), leaf.getOperand(slot).getType()));
                }
            } else if (control >= 20 && control <= 22) {
                auto guard = invocation->getParentOfType<mlir::scf::IfOp>();
                auto compare = guard.getCondition().getDefiningOp<mlir::arith::CmpIOp>();
                auto cast = compare.getLhs().getDefiningOp<mlir::arith::IndexCastUIOp>();
                if (control == 20) {
                    compare.setPredicate(mlir::arith::CmpIPredicate::ne);
                } else if (control == 21) {
                    auto lhs = compare.getLhs();
                    compare->setOperand(0, compare.getRhs());
                    compare->setOperand(1, lhs);
                } else {
                    auto tag = llvm::cast<mlir::OpResult>(cast.getIn());
                    cast->setOperand(0, tag.getOwner()->getResult(tag.getResultNumber() - 1));
                }
            }
            if (control >= 23) {
                ctjs::InvokeOp advancing;
                entry.walk([&](ctjs::InvokeOp found) {
                    auto nextCall = llvm::cast<ctjs::CallOp>(found.getBody().front().front());
                    auto load = nextCall.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
                    if (load && load.getName() == "__ctbrowser_iter_next") { advancing = found; }
                });
                if (!check(static_cast<bool>(advancing), "original next invocation is present")) {
                    continue;
                }
                if (control == 23 || control == 24) {
                    auto & arm =
                        (control == 23 ? advancing.getNormalBody() : advancing.getUnwindBody())
                            .front();
                    auto yield = llvm::cast<ctjs::InvokeYieldOp>(arm.back());
                    at.setInsertionPoint(yield);
                    yield->setOperand(
                        0, ctjs::ConstantOp::create(
                               at, call.getLoc(), ctjs::BooleanAttr::get(&context, control == 23)));
                } else if (control == 25) {
                    for (auto * user :
                         llvm::make_early_inc_range(advancing.getResult(0).getUsers())) {
                        if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(user)) {
                            truth->setOperand(0, entry.getBody().front().getArgument(3));
                        }
                    }
                } else if (control == 26 || control == 27) {
                    mlir::Operation * wrapped = advancing;
                    if (control == 27) {
                        wrapped = advancing->getParentOfType<mlir::scf::IfOp>()
                                      ->getParentOfType<mlir::scf::IfOp>();
                    }
                    at.setInsertionPoint(wrapped);
                    auto region = mlir::scf::ExecuteRegionOp::create(at, call.getLoc(),
                                                                     wrapped->getResultTypes());
                    wrapped->replaceAllUsesWith(region.getResults());
                    auto & body = region.getRegion().emplaceBlock();
                    wrapped->moveBefore(&body, body.end());
                    at.setInsertionPointToEnd(&body);
                    mlir::scf::YieldOp::create(at, call.getLoc(), wrapped->getResults());
                } else if (control >= 28 && control <= 34) {
                    mlir::scf::IfOp done;
                    entry.walk([&](mlir::scf::IfOp branch) {
                        auto truth = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
                        auto read = truth ? truth.getValue().getDefiningOp<ctjs::GetPropertyOp>()
                                          : ctjs::GetPropertyOp{};
                        if (read && read.getObject() == recordAlias &&
                            ctjs::constantKey(read.getKey()) == "done") {
                            done = branch;
                        }
                    });
                    if (!check(static_cast<bool>(done), "next has an exact done continuation")) {
                        continue;
                    }
                    auto & arm =
                        (control == 29 ? done.getThenRegion() : done.getElseRegion()).front();
                    at.setInsertionPoint(arm.getTerminator());
                    mlir::Value item = advancing.getResult(1);
                    if (control == 30) { at.setInsertionPointAfter(advancing); }
                    if (control == 31) {
                        // Saved item slot 8 becomes unwind argument 9, then
                        // result 11. Observing it must not bypass the item proof.
                        auto & unwind = invocation.getUnwindBody().front();
                        auto yield = llvm::cast<ctjs::InvokeYieldOp>(unwind.back());
                        check(yield.getValues()[11] == unwind.getArgument(9),
                              "saved item retains its exact unwind slot");
                        item = invocation.getResult(11);
                        at.setInsertionPointAfter(invocation);
                    }
                    if (control == 32 || control == 33) {
                        // A lookalike guard is no authority, even nested inside
                        // the exact success arm. Preserve the real done branch.
                        at.setInsertionPoint(done);
                        auto read = done.getCondition()
                                        .getDefiningOp<ctjs::TruthyOp>()
                                        .getValue()
                                        .getDefiningOp<ctjs::GetPropertyOp>();
                        auto other = ctjs::GetPropertyOp::create(
                            at, call.getLoc(), read.getType(),
                            control == 32 ? holder : read.getObject(),
                            control == 32
                                ? read.getKey()
                                : ctjs::ConstantOp::create(at, call.getLoc(),
                                                           ctjs::StringAttr::get(&context, "value"))
                                      .getResult());
                        auto truth =
                            ctjs::TruthyOp::create(at, call.getLoc(), at.getI1Type(), other);
                        auto fake = mlir::scf::IfOp::create(at, call.getLoc(), truth, true);
                        at.setInsertionPoint(fake.getElseRegion().front().getTerminator());
                    }
                    if (control == 34) {
                        // Another tuple position containing the normal item is
                        // also tracked, even if the usual item slot stays dead.
                        auto & normal = advancing.getNormalBody().front();
                        auto yield = llvm::cast<ctjs::InvokeYieldOp>(normal.back());
                        yield->setOperand(3, normal.getArgument(0));
                        item = advancing.getResult(3);
                        at.setInsertionPointAfter(advancing);
                    }
                    ctjs::StoreGlobalOp::create(at, call.getLoc(), "observed-item", item);
                }
            }
            if (control == 35) {
                at.setInsertionPointAfter(attempt);
                ctjs::StoreGlobalOp::create(at, call.getLoc(), "late-holder", holder);
            } else if (control == 36 || control == 37) {
                auto terminal = llvm::cast<ctjs::TryExitOp>(attempt.getBody().front().back());
                terminal.getCaughtValuesMutable().append(holder);
                auto argument =
                    attempt.getCatchBody().front().addArgument(holder.getType(), call.getLoc());
                if (control == 37) {
                    at.setInsertionPoint(attempt.getCatchBody().front().getTerminator());
                    ctjs::StoreGlobalOp::create(at, call.getLoc(), "caught-holder", argument);
                }
            } else if (control == 38) {
                // Follow the exact saved slot, not just the holder's direct uses.
                exit.getStateMutable().slice(0, 1).assign(holder);
                at.setInsertionPointAfter(invocation);
                ctjs::StoreGlobalOp::create(at, call.getLoc(), "saved-holder",
                                            invocation.getResult(3));
            } else if (control == 39) {
                at.setInsertionPoint(attempt);
                auto key = ctjs::ConstantOp::create(at, call.getLoc(),
                                                    ctjs::StringAttr::get(&context, "counter"));
                auto zero =
                    ctjs::ConstantOp::create(at, call.getLoc(), ctjs::NumberAttr::get(&context, 0));
                ctjs::SetPropertyOp::create(at, call.getLoc(), holder, key, zero);
            }
            if (control == 40 || control == 41) {
                // Only the exact proved eager fallback is inactive. A similar
                // test of an unrelated value supplies no state-lifetime proof.
                mlir::scf::IfOp fallback;
                entry.walk([&](mlir::scf::IfOp branch) {
                    auto truth = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
                    auto compare = truth ? truth.getValue().getDefiningOp<ctjs::CompareOp>()
                                         : ctjs::CompareOp{};
                    if (compare && compare.getLhs() == recordAlias) { fallback = branch; }
                });
                if (!check(static_cast<bool>(fallback), "original eager fallback is present")) {
                    continue;
                }
                if (control == 41) {
                    at.setInsertionPointAfter(opening);
                    auto absent = ctjs::ConstantOp::create(at, call.getLoc(),
                                                           ctjs::UndefinedAttr::get(&context));
                    auto compare = ctjs::CompareOp::create(
                        at, call.getLoc(), holder.getType(), ctjs::CompareKind::StrictEq,
                        entry.getBody().front().getArgument(3), absent);
                    auto condition =
                        ctjs::TruthyOp::create(at, call.getLoc(), at.getI1Type(), compare);
                    fallback = mlir::scf::IfOp::create(at, call.getLoc(), condition, true);
                }
                at.setInsertionPoint(fallback.getThenRegion().front().getTerminator());
                selectionEffect =
                    ctjs::StoreGlobalOp::create(at, call.getLoc(), "fallback-holder", holder);
            }
            if (control == 42) {
                at.setInsertionPointAfter(opening);
                ctjs::IterableOp::create(at, call.getLoc(), holder.getType(), holder);
            }
            const auto rewrittenClose = [&](ctjs::CallOp found) {
                if (control == 12 || control == 13 || control == 19 || control == 22) {
                    return false;
                }
                if (control == 17 || control == 18 || control == 20) {
                    return found != closeCalls.front();
                }
                return control == 0 || control == 11 || control == 14 || control == 15 ||
                       control == 16 || control == 21 || control >= 23;
            };
            auto proof = contract;
            proof.moduleSha256 = ctcompile::ctnative::hostContractFingerprint(*candidate);
            const auto snapshot = printed(*candidate);
            auto * openBlock = opening->getBlock();
            llvm::SmallVector<mlir::Value> openOperands(openCall->getOperands());
            struct NormalUse {
                mlir::OpOperand * use;
                mlir::Value value;
                mlir::Attribute literal;
                bool poison;
            };
            llvm::SmallVector<NormalUse> normalUses;
            auto normal = llvm::cast<ctjs::InvokeYieldOp>(opening.getNormalBody().front().back());
            const auto literalValue = [](mlir::Value value) -> mlir::Attribute {
                auto * definition = value.getDefiningOp();
                return llvm::isa_and_nonnull<ctjs::ConstantOp, mlir::arith::ConstantOp>(definition)
                           ? definition->getAttr("value")
                           : mlir::Attribute{};
            };
            const bool transported =
                control == 0 || control == 15 || control == 21 || control == 28 || control == 40;
            unsigned caughtArguments = 0, caughtAnchor = 0;
            if (transported) {
                auto & caught = attempt.getCatchBody().front();
                caughtArguments = caught.getNumArguments();
                attempt.getCatchBody().walk([&](ctjs::CompareOp compare) {
                    if (compare.getKind() != ctjs::CompareKind::StrictEq) { return; }
                    auto read = compare.getRhs().getDefiningOp<ctjs::CellGetOp>();
                    auto anchor = read ? llvm::dyn_cast<mlir::BlockArgument>(read.getCell())
                                       : mlir::BlockArgument{};
                    if (check(compare.getLhs() == caught.getArgument(0) && anchor &&
                                  anchor.getOwner() == &caught,
                              "original catch compares its payload with its saved anchor cell")) {
                        caughtAnchor = anchor.getArgNumber();
                    }
                });
            }
            const auto remember = [&](mlir::OpOperand & use, mlir::Value value) {
                if (value == opening.getNormalBody().front().getArgument(0)) {
                    value = openCall.getResult();
                }
                if (value == recordAlias && control != 12 && control != 13) {
                    value = openCall.getResult();
                }
                if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                    call && llvm::is_contained(closeCalls, call) && rewrittenClose(call) &&
                    use.getOperandNumber() == 2) {
                    value = openCall.getResult();
                }
                normalUses.push_back(
                    {&use, value, literalValue(value),
                     static_cast<bool>(value.getDefiningOp<mlir::ub::PoisonOp>())});
            };
            for (auto [result, value] : llvm::zip(opening.getResults(), normal.getValues())) {
                for (auto & use : result.getUses()) { remember(use, value); }
            }
            for (auto & use : recordAlias.getUses()) { remember(use, recordAlias); }
            entry.walk([&](ctjs::InvokeOp kept) {
                if (kept == opening) { return; }
                kept.walk([&](mlir::Operation * operation) {
                    for (auto & use : operation->getOpOperands()) {
                        auto value = use.get();
                        for (auto [result, projected] :
                             llvm::zip(opening.getResults(), normal.getValues())) {
                            if (value == result) {
                                value = projected;
                                break;
                            }
                        }
                        remember(use, value);
                    }
                });
            });
            // Complete preparation must roll back even after a successful open
            // projection; later effects and unresolved record aliases still refuse.
            if (control >= 7) {
                mlir::OwningOpRef<mlir::ModuleOp> transactional(candidate->clone());
                auto contractCopy = proof;
                auto failed =
                    ctcompile::ctnative::prepareDOMEntry(*transactional, contractCopy, 100000);
                check(failed && printed(*transactional) == snapshot &&
                          contractCopy.moduleSha256 == proof.moduleSha256,
                      "protected-open mutation controls retain complete preparation rollback");
                if (failed) { llvm::consumeError(std::move(failed)); }
            }
            mlir::ScopedDiagnosticHandler quiet(&context,
                                                [](mlir::Diagnostic &) { return mlir::success(); });
            auto failure = ctcompile::ctnative::normalizeDOMCustomIteration(
                *candidate, proof, control == 6 ? 0 : 100000);
            const auto reason = failure ? llvm::toString(std::move(failure)) : std::string{};
            const auto expected =
                control == 39 ? "DOM iterator observed completions require protocol-only Try state"
                : control == 35 ? "holder has an earlier observer"
                : control == 11 || (control >= 36 && control <= 38) || control >= 41
                    ? "DOM iterator protocol state has an unproved Try observer"
                : control == 29 || control == 30 || control == 31 || control == 32 || control == 34
                    ? "DOM iterator item requires its not-done continuation"
                : control == 33 ? "DOM iterator record has an unsupported observer"
                : control == 17 || control == 18 || control == 20 ||
                        (control >= 23 && control <= 25)
                    ? "DOM iterator completion must close every traversal exit"
                : control == 26 || control == 27 ? "DOM custom next requires one direct loop test"
                : control == 19 || control == 22
                    ? "DOM iterator close requires completion-selected record identity"
                : control == 0 || control == 11 || control == 15 || control >= 17 ? boundary
                : control == 12 || control == 13
                    ? "DOM iterator close must follow its complete traversal"
                : control == 14 ? "DOM iterator record has an unsupported observer"
                : control == 16 ? "DOM iterator done field requires truth-only observations"
                : control == 4  ? "requires a well-formed entry"
                : control == 6  ? "budget"
                : control == 7 || control == 8 ? "holder has an earlier observer"
                : control == 9                 ? "state aliases its iterator method"
                : control == 10                ? "identity method has additional effects"
                                               : "abrupt completion needs a handler proof";
            check((transported ? reason.empty() : reason.find(expected) != std::string::npos) &&
                      (!(control > 0 && control < 7) || printed(*candidate) == snapshot),
                  "protocol proof retains calls and refuses unknown identity, effects and state");
            if (transported ? !reason.empty() : reason.find(expected) == std::string::npos) {
                llvm::errs() << "iterator control " << control << ": " << reason << '\n';
            }
            if (transported) {
                // Completion replaces the entry and its tuple layout. Inspect
                // the final protected values, not retired staged SSA pointers.
                entry = candidate->lookupSymbol<ctjs::FuncOp>(contract.entry);
                check(mlir::succeeded(mlir::verify(*candidate)),
                      "integrated observed completion verifies");
                const auto isTrue = [&](mlir::Value value) {
                    return literalValue(value) == ctjs::BooleanAttr::get(&context, true);
                };
                unsigned kept = 0, closes = 0, attempts = 0, caughtComparisons = 0, poison = 0;
                ctjs::InvokeOp advancing;
                entry.walk([&](mlir::ub::PoisonOp) { ++poison; });
                entry.walk([&](ctjs::InvokeOp found) {
                    auto & body = found.getBody().front();
                    auto called = llvm::cast<ctjs::CallOp>(body.front());
                    auto dispatch = llvm::cast<ctjs::InvokeExitOp>(body.back());
                    auto method = called.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                    const bool next = method && ctjs::constantKey(method.getKey()) == "next";
                    check(method && called.getReceiver() == method.getObject() &&
                              called.getArgs().empty() &&
                              std::distance(body.begin(), body.end()) == 2 &&
                              dispatch.getNormalResult() == called.getResult() &&
                              dispatch.getState().size() == 15 && found.getNumResults() == 4,
                          "method invocations retain call/exit bodies and all 15 saved slots");
                    ++kept;
                    auto & normal = found.getNormalBody().front();
                    auto & unwind = found.getUnwindBody().front();
                    auto success = llvm::cast<ctjs::InvokeYieldOp>(normal.back());
                    auto failure = llvm::cast<ctjs::InvokeYieldOp>(unwind.back());
                    auto failed = failure.getValues();
                    check(unwind.getNumArguments() == 16 && isTrue(failed[0]) &&
                              literalValue(failed[1]) == ctjs::UndefinedAttr::get(&context) &&
                              failed[2] == unwind.getArgument(0) &&
                              failed[3] == unwind.getArgument(1),
                          "actual unwind carries its thrown payload and saved anchor to catch");
                    if (next) {
                        advancing = found;
                        auto owner = llvm::dyn_cast<ctjs::TryOp>(found->getParentOp());
                        check(owner && found->getParentRegion() == &owner.getBody() &&
                                  owner->getBlock() == &entry.getBody().front(),
                              "next remains a direct invocation in its original root Try body");
                        ctjs::GetPropertyOp done, item;
                        for (auto read : normal.getOps<ctjs::GetPropertyOp>()) {
                            if (read.getObject() != normal.getArgument(0)) { continue; }
                            if (ctjs::constantKey(read.getKey()) == "done") { done = read; }
                            if (ctjs::constantKey(read.getKey()) == "value") { item = read; }
                        }
                        auto selected = success.getValues()[0].getDefiningOp<mlir::scf::IfOp>();
                        auto truth = selected
                                         ? selected.getCondition().getDefiningOp<ctjs::TruthyOp>()
                                         : ctjs::TruthyOp{};
                        check(done && item && done->isBeforeInBlock(item) && truth &&
                                  truth.getValue() == done.getResult(),
                              "only successful next reads its ordered own done/value fields");
                        unsigned failureCalls = 0;
                        found.getUnwindBody().walk([&](ctjs::CallOp) { ++failureCalls; });
                        check(!failureCalls, "next failure reaches catch without closing again");
                    } else {
                        auto guard = llvm::dyn_cast<mlir::scf::IfOp>(found->getParentOp());
                        if (!check(method && ctjs::constantKey(method.getKey()) == "return" &&
                                       guard &&
                                       found->getParentRegion() == &guard.getElseRegion() &&
                                       method->getParentRegion() == &guard.getElseRegion(),
                                   "close lookup and call remain together in the not-done arm")) {
                            return;
                        }
                        auto literal =
                            guard.getCondition().getDefiningOp<mlir::arith::ConstantOp>();
                        auto flag = literal ? llvm::dyn_cast<mlir::IntegerAttr>(literal.getValue())
                                            : mlir::IntegerAttr{};
                        check(flag && flag.getType().isInteger(1) &&
                                  flag.getValue().getBoolValue() == (closes == 0),
                              "exhaustion skips close while the saved return attempts it");
                        unsigned skippedCloses = 0;
                        guard.getThenRegion().walk([&](ctjs::GetPropertyOp read) {
                            skippedCloses += ctjs::constantKey(read.getKey()) == "return";
                        });
                        auto skipped =
                            llvm::cast<mlir::scf::YieldOp>(guard.getThenRegion().front().back());
                        check(!skippedCloses && guard.getNumResults() == found.getNumResults() &&
                                  literalValue(skipped.getOperand(0)) ==
                                      ctjs::BooleanAttr::get(&context, false),
                              "skipped close preserves its normal continuation and source effects");
                        if (closes == 1) {
                            check(literalValue(skipped.getOperand(1)) ==
                                      ctjs::NumberAttr::get(&context, std::bit_cast<uint64_t>(1.0)),
                                  "the original saved return survives the close guard");
                        }
                        check(isTrue(success.getValues()[0]) &&
                                  literalValue(success.getValues()[1]) ==
                                      ctjs::UndefinedAttr::get(&context) &&
                                  success.getValues()[2] == normal.getArgument(0) &&
                                  success.getValues()[3] == dispatch.getState()[0],
                              "normalized throwing close routes its payload and pre-call anchor");
                        ++closes;
                    }
                });
                entry.walk([&](ctjs::TryOp found) {
                    ++attempts;
                    auto & caught = found.getCatchBody().front();
                    auto exit = llvm::cast<ctjs::TryExitOp>(found.getBody().front().back());
                    check(found->getNumResults() == 1 &&
                              caught.getNumArguments() == caughtArguments &&
                              exit.getCaughtValues().size() == caughtArguments,
                          "Try keeps one result and its original caught tuple");
                    found.getCatchBody().walk([&](ctjs::CompareOp compare) {
                        if (compare.getKind() != ctjs::CompareKind::StrictEq) { return; }
                        ++caughtComparisons;
                        auto read = compare.getRhs().getDefiningOp<ctjs::CellGetOp>();
                        check(compare.getLhs() == caught.getArgument(0) && read &&
                                  read.getCell() == caught.getArgument(caughtAnchor),
                              "catch retains exact thrown-payload and saved-anchor cell identity");
                    });
                });
                check(!poison && kept == 3 && closes == 2 && advancing && attempts == 1 &&
                          caughtComparisons == 1,
                      "state transport retains next, both closes and the original local catch");
                if (control == 0 && structured) {
                    unsigned required = 0;
                    for (unsigned mutation = 0; mutation != 9; ++mutation) {
                        auto staged = mlir::parseSourceString<mlir::ModuleOp>(observedIteratorState,
                                                                              &context);
                        if (!check(static_cast<bool>(staged), "frozen observed state parses")) {
                            continue;
                        }
                        auto stagedEntry = *staged->getOps<ctjs::FuncOp>().begin();
                        mlir::OwningOpRef<ctjs::FuncOp> input(
                            llvm::cast<ctjs::FuncOp>(stagedEntry->clone()));
                        ctjs::InvokeOp next;
                        ctjs::TryOp protectedBody;
                        input->walk([&](ctjs::TryOp found) { protectedBody = found; });
                        input->walk([&](ctjs::InvokeOp found) {
                            auto call = llvm::cast<ctjs::CallOp>(found.getBody().front().front());
                            auto key = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                            if (key && ctjs::constantKey(key.getKey()) == "next") { next = found; }
                        });
                        auto exit = llvm::cast<ctjs::InvokeExitOp>(next.getBody().front().back());
                        unsigned inactive = 0;
                        while (inactive < exit.getState().size() &&
                               !exit.getState()[inactive].getDefiningOp<mlir::ub::PoisonOp>()) {
                            ++inactive;
                        }
                        if (!check(inactive < exit.getState().size(),
                                   "original next retains an inactive saved slot")) {
                            continue;
                        }
                        auto marker = exit.getState()[inactive];
                        if (mutation == 1) {
                            auto call = llvm::cast<ctjs::CallOp>(next.getBody().front().front());
                            call.getReceiverMutable().assign(marker);
                        } else if (mutation >= 2 && mutation <= 4) {
                            auto & block = mutation == 2   ? next.getUnwindBody().front()
                                           : mutation == 3 ? next.getNormalBody().front()
                                                           : protectedBody.getCatchBody().front();
                            auto at = mlir::OpBuilder::atBlockBegin(&block);
                            ctjs::StoreGlobalOp::create(
                                at, next.getLoc(), "inactive-observer",
                                mutation == 2 ? block.getArgument(inactive + 1) : marker);
                        } else if (mutation == 5) {
                            auto terminal =
                                llvm::cast<ctjs::TryExitOp>(protectedBody.getBody().front().back());
                            mlir::OpBuilder at(terminal);
                            auto unknown = ctjs::TruthyOp::create(
                                at, terminal.getLoc(), input->getBody().front().getArgument(3));
                            terminal->setOperand(0, unknown);
                        } else if (mutation == 6) {
                            auto & normal = next.getNormalBody().front();
                            auto at = mlir::OpBuilder::atBlockBegin(&normal);
                            ctjs::StoreGlobalOp::create(at, next.getLoc(), "normal-observer",
                                                        normal.getArgument(0));
                        }
                        const auto snapshot = printed(*input);
                        const unsigned budget = mutation == 7   ? 0
                                                : mutation == 8 ? required - 1
                                                                : 100000;
                        ctcompile::ctnative::dom_source_detail::DOMSource completion(budget);
                        const bool normalized = completion.normalizeCompletion(*input);
                        if (mutation == 0 || mutation == 6) {
                            if (!check(normalized, "observed iterator completion normalizes")) {
                                llvm::errs() << "completion: " << completion.reason << '\n';
                                continue;
                            }
                            if (mutation == 0) { required = budget - completion.remaining; }
                            unsigned poison = 0, invocations = 0, tries = 0, effects = 0;
                            input->walk([&](mlir::ub::PoisonOp) { ++poison; });
                            input->walk([&](ctjs::InvokeOp found) {
                                ++invocations;
                                auto state =
                                    llvm::cast<ctjs::InvokeExitOp>(found.getBody().front().back());
                                check(state.getState().size() == 15 &&
                                          llvm::all_of(found.getResultTypes(),
                                                       [](mlir::Type type) {
                                                           return llvm::isa<ctjs::ValueType>(type);
                                                       }),
                                      "normalized invocations retain saved slots and source result "
                                      "types");
                            });
                            input->walk([&](ctjs::TryOp found) {
                                ++tries;
                                check(found->getNumResults() == 1 &&
                                          found.getCatchBody().front().getNumArguments() ==
                                              caughtArguments,
                                      "normalization retains the Try result and caught tuple");
                                found.getCatchBody().walk([&](ctjs::CompareOp compare) {
                                    auto read = compare.getRhs().getDefiningOp<ctjs::CellGetOp>();
                                    check(read &&
                                              compare.getLhs() ==
                                                  found.getCatchBody().front().getArgument(0) &&
                                              read.getCell() ==
                                                  found.getCatchBody().front().getArgument(
                                                      caughtAnchor),
                                          "normalized catch preserves thrown payload and saved "
                                          "anchor identity");
                                });
                            });
                            input->walk([&](ctjs::StoreGlobalOp store) {
                                if (store.getName() != "normal-observer") { return; }
                                ++effects;
                                auto argument =
                                    llvm::dyn_cast<mlir::BlockArgument>(store.getValue());
                                auto invocation =
                                    llvm::dyn_cast<ctjs::InvokeOp>(store->getParentOp());
                                check(argument && invocation &&
                                          argument ==
                                              invocation.getNormalBody().front().getArgument(0),
                                      "normal observer stays outside the call body with its "
                                      "original payload");
                            });
                            check(!poison && invocations == 3 && tries == 1 &&
                                      effects == (mutation == 6 ? 1u : 0u) &&
                                      mlir::succeeded(mlir::verify(*input)),
                                  "completion removes only inactive transport and preserves "
                                  "guarded calls");
                        } else {
                            check(!normalized && printed(*input) == snapshot,
                                  "live poison, unknown exit flags and incomplete budgets refuse "
                                  "transactionally");
                            if (mutation >= 7) {
                                check(completion.reason.find("budget") != std::string::npos,
                                      "completion budget refusal retains its diagnostic");
                            }
                        }
                    }
                }
            } else if ((control == 0 || control >= 11) && control != 35 && structured) {
                check(openCall->getBlock() == openBlock &&
                          llvm::equal(openCall->getOperands(), openOperands) &&
                          mlir::succeeded(mlir::verify(*candidate)),
                      "proved open retains its exact call and source position outside Invoke");
                for (auto & expected : normalUses) {
                    auto value = expected.use->get();
                    check(expected.literal ? literalValue(value) == expected.literal
                          : expected.poison
                              ? static_cast<bool>(value.getDefiningOp<mlir::ub::PoisonOp>())
                              : value == expected.value,
                          "success and retained invocations keep every payload, tag, flag and "
                          "saved value");
                }
                for (auto close : closeCalls) {
                    check((close.getArgs()[0] == openCall.getResult()) == rewrittenClose(close),
                          "only calls with complete matching record/tag leaves forward identity");
                }
                unsigned kept = 0;
                entry.walk([&](ctjs::InvokeOp found) {
                    ++kept;
                    auto dispatch = llvm::cast<ctjs::InvokeExitOp>(found.getBody().front().back());
                    check(dispatch.getState().size() == 15,
                          "next and both closes retain their full pre-call state");
                });
                check(kept == 3, "only the independently proved open loses its failure edge");
                check((control == 12 || control == 13) ? !recordAlias.use_empty()
                                                       : recordAlias.use_empty(),
                      "only identical arms forward the record; unknown and poison arms retain "
                      "transport");
                if (selectionEffect) {
                    check(selectionEffect.getValue() ==
                              (control == 14 ? openCall.getResult() : holder),
                          "forwarding retains branch effects and exposes escaped alias observers");
                }
            } else if (control >= 7) {
                check(openCall->getParentOp() == opening,
                      "unproved open effects keep the original exceptional completion");
            }
        }
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
            llvm::SmallVector<std::pair<mlir::Operation *, llvm::SmallVector<mlir::Value>>>
                unchanged;
            invocation.walk([&](mlir::Operation * operation) {
                unchanged.emplace_back(operation,
                                       llvm::SmallVector<mlir::Value>(operation->getOperands()));
            });
            mlir::OpBuilder at(invocation);
            for (bool failed : {false, true}) {
                auto & selected =
                    (failed ? invocation.getUnwindBody() : invocation.getNormalBody()).front();
                auto incoming = llvm::cast<ctjs::InvokeYieldOp>(selected.back());
                mlir::Value payload = failed
                                          ? mlir::Value{function.getBody().front().getArgument(3)}
                                          : call.getResult();
                unsigned remaining = 100000;
                auto projected =
                    projectInvocationContinuation(invocation, failed, payload, at, remaining);
                if (!check(mlir::succeeded(projected),
                           "original iterator continuation tuple can be projected")) {
                    continue;
                }
                check(projected->size() == invocation.getNumResults(),
                      "projection keeps the entire original tuple width");
                for (auto [value, input] : llvm::zip(*projected, incoming.getValues())) {
                    if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(input);
                        argument && argument.getOwner() == &selected) {
                        auto expected = argument.getArgNumber() == 0
                                            ? payload
                                            : dispatch.getState()[argument.getArgNumber() - 1];
                        check(value == expected,
                              "projection separates call payload from every saved register");
                    } else if (auto * producer = input.getDefiningOp();
                               producer && producer->getBlock() == &selected) {
                        auto * copied = value.getDefiningOp();
                        check(copied && copied != producer &&
                                  copied->getName() == producer->getName() &&
                                  copied->getAttrs() == producer->getAttrs() &&
                                  value.getType() == input.getType(),
                              "projection copies original completion tags and inactive padding");
                    } else {
                        check(value == input, "projection retains successful outer SSA state");
                    }
                }
                unsigned operationCount = 0;
                invocation.walk([&](mlir::Operation *) { ++operationCount; });
                check(operationCount == unchanged.size() &&
                          llvm::all_of(unchanged,
                                       [](const auto & original) {
                                           return llvm::equal(original.first->getOperands(),
                                                              original.second);
                                       }),
                      "projection grants no authority to erase a call or its other edge");
                const unsigned used = 100000 - remaining;
                for (unsigned budget : {0u, used - 1}) {
                    const auto beforeProjection = printed(function);
                    auto refused =
                        projectInvocationContinuation(invocation, failed, payload, at, budget);
                    check(mlir::failed(refused) && printed(function) == beforeProjection,
                          "incomplete projection budgets leave all source IR unchanged");
                }
            }
            // A failed call result is never a saved pre-call register, even if
            // a caller forges otherwise well-typed invocation operands.
            auto saved = dispatch.getState()[0];
            dispatch.getStateMutable().slice(0, 1).assign(call.getResult());
            const auto invalid = printed(function);
            unsigned budget = 100000;
            mlir::ScopedDiagnosticHandler quiet(&context,
                                                [](mlir::Diagnostic &) { return mlir::success(); });
            auto refused = projectInvocationContinuation(
                invocation, true, function.getBody().front().getArgument(3), at, budget);
            check(mlir::failed(refused) && printed(function) == invalid,
                  "projection rejects a forged failed-result snapshot without rewriting");
            dispatch.getStateMutable().slice(0, 1).assign(saved);
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

static void testNumberAttributeOrigins(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    const std::string source = R"js(function guarded(element) {
        const saved = Number(element.getAttribute('data-number'));
        try { element.setAttribute('data-written', saved); throw element; }
        catch (error) { return error === element; }
    })js";
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        for (unsigned control = 0; control < 18; ++control) {
            auto text = source;
            if (control == 1) {
                replaceOnce(text, "try {", "try { element.setAttribute('data-number', '99');");
            }
            if (control == 2 || control == 16) {
                replaceOnce(text, "Number(element.getAttribute('data-number'))",
                            control == 2 ? "element.hasAttribute('flag') ? "
                                           "Number(element.getAttribute('data-number')) : 7.5"
                                         : "element.hasAttribute('flag') ? "
                                           "Number(element.getAttribute('data-number')) : element");
            }
            if (control == 10) {
                replaceOnce(text, "try {", "");
                replaceOnce(text, "const saved", "try { const saved");
            }
            if (control == 11) { replaceOnce(text, "const saved", "unknown(); const saved"); }
            if (control == 12) { replaceOnce(text, "data-written", "bad name"); }
            if (control == 17) { replaceOnce(text, "saved); throw", "saved + 1); throw"); }
            auto candidate = import(context, text, true);
            if (!candidate) { continue; }
            ctjs::LoadGlobalOp number;
            ctjs::CallOp conversion;
            candidate->walk([&](ctjs::LoadGlobalOp load) {
                if (load.getName() == "Number") { number = load; }
            });
            candidate->walk([&](ctjs::CallOp call) {
                if (number && call.getCallee() == number.getResult()) { conversion = call; }
            });
            // Inside try, status edges forward the callee through block arguments.
            if (!check(number && (conversion || control == 10),
                       "saved Number source retains its original call")) {
                continue;
            }
            auto element = guarded(*candidate).getBody().front().getArgument(3);
            mlir::Builder builder(&context);
            if (control == 4) { number.setName("Other"); }
            if (control == 5) { conversion.getReceiverMutable().assign(element); }
            if (control == 6) { conversion.getArgsMutable().append(element); }
            if (control == 7) { conversion.getArgsMutable().clear(); }
            if (control == 8) { conversion.getArgsMutable().assign(element); }
            if (control == 9) {
                mlir::OpBuilder at(number);
                ctjs::StoreGlobalOp::create(at, number.getLoc(), "Number", element);
            }
            number->setAttr("ctnative.host_intrinsic", builder.getStringAttr("Number"));
            if (conversion) {
                conversion->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            }
            HostContract request;
            request.provider = provider;
            request.entry = guarded(*candidate).getSymName().str();
            request.elementParameters = {0};
            if (control != 3) { request.initialIntrinsics = {"Number"}; }
            request.moduleSha256 = hostContractFingerprint(*candidate);
            const auto originalContract = request;
            if (control == 15) { number.setName("Other"); }
            const auto original = printed(*candidate);
            auto error = prepareDOMEntry(*candidate, request,
                                         control == 13   ? 0
                                         : control == 14 ? 500
                                                         : 100000);
            if (control >= 3 && control != 10 && control != 17) {
                checkRefused(
                    std::move(error),
                    printed(*candidate) == original && sameContract(request, originalContract),
                    "Number origin grants no binding, coercion, effect or budget authority");
                continue;
            }
            if (!check(!error, "saved Number origin completes the original protected write")) {
                llvm::errs() << llvm::toString(std::move(error)) << '\n';
                continue;
            }
            DOMEntryAnalysis proof(*candidate, request);
            check(proof.proved() && mlir::succeeded(mlir::verify(*candidate)),
                  "saved Number origin requires fresh complete DOM proof");
            llvm::SmallVector<ctjs::CallOp> calls;
            candidate->walk([&](ctjs::CallOp call) { calls.push_back(call); });
            if (control == 17 && check(calls.size() == 3, "saved arithmetic keeps three calls")) {
                auto add = calls.back().getArgs()[1].getDefiningOp<ctjs::BinaryOp>();
                check(add && add.getKind() == ctjs::BinaryKind::Add &&
                          add.getLhs() == calls[1].getResult() && calls[1]->isBeforeInBlock(add) &&
                          add->isBeforeInBlock(calls.back()),
                      "protected arithmetic consumes the saved Number before writing");
            }
            if (control < 2 && check(calls.size() == (control == 1 ? 4u : 3u),
                                     "saved read and Number conversion execute exactly once")) {
                const auto * read = proof.call(calls[0]);
                const auto * numeric = proof.call(calls[1]);
                const auto * write = proof.call(calls.back());
                check(read && read->kind == HostDOMMethod::getAttribute && numeric &&
                          numeric->kind == HostDOMMethod::number && write &&
                          write->kind == HostDOMMethod::setAttribute &&
                          calls[1].getArgs()[0] == calls[0].getResult() &&
                          calls.back().getArgs()[1] == calls[1].getResult() &&
                          calls[0]->isBeforeInBlock(calls[1]) &&
                          calls[1]->isBeforeInBlock(calls[2]) &&
                          (control != 1 || calls[2]->isBeforeInBlock(calls[3])),
                      "write uses the pre-mutation Number snapshot after the ordered read");
            }
        }
    }
}

static void testProtectedNumberReads(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    const std::string source = R"js(function guarded(element) {
        try {
            const saved = Number(element.getAttribute('data-number'));
            element.setAttribute('data-written', saved);
            throw element;
        } catch (error) { return error === element; }
    })js";
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        for (unsigned control = 0; control < 24; ++control) {
            auto text = source;
            if (control == 1) {
                replaceOnce(text, "element.setAttribute('data-written'",
                            "element.setAttribute('data-number', '99'); "
                            "element.setAttribute('data-written'");
            }
            if (control == 2) { replaceOnce(text, "throw element;", "return true;"); }
            if (control == 3 || control == 4) {
                replaceOnce(text, "element.getAttribute('data-number')",
                            control == 3 ? "'7.5'" : "null");
            }
            if (control == 5 || control == 23) {
                replaceOnce(text, "element.getAttribute('data-number')",
                            control == 5 ? "element.hasAttribute('flag') ? "
                                           "element.getAttribute('data-number') : null"
                                         : "element.hasAttribute('flag') ? "
                                           "element.getAttribute('data-number') : element");
            }
            if (control == 12) { replaceOnce(text, "getAttribute", "unknown"); }
            if (control == 13) { replaceOnce(text, "data-written", "bad name"); }
            if (control == 17) {
                replaceOnce(text, "'data-written', saved", "'data-written', element");
            }
            if (control == 18) { replaceOnce(text, "try {", "Number = element; try {"); }
            if (control == 19) { replaceOnce(text, "const saved", "unknown(); const saved"); }
            auto candidate = import(context, text, true);
            if (!candidate) { continue; }
            auto function = guarded(*candidate);
            llvm::SmallVector<ctjs::CallOp> originalCalls;
            ctjs::LoadGlobalOp number;
            function.walk([&](ctjs::CallOp call) { originalCalls.push_back(call); });
            function.walk([&](ctjs::LoadGlobalOp load) {
                if (load.getName() == "Number") { number = load; }
            });
            if (!check(number && originalCalls.size() >= 2,
                       "protected Number source retains original calls and binding")) {
                continue;
            }
            // Imported status edges forward the callee through block arguments;
            // source order identifies these calls without assuming direct SSA.
            auto conversion = originalCalls[1];
            auto read = originalCalls.front();
            auto element = function.getBody().front().getArgument(3);
            if (control == 7) { number.setName("Other"); }
            if (control == 8) { conversion.getReceiverMutable().assign(element); }
            if (control == 9) { conversion.getArgsMutable().append(element); }
            if (control == 10) { conversion.getArgsMutable().clear(); }
            if (control == 11) { conversion.getArgsMutable().assign(element); }
            if (control == 20) { read.getReceiverMutable().assign(read.getCallee()); }
            if (control == 21) { read.getArgsMutable().append(element); }
            if (control == 22) { read.getArgsMutable().assign(element); }
            mlir::Builder builder(&context);
            number->setAttr("ctnative.host_intrinsic", builder.getStringAttr("Number"));
            for (auto call : originalCalls) {
                call->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            }
            HostContract request;
            request.provider = provider;
            request.entry = function.getSymName().str();
            request.elementParameters = {0};
            if (control != 6) { request.initialIntrinsics = {"Number"}; }
            request.moduleSha256 = hostContractFingerprint(*candidate);
            const auto originalContract = request;
            if (control == 16) { number.setName("Other"); }
            const auto original = printed(*candidate);
            auto error = prepareDOMEntry(*candidate, request,
                                         control == 14   ? 0
                                         : control == 15 ? 500
                                                         : 100000);
            // Control 5 forwards the identical saved Number callee through both arms.
            if (control >= 6) {
                checkRefused(
                    std::move(error),
                    printed(*candidate) == original && sameContract(request, originalContract),
                    "protected conversion refuses incomplete identity, input, effect or budget");
                continue;
            }
            if (!check(!error, "protected read and conversion preserve original completion")) {
                llvm::errs() << "control " << control << ": " << llvm::toString(std::move(error))
                             << '\n';
                continue;
            }
            DOMEntryAnalysis proof(*candidate, request);
            check(proof.proved() && mlir::succeeded(mlir::verify(*candidate)),
                  "protected read and conversion require fresh complete DOM proof");
            if (control > 2) { continue; }
            llvm::SmallVector<ctjs::CallOp> calls;
            candidate->walk([&](ctjs::CallOp call) { calls.push_back(call); });
            if (!check(calls.size() == (control == 1 ? 4u : 3u),
                       "protected read, conversion and writes execute exactly once")) {
                continue;
            }
            const auto * getter = proof.call(calls[0]);
            const auto * numeric = proof.call(calls[1]);
            const auto * setter = proof.call(calls.back());
            check(getter && getter->kind == HostDOMMethod::getAttribute && numeric &&
                      numeric->kind == HostDOMMethod::number && setter &&
                      setter->kind == HostDOMMethod::setAttribute &&
                      calls[1].getArgs()[0] == calls[0].getResult() &&
                      calls.back().getArgs()[1] == calls[1].getResult() &&
                      calls[0]->isBeforeInBlock(calls[1]) && calls[1]->isBeforeInBlock(calls[2]) &&
                      (control != 1 || calls[2]->isBeforeInBlock(calls[3])),
                  "protected write consumes the exact read-time Number snapshot in source order");
        }
    }
}

static void testConditionalNumberCallee(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        for (unsigned variant = 0; variant < 38; ++variant) {
            const unsigned control = variant % 19;
            if (variant >= 19 && control <= 1) { continue; }
            const bool normalReturn = variant >= 19 || control == 1;
            std::string source = R"js(function guarded(element) {
                try {
                    const saved = Number(element.hasAttribute('flag') ?
                        element.getAttribute('data-number') : null);
                    element.setAttribute('data-written', saved);
                    throw element;
                } catch (error) { return error === element; }
            })js";
            if (normalReturn) { replaceOnce(source, "throw element;", "return true;"); }
            if (control == 2) {
                replaceOnce(source, "element.setAttribute('data-written'",
                            "element.setAttribute('data-number', '99'); "
                            "element.setAttribute('data-written'");
            }
            if (control == 3) {
                replaceOnce(source, "element.getAttribute('data-number')",
                            "element.hasAttribute('nested') ? "
                            "element.getAttribute('data-number') : '1'");
            }
            if (control == 4) {
                replaceOnce(source, "Number(element.hasAttribute('flag') ?",
                            "(element.hasAttribute('flag') ? Number : element)(true ?");
            }
            if (control == 5) {
                replaceOnce(source, "Number(element.hasAttribute('flag') ?",
                            "(element.hasAttribute('flag') ? Number : Number)(true ?");
            }
            if (control == 6) {
                replaceOnce(source, "const saved", "Number = element; const saved");
            }
            if (control == 7) { replaceOnce(source, " : null", " : element"); }
            if (control == 8 || control == 9) {
                replaceOnce(source, control == 8 ? "element.getAttribute('data-number')" : "null",
                            control == 8 ? "(unknown(), element.getAttribute('data-number'))"
                                         : "(unknown(), null)");
            }
            if (control == 10) {
                replaceOnce(source, "element.getAttribute('data-number')",
                            "(Number = element, '1')");
            }
            if (control == 11) { replaceOnce(source, "getAttribute", "unknown"); }
            if (control == 12) { replaceOnce(source, "data-written", "bad name"); }
            if (control == 18) {
                replaceOnce(
                    source, "const saved = Number(",
                    "let convert = Number; if (element.hasAttribute('other')) convert = element; "
                    "const saved = convert(");
            }
            auto candidate = import(context, source, true);
            if (!candidate) { continue; }
            auto function = guarded(*candidate);
            ctjs::LoadGlobalOp number;
            function.walk([&](ctjs::LoadGlobalOp load) {
                if (load.getName() == "Number") { number = load; }
            });
            if (!check(static_cast<bool>(number), "conditional source retains Number load")) {
                continue;
            }
            mlir::Builder builder(&context);
            number->setAttr("ctnative.host_intrinsic", builder.getStringAttr("Number"));
            function.walk([&](ctjs::CallOp call) {
                call->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            });
            HostContract request;
            request.provider = provider;
            request.entry = function.getSymName().str();
            request.elementParameters = {0};
            if (control != 13) { request.initialIntrinsics = {"Number"}; }
            if (control == 14) { number.setName("Other"); }
            request.moduleSha256 = hostContractFingerprint(*candidate);
            const auto originalContract = request;
            if (control == 15) { number.setName("Other"); }
            const auto original = printed(*candidate);
            auto error = prepareDOMEntry(*candidate, request,
                                         control == 16   ? 0
                                         : control == 17 ? 500
                                                         : 100000);
            if (control >= 4) {
                checkRefused(
                    std::move(error),
                    printed(*candidate) == original && sameContract(request, originalContract),
                    "conditional callee refuses changed identity, effects and incomplete proof");
                continue;
            }
            if (!check(!error, "identical saved conditional callee completes its DOM proof")) {
                llvm::errs() << "conditional callee variant " << variant << ": "
                             << llvm::toString(std::move(error)) << '\n';
                continue;
            }
            DOMEntryAnalysis proof(*candidate, request);
            check(proof.proved() && mlir::succeeded(mlir::verify(*candidate)),
                  "forwarded callee requires fresh complete DOM proof");
            if (normalReturn || control == 3) { continue; }
            ctjs::CallOp conversion, write;
            candidate->walk([&](ctjs::CallOp call) {
                const auto * method = proof.call(call);
                if (method && method->kind == HostDOMMethod::number) { conversion = call; }
                if (method && method->kind == HostDOMMethod::setAttribute) { write = call; }
            });
            auto callee = conversion ? conversion.getCallee().getDefiningOp<ctjs::LoadGlobalOp>()
                                     : ctjs::LoadGlobalOp{};
            auto selected = conversion ? conversion.getArgs()[0].getDefiningOp<mlir::scf::IfOp>()
                                       : mlir::scf::IfOp{};
            check(callee && selected && write && callee.getName() == "Number" &&
                      callee->getBlock() == selected->getBlock() &&
                      selected->getBlock() == conversion->getBlock() &&
                      conversion->getBlock() == write->getBlock() &&
                      callee->isBeforeInBlock(selected) && selected->isBeforeInBlock(conversion) &&
                      conversion->isBeforeInBlock(write) &&
                      write.getArgs()[1] == conversion.getResult(),
                  "conditional argument and later writes preserve the original callee snapshot");
        }
    }
}

static void testProtectedNumberArithmetic(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    const std::array expressions = {"converted + 1",
                                    "converted - 1",
                                    "converted * 2",
                                    "converted / 0",
                                    "converted % 3",
                                    "converted ** 2",
                                    "converted << 1",
                                    "converted >> 1",
                                    "converted >>> 1",
                                    "converted & 3",
                                    "converted | 3",
                                    "converted ^ 3",
                                    "-converted",
                                    "+converted",
                                    "~converted",
                                    "element.hasAttribute('flag') ? converted + 1 : converted - 1",
                                    "converted + element",
                                    "element + converted",
                                    "converted + '1'",
                                    "converted + true",
                                    "converted + null",
                                    "converted + 1n",
                                    "converted + unknown()",
                                    "element.hasAttribute('flag') ? converted : element"};
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        for (unsigned control = 0; control < 32; ++control) {
            std::string source = R"js(function guarded(element) {
                try {
                    const converted = Number(element.getAttribute('data-number'));
                    const saved = converted + 1;
                    element.setAttribute('data-written', saved);
                    throw element;
                } catch (error) { return error === element; }
            })js";
            if (control < expressions.size()) {
                replaceOnce(source, "converted + 1", expressions[control]);
            }
            if (control == 29) {
                replaceOnce(source, "const converted", "unknown(); const converted");
            }
            if (control == 30) { replaceOnce(source, "throw element;", "return true;"); }
            if (control == 31) {
                replaceOnce(source, "element.setAttribute('data-written'",
                            "element.setAttribute('data-number', '99'); "
                            "element.setAttribute('data-written'");
            }
            auto candidate = import(context, source, true);
            if (!candidate) { continue; }
            auto function = guarded(*candidate);
            ctjs::LoadGlobalOp number;
            ctjs::BinaryOp arithmetic;
            function.walk([&](ctjs::LoadGlobalOp load) {
                if (load.getName() == "Number") { number = load; }
            });
            function.walk([&](ctjs::BinaryOp binary) { arithmetic = binary; });
            if (!check(static_cast<bool>(number), "protected arithmetic retains Number load")) {
                continue;
            }
            mlir::Builder builder(&context);
            number->setAttr("ctnative.host_intrinsic", builder.getStringAttr("Number"));
            if (arithmetic) {
                arithmetic->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            }
            if (control == 26) {
                if (!check(static_cast<bool>(arithmetic), "arithmetic mutation has a producer")) {
                    continue;
                }
                arithmetic.getRhsMutable().assign(function.getBody().front().getArgument(3));
            }
            HostContract request;
            request.provider = provider;
            request.entry = function.getSymName().str();
            request.elementParameters = {0};
            if (control != 24) { request.initialIntrinsics = {"Number"}; }
            request.moduleSha256 = hostContractFingerprint(*candidate);
            const auto originalContract = request;
            if (control == 25) { number.setName("Other"); }
            const auto original = printed(*candidate);
            auto error = prepareDOMEntry(*candidate, request,
                                         control == 27   ? 0
                                         : control == 28 ? 500
                                                         : 100000);
            if (control >= 16 && control < 30) {
                checkRefused(
                    std::move(error),
                    printed(*candidate) == original && sameContract(request, originalContract),
                    "protected arithmetic refuses coercion, effects, stale proofs and budgets");
                continue;
            }
            if (!check(!error, "exact Number arithmetic completes its protected source")) {
                llvm::errs() << "arithmetic control " << control << ": "
                             << llvm::toString(std::move(error)) << '\n';
                continue;
            }
            DOMEntryAnalysis proof(*candidate, request);
            check(proof.proved() && mlir::succeeded(mlir::verify(*candidate)),
                  "protected arithmetic requires fresh complete DOM proof");
            if (control != 0 && control < 30) { continue; }
            llvm::SmallVector<ctjs::CallOp> calls;
            candidate->walk([&](ctjs::CallOp call) { calls.push_back(call); });
            if (!check(calls.size() == (control == 31 ? 4u : 3u),
                       "protected arithmetic keeps all read, conversion and write calls")) {
                continue;
            }
            auto add = calls.back().getArgs()[1].getDefiningOp<ctjs::BinaryOp>();
            check(add && add.getKind() == ctjs::BinaryKind::Add &&
                      calls[1].getArgs()[0] == calls[0].getResult() &&
                      add.getLhs() == calls[1].getResult() && calls[0]->isBeforeInBlock(calls[1]) &&
                      calls[1]->isBeforeInBlock(add) && add->isBeforeInBlock(calls[2]) &&
                      (control != 31 || calls[2]->isBeforeInBlock(calls[3])),
                  "arithmetic and writes retain the original converted snapshot and order");
        }
    }
}

static void testBooleanNumberInputs(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    const std::array inputs = {
        "anchor.hasAttribute('flag')",
        "true",
        "false",
        "anchor === anchor",
        "!anchor.hasAttribute('flag')",
        "anchor.hasAttribute('flag') === false",
        "anchor.hasAttribute('flag') ? true : false",
        "anchor.hasAttribute('flag') ? anchor.hasAttribute('other') : false",
        "!!anchor.hasAttribute('flag')",
        "anchor",
        "{}",
        "unknown()",
        "(unknown(), true)",
        "anchor.hasAttribute('flag') ? true : anchor",
        "anchor.hasAttribute('flag') ? (unknown(), true) : false",
        "anchor.hasAttribute('flag') ? true : (unknown(), false)",
        "unknown() === true",
        "anchor.unknown === true",
    };
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        for (unsigned control = 0; control < 32; ++control) {
            std::string source = R"js(function guarded(anchor) {
                try {
                    const saved = Number(anchor.hasAttribute('flag'));
                    anchor.setAttribute('data-written', saved);
                    return true;
                } catch (error) { return error === anchor; }
            })js";
            if (control < inputs.size()) {
                replaceOnce(source, "anchor.hasAttribute('flag')", inputs[control]);
            }
            if (control == 18) {
                replaceOnce(source, "anchor.setAttribute('data-written'",
                            "anchor.setAttribute('flag', ''); anchor.setAttribute('data-written'");
            }
            if (control == 19) {
                replaceOnce(source, "const saved = Number(anchor.hasAttribute('flag'));",
                            "const present = anchor.hasAttribute('flag'); "
                            "anchor.setAttribute('flag', ''); const saved = Number(present);");
            }
            if (control == 20) { replaceOnce(source, "return true;", "throw anchor;"); }
            if (control == 21) { replaceOnce(source, "try {", "Number = anchor; try {"); }
            if (control == 27) { replaceOnce(source, "data-written", "bad name"); }
            if (control == 31) {
                replaceOnce(source, "anchor.hasAttribute('flag')",
                            "anchor.hasAttribute('flag', unknown())");
            }
            auto candidate = import(context, source, true);
            if (!candidate) { continue; }
            auto function = guarded(*candidate);
            ctjs::LoadGlobalOp number;
            llvm::SmallVector<ctjs::CallOp> originalCalls;
            function.walk([&](ctjs::LoadGlobalOp load) {
                if (load.getName() == "Number") { number = load; }
            });
            function.walk([&](ctjs::CallOp call) { originalCalls.push_back(call); });
            if (!check(number && originalCalls.size() >= 2,
                       "Boolean conversion retains original Number binding and calls")) {
                continue;
            }
            // These controls retain the original direct read/conversion/write order;
            // imported completion state may forward their callees through arguments.
            auto conversion = originalCalls[1];
            auto anchor = function.getBody().front().getArgument(ctjs::implicit_arguments);
            if (control == 28) { conversion.getReceiverMutable().assign(anchor); }
            if (control == 29) { conversion.getArgsMutable().append(anchor); }
            if (control == 30) { conversion.getArgsMutable().clear(); }
            mlir::Builder builder(&context);
            number->setAttr("ctnative.host_intrinsic", builder.getStringAttr("Number"));
            for (auto call : originalCalls) {
                call->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            }
            HostContract request;
            request.provider = provider;
            request.entry = function.getSymName().str();
            request.elementParameters = {0};
            if (control != 22) { request.initialIntrinsics = {"Number"}; }
            if (control == 23) { number.setName("Other"); }
            request.moduleSha256 = hostContractFingerprint(*candidate);
            const auto originalContract = request;
            if (control == 24) { number.setName("Other"); }
            const auto original = printed(*candidate);
            auto error = prepareDOMEntry(*candidate, request,
                                         control == 25   ? 0
                                         : control == 26 ? 500
                                                         : 100000);
            const bool admitted = control < 9 || (control >= 18 && control <= 20);
            if (!admitted) {
                checkRefused(
                    std::move(error),
                    printed(*candidate) == original && sameContract(request, originalContract),
                    "Boolean conversion refuses coercion, effects, invalid bindings and budgets");
                continue;
            }
            if (!check(!error, "proved Boolean Number input completes its protected source")) {
                llvm::errs() << "Boolean Number control " << control << ": "
                             << llvm::toString(std::move(error)) << '\n';
                continue;
            }
            DOMEntryAnalysis proof(*candidate, request);
            check(proof.proved() && mlir::succeeded(mlir::verify(*candidate)),
                  "Boolean conversion still requires fresh complete DOM proof");
            if (control != 0 && control < 18) { continue; }
            llvm::SmallVector<ctjs::CallOp> calls;
            function = guarded(*candidate);
            function.walk([&](ctjs::CallOp call) { calls.push_back(call); });
            const bool snapshot = control == 18 || control == 19;
            if (!check(calls.size() == (snapshot ? 4u : 3u),
                       "Boolean read, conversion and ordered writes execute exactly once")) {
                continue;
            }
            const unsigned converted = control == 19 ? 2 : 1;
            const auto * read = proof.call(calls.front());
            const auto * numeric = proof.call(calls[converted]);
            const auto * write = proof.call(calls.back());
            bool ordered = true;
            for (unsigned index = 1; index < calls.size(); ++index) {
                ordered &= calls[index - 1]->getBlock() == calls[index]->getBlock() &&
                           calls[index - 1]->isBeforeInBlock(calls[index]);
            }
            check(read && read->kind == HostDOMMethod::hasAttribute && numeric &&
                      numeric->kind == HostDOMMethod::number && write &&
                      write->kind == HostDOMMethod::setAttribute && ordered &&
                      calls[converted].getArgs()[0] == calls.front().getResult() &&
                      calls.back().getArgs()[1] == calls[converted].getResult(),
                  "Number and later writes consume the Boolean snapshot from its original read");
        }
    }
}

static void testNumberNumberInputs(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    const std::array inputs = {
        "Number(anchor.hasAttribute('flag'))",
        "Number(anchor.getAttribute('data-number'))",
        "Number(anchor.hasAttribute('flag')) + 1",
        "-0",
        "0 / 0",
        "anchor.hasAttribute('flag') ? Number(anchor.getAttribute('data-number')) : -0",
        "Number(anchor.hasAttribute('flag')) + anchor",
        "anchor.hasAttribute('flag') ? Number(anchor.getAttribute('data-number')) : anchor",
        "anchor.hasAttribute('flag') ? (unknown(), Number('1')) : 0",
        "anchor.hasAttribute('flag') ? Number('1') : (unknown(), 0)",
    };
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        for (unsigned control = 0; control < 24; ++control) {
            std::string source = R"js(function guarded(anchor) {
                try {
                    const saved = Number(Number(anchor.hasAttribute('flag')));
                    anchor.setAttribute('data-written', saved);
                    return true;
                } catch (error) { return error === anchor; }
            })js";
            if (control < inputs.size()) {
                replaceOnce(source, "Number(anchor.hasAttribute('flag'))", inputs[control]);
            }
            if (control == 10) {
                replaceOnce(source, "const saved = Number(Number(anchor.hasAttribute('flag')));",
                            "const numeric = Number(anchor.hasAttribute('flag')); "
                            "anchor.setAttribute('flag', ''); const saved = Number(numeric);");
            }
            if (control == 11) { replaceOnce(source, "return true;", "throw anchor;"); }
            if (control == 12) { replaceOnce(source, "try {", "Number = anchor; try {"); }
            if (control == 18) { replaceOnce(source, "data-written", "bad name"); }
            if (control == 23) {
                replaceOnce(source, "const saved = Number(",
                            "const saved = (anchor.hasAttribute('other') ? Number : anchor)(");
            }
            auto candidate = import(context, source, true);
            if (!candidate) { continue; }
            auto function = guarded(*candidate);
            llvm::SmallVector<ctjs::LoadGlobalOp> numbers;
            llvm::SmallVector<ctjs::CallOp> originalCalls;
            function.walk([&](ctjs::LoadGlobalOp load) {
                if (load.getName() == "Number") { numbers.push_back(load); }
            });
            function.walk([&](ctjs::CallOp call) { originalCalls.push_back(call); });
            if (!check(!numbers.empty() && originalCalls.size() >= 2,
                       "Number input retains its original binding and calls")) {
                continue;
            }
            // Only the direct nested source is mutated below: read, inner Number,
            // outer Number, write. Completion transport may hide its saved callee.
            auto anchor = function.getBody().front().getArgument(ctjs::implicit_arguments);
            if (control == 19) { originalCalls[2].getReceiverMutable().assign(anchor); }
            if (control == 20) { originalCalls[2].getArgsMutable().append(anchor); }
            if (control == 21) { originalCalls[2].getArgsMutable().clear(); }
            mlir::Builder builder(&context);
            for (auto number : numbers) {
                number->setAttr("ctnative.host_intrinsic", builder.getStringAttr("Number"));
            }
            for (auto call : originalCalls) {
                call->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            }
            HostContract request;
            request.provider = provider;
            request.entry = function.getSymName().str();
            request.elementParameters = {0};
            if (control != 13) { request.initialIntrinsics = {"Number"}; }
            if (control == 14) { numbers.front().setName("Other"); }
            if (control == 22) { numbers.back().setName("Other"); }
            request.moduleSha256 = hostContractFingerprint(*candidate);
            const auto originalContract = request;
            if (control == 15) { numbers.front().setName("Other"); }
            const auto original = printed(*candidate);
            auto error = prepareDOMEntry(*candidate, request,
                                         control == 16   ? 0
                                         : control == 17 ? 500
                                                         : 100000);
            const bool admitted = control < 6 || control == 10 || control == 11;
            if (!admitted) {
                checkRefused(
                    std::move(error),
                    printed(*candidate) == original && sameContract(request, originalContract),
                    "Number input refuses coercion, effects, invalid bindings and budgets");
                continue;
            }
            if (!check(!error, "exact Number input completes its protected source")) {
                llvm::errs() << "Number input control " << control << ": "
                             << llvm::toString(std::move(error)) << '\n';
                continue;
            }
            DOMEntryAnalysis proof(*candidate, request);
            check(proof.proved() && mlir::succeeded(mlir::verify(*candidate)),
                  "Number input still requires fresh complete DOM proof");
            if (control != 0 && control != 10 && control != 11) { continue; }
            llvm::SmallVector<ctjs::CallOp> calls;
            guarded(*candidate).walk([&](ctjs::CallOp call) { calls.push_back(call); });
            if (!check(calls.size() == (control == 10 ? 5u : 4u),
                       "nested conversions and ordered writes execute exactly once")) {
                continue;
            }
            const unsigned outer = control == 10 ? 3 : 2;
            const auto * innerProof = proof.call(calls[1]);
            const auto * outerProof = proof.call(calls[outer]);
            bool ordered = true;
            for (unsigned index = 1; index < calls.size(); ++index) {
                ordered &= calls[index - 1]->getBlock() == calls[index]->getBlock() &&
                           calls[index - 1]->isBeforeInBlock(calls[index]);
            }
            check(innerProof && innerProof->kind == HostDOMMethod::number && outerProof &&
                      outerProof->kind == HostDOMMethod::number && ordered &&
                      calls[1].getArgs()[0] == calls.front().getResult() &&
                      calls[outer].getArgs()[0] == calls[1].getResult() &&
                      calls.back().getArgs()[1] == calls[outer].getResult(),
                  "nested conversion consumes the saved Number despite later DOM mutation");
        }
    }
}

static void testProtectedNumberStrings(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    const std::array beforeTryControls = {0u,  1u,  3u,  4u,  5u,  7u,  8u,  9u,
                                          10u, 13u, 14u, 15u, 16u, 17u, 18u, 19u};
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        for (unsigned index = 0; index < 19 + beforeTryControls.size(); ++index) {
            const bool beforeTry = index >= 19;
            const unsigned control = beforeTry ? beforeTryControls[index - 19] : index;
            std::string source = R"js(function guarded(anchor) {
                try {
                    const saved = Number(anchor.getAttribute('data-number')).toString();
                    anchor.setAttribute('data-written', saved);
                    return true;
                } catch (error) { return error === anchor; }
            })js";
            if (control == 1 || control == 2) {
                replaceOnce(source, "anchor.setAttribute('data-written', saved);",
                            "anchor.setAttribute('data-number', '99'); "
                            "anchor.setAttribute('data-written', saved);");
            }
            if (control == 2) {
                replaceOnce(source, "Number(anchor.getAttribute('data-number')).toString()",
                            "anchor.getAttribute('data-number')");
            }
            if (control == 3) {
                replaceOnce(source, "Number(anchor.getAttribute('data-number'))",
                            "(1 / Number(anchor.getAttribute('data-number')))");
            }
            if (control == 4) { replaceOnce(source, "return true;", "throw anchor;"); }
            if (control == 5) { replaceOnce(source, ".toString()", ".toString(16)"); }
            if (control == 6) { replaceOnce(source, ".toString()", ".toString(anchor)"); }
            if (control == 7) {
                replaceOnce(source, "anchor.getAttribute('data-number')", "anchor");
            }
            if (control == 8) { replaceOnce(source, "try {", "Number = anchor; try {"); }
            if (control == 9) {
                replaceOnce(source, "try {", "Number.prototype.toString = anchor; try {");
            }
            if (control == 10) { replaceOnce(source, "const saved =", "unknown(); const saved ="); }
            if (control == 11) { replaceOnce(source, "data-written", "bad name"); }
            if (control == 12) {
                replaceOnce(source, "data-written', saved", "data-written', null");
            }
            if (beforeTry) {
                const auto begin = source.find("const saved =");
                const auto end = source.find(';', begin) + 1;
                const auto saved = source.substr(begin, end - begin);
                source.erase(begin, end - begin);
                source.insert(source.find("try {"), saved + " ");
            }
            if (control == 19) { replaceOnce(source, "const saved =", "unknown(); const saved ="); }
            auto candidate = import(context, source, true);
            if (!candidate) { continue; }
            auto function = guarded(*candidate);
            llvm::SmallVector<ctjs::CallOp> calls;
            function.walk([&](ctjs::CallOp call) { calls.push_back(call); });
            if (!check(calls.size() >= 3, "Number String source retains its original calls")) {
                continue;
            }
            auto anchor = function.getBody().front().getArgument(ctjs::implicit_arguments);
            if (control == 13) { calls[2].getReceiverMutable().assign(anchor); }
            if (control == 14) { calls[2].getCalleeMutable().assign(calls[1].getCallee()); }
            mlir::Builder builder(&context);
            for (auto call : calls) {
                call->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            }
            HostContract request;
            request.provider = provider;
            request.entry = function.getSymName().str();
            request.elementParameters = {0};
            if (control != 15) { request.initialIntrinsics = {"Number"}; }
            request.moduleSha256 = hostContractFingerprint(*candidate);
            if (control == 16) { request.moduleSha256 = "stale"; }
            const auto originalContract = request;
            const auto original = printed(*candidate);
            auto error = prepareDOMEntry(*candidate, request,
                                         control == 17   ? 0
                                         : control == 18 ? 500
                                                         : 100000);
            if (control >= 5) {
                checkRefused(
                    std::move(error),
                    printed(*candidate) == original && sameContract(request, originalContract),
                    "Number String refusal preserves source and contract despite forged proof");
                continue;
            }
            if (!check(!error, "protected Number method and saved String complete")) {
                llvm::errs() << "Number String control " << index << ": "
                             << llvm::toString(std::move(error)) << '\n';
                continue;
            }
            DOMEntryAnalysis proof(*candidate, request);
            check(proof.proved() && mlir::succeeded(mlir::verify(*candidate)),
                  "Number String completion requires fresh complete DOM proof");
            calls.clear();
            guarded(*candidate).walk([&](ctjs::CallOp call) { calls.push_back(call); });
            if (!check(calls.size() == (control == 1   ? 5u
                                        : control == 2 ? 3u
                                                       : 4u),
                       "Number String conversion and DOM writes occur exactly once")) {
                continue;
            }
            bool ordered = true;
            for (unsigned index = 1; index < calls.size(); ++index) {
                ordered &= calls[index - 1]->getBlock() == calls[index]->getBlock() &&
                           calls[index - 1]->isBeforeInBlock(calls[index]);
            }
            const unsigned saved = control == 2 ? 0 : 2;
            const auto * savedProof = proof.call(calls[saved]);
            check(ordered && savedProof &&
                      savedProof->kind == (control == 2 ? HostDOMMethod::getAttribute
                                                        : HostDOMMethod::numberToString) &&
                      calls.back().getArgs()[1] == calls[saved].getResult(),
                  "writes consume the saved String/null result after intervening mutations");
        }
    }
}

static void testSavedAttributeStrings(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        for (unsigned control = 0; control < 24; ++control) {
            std::string source = R"js(function guarded(anchor) {
                const saved = anchor.getAttribute('data-number');
                try {
                    anchor.setAttribute('data-written', saved);
                    return true;
                } catch (error) { return error === anchor; }
            })js";
            if (control == 1) {
                replaceOnce(source, "try {", "try { anchor.setAttribute('data-number', '99');");
            }
            if (control == 2) {
                replaceOnce(source, "try {", "anchor.setAttribute('data-number', '99'); try {");
            }
            if (control == 3) {
                replaceOnce(source, "try {",
                            "const alias = anchor; "
                            "alias.setAttribute('data-number', '99'); try {");
            }
            if (control == 4) { replaceOnce(source, "return true;", "throw anchor;"); }
            if (control == 5) {
                replaceOnce(source, "anchor.getAttribute('data-number')", "unknown()");
            }
            if (control == 6) { replaceOnce(source, "const saved =", "unknown(); const saved ="); }
            if (control == 7) { replaceOnce(source, "try {", "try { unknown();"); }
            if (control == 8) {
                replaceOnce(source, "anchor.getAttribute('data-number')",
                            "(unknown(), anchor.getAttribute('data-number'))");
            }
            if (control == 9) {
                replaceOnce(source, "const saved =", "anchor.getAttribute = anchor; const saved =");
            }
            if (control == 10) { replaceOnce(source, "data-written", "bad name"); }
            if (control == 11) {
                replaceOnce(source, "data-written', saved", "data-written', null");
            }
            if (control == 16) { replaceOnce(source, ".getAttribute(", ".unknown("); }
            if (control == 21) { replaceOnce(source, "try {", "anchor = false; try {"); }
            if (control == 22) {
                replaceOnce(source, "getAttribute('data-number')", "getAttribute(anchor)");
            }
            if (control == 23) {
                replaceOnce(source, "const saved =", "let saved =");
                replaceOnce(source, "try {", "saved = anchor; try {");
            }
            auto candidate = import(context, source, true);
            if (!candidate) { continue; }
            auto function = guarded(*candidate);
            llvm::SmallVector<ctjs::CallOp> calls;
            function.walk([&](ctjs::CallOp call) { calls.push_back(call); });
            if (!check(calls.size() >= 2, "saved attribute source retains producer and write")) {
                continue;
            }
            auto anchor = function.getBody().front().getArgument(ctjs::implicit_arguments);
            if (control == 12) {
                calls.front().getReceiverMutable().assign(
                    function.getBody().front().getArgument(0));
            }
            if (control == 13) { calls.front().getCalleeMutable().assign(anchor); }
            if (control == 14) { calls.front().getArgsMutable().append(anchor); }
            if (control == 15) { calls.front().getArgsMutable().clear(); }
            mlir::Builder builder(&context);
            for (auto call : calls) {
                call->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            }
            HostContract request;
            request.provider = provider;
            request.entry = function.getSymName().str();
            if (control != 18) { request.elementParameters = {0}; }
            request.moduleSha256 = hostContractFingerprint(*candidate);
            if (control == 17) { request.moduleSha256 = "stale"; }
            const auto originalContract = request;
            const auto original = printed(*candidate);
            auto error = prepareDOMEntry(*candidate, request,
                                         control == 19   ? 0
                                         : control == 20 ? 500
                                                         : 100000);
            if (control >= 5) {
                checkRefused(
                    std::move(error),
                    printed(*candidate) == original && sameContract(request, originalContract),
                    "saved attribute refusal preserves source and contract despite forged proof");
                continue;
            }
            if (!check(!error, "saved nullable attribute completes its original protected write")) {
                llvm::errs() << "saved attribute control " << control << ": "
                             << llvm::toString(std::move(error)) << '\n';
                continue;
            }
            DOMEntryAnalysis proof(*candidate, request);
            check(proof.proved() && mlir::succeeded(mlir::verify(*candidate)),
                  "saved attribute still requires fresh complete DOM proof");
            calls.clear();
            guarded(*candidate).walk([&](ctjs::CallOp call) { calls.push_back(call); });
            const bool mutation = control >= 1 && control <= 3;
            if (!check(calls.size() == (mutation ? 3u : 2u),
                       "saved attribute read and ordered writes occur exactly once")) {
                continue;
            }
            bool ordered = true;
            for (unsigned index = 1; index < calls.size(); ++index) {
                ordered &= calls[index - 1]->getBlock() == calls[index]->getBlock() &&
                           calls[index - 1]->isBeforeInBlock(calls[index]);
            }
            const auto * read = proof.call(calls.front());
            const auto * write = proof.call(calls.back());
            check(
                ordered && read && read->kind == HostDOMMethod::getAttribute && write &&
                    write->kind == HostDOMMethod::setAttribute &&
                    calls.back().getArgs()[1] == calls.front().getResult(),
                "write consumes the original nullable snapshot across direct and alias mutations");
        }
    }
}

static void testSelectedAttributeStrings(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        for (unsigned control = 0; control < 24; ++control) {
            std::string source = R"js(function guarded(anchor) {
                const saved = anchor.hasAttribute('flag') ? anchor.getAttribute('data-number') : null;
                try {
                    anchor.setAttribute('data-written', saved);
                    return true;
                } catch (error) { return error === anchor; }
            })js";
            if (control == 1) {
                replaceOnce(source, "try {", "try { anchor.setAttribute('data-number', '99');");
            }
            if (control == 2) {
                replaceOnce(source, "try {", "anchor.setAttribute('data-number', '99'); try {");
            }
            if (control == 3) {
                replaceOnce(source, "anchor.getAttribute('data-number') : null",
                            "null : anchor.getAttribute('data-number')");
            }
            if (control == 4 || control == 22) {
                replaceOnce(
                    source, "anchor.getAttribute('data-number')",
                    "(anchor.hasAttribute('inner') ? anchor.getAttribute('data-number') : null)");
                if (control == 22) { replaceOnce(source, ": null)", ": anchor)"); }
            }
            if (control == 5) { replaceOnce(source, ": null", ": anchor.getAttribute('other')"); }
            if (control == 6) { replaceOnce(source, "return true;", "throw anchor;"); }
            if (control == 7) {
                replaceOnce(source, "anchor.getAttribute('data-number')", "anchor");
            }
            if (control == 8) { replaceOnce(source, ": null", ": anchor"); }
            if (control == 9) { replaceOnce(source, "anchor.hasAttribute('flag')", "unknown()"); }
            if (control == 10) {
                replaceOnce(source, "anchor.getAttribute('data-number')",
                            "(unknown(), anchor.getAttribute('data-number'))");
            }
            if (control == 11) { replaceOnce(source, ": null", ": (unknown(), null)"); }
            if (control == 12) { replaceOnce(source, "try {", "try { unknown();"); }
            if (control == 13) {
                replaceOnce(source, "const saved =", "anchor.getAttribute = anchor; const saved =");
            }
            if (control == 14) { replaceOnce(source, "data-written", "bad name"); }
            if (control == 23) { replaceOnce(source, ": null", ": (anchor = false, null)"); }
            auto candidate = import(context, source, true);
            if (!candidate) { continue; }
            auto function = guarded(*candidate);
            llvm::SmallVector<ctjs::CallOp> calls;
            function.walk([&](ctjs::CallOp call) { calls.push_back(call); });
            if (!check(calls.size() >= 2, "selected attribute source retains its calls")) {
                continue;
            }
            auto anchor = function.getBody().front().getArgument(ctjs::implicit_arguments);
            if (control == 15) {
                calls[1].getReceiverMutable().assign(function.getBody().front().getArgument(0));
            }
            if (control == 16) { calls[1].getCalleeMutable().assign(anchor); }
            if (control == 17) { calls[1].getArgsMutable().append(anchor); }
            mlir::Builder builder(&context);
            for (auto call : calls) {
                call->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            }
            HostContract request;
            request.provider = provider;
            request.entry = function.getSymName().str();
            if (control != 18) { request.elementParameters = {0}; }
            request.moduleSha256 = hostContractFingerprint(*candidate);
            if (control == 19) { request.moduleSha256 = "stale"; }
            const auto originalContract = request;
            const auto original = printed(*candidate);
            auto error = prepareDOMEntry(*candidate, request,
                                         control == 20   ? 0
                                         : control == 21 ? 500
                                                         : 100000);
            if (control >= 7) {
                checkRefused(
                    std::move(error),
                    printed(*candidate) == original && sameContract(request, originalContract),
                    "selected attribute refusal preserves both arms and the whole transaction");
                continue;
            }
            if (!check(!error, "selected nullable attribute completes its protected write")) {
                llvm::errs() << "selected attribute control " << control << ": "
                             << llvm::toString(std::move(error)) << '\n';
                continue;
            }
            DOMEntryAnalysis proof(*candidate, request);
            check(proof.proved() && mlir::succeeded(mlir::verify(*candidate)),
                  "selected attribute requires fresh complete DOM proof");
            unsigned reads = 0, predicates = 0;
            llvm::SmallVector<ctjs::CallOp> writes;
            guarded(*candidate).walk([&](ctjs::CallOp call) {
                const auto * method = proof.call(call);
                if (!check(method != nullptr, "every retained selected call has DOM proof")) {
                    return;
                }
                reads += method->kind == HostDOMMethod::getAttribute;
                predicates += method->kind == HostDOMMethod::hasAttribute;
                if (method->kind == HostDOMMethod::setAttribute) { writes.push_back(call); }
            });
            const bool mutation = control == 1 || control == 2;
            if (!check(reads == (control == 5 ? 2u : 1u) &&
                           predicates == (control == 4 ? 2u : 1u) &&
                           writes.size() == (mutation ? 2u : 1u),
                       "each selected read, predicate and ordered write occurs exactly once")) {
                continue;
            }
            auto selected = writes.back().getArgs()[1].getDefiningOp<mlir::scf::IfOp>();
            check(selected && selected->getBlock() == writes.front()->getBlock() &&
                      selected->isBeforeInBlock(writes.front()) &&
                      (!mutation || writes.front()->isBeforeInBlock(writes.back())),
                  "protected write consumes the selected snapshot before either later mutation");
        }
    }
}

enum class SavedSelection {
    boolean,
    nullableBoolean,
    nullableNumber
};

static void testSavedAttributePredicates(mlir::MLIRContext & context, SavedSelection mode) {
    using namespace ctcompile::ctnative;
    const bool nullable = mode != SavedSelection::boolean;
    const bool number = mode == SavedSelection::nullableNumber;
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        for (unsigned control = 0; control < (number ? 36u : 30u); ++control) {
            if (nullable && (control == 1 || control == 4)) { continue; }
            // Number mode replaces the data-number producer with 42; 9, 18 and 19
            // already replaced it, so they would repeat the null mode's sources.
            if (number && (control == 9 || control == 18 || control == 19)) { continue; }
            std::string source = R"js(function guarded(anchor) {
                const saved = anchor.hasAttribute('flag') ? anchor.hasAttribute('data-number') : false;
                try {
                    anchor.setAttribute('data-written', saved);
                    return true;
                } catch (error) { return error === anchor; }
            })js";
            if (control == 1) {
                replaceOnce(
                    source,
                    "anchor.hasAttribute('flag') ? anchor.hasAttribute('data-number') : false",
                    "anchor.hasAttribute('data-number')");
            }
            if (control == 2) {
                replaceOnce(source, "anchor.hasAttribute('data-number') : false",
                            "false : anchor.hasAttribute('data-number')");
            }
            if (control == 3 || control == 11) {
                replaceOnce(
                    source, "anchor.hasAttribute('data-number')",
                    "(anchor.hasAttribute('inner') ? anchor.hasAttribute('data-number') : false)");
                if (control == 11) { replaceOnce(source, ": false)", ": anchor)"); }
            }
            if (control == 4) { replaceOnce(source, ": false", ": anchor.hasAttribute('other')"); }
            if (control == 5) {
                replaceOnce(source, "try {", "try { anchor.setAttribute('data-number', '99');");
            }
            if (control == 6) {
                replaceOnce(source, "try {", "anchor.setAttribute('data-number', '99'); try {");
            }
            if (control == 7) {
                replaceOnce(source, "try {",
                            "const alias = anchor; "
                            "alias.setAttribute('data-number', '99'); try {");
            }
            if (control == 8) { replaceOnce(source, "return true;", "throw anchor;"); }
            if (control == 9) {
                replaceOnce(source, "anchor.hasAttribute('data-number')", "anchor");
            }
            if (control == 10) { replaceOnce(source, ": false", ": anchor"); }
            if (control == 12) { replaceOnce(source, "anchor.hasAttribute('flag')", "unknown()"); }
            if (control == 13) {
                replaceOnce(source, "anchor.hasAttribute('data-number')",
                            "(unknown(), anchor.hasAttribute('data-number'))");
            }
            if (control == 14) { replaceOnce(source, ": false", ": (unknown(), false)"); }
            if (control == 15) { replaceOnce(source, "try {", "try { unknown();"); }
            if (control == 16) {
                replaceOnce(source, "const saved =", "anchor.hasAttribute = anchor; const saved =");
            }
            if (control == 17) { replaceOnce(source, "data-written", "bad name"); }
            if (control == 18) {
                replaceOnce(source, "anchor.hasAttribute('data-number')",
                            "anchor.unknown('data-number')");
            }
            if (control == 19) {
                replaceOnce(source, "anchor.hasAttribute('data-number')",
                            "anchor.hasAttribute(anchor)");
            }
            if (control == 28) { replaceOnce(source, ": false", ": (anchor = false, false)"); }
            if (control == 29) { replaceOnce(source, "const saved =", "unknown(); const saved ="); }
            if (nullable) {
                for (auto offset = source.find("false"); offset != std::string::npos;
                     offset = source.find("false", offset + 4)) {
                    source.replace(offset, 5, "null");
                }
            }
            if (number) {
                replaceOnce(source, "anchor.hasAttribute('data-number')", "42");
                if (control == 30) { replaceOnce(source, "? 42", "? (40 + 2)"); }
                if (control == 31) {
                    replaceOnce(source, ": null", ": (anchor.hasAttribute('inner') ? null : -7)");
                }
                if (control == 32) {
                    replaceOnce(source, "'data-written', saved",
                                "'data-written', saved.toString()");
                }
                if (control == 33) { replaceOnce(source, "? 42", "? -anchor"); }
                if (control == 34) { replaceOnce(source, ": null", ": 'text'"); }
                if (control == 35) { replaceOnce(source, ": null", ": undefined"); }
            }
            auto candidate = import(context, source, true);
            if (!candidate) { continue; }
            auto function = guarded(*candidate);
            llvm::SmallVector<ctjs::CallOp> calls;
            function.walk([&](ctjs::CallOp call) { calls.push_back(call); });
            if (!check(calls.size() >= 2, "saved predicate source retains producer and write")) {
                continue;
            }
            auto anchor = function.getBody().front().getArgument(ctjs::implicit_arguments);
            if (control == 20) {
                calls[1].getReceiverMutable().assign(function.getBody().front().getArgument(0));
            }
            if (control == 21) { calls[1].getCalleeMutable().assign(anchor); }
            if (control == 22) { calls[1].getArgsMutable().append(anchor); }
            if (control == 23) { calls[1].getArgsMutable().clear(); }
            mlir::Builder builder(&context);
            for (auto call : calls) {
                call->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            }
            HostContract request;
            request.provider = provider;
            request.entry = function.getSymName().str();
            if (control != 24) { request.elementParameters = {0}; }
            // With the initial Number.prototype.toString, only the null arm refuses.
            if (control == 32) { request.initialIntrinsics = {"Number"}; }
            request.moduleSha256 = hostContractFingerprint(*candidate);
            if (control == 25) { request.moduleSha256 = "stale"; }
            const auto originalContract = request;
            const auto original = printed(*candidate);
            auto error = prepareDOMEntry(*candidate, request,
                                         control == 26   ? 0
                                         : control == 27 ? 500
                                                         : 100000);
            if (control >= 9 && control != 30 && control != 31) {
                checkRefused(
                    std::move(error),
                    printed(*candidate) == original && sameContract(request, originalContract),
                    "saved predicate refusal preserves both arms and the whole transaction");
                continue;
            }
            if (!check(!error, "saved Boolean predicate completes its protected write")) {
                llvm::errs() << "saved predicate control " << control << " nullable=" << nullable
                             << " number=" << number << ": " << llvm::toString(std::move(error))
                             << '\n';
                continue;
            }
            DOMEntryAnalysis proof(*candidate, request);
            check(proof.proved() && mlir::succeeded(mlir::verify(*candidate)),
                  "saved predicate requires fresh complete DOM proof");
            llvm::SmallVector<ctjs::CallOp> predicates, writes;
            guarded(*candidate).walk([&](ctjs::CallOp call) {
                const auto * method = proof.call(call);
                if (!check(method != nullptr, "every retained predicate call has DOM proof")) {
                    return;
                }
                if (method->kind == HostDOMMethod::hasAttribute) { predicates.push_back(call); }
                if (method->kind == HostDOMMethod::setAttribute) { writes.push_back(call); }
            });
            const bool mutation = control >= 5 && control <= 7;
            // Number mode's 42 stands where the data-number predicate was.
            const unsigned expected = (control == 1                                    ? 1u
                                       : control == 3 || control == 4 || control == 31 ? 3u
                                                                                       : 2u) -
                                      (number ? 1u : 0u);
            if (!check(predicates.size() == expected && writes.size() == (mutation ? 2u : 1u),
                       "each predicate and ordered write occurs exactly once")) {
                continue;
            }
            auto saved = writes.back().getArgs()[1];
            auto selected = saved.getDefiningOp<mlir::scf::IfOp>();
            auto * origin =
                control == 1 ? predicates.front().getOperation() : selected.getOperation();
            check((control == 1 ? saved == predicates.front().getResult() : bool(selected)) &&
                      origin && origin->getBlock() == writes.front()->getBlock() &&
                      origin->isBeforeInBlock(writes.front()) &&
                      (!mutation || writes.front()->isBeforeInBlock(writes.back())),
                  "write consumes the saved Boolean before direct and alias DOM mutations");
        }
    }
}

void testDOMURITransaction(mlir::MLIRContext & context) {
    testSavedAttributePredicates(context, SavedSelection::boolean);
    testSavedAttributePredicates(context, SavedSelection::nullableBoolean);
    testSavedAttributePredicates(context, SavedSelection::nullableNumber);
    testSelectedAttributeStrings(context);
    testSavedAttributeStrings(context);
    testProtectedNumberStrings(context);
    testNumberNumberInputs(context);
    testBooleanNumberInputs(context);
    testConditionalNumberCallee(context);
    testProtectedNumberArithmetic(context);
    testProtectedNumberReads(context);
    testNumberAttributeOrigins(context);
    testInvocationCallableReads(context);
    testInvocationCellReads(context);
    testObservedIteratorRecovery(context);
    testInertHelperCompletion(context);
    testHelperCatchSource(context);
    for (unsigned control = 0; control < 57; ++control) {
        std::string source = "function guarded(element) { try { throw element; } "
                             "catch (error) { return error === element; } }";
        if (control == 1) {
            replaceOnce(source, "error === element", "error.hasAttribute('data-closed')");
        }
        if (control == 2) {
            source.insert(source.find("throw element"), "element.hasAttribute('data-before'); ");
        }
        if (control == 3) { replaceOnce(source, "return error === element;", "throw error;"); }
        if (control == 4) { replaceOnce(source, "return error === element;", "return error;"); }
        if (control == 5) { source.insert(source.find("throw element"), "if (element) "); }
        if (control >= 7) {
            source = "function guarded(element) { "
                     "const flag = element.hasAttribute('flag'); let saved = false; "
                     "try { if (flag) { saved = true; throw element; } "
                     "else { throw element; } } catch (error) { "
                     "return error === element && saved; } }";
            if (control == 8) {
                replaceOnce(source, "error === element && saved",
                            "saved && error.hasAttribute('flag')");
            }
            if (control == 9) {
                source.insert(source.find("saved = true"), "element.hasAttribute('inside'); ");
            }
            if (control == 10) {
                replaceOnce(source, "return error === element && saved;", "throw error;");
            }
            if (control == 11) {
                replaceOnce(source, "return error === element && saved;", "return error;");
            }
        }
        if (control >= 12) {
            source = "function guarded(element) { "
                     "const flag = element.hasAttribute('flag'); let saved = false; "
                     "try { if (flag) { saved = true; throw element; } "
                     "else { saved = true; } } catch (error) { "
                     "return error === element && saved; } return !saved; }";
            if (control == 13) { replaceOnce(source, "if (flag)", "if (!flag)"); }
            if (control == 14) {
                replaceOnce(source, "throw element;", "if (flag) { throw element; } return false;");
            }
            if (control == 15) {
                source.insert(source.find("else { saved") + 7, "element.hasAttribute('inside'); ");
            }
            if (control == 16) {
                replaceOnce(source, "return error === element && saved;", "throw error;");
            }
            if (control == 17) {
                replaceOnce(source, "return error === element && saved;", "return error;");
            }
            if (control == 18) { replaceOnce(source, "return !saved;", "return element;"); }
        }
        if (control >= 20) {
            source = "function guarded(element) { "
                     "const flag = element.hasAttribute('flag'); "
                     "const inner = element.hasAttribute('inner'); let saved = false; "
                     "try { if (flag) { saved = true; if (inner) { throw element; } } "
                     "else { saved = true; } } catch (error) { "
                     "return error === element && saved; } return !saved; }";
            if (control == 21) { replaceOnce(source, "if (inner)", "if (!inner)"); }
            if (control == 22) {
                replaceOnce(source, "error === element && saved",
                            "saved && error.hasAttribute('flag')");
            }
            if (control == 23) {
                source.insert(source.find("else { saved") + 7, "element.hasAttribute('inside'); ");
            }
            if (control == 24) {
                replaceOnce(source, "return error === element && saved;", "return error;");
            }
            if (control == 25) {
                replaceOnce(source, "return error === element && saved;", "throw error;");
            }
        }
        if (control >= 28) {
            source = "function guarded(element) { let saved = false; try { "
                     "saved = element.hasAttribute('flag'); if (saved) throw element; "
                     "} catch (error) { return error === element && saved; } return saved; }";
            if (control == 29) {
                replaceOnce(source, "element.hasAttribute('flag')",
                            "({hasAttribute() { throw false; }}).hasAttribute('flag')");
            }
            if (control == 30) { replaceOnce(source, "'flag'", "42"); }
            if (control == 31) {
                replaceOnce(source, "element.hasAttribute('flag')", "decodeURIComponent('%')");
            }
        }
        if (control >= 33) {
            source = "function guarded(element) { let saved = false; try { "
                     "saved = element.hasAttribute('flag'); return saved; "
                     "} catch (error) { return error === element && saved; } }";
            if (control == 34) {
                replaceOnce(source, "return saved;",
                            "return saved && element.hasAttribute('second');");
            }
            if (control == 35) {
                replaceOnce(source, "saved = element", "if (element) saved = element");
            }
            if (control == 36) { replaceOnce(source, "'flag'", "42"); }
            if (control == 37) {
                replaceOnce(source, "element.hasAttribute('flag')", "decodeURIComponent('%')");
            }
        }
        if (control >= 40) {
            source = "function guarded(element) { let saved = false; try { "
                     "element.setAttribute('data-written', 'yes'); "
                     "saved = element.hasAttribute('data-written'); throw element; "
                     "} catch (error) { return error === element && saved; } }";
            if (control == 41) { replaceOnce(source, "throw element;", "return saved;"); }
            if (control == 42) {
                replaceOnce(source, "element.setAttribute",
                            "if (element.hasAttribute('flag')) element.setAttribute");
            }
            if (control == 43) { replaceOnce(source, "'data-written'", "'bad name'"); }
            if (control == 44) { replaceOnce(source, "'yes'", "42"); }
            if (control == 45) { replaceOnce(source, "'yes'", "element.getAttribute('value')"); }
            if (control == 46) {
                replaceOnce(source, "element.setAttribute",
                            "({setAttribute() { throw false; }}).setAttribute");
            }
            if (control == 49) {
                source.insert(source.find("element.setAttribute"),
                              "if (element.hasAttribute('flag')) element = false; ");
            }
        }
        if (control >= 50) {
            source = "function guarded(element) { "
                     "const saved = element.hasAttribute('data-closed') ? 7.5 : -0; "
                     "try { element.setAttribute('data-closed', 42); "
                     "element.setAttribute('data-written', saved); throw element; "
                     "} catch (error) { return error === element && saved === 7.5; } }";
            if (control == 53) {
                source.insert(source.find("try {"), "if (element) return false; ");
            }
            if (control == 54) { replaceOnce(source, " : -0", " : element"); }
            if (control == 55) { source.insert(source.find("try {"), "element.unknown(); "); }
            if (control == 56) {
                source.insert(source.find("try {"), "if (saved) element = false; ");
            }
        }
        auto candidate = import(context, source, true);
        if (!candidate) { continue; }
        const auto original = printed(*candidate);
        ctnative::HostContract request;
        request.provider = ctnative::HostContract::Provider::ctbrowserDOM;
        request.entry = guarded(*candidate).getSymName().str();
        request.elementParameters = {0};
        if (control == 32 || control == 38 || control == 47) { request.elementParameters.clear(); }
        request.moduleSha256 = ctnative::hostContractFingerprint(*candidate);
        const auto fingerprint = request.moduleSha256;
        auto error =
            ctnative::prepareDOMEntry(*candidate, request,
                                      control == 6 || control == 19 || control == 26 ||
                                              control == 39 || control == 48 || control == 51
                                          ? 0
                                      : control == 27 || control == 52 ? 500
                                                                       : 100000);
        if (control >= 3 && control != 5 && control != 7 && control != 8 && control != 9 &&
            !(control >= 12 && control <= 15) && !(control >= 20 && control <= 23) &&
            control != 28 && !(control >= 33 && control <= 35) &&
            !(control >= 40 && control <= 42) && control != 44 && control != 45 && control != 50) {
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
                replaceOnce(source, "H.second(element, 'x')", "H.second(element, 'X')");
            }
            if (control == 2) {
                replaceOnce(source, "H.second(element, 'x')",
                            "H.second(element, element.getAttribute('key'))");
            }
            if (control == 3) { source.insert(source.find("return saved"), "element.unknown(); "); }
            if (control >= 6) {
                replaceOnce(source, "H.first(element, 'x')", "H.first(element, 'ÉAZ')");
            }
            if (control == 7) { replaceOnce(source, "t.toLowerCase()", "t.toUpperCase()"); }
            if (control == 8) { replaceOnce(source, "t.toLowerCase()", "unknown(t)"); }
            if (control == 9) { replaceOnce(source, "t.toLowerCase()", "t.length"); }
            if (control == 10) { replaceOnce(source, "t.toLowerCase()", "arguments[0]"); }
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
                check(printed(*candidate) == original && sameContract(request, before),
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
            if (control == 1) { replaceOnce(source, "t.toLowerCase()", "t.toUpperCase()"); }
            if (control == 2) {
                source.insert(source.find("return result;"), "F(element.getAttribute('key')); ");
            }
            if (control == 3) {
                source.insert(source.find("var result"), "String.prototype.toLowerCase = F; ");
            }
            if (control == 6) { replaceOnce(source, "/[A-Z]/g", "/[A-Z]/i"); }
            if (control == 7) {
                replaceOnce(source, "t => `-${t.toLowerCase()}`",
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
