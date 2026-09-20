#pragma once

#include "HostContractFixtures.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Verifier.h"

namespace ctcompile::test::host_contract {

inline void checkDOMSnapshotLengths(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    const std::string source = R"MLIR(
module {
  ctjs.func @count$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %empty = ctjs.create_array[]
    %concatName = ctjs.constant #ctjs.string<"concat">
    %concat = ctjs.get_property %empty[%concatName]
    %arguments = ctjs.create_array[]
    %queryName = ctjs.constant #ctjs.string<"querySelectorAll">
    %query = ctjs.get_property %element[%queryName]
    %selector = ctjs.constant #ctjs.string<"button">
    %nodes = ctjs.call %query(%element, %selector)
    %iterable = ctjs.iterable of %nodes
    %lengthName = ctjs.constant #ctjs.string<"length">
    %length = ctjs.get_property %iterable[%lengthName]
    %zero = ctjs.constant #ctjs.number<0>
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %loop = scf.while (%index = %zero) : (!ctjs.value) -> !ctjs.value {
      %less = ctjs.compare lt %index, %length
      %test = ctjs.truthy %less
      scf.condition(%test) %index : !ctjs.value
    } do {
    ^bb0(%index: !ctjs.value):
      %node = ctjs.get_property %iterable[%index]
      ctjs.append %node to %arguments
      %next = ctjs.binary_static add %index, %one
      scf.yield %next : !ctjs.value
    }
    %result = ctjs.call_spread %concat(%empty, %arguments)
    %answer = ctjs.get_property %result[%lengthName]
    ctjs.return %answer
  }
}
)MLIR";
    HostContract contract;
    contract.entry = "count$0";
    contract.elementParameters = {0};
    contract.initialIntrinsics = {"Array", "Element"};
    const auto query = [&](const std::string & text, bool expected) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(input), "independent snapshot length fixture parses");
        if (!input) { return; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*input);
        auto failure = normalizeDOMSnapshotLengths(*input, request, 10000);
        const bool accepted = !failure;
        const auto reason = llvm::toString(std::move(failure));
        check(accepted == expected, "snapshot length requires the complete source proof");
        if (accepted != expected) {
            std::fprintf(stderr, "%s\n%s\n", text.c_str(), reason.c_str());
        }
        if (!accepted) {
            check(hostContractFingerprint(*input) == request.moduleSha256,
                  "refused snapshot normalization leaves all source operations unchanged");
            return;
        }
        check(mlir::succeeded(mlir::verify(*input)), "normalized snapshot IR remains valid");
        request.moduleSha256 = hostContractFingerprint(*input);
        DOMEntryAnalysis proof(*input, request);
        check(proof.proved(), "normalized count retains complete typed DOM evidence");
        unsigned calls = 0, bounds = 0, removed = 0;
        input->walk([&](mlir::Operation * operation) {
            removed += llvm::isa<ctjs::CallSpreadOp, ctjs::IterableOp, ctjs::AppendOp,
                                 ctjs::CreateArrayOp, mlir::scf::WhileOp>(operation);
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation)) {
                const auto * edge = proof.call(call);
                calls += edge && edge->returnsElementVector();
            }
            auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation);
            if (!branch) { return; }
            auto truth = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
            auto compare = truth.getValue().getDefiningOp<ctjs::CompareOp>();
            auto cap = compare.getRhs().getDefiningOp<ctjs::ConstantOp>();
            const auto limit = llvm::cast<ctjs::NumberAttr>(cap.getValue()).getDouble();
            auto yes = llvm::cast<mlir::scf::YieldOp>(branch.getThenRegion().front().back());
            auto no = llvm::cast<mlir::scf::YieldOp>(branch.getElseRegion().front().back());
            check(compare.getKind() == ctjs::CompareKind::Lt && limit == double(1U << 24) &&
                      yes.getOperand(0) == compare.getLhs() && no.getOperand(0) == compare.getRhs(),
                  "count retains the VM cap without truncating the original NodeList");
            ++bounds;
        });
        check(calls == 1 && bounds == 1 && removed == 0,
              "one real selector remains and only the confined spread machinery disappears");
    };
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        contract.provider = provider;
        query(source, true);
        for (const auto & identities : {std::vector<std::string>{}, {"Array"}, {"Element"}}) {
            contract.initialIntrinsics = identities;
            query(source, false);
        }
        contract.initialIntrinsics = {"Array", "Element"};
    }
    for (const auto & invalid : {
             replaced(source, "ctjs.create_array[]", "ctjs.create_array[%element]"),
             replaced(source, "#ctjs.string<\"concat\">", "#ctjs.string<\"slice\">"),
             replaced(source, "%concat(%empty, %arguments)", "%concat(%arguments, %arguments)"),
             replaced(source, "%index = %zero", "%index = %one"),
             replaced(source, "compare lt %index", "compare le %index"),
             replaced(source, "add %index, %one", "add %index, %zero"),
             replaced(source, "scf.yield %next", "scf.yield %index"),
             replaced(source, "%iterable[%index]", "%iterable[%zero]"),
             replaced(source, "ctjs.append %node", "ctjs.append %element"),
             replaced(source, "ctjs.append %node to %arguments",
                      "ctjs.append %node to %arguments\n      ctjs.store_global \"saved\", %node"),
             replaced(source,
                      "    %result =", "    ctjs.append %element to %arguments\n    %result ="),
             replaced(source, "    %result =", "    ctjs.append %element to %empty\n    %result ="),
             replaced(source, "%result[%lengthName]", "%result[%zero]"),
             replaced(source, "ctjs.return %answer", "ctjs.return %result"),
             replaced(source,
                      "    %result =", "    ctjs.store_global \"saved\", %length\n    %result ="),
             replaced(source, "#ctjs.string<\"querySelectorAll\">",
                      "#ctjs.string<\"querySelector\">"),
             replaced(source, "ctjs.iterable of %nodes", "ctjs.iterable of %selector"),
             replaced(source,
                      "    %nodes =", "    ctjs.store_global \"Array\", %element\n    %nodes ="),
         }) {
        query(invalid, false);
    }
    auto original = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    contract.moduleSha256 = hostContractFingerprint(*original);
    unsigned complete = 0;
    for (unsigned budget = 0; budget < 10000; ++budget) {
        mlir::OwningOpRef<mlir::ModuleOp> input(original->clone());
        auto failure = normalizeDOMSnapshotLengths(*input, contract, budget);
        if (!failure) {
            complete = budget;
            break;
        }
        llvm::consumeError(std::move(failure));
        check(hostContractFingerprint(*input) == contract.moduleSha256,
              "every incomplete snapshot budget leaves the entire source untouched");
    }
    check(complete > 0, "snapshot normalization completes under a finite reproducible budget");
    auto stale = contract;
    stale.moduleSha256.assign(64, '0');
    auto failure = normalizeDOMSnapshotLengths(*original, stale, 10000);
    check(static_cast<bool>(failure), "stale fingerprint refuses snapshot normalization");
    llvm::consumeError(std::move(failure));
    check(hostContractFingerprint(*original) == contract.moduleSha256,
          "stale request does not erase or replace source operations");
}

} // namespace ctcompile::test::host_contract
