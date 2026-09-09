// Owned global method tables: the shared two- and three-method Map family -
// source and prepared forms, parameterized siblings, result-fed actuals, the
// seeded/per-key/alias-join variants, and every refusal control.
//
// SPLIT 2026-09-08: this is checkSharedMap, verbatim, out of a 1,099-line
// test/OwnedGlobalMethods.cpp; the fixtures it reads are in
// OwnedGlobalMethodsFixtures.h beside this.

#include "OwnedGlobalMethodsFixtures.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

using namespace ctcompile::test::owned_global_methods;

namespace {

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
        return empty(module, query);
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

void checkCapturedMapClearOwner(mlir::MLIRContext & context, const std::string & source,
                                bool prepared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto withheld = [](mlir::ModuleOp module, const OwnedGlobalRoots & query) {
        return empty(module, query);
    };
    auto fixture =
        replaced(source, "%entryKey: !ctjs.value)",
                 "%entryKey: !ctjs.value, %aliasKey: !ctjs.value, %choice: !ctjs.value)");
    fixture = replaced(fixture, "    %actual =",
                       "    %other = ctjs.constant #ctjs.string<\"z\">\n"
                       "    %choice = ctjs.constant #ctjs.boolean<true>\n"
                       "    %actual =");
    for (const auto & [name, actual] :
         {std::pair{"putterEnv", "actual"}, {"repeatEnv", "actual"}, {"laterEnv", "future"}}) {
        const std::string prefix = prepared ? std::string("%") + name + ", " : "%owned, ";
        fixture = replaced(fixture, prefix + "%" + actual + ")",
                           prefix + "%" + actual + ", %other, %choice)");
    }
    const std::string getterSignature =
        "@get$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value" +
        std::string(prepared ? ", %state: !ctjs.value" : "") + ")";
    fixture =
        replaced(fixture, getterSignature,
                 getterSignature.substr(0, getterSignature.size() - 1) + ", %input: !ctjs.value)");
    fixture = replaced(fixture, prepared ? "%getterEnv)" : "%getter(%owned)",
                       prepared ? "%getterEnv, %putResult)" : "%getter(%owned, %putResult)");
    const std::string write = "    %written = ctjs.call %setter(%state, %entryKey, %value)";
    const std::string clear = "    %cleared = ctjs.call %clearer(%state)\n";
    const std::string erase = "    %deleted = ctjs.call %eraser(%state, %entryKey)\n";
    const std::string read = "    %loaded = ctjs.call %reader(%state, %aliasKey)\n";
    const std::string reseed = "    %reseeded = ctjs.call %setter(%state, %entryKey, %value)\n";
    const std::string aliasWrite = "    %aliased = ctjs.call %setter(%state, %aliasKey, %value)\n";
    const std::string disjointWrite =
        "    %disjoint = ctjs.call %setter(%state, %numericKey, %value)\n";
    const std::string setup =
        "\n    %clearKey = ctjs.constant #ctjs.string<\"clear\">\n"
        "    %clearer = ctjs.get_property %state[%clearKey]\n"
        "    %deleteKey = ctjs.constant #ctjs.string<\"delete\">\n"
        "    %eraser = ctjs.get_property %state[%deleteKey]\n"
        "    %getKey = ctjs.constant #ctjs.string<\"get\">\n"
        "    %reader = ctjs.get_property %state[%getKey]\n"
        "    %undefined = ctjs.constant #ctjs.undefined\n"
        "    %numericKey = ctjs.constant #ctjs.number<0>\n"
        "    %negativeZero = ctjs.constant #ctjs.number<9223372036854775808>\n";
    const auto base =
        replaced(replaced(fixture, write, write + setup + clear + read),
                 "    ctjs.return %size\n  }\n}\n", "    ctjs.return %loaded\n  }\n}\n");
    const auto saved = replaced(base, read, read + aliasWrite);
    const std::string branch = "    %flag = ctjs.truthy %choice\n"
                               "    scf.if %flag {\n" +
                               clear +
                               "      scf.yield\n"
                               "    } else {\n"
                               "      %again = ctjs.call %clearer(%state)\n"
                               "      %twice = ctjs.call %clearer(%state)\n"
                               "      scf.yield\n    }\n";
    const auto bothArms = replaced(base, clear, branch);
    const auto census = [&](mlir::ModuleOp module, const OwnedGlobalRoots & query,
                            unsigned resultMask) {
        const auto strings = Alternatives::forTag(mlir::TypeID::get<ctjs::StringAttr>());
        const auto booleans = Alternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>());
        const Alternatives returned{
            resultMask & (Alternatives::Boolean | Alternatives::Number | Alternatives::String),
            resultMask, true};
        auto setter = module.lookupSymbol<ctjs::FuncOp>("put$4");
        auto getter = module.lookupSymbol<ctjs::FuncOp>("get$3");
        std::vector<ctjs::CreateObjectOp> objects;
        std::vector<ctjs::GetPropertyOp> reads;
        std::vector<ctjs::CallOp> calls;
        getter.walk([&](ctjs::GetPropertyOp operation) { reads.push_back(operation); });
        setter.walk([&](ctjs::CreateObjectOp operation) { objects.push_back(operation); });
        setter.walk([&](ctjs::GetPropertyOp operation) { reads.push_back(operation); });
        setter.walk([&](ctjs::CallOp operation) { calls.push_back(operation); });
        check(query.roots().size() == 1 && query.roots().front().methodTable &&
                  query.roots().front().methodTable->capturedMap,
              "clear publishes one complete method-table and Map owner");
        if (query.roots().size() != 1 || !query.roots().front().methodTable ||
            !query.roots().front().methodTable->capturedMap) {
            return;
        }
        const auto & table = *query.roots().front().methodTable;
        const auto & owner = *table.capturedMap;
        bool complete = table.calls.size() == 4 && table.methods.size() == 2 &&
                        owner.leafObjects == objects && owner.reads == reads &&
                        owner.calls == calls;
        unsigned consumers = 0;
        for (const auto & edge : table.calls) {
            complete &= edge.capturedMap.has_value();
            if (!edge.capturedMap) { continue; }
            const auto & capture = *edge.capturedMap;
            complete &= capture.allocation == owner.allocation && capture.leafObjects == objects &&
                        capture.leafWrites.empty() && capture.leafReads.empty() &&
                        capture.reads == reads && capture.calls == calls &&
                        capture.closures.size() == 2 && capture.parameters.size() == 2 &&
                        capture.upvalues.size() == (prepared ? 0u : 2u);
            if (edge.function == setter) {
                complete &= edge.arguments.size() == 3;
                if (edge.arguments.size() == 3) {
                    complete &= edge.arguments[0].alternatives == strings &&
                                edge.arguments[1].alternatives == strings &&
                                edge.arguments[2].alternatives == booleans;
                }
            } else if (edge.function == getter) {
                ++consumers;
                complete &= edge.arguments.size() == 1;
                if (edge.arguments.size() == 1) {
                    complete &=
                        edge.arguments.front().alternatives == returned &&
                        edge.arguments.front().actual == edge.call->getOperand(prepared ? 4 : 2);
                }
            } else {
                complete = false;
            }
        }
        check(complete && consumers == 1,
              "clear retains every source call and independently types the result-fed formal");
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & text, bool expected, const char * message,
                             unsigned mask = Alternatives::Undefined) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "source/prepared captured clear fixture parses");
        if (!module) { return; }
        auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        check(query.proved() == expected && !query.exhausted() &&
                  (expected || withheld(*module, query)),
              message);
        if (query.proved() != expected || query.exhausted()) {
            std::fprintf(stderr, "clear owner %s row %u: %s\n", prepared ? "prepared" : "source",
                         rows, query.reason().str().c_str());
        }
        if (expected && query.proved()) { census(*module, query, mask); }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "clear proof preserves every source read, mutation and branch");
    };
    variant(base, true, "clear proves Undefined for a key never previously mentioned by set");
    variant(replaced(base, "    ctjs.return %loaded", "    ctjs.return %cleared"), true,
            "the standard zero-argument clear result is independently Undefined");
    variant(replaced(base, write, ""), true,
            "clear establishes whole-Map absence without a preceding local seed");
    variant(replaced(base, clear, clear + "    %again = ctjs.call %clearer(%state)\n"), true,
            "repeated standard clear remains definite whole-Map absence");
    variant(saved, true, "saved Undefined retains its read-time value after an aliasing write");
    const auto objectRead = replaced(read, "%aliasKey", "%entryKey");
    const auto savedObject =
        replaced(replaced(base, clear + read, objectRead + clear), "    ctjs.return %loaded",
                 "    %same = ctjs.compare strict_eq %loaded, %value\n"
                 "    ctjs.return %same");
    variant(savedObject, true, "saved object identity survives clear of its former Map entry",
            Alternatives::Boolean);
    const auto savedString =
        replaced(replaced(base, clear + read, objectRead + clear), "%value = ctjs.create_object",
                 "%value = ctjs.constant #ctjs.string<\"saved\">");
    variant(savedString, true, "saved String values survive clear independently of new contents",
            Alternatives::String);
    variant(replaced(replaced(base, "%clearer = ctjs.get_property %state[%clearKey]",
                              "%clearer = ctjs.get_property %written[%clearKey]"),
                     "%clearer(%state)", "%clearer(%written)"),
            true, "the fluent set result identifies the same Map for a later standard clear");
    variant(replaced(base, read, aliasWrite + read), false,
            "an exact later object write invalidates the absent primitive result");
    variant(replaced(base, read, reseed + read), false,
            "distinct future String formals may alias despite distinct startup constants");
    variant(replaced(base, read, disjointWrite + read), true,
            "a Number write preserves a cleared String query without a prior key fact");
    variant(replaced(base, read, aliasWrite + replaced(clear, "%cleared", "%recleared") + read),
            true, "another clear restores whole-Map absence after arbitrary writes");
    variant(replaced(base, "%clearer(%state)", "%clearer(%this)"), false,
            "standard clear must retain its exact proved Map receiver");
    variant(replaced(base, "%clearer(%state)", "%clearer(%state, %aliasKey)"), false,
            "clear with an extra argument is outside the standard host method contract");
    variant(replaced(base, "#ctjs.string<\"clear\">", "#ctjs.string<\"delete\">"), false,
            "zero-argument delete cannot borrow the whole-Map clear contract");
    variant(replaced(base, "%state[%clearKey]", "%state[%aliasKey]"), false,
            "startup String values cannot prove the spelling of a future method read");
    variant(replaced(base, read, "    %unknown = ctjs.call %this(%state)\n" + read), false,
            "an unknown effect cannot preserve clear's absent-read proof");
    const std::string oneArm = "    %flag = ctjs.truthy %choice\n"
                               "    scf.if %flag {\n" +
                               clear + "      scf.yield\n    }\n";
    const std::string clearAndDelete = "    %flag = ctjs.truthy %choice\n"
                                       "    scf.if %flag {\n" +
                                       clear + "      scf.yield\n    } else {\n" +
                                       replaced(erase, "%entryKey", "%aliasKey") +
                                       "      scf.yield\n    }\n";
    const std::string conditionalWrite = "    %flag = ctjs.truthy %choice\n"
                                         "    scf.if %flag {\n" +
                                         aliasWrite + "      scf.yield\n    }\n";
    for (const char * actual : {"true", "false"}) {
        const auto withChoice = [&](const std::string & text) {
            return replaced(text, "#ctjs.boolean<true>",
                            std::string("#ctjs.boolean<") + actual + ">");
        };
        variant(withChoice(bothArms), true,
                "one clear versus two clears establishes absence on nonidentical surviving arms");
        variant(withChoice(replaced(base, clear, oneArm)), false,
                "a single cleared arm cannot specialize future Boolean inputs to startup values");
        variant(withChoice(replaced(base, clear, clearAndDelete)), true,
                "clear and exact deletion intersect at their common absent key");
        variant(withChoice(replaced(base, read, conditionalWrite + read)), false,
                "a write on one arm invalidates whole-Map absence for a possibly aliasing query");
        variant(withChoice(replaced(base, read,
                                    replaced(conditionalWrite, aliasWrite, disjointWrite) + read)),
                true, "a disjoint write on one arm preserves the cleared String query");
    }
    const std::string selectedKey =
        "    %stringKey = ctjs.constant #ctjs.string<\"selected\">\n"
        "    %nullKey = ctjs.constant #ctjs.null\n"
        "    %selectFlag = ctjs.truthy %choice\n"
        "    %selected = scf.if %selectFlag -> (!ctjs.value) {\n"
        "      scf.yield %stringKey : !ctjs.value\n"
        "    } else {\n"
        "      scf.yield %nullKey : !ctjs.value\n    }\n"
        "    %selectedFlag = ctjs.truthy %selected\n"
        "    scf.if %selectedFlag {\n" +
        clear +
        "      %selectedWrite = ctjs.call %setter(%state, %selected, %value)\n"
        "      scf.yield\n"
        "    } else {\n"
        "      %stringDeleted = ctjs.call %eraser(%state, %stringKey)\n"
        "      scf.yield\n    }\n";
    const auto pathDependent =
        replaced(replaced(base, clear, selectedKey), "%reader(%state, %aliasKey)",
                 "%reader(%state, %stringKey)");
    variant(pathDependent, false,
            "else-only Null narrowing cannot prove the then-written String key absent at join");
    variant(replaced(pathDependent, "%setter(%state, %selected, %value)",
                     "%setter(%state, %numericKey, %value)"),
            true, "a path-independent disjoint key proves the exact cross-arm absence repair");
    check(rows == 29, "all clear admission, saved-value, alias and branch controls ran");

    // Both source and prepared queries recheck SSA dominance independently of
    // complete-looking reports, even when a misplaced fluent receiver still
    // exactly matches the method's object operand.
    unsigned scopeMutations = 0;
    for (const bool sibling : {false, true}) {
        for (const unsigned kind : {0u, 1u, 2u, 3u}) {
            const std::string scoped =
                "    %flag = ctjs.truthy %choice\n"
                "    scf.if %flag {\n"
                "      %branchClearer = ctjs.get_property %state[%clearKey]\n"
                "      %branchFlag = ctjs.truthy %choice\n"
                "      %branchMap = ctjs.call %setter(%state, %numericKey, %value)\n"
                "      %branchLoad = ctjs.call %reader(%state, %aliasKey)\n"
                "      scf.yield\n"
                "    } else {\n"
                "      %elseClearer = ctjs.get_property %state[%clearKey]\n"
                "      %elseCleared = ctjs.call %elseClearer(%state)\n"
                "      %elseSame = ctjs.compare strict_eq %loaded, %undefined\n"
                "      scf.if %flag {\n        scf.yield\n      }\n"
                "      scf.yield\n    }\n"
                "    %outerClearer = ctjs.get_property %state[%clearKey]\n"
                "    %outerCleared = ctjs.call %outerClearer(%state)\n"
                "    scf.if %flag {\n      scf.yield\n    }\n"
                "    ctjs.return %loaded";
            auto module = mlir::parseSourceString<mlir::ModuleOp>(
                replaced(base, "    ctjs.return %loaded", scoped), &context);
            check(static_cast<bool>(module), "clear dependency scope fixture parses");
            if (!module) { continue; }
            mlir::Builder attributes(&context);
            module->walk([&](mlir::Operation * operation) {
                operation->setAttr("ctnative.map_empty", attributes.getBoolAttr(true));
                operation->setAttr("ctnative.map_absent", attributes.getBoolAttr(true));
                operation->setAttr("ctnative.map_read_type", attributes.getStringAttr("undefined"));
            });
            const auto contract = requested(*module);
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "properly scoped clear dependencies have an independent complete proof");
            auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
            std::vector<mlir::scf::IfOp> branches;
            for (auto & operation : setter.getBody().front()) {
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                    branches.push_back(branch);
                }
            }
            check(branches.size() == 2, "clear scope witness keeps both outer conditionals");
            if (branches.size() != 2) { continue; }
            auto & thenBlock = branches.front().getThenRegion().front();
            auto branchClearer = llvm::cast<ctjs::GetPropertyOp>(thenBlock.front());
            ctjs::TruthyOp branchFlag;
            ctjs::CallOp branchMap, branchLoad;
            for (auto & operation : thenBlock) {
                if (auto flag = llvm::dyn_cast<ctjs::TruthyOp>(operation)) { branchFlag = flag; }
                if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation)) {
                    if (call.getArgs().size() == 2) {
                        branchMap = call;
                    } else {
                        branchLoad = call;
                    }
                }
            }
            auto * target = &setter.getBody().front();
            if (sibling) { target = &branches.front().getElseRegion().front(); }
            ctjs::GetPropertyOp clearer;
            ctjs::CallOp targetCall;
            mlir::scf::IfOp targetBranch;
            mlir::Operation * scalarUse = target->getTerminator();
            for (auto & operation : *target) {
                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) { clearer = read; }
                if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation)) { targetCall = call; }
                if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
                    scalarUse = compare;
                }
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                    targetBranch = branch;
                }
            }
            check(branchFlag && branchMap && branchLoad && clearer && targetCall && targetBranch,
                  "scope fixture retains its scalar, callee, receiver and flag operands");
            if (!branchFlag || !branchMap || !branchLoad || !clearer || !targetCall ||
                !targetBranch) {
                continue;
            }
            struct edit {
                mlir::Operation * op;
                unsigned operand;
                mlir::Value original;
            };
            std::vector<edit> edits;
            const auto change = [&](mlir::Operation * operation, unsigned operand,
                                    mlir::Value value) {
                edits.push_back({operation, operand, operation->getOperand(operand)});
                operation->setOperand(operand, value);
            };
            if (kind == 0) {
                change(scalarUse, 0, branchLoad.getResult());
            } else if (kind == 1) {
                change(targetCall, 0, branchClearer.getResult());
            } else if (kind == 2) {
                change(targetBranch, 0, branchFlag.getResult());
            } else {
                change(clearer, 0, branchMap.getResult());
                change(targetCall, 1, branchMap.getResult());
            }
            OwnedGlobalRoots stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      withheld(*module, stale),
                  "a clear dependency scope edit invalidates old reports");
            OwnedGlobalRoots fresh(*module, requested(*module));
            check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                  "fresh reports cannot authorize a then-only clear dependency outside its scope");
            for (const auto & edit : edits) { edit.op->setOperand(edit.operand, edit.original); }
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring dependency dominance restores clear's complete proof");
            ++scopeMutations;
        }
    }
    check(scopeMutations == 8, "all scalar, callee, flag and fluent Map scope controls ran");
    for (const auto & [text, label] : {std::pair{base, "unseen key"}, {bothArms, "two arms"}}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "clear budget fixture parses");
        if (!module) { continue; }
        auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        const unsigned completion = query.steps();
        check(query.proved() && completion < 20000, "clear proof has bounded complete work");
        if (!query.proved() || completion >= 20000) { continue; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            OwnedGlobalRoots limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      withheld(*module, limited),
                  "every incomplete clear budget withholds the complete published family");
        }
        OwnedGlobalRoots exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion,
              "the exact clear budget reproduces the whole source family");
        if (exact.proved()) { census(*module, exact, Alternatives::Undefined); }
        mlir::Builder attributes(&context);
        module->walk([&](mlir::Operation * operation) {
            operation->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.map_empty", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.map_absent", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.map_read_type", attributes.getStringAttr("undefined"));
        });
        contract = requested(*module);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "forged clear reports leave the positive live proof independently reproducible");
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        std::vector<ctjs::CallOp> clears;
        setter.walk([&](ctjs::CallOp operation) {
            auto method = operation.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (!method) { return; }
            auto key = method.getKey().getDefiningOp<ctjs::ConstantOp>();
            auto name = key ? llvm::dyn_cast<ctjs::StringAttr>(key.getValue()) : ctjs::StringAttr{};
            if (name && name.getValue() == "clear") { clears.push_back(operation); }
        });
        check(clears.size() == (std::string(label) == "two arms" ? 3u : 1u),
              "clear census keeps every nonidentical branch operation");
        if (clears.empty()) { continue; }
        auto clearCall = clears.front();
        auto clearMethod = clearCall.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        auto clearKey = clearMethod.getKey().getDefiningOp<ctjs::ConstantOp>();
        const auto originalOperands = llvm::to_vector(clearCall->getOperands());
        const auto originalName = clearKey.getValue();
        unsigned mutations = 0;
        const auto refuse = [&] {
            ++mutations;
            OwnedGlobalRoots stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      withheld(*module, stale),
                  "live clear edits invalidate the prior fingerprint");
            OwnedGlobalRoots fresh(*module, requested(*module));
            check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                  "fresh fingerprints and forged empty reports cannot repair an invalid clear");
        };
        clearCall->setOperand(1, setter.getBody().front().getArgument(0));
        refuse();
        clearCall->setOperands(originalOperands);
        clearCall->insertOperands(clearCall->getNumOperands(),
                                  setter.getBody().front().getArgument(prepared ? 5 : 4));
        refuse();
        clearCall->setOperands(originalOperands);
        clearKey->setAttr("value", ctjs::StringAttr::get(&context, "delete"));
        refuse();
        clearKey->setAttr("value", originalName);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring clear spelling, receiver and arity restores its current proof");
        std::printf("clear owner %s %s: %u rows, %u scope mutations, %u live edits, all %u budgets "
                    "checked\n",
                    prepared ? "prepared" : "source", label, rows, scopeMutations, mutations,
                    completion);
    }
}

