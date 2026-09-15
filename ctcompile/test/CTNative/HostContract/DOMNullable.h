#pragma once

#include "HostContractFixtures.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

namespace ctcompile::test::host_contract {

inline void checkDOMNullable(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    const std::string prefix = R"MLIR(
module {
  ctjs.func @guarded$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %frame = ctjs.frame_enter 20
    %undefined = ctjs.constant #ctjs.undefined
    %getKey = ctjs.constant #ctjs.string<"getAttribute">
    %get = ctjs.get_property %element[%getKey]
    %name = ctjs.constant #ctjs.string<"data-bs-config">
    %saved = ctjs.call %get(%element, %name)
    ctjs.root %saved in %frame
    %kind = ctjs.unary typeof %saved
    %word = ctjs.constant #ctjs.string<"string">
    %same = ctjs.compare eq %word, %kind
    %other = ctjs.unary not %same
)MLIR";
    const std::string present = R"MLIR(
      ctjs.root %saved in %frame
      %decode = ctjs.load_global "decodeURIComponent"
      %decoded = ctjs.invoke {
        %called = ctjs.call %decode(%undefined, %saved)
        ctjs.invoke_exit %called state()
      } normal {
      ^bb0(%returned: !ctjs.value):
        ctjs.invoke_yield(%returned)
      } unwind {
      ^bb0(%error: !ctjs.value):
        ctjs.invoke_yield(%saved)
      } : !ctjs.value
      scf.yield %decoded : !ctjs.value
)MLIR";
    const std::string absent = "      scf.yield %saved : !ctjs.value\n";
    const auto guarded = [&](llvm::StringRef condition, bool stringFirst) {
        return prefix + "    %condition = ctjs.truthy " + condition.str() +
               "\n    %answer = scf.if %condition -> (!ctjs.value) {\n" +
               (stringFirst ? present : absent) + "    } else {\n" +
               (stringFirst ? absent : present) + R"MLIR(
    }
    ctjs.frame_exit %frame
    ctjs.return %answer
  }
}
)MLIR";
    };
    const std::string source = guarded("%other", false);
    const auto noEvidence = [](const DOMEntryAnalysis & proof) {
        return !proof.proved() && !proof.entry() && !proof.wrapper() &&
               proof.parameters().empty() && proof.optionalStringJoins().empty() &&
               proof.stringRefinements().empty() && proof.stringResults().empty();
    };
    HostContract contract;
    contract.provider = HostContract::Provider::ctbrowserDOM;
    contract.entry = "guarded$0";
    contract.elementParameters = {0};
    contract.initialIntrinsics = {"decodeURIComponent"};
    const auto query = [&](const std::string & text, bool expected, bool budgets = false) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "nullable typeof guard fixture parses");
        if (!module) { return; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*module);
        DOMEntryAnalysis proof(*module, request);
        check(proof.proved() == expected, "nullable guard proof matches complete source");
        if (proof.proved() != expected) {
            std::fprintf(stderr, "%s\n%s\n", text.c_str(), proof.reason().str().c_str());
        }
        check(hostContractFingerprint(*module) == request.moduleSha256,
              "nullable presence proof never changes source or producer types");
        if (!expected) {
            check(noEvidence(proof), "refused nullable source publishes no partial refinements");
            return;
        }
        if (!proof.proved()) { return; }
        for (const auto & refinement : proof.stringRefinements()) {
            check(refinement.block && refinement.optional &&
                      llvm::isa<ctjs::ValueType>(refinement.optional.getType()),
                  "String refinement keeps its original optional SSA value");
            for (const auto & use : refinement.uses) {
                auto * scope = use.operation ? use.operation->getBlock() : nullptr;
                while (scope && scope != refinement.block) {
                    auto * parent = scope->getParentOp();
                    scope = parent ? parent->getBlock() : nullptr;
                }
                check(use.operation && use.operandIndex < use.operation->getNumOperands() &&
                          use.operation->getOperand(use.operandIndex) == refinement.optional &&
                          !llvm::isa<ctjs::RootOp>(use.operation) && scope == refinement.block,
                      "every refinement use names the exact dominated source operand");
            }
        }
        if (!budgets) { return; }
        check(DOMEntryAnalysis(*module, request, proof.steps()).proved(),
              "nullable guard proof reproduces the exact complete budget");
        for (unsigned budget = 0; budget < proof.steps(); ++budget) {
            DOMEntryAnalysis limited(*module, request, budget);
            check(noEvidence(limited) && limited.exhausted(),
                  "every incomplete guard budget withholds all live branch evidence");
        }
    };
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        contract.provider = provider;
        query(source, true, true);
        query(guarded("%same", true), true, true);
    }
    for (llvm::StringRef comparison :
         {"eq %kind, %word", "strict_eq %word, %kind", "strict_eq %kind, %word"}) {
        query(replaced(source, "eq %word, %kind", comparison), true);
    }
    query(replaced(guarded("%same", false), "#ctjs.string<\"string\">", "#ctjs.string<\"object\">"),
          true);
    query(replaced(source, "%condition = ctjs.truthy %other",
                   "%twice = ctjs.unary not %other\n    %again = ctjs.unary not %twice\n"
                   "    %condition = ctjs.truthy %again"),
          true);

    // A selected result carries exactly its two original predecessors. Its own
    // guard may refine it, but a guard on just one predecessor cannot.
    const std::string joined = R"MLIR(
    %null = ctjs.constant #ctjs.null
    %false = ctjs.constant #ctjs.boolean<false>
    %choice = ctjs.truthy %false
    %saved = scf.if %choice -> (!ctjs.value) {
      scf.yield %read : !ctjs.value
    } else {
      scf.yield %null : !ctjs.value
    }
)MLIR";
    auto joinedSource = replaced(source, "%saved = ctjs.call", "%read = ctjs.call");
    joinedSource = replaced(joinedSource, "    ctjs.root %saved", joined + "    ctjs.root %saved");
    query(joinedSource, true, true);
    query(replaced(joinedSource, "typeof %saved", "typeof %read"), false);

    const std::string inherited = R"MLIR(
      %inherited = scf.if %condition -> (!ctjs.value) {
        scf.yield %saved : !ctjs.value
      } else {
        scf.yield %saved : !ctjs.value
      }
)MLIR";
    auto nested = replaced(source, "      %decode =", inherited + "      %decode =");
    nested = replaced(nested, "%decode(%undefined, %saved)", "%decode(%undefined, %inherited)");
    query(nested, true, true);

    query(guarded("%same", false), false);
    query(guarded("%saved", true), false);
    query(replaced(source, "eq %word, %kind", "eq %word, %saved"), false);
    query(replaced(replaced(source, "eq %word, %kind", "strict_eq %word, %saved"),
                   "#ctjs.string<\"string\">", "#ctjs.string<\"\">"),
          false);
    query(replaced(source, "#ctjs.string<\"string\">", "#ctjs.string<\"number\">"), false);
    query(replaced(source, "typeof %saved", "typeof %element"), false);
    query(replaced(source, "ctjs.invoke_yield(%saved)", "ctjs.invoke_yield(%error)"), false);
    auto reread = replaced(source, "      %decode =",
                           "      %reread = ctjs.call %get(%element, %name)\n      %decode =");
    query(replaced(reread, "%decode(%undefined, %saved)", "%decode(%undefined, %reread)"), false);
    query(replaced(source, "scf.yield %saved : !ctjs.value", present), false);
    query(replaced(source, "    ctjs.return %answer",
                   "    %decodeOutside = ctjs.load_global \"decodeURIComponent\"\n"
                   "    %outside = ctjs.invoke {\n"
                   "      %called = ctjs.call %decodeOutside(%undefined, %saved)\n"
                   "      ctjs.invoke_exit %called state()\n"
                   "    } normal {\n"
                   "    ^bb0(%returned: !ctjs.value):\n"
                   "      ctjs.invoke_yield(%returned)\n"
                   "    } unwind {\n"
                   "    ^bb0(%error: !ctjs.value):\n"
                   "      ctjs.invoke_yield(%saved)\n"
                   "    } : !ctjs.value\n"
                   "    ctjs.return %outside"),
          false);

    auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    if (!module) { return; }
    contract.moduleSha256 = hostContractFingerprint(*module);
    ctjs::CallOp getter, call;
    ctjs::InvokeOp invocation;
    ctjs::UnaryOp negated;
    mlir::scf::IfOp branch;
    module->walk([&](ctjs::CallOp operation) {
        if (operation->getParentOfType<ctjs::InvokeOp>()) {
            call = operation;
        } else {
            getter = operation;
        }
    });
    module->walk([&](ctjs::InvokeOp operation) { invocation = operation; });
    module->walk([&](mlir::scf::IfOp operation) { branch = operation; });
    module->walk([&](ctjs::UnaryOp operation) {
        if (operation.getKind() == ctjs::UnaryKind::Not) { negated = operation; }
    });
    DOMEntryAnalysis proof(*module, contract);
    if (proof.proved() && proof.stringRefinements().size() == 1) {
        const auto & refinement = proof.stringRefinements().front();
        check(refinement.block == &branch.getElseRegion().front() &&
                  refinement.optional == getter.getResult() && refinement.uses.size() == 2 &&
                  proof.call(getter) && proof.call(getter)->returnsOptionalString() &&
                  proof.call(call) && proof.call(call)->element == getter.getResult() &&
                  proof.optionalStringJoins().size() == 1 &&
                  proof.optionalStringJoins().front() == branch.getResult(0) &&
                  proof.stringResults().size() == 1 &&
                  proof.stringResults().front() == invocation.getResult(0),
              "guard proves exactly URI input and catch snapshot without narrowing producer");
    } else {
        check(false, "original nullable URI guard has one selected String refinement");
    }
    mlir::Builder builder(&context);
    getter->setAttr("ctnative.host_string", builder.getBoolAttr(true));
    negated->setAttr("ctnative.host_string_guard", builder.getBoolAttr(true));
    check(DOMEntryAnalysis(*module, contract).proved(),
          "advisory String reports never replace the live guard proof");
    auto condition = branch.getCondition();
    auto changedCondition = negated.getOperand();
    // Truthy takes a JavaScript Boolean and returns i1; retain well-typed IR
    // while reversing its real predicate underneath the printed reports.
    auto truth = condition.getDefiningOp<ctjs::TruthyOp>();
    truth->setOperand(0, changedCondition);
    DOMEntryAnalysis stale(*module, contract);
    check(noEvidence(stale) && stale.reason().contains("fingerprint"),
          "stale nullable predicate fingerprint grants no use evidence");
    contract.moduleSha256 = hostContractFingerprint(*module);
    check(noEvidence(DOMEntryAnalysis(*module, contract)),
          "fresh fingerprint and forged reports cannot reverse String-arm polarity");
    truth->setOperand(0, negated.getResult());
    contract.moduleSha256 = hostContractFingerprint(*module);
    auto actual = call.getArgs().front();
    call->setOperand(
        2, module->lookupSymbol<ctjs::FuncOp>(contract.entry).getBody().front().getArgument(3));
    contract.moduleSha256 = hostContractFingerprint(*module);
    check(noEvidence(DOMEntryAnalysis(*module, contract)),
          "fresh source cannot transfer a saved String guard to another actual");
    call->setOperand(2, actual);
    contract.moduleSha256 = hostContractFingerprint(*module);
    contract.initialIntrinsics = {"Number"};
    check(noEvidence(DOMEntryAnalysis(*module, contract)),
          "nullable refinement never grants initial URI identity");
}

} // namespace ctcompile::test::host_contract
