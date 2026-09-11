#include "Tests.h"

namespace ctcompile::test::owned_global_shared_map {

void checkNestedOwner(mlir::MLIRContext & context, const std::string & source, bool lifted) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto extra = [&](bool before, const char * literal) {
        const std::string marker = before   ? "    %putResult ="
                                   : lifted ? "    %getterEnv ="
                                            : "    %answer =";
        auto addition = std::string("    %extra = ctjs.constant ") + literal +
                        "\n    %extraPutter = ctjs.get_property %owned[%putKey]\n";
        addition += lifted ? "    %extraEnv = ctjs.load_upvalue %extraPutter[0]\n"
                             "    %extraResult = ctjs.call_direct @put$4(%owned, %u, "
                             "%extraPutter, %extraEnv, %extra)\n"
                           : "    %extraResult = ctjs.call %extraPutter(%owned, %extra)\n";
        return replaced(source, marker, addition + marker);
    };
    unsigned rows = 0;
    for (const auto & [program, mask, count] :
         {std::tuple{source, unsigned(Alternatives::String), 3u},
          {extra(true, "#ctjs.null"), Alternatives::String | Alternatives::Null, 4u},
          {extra(false, "#ctjs.null"), Alternatives::String | Alternatives::Null, 4u},
          {extra(false, "#ctjs.undefined"), Alternatives::String | Alternatives::Undefined, 4u}}) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "source/prepared nested owner fixture parses");
        if (!module) { continue; }
        auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        check(query.proved() && query.roots().size() == 1,
              "a nested result DAG publishes one owner only after the full family proof");
        if (!query.proved() || query.roots().empty()) {
            std::fprintf(stderr, "nested owner %s row %u: %s\n", lifted ? "prepared" : "source",
                         rows, query.reason().str().c_str());
            continue;
        }
        const auto & table = *query.roots().front().methodTable;
        const auto & capture = *table.capturedMap;
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        const Alternatives expected{Alternatives::String, mask, true};
        bool complete = table.methods.size() == 2 && table.calls.size() == count &&
                        capture.parameters.size() == 2 && capture.closures.size() == 2 &&
                        capture.reads.size() == 3 && capture.calls.size() == 2 &&
                        capture.upvalues.size() == (lifted ? 0u : 2u);
        unsigned dependencies = 0, setters = 0;
        mlir::Operation * previous = nullptr;
        mlir::Operation * inner = nullptr;
        mlir::Operation * outer = nullptr;
        for (const auto & edge : table.calls) {
            complete &= edge.capturedMap && (!previous || previous->isBeforeInBlock(edge.call));
            previous = edge.call;
            if (!edge.capturedMap) { continue; }
            complete &= edge.capturedMap->parameters == capture.parameters &&
                        edge.capturedMap->calls == capture.calls &&
                        edge.capturedMap->reads == capture.reads;
            if (edge.function != setter) {
                complete &= edge.arguments.empty();
                continue;
            }
            ++setters;
            complete &= edge.arguments.size() == 1;
            if (edge.arguments.size() != 1) { continue; }
            const auto & argument = edge.arguments.front();
            complete &=
                argument.actual == edge.call->getOperand(lifted ? 4 : 2) &&
                argument.parameter == setter.getBody().front().getArgument(lifted ? 4 : 3) &&
                argument.alternatives == expected;
            for (const auto & producer : table.calls) {
                if (argument.actual != producer.call->getResult(0)) { continue; }
                ++dependencies;
                inner = producer.call;
                outer = edge.call;
                complete &=
                    producer.function == setter && producer.call->isBeforeInBlock(edge.call);
            }
        }
        check(complete && setters == count - 1 && dependencies == 1 && inner && outer,
              "owner calls keep exact result edges and one complete generalized parameter family");
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "nested owning analysis preserves the original invocation graph");
        if (!complete || !inner || !outer) { continue; }
        if (rows <= 2) {
            const unsigned completion = query.steps();
            check(completion < 15000, "nested owner census remains bounded");
            if (completion >= 15000) { continue; }
            for (unsigned budget = 0; budget < completion; ++budget) {
                OwnedGlobalRoots limited(*module, contract, budget);
                check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                          empty(*module, limited),
                      "no partial invocation result becomes an owner at an incomplete budget");
            }
            OwnedGlobalRoots exact(*module, contract, completion);
            check(exact.proved() && exact.steps() == completion,
                  "the exact nested owner budget reproduces every source method and call");
            std::printf("nested owner %s row %u: all %u incomplete budgets checked\n",
                        lifted ? "prepared" : "source", rows, completion);
        }
        mlir::Builder attributes(&context);
        module->walk([&](mlir::Operation * operation) {
            operation->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.host_proved", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.map_present", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.map_read_type",
                               attributes.getStringAttr("nullable_string"));
            operation->setAttr("ctnative.map_write_type", attributes.getStringAttr("string"));
        });
        contract = requested(*module);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "forged host/native reports leave the independent nested owner proof reproducible");
        const auto mutation = [&](mlir::Operation * operation, unsigned operand,
                                  mlir::Value value) {
            const auto saved = operation->getOperand(operand);
            operation->setOperand(operand, value);
            OwnedGlobalRoots stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      empty(*module, stale),
                  "a nested source mutation invalidates its original owner fingerprint");
            OwnedGlobalRoots fresh(*module, requested(*module));
            check(
                !fresh.proved() && !fresh.exhausted() && empty(*module, fresh),
                "fresh fingerprints and forged reports cannot publish an incomplete nested owner");
            operation->setOperand(operand, saved);
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring the live source edge restores the whole nested owner");
        };
        const unsigned argument = lifted ? 4u : 2u;
        mutation(outer, argument, outer->getResult(0));
        mutation(inner, argument, outer->getResult(0));
        mutation(outer, argument,
                 outer->getParentOfType<ctjs::FuncOp>().getBody().front().getArgument(0));
        // The later call has its own String literal, so the reversed result
        // edge is neither a cycle nor an incompatible parameter category.
        const auto nestedContract = contract;
        const auto nestedActual = outer->getOperand(argument);
        outer->setOperand(argument, inner->getOperand(argument));
        contract = requested(*module);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "independent same-tag calls retain the complete owner before reversing an edge");
        mutation(inner, argument, outer->getResult(0));
        outer->setOperand(argument, nestedActual);
        contract = nestedContract;
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring a dominating producer restores the original nested owner");
        auto getter = module->lookupSymbol<ctjs::FuncOp>("get$3");
        auto returned = llvm::cast<ctjs::ReturnOp>(setter.getBody().front().getTerminator());
        auto sibling = llvm::cast<ctjs::ReturnOp>(getter.getBody().front().getTerminator());
        mutation(returned, 0, setter.getBody().front().getArgument(0));
        mutation(sibling, 0, getter.getBody().front().getArgument(0));
        auto write = capture.calls.front();
        mutation(write, 3, write.getReceiver());
    }
    check(rows == 4, "all String/nullable nested owner census orderings ran");
}

} // namespace ctcompile::test::owned_global_shared_map