void checkDefiniteMapAbsenceOwner(mlir::MLIRContext & context, const std::string & source,
                                  bool prepared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto withheld = [](mlir::ModuleOp module, const OwnedGlobalRoots & query) {
        return empty(module, query);
    };
    auto fixture =
        replaced(source, "%entryKey: !ctjs.value)",
                 "%entryKey: !ctjs.value, %aliasKey: !ctjs.value, %choice: !ctjs.value)");
    fixture = replaced(fixture, "    %actual =",
                       "    %other = ctjs.constant #ctjs.string<\"z\">\n"
                       "    %choice = ctjs.constant #ctjs.boolean<true>\n"
                       "    %actual =");
    for (const auto & [name, actual] :
         {std::pair{"putterEnv", "actual"}, {"repeatEnv", "actual"}, {"laterEnv", "future"}}) {
        const std::string prefix = prepared ? std::string("%") + name + ", " : "%owned, ";
        fixture = replaced(fixture, prefix + "%" + actual + ")",
                           prefix + "%" + actual + ", %other, %choice)");
    }
    const std::string getterSignature =
        "@get$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value" +
        std::string(prepared ? ", %state: !ctjs.value" : "") + ")";
    fixture =
        replaced(fixture, getterSignature,
                 getterSignature.substr(0, getterSignature.size() - 1) + ", %input: !ctjs.value)");
    fixture = replaced(fixture, prepared ? "%getterEnv)" : "%getter(%owned)",
                       prepared ? "%getterEnv, %putResult)" : "%getter(%owned, %putResult)");
    const std::string write = "    %written = ctjs.call %setter(%state, %entryKey, %value)";
    const std::string erase = "    %deleted = ctjs.call %eraser(%state, %entryKey)\n";
    const std::string read = "    %loaded = ctjs.call %reader(%state, %entryKey)\n";
    const std::string reseed = "    %reseeded = ctjs.call %setter(%state, %entryKey, %value)\n";
    const std::string aliasWrite = "    %aliased = ctjs.call %setter(%state, %aliasKey, %value)\n";
    const std::string disjointWrite =
        "    %disjoint = ctjs.call %setter(%state, %numericKey, %value)\n";
    const std::string setup =
        "\n    %deleteKey = ctjs.constant #ctjs.string<\"delete\">\n"
        "    %eraser = ctjs.get_property %state[%deleteKey]\n"
        "    %getKey = ctjs.constant #ctjs.string<\"get\">\n"
        "    %reader = ctjs.get_property %state[%getKey]\n"
        "    %undefined = ctjs.constant #ctjs.undefined\n"
        "    %numericKey = ctjs.constant #ctjs.number<0>\n"
        "    %negativeZero = ctjs.constant #ctjs.number<9223372036854775808>\n";
    const auto base =
        replaced(replaced(fixture, write, write + setup + erase + read),
                 "    ctjs.return %size\n  }\n}\n", "    ctjs.return %loaded\n  }\n}\n");
    const auto saved = replaced(base, read, read + reseed);
    const std::string branch = "    %flag = ctjs.truthy %choice\n"
                               "    scf.if %flag {\n" +
                               erase +
                               "      scf.yield\n"
                               "    } else {\n"
                               "      %again = ctjs.call %eraser(%state, %entryKey)\n"
                               "      %twice = ctjs.call %eraser(%state, %entryKey)\n"
                               "      scf.yield\n    }\n";
    const auto bothArms = replaced(base, erase, branch);
    const auto census = [&](mlir::ModuleOp module, const OwnedGlobalRoots & query,
                            unsigned resultMask) {
        const auto strings = Alternatives::forTag(mlir::TypeID::get<ctjs::StringAttr>());
        const auto booleans = Alternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>());
        const Alternatives returned{resultMask & Alternatives::Boolean, resultMask, true};
        auto setter = module.lookupSymbol<ctjs::FuncOp>("put$4");
        auto getter = module.lookupSymbol<ctjs::FuncOp>("get$3");
        std::vector<ctjs::CreateObjectOp> objects;
        std::vector<ctjs::GetPropertyOp> reads;
        std::vector<ctjs::CallOp> calls;
        getter.walk([&](ctjs::GetPropertyOp operation) { reads.push_back(operation); });
        setter.walk([&](ctjs::CreateObjectOp operation) { objects.push_back(operation); });
        setter.walk([&](ctjs::GetPropertyOp operation) { reads.push_back(operation); });
        setter.walk([&](ctjs::CallOp operation) { calls.push_back(operation); });
        check(query.roots().size() == 1 && query.roots().front().methodTable &&
                  query.roots().front().methodTable->capturedMap,
              "absence publishes one complete method-table and Map owner");
        if (query.roots().size() != 1 || !query.roots().front().methodTable ||
            !query.roots().front().methodTable->capturedMap) {
            return;
        }
        const auto & table = *query.roots().front().methodTable;
        const auto & owner = *table.capturedMap;
        bool complete = table.calls.size() == 4 && table.methods.size() == 2 &&
                        owner.leafObjects == objects && owner.reads == reads &&
                        owner.calls == calls;
        unsigned consumers = 0;
        for (const auto & edge : table.calls) {
            complete &= edge.capturedMap.has_value();
            if (!edge.capturedMap) { continue; }
            const auto & capture = *edge.capturedMap;
            complete &= capture.allocation == owner.allocation && capture.leafObjects == objects &&
                        capture.leafWrites.empty() && capture.leafReads.empty() &&
                        capture.reads == reads && capture.calls == calls &&
                        capture.closures.size() == 2 && capture.parameters.size() == 2 &&
                        capture.upvalues.size() == (prepared ? 0u : 2u);
            if (edge.function == setter) {
                complete &= edge.arguments.size() == 3;
                if (edge.arguments.size() == 3) {
                    complete &= edge.arguments[0].alternatives == strings &&
                                edge.arguments[1].alternatives == strings &&
                                edge.arguments[2].alternatives == booleans;
                }
            } else if (edge.function == getter) {
                ++consumers;
                complete &= edge.arguments.size() == 1;
                if (edge.arguments.size() == 1) {
                    complete &=
                        edge.arguments.front().alternatives == returned &&
                        edge.arguments.front().actual == edge.call->getOperand(prepared ? 4 : 2);
                }
            } else {
                complete = false;
            }
        }
        check(complete && consumers == 1,
              "absence retains every source call and independently types the result-fed formal");
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & text, bool expected, const char * message,
                             unsigned mask = Alternatives::Undefined) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "source/prepared definite absence fixture parses");
        if (!module) { return; }
        auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        check(query.proved() == expected && !query.exhausted() &&
                  (expected ? query.roots().size() == 1 : withheld(*module, query)),
              message);
        if (query.proved() != expected || query.exhausted()) {
            std::fprintf(stderr, "absence owner %s row %u: %s\n", prepared ? "prepared" : "source",
                         rows, query.reason().str().c_str());
        }
        if (expected && query.proved()) { census(*module, query, mask); }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "the absence query preserves exact deletion, read and branch operations");
    };
    variant(base, true, "exact deletion proves Undefined independently of an object payload");
    variant(replaced(base, erase, erase + "    %again = ctjs.call %eraser(%state, %entryKey)\n"),
            true, "repeated deletion retains exact-key absence");
    variant(replaced(base, write, ""), true,
            "exact deletion proves absence without any tracked earlier entry");
    variant(saved, true, "saved Undefined keeps its read-time value after exact reseeding");
    variant(replaced(base, read, read + aliasWrite), true,
            "saved Undefined survives a later potentially aliasing write");
    variant(
        replaced(base, read, "    %otherDeleted = ctjs.call %eraser(%state, %aliasKey)\n" + read),
        true, "a potentially aliasing deletion cannot resurrect an absent entry");
    variant(replaced(base, read,
                     read +
                         "    %hasKey = ctjs.constant #ctjs.string<\"has\">\n"
                         "    %hasMethod = ctjs.get_property %state[%hasKey]\n"
                         "    %observed = ctjs.call %hasMethod(%state, %entryKey)\n"
                         "    %observedFlag = ctjs.truthy %observed\n"
                         "    scf.if %observedFlag {\n" +
                         reseed + "      scf.yield\n    }\n"),
            true, "a later has refinement and conditional write cannot retarget saved Undefined");
    variant(replaced(base, read, disjointWrite + read), true,
            "a Number-key write preserves absence for a String formal key");
    variant(replaced(base, read, aliasWrite + read), false,
            "two String formals may alias even when their startup literals differ");
    variant(replaced(base, read, aliasWrite + replaced(erase, "%deleted", "%redeleted") + read),
            true, "a later exact deletion restores absence after a potentially aliasing write");
    variant(replaced(base, "%eraser(%state, %entryKey)", "%eraser(%state, %aliasKey)"), false,
            "possibly aliasing deletion does not prove absence for another formal");
    variant(replaced(base, "%eraser(%state, %entryKey)", "%eraser(%state, %numericKey)"), false,
            "deleting a disjoint key leaves the original object present");
    variant(replaced(base, "%reader(%state, %entryKey)", "%reader(%state, %aliasKey)"), false,
            "absence of one formal key supplies no value for another formal key");
    variant(replaced(base, read, reseed + read), false,
            "exact reseeding restores the object and invalidates the Undefined return proof");
    const auto compared = replaced(base, "    ctjs.return %loaded",
                                   "    %same = ctjs.compare strict_eq %loaded, %undefined\n"
                                   "    ctjs.return %same");
    variant(compared, true, "the exact deleted get can be strictly compared with Undefined",
            Alternatives::Boolean);
    variant(replaced(compared, "strict_eq %loaded, %undefined", "strict_eq %loaded, %value"), true,
            "a definitely absent read can be compared with its former local object",
            Alternatives::Boolean);
    variant(replaced(compared, read, reseed + read), true,
            "the existing exact-reseed object comparison remains independently admissible",
            Alternatives::Boolean);
    auto zero = replaced(base, write,
                         "    %zero = ctjs.constant #ctjs.number<0>\n"
                         "    %written = ctjs.call %setter(%state, %zero, %value)");
    zero = replaced(zero, "%eraser(%state, %entryKey)", "%eraser(%state, %negativeZero)");
    zero = replaced(zero, "%reader(%state, %entryKey)", "%reader(%state, %zero)");
    variant(zero, true, "positive and negative zero name one SameValueZero absence fact");
    variant(replaced(replaced(zero, "#ctjs.number<0>", "#ctjs.number<9221120237041090560>"),
                     "#ctjs.number<9223372036854775808>", "#ctjs.number<9221120237041090561>"),
            true, "distinct NaN bit patterns name one SameValueZero absence fact");
    variant(replaced(zero, "#ctjs.number<9223372036854775808>", "#ctjs.boolean<false>"), false,
            "false and numeric zero remain distinct Map keys despite coercing equality");
    variant(replaced(base, "%deleteKey = ctjs.constant #ctjs.string<\"delete\">",
                     "%deleteKey = ctjs.constant #ctjs.string<\"clear\">"),
            false, "unsupported clear cannot borrow exact delete's absence contract");
    variant(replaced(base, read, "    %unknown = ctjs.call %this(%state)\n" + read), false,
            "an unknown call cannot preserve an earlier absence fact");
    for (const char * actual : {"true", "false"}) {
        const auto withChoice = [&](const std::string & text) {
            return replaced(text, "#ctjs.boolean<true>",
                            std::string("#ctjs.boolean<") + actual + ">");
        };
        variant(withChoice(bothArms), true,
                "nonidentical surviving arms both establish absence for every Boolean input");
        const std::string oneArm = "    %flag = ctjs.truthy %choice\n"
                                   "    scf.if %flag {\n" +
                                   erase + "      scf.yield\n    }\n";
        variant(withChoice(replaced(base, erase, oneArm)), false,
                "one-arm deletion cannot use the observed Boolean to select future paths");
        const std::string conditionalReseed = "    %flag = ctjs.truthy %choice\n"
                                              "    scf.if %flag {\n" +
                                              reseed + "      scf.yield\n    }\n";
        variant(withChoice(replaced(base, read, conditionalReseed + read)), false,
                "conditional reseeding destroys definite absence across the join");
        variant(withChoice(replaced(base, read,
                                    replaced(conditionalReseed, reseed, disjointWrite) + read)),
                true, "a conditional disjoint write preserves independently known absence");
    }
    check(rows == 30, "all definite absence, saved value, alias and branch controls ran");

    // A live query must not consume a then-only read merely because its
    // primitive category was already visited while scanning the other arm.
    for (const bool sibling : {false, true}) {
        const std::string scoped = "    %flag = ctjs.truthy %choice\n"
                                   "    scf.if %flag {\n"
                                   "      %branchLoaded = ctjs.call %reader(%state, %entryKey)\n"
                                   "      scf.yield\n"
                                   "    } else {\n"
                                   "      %elseSame = ctjs.compare strict_eq %loaded, %undefined\n"
                                   "      scf.yield\n    }\n"
                                   "    ctjs.return %loaded";
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            replaced(base, "    ctjs.return %loaded", scoped), &context);
        check(static_cast<bool>(module), "absence scope fixture parses before live mutation");
        if (!module) { continue; }
        mlir::Builder attributes(&context);
        module->walk([&](mlir::Operation * operation) {
            operation->setAttr("ctnative.map_absent", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.map_read_type", attributes.getStringAttr("undefined"));
        });
        const auto contract = requested(*module);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "the original scoped absence read has a complete independent proof");
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        mlir::scf::IfOp conditional;
        setter.walk([&](mlir::scf::IfOp operation) { conditional = operation; });
        auto inside = llvm::cast<ctjs::CallOp>(conditional.getThenRegion().front().front());
        auto * consumer = sibling ? &conditional.getElseRegion().front().front()
                                  : setter.getBody().front().getTerminator();
        const auto original = consumer->getOperand(0);
        consumer->setOperand(0, inside.getResult());
        OwnedGlobalRoots stale(*module, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && withheld(*module, stale),
              "an out-of-scope absent value invalidates the previous fingerprint");
        OwnedGlobalRoots fresh(*module, requested(*module));
        check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
              "fresh fingerprints and forged absence cannot authorize a then-only SSA value");
        consumer->setOperand(0, original);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring source dominance restores the independent absent-read proof");
    }
    // The absent read also depends on a live method, receiver and condition.
    // Mutating their SSA scope must fail even when the key and payload remain
    // valid and the forged report still claims a definitely absent entry.
    unsigned scopeMutations = 0;
    for (const bool sibling : {false, true}) {
        for (const unsigned kind : {0u, 1u, 2u}) {
            const std::string scoped =
                "    %flag = ctjs.truthy %choice\n"
                "    scf.if %flag {\n"
                "      %branchReader = ctjs.get_property %state[%getKey]\n"
                "      %branchFlag = ctjs.truthy %choice\n"
                "      %branchMap = ctjs.call %setter(%state, %numericKey, %value)\n"
                "      %branchLoad = ctjs.call %branchReader(%state, %entryKey)\n"
                "      scf.yield\n"
                "    } else {\n"
                "      %elseReader = ctjs.get_property %state[%getKey]\n"
                "      %elseLoad = ctjs.call %elseReader(%state, %entryKey)\n"
                "      scf.if %flag {\n        scf.yield\n      }\n"
                "      scf.yield\n    }\n"
                "    %outerReader = ctjs.get_property %state[%getKey]\n"
                "    %outerLoad = ctjs.call %outerReader(%state, %entryKey)\n"
                "    scf.if %flag {\n      scf.yield\n    }\n"
                "    ctjs.return %loaded";
            auto module = mlir::parseSourceString<mlir::ModuleOp>(
                replaced(base, "    ctjs.return %loaded", scoped), &context);
            check(static_cast<bool>(module), "absence dependency scope fixture parses");
            if (!module) { continue; }
            mlir::Builder attributes(&context);
            module->walk([&](mlir::Operation * operation) {
                operation->setAttr("ctnative.map_absent", attributes.getBoolAttr(true));
                operation->setAttr("ctnative.map_read_type", attributes.getStringAttr("undefined"));
            });
            const auto contract = requested(*module);
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "valid method, flag and fluent Map scopes have an independent absence proof");
            auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
            std::vector<mlir::scf::IfOp> branches;
            for (auto & operation : setter.getBody().front()) {
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                    branches.push_back(branch);
                }
            }
            check(branches.size() == 2, "dependency witness retains both enclosing conditionals");
            if (branches.size() != 2) { continue; }
            auto & thenBlock = branches.front().getThenRegion().front();
            auto branchReader = llvm::cast<ctjs::GetPropertyOp>(thenBlock.front());
            ctjs::TruthyOp branchFlag;
            ctjs::CallOp branchMap;
            for (auto & operation : thenBlock) {
                if (auto flag = llvm::dyn_cast<ctjs::TruthyOp>(operation)) { branchFlag = flag; }
                if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
                    call && call.getArgs().size() == 2) {
                    branchMap = call;
                }
            }
            auto * target = &setter.getBody().front();
            if (sibling) { target = &branches.front().getElseRegion().front(); }
            ctjs::GetPropertyOp reader;
            ctjs::CallOp targetCall;
            mlir::scf::IfOp targetBranch;
            for (auto & operation : *target) {
                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) { reader = read; }
                if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation)) { targetCall = call; }
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                    targetBranch = branch;
                }
            }
            check(branchFlag && branchMap && reader && targetCall && targetBranch &&
                      targetCall.getCallee() == reader.getResult(),
                  "scope controls retain exact method, receiver and condition operands");
            if (!branchFlag || !branchMap || !reader || !targetCall || !targetBranch) { continue; }
            struct edit {
                mlir::Operation * op;
                unsigned operand;
                mlir::Value original;
            };
            std::vector<edit> edits;
            const auto change = [&](mlir::Operation * operation, unsigned operand,
                                    mlir::Value value) {
                edits.push_back({operation, operand, operation->getOperand(operand)});
                operation->setOperand(operand, value);
            };
            if (kind == 0) {
                change(targetCall, 0, branchReader.getResult());
            } else if (kind == 1) {
                change(targetBranch, 0, branchFlag.getResult());
            } else {
                // Mutate both operands, retaining exact callee/receiver equality.
                // Only the then-only fluent Map's invalid scope rejects this.
                change(reader, 0, branchMap.getResult());
                change(targetCall, 1, branchMap.getResult());
            }
            OwnedGlobalRoots stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      withheld(*module, stale),
                  "a live dependency scope edit invalidates the old absence fingerprint");
            OwnedGlobalRoots fresh(*module, requested(*module));
            check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                  "fresh reports cannot authorize then-only method, flag or fluent Map values");
            for (const auto & edit : edits) { edit.op->setOperand(edit.operand, edit.original); }
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring every dependency operand restores the valid absence proof");
            ++scopeMutations;
        }
    }
    check(scopeMutations == 6, "all outer and sibling dependency scope controls ran");
    for (const auto & [text, label] : {std::pair{base, "deleted"}, {bothArms, "two arms"}}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "definite absence budget fixture parses");
        if (!module) { continue; }
        auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        const unsigned completion = query.steps();
        check(query.proved() && completion < 20000, "definite absence proof has bounded work");
        if (!query.proved() || completion >= 20000) { continue; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            OwnedGlobalRoots limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      withheld(*module, limited),
                  "every incomplete absence budget withholds the whole published owner");
        }
        OwnedGlobalRoots exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion,
              "the exact absence completion budget reproduces the complete family");
        if (exact.proved()) { census(*module, exact, Alternatives::Undefined); }
        mlir::Builder attributes(&context);
        module->walk([&](mlir::Operation * operation) {
            operation->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.map_absent", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.map_present", attributes.getBoolAttr(false));
            operation->setAttr("ctnative.map_read_type", attributes.getStringAttr("undefined"));
        });
        contract = requested(*module);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "forged absence reports leave the positive source independently provable");
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        ctjs::CallOp get;
        std::vector<ctjs::CallOp> deletes;
        setter.walk([&](ctjs::CallOp operation) {
            auto method = operation.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (!method) { return; }
            auto key = method.getKey().getDefiningOp<ctjs::ConstantOp>();
            auto name = key ? llvm::dyn_cast<ctjs::StringAttr>(key.getValue()) : ctjs::StringAttr{};
            if (name && name.getValue() == "get") { get = operation; }
            if (name && name.getValue() == "delete") { deletes.push_back(operation); }
        });
        check(get && !deletes.empty(), "absence retains its actual get and deletion operations");
        if (!get || deletes.empty()) { continue; }
        if (std::string(label) == "two arms") {
            unsigned branches = 0;
            setter.walk([&](mlir::scf::IfOp operation) {
                ++branches;
                check(!operation.getElseRegion().empty(), "the absence join retains both arms");
            });
            check(branches == 1 && deletes.size() == 3,
                  "the branch witness keeps one versus two deletions without block collapse");
        }
        unsigned mutations = 0;
        const auto mutation = [&](mlir::Operation * operation, unsigned operand,
                                  mlir::Value value) {
            ++mutations;
            const auto original = operation->getOperand(operand);
            operation->setOperand(operand, value);
            OwnedGlobalRoots stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      withheld(*module, stale),
                  "a live absence mutation invalidates the old source fingerprint");
            OwnedGlobalRoots fresh(*module, requested(*module));
            check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                  "a fresh contract and forged reports cannot repair the missing source proof");
            operation->setOperand(operand, original);
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring exact deletion restores its independent absence proof");
        };
        const auto aliasKey = setter.getBody().front().getArgument(prepared ? 5 : 4);
        mutation(get, 2, aliasKey);
        // In the two-arm witness, this removes the only exact deletion in
        // the first arm; the second arm still has two real deletions.
        mutation(deletes.front(), 2, aliasKey);
        mutation(deletes.front(), 1, setter.getBody().front().getArgument(0));
        std::printf("absence owner %s %s: %u rows, %u live mutations, all %u budgets checked\n",
                    prepared ? "prepared" : "source", label, rows, mutations, completion);
    }
}

