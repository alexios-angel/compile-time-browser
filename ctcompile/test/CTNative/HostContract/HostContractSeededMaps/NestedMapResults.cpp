#include "Tests.h"

namespace ctcompile::test::host_contract_seeded_maps {

void checkNestedMapResults(mlir::MLIRContext & context, const std::string & shared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    constexpr unsigned nullable = Alternatives::String | Alternatives::Null;
    auto source =
        replaced(shared, "@put$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)",
                 "@put$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                 "%entryKey: !ctjs.value)");
    source = replaced(source, "    %entryKey = ctjs.constant #ctjs.string<\"x\">\n", "");
    source = replaced(source, "%setter(%state, %entryKey, %value)",
                      "%setter(%state, %entryKey, %entryKey)");
    source = replaced(source,
                      "    %u = ctjs.constant #ctjs.undefined\n"
                      "    ctjs.return %u\n  }\n}\n",
                      "    %getKey = ctjs.constant #ctjs.string<\"get\">\n"
                      "    %reader = ctjs.get_property %state[%getKey]\n"
                      "    %loaded = ctjs.call %reader(%state, %entryKey)\n"
                      "    ctjs.return %loaded\n  }\n}\n");
    source = replaced(source, "    %putResult = ctjs.call %putter(%owned)",
                      "    %actual = ctjs.constant #ctjs.string<\"future\">\n"
                      "    %putResult = ctjs.call %putter(%owned, %actual)\n"
                      "    %outerPutter = ctjs.get_property %owned[%putKey]\n"
                      "    %outerResult = ctjs.call %outerPutter(%owned, %putResult)");
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto withheld = [](mlir::ModuleOp module, const HostContractAnalysis & query) {
        bool empty = query.callables().empty();
        module.walk([&](mlir::Operation * operation) {
            empty &= !query.callable(operation);
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                empty &= !query.property(read);
            }
        });
        return empty;
    };
    for (const bool prepared : {false, true}) {
        auto program = source;
        if (prepared) {
            for (const char * name : {"get$2", "put$3"}) {
                program = replaced(program, "captures %cell", "captures %state");
                const std::string header = std::string("@") + name +
                                           "(%this: !ctjs.value, %new: !ctjs.value, "
                                           "%callee: !ctjs.value";
                program = replaced(program, header, header + ", %state: !ctjs.value");
                program = replaced(program,
                                   "attributes {upvalue_count = 1 : i32} {\n"
                                   "    %state = ctjs.load_upvalue %callee[0]\n",
                                   "attributes {upvalue_count = 0 : i32} {\n");
            }
            for (const auto & [closure, result, actual] :
                 {std::tuple{"putter", "putResult", ", %actual"},
                  {"outerPutter", "outerResult", ", %putResult"},
                  {"getter", "answer", ""}}) {
                const auto call = std::string("%") + result + " = ctjs.call %" + closure +
                                  "(%owned" + actual + ")";
                const auto direct = std::string("%") + closure + "Env = ctjs.load_upvalue %" +
                                    closure + "[0]\n    %" + result + " = ctjs.call_direct @" +
                                    (std::string(closure) == "getter" ? "get$2" : "put$3") +
                                    "(%owned, %u, %" + closure + ", %" + closure + "Env" + actual +
                                    ")";
                program = replaced(program, call, direct);
            }
        }
        const auto call = [&](const std::string & name, const std::string & actual) {
            auto text = "    %" + name + "Putter = ctjs.get_property %owned[%putKey]\n";
            if (prepared) {
                text += "    %" + name + "Env = ctjs.load_upvalue %" + name + "Putter[0]\n";
            }
            return text + "    %" + name + "Result = " +
                   (prepared ? "ctjs.call_direct @put$3(%owned, %u, %" + name + "Putter, %" + name +
                                   "Env, "
                             : "ctjs.call %" + name + "Putter(%owned, ") +
                   actual + ")\n";
        };
        const std::string first =
            prepared ? "    %putterEnv = ctjs.load_upvalue" : "    %putResult = ctjs.call";
        const std::string last =
            prepared ? "    %getterEnv = ctjs.load_upvalue" : "    %answer = ctjs.call";
        const auto extra = [&](const std::string & definition, bool before) {
            const auto & marker = before ? first : last;
            return replaced(program, marker,
                            "    %extra = " + definition + "\n" + call("extra", "%extra") + marker);
        };
        const auto edges = [&](mlir::ModuleOp module, const HostContractAnalysis & query,
                               unsigned count, unsigned mask, unsigned dependencies) {
            auto getter = module.lookupSymbol<ctjs::FuncOp>("get$2");
            auto setter = module.lookupSymbol<ctjs::FuncOp>("put$3");
            const Alternatives expected{mask & Alternatives::String, mask, true};
            bool complete = query.callables().size() == count;
            unsigned setters = 0, results = 0;
            mlir::Operation * previous = nullptr;
            for (const auto & edge : query.callables()) {
                complete &= edge.capturedMap && (!previous || previous->isBeforeInBlock(edge.call));
                previous = edge.call;
                if (!edge.capturedMap) { continue; }
                const auto & capture = *edge.capturedMap;
                complete &= capture.closures.size() == 2 && capture.parameters.size() == 2 &&
                            capture.reads.size() == 3 && capture.calls.size() == 2 &&
                            capture.upvalues.size() == (prepared ? 0u : 2u);
                if (edge.function == getter) {
                    complete &= edge.arguments.empty();
                    continue;
                }
                ++setters;
                complete &= edge.function == setter && edge.arguments.size() == 1;
                if (edge.arguments.size() != 1) { continue; }
                const auto & argument = edge.arguments.front();
                complete &=
                    argument.parameter == setter.getBody().front().getArgument(prepared ? 4 : 3) &&
                    argument.actual == edge.call->getOperand(prepared ? 4 : 2) &&
                    argument.alternatives == expected;
                unsigned families = 0;
                for (const auto & parameters : capture.parameters) {
                    if (parameters.function != setter) { continue; }
                    ++families;
                    complete &= parameters.alternatives == std::vector{expected};
                }
                complete &= families == 1;
                for (const auto & producer : query.callables()) {
                    if (argument.actual != producer.call->getResult(0)) { continue; }
                    ++results;
                    complete &=
                        producer.function == setter && producer.call->isBeforeInBlock(edge.call);
                }
            }
            check(complete && setters == count - 1 && results == dependencies,
                  "every invocation retains its SSA edge and the final generalized whole-method "
                  "census");
        };
        unsigned rows = 0;
        const auto variant = [&](const std::string & text, bool expected, const char * message,
                                 unsigned count = 3, unsigned mask = Alternatives::String,
                                 unsigned dependencies = 1) {
            ++rows;
            auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
            check(static_cast<bool>(module), "source/prepared nested Map fixture parses");
            if (!module) { return; }
            const auto contract = requested(*module);
            HostContractAnalysis query(*module, contract);
            check(query.proved() == expected && !query.exhausted() &&
                      (expected || withheld(*module, query)),
                  message);
            if (query.proved() != expected || query.exhausted()) {
                std::fprintf(stderr, "nested Map host %s case %u: %s\n",
                             prepared ? "prepared" : "source", rows, query.reason().str().c_str());
            }
            if (expected && query.proved()) { edges(*module, query, count, mask, dependencies); }
            check(hostContractFingerprint(*module) == contract.moduleSha256,
                  "invocation and final census proofs preserve every source operation and operand");
        };
        variant(program, true, "an earlier result seeds another invocation of the same method");
        const auto nullBefore = extra("ctjs.constant #ctjs.null", true);
        for (const bool before : {false, true}) {
            variant(extra("ctjs.constant #ctjs.null", before), true,
                    "a Null actual joins all nested String calls independently of source order", 4,
                    nullable);
            variant(extra("ctjs.constant #ctjs.undefined", before), true,
                    "a wider Undefined actual joins the complete nested String family", 4,
                    Alternatives::String | Alternatives::Undefined);
            for (const char * definition :
                 {"ctjs.constant #ctjs.boolean<true>", "ctjs.constant #ctjs.number<0>",
                  "ctjs.create_object", "ctjs.load_global \"unknown\""}) {
                variant(extra(definition, before), false,
                        "one incompatible or unknown actual withholds every nested family edge");
            }
        }
        variant(replaced(nullBefore, first,
                         "    %missing = ctjs.constant #ctjs.undefined\n" +
                             call("missing", "%missing") + first),
                true, "a Null/Undefined prefix waits for the later complete String census", 5,
                nullable | Alternatives::Undefined);
        variant(replaced(program, last, call("deeper", "%outerResult") + last), true,
                "a three-invocation result chain completes without recursive method authority", 4,
                Alternatives::String, 2);
        variant(replaced(program, last, call("fanout", "%putResult") + last), true,
                "two later calls independently consume one completed source invocation", 4,
                Alternatives::String, 2);
        variant(replaced(program, "#ctjs.string<\"future\">", "#ctjs.null"), true,
                "a Null-only dependency retains its exact primitive category", 3,
                Alternatives::Null);
        variant(replaced(program, "#ctjs.string<\"future\">", "#ctjs.undefined"), true,
                "an Undefined-only dependency retains its exact primitive category", 3,
                Alternatives::Undefined);
        variant(replaced(program, "    ctjs.return %loaded", "    ctjs.return %this"), false,
                "a method with an unknown result cannot seed its next invocation");
        variant(replaced(program, "    ctjs.return %answer", "    ctjs.return %this"), false,
                "a bad sibling body vetoes otherwise complete nested result evidence");
        auto foreign =
            replaced(program, last,
                     "    %foreign = ctjs.create_closure %callee[4] this %u\n"
                     "    %foreignResult = ctjs.call_direct @foreign$4(%u, %u, %foreign)\n" +
                         call("foreignConsumer", "%foreignResult") + last);
        foreign = replaced(foreign, "\n}\n", R"MLIR(
  ctjs.func private @foreign$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %value = ctjs.constant #ctjs.string<"foreign">
    ctjs.return %value
  }
}
)MLIR");
        variant(foreign, false,
                "a foreign call result cannot borrow a captured-family result proof");
        check(rows == 21, "all nested invocation and complete-census source controls ran");
        for (const auto & [text, label, count, mask] :
             {std::tuple{program, "String", 3u, unsigned(Alternatives::String)},
              {nullBefore, "nullable", 4u, nullable}}) {
            auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
            check(static_cast<bool>(module), "nested result budget fixture parses");
            if (!module) { continue; }
            auto contract = requested(*module);
            HostContractAnalysis complete(*module, contract);
            const unsigned completion = complete.steps();
            check(complete.proved() && completion < 15000,
                  "invocation and final family proofs stay within the fixture work limit");
            if (!complete.proved() || completion >= 15000) { continue; }
            for (unsigned budget = 0; budget < completion; ++budget) {
                HostContractAnalysis limited(*module, contract, budget);
                check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                          withheld(*module, limited),
                      "incomplete invocation or final census work exposes no provisional proof");
            }
            HostContractAnalysis exact(*module, contract, completion);
            check(exact.proved() && exact.steps() == completion,
                  "the exact nested budget reproduces the completed source graph");
            if (exact.proved()) { edges(*module, exact, count, mask, 1); }
            mlir::Builder builder(&context);
            module->walk([&](mlir::Operation * operation) {
                operation->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
                operation->setAttr("ctnative.host_owner_proved", builder.getBoolAttr(true));
                operation->setAttr("ctnative.map_read_type",
                                   builder.getStringAttr("nullable_string"));
                operation->setAttr("ctnative.map_write_type", builder.getStringAttr("string"));
                operation->setAttr("ctnative.map_present", builder.getBoolAttr(true));
            });
            contract = requested(*module);
            check(HostContractAnalysis(*module, contract).proved(),
                  "forged reports leave independent nested source proofs reproducible");
            const auto refusal = [&]() {
                HostContractAnalysis stale(*module, contract);
                check(!stale.proved() && stale.reason().contains("fingerprint") &&
                          withheld(*module, stale),
                      "a changed dependency invalidates the original whole-module fingerprint");
                HostContractAnalysis fresh(*module, requested(*module));
                check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                      "fresh fingerprints and forged results cannot repair incomplete source "
                      "evidence");
            };
            const auto mutate = [&](mlir::Operation * operation, unsigned operand,
                                    mlir::Value value) {
                const auto original = operation->getOperand(operand);
                operation->setOperand(operand, value);
                refusal();
                operation->setOperand(operand, original);
                check(HostContractAnalysis(*module, contract).proved(),
                      "restoring the live dependency restores the complete nested proof");
            };
            const auto calls = exact.callables();
            auto * inner = calls[count - 3].call;
            auto * outer = calls[count - 2].call;
            const unsigned argument = prepared ? 4u : 2u;
            mutate(outer, argument, outer->getResult(0));
            mutate(inner, argument, outer->getResult(0));
            mutate(outer, argument, calls.back().call->getResult(0));
            auto finalRead = calls.back().read;
            mutate(outer, argument, finalRead.getObject());
            // Give the later call an independent String seed first. Reversing
            // this edge is acyclic and same-tag, but still violates SSA order.
            const auto nestedContract = contract;
            const auto nestedActual = outer->getOperand(argument);
            outer->setOperand(argument, inner->getOperand(argument));
            contract = requested(*module);
            HostContractAnalysis independent(*module, contract);
            check(independent.proved(), "two independent String calls retain complete host proof");
            if (independent.proved()) { edges(*module, independent, count, mask, 0); }
            mutate(inner, argument, outer->getResult(0));
            outer->setOperand(argument, nestedActual);
            contract = nestedContract;
            check(HostContractAnalysis(*module, contract).proved(),
                  "restoring source-order result consumption restores the original nested proof");
            auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
            auto getter = module->lookupSymbol<ctjs::FuncOp>("get$2");
            auto returned = llvm::cast<ctjs::ReturnOp>(setter.getBody().front().getTerminator());
            auto sibling = llvm::cast<ctjs::ReturnOp>(getter.getBody().front().getTerminator());
            mutate(returned, 0, setter.getBody().front().getArgument(0));
            mutate(sibling, 0, getter.getBody().front().getArgument(0));
            mlir::OpBuilder at(returned);
            auto effect =
                ctjs::StoreGlobalOp::create(at, returned.getLoc(), "trace", returned.getValue());
            refusal();
            effect.erase();
            check(HostContractAnalysis(*module, contract).proved(),
                  "removing a late body effect restores only the independently checked family");
            std::printf("nested %s host %s: %u rows and all %u incomplete budgets checked\n", label,
                        prepared ? "prepared" : "source", rows, completion);
        }
    }
}

} // namespace ctcompile::test::host_contract_seeded_maps
