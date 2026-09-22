#pragma once

#include "HostContractFixtures.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Verifier.h"

namespace ctcompile::test::host_contract {

inline void checkDOMIteratorClose(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    const std::string source = R"MLIR(
module {
  ctjs.func @entry$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %frame = ctjs.frame_enter 4
    %record = ctjs.create_object
    %saved = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.store_global "before", %saved
    ctjs.push_handler ^body catch ^caught
  ^body:
    %close = ctjs.load_global "__ctbrowser_iter_close"
    ctjs.check ^args caught ^caught
  ^args:
    %abrupt = ctjs.constant #ctjs.boolean<true>
    %undefined = ctjs.constant #ctjs.undefined
    ctjs.check ^call caught ^caught
  ^call:
    %closed = ctjs.call %close(%undefined, %record, %abrupt)
    ctjs.check ^success caught ^caught
  ^success:
    ctjs.pop_handler
    cf.br ^joined
  ^caught:
    %pad, %error = ctjs.catch_land
    cf.br ^joined
  ^joined:
    ctjs.throw %saved
  }
})MLIR";
    const auto contractForClose = [&](mlir::ModuleOp input) {
        HostContract contract;
        contract.provider = HostContract::Provider::ctbrowserDOM;
        contract.entry = "entry$0";
        contract.initialIntrinsics = {"__ctbrowser_iter_close"};
        contract.moduleSha256 = hostContractFingerprint(input);
        return contract;
    };
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
        check(static_cast<bool>(input), "independent iterator close suppression parses");
        if (!input) { return; }
        auto contract = contractForClose(*input);
        contract.provider = provider;
        auto normalized = normalizeDOMIteratorClose(*input, contract);
        check(normalized && *normalized && mlir::succeeded(mlir::verify(*input)),
              "both DOM providers preserve the close suppression as a verified invocation");
        if (!normalized) {
            std::fprintf(stderr, "%s\n", llvm::toString(normalized.takeError()).c_str());
            continue;
        }
        unsigned calls = 0, invokes = 0, throws = 0, writes = 0, handlers = 0;
        input->walk([&](ctjs::CallOp call) {
            ++calls;
            auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
            auto flag = call.getArgs()[1].getDefiningOp<ctjs::ConstantOp>();
            check(load && load.getName() == "__ctbrowser_iter_close" && flag &&
                      llvm::cast<ctjs::BooleanAttr>(flag.getValue()).getValue() &&
                      call.getArgs()[0].getDefiningOp<ctjs::CreateObjectOp>(),
                  "the original abrupt flag, record and close callee survive");
        });
        input->walk([&](ctjs::InvokeOp invoke) {
            ++invokes;
            check(invoke.getNumResults() == 0 &&
                      invoke.getNormalBody().front().getArgument(0).use_empty() &&
                      invoke.getUnwindBody().front().getArgument(0).use_empty() &&
                      llvm::hasSingleElement(invoke.getNormalBody().front()) &&
                      llvm::hasSingleElement(invoke.getUnwindBody().front()),
                  "normal and thrown close outcomes both resume without observing the result");
        });
        input->walk([&](ctjs::ThrowOp thrown) {
            ++throws;
            auto saved = thrown.getValue().getDefiningOp<ctjs::ConstantOp>();
            check(saved && llvm::cast<ctjs::NumberAttr>(saved.getValue()).getDouble() == 1,
                  "the saved body exception survives both close outcomes");
        });
        input->walk([&](ctjs::StoreGlobalOp) { ++writes; });
        input->walk([&](ctjs::PushHandlerOp) { ++handlers; });
        check(calls == 1 && invokes == 1 && throws == 1 && writes == 1 && handlers == 0,
              "normalization preserves each source effect and represents exactly one handler");
        contract.moduleSha256 = hostContractFingerprint(*input);
        auto repeated = normalizeDOMIteratorClose(*input, contract);
        check(repeated && !*repeated && hostContractFingerprint(*input) == contract.moduleSha256,
              "an already represented close is an unchanged no-op");
        if (!repeated) { llvm::consumeError(repeated.takeError()); }
    }
    const auto refuses = [&](const std::string & text, unsigned budget = 100000) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(input), "hostile close suppression is valid source IR");
        if (!input) { return; }
        auto contract = contractForClose(*input);
        auto normalized = normalizeDOMIteratorClose(*input, contract, budget);
        check(!normalized && hostContractFingerprint(*input) == contract.moduleSha256,
              "unproved close suppression refuses without changing the source");
        if (!normalized) { llvm::consumeError(normalized.takeError()); }
    };
    refuses(replaced(source, "#ctjs.boolean<true>", "#ctjs.boolean<false>"));
    refuses(replaced(source, "ctjs.store_global \"before\"",
                     "ctjs.store_global \"__ctbrowser_iter_close\""));
    refuses(replaced(source, "%close(%undefined,", "%close(%record,"));
    refuses(replaced(source, "    ctjs.pop_handler\n", ""));
    refuses(replaced(source, "    ctjs.pop_handler\n",
                     "    ctjs.pop_handler\n    ctjs.store_global \"after\", %closed\n"));
    refuses(replaced(source, "    %pad, %error = ctjs.catch_land\n",
                     "    %pad, %error = ctjs.catch_land\n"
                     "    ctjs.store_global \"observed\", %error\n"));
    refuses(replaced(source, "    ctjs.check ^args caught ^caught\n",
                     "    ctjs.store_global \"protected\", %saved\n"
                     "    ctjs.check ^args caught ^caught\n"));
    refuses(replaced(source, "    ctjs.check ^success caught ^caught\n",
                     "    %again = ctjs.call %close(%undefined, %record, %abrupt)\n"
                     "    ctjs.check ^success caught ^caught\n"));
    for (llvm::StringRef target : {"caught", "joined"}) {
        refuses(replaced(source, "    ctjs.push_handler ^body catch ^caught\n",
                         ("    %yes = arith.constant true\n"
                          "    cf.cond_br %yes, ^install, ^" +
                          target + "\n  ^install:\n    ctjs.push_handler ^body catch ^caught\n")
                             .str()));
    }
    for (unsigned budget : {0u, 1u, 128u}) { refuses(source, budget); }
    for (unsigned boundary = 0; boundary != 4; ++boundary) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
        auto contract = contractForClose(*input);
        if (boundary == 0) { contract.initialIntrinsics.clear(); }
        if (boundary == 1) { contract.moduleSha256 = std::string(64, '0'); }
        if (boundary == 2) { contract.provider = HostContract::Provider::ctbrowserIntrinsics; }
        if (boundary == 3) {
            input->getOperation()->setAttr("ctjs.skipped", mlir::UnitAttr::get(&context));
        }
        const auto before = hostContractFingerprint(*input);
        auto normalized = normalizeDOMIteratorClose(*input, contract);
        check(!normalized && hostContractFingerprint(*input) == before,
              "close structure never grants missing provider or complete-source authority");
        if (!normalized) { llvm::consumeError(normalized.takeError()); }
    }
    auto input = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    auto contract = contractForClose(*input);
    auto normalized = normalizeDOMIteratorClose(*input, contract);
    if (!normalized) { llvm::consumeError(normalized.takeError()); }
    contract.moduleSha256 = hostContractFingerprint(*input);
    check(!DOMEntryAnalysis(*input, contract).proved(),
          "structural close success does not bypass complete typed DOM admission");

    const auto mixed =
        replaced(replaced(source, "    ctjs.push_handler ^body catch ^caught\n",
                          "    %condition = ctjs.truthy %this\n"
                          "    cf.cond_br %condition, ^install, ^normal\n"
                          "  ^install:\n    ctjs.push_handler ^body catch ^caught\n"),
                 "    ctjs.throw %saved\n",
                 "    ctjs.throw %saved\n"
                 "  ^normal:\n"
                 "    %returned = ctjs.constant #ctjs.number<4611686018427387904>\n"
                 "    ctjs.store_global \"normal\", %returned\n"
                 "    ctjs.frame_exit %frame\n"
                 "    ctjs.return %returned\n");
    for (bool reversed : {false, true}) {
        auto text = reversed ? replaced(mixed, "^install, ^normal", "^normal, ^install") : mixed;
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            auto candidate = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
            check(static_cast<bool>(candidate), "mixed throw and return source parses");
            if (!candidate) { continue; }
            auto proof = contractForClose(*candidate);
            proof.provider = provider;
            auto result = normalizeDOMIteratorClose(*candidate, proof);
            check(result && *result && mlir::succeeded(mlir::verify(*candidate)),
                  "mixed source exits retain verified structured correspondence");
            if (!result) {
                std::fprintf(stderr, "%s\n", llvm::toString(result.takeError()).c_str());
                continue;
            }
            auto entry = candidate->lookupSymbol<ctjs::FuncOp>(proof.entry);
            check(entry.getBody().hasOneBlock(), "mixed completion has one root entry");
            unsigned throws = 0, returns = 0, invokes = 0, normalWrites = 0;
            mlir::scf::IfOp terminal;
            entry.walk([&](ctjs::ThrowOp thrown) {
                ++throws;
                auto region = llvm::dyn_cast<mlir::scf::ExecuteRegionOp>(thrown->getParentOp());
                terminal = region ? llvm::dyn_cast<mlir::scf::IfOp>(region->getParentOp())
                                  : mlir::scf::IfOp{};
                auto saved = thrown.getValue().getDefiningOp<ctjs::ConstantOp>();
                check(region && region.getNumResults() == 0 && region.getNoInline() &&
                          llvm::hasSingleElement(region.getRegion().front()) && terminal && saved &&
                          llvm::cast<ctjs::NumberAttr>(saved.getValue()).getDouble() == 1,
                      "the original saved throw terminates its own standard SCF region");
                auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(
                    region ? region->getBlock()->getTerminator() : nullptr);
                check(yield && yield.getNumOperands() == 1 &&
                          yield.getOperand(0).getDefiningOp<mlir::ub::PoisonOp>(),
                      "the throwing arm has no fabricated return value");
            });
            entry.walk([&](ctjs::ReturnOp returned) {
                ++returns;
                check(returned->getParentOp() == entry &&
                          returned.getValue().getDefiningOp<mlir::scf::IfOp>(),
                      "the single real return receives the terminal dispatch result");
            });
            entry.walk([&](ctjs::InvokeOp invoke) {
                ++invokes;
                check(invoke->getParentOp() == terminal && invoke.getNumResults() == 0 &&
                          invoke.getUnwindBody().front().getArgument(0).use_empty(),
                      "the close stays suppressed inside the throwing completion arm");
            });
            entry.walk([&](ctjs::StoreGlobalOp store) {
                if (store.getName() != "normal") { return; }
                ++normalWrites;
                auto branch = llvm::dyn_cast<mlir::scf::IfOp>(store->getParentOp());
                auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(store->getBlock()->getTerminator());
                auto value = store.getValue().getDefiningOp<ctjs::ConstantOp>();
                check(branch == terminal && yield && yield.getOperand(0) == store.getValue() &&
                          value && llvm::cast<ctjs::NumberAttr>(value.getValue()).getDouble() == 2,
                      "normal effects and the actual return value remain in their source arm");
            });
            check(throws == 1 && returns == 1 && invokes == 1 && normalWrites == 1,
                  "structuring neither duplicates nor drops source effects or exits");
            proof.moduleSha256 = hostContractFingerprint(*candidate);
            check(!DOMEntryAnalysis(*candidate, proof).proved(),
                  "structured abrupt completion still needs full native payload and host proof");
        }
    }
    for (unsigned budget : {0u, 128u, 512u}) { refuses(mixed, budget); }
}

} // namespace ctcompile::test::host_contract