void checkLeafReadbackOwner(mlir::MLIRContext & context, const std::string & source, bool lifted) {
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const std::string write = "    %written = ctjs.call %setter(%state, %entryKey, %value)";
    const std::string field = "    %fieldKey = ctjs.constant #ctjs.string<\"value\">\n"
                              "    %one = ctjs.constant #ctjs.number<4607182418800017408>\n"
                              "    ctjs.set_property %value[%fieldKey], %one\n";
    const std::string read = "\n    %getKey = ctjs.constant #ctjs.string<\"get\">\n"
                             "    %reader = ctjs.get_property %state[%getKey]\n"
                             "    %saved = ctjs.call %reader(%state, %entryKey)\n";
    const std::string overwrite =
        "    %replacement = ctjs.create_object\n"
        "    ctjs.set_property %replacement[%fieldKey], %one\n"
        "    %replaced = ctjs.call %setter(%state, %entryKey, %replacement)\n";
    const std::string erase = "    %deleteKey = ctjs.constant #ctjs.string<\"delete\">\n"
                              "    %eraser = ctjs.get_property %state[%deleteKey]\n"
                              "    %erased = ctjs.call %eraser(%state, %entryKey)\n";
    const std::string identity = "    %same = ctjs.compare strict_eq %saved, %value\n"
                                 "    ctjs.return %same";
    const std::string fieldReturn = "    %loaded = ctjs.get_property %saved[%fieldKey]\n"
                                    "    ctjs.return %loaded";
    const auto numeric = replaced(source, write, field + write);
    const auto saved = replaced(numeric, write, write + read);
    const auto same = replaced(saved, "    ctjs.return %size\n  }\n}\n", identity + "\n  }\n}\n");
    const auto loaded =
        replaced(saved, "    ctjs.return %size\n  }\n}\n", fieldReturn + "\n  }\n}\n");
    const auto savedAfter =
        replaced(loaded, "    %loaded =",
                 overwrite + erase +
                     "    %two = ctjs.constant #ctjs.number<4611686018427387904>\n"
                     "    ctjs.set_property %value[%fieldKey], %two\n"
                     "    %loaded =");
    const auto census = [&](mlir::ModuleOp module, const OwnedGlobalRoots & query) {
        auto setter = module.lookupSymbol<ctjs::FuncOp>("put$4");
        const auto & table = *query.roots().front().methodTable;
        const auto & capture = *table.capturedMap;
        std::vector<ctjs::CreateObjectOp> objects;
        std::vector<ctjs::SetPropertyOp> writes;
        std::vector<ctjs::GetPropertyOp> fields, mapReads;
        std::vector<ctjs::CallOp> mapCalls;
        module.lookupSymbol<ctjs::FuncOp>("get$3").walk(
            [&](ctjs::GetPropertyOp operation) { mapReads.push_back(operation); });
        setter.walk([&](ctjs::CreateObjectOp operation) { objects.push_back(operation); });
        setter.walk([&](ctjs::SetPropertyOp operation) { writes.push_back(operation); });
        setter.walk([&](ctjs::CallOp operation) { mapCalls.push_back(operation); });
        setter.walk([&](ctjs::GetPropertyOp operation) {
            auto key = operation.getKey().getDefiningOp<ctjs::ConstantOp>();
            auto name = key ? llvm::dyn_cast<ctjs::StringAttr>(key.getValue()) : ctjs::StringAttr{};
            (name && name.getValue() == "value" ? fields : mapReads).push_back(operation);
        });
        bool complete =
            table.methods.size() == 2 && table.calls.size() == 4 && capture.closures.size() == 2 &&
            capture.parameters.size() == 2 && capture.upvalues.size() == (lifted ? 0u : 2u) &&
            capture.leafObjects == objects && capture.leafWrites == writes &&
            capture.leafReads == fields && capture.reads == mapReads && capture.calls == mapCalls;
        for (const auto & edge : table.calls) {
            complete &= edge.capturedMap.has_value();
            if (!edge.capturedMap) { continue; }
            complete &= edge.capturedMap->allocation == capture.allocation &&
                        edge.capturedMap->leafObjects == objects &&
                        edge.capturedMap->leafWrites == writes &&
                        edge.capturedMap->leafReads == fields &&
                        edge.capturedMap->reads == mapReads && edge.capturedMap->calls == mapCalls;
        }
        check(complete, "the published owner retains each exact readback operation and shared leaf "
                        "origin once");
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & text, bool expected, const char * message) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "source/prepared leaf readback owner fixture parses");
        if (!module) { return; }
        auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        check(query.proved() == expected && !query.exhausted() &&
                  (expected ? query.roots().size() == 1 : empty(*module, query)),
              message);
        if (query.proved() != expected || query.exhausted()) {
            std::fprintf(stderr, "leaf readback owner %s row %u: %s\n",
                         lifted ? "prepared" : "source", rows, query.reason().str().c_str());
        }
        if (expected && query.proved() && !query.roots().empty()) { census(*module, query); }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "the owner query never rewrites the current saved read or comparison operands");
    };
    variant(same, true, "same-key object readback and strict identity publish a complete owner");
    variant(loaded, true,
            "a definitely initialized scalar field on the saved alias has a complete owner");
    variant(savedAfter, true,
            "a saved alias owns its identity and updated field after overwrite and deletion");
    variant(replaced(same, "    %same =", overwrite + erase + "    %same ="), true,
            "the saved object identity survives both replacement and deletion");
    variant(replaced(replaced(same, "    %same =", overwrite + erase + "    %same ="),
                     "strict_eq %saved, %value", "strict_eq %saved, %replacement"),
            true, "equal fields cannot merge distinct source allocation identities");
    variant(replaced(loaded, "%saved[%fieldKey]", "%value[%fieldKey]"), true,
            "direct own-field readback does not require a provider object token");
    variant(replaced(savedAfter, "ctjs.set_property %value[%fieldKey], %two",
                     "ctjs.set_property %saved[%fieldKey], %two"),
            true, "the same owning field can be updated through the saved alias");
    variant(replaced(loaded, "%saved[%fieldKey]", "%saved[%getKey]"), false,
            "an owner cannot infer an absent own field from object identity");
    variant(replaced(loaded, "%reader(%state, %entryKey)", "%reader(%state, %fieldKey)"), false,
            "an unknown incoming Map object cannot borrow this invocation's fresh identity");
    variant(replaced(loaded, "    ctjs.return %loaded", "    ctjs.return %saved"), false,
            "owning the shared Map does not authorize an object-valued public result");
    const auto freshAfterDelete =
        replaced(loaded, "    %loaded =",
                 erase + "    %current = ctjs.call %reader(%state, %entryKey)\n"
                         "    %loaded =");
    variant(replaced(freshAfterDelete, "%saved[%fieldKey]", "%current[%fieldKey]"), false,
            "a fresh deleted read is separate from the saved alias's definite field");
    variant(replaced(same, "strict_eq %saved, %value", "strict_eq %saved, %this"), false,
            "unproved external identity prevents publication of the whole method family");
    variant(replaced(loaded, "    %loaded =",
                     "    ctjs.set_property %saved[%fieldKey], %value\n"
                     "    %loaded ="),
            false, "a graph introduced through a saved alias invalidates the whole owner");
    check(rows == 13, "all readback owner and independent unsafe-use controls ran");

    // Live queries also see IR changed after parsing. A then-only allocation
    // or read cannot be borrowed by a later statement or the sibling arm.
    for (const bool alias : {false, true}) {
        const std::string thenBody =
            alias ? "      %branchSaved = ctjs.call %reader(%state, %entryKey)\n"
                    "      %thenSame = ctjs.compare strict_eq %branchSaved, %value\n"
                  : "      %branchObject = ctjs.create_object\n"
                    "      %branchStored = ctjs.call %setter(%state, %entryKey, %branchObject)\n";
        const std::string branch = "    %flag = ctjs.truthy %entryKey\n"
                                   "    scf.if %flag {\n" +
                                   thenBody +
                                   "      scf.yield\n"
                                   "    } else {\n"
                                   "      %elseSame = ctjs.compare strict_eq %saved, %value\n"
                                   "      scf.yield\n"
                                   "    }\n"
                                   "    %same =";
        auto module = mlir::parseSourceString<mlir::ModuleOp>(replaced(same, "    %same =", branch),
                                                              &context);
        check(static_cast<bool>(module), "leaf scope mutation fixture parses before mutation");
        if (!module) { continue; }
        mlir::Builder attributes(&context);
        module->walk([&](mlir::Operation * operation) {
            operation->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.map_present", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.object_origin", attributes.getStringAttr("same"));
        });
        const auto contract = requested(*module);
        OwnedGlobalRoots checked(*module, contract);
        check(checked.proved(),
              "valid branch-local object operations retain their owning source proof");
        if (!checked.proved()) { continue; }
        census(*module, checked);
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        mlir::scf::IfOp conditional;
        ctjs::CompareOp outer;
        setter.walk([&](mlir::scf::IfOp operation) { conditional = operation; });
        setter.walk([&](ctjs::CompareOp operation) {
            if (operation->getBlock() == &setter.getBody().front()) { outer = operation; }
        });
        ctjs::CreateObjectOp leaf;
        ctjs::CallOp get;
        conditional.getThenRegion().walk([&](ctjs::CreateObjectOp operation) { leaf = operation; });
        conditional.getThenRegion().walk([&](ctjs::CallOp operation) {
            if (operation.getArgs().size() == 1) { get = operation; }
        });
        auto compared =
            alias ? llvm::cast<ctjs::CompareOp>(conditional.getElseRegion().front().front())
                  : outer;
        const unsigned operand = alias ? 0u : 1u;
        const auto original = compared->getOperand(operand);
        compared->setOperand(operand, alias ? get.getResult() : leaf.getResult());
        OwnedGlobalRoots stale(*module, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*module, stale),
              "a cross-scope identity operand invalidates the previous source fingerprint");
        OwnedGlobalRoots fresh(*module, requested(*module));
        check(!fresh.proved() && !fresh.exhausted() && empty(*module, fresh),
              "forged origin reports cannot authorize a then-only object outside its SSA scope");
        compared->setOperand(operand, original);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring the actual in-scope operand restores the independent object proof");
    }

    for (const auto & [text, label] : {std::pair{same, "identity"}, {savedAfter, "saved field"}}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "leaf readback owner budget fixture parses");
        if (!module) { continue; }
        auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        const unsigned completion = query.steps();
        check(query.proved() && completion < 25000, "the complete readback owner proof is bounded");
        if (!query.proved() || completion >= 25000) { continue; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            OwnedGlobalRoots limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      empty(*module, limited),
                  "every incomplete readback owner budget withholds the whole publication");
        }
        OwnedGlobalRoots exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion,
              "the exact readback owner budget retains the entire source family");
        if (exact.proved()) { census(*module, exact); }
        mlir::Builder attributes(&context);
        module->walk([&](mlir::Operation * operation) {
            operation->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.map_present", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.map_read_type", attributes.getStringAttr("object"));
            operation->setAttr("ctnative.object_schema", attributes.getStringAttr("leaf"));
            operation->setAttr("ctnative.object_origin", attributes.getStringAttr("same"));
        });
        contract = requested(*module);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "reports never replace the independent owning readback proof");
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        ctjs::CallOp get;
        ctjs::GetPropertyOp fieldRead;
        ctjs::CompareOp compare;
        ctjs::ConstantOp fieldKey;
        setter.walk([&](ctjs::CallOp operation) {
            if (operation.getArgs().size() == 1 && !get) { get = operation; }
        });
        setter.walk([&](ctjs::GetPropertyOp operation) {
            if (operation.getObject().getDefiningOp<ctjs::CallOp>()) { fieldRead = operation; }
        });
        setter.walk([&](ctjs::CompareOp operation) { compare = operation; });
        setter.walk([&](ctjs::ConstantOp operation) {
            auto name = llvm::dyn_cast<ctjs::StringAttr>(operation.getValue());
            if (name && name.getValue() == "value") { fieldKey = operation; }
        });
        check(get && fieldKey,
              "the published owner retains its real source readback and field key");
        if (!get || !fieldKey) { continue; }
        unsigned mutations = 0;
        const auto mutation = [&](mlir::Operation * operation, unsigned operand,
                                  mlir::Value value) {
            ++mutations;
            const auto original = operation->getOperand(operand);
            operation->setOperand(operand, value);
            OwnedGlobalRoots stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      empty(*module, stale),
                  "changing a live readback source invalidates the old owner fingerprint");
            OwnedGlobalRoots fresh(*module, requested(*module));
            check(!fresh.proved() && !fresh.exhausted() && empty(*module, fresh),
                  "a fresh fingerprint cannot turn forged object origin reports into an owner");
            operation->setOperand(operand, original);
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring the actual source restores the entire independent owner");
        };
        mutation(get, 2, fieldKey.getResult());
        auto returned = llvm::cast<ctjs::ReturnOp>(setter.getBody().front().getTerminator());
        mutation(returned, 0, get.getResult());
        if (compare) { mutation(compare, 1, setter.getBody().front().getArgument(0)); }
        if (fieldRead) {
            mutation(fieldRead, 0, setter.getBody().front().getArgument(0));
            mutation(fieldRead, 1, setter.getBody().front().getArgument(lifted ? 4 : 3));
        }
        std::printf(
            "leaf readback owner %s %s: %u rows, %u live mutations, all %u budgets checked\n",
            lifted ? "prepared" : "source", label, rows, mutations, completion);
    }
}

