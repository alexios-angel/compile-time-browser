#include "Tests.h"

namespace ctcompile::test::host_contract_seeded_maps {

void checkLeafObjectPayloads(mlir::MLIRContext & context, const std::string & shared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    auto source =
        replaced(shared, "@put$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)",
                 "@put$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                 "%entryKey: !ctjs.value)");
    source = replaced(source, "    %entryKey = ctjs.constant #ctjs.string<\"x\">\n", "");
    source = replaced(source, "%value = ctjs.constant #ctjs.number<4607182418800017408>",
                      "%value = ctjs.create_object");
    source = replaced(source,
                      "    %u = ctjs.constant #ctjs.undefined\n"
                      "    ctjs.return %u\n  }\n}\n",
                      "    %sizeKey = ctjs.constant #ctjs.string<\"size\">\n"
                      "    %size = ctjs.get_property %state[%sizeKey]\n"
                      "    ctjs.return %size\n  }\n}\n");
    source = replaced(source, "    %putResult = ctjs.call %putter(%owned)",
                      "    %actual = ctjs.constant #ctjs.string<\"x\">\n"
                      "    %putResult = ctjs.call %putter(%owned, %actual)\n"
                      "    %repeatPutter = ctjs.get_property %owned[%putKey]\n"
                      "    %repeatResult = ctjs.call %repeatPutter(%owned, %actual)\n"
                      "    %future = ctjs.constant #ctjs.string<\"y\">\n"
                      "    %laterPutter = ctjs.get_property %owned[%putKey]\n"
                      "    %laterResult = ctjs.call %laterPutter(%owned, %future)");
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto withheld = [](mlir::ModuleOp module, const HostContractAnalysis & query) {
        bool none = query.callables().empty();
        module.walk([&](mlir::Operation * operation) {
            none &= !query.callable(operation);
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                none &= !query.property(read);
            }
        });
        return none;
    };
    const std::string field = "    %fieldKey = ctjs.constant #ctjs.string<\"value\">\n"
                              "    %fieldValue = ctjs.constant #ctjs.number<4607182418800017408>\n"
                              "    ctjs.set_property %value[%fieldKey], %fieldValue\n";
    const std::string write = "    %written = ctjs.call %setter(%state, %entryKey, %value)";
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
                  {"repeatPutter", "repeatResult", ", %actual"},
                  {"laterPutter", "laterResult", ", %future"},
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
        const auto edges = [&](mlir::ModuleOp module, const HostContractAnalysis & query) {
            auto setter = module.lookupSymbol<ctjs::FuncOp>("put$3");
            const auto expected = Alternatives::forTag(mlir::TypeID::get<ctjs::StringAttr>());
            std::vector<ctjs::CreateObjectOp> leafObjects;
            std::vector<ctjs::SetPropertyOp> leafWrites;
            std::vector<ctjs::GetPropertyOp> leafReads;
            setter.walk([&](ctjs::CreateObjectOp operation) { leafObjects.push_back(operation); });
            setter.walk([&](ctjs::SetPropertyOp operation) { leafWrites.push_back(operation); });
            setter.walk([&](ctjs::GetPropertyOp operation) {
                if (operation.getObject().getDefiningOp<ctjs::CreateObjectOp>()) {
                    leafReads.push_back(operation);
                }
            });
            bool complete = query.callables().size() == 4 && leafObjects.size() == 1;
            unsigned setters = 0;
            mlir::Operation * previous = nullptr;
            for (const auto & edge : query.callables()) {
                complete &= edge.capturedMap && (!previous || previous->isBeforeInBlock(edge.call));
                previous = edge.call;
                if (!edge.capturedMap) { continue; }
                const auto & capture = *edge.capturedMap;
                complete &= capture.closures.size() == 2 && capture.parameters.size() == 2 &&
                            capture.upvalues.size() == (prepared ? 0u : 2u) &&
                            capture.leafObjects == leafObjects &&
                            capture.leafWrites == leafWrites && capture.leafReads == leafReads;
                if (edge.function != setter) {
                    complete &= edge.arguments.empty();
                    continue;
                }
                ++setters;
                complete &= edge.arguments.size() == 1;
                if (edge.arguments.size() != 1) { continue; }
                const auto & argument = edge.arguments.front();
                complete &=
                    argument.alternatives == expected &&
                    argument.actual == edge.call->getOperand(prepared ? 4 : 2) &&
                    argument.parameter == setter.getBody().front().getArgument(prepared ? 4 : 3);
                unsigned summaries = 0;
                for (const auto & family : capture.parameters) {
                    if (family.function != setter) { continue; }
                    ++summaries;
                    complete &= family.alternatives == std::vector{expected};
                }
                complete &= summaries == 1;
            }
            check(
                complete && setters == 3,
                "repeated leaf allocations keep every call and a future String parameter category");
            check(complete, "every edge records each actual local leaf allocation and field write "
                            "exactly once");
        };
        unsigned rows = 0;
        const auto variant = [&](const std::string & text, bool expected, const char * message,
                                 bool localLeaf = true) {
            ++rows;
            auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
            check(static_cast<bool>(module), "source/prepared leaf Map fixture parses");
            if (!module) { return; }
            const auto contract = requested(*module);
            HostContractAnalysis query(*module, contract);
            check(query.proved() == expected && !query.exhausted() &&
                      (expected || withheld(*module, query)),
                  message);
            if (query.proved() != expected || query.exhausted()) {
                std::fprintf(stderr, "leaf Map host %s case %u: %s\n",
                             prepared ? "prepared" : "source", rows, query.reason().str().c_str());
            }
            if (expected && query.proved()) {
                if (localLeaf) {
                    edges(*module, query);
                } else {
                    bool complete = query.callables().size() == 4;
                    for (const auto & edge : query.callables()) {
                        complete &= edge.capturedMap && edge.capturedMap->leafObjects.empty() &&
                                    edge.capturedMap->leafReads.empty() &&
                                    edge.capturedMap->leafWrites.empty();
                    }
                    check(complete, "caller payloads grant no method-local object or field facts");
                }
            }
            check(hostContractFingerprint(*module) == contract.moduleSha256,
                  "leaf proof never hoists an allocation or substitutes a startup observation");
        };
        variant(program, true, "a method-local empty leaf can be owned by the captured Map");
        const auto numeric = replaced(program, write, field + write);
        for (const char * value : {"#ctjs.number<4607182418800017408>", "#ctjs.boolean<true>",
                                   "#ctjs.null", "#ctjs.undefined"}) {
            variant(replaced(numeric,
                             "%fieldValue = ctjs.constant #ctjs.number<4607182418800017408>",
                             std::string("%fieldValue = ctjs.constant ") + value),
                    true, "plain own fields accept independently proved fixed scalar values");
        }
        variant(replaced(numeric, write,
                         "    ctjs.set_property %value[%fieldKey], %fieldValue\n" + write),
                true, "repeated primitive own writes retain a closed leaf object");
        for (const char * value : {"%this", "%entryKey", "%state", "%value"}) {
            variant(replaced(numeric, "ctjs.set_property %value[%fieldKey], %fieldValue",
                             std::string("ctjs.set_property %value[%fieldKey], ") + value),
                    llvm::StringRef(value) == "%entryKey",
                    "only the independently proved future String formal supplies a field value");
        }
        variant(replaced(numeric, "#ctjs.string<\"value\">", "#ctjs.string<\"__proto__\">"), false,
                "a prototype setter key is not an ordinary own field");
        variant(replaced(numeric, "ctjs.set_property %value[%fieldKey], %fieldValue",
                         "ctjs.set_property %value[%entryKey], %fieldValue"),
                false, "a future String key cannot be treated as a fixed own field");
        variant(replaced(program, "%value = ctjs.create_object",
                         "%value = ctjs.load_global \"external\""),
                false, "external objects are not fresh method-local leaf allocations");
        variant(replaced(program, write,
                         "    %other = ctjs.create_object\n"
                         "    ctjs.copy_props %other into %value\n" +
                             write),
                false, "own-property copying requires a separate getter-aware proof");
        variant(replaced(program, write,
                         "    %null = ctjs.constant #ctjs.null\n"
                         "    ctjs.set_proto %null on %value\n" +
                             write),
                false, "even a literal prototype mutation is outside the leaf ownership proof");
        variant(replaced(program, write,
                         "    %u = ctjs.constant #ctjs.undefined\n"
                         "    ctjs.define_accessor \"value\" on %value get %callee set %u\n" +
                             write),
                false, "an accessor may not hide behind a fresh allocation or scalar return");
        variant(replaced(program, "    ctjs.return %size", "    ctjs.return %value"), false,
                "an escaping object result requires its own identity and retention proof");
        variant(replaced(numeric, "    ctjs.return %size",
                         "    %loaded = ctjs.get_property %value[%fieldKey]\n"
                         "    ctjs.return %loaded"),
                true, "an independently initialized scalar own field can now be read locally");
        variant(replaced(program, "%setter(%state, %entryKey, %value)",
                         "%setter(%state, %value, %value)"),
                false, "object Map keys are outside primitive-key leaf ownership");
        variant(replaced(program, write, "    %flag = ctjs.truthy %value\n" + write), false,
                "object truthiness is not authorized by its value-storage proof");
        variant(replaced(program, write, "    ctjs.store_global \"escaped\", %value\n" + write),
                false, "publication of the same leaf is not hidden by later Map storage");
        variant(replaced(program, write,
                         "    %flag = ctjs.truthy %entryKey\n"
                         "    %selected = scf.if %flag -> (!ctjs.value) {\n"
                         "      scf.yield %value : !ctjs.value\n"
                         "    } else {\n"
                         "      scf.yield %value : !ctjs.value\n"
                         "    }\n"
                         "    %written = ctjs.call %setter(%state, %entryKey, %selected)"),
                false, "structured object aliases require their own closed identity proof");
        auto formal = replaced(program, "%entryKey: !ctjs.value)",
                               "%entryKey: !ctjs.value, %payload: !ctjs.value)");
        for (const char * argument : {"%actual)", "%actual)", "%future)"}) {
            formal = replaced(formal, argument,
                              std::string(argument).substr(0, std::string(argument).size() - 1) +
                                  ", %owned)");
        }
        formal = replaced(formal, "%setter(%state, %entryKey, %value)",
                          "%setter(%state, %entryKey, %payload)");
        variant(formal, false, "a host object actual cannot become a proved local leaf formal");

