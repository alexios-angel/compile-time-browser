#include "DOMURI.hpp"
#include "../../../../lib/CTNative/HostContract/Analysis.h"
#include "../../../../lib/CTNative/HostContract/Preparation.h"
#include "ClassTransactions.hpp"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "mlir/IR/Dominance.h"

#include "check.hpp"

namespace ctcompile::test::exception_recovery {

using ctcompile::ctnative::host_detail::projectInvocationContinuation;

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
        text.replace(text.find(from.str()), from.size(), to.str());
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
            text.replace(text.find("'data-next'"), 11, "'bad name'");
        } else if (control == 2) {
            text.insert(text.find("    const done"), "    unknown();\n");
        } else if (control == 3) {
            text.replace(text.find("result.done && result.value === anchor"), 38,
                         "anchor.unproved");
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
        if (method) { source.replace(source.find("get return()"), 12, "return()"); }
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
        contract.initialIntrinsics = {"Object", "Symbol", "__ctbrowser_for_of_open",
                                      "__ctbrowser_iter_next", "__ctbrowser_iter_close"};
        contract.moduleSha256 = ctcompile::ctnative::hostContractFingerprint(*module);
        const auto originalModule = printed(*module);
        auto close = ctcompile::ctnative::normalizeDOMIteratorClose(*module, contract);
        check(close && !*close && printed(*module) == originalModule &&
                  contract.moduleSha256 == ctcompile::ctnative::hostContractFingerprint(*module),
              "original observing iterator defers suppression without changing any source state");
        if (!close) { llvm::consumeError(close.takeError()); }
        const auto boundary =
            structured ? "DOM iterator close requires completion-selected record identity"
            : method   ? "DOM Symbol.iterator must be a closed identity method"
                       : "DOM iterator getter requires a terminal throw";
        for (unsigned budget : {0u, 100000u}) {
            mlir::OwningOpRef<mlir::ModuleOp> candidate(module->clone());
            auto proof = contract;
            auto error = ctcompile::ctnative::prepareDOMEntry(*candidate, proof, budget);
            const auto reason = error ? llvm::toString(std::move(error)) : std::string{};
            check(reason.find(budget ? boundary : "budget") != std::string::npos &&
                      printed(*candidate) == originalModule &&
                      proof.moduleSha256 == contract.moduleSha256,
                  "observing iterator reaches protocol proof while late or budget refusal rolls "
                  "back");
            if (budget && reason.find(boundary) == std::string::npos) {
                llvm::errs() << reason << '\n';
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
        for (unsigned control = 0; control < (structured ? 17u : 7u); ++control) {
            mlir::OwningOpRef<mlir::ModuleOp> candidate(module->clone());
            auto entry = candidate->lookupSymbol<ctjs::FuncOp>(contract.entry);
            ctjs::InvokeOp invocation, opening;
            ctjs::CallOp openCall;
            entry.walk([&](ctjs::InvokeOp found) {
                auto call = llvm::cast<ctjs::CallOp>(found.getBody().front().front());
                auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
                if (!invocation && load && load.getName() == "__ctbrowser_iter_close") {
                    invocation = found;
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
                auto key = ctjs::ConstantOp::create(at, call.getLoc(),
                                                    ctjs::StringAttr::get(&context, "@@iterator"));
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
            const auto remember = [&](mlir::OpOperand & use, mlir::Value value) {
                if (value == opening.getNormalBody().front().getArgument(0)) {
                    value = openCall.getResult();
                }
                if (value == recordAlias && control != 12 && control != 13) {
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
                control == 0 || control == 11 || control == 15 ? boundary
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
            check(reason.find(expected) != std::string::npos &&
                      (!(control > 0 && control < 7) || printed(*candidate) == snapshot),
                  "protocol proof retains calls and refuses unknown identity, effects and state");
            if (reason.find(expected) == std::string::npos) { llvm::errs() << reason << '\n'; }
            if ((control == 0 || control >= 11) && structured) {
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

void testDOMURITransaction(mlir::MLIRContext & context) {
    testObservedIteratorRecovery(context);
    testInertHelperCompletion(context);
    testHelperCatchSource(context);
    for (unsigned control = 0; control < 50; ++control) {
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
        if (control >= 40) {
            source = "function guarded(element) { let saved = false; try { "
                     "element.setAttribute('data-written', 'yes'); "
                     "saved = element.hasAttribute('data-written'); throw element; "
                     "} catch (error) { return error === element && saved; } }";
            if (control == 41) {
                source.replace(source.find("throw element;"), 14, "return saved;");
            }
            if (control == 42) {
                source.replace(source.find("element.setAttribute"), 20,
                               "if (element.hasAttribute('flag')) element.setAttribute");
            }
            if (control == 43) { source.replace(source.find("'data-written'"), 14, "'bad name'"); }
            if (control == 44) { source.replace(source.find("'yes'"), 5, "42"); }
            if (control == 45) {
                source.replace(source.find("'yes'"), 5, "element.getAttribute('value')");
            }
            if (control == 46) {
                source.replace(source.find("element.setAttribute"), 20,
                               "({setAttribute() { throw false; }}).setAttribute");
            }
            if (control == 49) {
                source.insert(source.find("element.setAttribute"),
                              "if (element.hasAttribute('flag')) element = false; ");
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
        auto error = ctnative::prepareDOMEntry(
            *candidate, request,
            control == 6 || control == 19 || control == 26 || control == 39 || control == 48 ? 0
            : control == 27 ? 500
                            : 100000);
        if (control >= 3 && control != 5 && control != 7 && control != 8 && control != 9 &&
            !(control >= 12 && control <= 15) && !(control >= 20 && control <= 23) &&
            control != 28 && !(control >= 33 && control <= 35) &&
            !(control >= 40 && control <= 42)) {
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
