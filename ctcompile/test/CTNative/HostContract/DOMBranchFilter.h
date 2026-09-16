#pragma once

#include "HostContractFixtures.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

namespace ctcompile::test::host_contract {

inline void checkDOMBranchFilter(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    const std::string source = R"MLIR(
module {
  ctjs.func @branch$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %zero = ctjs.constant #ctjs.number<0>
    %flag = ctjs.constant #ctjs.boolean<false>
    %condition = ctjs.truthy %flag
    %answer = scf.if %condition -> (!ctjs.value) {
      scf.yield %zero : !ctjs.value
    } else {
      %Object = ctjs.load_global "Object"
      %keysName = ctjs.constant #ctjs.string<"keys">
      %keysMethod = ctjs.get_property %Object[%keysName]
      %datasetName = ctjs.constant #ctjs.string<"dataset">
      %dataset = ctjs.get_property %element[%datasetName]
      %keys = ctjs.call %keysMethod(%Object, %dataset)
      %filterName = ctjs.constant #ctjs.string<"filter">
      %filter = ctjs.get_property %keys[%filterName]
      %callback = ctjs.create_closure %callee[1] this %u
      %filtered = ctjs.call %filter(%keys, %callback)
      %lengthName = ctjs.constant #ctjs.string<"length">
      %length = ctjs.get_property %filtered[%lengthName]
      scf.yield %length : !ctjs.value
    }
    ctjs.return %answer
  }
  ctjs.func private @predicate$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %key: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %startsName = ctjs.constant #ctjs.string<"startsWith">
    %starts = ctjs.get_property %key[%startsName]
    %prefix = ctjs.constant #ctjs.string<"bs">
    %first = ctjs.call %starts(%key, %prefix)
    %condition = ctjs.truthy %first
    %selected = scf.if %condition -> (!ctjs.value) {
      %again = ctjs.get_property %key[%startsName]
      %excluded = ctjs.constant #ctjs.string<"bsConfig">
      %second = ctjs.call %again(%key, %excluded)
      %not = ctjs.unary not %second
      scf.yield %not : !ctjs.value
    } else {
      scf.yield %first : !ctjs.value
    }
    ctjs.return %selected
  }
}
)MLIR";
    const auto noEvidence = [](mlir::ModuleOp input, const DOMEntryAnalysis & proof) {
        bool empty = !proof.proved() && !proof.entry() && !proof.wrapper() &&
                     proof.parameters().empty() && proof.callbacks().empty() &&
                     proof.stringResults().empty() && proof.optionalStringJoins().empty() &&
                     proof.stringRefinements().empty();
        input.walk([&](ctjs::FuncOp function) {
            for (mlir::BlockArgument argument : function.getBody().front().getArguments()) {
                empty &= !proof.isCallbackParameter(argument) && !proof.isElement(argument) &&
                         !proof.isDatasetElement(argument);
            }
        });
        input.walk([&](ctjs::CreateClosureOp closure) { empty &= !proof.callback(closure); });
        input.walk([&](ctjs::LoadGlobalOp load) { empty &= !proof.isInitialIntrinsic(load); });
        input.walk([&](ctjs::CallOp call) { empty &= !proof.call(call); });
        input.walk([&](ctjs::GetPropertyOp read) {
            empty &= !proof.method(read) && !proof.isDataset(read.getResult()) &&
                     !proof.isStringVectorLength(read) && !proof.isStringVectorIndex(read);
        });
        return empty;
    };
    const auto checkPrepared = [&](mlir::ModuleOp input, llvm::StringRef entry) {
        HostContract contract;
        contract.entry = entry.str();
        contract.elementParameters = {0};
        contract.datasetParameters = {0};
        contract.initialIntrinsics = {"Object", "Array", "String"};
        contract.moduleSha256 = hostContractFingerprint(input);
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            contract.provider = provider;
            DOMEntryAnalysis proof(input, contract);
            check(proof.proved(), "branch-local source callback receives complete DOM reproof");
            if (!proof.proved()) {
                std::fprintf(stderr, "%s\n", proof.reason().str().c_str());
                continue;
            }
            unsigned callbacks = 0, calls = 0;
            input.walk([&](ctjs::CreateClosureOp closure) {
                ++callbacks;
                check(closure->getParentOfType<mlir::scf::IfOp>() &&
                          proof.callback(closure) ==
                              input.lookupSymbol<ctjs::FuncOp>("predicate$1"),
                      "retained callback keeps its exact identity inside the selected arm");
            });
            input.walk([&](ctjs::CallOp call) {
                ++calls;
                check(proof.call(call) != nullptr,
                      "every preserved callback and dataset call has independent evidence");
            });
            check(callbacks == 1 && calls == 4 && proof.callbacks().size() == 1,
                  "source expansion preserves the filter and both original predicate calls");
            check(hostContractFingerprint(input) == contract.moduleSha256,
                  "complete branch callback proof does not rewrite its source");
            check(DOMEntryAnalysis(input, contract, proof.steps()).proved(),
                  "branch callback proof reproduces its exact completion budget");
            for (unsigned budget = 0; budget < proof.steps(); ++budget) {
                DOMEntryAnalysis limited(input, contract, budget);
                check(limited.exhausted() && noEvidence(input, limited),
                      "every incomplete branch callback proof withholds all evidence");
            }
            for (const auto & names : {std::vector<std::string>{"Object", "Array"},
                                       std::vector<std::string>{"Object", "String"}}) {
                auto missing = contract;
                missing.initialIntrinsics = names;
                check(noEvidence(input, DOMEntryAnalysis(input, missing)),
                      "branch scheduling does not supply absent Array or String authority");
            }
        }
    };

    auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    check(static_cast<bool>(module), "branch-local filter scheduling fixture parses");
    if (!module) { return; }
    const auto fingerprint = hostContractFingerprint(*module);
    auto error = expandDOMHelpers(*module, "branch$0", 100000);
    check(!error, "source preparation visits the confined callback inside the continuation");
    if (error) {
        std::fprintf(stderr, "%s\n", llvm::toString(std::move(error)).c_str());
        return;
    }
    check(fingerprint == hostContractFingerprint(*module),
          "scheduling retains the complete source body and both function identities");
    checkPrepared(*module, "branch$0");

    bool completed = false;
    for (unsigned budget = 0; budget < 10000; ++budget) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
        auto limited = expandDOMHelpers(*input, "branch$0", budget);
        if (!limited) {
            completed = true;
            break;
        }
        const auto reason = llvm::toString(std::move(limited));
        check(reason.find("budget") != std::string::npos &&
                  fingerprint == hostContractFingerprint(*input),
              "every incomplete callback scheduling budget preserves the source");
    }
    check(completed, "bounded callback scheduling reaches its complete proof");

    auto helperSource = replaced(source, "ctjs.func @branch$0", "ctjs.func private @helper$2");
    helperSource = replaced(helperSource, "module {", R"MLIR(
module {
  ctjs.func @entry$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %helper = ctjs.create_closure %callee[2] this %u
    %answer = ctjs.call %helper(%u, %element)
    ctjs.return %answer
  }
)MLIR");
    auto helper = mlir::parseSourceString<mlir::ModuleOp>(helperSource, &context);
    check(static_cast<bool>(helper), "helper-owned branch callback fixture parses");
    if (helper) {
        auto expanded = expandDOMHelpers(*helper, "entry$0", 100000);
        check(!expanded, "helper expansion retains its branch-local callback source identity");
        if (expanded) {
            std::fprintf(stderr, "%s\n", llvm::toString(std::move(expanded)).c_str());
        } else {
            check(!helper->lookupSymbol<ctjs::FuncOp>("helper$2"),
                  "only the completely expanded helper is retired");
            checkPrepared(*helper, "entry$0");
        }
    }

    for (const auto & invalid :
         {replaced(source,
                   "%filtered =", "ctjs.store_global \"saved\", %callback\n      %filtered ="),
          replaced(source, "%filter(%keys, %callback)", "%callback(%u, %element)"),
          replaced(source, "%filter(%keys, %callback)", "%filter(%keys, %callback, %u)"),
          replaced(source, "%filtered = ctjs.call %filter(%keys, %callback)", R"MLIR(
      %filtered = scf.if %condition -> (!ctjs.value) {
        %left = ctjs.call %filter(%keys, %callback)
        scf.yield %left : !ctjs.value
      } else {
        %right = ctjs.call %filter(%keys, %callback)
        scf.yield %right : !ctjs.value
      }
)MLIR"),
          replaced(source, "%callee[1] this %u", "%element[1] this %u"),
          replaced(source, "%callee[1] this %u", "%callee[1] this %element"),
          replaced(source, "%callee[1] this %u", "%callee[1] this %u captures %element"),
          replaced(source, "%callee[1] this %u", "%callee[2] this %u"),
          replaced(source, "%callback =", "%cell = ctjs.create_cell %u\n      %callback ="),
          replaced(source, "%callback =",
                   "%duplicate = ctjs.create_closure %callee[1] this %u\n      %callback ="),
          replaced(source, "%callback =",
                   "%result = ctjs.create_object\n      ctjs.set_property %result[%filterName], "
                   "%u\n      %callback ="),
          replaced(source, "\n}\n", R"MLIR(
  ctjs.func private @unused$2(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
}
)MLIR")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "unsupported branch-local identity fixture parses");
        if (!input) { continue; }
        const auto before = hostContractFingerprint(*input);
        auto refused = expandDOMHelpers(*input, "branch$0", 100000);
        check(static_cast<bool>(refused) && before == hostContractFingerprint(*input),
              "escaping, captured, repeated, generic or unvisited identities remain refused");
        if (refused) { llvm::consumeError(std::move(refused)); }
    }

    auto loopSource = replaced(source, "%answer = scf.if", R"MLIR(
    %loop = scf.while (%state = %zero) : (!ctjs.value) -> !ctjs.value {
    %answer = scf.if
)MLIR");
    loopSource = replaced(loopSource, "ctjs.return %answer", R"MLIR(
      scf.condition(%condition) %answer : !ctjs.value
    } do {
    ^bb0(%state: !ctjs.value):
      scf.yield %state : !ctjs.value
    }
    ctjs.return %loop
)MLIR");
    auto loop = mlir::parseSourceString<mlir::ModuleOp>(loopSource, &context);
    check(static_cast<bool>(loop), "repeated branch callback fixture parses");
    if (loop) {
        const auto before = hostContractFingerprint(*loop);
        auto refused = expandDOMHelpers(*loop, "branch$0", 100000);
        check(static_cast<bool>(refused) && before == hostContractFingerprint(*loop),
              "loop-local callback creation remains refused without rewriting its source");
        if (refused) { llvm::consumeError(std::move(refused)); }
    }

    // Scheduling supplies no authority for effects or the original !element
    // guard. Those remain the complete entry proof's responsibility.
    for (const auto & invalid :
         {replaced(source, "ctjs.return %selected",
                   "ctjs.store_global \"saved\", %key\n    ctjs.return %selected"),
          replaced(source, "%condition = ctjs.truthy %flag",
                   "%not = ctjs.unary not %element\n    %condition = ctjs.truthy %not"),
          replaced(source,
                   "%filtered =", "ctjs.set_property %keys[%filterName], %u\n      %filtered =")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "complete branch callback refusal fixture parses");
        if (!input) { continue; }
        const auto before = hostContractFingerprint(*input);
        auto prepared = expandDOMHelpers(*input, "branch$0", 100000);
        check(!prepared && before == hostContractFingerprint(*input),
              "preparation preserves every unsupported source observation for complete reproof");
        if (prepared) {
            llvm::consumeError(std::move(prepared));
            continue;
        }
        HostContract contract;
        contract.provider = HostContract::Provider::ctbrowserDOM;
        contract.entry = "branch$0";
        contract.elementParameters = {0};
        contract.datasetParameters = {0};
        contract.initialIntrinsics = {"Object", "Array", "String"};
        contract.moduleSha256 = before;
        check(noEvidence(*input, DOMEntryAnalysis(*input, contract)),
              "complete reproof rejects callback effects, intrinsic mutation and unproved guards");
    }
}

} // namespace ctcompile::test::host_contract