        // This getter has no local CreateObject. The complete sibling census
        // must still prevent its unknown payload from becoming a primitive.
        const std::string sizeRead = "    %key = ctjs.constant #ctjs.string<\"size\">\n"
                                     "    %answer = ctjs.get_property %state[%key]";
        const std::string payloadRead = "    %key = ctjs.constant #ctjs.string<\"get\">\n"
                                        "    %reader = ctjs.get_property %state[%key]\n"
                                        "    %probeKey = ctjs.constant #ctjs.string<\"x\">\n"
                                        "    %answer = ctjs.call %reader(%state, %probeKey)";
        const std::string reseed = "    %setName = ctjs.constant #ctjs.string<\"set\">\n"
                                   "    %seedSetter = ctjs.get_property %state[%setName]\n"
                                   "    %seed = ctjs.constant #ctjs.number<4607182418800017408>\n"
                                   "    %seeded = ctjs.call %seedSetter(%state, %probeKey, %seed)\n"
                                   "    %answer = ctjs.call %reader";
        const auto getter = replaced(program, sizeRead, payloadRead);
        variant(getter, false,
                "an object-writing sibling vetoes an unknown supposedly primitive get");
        variant(replaced(getter, "    %answer = ctjs.call %reader", reseed), true,
                "a live exact primitive reseed proves its read despite object-writing siblings");
        check(rows == 25, "every leaf allocation, scalar field and escaping-use control ran");