void checkLeafOwner(mlir::MLIRContext & context, const std::string & source, bool lifted) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const std::string write = "    %written = ctjs.call %setter(%state, %entryKey, %value)";
    const auto numeric =
        replaced(source, write,
                 "    %fieldKey = ctjs.constant #ctjs.string<\"value\">\n"
                 "    %fieldValue = ctjs.constant #ctjs.number<4607182418800017408>\n"
                 "    ctjs.set_property %value[%fieldKey], %fieldValue\n" +
                     write);
    for (const auto & [program, hasField] : {std::pair{source, false}, {numeric, true}}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "source/prepared leaf owner fixture parses");
        if (!module) { continue; }
        auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        check(query.proved() && query.roots().size() == 1,
              "a wrapper publishes a complete owner for repeated method-local leaf allocations");
        if (!query.proved() || query.roots().empty()) {
            std::fprintf(stderr, "leaf owner %s %s: %s\n", lifted ? "prepared" : "source",
                         hasField ? "field" : "empty", query.reason().str().c_str());
            continue;
        }
        const auto & table = *query.roots().front().methodTable;
        const auto & capture = *table.capturedMap;
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        const auto expected = Alternatives::forTag(mlir::TypeID::get<ctjs::StringAttr>());
        bool complete =
            table.methods.size() == 2 && table.calls.size() == 4 && capture.closures.size() == 2 &&
            capture.parameters.size() == 2 && capture.calls.size() == 1 &&
            capture.reads.size() == 3 && capture.upvalues.size() == (lifted ? 0u : 2u) &&
            capture.leafObjects.size() == 1 && capture.leafWrites.size() == (hasField ? 1u : 0u) &&
            capture.leafReads.empty();
        unsigned setters = 0;
        mlir::Operation * previous = nullptr;
        for (const auto & edge : table.calls) {
            complete &= edge.capturedMap && (!previous || previous->isBeforeInBlock(edge.call));
            previous = edge.call;
            if (!edge.capturedMap) { continue; }
            complete &= edge.capturedMap->allocation == capture.allocation &&
                        edge.capturedMap->calls == capture.calls &&
                        edge.capturedMap->reads == capture.reads &&
                        edge.capturedMap->parameters == capture.parameters &&
                        edge.capturedMap->leafObjects == capture.leafObjects &&
                        edge.capturedMap->leafWrites == capture.leafWrites &&
                        edge.capturedMap->leafReads == capture.leafReads;
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
                argument.parameter == setter.getBody().front().getArgument(lifted ? 4 : 3) &&
                argument.actual == edge.call->getOperand(lifted ? 4 : 2);
        }
        check(complete && setters == 3,
              "one owner retains all sibling effects and repeated/future String setter actuals");
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "leaf ownership does not move allocations into the entry or rewrite source calls");
        const unsigned completion = query.steps();
        check(completion < 15000, "leaf owner proof stays within the fixture work bound");
        if (completion >= 15000) { continue; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            OwnedGlobalRoots limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      empty(*module, limited),
                  "every incomplete leaf owner budget withholds the whole published family");
        }
        OwnedGlobalRoots exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion,
              "the exact leaf owner budget reproduces the entire publication chain");
        ctjs::CreateObjectOp leaf;
        ctjs::SetPropertyOp field;
        setter.walk([&](ctjs::CreateObjectOp operation) { leaf = operation; });
        setter.walk([&](ctjs::SetPropertyOp operation) { field = operation; });
        check(leaf && static_cast<bool>(field) == hasField,
              "the owner retains the exact local allocation and its actual own-field write");
        if (!leaf) { continue; }
        check(
            capture.leafObjects == std::vector{leaf} &&
                capture.leafWrites ==
                    (hasField ? std::vector{field} : std::vector<ctjs::SetPropertyOp>{}),
            "source leaf handles occur once without entry allocations or scratch-body duplicates");
        auto store = capture.calls.front();
        check(store.getArgs().back() == leaf.getResult(),
              "the stored leaf is the current method allocation, never a provider token");
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
        check(OwnedGlobalRoots(*module, contract).proved(),
              "previous object and owner reports never replace the independent source query");
        const auto mutation = [&](mlir::Operation * operation, unsigned operand,
                                  mlir::Value value) {
            const auto saved = operation->getOperand(operand);
            operation->setOperand(operand, value);
            OwnedGlobalRoots stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      empty(*module, stale),
                  "a changed leaf source invalidates the original owner fingerprint");
            OwnedGlobalRoots fresh(*module, requested(*module));
            check(!fresh.proved() && !fresh.exhausted() && empty(*module, fresh),
                  "forged object reports cannot restore ownership of an unsafe fresh source");
            operation->setOperand(operand, saved);
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring the source restores its independently checked leaf owner");
        };
        mutation(store, 2, leaf.getResult());
        mutation(store, 3, store.getReceiver());
        auto returned = llvm::cast<ctjs::ReturnOp>(setter.getBody().front().getTerminator());
        mutation(returned, 0, leaf.getResult());
        auto sibling = module->lookupSymbol<ctjs::FuncOp>("get$3");
        auto siblingReturn = llvm::cast<ctjs::ReturnOp>(sibling.getBody().front().getTerminator());
        mutation(siblingReturn, 0, sibling.getBody().front().getArgument(0));
        if (field) {
            mutation(field, 0, setter.getBody().front().getArgument(0));
            mutation(field, 1, setter.getBody().front().getArgument(lifted ? 4 : 3));
            mutation(field, 2, leaf.getResult());
        }
        std::printf("leaf owner %s %s: all %u incomplete budgets checked\n",
                    lifted ? "prepared" : "source", hasField ? "field" : "empty", completion);
    }
    const auto refuse = [&](const std::string & program, const char * message) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "source/prepared leaf owner refusal parses");
        if (!module) { return; }
        OwnedGlobalRoots query(*module, requested(*module));
        check(!query.proved() && !query.exhausted() && empty(*module, query), message);
    };
    refuse(replaced(numeric, "ctjs.set_property %value[%fieldKey], %fieldValue",
                    "ctjs.set_property %value[%fieldKey], %state"),
           "a leaf field retaining its owning Map is not an acyclic owner");
    refuse(replaced(numeric, "#ctjs.number<4607182418800017408>", "#ctjs.string<\"owned\">"),
           "owning String fields still need a separate native object carrier");
    refuse(replaced(numeric, "#ctjs.string<\"value\">", "#ctjs.string<\"__proto__\">"),
           "a prototype field cannot be published as an ordinary leaf owner");
    refuse(
        replaced(source, "%value = ctjs.create_object", "%value = ctjs.load_global \"external\""),
        "an external object cannot borrow a local leaf allocation identity");
    refuse(replaced(source,
                    "    %key = ctjs.constant #ctjs.string<\"size\">\n"
                    "    %size = ctjs.get_property %state[%key]",
                    "    %key = ctjs.constant #ctjs.string<\"get\">\n"
                    "    %reader = ctjs.get_property %state[%key]\n"
                    "    %entryKey = ctjs.constant #ctjs.string<\"x\">\n"
                    "    %size = ctjs.call %reader(%state, %entryKey)"),
           "a separate sibling cannot publish an unproved object-bearing Map result");
}

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

