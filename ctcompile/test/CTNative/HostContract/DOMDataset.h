#pragma once

#include "HostContractFixtures.h"

namespace ctcompile::test::host_contract {

inline void checkDOMDataset(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    constexpr llvm::StringLiteral source = R"MLIR(
module {
  ctjs.func @keys$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %Object = ctjs.load_global "Object"
    %keysName = ctjs.constant #ctjs.string<"keys">
    %keys = ctjs.get_property %Object[%keysName]
    %datasetName = ctjs.constant #ctjs.string<"dataset">
    %dataset = ctjs.get_property %element[%datasetName]
    %result = ctjs.call %keys(%Object, %dataset)
    ctjs.return %result
  }
}
)MLIR";
    auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    check(static_cast<bool>(module), "dataset snapshot fixture parses");
    if (!module) { return; }
    HostContract contract;
    contract.provider = HostContract::Provider::ctbrowserDOM;
    contract.entry = "keys$0";
    contract.elementParameters = {0};
    contract.datasetParameters = {0};
    contract.initialIntrinsics = {"Object"};
    contract.moduleSha256 = hostContractFingerprint(*module);
    ctjs::LoadGlobalOp intrinsic;
    ctjs::GetPropertyOp dataset, method;
    ctjs::CallOp call;
    module->walk([&](ctjs::LoadGlobalOp op) { intrinsic = op; });
    module->walk([&](ctjs::GetPropertyOp op) {
        if (ctjs::constantKey(op.getKey()) == "keys") {
            method = op;
        } else {
            dataset = op;
        }
    });
    module->walk([&](ctjs::CallOp op) { call = op; });
    const auto noEvidence = [&](const DOMEntryAnalysis & proof) {
        return !proof.proved() && !proof.entry() && proof.parameters().empty() &&
               !proof.isInitialIntrinsic(intrinsic) && !proof.isDataset(dataset.getResult()) &&
               !proof.isDatasetElement(dataset.getObject()) && !proof.method(method) &&
               !proof.call(call);
    };
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        contract.provider = provider;
        DOMEntryAnalysis proof(*module, contract);
        const auto * edge = proof.call(call);
        check(proof.proved() && proof.isInitialIntrinsic(intrinsic) &&
                  proof.isDataset(dataset.getResult()) &&
                  proof.isDatasetElement(dataset.getObject()) && edge &&
                  edge->kind == HostDOMMethod::datasetKeys && edge->returnsStringVector() &&
                  !edge->returnsBoolean() && edge->element == dataset.getObject(),
              "dataset snapshot requires exact Object identity and the contracted element");
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "dataset proof preserves the entire source");
        if (proof.proved()) {
            check(DOMEntryAnalysis(*module, contract, proof.steps()).proved(),
                  "dataset proof reproduces its exact budget");
            for (unsigned budget = 0; budget < proof.steps(); ++budget) {
                DOMEntryAnalysis limited(*module, contract, budget);
                check(noEvidence(limited) && limited.exhausted(),
                      "every insufficient dataset budget withholds all live evidence");
            }
        }
    }
    for (auto indices :
         {std::vector<unsigned>{}, std::vector<unsigned>{1}, std::vector<unsigned>{0, 0}}) {
        auto invalid = contract;
        invalid.datasetParameters = indices;
        check(noEvidence(DOMEntryAnalysis(*module, invalid)),
              "typed dataset premises cannot bypass namespace subset validation");
    }
    contract.initialIntrinsics.clear();
    check(noEvidence(DOMEntryAnalysis(*module, contract)),
          "an Object global name cannot supply intrinsic authority");
    contract.initialIntrinsics = {"Object"};
    mlir::Builder builder(&context);
    method.getKey().getDefiningOp<ctjs::ConstantOp>().setValueAttr(
        ctjs::StringAttr::get(&context, "values"));
    check(noEvidence(DOMEntryAnalysis(*module, contract)),
          "changed dataset source invalidates the fingerprint");
    contract.moduleSha256 = hostContractFingerprint(*module);
    method->setAttr("ctnative.host_intrinsic", builder.getStringAttr("Object.keys"));
    check(noEvidence(DOMEntryAnalysis(*module, contract)),
          "forged Object.keys reports cannot replace the complete proof");
}

} // namespace ctcompile::test::host_contract
