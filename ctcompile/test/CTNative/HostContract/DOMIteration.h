#pragma once

#include "HostContractFixtures.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"

namespace ctcompile::test::host_contract {

inline void checkDOMIteration(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    context.getOrLoadDialect<mlir::arith::ArithDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    context.getOrLoadDialect<mlir::ub::UBDialect>();
    const std::string prefix = R"MLIR(
module {
  ctjs.func @iterate$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %Object = ctjs.load_global "Object"
    %keysName = ctjs.constant #ctjs.string<"keys">
    %keysMethod = ctjs.get_property %Object[%keysName]
    %datasetName = ctjs.constant #ctjs.string<"dataset">
    %dataset = ctjs.get_property %element[%datasetName]
    %keys = ctjs.call %keysMethod(%Object, %dataset)
    %lengthName = ctjs.constant #ctjs.string<"length">
    %length = ctjs.get_property %keys[%lengthName]
    %zero = ctjs.constant #ctjs.number<0>
    %one = ctjs.constant #ctjs.number<4607182418800017408>
)MLIR";
    const std::string suffix = R"MLIR(
    ctjs.return %loop#2
  }
}
)MLIR";
    const std::string source = prefix + R"MLIR(
    %loop:3 = scf.while (%count = %zero, %index = %zero) : (!ctjs.value, !ctjs.value) -> (!ctjs.value, !ctjs.value, !ctjs.value) {
      %less = ctjs.compare lt %index, %length
      %test = ctjs.truthy %less
      %selected:4 = scf.if %test -> (i1, !ctjs.value, !ctjs.value, !ctjs.value) {
        %key = ctjs.get_property %keys[%index]
        %nextCount = ctjs.binary add %count, %one
        %nextIndex = ctjs.binary_static add %index, %one
        %yes = arith.constant true
        scf.yield %yes, %nextCount, %nextIndex, %count : i1, !ctjs.value, !ctjs.value, !ctjs.value
      } else {
        %no = arith.constant false
        scf.yield %no, %count, %index, %count : i1, !ctjs.value, !ctjs.value, !ctjs.value
      }
      scf.condition(%selected#0) %selected#1, %selected#2, %selected#3 : !ctjs.value, !ctjs.value, !ctjs.value
    } do {
    ^bb0(%count: !ctjs.value, %index: !ctjs.value, %answer: !ctjs.value):
      scf.yield %count, %index : !ctjs.value, !ctjs.value
    }
)MLIR" + suffix;
    auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    check(static_cast<bool>(module), "independent prepared dataset loop fixture parses");
    if (!module) { return; }
    HostContract contract;
    contract.entry = "iterate$0";
    contract.elementParameters = {0};
    contract.datasetParameters = {0};
    contract.initialIntrinsics = {"Object", "Array"};
    contract.moduleSha256 = hostContractFingerprint(*module);
    const auto noEvidence = [](mlir::ModuleOp input, const DOMEntryAnalysis & proof) {
        bool empty = !proof.proved() && !proof.entry() && !proof.wrapper() &&
                     proof.parameters().empty() && proof.callbacks().empty() &&
                     proof.stringResults().empty() && proof.optionalStringJoins().empty() &&
                     proof.stringRefinements().empty();
        input.walk([&](ctjs::CallOp call) { empty &= !proof.call(call); });
        input.walk([&](ctjs::LoadGlobalOp load) { empty &= !proof.isInitialIntrinsic(load); });
        input.walk([&](ctjs::GetPropertyOp read) {
            empty &= !proof.method(read) && !proof.isDataset(read.getResult()) &&
                     !proof.isStringVectorLength(read) && !proof.isStringVectorIndex(read);
        });
        return empty;
    };
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        contract.provider = provider;
        DOMEntryAnalysis proof(*module, contract);
        check(proof.proved(), "scalar loop proves exact guarded snapshot extraction");
        if (!proof.proved()) {
            std::fprintf(stderr, "%s\n", proof.reason().str().c_str());
            continue;
        }
        unsigned indices = 0;
        module->walk([&](ctjs::GetPropertyOp read) { indices += proof.isStringVectorIndex(read); });
        check(indices == 1, "only the integral guarded vector read receives index evidence");
        check(DOMEntryAnalysis(*module, contract, proof.steps()).proved(),
              "loop proof reproduces its exact completion budget");
        for (unsigned budget = 0; budget < proof.steps(); ++budget) {
            DOMEntryAnalysis limited(*module, contract, budget);
            check(limited.exhausted() && noEvidence(*module, limited),
                  "every incomplete loop proof withholds all index and host evidence");
        }
    }
    for (const auto & invalid :
         {replaced(source, "%index = %zero", "%index = %one"),
          replaced(source, "ctjs.binary_static add %index, %one",
                   "ctjs.binary_static add %index, %zero"),
          replaced(source, "ctjs.compare lt %index, %length", "ctjs.compare le %index, %length"),
          replaced(source, "%key = ctjs.get_property %keys[%index]",
                   "%key = ctjs.get_property %keys[%count]"),
          replaced(source, "ctjs.compare lt %index, %length", "ctjs.compare lt %index, %one"),
          replaced(replaced(source, "        %key = ctjs.get_property %keys[%index]\n", ""),
                   "      %less =", "      %key = ctjs.get_property %keys[%index]\n      %less ="),
          replaced(
              replaced(source, "        %key = ctjs.get_property %keys[%index]\n", ""),
              "        %no =", "        %key = ctjs.get_property %keys[%index]\n        %no ="),
          replaced(source, "%length = ctjs.get_property %keys[%lengthName]",
                   "%other = ctjs.call %keysMethod(%Object, %dataset)\n"
                   "    %length = ctjs.get_property %other[%lengthName]"),
          replaced(source, "scf.yield %count, %index : !ctjs.value, !ctjs.value",
                   "scf.yield %index, %count : !ctjs.value, !ctjs.value")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "unsupported induction witness parses");
        if (!input) { continue; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*input);
        DOMEntryAnalysis refused(*input, request);
        check(noEvidence(*input, refused),
              "nonzero starts, wrong latches, wrong indices and inexact bounds withhold evidence");
    }

    const std::string completion = prefix + R"MLIR(
    %poison = ub.poison : !ctjs.value
    %i0 = arith.constant 0 : i32
    %i1 = arith.constant 1 : i32
    %loop:3 = scf.while (%count = %zero, %index = %zero, %unused = %poison) : (!ctjs.value, !ctjs.value, !ctjs.value) -> (!ctjs.value, !ctjs.value, !ctjs.value) {
      %less = ctjs.compare lt %index, %length
      %test = ctjs.truthy %less
      %selected:3 = scf.if %test -> (!ctjs.value, !ctjs.value, i32) {
        %key = ctjs.get_property %keys[%index]
        %nextCount = ctjs.binary add %count, %one
        %nextIndex = ctjs.binary_static add %index, %one
        scf.yield %nextCount, %nextIndex, %i1 : !ctjs.value, !ctjs.value, i32
      } else {
        scf.yield %poison, %poison, %i0 : !ctjs.value, !ctjs.value, i32
      }
      %selector = arith.index_castui %selected#2 : i32 to index
      %dispatched:3 = scf.index_switch %selector -> i32, !ctjs.value, !ctjs.value
      case 0 {
        scf.yield %i0, %selected#0, %selected#1 : i32, !ctjs.value, !ctjs.value
      }
      default {
        scf.yield %i1, %selected#0, %selected#1 : i32, !ctjs.value, !ctjs.value
      }
      %continued = arith.trunci %dispatched#0 : i32 to i1
      scf.condition(%continued) %dispatched#1, %dispatched#2, %count : !ctjs.value, !ctjs.value, !ctjs.value
    } do {
    ^bb0(%count: !ctjs.value, %index: !ctjs.value, %answer: !ctjs.value):
      scf.yield %count, %index, %answer : !ctjs.value, !ctjs.value, !ctjs.value
    }
)MLIR" + suffix;
    auto normalized = mlir::parseSourceString<mlir::ModuleOp>(completion, &context);
    check(static_cast<bool>(normalized), "source completion tuple fixture parses");
    if (!normalized) { return; }
    auto error = expandDOMHelpers(*normalized, contract.entry, 100000);
    check(!error, "completion normalizer preserves the loop condition and terminal tuple");
    if (error) {
        std::fprintf(stderr, "%s\n", llvm::toString(std::move(error)).c_str());
        return;
    }
    unsigned loops = 0, reads = 0, inactive = 0, dispatches = 0;
    normalized->walk([&](mlir::scf::WhileOp loop) {
        ++loops;
        check(loop.getInits().size() == 2 && loop.getNumResults() == 3,
              "unused entry slot is removed without losing the final count");
    });
    normalized->walk(
        [&](ctjs::GetPropertyOp read) { reads += llvm::isa<mlir::BlockArgument>(read.getKey()); });
    normalized->walk([&](mlir::ub::PoisonOp) { ++inactive; });
    normalized->walk([&](mlir::scf::IndexSwitchOp) { ++dispatches; });
    check(loops == 1 && reads == 1 && inactive == 0 && dispatches == 0,
          "normalization preserves source reads and removes only inactive completion machinery");
    contract.moduleSha256 = hostContractFingerprint(*normalized);
    check(DOMEntryAnalysis(*normalized, contract).proved(),
          "normalized original completion loop receives the same complete entry proof");
    for (unsigned budget : {0U, 64U}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(completion, &context);
        const auto fingerprint = hostContractFingerprint(*input);
        auto limited = expandDOMHelpers(*input, contract.entry, budget);
        const bool refused = static_cast<bool>(limited);
        const auto reason = refused ? llvm::toString(std::move(limited)) : std::string{};
        check(refused && reason.find("budget") != std::string::npos &&
                  fingerprint == hostContractFingerprint(*input),
              "completion budget cutoffs preserve the original source body");
    }
    for (const auto & invalid :
         {replaced(completion, "%count = %zero", "%count = %poison"),
          replaced(completion, "ctjs.return %loop#2", "ctjs.return %loop#0"),
          replaced(completion, "scf.yield %nextCount, %nextIndex, %i1",
                   "scf.yield %poison, %nextIndex, %i1"),
          replaced(completion, "scf.condition(%continued)", "scf.condition(%test)")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "observable inactive-slot witness parses");
        if (!input) { continue; }
        const auto fingerprint = hostContractFingerprint(*input);
        auto rejected = expandDOMHelpers(*input, contract.entry, 100000);
        const bool refused = static_cast<bool>(rejected);
        const auto reason = refused ? llvm::toString(std::move(rejected)) : std::string{};
        check(refused && reason.find("inactive value") != std::string::npos &&
                  fingerprint == hostContractFingerprint(*input),
              "live or uncorrelated poison refuses without replacing the source body");
    }
}

} // namespace ctcompile::test::host_contract