        // Remove the unused method-local allocation: the caller formal alone
        // must close primitive contents before any sibling return is checked.
        auto caller = replaced(formal, "    %value = ctjs.create_object\n", "");
        caller = replaced(
            caller, "    %actual = ", "    %payloadActual = ctjs.create_object\n    %actual = ");
        for (unsigned index = 0; index != 3; ++index) {
            caller = replaced(caller, ", %owned)", ", %payloadActual)");
        }
        for (const bool named : {false, true}) {
            auto actual = caller;
            if (named) {
                actual = replaced(actual, "    %payloadActual = ctjs.create_object",
                                  "    %payloadObject = ctjs.create_object\n"
                                  "    ctjs.store_global \"payload\", %payloadObject\n"
                                  "    %payloadActual = ctjs.load_global \"payload\"");
            }
            for (const bool writerFirst : {false, true}) {
                auto ordered = actual;
                if (writerFirst) {
                    const std::string creation =
                        "    %putter = ctjs.create_closure %callee[3] this %u captures " +
                        std::string(prepared ? "%state\n" : "%cell\n");
                    ordered = replaced(ordered, creation, "");
                    ordered = replaced(ordered, "    %getter = ctjs.create_closure",
                                       creation + "    %getter = ctjs.create_closure");
                } else {
                    const std::string invocation =
                        prepared ? "    %getterEnv = ctjs.load_upvalue %getter[0]\n"
                                   "    %answer = ctjs.call_direct @get$2(%owned, %u, %getter, "
                                   "%getterEnv)\n"
                                 : "    %answer = ctjs.call %getter(%owned)\n";
                    ordered = replaced(ordered, invocation, "");
                    ordered = replaced(ordered, "    %putKey = ctjs.constant",
                                       invocation + "    %putKey = ctjs.constant");
                }
                variant(ordered, true, "a caller-owned empty leaf can be a scalar-key Map payload",
                        false);
                const auto unknown = replaced(ordered, sizeRead, payloadRead);
                variant(unknown, false,
                        "caller payload siblings veto an unseeded get in either method order",
                        false);
                variant(replaced(unknown, "    %answer = ctjs.call %reader", reseed), true,
                        "an exact scalar reseed survives caller payload siblings in either order",
                        false);
                variant(replaced(unknown, "%setter(%state, %entryKey, %payload)",
                                 "%setter(%state, %payload, %entryKey)"),
                        true, "object keys alone keep the primitive-only contents guarantee",
                        false);
                const std::string origin = named ? "%payloadObject" : "%payloadActual";
                variant(replaced(unknown, origin + " = ctjs.create_object",
                                 origin + " = ctjs.constant #ctjs.number<4607182418800017408>"),
                        true, "scalar payload formals keep primitive-only contents", false);
            }
        }
        for (const char * use : {"    %field = ctjs.get_property %payload[%entryKey]\n",
                                 "    ctjs.set_property %payload[%entryKey], %entryKey\n",
                                 "    ctjs.set_property %payload[%entryKey], %payload\n",
                                 "    ctjs.set_property %payload[%entryKey], %state\n",
                                 "    ctjs.store_global \"escaped\", %payload\n"}) {
            variant(replaced(caller, "    ctjs.return %size",
                             std::string(use) + "    ctjs.return %size"),
                    false, "caller payload storage grants no fields, cycles or foreign uses",
                    false);
        }
        variant(replaced(caller, "    ctjs.return %size", "    ctjs.return %payload"), false,
                "a caller-owned payload cannot escape as a direct method result", false);
        variant(replaced(caller, "    ctjs.return %size",
                         "    %getName = ctjs.constant #ctjs.string<\"get\">\n"
                         "    %reader = ctjs.get_property %state[%getName]\n"
                         "    %loaded = ctjs.call %reader(%state, %entryKey)\n"
                         "    ctjs.return %loaded"),
                false, "storing a caller payload does not authorize its Map.get-derived return",
                false);
        check(rows == 52, "every local and caller payload family control ran");
        checkLeafReadbacks(context, program, prepared);
        checkDefiniteMapAbsence(context, program, prepared);
        checkCapturedMapClear(context, program, prepared);