void checkSharedMap(mlir::MLIRContext & context) {
    auto source = replaced(capturedFixture, "ctjs.call_direct @make$2(%u, %u, %factory)",
                           "ctjs.call %factory(%u)");
    source = replaced(source, "    ctjs.return %table",
                      "    %putter = ctjs.create_closure %callee[4] this %u captures %cell\n"
                      "    %putKey = ctjs.constant #ctjs.string<\"put\">\n"
                      "    ctjs.set_property %table[%putKey], %putter\n"
                      "    ctjs.return %table");
    source = replaced(source, "\n}\n", R"MLIR(
  ctjs.func private @put$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %state = ctjs.load_upvalue %callee[0]
    %setKey = ctjs.constant #ctjs.string<"set">
    %setter = ctjs.get_property %state[%setKey]
    %entryKey = ctjs.constant #ctjs.string<"x">
    %value = ctjs.constant #ctjs.number<4607182418800017408>
    %written = ctjs.call %setter(%state, %entryKey, %value)
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
}
)MLIR");
    source = replaced(source, "    %answer = ctjs.call %getter(%owned)",
                      "    %putKey = ctjs.constant #ctjs.string<\"put\">\n"
                      "    %putter = ctjs.get_property %owned[%putKey]\n"
                      "    %putResult = ctjs.call %putter(%owned)\n"
                      "    %answer = ctjs.call %getter(%owned)");
    checkEntryNumericOwner(context, source);
    const auto replaceAll = [](std::string text, llvm::StringRef from, llvm::StringRef to) {
        std::size_t offset = 0;
        while ((offset = text.find(from.str(), offset)) != std::string::npos) {
            text.replace(offset, from.size(), to.str());
            offset += to.size();
        }
        return text;
    };
    const auto prepare = [&](std::string text) {
        text =
            replaced(text, "ctjs.call %factory(%u)", "ctjs.call_direct @make$2(%u, %u, %factory)");
        text = replaced(text, "    %cell = ctjs.create_cell %u\n", "");
        text = replaced(text, "    ctjs.cell_set %cell, %state\n", "");
        text = replaceAll(text, "captures %cell", "captures %state");
        text = replaceAll(text, "    %state = ctjs.load_upvalue %callee[0]\n", "");
        text = replaceAll(text, "upvalue_count = 1 : i32", "upvalue_count = 0 : i32");
        for (const auto & [name, closure, result] : {std::tuple{"get$3", "getter", "answer"},
                                                     {"put$4", "putter", "putResult"},
                                                     {"has$5", "hasMethod", "hasResult"}}) {
            const auto signature = std::string("@") + name +
                                   "(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value";
            if (text.find(signature) == std::string::npos) { continue; }
            text = replaced(text, signature, signature + ", %state: !ctjs.value");
            const auto call = std::string("%") + result + " = ctjs.call %" + closure + "(%owned";
            const auto lifted = std::string("%") + closure + "Env = ctjs.load_upvalue %" + closure +
                                "[0]\n    %" + result + " = ctjs.call_direct @" + name +
                                "(%owned, %u, %" + closure + ", %" + closure + "Env";
            text = replaced(text, call, lifted);
        }
        return text;
    };
    auto three = replaced(source, "    ctjs.return %table",
                          "    %hasMethod = ctjs.create_closure %callee[5] this %u captures %cell\n"
                          "    %hasKey = ctjs.constant #ctjs.string<\"has\">\n"
                          "    ctjs.set_property %table[%hasKey], %hasMethod\n"
                          "    ctjs.return %table");
    three = replaced(three, "\n}\n", R"MLIR(
  ctjs.func private @has$5(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %state = ctjs.load_upvalue %callee[0]
    %key = ctjs.constant #ctjs.string<"has">
    %method = ctjs.get_property %state[%key]
    %entryKey = ctjs.constant #ctjs.string<"x">
    %found = ctjs.call %method(%state, %entryKey)
    ctjs.return %found
  }
}
)MLIR");
    three = replaced(three, "    %answer = ctjs.call %getter(%owned)",
                     "    %hasKey = ctjs.constant #ctjs.string<\"has\">\n"
                     "    %hasMethod = ctjs.get_property %owned[%hasKey]\n"
                     "    %hasResult = ctjs.call %hasMethod(%owned)\n"
                     "    %answer = ctjs.call %getter(%owned)");
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    for (const auto & [program, members, lifted] : {std::tuple{source, 2u, false},
                                                    {prepare(source), 2u, true},
                                                    {three, 3u, false},
                                                    {prepare(three), 3u, true}}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "shared Map source and prepared fixtures parse");
        if (!module) { continue; }
        const auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        if (!query.proved()) {
            std::fprintf(stderr, "shared owner: %s\n", query.reason().str().c_str());
        }
        check(query.proved() && query.roots().size() == 1,
              "every fixed method shares the same completely checked ordinary owner");
        if (!query.proved() || query.roots().empty()) { continue; }
        const auto & table = *query.roots().front().methodTable;
        const auto & capture = *table.capturedMap;
        check(table.methods.size() == members && table.calls.size() == members &&
                  capture.closures.size() == members && capture.reads.size() == members &&
                  capture.calls.size() == members - 1 &&
                  capture.upvalues.size() == (lifted ? 0 : members),
              "shared owner records all method, capture, body and current-call edges");
        for (const auto & edge : table.calls) {
            check(edge.capturedMap && edge.capturedMap->allocation == capture.allocation &&
                      edge.capturedMap->closures == capture.closures &&
                      edge.capturedMap->reads == capture.reads &&
                      edge.capturedMap->calls == capture.calls &&
                      static_cast<bool>(edge.capturedMap->argument) == lifted,
                  "all actual calls carry the same full family and their own lifted argument");
        }
        const unsigned completion = query.steps();
        check(completion < 10000, "shared Map source proof is bounded");
        if (completion < 10000) {
            for (unsigned budget = 0; budget < completion; ++budget) {
                OwnedGlobalRoots limited(*module, contract, budget);
                check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                          empty(*module, limited),
                      "every incomplete shared-owner budget exposes no partial source graph");
            }
            check(OwnedGlobalRoots(*module, contract, completion).proved(),
                  "exact shared-owner completion budget reproduces all methods");
        }
        mlir::Builder attributes(&context);
        (*module)->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
        check(OwnedGlobalRoots(*module, contract).proved(),
              "shared ownership rederives its family despite forged report attributes");
        auto mutation = capture.calls.front();
        const auto originalValue = mutation.getArgs().back();
        mutation->setOperand(3, mutation.getReceiver());
        OwnedGlobalRoots stale(*module, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*module, stale),
              "a changed sibling body invalidates the original shared-owner fingerprint");
        OwnedGlobalRoots changed(*module, requested(*module));
        check(!changed.proved() && empty(*module, changed),
              "a fresh shared-owner fingerprint cannot authorize a sibling Map cycle");
        mutation->setOperand(3, originalValue);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring the sibling method restores its live shared-owner proof");
        auto sibling = module->lookupSymbol<ctjs::FuncOp>("put$4");
        auto returned = llvm::cast<ctjs::ReturnOp>(sibling.getBody().front().getTerminator());
        const auto originalReturn = returned.getValue();
        for (unsigned implicit : {0u, 1u}) {
            returned->setOperand(0, sibling.getBody().front().getArgument(implicit));
            OwnedGlobalRoots oldReceiver(*module, contract);
            check(!oldReceiver.proved() && oldReceiver.reason().contains("fingerprint") &&
                      empty(*module, oldReceiver),
                  "a sibling implicit-receiver mutation invalidates the original fingerprint");
            OwnedGlobalRoots newReceiver(*module, requested(*module));
            check(!newReceiver.proved() && empty(*module, newReceiver),
                  "every sibling must remain independent of this and new.target");
            returned->setOperand(0, originalReturn);
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring sibling receiver independence restores the live family");
        }
        if (!lifted) {
            auto upvalue = llvm::cast<ctjs::LoadUpvalueOp>(sibling.getBody().front().front());
            upvalue.setIndexAttr(attributes.getI32IntegerAttr(1));
            OwnedGlobalRoots oldSlot(*module, contract);
            check(!oldSlot.proved() && oldSlot.reason().contains("fingerprint") &&
                      empty(*module, oldSlot),
                  "a sibling capture-index mutation invalidates the original fingerprint");
            OwnedGlobalRoots newSlot(*module, requested(*module));
            check(!newSlot.proved() && empty(*module, newSlot),
                  "an earlier sibling proof cannot authorize another environment slot");
            upvalue.setIndexAttr(attributes.getI32IntegerAttr(0));
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring the sibling environment index restores the live family");
        }
        std::printf("shared Map %u-method %s proof and all %u incomplete budgets checked\n",
                    members, lifted ? "prepared" : "source", completion);
    }
    const auto refuse = [&](std::string program, const char * message) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "shared Map refusal fixture parses");
        if (!module) { return; }
        OwnedGlobalRoots query(*module, requested(*module));
        check(!query.proved() && !query.exhausted() && empty(*module, query), message);
    };
    refuse(replaced(source, "    %putResult = ctjs.call %putter(%owned)\n", ""),
           "an uncalled published sibling withholds the complete owner plan");
    refuse(replaced(source, "ctjs.call %putter(%owned)", "ctjs.call %putter(%host)"),
           "every method needs its actual receiver in the checked table family");
    refuse(replaced(source, "ctjs.call %putter(%owned)", "ctjs.call %putter(%owned, %u)"),
           "surplus actuals cannot extend the source method signature");
    refuse(replaced(source, "ctjs.set_property %table[%putKey], %putter",
                    "ctjs.set_property %table[%key], %putter"),
           "fixed shared methods cannot replace each other");
    refuse(replaced(source, "%putter = ctjs.create_closure %callee[4] this %u captures %cell",
                    "%putter = ctjs.create_closure %callee[3] this %u captures %cell"),
           "two closure creations cannot silently merge one source method identity");
    refuse(replaced(source, "ctjs.call %setter(%state, %entryKey, %value)",
                    "ctjs.call %setter(%state, %entryKey, %state)"),
           "every captured method participates in the primitive contents proof");
    refuse(replaced(source, "    %written = ctjs.call %setter(%state, %entryKey, %value)",
                    "    ctjs.store_global \"escaped\", %state\n"
                    "    %written = ctjs.call %setter(%state, %entryKey, %value)"),
           "a sibling cannot separately publish the shared Map");
    refuse(replaced(source, "    ctjs.set_property %table[%putKey], %putter",
                    "    ctjs.set_property %table[%putKey], %putter\n"
                    "    ctjs.store_global \"escapedMethod\", %putter"),
           "a sibling callable cannot acquire an unchecked export alias");
    refuse(replaced(source, "    ctjs.cell_set %cell, %state",
                    "    ctjs.cell_set %cell, %state\n    ctjs.cell_set %cell, %u"),
           "all family members depend on one immutable Map slot");
    refuse(replaced(prepare(source), "%putterEnv = ctjs.load_upvalue %putter[0]",
                    "%putterEnv = ctjs.load_upvalue %getter[0]"),
           "prepared sibling arguments must come from their own current callable");
    auto mixed =
        replaced(source, "@put$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)",
                 "@put$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                 "%state: !ctjs.value)");
    mixed = replaced(mixed,
                     "attributes {upvalue_count = 1 : i32} {\n"
                     "    %state = ctjs.load_upvalue %callee[0]\n"
                     "    %setKey = ctjs.constant",
                     "attributes {upvalue_count = 0 : i32} {\n"
                     "    %setKey = ctjs.constant");
    mixed = replaced(mixed, "%putResult = ctjs.call %putter(%owned)",
                     "%putterEnv = ctjs.load_upvalue %putter[0]\n"
                     "    %putResult = ctjs.call_direct @put$4(%owned, %u, %putter, %putterEnv)");
    refuse(mixed, "partially lifted sibling signatures cannot publish a complete family proof");
    mixed = replaced(mixed, "%putter = ctjs.create_closure %callee[4] this %u captures %cell",
                     "%putter = ctjs.create_closure %callee[4] this %u captures %state");
    refuse(mixed, "raw-resource and original-cell siblings cannot mix capture ownership stages");

    auto parameterized =
        replaced(source, "@put$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)",
                 "@put$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                 "%entryKey: !ctjs.value)");
    parameterized =
        replaced(parameterized, "    %entryKey = ctjs.constant #ctjs.string<\"x\">\n", "");
    parameterized = replaced(parameterized, "    %putResult = ctjs.call %putter(%owned)",
                             "    %actual = ctjs.constant #ctjs.string<\"x\">\n"
                             "    %putResult = ctjs.call %putter(%owned, %actual)");
    auto leaf = replaced(parameterized, "%value = ctjs.constant #ctjs.number<4607182418800017408>",
                         "%value = ctjs.create_object");
    leaf = replaced(leaf,
                    "    %u = ctjs.constant #ctjs.undefined\n"
                    "    ctjs.return %u\n  }\n}\n",
                    "    %sizeKey = ctjs.constant #ctjs.string<\"size\">\n"
                    "    %size = ctjs.get_property %state[%sizeKey]\n"
                    "    ctjs.return %size\n  }\n}\n");
    leaf = replaced(leaf, "    %answer = ctjs.call %getter(%owned)",
                    "    %repeatPutter = ctjs.get_property %owned[%putKey]\n"
                    "    %repeatResult = ctjs.call %repeatPutter(%owned, %actual)\n"
                    "    %future = ctjs.constant #ctjs.string<\"y\">\n"
                    "    %laterPutter = ctjs.get_property %owned[%putKey]\n"
                    "    %laterResult = ctjs.call %laterPutter(%owned, %future)\n"
                    "    %answer = ctjs.call %getter(%owned)");
    for (const bool lifted : {false, true}) {
        auto program = lifted ? prepare(leaf) : leaf;
        if (lifted) {
            for (const auto & [name, argument] :
                 {std::pair{"repeat", "actual"}, {"later", "future"}}) {
                const auto call = std::string("%") + name + "Result = ctjs.call %" + name +
                                  "Putter(%owned, %" + argument + ")";
                const auto direct = std::string("%") + name + "Env = ctjs.load_upvalue %" + name +
                                    "Putter[0]\n    %" + name +
                                    "Result = ctjs.call_direct @put$4(%owned, %u, %" + name +
                                    "Putter, %" + name + "Env, %" + argument + ")";
                program = replaced(program, call, direct);
            }
        }
        checkLeafOwner(context, program, lifted);
        checkLeafReadbackOwner(context, program, lifted);
        checkDefiniteMapAbsenceOwner(context, program, lifted);
        checkCapturedMapClearOwner(context, program, lifted);
    }
    for (const bool lifted : {false, true}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            lifted ? prepare(parameterized) : parameterized, &context);
        check(static_cast<bool>(module), "parameterized shared source and prepared forms parse");
        if (!module) { continue; }
        const auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        if (!query.proved()) {
            std::fprintf(stderr, "parameter owner: %s\n", query.reason().str().c_str());
        }
        check(query.proved(), "a current primitive actual proves the explicit Map parameter");
        if (!query.proved()) { continue; }
        const auto & table = *query.roots().front().methodTable;
        check(table.calls.size() == 2 && table.capturedMap->parameters.size() == 2,
              "parameter proof retains the complete shared method family");
        auto edge = table.calls.front();
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        const auto tag = mlir::TypeID::get<ctjs::StringAttr>();
        check(edge.function == setter && edge.arguments.size() == 1 &&
                  edge.arguments.front().parameter ==
                      setter.getBody().front().getArgument(lifted ? 4 : 3) &&
                  edge.arguments.front().actual == edge.call->getOperand(lifted ? 4 : 2) &&
                  edge.arguments.front().alternatives.tag() == tag &&
                  table.calls.back().arguments.empty() &&
                  table.capturedMap->parameters.back().function == setter &&
                  table.capturedMap->parameters.back().alternatives ==
                      std::vector{ctcompile::ctnative::PrimitiveAlternatives::forTag(tag)},
              "formal/actual SSA evidence separates the Map environment from the explicit key");
        const auto actual = edge.arguments.front().actual;
        const unsigned operand = lifted ? 4u : 2u;
        for (mlir::Value replacement : {edge.read.getResult(), edge.read.getObject()}) {
            edge.call->setOperand(operand, replacement);
            OwnedGlobalRoots stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      empty(*module, stale),
                  "changing an actual invalidates its supplied source fingerprint");
            OwnedGlobalRoots fresh(*module, requested(*module));
            check(!fresh.proved() && empty(*module, fresh),
                  "fresh fingerprints cannot turn callable or object actuals into primitives");
        }
        edge.call->setOperand(operand, actual);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring the actual restores the proof");

        mlir::OpBuilder builder(&context);
        builder.setInsertionPoint(edge.call);
        auto different = ctjs::ConstantOp::create(builder, edge.call->getLoc(),
                                                  ctjs::StringAttr::get(&context, "different"));
        edge.call->setOperand(operand, different.getResult());
        OwnedGlobalRoots changed(*module, requested(*module));
        check(changed.proved() &&
                  changed.roots().front().methodTable->calls.front().arguments.front().actual ==
                      different.getResult(),
              "a different key of the same type retains its own live SSA actual");
        edge.call->setOperand(operand, actual);
        different.erase();
        const unsigned completion = query.steps();
        check(completion < 10000, "parameterized family proof remains bounded");
        if (completion < 10000) {
            for (unsigned budget = 0; budget < completion; ++budget) {
                OwnedGlobalRoots limited(*module, contract, budget);
                check(!limited.proved() && limited.exhausted() && empty(*module, limited),
                      "incomplete argument census never publishes a partial owning plan");
            }
            check(OwnedGlobalRoots(*module, contract, completion).proved(),
                  "exact argument census budget reproduces the complete proof");
        }
        std::printf("parameter Map %s proof and all %u incomplete budgets checked\n",
                    lifted ? "prepared" : "source", completion);
    }
    refuse(
        replaced(parameterized, "ctjs.call %putter(%owned, %actual)", "ctjs.call %putter(%owned)"),
        "a missing primitive actual is unproved");
    refuse(replaced(parameterized, "ctjs.call %putter(%owned, %actual)",
                    "ctjs.call %putter(%owned, %actual, %actual)"),
           "a surplus primitive actual is unproved");
    refuse(replaced(parameterized, "    %putResult = ctjs.call %putter(%owned, %actual)", ""),
           "an uncalled parameterized sibling has no independently proved parameter tags");
    refuse(replaced(parameterized, "    %answer = ctjs.call %getter(%owned)",
                    "    %incompatible = ctjs.constant #ctjs.boolean<false>\n"
                    "    %second = ctjs.call %putter(%owned, %incompatible)\n"
                    "    %answer = ctjs.call %getter(%owned)"),
           "a Boolean actual cannot join the String parameter family");
    auto nested = replaced(parameterized, "%setter(%state, %entryKey, %value)",
                           "%setter(%state, %entryKey, %entryKey)");
    nested = replaced(nested,
                      "    %u = ctjs.constant #ctjs.undefined\n"
                      "    ctjs.return %u\n  }\n}\n",
                      "    %getKey = ctjs.constant #ctjs.string<\"get\">\n"
                      "    %reader = ctjs.get_property %state[%getKey]\n"
                      "    %loaded = ctjs.call %reader(%state, %entryKey)\n"
                      "    ctjs.return %loaded\n  }\n}\n");
    nested = replaced(nested, "    %answer = ctjs.call %getter(%owned)",
                      "    %outerPutter = ctjs.get_property %owned[%putKey]\n"
                      "    %outerResult = ctjs.call %outerPutter(%owned, %putResult)\n"
                      "    %answer = ctjs.call %getter(%owned)");
    for (const bool lifted : {false, true}) {
        auto program = lifted ? prepare(nested) : nested;
        if (lifted) {
            program = replaced(program, "%outerResult = ctjs.call %outerPutter(%owned, %putResult)",
                               "%outerEnv = ctjs.load_upvalue %outerPutter[0]\n"
                               "    %outerResult = ctjs.call_direct @put$4(%owned, %u, "
                               "%outerPutter, %outerEnv, %putResult)");
        }
        checkNestedOwner(context, program, lifted);
    }
    auto fromResult = replaced(parameterized, "    %putResult = ctjs.call %putter(%owned, %actual)",
                               "    %priorGetter = ctjs.get_property %owned[%key]\n"
                               "    %prior = ctjs.call %priorGetter(%owned)\n"
                               "    %putResult = ctjs.call %putter(%owned, %prior)");
    for (const unsigned seeded : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u}) {
        for (const bool lifted : {false, true}) {
            auto sourceResult = fromResult;
            if (seeded) {
                sourceResult =
                    replaced(sourceResult, "    ctjs.return %size",
                             "    %seedKey = ctjs.constant #ctjs.number<0>\n"
                             "    %seedValue = ctjs.constant #ctjs.number<4607182418800017408>\n"
                             "    %seedSetKey = ctjs.constant #ctjs.string<\"set\">\n"
                             "    %seedSet = ctjs.get_property %state[%seedSetKey]\n"
                             "    %seeded = ctjs.call %seedSet(%state, %seedKey, %seedValue)\n"
                             "    %seedGetKey = ctjs.constant #ctjs.string<\"get\">\n"
                             "    %seedGet = ctjs.get_property %state[%seedGetKey]\n"
                             "    %loaded = ctjs.call %seedGet(%state, %seedKey)\n"
                             "    ctjs.return %loaded");
            }
            if (seeded >= 2 && seeded < 6) {
                std::string mutation =
                    seeded == 2
                        ? "    %other = ctjs.call %seedSet(%state, %seedValue, %seedValue)\n"
                    : seeded == 3
                        ? "    %deleteKey = ctjs.constant #ctjs.string<\"delete\">\n"
                          "    %deleter = ctjs.get_property %state[%deleteKey]\n"
                          "    %deleted = ctjs.call %deleter(%state, %seedValue)\n"
                        : "    %otherKey = ctjs.get_property %state[%key]\n"
                          "    %other = ctjs.call %seedSet(%state, %otherKey, %seedValue)\n";
                if (seeded == 5) {
                    mutation += "    %thirdKey = ctjs.get_property %state[%key]\n"
                                "    %third = ctjs.call %seedSet(%state, %thirdKey, %seedKey)\n";
                }
                sourceResult = replaced(sourceResult, "    %seedGetKey = ctjs.constant",
                                        std::string(mutation) + "    %seedGetKey = ctjs.constant");
            }
            if (seeded >= 6) {
                sourceResult =
                    replaced(sourceResult,
                             "    %seedValue = ctjs.constant #ctjs.number<4607182418800017408>",
                             R"MLIR(
    %nonempty = ctjs.truthy %size
    %seedValue = scf.if %nonempty -> (!ctjs.value) {
      %text = ctjs.constant #ctjs.string<"owned">
      scf.yield %text : !ctjs.value
    } else {
      %null = ctjs.constant #ctjs.null
      scf.yield %null : !ctjs.value
    }
)MLIR");
                if (seeded == 7) {
                    sourceResult = replaced(sourceResult, "    %seedGetKey = ctjs.constant",
                                            "    %null = ctjs.constant #ctjs.null\n"
                                            "    %aliased = ctjs.call %seedSet(%state, "
                                            "%size, %null)\n"
                                            "    %seedGetKey = ctjs.constant");
                }
                if (seeded == 8) {
                    sourceResult = replaced(sourceResult, "    ctjs.return %loaded", R"MLIR(
    %replacement = ctjs.constant #ctjs.boolean<false>
    %overwritten = ctjs.call %seedSet(%state, %seedKey, %replacement)
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %deleted = ctjs.call %deleter(%state, %seedKey)
    ctjs.return %loaded
)MLIR");
                }
            }
            auto program = lifted ? prepare(sourceResult) : sourceResult;
            if (lifted) {
                program = replaced(
                    program, "%prior = ctjs.call %priorGetter(%owned)",
                    "%priorEnv = ctjs.load_upvalue %priorGetter[0]\n"
                    "    %prior = ctjs.call_direct @get$3(%owned, %u, %priorGetter, %priorEnv)");
            }
            auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
            check(static_cast<bool>(module), "source/prepared result-argument fixtures parse");
            if (!module) { continue; }
            const auto contract = requested(*module);
            OwnedGlobalRoots query(*module, contract);
            check(query.proved(),
                  "an independently proved result supplies the consuming formal tag");
            if (!query.proved()) { continue; }
            const auto & calls = query.roots().front().methodTable->calls;
            using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
            const auto expected =
                seeded >= 6 ? Alternatives{Alternatives::String,
                                           Alternatives::String | Alternatives::Null, true}
                            : Alternatives::forTag(mlir::TypeID::get<ctjs::NumberAttr>());
            check(calls.size() == 3 && calls[1].arguments.size() == 1 &&
                      calls[1].arguments.front().actual == calls[0].call->getResult(0) &&
                      calls[1].arguments.front().alternatives == expected &&
                      calls[0].call->isBeforeInBlock(calls[1].call) &&
                      calls[1].call->isBeforeInBlock(calls[2].call),
                  "initial getter, setter and final getter retain their original SSA order");
            if (seeded >= 6) {
                const auto & table = *query.roots().front().methodTable;
                auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
                unsigned summaries = 0;
                for (const auto & parameters : table.capturedMap->parameters) {
                    if (parameters.function != setter) { continue; }
                    ++summaries;
                    check(parameters.alternatives == std::vector{expected},
                          "the owner retains the complete nullable consumer parameter family");
                }
                check(summaries == 1,
                      "the nullable consumer has exactly one independently proved family");
            }
            const unsigned completion = query.steps();
            check(completion < 10000, "result dependency proof remains bounded");
            if (completion < 10000) {
                for (unsigned budget = 0; budget < completion; ++budget) {
                    OwnedGlobalRoots limited(*module, contract, budget);
                    check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                              empty(*module, limited),
                          "every incomplete result budget withholds the whole family");
                }
                check(OwnedGlobalRoots(*module, contract, completion).proved(),
                      "the exact result dependency completion budget reproduces all calls");
            }
            auto getter = module->lookupSymbol<ctjs::FuncOp>("get$3");
            auto returned = llvm::cast<ctjs::ReturnOp>(getter.getBody().front().getTerminator());
            const auto saved = returned.getValue();
            mlir::OpBuilder builder(&context);
            builder.setInsertionPoint(returned);
            auto boolean = ctjs::ConstantOp::create(builder, returned.getLoc(),
                                                    ctjs::BooleanAttr::get(&context, true));
            returned->setOperand(0, boolean.getResult());
            OwnedGlobalRoots stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      empty(*module, stale),
                  "a producing return mutation invalidates the supplied fingerprint");
            OwnedGlobalRoots changed(*module, requested(*module));
            check(changed.proved() &&
                      changed.roots()
                              .front()
                              .methodTable->calls[1]
                              .arguments.front()
                              .alternatives.tag() == mlir::TypeID::get<ctjs::BooleanAttr>(),
                  "a fresh proof rederives the producer's changed tag for the consuming formal");
            returned->setOperand(0, getter.getBody().front().getArgument(0));
            OwnedGlobalRoots external(*module, requested(*module));
            check(!external.proved() && empty(*module, external),
                  "a formerly proved producer cannot authorize an external returned value");
            returned->setOperand(0, saved);
            boolean.erase();
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring the producing body restores its independent result proof");
            std::printf("%s Map %s proof and all %u incomplete budgets checked\n",
                        seeded == 8   ? "saved nullable result"
                        : seeded == 7 ? "nullable alias join"
                        : seeded == 6 ? "nullable result"
                        : seeded == 5 ? "repeated alias join"
                        : seeded == 4 ? "possible alias join"
                        : seeded == 3 ? "disjoint delete"
                        : seeded == 2 ? "per-key result"
                        : seeded      ? "seeded result"
                                      : "result",
                        lifted ? "prepared" : "source", completion);
        }
    }
    refuse(replaced(fromResult, "    ctjs.return %size", "    ctjs.return %state"),
           "a Map identity result cannot become a primitive argument");
    const auto undefinedResult =
        replaced(fromResult, "    %putResult = ctjs.call %putter(%owned, %prior)",
                 "    %seed = ctjs.call %putter(%owned, %actual)\n"
                 "    %putResult = ctjs.call %putter(%owned, %seed)");
    for (const bool lifted : {false, true}) {
        auto program = lifted ? prepare(undefinedResult) : undefinedResult;
        if (lifted) {
            program = replaced(program, "%seed = ctjs.call %putter(%owned, %actual)",
                               "%seedPutter = ctjs.get_property %owned[%putKey]\n"
                               "    %seedEnv = ctjs.load_upvalue %seedPutter[0]\n"
                               "    %seed = ctjs.call_direct @put$4(%owned, %u, %seedPutter, "
                               "%seedEnv, %actual)");
            program = replaced(
                program, "%prior = ctjs.call %priorGetter(%owned)",
                "%priorEnv = ctjs.load_upvalue %priorGetter[0]\n"
                "    %prior = ctjs.call_direct @get$3(%owned, %u, %priorGetter, %priorEnv)");
        }
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "same-method Undefined result fixture parses");
        if (!module) { continue; }
        OwnedGlobalRoots query(*module, requested(*module));
        check(query.proved() && query.roots().size() == 1,
              "an earlier checked Undefined result closes the complete String/Undefined census");
        if (!query.proved() || query.roots().empty()) { continue; }
        const auto & calls = query.roots().front().methodTable->calls;
        using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
        const Alternatives expected{Alternatives::String,
                                    Alternatives::String | Alternatives::Undefined, true};
        check(calls.size() == 4 && calls[1].arguments.size() == 1 &&
                  calls[2].arguments.size() == 1 &&
                  calls[2].arguments.front().actual == calls[1].call->getResult(0) &&
                  calls[1].arguments.front().alternatives == expected &&
                  calls[2].arguments.front().alternatives == expected &&
                  calls[1].call->isBeforeInBlock(calls[2].call),
              "the original formerly refused Undefined result edge keeps its full owning family");
    }
    refuse(replaced(prepare(parameterized), "@put$4(%owned, %u, %putter, %putterEnv, %actual)",
                    "@put$4(%owned, %u, %putter, %actual, %putterEnv)"),
           "prepared capture and explicit actual positions are not interchangeable");

    auto distinct =
        replaced(source, "%putter = ctjs.create_closure %callee[4] this %u captures %cell",
                 "%otherState = ctjs.construct %constructor(%constructor)\n"
                 "    %otherCell = ctjs.create_cell %otherState\n"
                 "    %putter = ctjs.create_closure %callee[4] this %u captures %otherCell");
    auto distinctModule = mlir::parseSourceString<mlir::ModuleOp>(distinct, &context);
    check(static_cast<bool>(distinctModule), "distinct sibling Map environment fixture parses");
    if (distinctModule) {
        const auto contract = requested(*distinctModule);
        HostContractAnalysis host(*distinctModule, contract);
        check(host.proved() && host.callables().size() == 2,
              "each separate sibling Map can satisfy its individual live callable proof");
        if (host.proved() && host.callables().size() == 2) {
            check(host.callables()[0].capturedMap && host.callables()[1].capturedMap &&
                      host.callables()[0].capturedMap->allocation !=
                          host.callables()[1].capturedMap->allocation,
                  "the live host census retains distinct sibling allocation identities");
        }
        OwnedGlobalRoots owner(*distinctModule, contract);
        check(!owner.proved() && !owner.exhausted() && empty(*distinctModule, owner),
              "individually valid sibling Maps cannot inherit the one-shared-Map owner plan");
    }
}

} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    checkSharedMap(context);
    if (failures == 0) { std::puts("owned global shared Map proofs passed"); }
    return failures == 0 ? 0 : 1;
}
