#include "Tests.h"

namespace ctcompile::test::owned_global_shared_map {

void checkEntryNumericOwner(mlir::MLIRContext & context, const std::string & shared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    const std::string setterName = "put$4";
    const std::string getterName = "get$3";
    auto source = replaced(
        shared, "@" + setterName + "(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)",
        "@" + setterName +
            "(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
            "%entryKey: !ctjs.value)");
    source = replaced(source, "    %entryKey = ctjs.constant #ctjs.string<\"x\">\n", "");
    source = replaced(source,
                      "    %u = ctjs.constant #ctjs.undefined\n"
                      "    ctjs.return %u\n  }\n}\n",
                      "    %sizeKey = ctjs.constant #ctjs.string<\"size\">\n"
                      "    %size = ctjs.get_property %state[%sizeKey]\n"
                      "    ctjs.return %size\n  }\n}\n");
    source = replaced(source, "    %putResult = ctjs.call %putter(%owned)",
                      "    %actual = ctjs.constant #ctjs.number<4607182418800017408>\n"
                      "    %putResult = ctjs.call %putter(%owned, %actual)\n"
                      "    %secondPutter = ctjs.get_property %owned[%putKey]\n"
                      "    %secondResult = ctjs.call %secondPutter(%owned, %actual)\n"
                      "    %sum = ctjs.binary add %putResult, %secondResult\n"
                      "    %thirdPutter = ctjs.get_property %owned[%putKey]\n"
                      "    %thirdResult = ctjs.call %thirdPutter(%owned, %sum)");
    source = replaced(source, "    ctjs.store_global \"trace\", %answer",
                      "    %combined = ctjs.binary add %sum, %thirdResult\n"
                      "    ctjs.store_global \"trace\", %combined");
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        module.walk([&](ctjs::StoreGlobalOp store) {
            if (store.getName() == "savedSum") { contract.observations.push_back("savedSum"); }
        });
        return contract;
    };
    const auto withheld = [](mlir::ModuleOp module, const OwnedGlobalRoots & query) {
        return empty(module, query) && scalarReadsEmpty(module, query);
    };
    for (const bool prepared : {false, true}) {
        auto program = source;
        if (prepared) {
            for (const auto & name : {getterName, setterName}) {
                program = replaced(program, "captures %cell", "captures %state");
                const auto header =
                    "@" + name + "(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value";
                program = replaced(program, header, header + ", %state: !ctjs.value");
                program = replaced(program,
                                   "attributes {upvalue_count = 1 : i32} {\n"
                                   "    %state = ctjs.load_upvalue %callee[0]\n",
                                   "attributes {upvalue_count = 0 : i32} {\n");
            }
            for (const auto & [name, result, actual] :
                 {std::tuple{"putter", "putResult", ", %actual"},
                  {"secondPutter", "secondResult", ", %actual"},
                  {"thirdPutter", "thirdResult", ", %sum"},
                  {"getter", "answer", ""}}) {
                const auto call =
                    std::string("%") + result + " = ctjs.call %" + name + "(%owned" + actual + ")";
                const auto direct = std::string("%") + name + "Env = ctjs.load_upvalue %" + name +
                                    "[0]\n    %" + result + " = ctjs.call_direct @" +
                                    (std::string(name) == "getter" ? getterName : setterName) +
                                    "(%owned, %u, %" + name + ", %" + name + "Env" + actual + ")";
                program = replaced(program, call, direct);
            }
        }
        const auto invoke = [&](const std::string & name, const std::string & actual) {
            std::string text = "    %" + name + "Putter = ctjs.get_property %owned[%putKey]\n";
            if (prepared) {
                text += "    %" + name + "Env = ctjs.load_upvalue %" + name + "Putter[0]\n";
            }
            return text + "    %" + name + "Result = " +
                   (prepared ? "ctjs.call_direct @" + setterName + "(%owned, %u, %" + name +
                                   "Putter, %" + name + "Env, "
                             : "ctjs.call %" + name + "Putter(%owned, ") +
                   actual + ")\n";
        };
        const std::string sum = "    %sum = ctjs.binary add %putResult, %secondResult\n";
        const std::string thirdActual = prepared ? "%thirdPutterEnv, %sum)" : "%owned, %sum)";
        const std::string first = prepared ? "    %putterEnv =" : "    %putResult =";
        const std::string last = prepared ? "    %getterEnv =" : "    %answer =";
        auto saved = replaced(program, sum,
                              sum + "    ctjs.store_global \"savedSum\", %sum\n"
                                    "    %savedSum = ctjs.load_global \"savedSum\"\n");
        saved = replaced(saved, thirdActual,
                         prepared ? "%thirdPutterEnv, %savedSum)" : "%owned, %savedSum)");
        checkSavedScalarReads(context, saved, prepared);
        const auto census = [&](mlir::ModuleOp module, const OwnedGlobalRoots & query,
                                unsigned count) {
            check(query.roots().size() == 1 && query.roots().front().methodTable &&
                      query.roots().front().methodTable->capturedMap,
                  "numeric results publish exactly one complete method-table owner");
            if (query.roots().size() != 1 || !query.roots().front().methodTable ||
                !query.roots().front().methodTable->capturedMap) {
                return;
            }
            const auto & table = *query.roots().front().methodTable;
            const auto & edges = table.calls;
            check(table.methods.size() == 2 && table.capturedMap->parameters.size() == 2,
                  "the owner retains both checked source methods and their final census");
            const auto numbers = Alternatives::forTag(mlir::TypeID::get<ctjs::NumberAttr>());
            auto setter = module.lookupSymbol<ctjs::FuncOp>(setterName);
            auto getter = module.lookupSymbol<ctjs::FuncOp>(getterName);
            unsigned setters = 0;
            mlir::Operation * previous = nullptr;
            bool complete = edges.size() == count;
            for (const auto & edge : edges) {
                complete &= edge.capturedMap && (!previous || previous->isBeforeInBlock(edge.call));
                previous = edge.call;
                if (!edge.capturedMap) { continue; }
                const auto & capture = *edge.capturedMap;
                complete &= capture.parameters.size() == 2 && capture.closures.size() == 2 &&
                            capture.reads.size() == 3 && capture.calls.size() == 1 &&
                            capture.upvalues.size() == (prepared ? 0u : 2u);
                if (edge.function == getter) {
                    complete &= edge.arguments.empty();
                    continue;
                }
                ++setters;
                complete &= edge.function == setter && edge.arguments.size() == 1;
                if (edge.arguments.size() != 1) { continue; }
                const auto & actual = edge.arguments.front();
                complete &=
                    actual.parameter == setter.getBody().front().getArgument(prepared ? 4 : 3) &&
                    actual.actual == edge.call->getOperand(prepared ? 4 : 2) &&
                    actual.alternatives == numbers;
                unsigned families = 0;
                for (const auto & parameters : capture.parameters) {
                    if (parameters.function != setter) { continue; }
                    ++families;
                    complete &= parameters.alternatives == std::vector{numbers};
                }
                complete &= families == 1;
            }
            check(complete && setters == count - 1, "numeric results retain source order, actual "
                                                    "operands and complete future categories");
        };
        unsigned rows = 0;
        const auto variant = [&](const std::string & text, bool expected, const char * message,
                                 unsigned count = 4) {
            ++rows;
            auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
            check(static_cast<bool>(module), "source/prepared entry numeric fixture parses");
            if (!module) { return; }
            auto contract = requested(*module);
            OwnedGlobalRoots query(*module, contract);
            check(query.proved() == expected && !query.exhausted() &&
                      (expected || withheld(*module, query)),
                  message);
            if (query.proved() != expected || query.exhausted()) {
                std::fprintf(stderr, "numeric owner %s row %u: %s\n",
                             prepared ? "prepared" : "source", rows, query.reason().str().c_str());
            }
            if (expected && query.proved()) { census(*module, query, count); }
            check(hostContractFingerprint(*module) == contract.moduleSha256,
                  "numeric proofs preserve every source call and arithmetic operand");
        };
        for (const char * kind : {"add", "sub", "mul", "div", "mod", "pow"}) {
            variant(
                replaced(program, "ctjs.binary add %putResult",
                         std::string("ctjs.binary ") + kind + " %putResult"),
                true,
                "Number arithmetic can feed a later invocation without evaluating its operands");
        }
        variant(saved, true,
                "a single dominating global write preserves a saved arithmetic category");
        variant(replaced(program, "ctjs.binary add %putResult, %secondResult",
                         "ctjs.binary add %putResult, %actual"),
                true, "Number result plus Number literal supplies independent operand categories");
        variant(replaced(program, thirdActual,
                         prepared ? "%thirdPutterEnv, %actual)" : "%owned, %actual)"),
                true, "ordinary entry arithmetic does not require a result-fed method argument");
        for (const bool before : {false, true}) {
            for (const auto & [definition, expected] :
                 {std::pair{"ctjs.constant #ctjs.number<0>", true},
                  {"ctjs.constant #ctjs.number<9223372036854775808>", true},
                  {"ctjs.constant #ctjs.number<9221120237041090560>", true},
                  {"ctjs.constant #ctjs.number<9218868437227405312>", true},
                  {"ctjs.constant #ctjs.boolean<true>", false},
                  {"ctjs.constant #ctjs.string<\"later\">", false},
                  {"ctjs.constant #ctjs.null", false},
                  {"ctjs.constant #ctjs.undefined", false},
                  {"ctjs.constant #ctjs.bigint<\"1\">", false},
                  {"ctjs.create_object", false},
                  {"ctjs.load_global \"unknown\"", false}}) {
                const auto & marker = before ? first : last;
                variant(replaced(program, marker,
                                 "    %future = " + std::string(definition) + "\n" +
                                     invoke("future", "%future") + marker),
                        expected,
                        "every earlier and later actual joins the whole numeric method family", 5);
            }
        }
        for (const char * definition :
             {"ctjs.constant #ctjs.string<\"1\">", "ctjs.constant #ctjs.boolean<true>",
              "ctjs.constant #ctjs.null", "ctjs.constant #ctjs.undefined",
              "ctjs.constant #ctjs.bigint<\"1\">", "ctjs.create_object",
              "ctjs.load_global \"unknown\""}) {
            const auto operand =
                replaced(program, sum, "    %operand = " + std::string(definition) + "\n" + sum);
            variant(replaced(operand, "ctjs.binary add %putResult, %secondResult",
                             "ctjs.binary add %putResult, %operand"),
                    false, "non-Number conversion cannot borrow a numeric method-result category");
        }
        variant(replaced(saved, "    %savedSum =",
                         "    ctjs.store_global \"savedSum\", %actual\n"
                         "    %savedSum ="),
                false, "multiple source writes cannot authorize a saved global category");
        for (const bool hiddenThen : {false, true}) {
            const std::string effect = "      %effect = ctjs.call %this(%owned)\n";
            const std::string branch = "    %condition = ctjs.truthy %sum\n"
                                       "    scf.if %condition {\n" +
                                       (hiddenThen ? effect : "") +
                                       "      scf.yield\n    } else {\n" +
                                       (hiddenThen ? "" : effect) + "      scf.yield\n    }\n";
            variant(replaced(program, "    %combined =", branch + "    %combined ="), false,
                    "an unknown Number category cannot select a branch and hide an invalid call");
        }
        check(rows == 41,
              "all numeric categories, saved operands and complete-census controls ran");

        const auto validScope = [&](mlir::ModuleOp module, const HostContract & contract) {
            HostContractAnalysis host(module, contract);
            OwnedGlobalRoots owner(module, contract);
            return host.proved() && !owner.proved() && !owner.exhausted() && empty(module, owner) &&
                   owner.reason() ==
                       "owned global method table requires unconditional straight-line operations";
        };
        unsigned scopeMutations = 0;
        for (const std::string selector : {"unknown", "true", "false"}) {
            for (const bool sibling : {false, true}) {
                for (const bool constant : {false, true}) {
                    const std::string condition =
                        selector == "unknown"
                            ? "    %condition = ctjs.truthy %sum\n"
                            : "    %choice = ctjs.constant #ctjs.boolean<" + std::string(selector) +
                                  ">\n"
                                  "    %condition = ctjs.truthy %choice\n";
                    const std::string branches =
                        condition + "    scf.if %condition {\n"
                                    "      %localNumber = ctjs.constant #ctjs.number<0>\n"
                                    "      %localSum = ctjs.binary add %putResult, %secondResult\n"
                                    "      scf.yield\n    } else {\n"
                                    "      %elseSum = ctjs.binary add %putResult, %secondResult\n"
                                    "      scf.yield\n    }\n";
                    auto module = mlir::parseSourceString<mlir::ModuleOp>(
                        replaced(program, "    %combined =", branches + "    %combined ="),
                        &context);
                    check(static_cast<bool>(module),
                          "numeric scope fixture parses before live mutation");
                    if (!module) { continue; }
                    mlir::Builder attributes(&context);
                    module->walk([&](mlir::Operation * operation) {
                        operation->setAttr("ctnative.host_proved", attributes.getBoolAttr(true));
                        operation->setAttr("ctnative.host_owner_proved",
                                           attributes.getBoolAttr(true));
                        operation->setAttr("ctnative.map_read_type",
                                           attributes.getStringAttr("number"));
                    });
                    auto contract = requested(*module);
                    check(validScope(*module, contract),
                          "proper numeric scopes prove host categories and retain the owner "
                          "entry-SCF boundary");
                    auto entry = module->lookupSymbol<ctjs::FuncOp>("script$0");
                    mlir::scf::IfOp branch;
                    ctjs::BinaryOp combined;
                    for (auto & operation : entry.getBody().front()) {
                        if (auto found = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                            branch = found;
                        }
                        if (auto found = llvm::dyn_cast<ctjs::BinaryOp>(operation)) {
                            combined = found;
                        }
                    }
                    check(branch && combined,
                          "numeric scope fixture retains branch and outer addition");
                    if (!branch || !combined) { continue; }
                    auto & thenBody = branch.getThenRegion().front();
                    auto local = constant ? thenBody.front().getResult(0)
                                          : std::next(thenBody.begin())->getResult(0);
                    mlir::Operation * use =
                        sibling ? &branch.getElseRegion().front().front() : combined.getOperation();
                    const auto original = use->getOperand(0);
                    use->setOperand(0, local);
                    OwnedGlobalRoots stale(*module, contract);
                    check(
                        !stale.proved() && stale.reason().contains("fingerprint") &&
                            withheld(*module, stale),
                        "a live cross-scope arithmetic operand invalidates its prior fingerprint");
                    OwnedGlobalRoots fresh(*module, requested(*module));
                    HostContractAnalysis freshHost(*module, requested(*module));
                    check(
                        !freshHost.proved() && !freshHost.exhausted() &&
                            freshHost.callables().empty() && !fresh.proved() &&
                            !fresh.exhausted() && withheld(*module, fresh) &&
                            fresh.reason() != "owned global method table requires unconditional "
                                              "straight-line operations",
                        "fresh forged reports cannot authorize a then-only constant or arithmetic "
                        "result");
                    use->setOperand(0, original);
                    check(validScope(*module, contract),
                          "restored numeric scope repairs the host proof and restores the exact "
                          "owner boundary");
                    ++scopeMutations;
                }
            }
        }
        // An scf.if result owns a value only through correctly scoped yield
        // operands. Constant source conditions cannot authorize a sibling's SSA
        // definition, including when that malformed arm would be inactive.
        for (const char * selector : {"true", "false"}) {
            for (const bool mutateElse : {false, true}) {
                const std::string selection =
                    "    %choice = ctjs.constant #ctjs.boolean<" + std::string(selector) +
                    ">\n"
                    "    %condition = ctjs.truthy %choice\n"
                    "    %selected = scf.if %condition -> (!ctjs.value) {\n"
                    "      %thenNumber = ctjs.constant #ctjs.number<0>\n"
                    "      scf.yield %thenNumber : !ctjs.value\n"
                    "    } else {\n"
                    "      %elseNumber = ctjs.constant #ctjs.number<4607182418800017408>\n"
                    "      scf.yield %elseNumber : !ctjs.value\n"
                    "    }\n"
                    "    %combined = ctjs.binary add %selected, %thirdResult";
                auto module = mlir::parseSourceString<mlir::ModuleOp>(
                    replaced(program, "    %combined = ctjs.binary add %sum, %thirdResult",
                             selection),
                    &context);
                check(static_cast<bool>(module),
                      "selected numeric yield fixture parses before mutation");
                if (!module) { continue; }
                mlir::Builder attributes(&context);
                module->walk([&](mlir::Operation * operation) {
                    operation->setAttr("ctnative.host_proved", attributes.getBoolAttr(true));
                    operation->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
                    operation->setAttr("ctnative.inferred_result",
                                       attributes.getStringAttr("number"));
                });
                auto contract = requested(*module);
                check(validScope(*module, contract),
                      "valid numeric yields prove host categories and retain the exact owner "
                      "entry-SCF boundary");
                auto entry = module->lookupSymbol<ctjs::FuncOp>("script$0");
                mlir::scf::IfOp branch;
                for (auto & operation : entry.getBody().front()) {
                    if (auto found = llvm::dyn_cast<mlir::scf::IfOp>(operation)) { branch = found; }
                }
                check(static_cast<bool>(branch), "selected result keeps its original conditional");
                if (!branch) { continue; }
                auto & target =
                    (mutateElse ? branch.getElseRegion() : branch.getThenRegion()).front();
                auto & sibling =
                    (mutateElse ? branch.getThenRegion() : branch.getElseRegion()).front();
                auto * yield = target.getTerminator();
                const auto original = yield->getOperand(0);
                yield->setOperand(0, sibling.front().getResult(0));
                OwnedGlobalRoots stale(*module, contract);
                check(!stale.proved() && stale.reason().contains("fingerprint") &&
                          withheld(*module, stale),
                      "a cross-arm yield invalidates its original fingerprint");
                OwnedGlobalRoots fresh(*module, requested(*module));
                HostContractAnalysis freshHost(*module, requested(*module));
                check(!freshHost.proved() && !freshHost.exhausted() &&
                          freshHost.callables().empty() && !fresh.proved() && !fresh.exhausted() &&
                          withheld(*module, fresh) &&
                          fresh.reason() != "owned global method table requires unconditional "
                                            "straight-line operations",
                      "constant conditions and fresh Number reports cannot authorize an invalid "
                      "yield");
                yield->setOperand(0, original);
                check(validScope(*module, contract),
                      "restoring the original yield repairs the host proof and restores the exact "
                      "owner boundary");
                ++scopeMutations;
                // Clone the valid i1 producer into the then arm. It cannot
                // become the enclosing conditional's own condition, even when
                // its operand still denotes the same constant Boolean.
                const auto originalCondition = branch.getCondition();
                mlir::OpBuilder builder(branch.getThenRegion().front().getTerminator());
                auto * localCondition = builder.clone(*originalCondition.getDefiningOp());
                const auto conditionContract = requested(*module);
                check(validScope(*module, conditionContract),
                      "an unused then-local predicate preserves the original source proof");
                branch->setOperand(0, localCondition->getResult(0));
                OwnedGlobalRoots staleCondition(*module, conditionContract);
                check(!staleCondition.proved() && staleCondition.reason().contains("fingerprint") &&
                          withheld(*module, staleCondition),
                      "a then-only conditional predicate invalidates its prior fingerprint");
                OwnedGlobalRoots freshCondition(*module, requested(*module));
                HostContractAnalysis freshConditionHost(*module, requested(*module));
                check(!freshConditionHost.proved() && !freshConditionHost.exhausted() &&
                          freshConditionHost.callables().empty() && !freshCondition.proved() &&
                          !freshCondition.exhausted() && withheld(*module, freshCondition) &&
                          freshCondition.reason() != "owned global method table requires "
                                                     "unconditional straight-line operations",
                      "a fresh Number report cannot authorize a conditional's then-only predicate");
                branch->setOperand(0, originalCondition);
                check(
                    validScope(*module, conditionContract),
                    "restoring the exact original predicate restores the independent source proof");
                localCondition->erase();
                check(
                    validScope(*module, contract),
                    "removing the temporary predicate restores the original complete fingerprint");
                ++scopeMutations;
            }
        }
        check(scopeMutations == 20,
              "all known/unknown numeric scopes, yield directions and predicate scopes ran");
        for (const auto & [text, label] : {std::pair{program, "direct"}, {saved, "saved"}}) {
            auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
            check(static_cast<bool>(module), "numeric budget fixture parses");
            if (!module) { continue; }
            auto contract = requested(*module);
            OwnedGlobalRoots complete(*module, contract);
            const unsigned completion = complete.steps();
            check(complete.proved() && completion < 18000,
                  "numeric dependency proof is bounded and complete");
            if (!complete.proved() || completion >= 18000) { continue; }
            for (unsigned budget = 0; budget < completion; ++budget) {
                OwnedGlobalRoots limited(*module, contract, budget);
                check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                          withheld(*module, limited),
                      "every incomplete numeric budget withholds the entire callable family");
            }
            OwnedGlobalRoots exact(*module, contract, completion);
            check(exact.proved() && exact.steps() == completion,
                  "the exact numeric budget reproduces every source dependency");
            if (exact.proved()) { census(*module, exact, 4); }
            mlir::Builder attributes(&context);
            module->walk([&](mlir::Operation * operation) {
                operation->setAttr("ctnative.host_proved", attributes.getBoolAttr(true));
                operation->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
                operation->setAttr("ctnative.map_read_type", attributes.getStringAttr("number"));
                operation->setAttr("ctnative.inferred_result", attributes.getStringAttr("number"));
            });
            contract = requested(*module);
            OwnedGlobalRoots forged(*module, contract);
            check(forged.proved(),
                  "forged reports leave the independent numeric proof reproducible");
            if (!forged.proved()) { continue; }
            const auto edges = forged.roots().front().methodTable->calls;
            auto * firstCall = edges[0].call;
            auto * thirdCall = edges[2].call;
            auto * lastCall = edges[3].call;
            auto entry = module->lookupSymbol<ctjs::FuncOp>("script$0");
            ctjs::BinaryOp addition, combined;
            ctjs::StoreGlobalOp savedStore;
            ctjs::LoadGlobalOp savedLoad;
            for (auto & operation : entry.getBody().front()) {
                if (auto found = llvm::dyn_cast<ctjs::BinaryOp>(operation)) {
                    if (!addition) { addition = found; }
                    combined = found;
                }
                if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation);
                    store && store.getName() == "savedSum") {
                    savedStore = store;
                }
                if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation);
                    load && load.getName() == "savedSum") {
                    savedLoad = load;
                }
            }
            check(static_cast<bool>(addition),
                  "numeric mutations retain the original source addition");
            if (!addition) { continue; }
            unsigned mutations = 0;
            const auto refusal = [&]() {
                OwnedGlobalRoots stale(*module, contract);
                check(!stale.proved() && stale.reason().contains("fingerprint") &&
                          withheld(*module, stale),
                      "changing numeric dependency evidence invalidates its old fingerprint");
                OwnedGlobalRoots fresh(*module, requested(*module));
                check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                      "fresh fingerprints and forged Number reports cannot repair invalid live "
                      "operands");
                ++mutations;
            };
            const auto mutation = [&](mlir::Operation * operation, unsigned operand,
                                      mlir::Value value) {
                const auto old = operation->getOperand(operand);
                operation->setOperand(operand, value);
                refusal();
                operation->setOperand(operand, old);
                check(OwnedGlobalRoots(*module, contract).proved(),
                      "restoring a numeric dependency restores the whole proof");
            };
            mutation(combined, 0, entry.getBody().front().getArgument(0));
            mutation(addition, 0, addition.getResult());
            mutation(addition, 1, lastCall->getResult(0));
            mutation(addition, 0, entry.getBody().front().getArgument(0));
            mutation(firstCall, prepared ? 4u : 2u, addition.getResult());
            mutation(thirdCall, prepared ? 4u : 2u, lastCall->getResult(0));
            mutation(thirdCall, prepared ? 0u : 1u, entry.getBody().front().getArgument(0));
            auto lastRead = edges[3].read;
            mutation(thirdCall, prepared ? 2u : 0u, lastRead.getResult());
            if (prepared) { mutation(thirdCall, 3, firstCall->getOperand(3)); }
            addition.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Concat));
            refusal();
            addition.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Add));
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring the live numeric operator restores its independent result proof");
            auto setter = module->lookupSymbol<ctjs::FuncOp>(setterName);
            auto getter = module->lookupSymbol<ctjs::FuncOp>(getterName);
            mutation(setter.getBody().front().getTerminator(), 0,
                     setter.getBody().front().getArgument(0));
            mutation(getter.getBody().front().getTerminator(), 0,
                     getter.getBody().front().getArgument(0));
            if (savedStore && savedLoad) {
                mutation(savedStore, 0, lastCall->getResult(0));
                savedStore->moveAfter(savedLoad);
                refusal();
                savedStore->moveBefore(savedLoad);
                check(OwnedGlobalRoots(*module, contract).proved(),
                      "restoring store-before-load order restores the saved category");
            }
            std::printf("numeric owner %s %s: %u rows, %u scope controls, %u live edits, all %u "
                        "budgets checked\n",
                        prepared ? "prepared" : "source", label, rows, scopeMutations, mutations,
                        completion);
        }
    }
}

} // namespace ctcompile::test::owned_global_shared_map