        for (const auto & [text, label] :
             {std::pair{program, "empty"}, {numeric, "number field"}}) {
            auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
            check(static_cast<bool>(module), "leaf object budget fixture parses");
            if (!module) { continue; }
            auto contract = requested(*module);
            HostContractAnalysis query(*module, contract);
            const unsigned completion = query.steps();
            check(query.proved() && completion < 15000,
                  "leaf family proof has bounded complete work");
            if (!query.proved() || completion >= 15000) { continue; }
            for (unsigned budget = 0; budget < completion; ++budget) {
                HostContractAnalysis limited(*module, contract, budget);
                check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                          withheld(*module, limited),
                      "incomplete leaf proof never publishes provisional object or callable "
                      "evidence");
            }
            HostContractAnalysis exact(*module, contract, completion);
            check(exact.proved() && exact.steps() == completion,
                  "exact leaf completion budget reproduces the complete family");
            auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
            ctjs::CreateObjectOp leaf;
            ctjs::CallOp store;
            ctjs::SetPropertyOp fieldWrite;
            setter.walk([&](ctjs::CreateObjectOp operation) { leaf = operation; });
            setter.walk([&](ctjs::CallOp operation) { store = operation; });
            setter.walk([&](ctjs::SetPropertyOp operation) { fieldWrite = operation; });
            check(leaf && store, "the checked object allocation remains inside the setter body");
            if (!leaf || !store) { continue; }
            mlir::Builder attributes(&context);
            module->walk([&](mlir::Operation * operation) {
                operation->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
                operation->setAttr("ctnative.host_proved", attributes.getBoolAttr(true));
                operation->setAttr("ctnative.map_present", attributes.getBoolAttr(true));
                operation->setAttr("ctnative.map_read_type", attributes.getStringAttr("number"));
                operation->setAttr("ctnative.map_write_type", attributes.getStringAttr("number"));
                operation->setAttr("ctnative.object_schema", attributes.getStringAttr("leaf"));
            });
            contract = requested(*module);
            check(HostContractAnalysis(*module, contract).proved(),
                  "forged reports leave the real leaf proof independently reproducible");
            const auto mutation = [&](mlir::Operation * operation, unsigned operand,
                                      mlir::Value value) {
                const auto saved = operation->getOperand(operand);
                operation->setOperand(operand, value);
                HostContractAnalysis stale(*module, contract);
                check(!stale.proved() && stale.reason().contains("fingerprint") &&
                          withheld(*module, stale),
                      "a changed object-use edge invalidates its prior fingerprint");
                HostContractAnalysis fresh(*module, requested(*module));
                check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                      "fresh fingerprints and forged schemas cannot repair an unsafe leaf use");
                operation->setOperand(operand, saved);
                check(HostContractAnalysis(*module, contract).proved(),
                      "restoring the actual source use restores the independent leaf proof");
            };
            mutation(store, 2, leaf.getResult());
            mutation(store, 3, setter.getBody().front().getArgument(0));
            auto returned = llvm::cast<ctjs::ReturnOp>(setter.getBody().front().getTerminator());
            mutation(returned, 0, leaf.getResult());
            if (fieldWrite) {
                mutation(fieldWrite, 0, setter.getBody().front().getArgument(0));
                mutation(fieldWrite, 1, setter.getBody().front().getArgument(prepared ? 4 : 3));
                mutation(fieldWrite, 2, leaf.getResult());
                auto literal = fieldWrite.getValue().getDefiningOp<ctjs::ConstantOp>();
                check(static_cast<bool>(literal), "the field has an independent source literal");
                if (literal) {
                    const auto saved = literal.getValue();
                    literal->setAttr("value", ctjs::BooleanAttr::get(&context, false));
                    HostContractAnalysis stale(*module, contract);
                    check(!stale.proved() && stale.reason().contains("fingerprint") &&
                              withheld(*module, stale),
                          "even a safe scalar field mutation invalidates the old fingerprint");
                    check(HostContractAnalysis(*module, requested(*module)).proved(),
                          "a new fingerprint rederives a changed safe primitive field");
                    literal->setAttr("value", ctjs::StringAttr::get(&context, "unsupported"));
                    HostContractAnalysis staleString(*module, contract);
                    check(!staleString.proved() && staleString.reason().contains("fingerprint") &&
                              withheld(*module, staleString),
                          "a changed String literal still invalidates the old fingerprint");
                    HostContractAnalysis string(*module, requested(*module));
                    check(string.proved() && !string.exhausted(),
                          "String field ownership rederives its source without a Number schema");
                    if (string.proved()) { edges(*module, string); }
                    literal->setAttr("value", saved);
                    check(HostContractAnalysis(*module, contract).proved(),
                          "restoring the field literal restores the original independent proof");
                }
            }
            std::printf("leaf Map host %s %s: %u rows and all %u incomplete budgets checked\n",
                        prepared ? "prepared" : "source", label, rows, completion);
        }
    }
}

} // namespace ctcompile::test::host_contract_seeded_maps
