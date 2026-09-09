// Host contract live proof queries over the shared two-method Map: seeded and
// per-key set facts typing a later get, possible-alias joins, SameValueZero
// keys, and every incomplete presence/join budget.
//
// SPLIT 2026-09-08: this is checkSeededMapResults, verbatim, out of a
// 1,068-line test/HostContract.cpp; it takes the same `shared` fixture that
// checkCapturedCallables used to build inline, now from HostContractFixtures.h.

#include "HostContractFixtures.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "llvm/ADT/APFloat.h"

#include <tuple>

using namespace ctcompile::test::host_contract;

namespace {

void checkEntryNumericResults(mlir::MLIRContext & context, const std::string & shared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    const std::string setterName = "put$3";
    const std::string getterName = "get$2";
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
        const auto census = [&](mlir::ModuleOp module, const HostContractAnalysis & query,
                                unsigned count) {
            const auto edges = query.callables();
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
            HostContractAnalysis query(*module, contract);
            check(query.proved() == expected && !query.exhausted() &&
                      (expected || withheld(*module, query)),
                  message);
            if (query.proved() != expected || query.exhausted()) {
                std::fprintf(stderr, "numeric host %s row %u: %s\n",
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
                    check(HostContractAnalysis(*module, contract).proved(),
                          "properly scoped numeric expressions have an independent proof");
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
                    HostContractAnalysis stale(*module, contract);
                    check(
                        !stale.proved() && stale.reason().contains("fingerprint") &&
                            withheld(*module, stale),
                        "a live cross-scope arithmetic operand invalidates its prior fingerprint");
                    HostContractAnalysis fresh(*module, requested(*module));
                    check(
                        !fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                        "fresh forged reports cannot authorize a then-only constant or arithmetic "
                        "result");
                    use->setOperand(0, original);
                    check(HostContractAnalysis(*module, contract).proved(),
                          "restoring numeric operand scope restores the complete proof");
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
                check(HostContractAnalysis(*module, contract).proved(),
                      "both valid numeric yield operands have an independent category proof");
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
                HostContractAnalysis stale(*module, contract);
                check(!stale.proved() && stale.reason().contains("fingerprint") &&
                          withheld(*module, stale),
                      "a cross-arm yield invalidates its original fingerprint");
                HostContractAnalysis fresh(*module, requested(*module));
                check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                      "constant conditions and fresh Number reports cannot authorize an invalid "
                      "yield");
                yield->setOperand(0, original);
                check(HostContractAnalysis(*module, contract).proved(),
                      "restoring the exact original yield repairs the independent proof");
                ++scopeMutations;
                // Clone the valid i1 producer into the then arm. It cannot
                // become the enclosing conditional's own condition, even when
                // its operand still denotes the same constant Boolean.
                const auto originalCondition = branch.getCondition();
                mlir::OpBuilder builder(branch.getThenRegion().front().getTerminator());
                auto * localCondition = builder.clone(*originalCondition.getDefiningOp());
                const auto conditionContract = requested(*module);
                check(HostContractAnalysis(*module, conditionContract).proved(),
                      "an unused then-local predicate preserves the original source proof");
                branch->setOperand(0, localCondition->getResult(0));
                HostContractAnalysis staleCondition(*module, conditionContract);
                check(!staleCondition.proved() && staleCondition.reason().contains("fingerprint") &&
                          withheld(*module, staleCondition),
                      "a then-only conditional predicate invalidates its prior fingerprint");
                HostContractAnalysis freshCondition(*module, requested(*module));

                check(!freshCondition.proved() && !freshCondition.exhausted() &&
                          withheld(*module, freshCondition),
                      "a fresh Number report cannot authorize a conditional's then-only predicate");
                branch->setOperand(0, originalCondition);
                check(
                    HostContractAnalysis(*module, conditionContract).proved(),
                    "restoring the exact original predicate restores the independent source proof");
                localCondition->erase();
                check(
                    HostContractAnalysis(*module, contract).proved(),
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
            HostContractAnalysis complete(*module, contract);
            const unsigned completion = complete.steps();
            check(complete.proved() && completion < 18000,
                  "numeric dependency proof is bounded and complete");
            if (!complete.proved() || completion >= 18000) { continue; }
            for (unsigned budget = 0; budget < completion; ++budget) {
                HostContractAnalysis limited(*module, contract, budget);
                check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                          withheld(*module, limited),
                      "every incomplete numeric budget withholds the entire callable family");
            }
            HostContractAnalysis exact(*module, contract, completion);
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
            HostContractAnalysis forged(*module, contract);
            check(forged.proved(),
                  "forged reports leave the independent numeric proof reproducible");
            if (!forged.proved()) { continue; }
            const auto edges = forged.callables();
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
                HostContractAnalysis stale(*module, contract);
                check(!stale.proved() && stale.reason().contains("fingerprint") &&
                          withheld(*module, stale),
                      "changing numeric dependency evidence invalidates its old fingerprint");
                HostContractAnalysis fresh(*module, requested(*module));
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
                check(HostContractAnalysis(*module, contract).proved(),
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
            check(HostContractAnalysis(*module, contract).proved(),
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
                check(HostContractAnalysis(*module, contract).proved(),
                      "restoring store-before-load order restores the saved category");
            }
            std::printf("numeric host %s %s: %u rows, %u scope controls, %u live edits, all %u "
                        "budgets checked\n",
                        prepared ? "prepared" : "source", label, rows, scopeMutations, mutations,
                        completion);
        }
    }
}

void checkCapturedMapClear(mlir::MLIRContext & context, const std::string & source, bool prepared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
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
    auto fixture =
        replaced(source, "%entryKey: !ctjs.value)",
                 "%entryKey: !ctjs.value, %aliasKey: !ctjs.value, %choice: !ctjs.value)");
    fixture = replaced(fixture, "    %actual =",
                       "    %other = ctjs.constant #ctjs.string<\"z\">\n"
                       "    %choice = ctjs.constant #ctjs.boolean<true>\n"
                       "    %actual =");
    for (const auto & [name, actual] :
         {std::pair{"putter", "actual"}, {"repeatPutter", "actual"}, {"laterPutter", "future"}}) {
        const std::string prefix = prepared ? std::string("%") + name + "Env, " : "%owned, ";
        fixture = replaced(fixture, prefix + "%" + actual + ")",
                           prefix + "%" + actual + ", %other, %choice)");
    }
    const std::string getterSignature =
        "@get$2(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value" +
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
    const auto census = [&](mlir::ModuleOp module, const HostContractAnalysis & query,
                            unsigned resultMask) {
        const auto strings = Alternatives::forTag(mlir::TypeID::get<ctjs::StringAttr>());
        const auto booleans = Alternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>());
        const Alternatives returned{
            resultMask & (Alternatives::Boolean | Alternatives::Number | Alternatives::String),
            resultMask, true};
        auto setter = module.lookupSymbol<ctjs::FuncOp>("put$3");
        auto getter = module.lookupSymbol<ctjs::FuncOp>("get$2");
        std::vector<ctjs::CreateObjectOp> objects;
        std::vector<ctjs::GetPropertyOp> reads;
        std::vector<ctjs::CallOp> calls;
        getter.walk([&](ctjs::GetPropertyOp operation) { reads.push_back(operation); });
        setter.walk([&](ctjs::CreateObjectOp operation) { objects.push_back(operation); });
        setter.walk([&](ctjs::GetPropertyOp operation) { reads.push_back(operation); });
        setter.walk([&](ctjs::CallOp operation) { calls.push_back(operation); });
        bool complete = query.callables().size() == 4;
        unsigned consumers = 0;
        for (const auto & edge : query.callables()) {
            complete &= edge.capturedMap.has_value();
            if (!edge.capturedMap) { continue; }
            const auto & capture = *edge.capturedMap;
            complete &= capture.leafObjects == objects && capture.leafWrites.empty() &&
                        capture.leafReads.empty() && capture.reads == reads &&
                        capture.calls == calls && capture.closures.size() == 2 &&
                        capture.parameters.size() == 2 &&
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
        HostContractAnalysis query(*module, contract);
        check(query.proved() == expected && !query.exhausted() &&
                  (expected || withheld(*module, query)),
              message);
        if (query.proved() != expected || query.exhausted()) {
            std::fprintf(stderr, "clear host %s row %u: %s\n", prepared ? "prepared" : "source",
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
            check(HostContractAnalysis(*module, contract).proved(),
                  "properly scoped clear dependencies have an independent complete proof");
            auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
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
            HostContractAnalysis stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      withheld(*module, stale),
                  "a clear dependency scope edit invalidates old reports");
            HostContractAnalysis fresh(*module, requested(*module));
            check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                  "fresh reports cannot authorize a then-only clear dependency outside its scope");
            for (const auto & edit : edits) { edit.op->setOperand(edit.operand, edit.original); }
            check(HostContractAnalysis(*module, contract).proved(),
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
        HostContractAnalysis query(*module, contract);
        const unsigned completion = query.steps();
        check(query.proved() && completion < 20000, "clear proof has bounded complete work");
        if (!query.proved() || completion >= 20000) { continue; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            HostContractAnalysis limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      withheld(*module, limited),
                  "every incomplete clear budget withholds the complete published family");
        }
        HostContractAnalysis exact(*module, contract, completion);
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
        check(HostContractAnalysis(*module, contract).proved(),
              "forged clear reports leave the positive live proof independently reproducible");
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
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
            HostContractAnalysis stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      withheld(*module, stale),
                  "live clear edits invalidate the prior fingerprint");
            HostContractAnalysis fresh(*module, requested(*module));
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
        check(HostContractAnalysis(*module, contract).proved(),
              "restoring clear spelling, receiver and arity restores its current proof");
        std::printf("clear host %s %s: %u rows, %u scope mutations, %u live edits, all %u budgets "
                    "checked\n",
                    prepared ? "prepared" : "source", label, rows, scopeMutations, mutations,
                    completion);
    }
}

void checkDefiniteMapAbsence(mlir::MLIRContext & context, const std::string & source,
                             bool prepared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
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
    auto fixture =
        replaced(source, "%entryKey: !ctjs.value)",
                 "%entryKey: !ctjs.value, %aliasKey: !ctjs.value, %choice: !ctjs.value)");
    fixture = replaced(fixture, "    %actual =",
                       "    %other = ctjs.constant #ctjs.string<\"z\">\n"
                       "    %choice = ctjs.constant #ctjs.boolean<true>\n"
                       "    %actual =");
    for (const auto & [name, actual] :
         {std::pair{"putter", "actual"}, {"repeatPutter", "actual"}, {"laterPutter", "future"}}) {
        const std::string prefix = prepared ? std::string("%") + name + "Env, " : "%owned, ";
        fixture = replaced(fixture, prefix + "%" + actual + ")",
                           prefix + "%" + actual + ", %other, %choice)");
    }
    const std::string getterSignature =
        "@get$2(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value" +
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
    const auto census = [&](mlir::ModuleOp module, const HostContractAnalysis & query,
                            unsigned resultMask) {
        const auto strings = Alternatives::forTag(mlir::TypeID::get<ctjs::StringAttr>());
        const auto booleans = Alternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>());
        const Alternatives returned{resultMask & Alternatives::Boolean, resultMask, true};
        auto setter = module.lookupSymbol<ctjs::FuncOp>("put$3");
        auto getter = module.lookupSymbol<ctjs::FuncOp>("get$2");
        std::vector<ctjs::CreateObjectOp> objects;
        std::vector<ctjs::GetPropertyOp> reads;
        std::vector<ctjs::CallOp> calls;
        getter.walk([&](ctjs::GetPropertyOp operation) { reads.push_back(operation); });
        setter.walk([&](ctjs::CreateObjectOp operation) { objects.push_back(operation); });
        setter.walk([&](ctjs::GetPropertyOp operation) { reads.push_back(operation); });
        setter.walk([&](ctjs::CallOp operation) { calls.push_back(operation); });
        bool complete = query.callables().size() == 4;
        unsigned consumers = 0;
        for (const auto & edge : query.callables()) {
            complete &= edge.capturedMap.has_value();
            if (!edge.capturedMap) { continue; }
            const auto & capture = *edge.capturedMap;
            complete &= capture.leafObjects == objects && capture.leafWrites.empty() &&
                        capture.leafReads.empty() && capture.reads == reads &&
                        capture.calls == calls && capture.closures.size() == 2 &&
                        capture.parameters.size() == 2 &&
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
        HostContractAnalysis query(*module, contract);
        check(query.proved() == expected && !query.exhausted() &&
                  (expected || withheld(*module, query)),
              message);
        if (query.proved() != expected || query.exhausted()) {
            std::fprintf(stderr, "absence host %s row %u: %s\n", prepared ? "prepared" : "source",
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
        check(HostContractAnalysis(*module, contract).proved(),
              "the original scoped absence read has a complete independent proof");
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
        mlir::scf::IfOp conditional;
        setter.walk([&](mlir::scf::IfOp operation) { conditional = operation; });
        auto inside = llvm::cast<ctjs::CallOp>(conditional.getThenRegion().front().front());
        auto * consumer = sibling ? &conditional.getElseRegion().front().front()
                                  : setter.getBody().front().getTerminator();
        const auto original = consumer->getOperand(0);
        consumer->setOperand(0, inside.getResult());
        HostContractAnalysis stale(*module, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && withheld(*module, stale),
              "an out-of-scope absent value invalidates the previous fingerprint");
        HostContractAnalysis fresh(*module, requested(*module));
        check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
              "fresh fingerprints and forged absence cannot authorize a then-only SSA value");
        consumer->setOperand(0, original);
        check(HostContractAnalysis(*module, contract).proved(),
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
            check(HostContractAnalysis(*module, contract).proved(),
                  "valid method, flag and fluent Map scopes have an independent absence proof");
            auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
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
            HostContractAnalysis stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      withheld(*module, stale),
                  "a live dependency scope edit invalidates the old absence fingerprint");
            HostContractAnalysis fresh(*module, requested(*module));
            check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                  "fresh reports cannot authorize then-only method, flag or fluent Map values");
            for (const auto & edit : edits) { edit.op->setOperand(edit.operand, edit.original); }
            check(HostContractAnalysis(*module, contract).proved(),
                  "restoring every dependency operand restores the valid absence proof");
            ++scopeMutations;
        }
    }
    check(scopeMutations == 6, "all outer and sibling dependency scope controls ran");
    for (const auto & [text, label] :
         {std::pair{base, "deleted"}, {saved, "saved"}, {bothArms, "two arms"}}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "definite absence budget fixture parses");
        if (!module) { continue; }
        auto contract = requested(*module);
        HostContractAnalysis query(*module, contract);
        const unsigned completion = query.steps();
        check(query.proved() && completion < 20000, "definite absence proof has bounded work");
        if (!query.proved() || completion >= 20000) { continue; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            HostContractAnalysis limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      withheld(*module, limited),
                  "every incomplete absence budget withholds the whole callable family");
        }
        HostContractAnalysis exact(*module, contract, completion);
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
        check(HostContractAnalysis(*module, contract).proved(),
              "forged absence reports leave the positive source independently provable");
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
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
            HostContractAnalysis stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      withheld(*module, stale),
                  "a live absence mutation invalidates the old source fingerprint");
            HostContractAnalysis fresh(*module, requested(*module));
            check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                  "a fresh contract and forged reports cannot repair the missing source proof");
            operation->setOperand(operand, original);
            check(HostContractAnalysis(*module, contract).proved(),
                  "restoring exact deletion restores its independent absence proof");
        };
        const auto aliasKey = setter.getBody().front().getArgument(prepared ? 5 : 4);
        mutation(get, 2, aliasKey);
        // In the two-arm witness, this removes the only exact deletion in
        // the first arm; the second arm still has two real deletions.
        mutation(deletes.front(), 2, aliasKey);
        mutation(deletes.front(), 1, setter.getBody().front().getArgument(0));
        std::printf("absence host %s %s: %u rows, %u live mutations, all %u budgets checked\n",
                    prepared ? "prepared" : "source", label, rows, mutations, completion);
    }
}

void checkLeafReadbacks(mlir::MLIRContext & context, const std::string & source, bool prepared) {
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
    const std::string write = "    %written = ctjs.call %setter(%state, %entryKey, %value)";
    const std::string field = "    %fieldKey = ctjs.constant #ctjs.string<\"value\">\n"
                              "    %fieldValue = ctjs.constant #ctjs.number<4607182418800017408>\n"
                              "    ctjs.set_property %value[%fieldKey], %fieldValue\n";
    const std::string read = "\n    %getKey = ctjs.constant #ctjs.string<\"get\">\n"
                             "    %reader = ctjs.get_property %state[%getKey]\n"
                             "    %saved = ctjs.call %reader(%state, %entryKey)\n";
    const std::string overwrite =
        "    %replacement = ctjs.create_object\n"
        "    ctjs.set_property %replacement[%fieldKey], %fieldValue\n"
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
    const auto same = replaced(saved, "    ctjs.return %size", identity);
    const auto loaded = replaced(saved, "    ctjs.return %size", fieldReturn);
    const auto savedAfter =
        replaced(saved, "    ctjs.return %size", overwrite + erase + fieldReturn);
    unsigned rows = 0;
    const auto census = [&](mlir::ModuleOp module, const HostContractAnalysis & query) {
        auto setter = module.lookupSymbol<ctjs::FuncOp>("put$3");
        std::vector<ctjs::CreateObjectOp> objects;
        std::vector<ctjs::SetPropertyOp> writes;
        std::vector<ctjs::GetPropertyOp> fields, mapReads;
        std::vector<ctjs::CallOp> mapCalls;
        module.lookupSymbol<ctjs::FuncOp>("get$2").walk(
            [&](ctjs::GetPropertyOp operation) { mapReads.push_back(operation); });
        setter.walk([&](ctjs::CreateObjectOp operation) { objects.push_back(operation); });
        setter.walk([&](ctjs::SetPropertyOp operation) { writes.push_back(operation); });
        setter.walk([&](ctjs::CallOp operation) { mapCalls.push_back(operation); });
        setter.walk([&](ctjs::GetPropertyOp operation) {
            auto key = operation.getKey().getDefiningOp<ctjs::ConstantOp>();
            auto name = key ? llvm::dyn_cast<ctjs::StringAttr>(key.getValue()) : ctjs::StringAttr{};
            (name && name.getValue() == "value" ? fields : mapReads).push_back(operation);
        });
        bool complete = query.callables().size() == 4 && !objects.empty();
        const auto string = ctcompile::ctnative::PrimitiveAlternatives::forTag(
            mlir::TypeID::get<ctjs::StringAttr>());
        unsigned setters = 0;
        for (const auto & edge : query.callables()) {
            complete &= edge.capturedMap.has_value();
            if (!edge.capturedMap) { continue; }
            const auto & capture = *edge.capturedMap;
            complete &= capture.leafObjects == objects && capture.leafWrites == writes &&
                        capture.leafReads == fields && capture.reads == mapReads &&
                        capture.calls == mapCalls && capture.closures.size() == 2 &&
                        capture.parameters.size() == 2 &&
                        capture.upvalues.size() == (prepared ? 0u : 2u);
            if (edge.function != setter) { continue; }
            ++setters;
            complete &= edge.arguments.size() == 1;
            if (edge.arguments.size() != 1) { continue; }
            complete &= edge.arguments.front().alternatives == string &&
                        edge.arguments.front().actual == edge.call->getOperand(prepared ? 4 : 2);
        }
        check(complete && setters == 3,
              "every future invocation retains the exact source leaf/read/write/Map census once");
    };
    const auto variant = [&](const std::string & text, bool expected, const char * message) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "source/prepared leaf readback fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
        HostContractAnalysis query(*module, contract);
        check(query.proved() == expected && !query.exhausted() &&
                  (expected || withheld(*module, query)),
              message);
        if (query.proved() != expected || query.exhausted()) {
            std::fprintf(stderr, "leaf readback host %s row %u: %s\n",
                         prepared ? "prepared" : "source", rows, query.reason().str().c_str());
        }
        if (expected && query.proved()) { census(*module, query); }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "readback proof preserves all original allocations, calls and identity operands");
    };
    variant(same, true, "same-key get has the independently proved local allocation origin");
    variant(loaded, true,
            "a saved same-key get can read a definitely initialized scalar own field");
    variant(replaced(same, "    %same =", overwrite + "    %same ="), true,
            "Map replacement cannot retarget the saved object identity");
    variant(replaced(same, "    %same =", erase + "    %same ="), true,
            "Map deletion cannot destroy the saved object origin");
    variant(replaced(same, "    %same =", overwrite + erase + "    %same ="), true,
            "the historical saved comparison remains proved across overwrite and deletion");
    variant(replaced(replaced(same, "    %same =", overwrite + erase + "    %same ="),
                     "strict_eq %saved, %value", "strict_eq %saved, %replacement"),
            true, "equal-field distinct allocations remain separate identities");
    variant(savedAfter, true, "saved own-field evidence survives replacement and deletion");
    const auto freshIdentity = replaced(same, "    %same =",
                                        "    %distinct = ctjs.create_object\n"
                                        "    ctjs.set_property %distinct[%fieldKey], %fieldValue\n"
                                        "    %same =");
    variant(replaced(freshIdentity, "strict_eq %saved, %value", "strict_eq %saved, %distinct"),
            true, "comparison-only fresh allocations have their own independently proved identity");
    variant(replaced(loaded, "    %loaded =",
                     "    %two = ctjs.constant #ctjs.number<4611686018427387904>\n"
                     "    ctjs.set_property %value[%fieldKey], %two\n"
                     "    %loaded ="),
            true, "a write through the original object updates its saved alias field evidence");
    variant(replaced(loaded, "    %loaded =",
                     "    %two = ctjs.constant #ctjs.number<4611686018427387904>\n"
                     "    ctjs.set_property %saved[%fieldKey], %two\n"
                     "    %loaded ="),
            true, "a saved object alias can receive a scalar own-field write");
    variant(replaced(loaded, "    %loaded =",
                     "    %flag = ctjs.truthy %entryKey\n"
                     "    scf.if %flag {\n" +
                         overwrite + "      scf.yield\n    }\n    %loaded ="),
            true, "a saved alias survives a conditional replacement of the Map entry");
    for (const char * literal : {"#ctjs.boolean<false>", "#ctjs.null", "#ctjs.undefined"}) {
        variant(replaced(loaded, "#ctjs.number<4607182418800017408>", literal), true,
                "a field read derives its current scalar category independently of old reports");
    }
    variant(replaced(saved, "    ctjs.return %size", "    ctjs.return %saved"), false,
            "a proved local get origin does not authorize an object-valued public return");
    variant(replaced(loaded, "%reader(%state, %entryKey)", "%reader(%state, %fieldKey)"), false,
            "a different possibly absent key has no local object origin");
    variant(replaced(loaded, "%saved[%fieldKey]", "%saved[%entryKey]"), false,
            "a future String actual does not prove a fixed own-field name");
    variant(replaced(loaded, "%saved[%fieldKey]", "%saved[%getKey]"), false,
            "a fresh leaf does not prove a never-initialized own field");
    variant(replaced(loaded, "    ctjs.set_property %value[%fieldKey], %fieldValue\n", ""), false,
            "object presence and identity do not supply missing field initialization");
    const auto freshAfterDelete =
        replaced(loaded, "    %loaded =",
                 erase + "    %current = ctjs.call %reader(%state, %entryKey)\n"
                         "    %loaded =");
    variant(replaced(freshAfterDelete, "%saved[%fieldKey]", "%current[%fieldKey]"), false,
            "a fresh read after deletion cannot use the saved object's presence proof");
    variant(replaced(same, "strict_eq %saved, %value", "strict_eq %saved, %this"), false,
            "unknown incoming receiver identity cannot borrow local object evidence");
    variant(replaced(same, "compare strict_eq", "compare eq"), false,
            "loose object equality requires an independent conversion and effects proof");
    variant(replaced(loaded, "    %loaded =",
                     "    ctjs.set_property %saved[%fieldKey], %value\n"
                     "    %loaded ="),
            false, "a retaining object field through an alias is still a graph edge");
    variant(replaced(loaded, "#ctjs.number<4607182418800017408>", "#ctjs.string<\"owned\">"), false,
            "readback does not provide an owning String field representation");
    variant(replaced(loaded, "    %loaded =",
                     "    %u = ctjs.constant #ctjs.undefined\n"
                     "    ctjs.define_accessor \"value\" on %saved get %callee set %u\n"
                     "    %loaded ="),
            false, "a live accessor on a saved alias cannot hide behind its former plain field");
    const auto uninitializedBranch =
        replaced(loaded, "    ctjs.set_property %value[%fieldKey], %fieldValue\n",
                 "    %flag = ctjs.truthy %entryKey\n"
                 "    scf.if %flag {\n"
                 "      ctjs.set_property %value[%fieldKey], %fieldValue\n"
                 "      scf.yield\n    }\n");
    variant(uninitializedBranch, false,
            "both paths must definitely initialize a field before readback");
    check(rows == 26, "all local identity, field, presence and unsafe-use controls ran");

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
        HostContractAnalysis checked(*module, contract);
        check(checked.proved(),
              "valid branch-local object operations retain their owning source proof");
        if (!checked.proved()) { continue; }
        census(*module, checked);
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
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
        HostContractAnalysis stale(*module, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && withheld(*module, stale),
              "a cross-scope identity operand invalidates the previous source fingerprint");
        HostContractAnalysis fresh(*module, requested(*module));
        check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
              "forged origin reports cannot authorize a then-only object outside its SSA scope");
        compared->setOperand(operand, original);
        check(HostContractAnalysis(*module, contract).proved(),
              "restoring the actual in-scope operand restores the independent object proof");
    }

    for (const auto & [text, label] : {std::pair{same, "identity"}, {savedAfter, "saved field"}}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "leaf readback budget fixture parses");
        if (!module) { continue; }
        auto contract = requested(*module);
        HostContractAnalysis query(*module, contract);
        const unsigned completion = query.steps();
        check(query.proved() && completion < 20000,
              "leaf readback has bounded complete proof work");
        if (!query.proved() || completion >= 20000) { continue; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            HostContractAnalysis limited(*module, contract, budget);
            check(
                !limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                    withheld(*module, limited),
                "every unfinished readback budget withholds object origins and the entire family");
        }
        HostContractAnalysis exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion,
              "the exact readback budget reproduces the independent source family");
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
        check(
            HostContractAnalysis(*module, contract).proved(),
            "forged identity and presence reports leave the positive source independently proved");
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
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
        check(get && fieldKey, "the saved readback retains its exact source get and field key");
        if (!get || !fieldKey) { continue; }
        unsigned mutations = 0;
        const auto mutation = [&](mlir::Operation * operation, unsigned operand,
                                  mlir::Value value) {
            ++mutations;
            const auto original = operation->getOperand(operand);
            operation->setOperand(operand, value);
            HostContractAnalysis stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      withheld(*module, stale),
                  "a live readback operand mutation invalidates its previous fingerprint");
            HostContractAnalysis fresh(*module, requested(*module));
            check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                  "forged identity/presence facts cannot repair an unsafe fresh readback source");
            operation->setOperand(operand, original);
            check(HostContractAnalysis(*module, contract).proved(),
                  "restoring the source restores the independently proved saved object origin");
        };
        mutation(get, 2, fieldKey.getResult());
        auto returned = llvm::cast<ctjs::ReturnOp>(setter.getBody().front().getTerminator());
        mutation(returned, 0, get.getResult());
        if (compare) { mutation(compare, 1, setter.getBody().front().getArgument(0)); }
        if (fieldRead) {
            mutation(fieldRead, 0, setter.getBody().front().getArgument(0));
            mutation(fieldRead, 1, setter.getBody().front().getArgument(prepared ? 4 : 3));
        }
        std::printf(
            "leaf readback host %s %s: %u rows, %u live mutations, all %u budgets checked\n",
            prepared ? "prepared" : "source", label, rows, mutations, completion);
    }
}

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
        const auto variant = [&](const std::string & text, bool expected, const char * message) {
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
            if (expected && query.proved()) { edges(*module, query); }
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
                    false, "unknown, String and retaining object fields cannot borrow leaf proof");
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
        const auto getter = replaced(program,
                                     "    %key = ctjs.constant #ctjs.string<\"size\">\n"
                                     "    %answer = ctjs.get_property %state[%key]",
                                     "    %key = ctjs.constant #ctjs.string<\"get\">\n"
                                     "    %reader = ctjs.get_property %state[%key]\n"
                                     "    %probeKey = ctjs.constant #ctjs.string<\"x\">\n"
                                     "    %answer = ctjs.call %reader(%state, %probeKey)");
        variant(getter, false,
                "an object-writing sibling vetoes an unknown supposedly primitive get");
        variant(replaced(getter, "    %answer = ctjs.call %reader",
                         "    %setName = ctjs.constant #ctjs.string<\"set\">\n"
                         "    %seedSetter = ctjs.get_property %state[%setName]\n"
                         "    %seed = ctjs.constant #ctjs.number<4607182418800017408>\n"
                         "    %seeded = ctjs.call %seedSetter(%state, %probeKey, %seed)\n"
                         "    %answer = ctjs.call %reader"),
                true,
                "a live exact primitive reseed proves its read despite object-writing siblings");
        check(rows == 25, "every leaf allocation, scalar field and escaping-use control ran");
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
                    HostContractAnalysis unsupported(*module, requested(*module));
                    check(
                        !unsupported.proved() && !unsupported.exhausted() &&
                            withheld(*module, unsupported),
                        "a formerly numeric field cannot retain its schema after String mutation");
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

void checkNullablePayloadResults(mlir::MLIRContext & context, const std::string & nullable,
                                 bool prepared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    constexpr unsigned nullableMask = Alternatives::String | Alternatives::Null;
    constexpr llvm::StringLiteral write =
        "    %written = ctjs.call %setter(%state, %entryKey, %entryKey)";
    constexpr llvm::StringLiteral read = "    %loaded = ctjs.call %reader(%state, %entryKey)";
    constexpr llvm::StringLiteral returned = "    ctjs.return %loaded";
    auto source =
        replaced(nullable, "    %written = ctjs.call %setter(%state, %entryKey, %value)", write);
    source = replaced(source,
                      "    %u = ctjs.constant #ctjs.undefined\n"
                      "    ctjs.return %u\n  }\n}\n",
                      "    %getKey = ctjs.constant #ctjs.string<\"get\">\n"
                      "    %reader = ctjs.get_property %state[%getKey]\n" +
                          read.str() + "\n" + returned.str() + "\n  }\n}\n");
    source = replaced(source, "    ctjs.return %table",
                      "    %sizer = ctjs.create_closure %callee[4] this %u captures %" +
                          std::string(prepared ? "state" : "cell") +
                          "\n    %sizeKey = ctjs.constant #ctjs.string<\"size\">\n"
                          "    ctjs.set_property %table[%sizeKey], %sizer\n"
                          "    ctjs.return %table");
    const std::string environment = prepared ? ", %state: !ctjs.value" : "";
    source = replaced(source, "\n}\n",
                      "\n  ctjs.func private @size$4(%this: !ctjs.value, %new: !ctjs.value, "
                      "%callee: !ctjs.value" +
                          environment +
                          ", %entryKey: !ctjs.value) -> !ctjs.value attributes {upvalue_count = " +
                          std::string(prepared ? "0" : "1") + " : i32} {\n" +
                          (prepared ? "" : "    %state = ctjs.load_upvalue %callee[0]\n") + R"MLIR(
    %setKey = ctjs.constant #ctjs.string<"set">
    %setter = ctjs.get_property %state[%setKey]
    %written = ctjs.call %setter(%state, %entryKey, %entryKey)
    %sizeKey = ctjs.constant #ctjs.string<"size">
    %size = ctjs.get_property %state[%sizeKey]
    ctjs.return %size
  }
}
)MLIR");
    std::string consumer = "    %sizeKey = ctjs.constant #ctjs.string<\"size\">\n"
                           "    %sizer = ctjs.get_property %owned[%sizeKey]\n";
    if (prepared) {
        consumer += "    %sizeEnvironment = ctjs.load_upvalue %sizer[0]\n"
                    "    %answer = ctjs.call_direct @size$4(%owned, %u, %sizer, "
                    "%sizeEnvironment, %putResult)";
        source = replaced(source,
                          "    %getEnvironment = ctjs.load_upvalue %getter[0]\n"
                          "    %answer = ctjs.call_direct @get$2(%owned, %u, %getter, "
                          "%getEnvironment, %falseFlag)",
                          consumer);
    } else {
        consumer += "    %answer = ctjs.call %sizer(%owned, %putResult)";
        source = replaced(source, "    %answer = ctjs.call %getter(%owned, %falseFlag)", consumer);
    }
    source = replaced(source, "    %getter = ctjs.get_property %owned[%key]\n", "");
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
    const auto edges = [&](mlir::ModuleOp module, const HostContractAnalysis & query,
                           unsigned resultMask) {
        const auto calls = query.callables();
        bool complete = calls.size() == 3;
        for (unsigned index = 0; index < calls.size(); ++index) {
            const auto & call = calls[index];
            const char * name = index == 0 ? "get$2" : index == 1 ? "put$3" : "size$4";
            auto function = module.lookupSymbol<ctjs::FuncOp>(name);
            const unsigned mask = index == 0   ? Alternatives::Boolean
                                  : index == 1 ? nullableMask
                                               : resultMask;
            const Alternatives expected{
                mask & (Alternatives::Boolean | Alternatives::Number | Alternatives::String), mask,
                true};
            complete &= call.function == function && call.capturedMap && call.arguments.size() == 1;
            if (!function || !call.capturedMap || call.arguments.size() != 1) { continue; }
            const auto & argument = call.arguments.front();
            complete &=
                argument.parameter == function.getBody().front().getArgument(prepared ? 4 : 3) &&
                argument.actual == call.call->getOperand(prepared ? 4 : 2) &&
                argument.alternatives == expected;
            if (index) {
                complete &= argument.actual == calls[index - 1].call->getResult(0) &&
                            calls[index - 1].call->isBeforeInBlock(call.call);
            }
            unsigned families = 0;
            for (const auto & parameters : call.capturedMap->parameters) {
                if (parameters.function != function) { continue; }
                ++families;
                complete &= parameters.alternatives == std::vector{expected};
            }
            complete &= families == 1;
        }
        check(complete,
              "get, nullable payload readback and size retain independent complete SSA families");
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & program, bool expected, const char * message,
                             unsigned resultMask = Alternatives::String | Alternatives::Null) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "source/prepared nullable payload fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
        HostContractAnalysis query(*module, contract);
        check(query.proved() == expected && !query.exhausted() &&
                  (expected || withheld(*module, query)),
              message);
        if (query.proved() != expected || query.exhausted()) {
            std::fprintf(stderr, "nullable payload host %s case %u: %s\n",
                         prepared ? "prepared" : "source", rows, query.reason().str().c_str());
        }
        if (expected && query.proved()) { edges(*module, query, resultMask); }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "nullable payload proof preserves every source operation and operand");
    };
    variant(source, true,
            "a definitely present nullable Map read feeds a distinct published method");
    const auto conditional = replaced(source, write, R"MLIR(
    %present = ctjs.truthy %entryKey
    scf.if %present {
      %left = ctjs.call %setter(%state, %entryKey, %entryKey)
      scf.yield
    } else {
      %null = ctjs.constant #ctjs.null
      %right = ctjs.call %setter(%state, %entryKey, %null)
      scf.yield
    }
)MLIR");
    variant(conditional, true,
            "both reaching writes independently retain String and Null payloads");
    variant(replaced(source, read, R"MLIR(
    %present = ctjs.truthy %entryKey
    scf.if %present {
      %null = ctjs.constant #ctjs.null
      %right = ctjs.call %setter(%state, %entryKey, %null)
      scf.yield
    }
)MLIR" + read.str()),
            true, "a no-else nullable overwrite joins with the incoming payload");
    const auto undefined = replaced(source, write, R"MLIR(
    %present = ctjs.truthy %entryKey
    %payload = scf.if %present -> (!ctjs.value) {
      scf.yield %entryKey : !ctjs.value
    } else {
      %missing = ctjs.constant #ctjs.undefined
      scf.yield %missing : !ctjs.value
    }
    %written = ctjs.call %setter(%state, %entryKey, %payload)
)MLIR");
    variant(undefined, true, "a conditional payload read preserves String and Undefined",
            Alternatives::String | Alternatives::Undefined);
    constexpr llvm::StringLiteral alias = R"MLIR(
    %aliasKey = ctjs.constant #ctjs.string<"possible">
    %aliasValue = ctjs.constant #ctjs.null
    %aliased = ctjs.call %setter(%state, %aliasKey, %aliasValue)
)MLIR";
    const auto joined = replaced(source, read, alias.str() + read.str());
    variant(joined, true, "a possibly aliasing Null write retains the finite nullable payload set");
    variant(replaced(joined, "%aliasValue = ctjs.constant #ctjs.null",
                     "%aliasValue = ctjs.constant #ctjs.boolean<true>"),
            false, "a possible truthy Boolean alias cannot borrow the nullable consumer signature");
    variant(replaced(joined, "%aliasValue = ctjs.constant #ctjs.null",
                     "%aliasValue = ctjs.load_global \"unknown\""),
            false, "a possible unknown write cannot be repaired by prior finite payload evidence");
    constexpr llvm::StringLiteral erase = R"MLIR(
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %erased = ctjs.call %deleter(%state, %entryKey)
)MLIR";
    const auto saved = replaced(source, returned, R"MLIR(
    %changed = ctjs.constant #ctjs.boolean<true>
    %overwrite = ctjs.call %setter(%state, %entryKey, %changed)
)MLIR" + erase.str() + returned.str());
    variant(saved, true, "saved nullable SSA read evidence survives exact overwrite and deletion");
    variant(
        replaced(source, read, erase.str() + read.str()), true,
        "exact deletion replaces the nullable payload result with independently proved Undefined",
        Alternatives::Undefined);
    variant(replaced(source, write, ""), false,
            "a captured Map starts with unknown contents at each invocation");
    variant(replaced(source, read,
                     "    %missingKey = ctjs.constant #ctjs.string<\"missing\">\n"
                     "    %loaded = ctjs.call %reader(%state, %missingKey)"),
            false, "a different key cannot inherit the nullable entry's definite presence");
    variant(replaced(source, write, "    %written = ctjs.call %setter(%state, %entryKey, %this)"),
            false, "an unproved payload cannot inherit the nullable input's alternatives");
    variant(replaced(conditional, "      %right = ctjs.call %setter(%state, %entryKey, %null)", ""),
            false, "one reaching write cannot prove membership on both arms");
    variant(replaced(conditional, "%null = ctjs.constant #ctjs.null",
                     "%null = ctjs.constant #ctjs.boolean<true>"),
            false, "a truthy mixed branch remains outside the nullable consumer parameter family");
    variant(replaced(saved, returned,
                     "    %fresh = ctjs.call %reader(%state, %entryKey)\n"
                     "    ctjs.return %fresh"),
            true, "a fresh deleted read proves Undefined independently of the saved nullable value",
            Alternatives::Undefined);
    variant(replaced(source, read, "    %loaded = ctjs.call %reader(%this, %entryKey)"), false,
            "a different receiver cannot inherit captured Map payload evidence");
    check(rows == 16, "all nullable payload propagation and refusal rows ran");
    for (const auto & [program, label] :
         {std::pair{source, "nullable payload"}, std::pair{conditional, "nullable payload join"}}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "nullable payload budget fixture parses");
        if (!module) { continue; }
        auto contract = requested(*module);
        HostContractAnalysis complete(*module, contract);
        const unsigned completion = complete.steps();
        check(complete.proved() && completion < 20000,
              "the nullable payload dependency proof stays within its fixture work limit");
        if (!complete.proved() || completion >= 20000) { continue; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            HostContractAnalysis limited(*module, contract, budget);
            check(
                !limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                    withheld(*module, limited),
                "every incomplete payload budget withholds the whole callable and property proof");
        }
        HostContractAnalysis exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion,
              "the exact payload budget reproduces all dependency edges");
        if (exact.proved()) { edges(*module, exact, nullableMask); }
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
        auto result = llvm::cast<ctjs::ReturnOp>(setter.getBody().front().getTerminator());
        auto lookup = result.getValue().getDefiningOp<ctjs::CallOp>();
        check(static_cast<bool>(lookup), "the nullable payload producer retains its source read");
        if (!lookup) { continue; }
        mlir::Builder builder(&context);
        module->walk([&](mlir::Operation * operation) {
            if (!llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(operation)) { return; }
            operation->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            operation->setAttr("ctnative.map_present", builder.getBoolAttr(true));
            operation->setAttr("ctnative.map_read_type", builder.getStringAttr("Opt<Str>"));
        });
        contract = requested(*module);
        check(HostContractAnalysis(*module, contract).proved(),
              "forged nullable reports leave the live proof independently reproducible");
        const auto mutation = [&](mlir::Operation * operation, unsigned operand,
                                  mlir::Value replacement) {
            const auto original = operation->getOperand(operand);
            operation->setOperand(operand, replacement);
            HostContractAnalysis stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      withheld(*module, stale),
                  "a live payload mutation invalidates its earlier host fingerprint");
            HostContractAnalysis fresh(*module, requested(*module));
            check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                  "fresh forged reports cannot recover unknown nullable payload evidence");
            operation->setOperand(operand, original);
            check(HostContractAnalysis(*module, contract).proved(),
                  "restoring the nullable payload edge restores the complete proof");
        };
        const auto unknown = setter.getBody().front().getArgument(0);
        mutation(lookup, 2, unknown);
        mutation(result, 0, lookup.getReceiver());
        setter.walk([&](ctjs::CallOp call) {
            if (call.getArgs().size() == 2) { mutation(call, 3, unknown); }
        });
        std::printf("%s host %s: %u rows and all %u incomplete budgets checked\n", label,
                    prepared ? "prepared" : "source", rows, completion);
    }
}

void checkNullableMapResults(mlir::MLIRContext & context, const std::string & scalar,
                             bool prepared) {
    constexpr llvm::StringLiteral nullableReturn = R"MLIR(
    %present = ctjs.truthy %answer
    %nullable = scf.if %present -> (!ctjs.value) {
      scf.yield %answer : !ctjs.value
    } else {
      %null = ctjs.constant #ctjs.null
      scf.yield %null : !ctjs.value
    }
    ctjs.return %nullable
)MLIR";
    constexpr llvm::StringLiteral normalize = R"MLIR(
    %keyPresent = ctjs.truthy %entryKey
    %normalized = scf.if %keyPresent -> (!ctjs.value) {
      scf.yield %entryKey : !ctjs.value
    } else {
      %missing = ctjs.constant #ctjs.string<"missing">
      scf.yield %missing : !ctjs.value
    }
    %written = ctjs.call %setter(%state, %normalized, %value)
)MLIR";
    constexpr llvm::StringLiteral write =
        "    %written = ctjs.call %setter(%state, %entryKey, %value)";
    const auto nullable = replaced(scalar, "    ctjs.return %answer", nullableReturn);
    const auto normalized = replaced(nullable, write, normalize);
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto withheld = [](mlir::ModuleOp module, const HostContractAnalysis & result) {
        bool empty = result.callables().empty();
        module.walk([&](mlir::Operation * operation) {
            empty &= !result.callable(operation);
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                empty &= !result.property(read);
            }
        });
        return empty;
    };
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    constexpr unsigned nullableMask = Alternatives::String | Alternatives::Null;
    const auto edges = [&](mlir::ModuleOp module, const HostContractAnalysis & result,
                           unsigned expectedCalls, unsigned expectedMask) {
        auto getter = module.lookupSymbol<ctjs::FuncOp>("get$2");
        auto setter = module.lookupSymbol<ctjs::FuncOp>("put$3");
        const auto calls = result.callables();
        bool complete = calls.size() == expectedCalls;
        unsigned getters = 0, setters = 0, dependencies = 0;
        mlir::Operation * previous = nullptr;
        for (const auto & call : calls) {
            complete &= call.capturedMap && call.arguments.size() == 1 &&
                        (!previous || previous->isBeforeInBlock(call.call));
            previous = call.call;
            if (!call.capturedMap || call.arguments.size() != 1) { continue; }
            const auto & argument = call.arguments.front();
            auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(call.call);
            const auto actuals =
                direct ? direct.getArgs() : llvm::cast<ctjs::CallOp>(call.call).getArgs();
            complete &= actuals.size() == (prepared ? 2u : 1u) &&
                        argument.actual == actuals[prepared ? 1 : 0];
            if (call.function == getter) {
                ++getters;
                complete &=
                    argument.parameter == getter.getBody().front().getArgument(prepared ? 4 : 3) &&
                    argument.alternatives.tag() == mlir::TypeID::get<ctjs::BooleanAttr>();
            } else if (call.function == setter) {
                ++setters;
                complete &=
                    argument.parameter == setter.getBody().front().getArgument(prepared ? 4 : 3) &&
                    argument.alternatives ==
                        Alternatives{expectedMask & Alternatives::String, expectedMask, true};
                unsigned parameterFamilies = 0;
                for (const auto & parameters : call.capturedMap->parameters) {
                    if (parameters.function == setter) {
                        ++parameterFamilies;
                        complete &= parameters.alternatives.size() == 1 &&
                                    parameters.alternatives.front() == argument.alternatives;
                    }
                }
                complete &= parameterFamilies == 1;
                for (const auto & producer : calls) {
                    if (producer.function == getter &&
                        argument.actual == producer.call->getResult(0)) {
                        ++dependencies;
                        complete &= producer.call->isBeforeInBlock(call.call);
                    }
                }
            } else {
                complete = false;
            }
        }
        check(complete && getters == 2 && setters == expectedCalls - 2 && dependencies == 1,
              "nullable complete actual census preserves every source call and producer edge");
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & program, bool expected, const char * message,
                             unsigned expectedCalls = 3,
                             unsigned expectedMask = Alternatives::String | Alternatives::Null) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "source/prepared nullable Map fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
        HostContractAnalysis result(*module, contract);
        check(result.proved() == expected && !result.exhausted() &&
                  (expected || withheld(*module, result)),
              message);
        if (result.proved() != expected || result.exhausted()) {
            std::fprintf(stderr, "nullable Map host %s case %u: %s\n",
                         prepared ? "prepared" : "source", rows, result.reason().str().c_str());
        }
        if (expected && result.proved()) { edges(*module, result, expectedCalls, expectedMask); }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "nullable evidence leaves all source operations and operands intact");
    };
    variant(normalized, true, "finite nullable results reach a normalized String consumer");
    variant(nullable, true,
            "host primitive ownership does not promise a native nullable Map key carrier");
    variant(replaced(normalized, "%present = ctjs.truthy %answer", "%present = ctjs.truthy %flag"),
            true, "nullable ternary results preserve both independently checked alternatives");
    variant(replaced(normalized, "#ctjs.string<\"owned\">", "#ctjs.string<\"\">"), true,
            "a present empty String read reaches the independently proved null fallback", 3,
            Alternatives::Null);
    variant(replaced(normalized, "%normalized, %value)", "%normalized, %entryKey)"), true,
            "primitive nullable payload ownership is independent of native storage admission");
    variant(replaced(normalized, "    %answer = ctjs.call %mapGetter(%state, %seedKey)",
                     "    %answer = ctjs.call %mapGetter(%state, %payload)"),
            false, "null fallback cannot prove the tag of a missing Map read");
    variant(replaced(normalized,
                     "    %otherSeed = ctjs.call %mapSetter(%state, %probeKey, "
                     "%payload)\n",
                     ""),
            false, "a nullable return cannot supply a missing guarded payload tag");
    variant(replaced(normalized, "    %keyPresent = ctjs.truthy %entryKey",
                     "    %keyPresent = ctjs.truthy %this"),
            false, "an unproved condition cannot normalize a nullable consumer");
    variant(replaced(normalized, "      scf.yield %missing : !ctjs.value",
                     "      scf.yield %state : !ctjs.value"),
            false, "nullable normalization cannot leak the captured Map through its fallback");
    variant(replaced(normalized, "%normalized, %value)", "%normalized, %state)"), false,
            "finite parameter evidence cannot authorize a cyclic Map payload");
    for (const char * yielded : {"%answer", "%null", "%entryKey", "%missing"}) {
        const auto terminator = std::string("      scf.yield ") + yielded + " : !ctjs.value";
        variant(replaced(normalized, terminator,
                         "      ctjs.store_global \"trace\", " + std::string(yielded) + "\n" +
                             terminator),
                false, "every nullable producer and consumer arm retains its late effect check");
    }
    const auto extraActual = [&](const std::string & literal, bool before) {
        std::string addition = "    %extra = ctjs.constant " + literal +
                               "\n    %extraPutter = ctjs.get_property %owned[%putKey]\n";
        if (prepared) {
            addition += "    %extraEnvironment = ctjs.load_upvalue %extraPutter[0]\n"
                        "    %extraPut = ctjs.call_direct @put$3(%owned, %u, %extraPutter, "
                        "%extraEnvironment, %extra)\n";
        } else {
            addition += "    %extraPut = ctjs.call %extraPutter(%owned, %extra)\n";
        }
        const std::string marker = before     ? "    %priorGetter = ctjs.get_property"
                                   : prepared ? "    %getEnvironment = ctjs.load_upvalue"
                                              : "    %answer = ctjs.call %getter";
        return replaced(normalized, marker, addition + marker);
    };
    for (const bool before : {false, true}) {
        for (const char * literal : {"#ctjs.string<\"other\">", "#ctjs.null"}) {
            variant(extraActual(literal, before), true,
                    "all literal and nullable-result actuals join independently of call order", 4);
        }
        variant(extraActual("#ctjs.undefined", before), true,
                "a proved Undefined actual joins the closed String and Null parameter set", 4,
                nullableMask | Alternatives::Undefined);
        for (const char * literal : {"#ctjs.boolean<true>", "#ctjs.number<0>"}) {
            variant(extraActual(literal, before), false,
                    "an incompatible actual cannot borrow the nullable producer's argument proof");
        }
    }
    for (const bool nullFirst : {false, true}) {
        const auto program = extraActual(nullFirst ? "#ctjs.null" : "#ctjs.undefined", true);
        std::string addition = "    %lastActual = ctjs.constant " +
                               std::string(nullFirst ? "#ctjs.undefined" : "#ctjs.null") +
                               "\n    %lastPutter = ctjs.get_property %owned[%putKey]\n";
        if (prepared) {
            addition += "    %lastEnvironment = ctjs.load_upvalue %lastPutter[0]\n"
                        "    %lastPut = ctjs.call_direct @put$3(%owned, %u, %lastPutter, "
                        "%lastEnvironment, %lastActual)\n";
        } else {
            addition += "    %lastPut = ctjs.call %lastPutter(%owned, %lastActual)\n";
        }
        constexpr llvm::StringLiteral marker = "    %priorGetter = ctjs.get_property";
        variant(replaced(program, marker, addition + marker.str()), true,
                "an early Null/Undefined prefix waits for the complete nullable String census", 5,
                nullableMask | Alternatives::Undefined);
    }
    const auto extraString = extraActual("#ctjs.string<\"other\">", false);
    variant(replaced(extraString, "%extra = ctjs.constant #ctjs.string<\"other\">",
                     "%extra = ctjs.create_object"),
            false, "a later object actual withholds the entire nullable family");
    variant(replaced(extraString, "%extra = ctjs.constant #ctjs.string<\"other\">",
                     "%extra = ctjs.load_global \"unknown\""),
            false, "a later unknown global actual withholds the entire nullable family");
    // Prepared direct calls verify arity while parsing. Keep that form valid
    // and test an unknown actual; source calls also exercise missing arity.
    const auto missingActual =
        prepared ? replaced(normalized, "%putEnvironment, %produced)", "%putEnvironment, %this)")
                 : replaced(normalized, "%putter(%owned, %produced)", "%putter(%owned)");
    variant(missingActual, false,
            prepared ? "an unknown prepared nullable consumer actual supplies no primitive seed"
                     : "an omitted nullable consumer actual is not a null seed");
    variant(replaced(normalized, "    ctjs.return %nullable", "    ctjs.return %state"), false,
            "a returned captured Map cannot borrow a finite nullable signature");
    const auto returnsFlag = replaced(normalized,
                                      "    %u = ctjs.constant #ctjs.undefined\n"
                                      "    ctjs.return %u\n  }\n}\n",
                                      "    %u = ctjs.constant #ctjs.boolean<false>\n"
                                      "    ctjs.return %u\n  }\n}\n");
    variant(returnsFlag, true, "an independently checked sibling may return an exact Boolean");
    const auto returningDependency =
        prepared
            ? replaced(returnsFlag, "%getEnvironment, %falseFlag)", "%getEnvironment, %putResult)")
            : replaced(returnsFlag, "%getter(%owned, %falseFlag)", "%getter(%owned, %putResult)");
    variant(returningDependency, true,
            "source-order get-to-put-to-get results close an independently checked Boolean census");
    const auto checkBudgets = [&](const std::string & program, const char * label,
                                  unsigned expectedCalls) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "nullable budget fixture parses");
        if (!module) { return; }
        auto contract = requested(*module);
        HostContractAnalysis complete(*module, contract);
        const unsigned completion = complete.steps();
        check(complete.proved() && completion < 20000,
              "nullable complete actual census stays within the fixture work limit");
        if (!complete.proved() || completion >= 20000) { return; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            HostContractAnalysis limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      withheld(*module, limited),
                  "every incomplete nullable budget withholds every result and parameter edge");
        }
        HostContractAnalysis exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion,
              "the exact nullable completion budget reproduces the complete census");
        if (exact.proved()) { edges(*module, exact, expectedCalls, nullableMask); }
        auto getter = module->lookupSymbol<ctjs::FuncOp>("get$2");
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
        auto returned = llvm::cast<ctjs::ReturnOp>(getter.getBody().front().getTerminator());
        auto selection = returned.getValue().getDefiningOp<mlir::scf::IfOp>();
        mlir::scf::IfOp normalization;
        setter.walk([&](mlir::scf::IfOp branch) { normalization = branch; });
        check(selection && normalization,
              "the live nullable fixture retains its producer and normalization branches");
        if (!selection || !normalization) { return; }
        mlir::Builder builder(&context);
        module->walk([&](mlir::Operation * operation) {
            if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp, mlir::scf::IfOp>(operation)) {
                operation->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
                operation->setAttr("ctnative.map_read_type", builder.getStringAttr("string"));
                operation->setAttr("ctnative.map_write_type", builder.getStringAttr("string"));
                operation->setAttr("ctnative.map_present", builder.getBoolAttr(true));
            }
        });
        contract = requested(*module);
        check(HostContractAnalysis(*module, contract).proved(),
              "forged reports leave a complete nullable proof independently reproducible");
        const auto refusal = [&]() {
            HostContractAnalysis old(*module, contract);
            check(!old.proved() && old.reason().contains("fingerprint") && withheld(*module, old),
                  "live nullable mutation invalidates the previous host fingerprint");
            HostContractAnalysis fresh(*module, requested(*module));
            check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                  "a fresh fingerprint and forged markers cannot repair a nullable proof");
        };
        const auto mutation = [&](mlir::Operation * operation, unsigned operand,
                                  mlir::Value replacement) {
            const auto previous = operation->getOperand(operand);
            operation->setOperand(operand, replacement);
            refusal();
            operation->setOperand(operand, previous);
            check(HostContractAnalysis(*module, contract).proved(),
                  "restoring the real nullable edge restores its complete proof");
        };
        auto nullYield =
            llvm::cast<mlir::scf::YieldOp>(selection.getElseRegion().front().getTerminator());
        auto normalizeTruthy = normalization.getCondition().getDefiningOp<ctjs::TruthyOp>();
        mutation(nullYield, 0, getter.getBody().front().getArgument(0));
        mutation(normalizeTruthy, 0, setter.getBody().front().getArgument(0));
        mlir::Operation * consumer = nullptr;
        for (const auto & call : exact.callables()) {
            if (call.function == setter) { consumer = call.call; }
        }
        check(consumer != nullptr, "the nullable consumer survives the live proof query");
        if (consumer) {
            const unsigned argument = prepared ? 4u : 2u;
            mutation(consumer, argument,
                     consumer->getParentOfType<ctjs::FuncOp>().getBody().front().getArgument(0));
            // An invalid cyclic SSA edit must not bootstrap the dependency
            // worklist from its own unproved result or a forged result marker.
            mutation(consumer, argument, consumer->getResult(0));
        }
        mlir::OpBuilder at(returned);
        auto effect =
            ctjs::StoreGlobalOp::create(at, returned.getLoc(), "trace", returned.getValue());
        refusal();
        effect.erase();
        check(HostContractAnalysis(*module, contract).proved(),
              "removing a late effect restores only the original complete nullable proof");
        std::printf("%s Map host %s: %u rows and all %u incomplete budgets checked\n", label,
                    prepared ? "prepared" : "source", rows, completion);
    };
    check(rows == 32, "all nullable result, actual-census and refusal rows ran");
    checkBudgets(normalized, "nullable", 3);
    checkBudgets(extraActual("#ctjs.null", true), "nullable census", 4);
    checkNullablePayloadResults(context, nullable, prepared);
}

void checkConditionalMapResults(mlir::MLIRContext & context, std::string source, bool prepared) {
    const std::string header =
        "@get$2(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value" +
        std::string(prepared ? ", %state: !ctjs.value" : "");
    source = replaced(source, header + ")", header + ", %flag: !ctjs.value)");
    source = replaced(source, "    %priorGetter = ctjs.get_property",
                      "    %trueFlag = ctjs.constant #ctjs.boolean<true>\n"
                      "    %falseFlag = ctjs.constant #ctjs.boolean<false>\n"
                      "    %priorGetter = ctjs.get_property");
    if (prepared) {
        source = replaced(source, "%priorEnvironment)", "%priorEnvironment, %trueFlag)");
        source = replaced(source, "%getEnvironment)", "%getEnvironment, %falseFlag)");
    } else {
        source = replaced(source, "%produced = ctjs.call %priorGetter(%owned)",
                          "%produced = ctjs.call %priorGetter(%owned, %trueFlag)");
        source = replaced(source, "%answer = ctjs.call %getter(%owned)",
                          "%answer = ctjs.call %getter(%owned, %falseFlag)");
    }
    constexpr llvm::StringLiteral continuation = R"MLIR(
    %changedPayload = ctjs.constant #ctjs.boolean<false>
    %overwritten = ctjs.call %mapSetter(%state, %seedKey, %changedPayload)
    %writeback = ctjs.call %mapSetter(%state, %seedKey, %saved)
    %answer = ctjs.call %mapGetter(%state, %seedKey)
)MLIR";
    constexpr llvm::StringLiteral selection = R"MLIR(
    %unused = arith.constant 0 : i32
    %branch = ctjs.truthy %flag
    %saved = scf.if %branch -> (!ctjs.value) {
      %left = ctjs.call %mapGetter(%state, %seedKey)
      scf.yield %left : !ctjs.value
    } else {
      %right = ctjs.call %mapGetter(%state, %probeKey)
      scf.yield %right : !ctjs.value
    }
)MLIR";
    source = replaced(source, "    %answer = ctjs.call %mapGetter(%state, %probeKey)",
                      selection.str() + continuation.str());
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto withheld = [](mlir::ModuleOp module, const HostContractAnalysis & result) {
        bool empty = result.callables().empty();
        module.walk([&](mlir::Operation * operation) {
            empty &= !result.callable(operation);
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                empty &= !result.property(read);
            }
        });
        return empty;
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & program, bool expected, const char * message,
                             mlir::TypeID tag = mlir::TypeID::get<ctjs::NumberAttr>()) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "source/prepared conditional Map fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
        HostContractAnalysis result(*module, contract);
        check(result.proved() == expected && !result.exhausted() &&
                  (expected || withheld(*module, result)),
              message);
        if (result.proved() != expected || result.exhausted()) {
            std::fprintf(stderr, "conditional Map host %s case %u: %s\n",
                         prepared ? "prepared" : "source", rows, result.reason().str().c_str());
        }
        if (expected && result.proved()) {
            auto getter = module->lookupSymbol<ctjs::FuncOp>("get$2");
            auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
            const auto calls = result.callables();
            check(calls.size() == 3 && calls[0].function == getter && calls[1].function == setter &&
                      calls[2].function == getter && calls[0].arguments.size() == 1 &&
                      calls[1].arguments.size() == 1 && calls[2].arguments.size() == 1 &&
                      calls[0].arguments.front().alternatives.tag() ==
                          mlir::TypeID::get<ctjs::BooleanAttr>() &&
                      calls[1].arguments.front().alternatives ==
                          ctcompile::ctnative::PrimitiveAlternatives::forTag(tag) &&
                      calls[1].arguments.front().actual == calls[0].call->getResult(0) &&
                      calls[1].arguments.front().parameter ==
                          setter.getBody().front().getArgument(prepared ? 4 : 3) &&
                      calls[0].call->isBeforeInBlock(calls[1].call) &&
                      calls[1].call->isBeforeInBlock(calls[2].call),
                  "conditional producer results reach the consumer without selecting a call value");
        }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "conditional proofs retain every source operation and operand");
    };
    variant(source, true, "same-tag scalar yields survive overwrite and later writeback");
    for (const auto & [payload, tag] :
         {std::pair{"#ctjs.boolean<true>", mlir::TypeID::get<ctjs::BooleanAttr>()},
          std::pair{"#ctjs.string<\"owned\">", mlir::TypeID::get<ctjs::StringAttr>()}}) {
        variant(replaced(source, "#ctjs.number<4607182418800017408>", payload), true,
                "Boolean and owning String joins retain independent scalar result tags", tag);
    }
    const auto missing =
        replaced(source, "%mapGetter(%state, %probeKey)", "%mapGetter(%state, %payload)");
    variant(missing, false, "one missing arm cannot supply a scalar tag to the consumer");
    variant(replaced(missing, "%branch = ctjs.truthy %flag",
                     "%always = ctjs.constant #ctjs.boolean<true>\n"
                     "    %branch = ctjs.truthy %always"),
            false, "a literal predicate cannot exempt the other arm from the complete body proof");
    variant(replaced(source, "scf.yield %right : !ctjs.value",
                     "%wrong = ctjs.constant #ctjs.boolean<false>\n"
                     "      scf.yield %wrong : !ctjs.value"),
            false, "different yielded tags cannot be joined into a definite consumer category");
    for (const char * yielded : {"%left", "%right"}) {
        const std::string terminator = std::string("scf.yield ") + yielded + " : !ctjs.value";
        variant(replaced(source, terminator,
                         "ctjs.store_global \"trace\", " + std::string(yielded) + "\n      " +
                             terminator),
                false, "a late unsupported effect in either arm rejects the entire family");
        for (const char * escaped : {"%state", "%mapGetter"}) {
            variant(replaced(source, terminator,
                             std::string("scf.yield ") + escaped + " : !ctjs.value"),
                    false, "Map and method values cannot escape through a scalar yield");
        }
    }
    for (const char * object : {"%state", "%callee", "%this"}) {
        variant(replaced(source, "%branch = ctjs.truthy %flag",
                         std::string("%branch = ctjs.truthy ") + object),
                false, "truthiness cannot authorize a nonprimitive or unproved input");
    }
    const auto deleted = replaced(source, "      scf.yield %left : !ctjs.value", R"MLIR(
      %deleteKey = ctjs.constant #ctjs.string<"delete">
      %deleter = ctjs.get_property %state[%deleteKey]
      %erased = ctjs.call %deleter(%state, %seedKey)
      scf.yield %left : !ctjs.value
)MLIR");
    variant(deleted, true, "a saved scalar survives a source deletion on just one arm");
    variant(
        replaced(deleted, continuation, "    %answer = ctjs.call %mapGetter(%state, %seedKey)\n"),
        false, "contents intersection drops membership deleted on one reaching arm");
    const auto changed = replaced(source, "      scf.yield %left : !ctjs.value", R"MLIR(
      %boolean = ctjs.constant #ctjs.boolean<false>
      %written = ctjs.call %mapSetter(%state, %seedKey, %boolean)
      scf.yield %left : !ctjs.value
)MLIR");
    variant(
        replaced(changed, continuation, "    %answer = ctjs.call %mapGetter(%state, %seedKey)\n"),
        false, "contents intersection clears a payload changed on one reaching arm");
    variant(changed, true, "a joined saved scalar can reseed after a one-arm payload overwrite");
    for (const unsigned depth : {4u, 32u, 33u}) {
        std::string nested = "      %left = ctjs.call %mapGetter(%state, %seedKey)\n";
        for (unsigned index = 1; index < depth; ++index) {
            const std::string prior = index == 1 ? "%left" : "%nested" + std::to_string(index - 1);
            nested = "      %nested" + std::to_string(index) +
                     " = scf.if %branch -> (!ctjs.value) {\n" + nested + "      scf.yield " +
                     prior +
                     " : !ctjs.value\n"
                     "    } else {\n      scf.yield %payload : !ctjs.value\n    }\n";
        }
        nested += "      scf.yield %nested" + std::to_string(depth - 1) + " : !ctjs.value";
        variant(replaced(source,
                         "      %left = ctjs.call %mapGetter(%state, %seedKey)\n"
                         "      scf.yield %left : !ctjs.value",
                         nested),
                depth <= 32, "nested conditionals respect the bounded complete-body depth");
    }
    const auto noElse = replaced(source, "    %saved = scf.if", R"MLIR(
    scf.if %branch {
      %effect = ctjs.call %mapSetter(%state, %seedKey, %payload)
      scf.yield
    }
    %saved = scf.if)MLIR");
    variant(noElse, true,
            "an absent else preserves the incoming same-tag entry on its implicit path");
    const auto literalNoElse = replaced(noElse, "%branch = ctjs.truthy %flag",
                                        "%never = ctjs.constant #ctjs.boolean<false>\n"
                                        "    %branch = ctjs.truthy %never");
    constexpr llvm::StringLiteral effect =
        "      %effect = ctjs.call %mapSetter(%state, %seedKey, %payload)";
    variant(replaced(literalNoElse, effect, R"MLIR(
      %deleteKey = ctjs.constant #ctjs.string<"delete">
      %deleter = ctjs.get_property %state[%deleteKey]
      %effect = ctjs.call %deleter(%state, %seedKey)
)MLIR"),
            false, "a no-else delete loses membership even behind a literal false predicate");
    variant(replaced(literalNoElse, effect,
                     "      %different = ctjs.constant #ctjs.boolean<false>\n"
                     "      %effect = ctjs.call %mapSetter(%state, %seedKey, %different)"),
            false, "a no-else incompatible write loses its tag even behind a literal predicate");

    auto guarded = replaced(source, "%probeKey = ctjs.constant #ctjs.number<0>", R"MLIR(
    %probeKey = ctjs.constant #ctjs.number<4611686018427387904>
    %otherSeed = ctjs.call %mapSetter(%state, %probeKey, %payload)
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %hasKey = ctjs.constant #ctjs.string<"has">
    %mapHas = ctjs.get_property %state[%hasKey]
)MLIR");
    guarded = replaced(guarded, selection, R"MLIR(
    %branch = ctjs.truthy %flag
    scf.if %branch {
      %erased = ctjs.call %deleter(%state, %probeKey)
      scf.yield
    }
    %observed = ctjs.call %mapHas(%state, %probeKey)
    %guard = ctjs.truthy %observed
    %saved = scf.if %guard -> (!ctjs.value) {
      %left = ctjs.call %mapGetter(%state, %probeKey)
      scf.yield %left : !ctjs.value
    } else {
      %right = ctjs.call %mapGetter(%state, %seedKey)
      scf.yield %right : !ctjs.value
    }
)MLIR");
    variant(guarded, true, "a fresh has restores membership without inventing its payload tag");
    for (const auto & [payload, tag] :
         {std::pair{"#ctjs.boolean<true>", mlir::TypeID::get<ctjs::BooleanAttr>()},
          std::pair{"#ctjs.string<\"owned\">", mlir::TypeID::get<ctjs::StringAttr>()}}) {
        variant(replaced(guarded, "#ctjs.number<4607182418800017408>", payload), true,
                "guarded Boolean and String reads retain tags across a conditional deletion", tag);
    }
    constexpr llvm::StringLiteral observed =
        "    %observed = ctjs.call %mapHas(%state, %probeKey)\n";
    constexpr llvm::StringLiteral guard = "    %guard = ctjs.truthy %observed";
    constexpr llvm::StringLiteral erase = "%erased = ctjs.call %deleter(%state, %probeKey)";
    const auto staleHas = replaced(replaced(guarded, observed, ""), "    scf.if %branch {",
                                   observed.str() + "    scf.if %branch {");
    variant(staleHas, false, "a has snapshot cannot survive deletion on one structural arm");
    variant(replaced(guarded, guard,
                     "    %later = ctjs.call %deleter(%state, %probeKey)\n" + guard.str()),
            false, "a later same-key deletion invalidates the has implication");
    variant(replaced(guarded, observed, "    %observed = ctjs.call %mapHas(%state, %seedKey)\n"),
            false, "a live has for another key cannot restore the deleted key");
    variant(replaced(guarded, "%mapHas = ctjs.get_property %state[%hasKey]",
                     "%mapHas = ctjs.get_property %this[%hasKey]"),
            false, "an unproved Map receiver cannot provide a has guard");
    const auto noTag = replaced(
        guarded, "    %otherSeed = ctjs.call %mapSetter(%state, %probeKey, %payload)\n", "");
    variant(noTag, false, "has membership cannot type unknown contents from a prior invocation");
    variant(replaced(noTag, erase, "%erased = ctjs.call %mapSetter(%state, %probeKey, %payload)"),
            false, "a tag on one arm cannot be unioned with unknown incoming contents");
    variant(replaced(guarded, erase,
                     "%different = ctjs.constant #ctjs.boolean<false>\n"
                     "      %erased = ctjs.call %mapSetter(%state, %probeKey, %different)"),
            false, "has membership cannot choose between differing payload tags across a join");
    for (const char * literal : {"true", "false"}) {
        variant(replaced(guarded, guard,
                         "    %literal = ctjs.constant #ctjs.boolean<" + std::string(literal) +
                             ">\n    %guard = ctjs.truthy %literal"),
                false, "a literal predicate supplies no missing membership proof on either arm");
    }
    auto premature =
        replaced(guarded, observed,
                 "    %premature = ctjs.call %mapGetter(%state, %probeKey)\n" + observed.str());
    premature = replaced(premature, "      %left = ctjs.call %mapGetter(%state, %probeKey)\n", "");
    variant(
        replaced(premature, "scf.yield %left : !ctjs.value", "scf.yield %premature : !ctjs.value"),
        false, "a later has guard cannot retroactively type an earlier read");
    const auto boolKey =
        replaced(guarded, "#ctjs.number<4611686018427387904>", "#ctjs.boolean<true>");
    variant(replaced(boolKey, observed,
                     "    %possible = ctjs.call %mapSetter(%state, %flag, %payload)\n" +
                         observed.str()),
            true, "possible same-tag writes preserve a guarded key's payload evidence");
    variant(
        replaced(boolKey, observed,
                 "    %possible = ctjs.call %mapSetter(%state, %flag, %flag)\n" + observed.str()),
        false, "a possible incompatible write clears the guarded payload evidence");
    variant(replaced(boolKey, guard,
                     "    %possible = ctjs.call %deleter(%state, %flag)\n" + guard.str()),
            false, "a possible alias deletion invalidates a saved has observation");
    variant(replaced(guarded, guard, R"MLIR(
    %disjoint = ctjs.constant #ctjs.number<4613937818241073152>
    %unrelated = ctjs.call %deleter(%state, %disjoint)
)MLIR" + guard.str()),
            true, "an independently disjoint deletion preserves the live has observation");
    for (const char * yielded : {"%left", "%right"}) {
        const std::string terminator = std::string("scf.yield ") + yielded + " : !ctjs.value";
        variant(replaced(guarded, terminator,
                         "ctjs.store_global \"trace\", " + std::string(yielded) + "\n      " +
                             terminator),
                false, "a has guard cannot hide an unsupported effect on either structural arm");
    }

    constexpr llvm::StringLiteral guardedSelection = R"MLIR(
    %saved = scf.if %guard -> (!ctjs.value) {
      %left = ctjs.call %mapGetter(%state, %probeKey)
      scf.yield %left : !ctjs.value
    } else {
      %right = ctjs.call %mapGetter(%state, %seedKey)
      scf.yield %right : !ctjs.value
    }
)MLIR";
    constexpr llvm::StringLiteral shortSelection = R"MLIR(
    %intermediate = scf.if %guard -> (!ctjs.value) {
      %left = ctjs.call %mapGetter(%state, %probeKey)
      scf.yield %left : !ctjs.value
    } else {
      scf.yield %observed : !ctjs.value
    }
    %selected = ctjs.truthy %intermediate
    %saved = scf.if %selected -> (!ctjs.value) {
      scf.yield %intermediate : !ctjs.value
    } else {
      %right = ctjs.call %mapGetter(%state, %seedKey)
      scf.yield %right : !ctjs.value
    }
)MLIR";
    const auto shortCircuit = replaced(guarded, guardedSelection, shortSelection);
    const auto shortString =
        replaced(shortCircuit, "#ctjs.number<4607182418800017408>", "#ctjs.string<\"owned\">");
    constexpr llvm::StringLiteral falseYield = "      scf.yield %observed : !ctjs.value";
    constexpr llvm::StringLiteral select = "    %selected = ctjs.truthy %intermediate";
    variant(shortCircuit, true, "false or Number alternatives produce a same-tag Number result");
    variant(shortString, true, "false or String alternatives produce a same-tag owning result",
            mlir::TypeID::get<ctjs::StringAttr>());
    variant(replaced(shortCircuit, "#ctjs.number<4607182418800017408>", "#ctjs.boolean<true>"),
            true, "Boolean short-circuit results retain their independent scalar tag",
            mlir::TypeID::get<ctjs::BooleanAttr>());
    for (const auto & [program, tag] :
         {std::pair{&shortCircuit, mlir::TypeID::get<ctjs::NumberAttr>()},
          std::pair{&shortString, mlir::TypeID::get<ctjs::StringAttr>()}}) {
        for (const char * literal :
             {"#ctjs.boolean<false>", "#ctjs.number<0>", "#ctjs.number<9223372036854775808>",
              "#ctjs.number<9221120237041090561>", "#ctjs.string<\"\">", "#ctjs.null",
              "#ctjs.undefined"}) {
            variant(replaced(*program, falseYield,
                             "      %falsy = ctjs.constant " + std::string(literal) +
                                 "\n      scf.yield %falsy : !ctjs.value"),
                    true, "known falsy alternatives stay internal to a same-tag scalar result",
                    tag);
        }
        variant(replaced(*program, falseYield,
                         "      %other = ctjs.constant #ctjs.boolean<true>\n"
                         "      scf.yield %other : !ctjs.value"),
                false, "a genuinely truthy Boolean cannot disappear from a mixed scalar result");
        variant(replaced(*program, falseYield, "      scf.yield %flag : !ctjs.value"), false,
                "an unrelated Boolean retains its true alternative on the false has arm");
    }
    variant(replaced(shortCircuit, "      %left = ctjs.call %mapGetter(%state, %probeKey)",
                     "      %left = ctjs.call %mapGetter(%state, %payload)"),
            false, "truthiness cannot establish a scalar tag for a missing guarded read");
    variant(replaced(shortCircuit, "      %right = ctjs.call %mapGetter(%state, %seedKey)",
                     "      %right = ctjs.call %mapGetter(%state, %payload)"),
            false, "the short-circuit fallback needs its own definite same-tag read");
    variant(
        replaced(shortCircuit, observed, "    %observed = ctjs.call %mapHas(%state, %seedKey)\n"),
        false, "a different-key has cannot type the short-circuit's guarded read");
    variant(replaced(shortCircuit,
                     "    %otherSeed = ctjs.call %mapSetter(%state, %probeKey, "
                     "%payload)\n",
                     ""),
            false, "a short-circuit cannot turn unknown prior Map contents into a scalar");
    for (const bool truth : {false, true}) {
        variant(
            replaced(shortCircuit, erase,
                     "%different = ctjs.constant #ctjs.boolean<" +
                         std::string(truth ? "true" : "false") +
                         ">\n      %erased = ctjs.call %mapSetter(%state, %probeKey, %different)"),
            !truth,
            truth ? "a truthy Boolean payload survives refinement and keeps a mixed result"
                  : "an exact false payload takes the independently proved Number fallback");
    }
    variant(replaced(replaced(shortCircuit, observed, ""), "    scf.if %branch {",
                     observed.str() + "    scf.if %branch {"),
            false, "a short-circuit cannot reuse has membership from before conditional deletion");
    variant(replaced(shortCircuit, guard,
                     "    %later = ctjs.call %deleter(%state, %probeKey)\n" + guard.str()),
            true, "exact deletion gives the stale guard an Undefined read and Number fallback");
    for (const char * literal : {"true", "false"}) {
        variant(replaced(shortCircuit, guard,
                         "    %literal = ctjs.constant #ctjs.boolean<" + std::string(literal) +
                             ">\n    %guard = ctjs.truthy %literal"),
                false, "literal predicates cannot hide the short-circuit's missing read arm");
    }
    variant(replaced(shortCircuit, select, "    %selected = ctjs.truthy %flag"), false,
            "truthiness of another SSA value cannot refine an intermediate union");
    variant(replaced(shortCircuit, "%writeback = ctjs.call %mapSetter(%state, %seedKey, %saved)",
                     "%writeback = ctjs.call %mapSetter(%state, %seedKey, %intermediate)"),
            false, "the outer true arm cannot leak its refinement past the structural join");
    for (const char * yielded : {"%left", "%observed", "%intermediate", "%right"}) {
        const std::string terminator = std::string("scf.yield ") + yielded + " : !ctjs.value";
        variant(replaced(shortCircuit, terminator,
                         "ctjs.store_global \"trace\", " + std::string(yielded) + "\n      " +
                             terminator),
                false, "every short-circuit arm is checked through its last effect");
    }
    variant(replaced(shortString, select,
                     "    %afterRead = ctjs.call %deleter(%state, %probeKey)\n" + select.str()),
            true, "a saved false or String result survives deletion before truthy refinement",
            mlir::TypeID::get<ctjs::StringAttr>());
    variant(replaced(shortString, select,
                     "    %replacement = ctjs.constant #ctjs.boolean<false>\n"
                     "    %afterRead = ctjs.call %mapSetter(%state, %probeKey, %replacement)\n" +
                         select.str()),
            true, "a saved scalar alternative is independent of subsequent payload overwrites",
            mlir::TypeID::get<ctjs::StringAttr>());

    const auto checkBudgets = [&](const std::string & program, const char * label) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        if (!module) { return; }
        auto contract = requested(*module);
        HostContractAnalysis complete(*module, contract);
        const unsigned completion = complete.steps();
        check(complete.proved() && completion < 15000,
              "the complete conditional proof stays within its fixture work limit");
        if (!complete.proved() || completion >= 15000) { return; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            HostContractAnalysis limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      withheld(*module, limited),
                  "every incomplete conditional budget withholds all callable and argument edges");
        }
        HostContractAnalysis exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion && exact.callables().size() == 3,
              "the exact conditional budget reproduces the completed family");
        auto getter = module->lookupSymbol<ctjs::FuncOp>("get$2");
        mlir::scf::IfOp branch, guardedBranch;
        ctjs::CallOp seed, left, right, observedCall;
        getter.walk([&](mlir::scf::IfOp conditional) {
            branch = conditional;
            auto truthy = conditional.getCondition().getDefiningOp<ctjs::TruthyOp>();
            if (truthy) {
                if (auto call = truthy.getValue().getDefiningOp<ctjs::CallOp>()) {
                    observedCall = call;
                    guardedBranch = conditional;
                }
            }
        });
        getter.walk([&](ctjs::CallOp call) {
            if (!seed) { seed = call; }
            if (branch && call->getParentRegion() == &branch.getElseRegion()) { right = call; }
            if (guardedBranch && call->getParentRegion() == &guardedBranch.getThenRegion()) {
                left = call;
            }
        });
        check(branch && seed && right,
              "the live conditional fixture retains its seed and both arms");
        if (!branch || !seed || !right) { return; }
        mlir::Builder builder(&context);
        right->setAttr("ctnative.map_present", builder.getBoolAttr(true));
        right->setAttr("ctnative.map_read_type", builder.getStringAttr("number"));
        branch->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
        contract = requested(*module);
        check(HostContractAnalysis(*module, contract).proved(),
              "forged reports leave an otherwise complete conditional proof reproducible");
        const auto originalKey = right.getArgs()[0];
        right->setOperand(2, seed.getArgs()[1]);
        HostContractAnalysis stale(*module, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && withheld(*module, stale),
              "a changed conditional arm cannot reuse the previous fingerprint");
        HostContractAnalysis fresh(*module, requested(*module));
        check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
              "fresh forged tags cannot recover the missing arm or its consumer argument");
        right->setOperand(2, originalKey);
        check(HostContractAnalysis(*module, contract).proved(),
              "restoring the real arm restores its complete conditional result proof");
        if (observedCall) {
            const auto watchedKey = observedCall.getArgs()[0];
            observedCall->setOperand(2, seed.getArgs()[0]);
            HostContractAnalysis staleGuard(*module, contract);
            check(!staleGuard.proved() && staleGuard.reason().contains("fingerprint") &&
                      withheld(*module, staleGuard),
                  "a live guard-key edit cannot reuse its earlier host fingerprint");
            HostContractAnalysis freshGuard(*module, requested(*module));
            check(!freshGuard.proved() && !freshGuard.exhausted() && withheld(*module, freshGuard),
                  "a fresh forged report cannot turn a different-key has into membership");
            observedCall->setOperand(2, watchedKey);
            check(HostContractAnalysis(*module, contract).proved(),
                  "restoring the real guard restores its complete saved-value proof");
        }
        if (guardedBranch && guardedBranch != branch && left) {
            left->setAttr("ctnative.map_present", builder.getBoolAttr(true));
            left->setAttr("ctnative.map_read_type", builder.getStringAttr("string"));
            guardedBranch->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            contract = requested(*module);
            check(HostContractAnalysis(*module, contract).proved(),
                  "forged intermediate tags leave the complete short-circuit proof reproducible");
            const auto mutation = [&](mlir::Operation * operation, unsigned operand,
                                      mlir::Value replacement) {
                const auto previous = operation->getOperand(operand);
                operation->setOperand(operand, replacement);
                HostContractAnalysis old(*module, contract);
                check(!old.proved() && old.reason().contains("fingerprint") &&
                          withheld(*module, old),
                      "a changed short-circuit SSA edge invalidates its previous fingerprint");
                HostContractAnalysis changed(*module, requested(*module));
                check(!changed.proved() && !changed.exhausted() && withheld(*module, changed),
                      "fresh forged tags cannot recover an invalid short-circuit scalar proof");
                operation->setOperand(operand, previous);
                check(HostContractAnalysis(*module, contract).proved(),
                      "restoring a real short-circuit SSA edge restores the complete proof");
            };
            auto truthy = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
            auto falseArm = llvm::cast<mlir::scf::YieldOp>(
                guardedBranch.getElseRegion().front().getTerminator());
            const auto flag = getter.getBody().front().getArgument(prepared ? 4 : 3);
            mutation(left, 2, seed.getArgs()[1]);
            mutation(truthy, 0, flag);
            mutation(falseArm, 0, flag);
        }
        std::printf("%s Map host %s: %u rows and all %u incomplete budgets checked\n", label,
                    prepared ? "prepared" : "source", rows, completion);
    };
    checkBudgets(source, "conditional");
    checkBudgets(guarded, "guarded");
    checkBudgets(shortString, "short-circuit");
    checkNullableMapResults(context, shortString, prepared);
}

void checkSeededMapResults(mlir::MLIRContext & context, const std::string & shared) {
    auto source =
        replaced(shared, "@put$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)",
                 "@put$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                 "%entryKey: !ctjs.value)");
    source = replaced(source, "    %entryKey = ctjs.constant #ctjs.string<\"x\">\n", "");
    source = replaced(source, "    %putResult = ctjs.call %putter(%owned)",
                      "    %priorGetter = ctjs.get_property %owned[%key]\n"
                      "    %produced = ctjs.call %priorGetter(%owned)\n"
                      "    %putResult = ctjs.call %putter(%owned, %produced)");
    source = replaced(source,
                      "    %key = ctjs.constant #ctjs.string<\"size\">\n"
                      "    %answer = ctjs.get_property %state[%key]",
                      R"MLIR(
    %seedKey = ctjs.constant #ctjs.number<0>
    %payload = ctjs.constant #ctjs.number<4607182418800017408>
    %setKey = ctjs.constant #ctjs.string<"set">
    %mapSetter = ctjs.get_property %state[%setKey]
    %seeded = ctjs.call %mapSetter(%state, %seedKey, %payload)
    %getKey = ctjs.constant #ctjs.string<"get">
    %mapGetter = ctjs.get_property %state[%getKey]
    %probeKey = ctjs.constant #ctjs.number<0>
    %answer = ctjs.call %mapGetter(%state, %probeKey)
)MLIR");
    const auto prepare = [](std::string text) {
        for (const char * name : {"get$2", "put$3"}) {
            text = replaced(text, "captures %cell", "captures %state");
            text = replaced(text,
                            std::string("@") + name +
                                "(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value",
                            std::string("@") + name +
                                "(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                                "%state: !ctjs.value");
            text = replaced(text,
                            "attributes {upvalue_count = 1 : i32} {\n"
                            "    %state = ctjs.load_upvalue %callee[0]\n",
                            "attributes {upvalue_count = 0 : i32} {\n");
        }
        text = replaced(text, "%produced = ctjs.call %priorGetter(%owned)",
                        "%priorEnvironment = ctjs.load_upvalue %priorGetter[0]\n"
                        "    %produced = ctjs.call_direct @get$2(%owned, %u, %priorGetter, "
                        "%priorEnvironment)");
        text = replaced(text, "%putResult = ctjs.call %putter(%owned, %produced)",
                        "%putEnvironment = ctjs.load_upvalue %putter[0]\n"
                        "    %putResult = ctjs.call_direct @put$3(%owned, %u, %putter, "
                        "%putEnvironment, %produced)");
        return replaced(text, "%answer = ctjs.call %getter(%owned)",
                        "%getEnvironment = ctjs.load_upvalue %getter[0]\n"
                        "    %answer = ctjs.call_direct @get$2(%owned, %u, %getter, "
                        "%getEnvironment)");
    };
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto empty = [](mlir::ModuleOp module, const HostContractAnalysis & query) {
        bool withheld = query.callables().empty();
        module.walk([&](mlir::Operation * operation) {
            withheld &= !query.callable(operation);
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                withheld &= !query.property(read);
            }
        });
        return withheld;
    };
    const auto singleKeySource = source;
    for (const bool prepared : {false, true}) {
        checkConditionalMapResults(context, prepared ? prepare(source) : source, prepared);
    }
    for (const bool multiple : {false, true}) {
        source = singleKeySource;
        if (multiple) {
            source = replaced(source, "    %getKey = ctjs.constant",
                              "    %otherKey = ctjs.constant #ctjs.number<4611686018427387904>\n"
                              "    %otherSeed = ctjs.call %mapSetter(%state, %otherKey, %payload)\n"
                              "    %getKey = ctjs.constant");
        }
        for (const bool prepared : {false, true}) {
            auto module = mlir::parseSourceString<mlir::ModuleOp>(
                prepared ? prepare(source) : source, &context);
            check(static_cast<bool>(module), "source/prepared seeded Map result fixture parses");
            if (!module) { continue; }
            auto contract = requested(*module);
            HostContractAnalysis query(*module, contract);
            check(query.proved(),
                  "live per-key set facts independently type the same-key get result");
            if (!query.proved()) {
                std::fprintf(stderr, "seeded host: %s\n", query.reason().str().c_str());
                continue;
            }
            const auto calls = query.callables();
            check(calls.size() == 3 && calls[1].arguments.size() == 1,
                  "seeded producer, consuming setter and final getter remain distinct live calls");
            if (calls.size() != 3 || calls[1].arguments.size() != 1) { continue; }
            auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
            auto getter = module->lookupSymbol<ctjs::FuncOp>("get$2");
            check(calls[0].function == getter && calls[1].function == setter &&
                      calls[2].function == getter &&
                      calls[0].call->isBeforeInBlock(calls[1].call) &&
                      calls[1].call->isBeforeInBlock(calls[2].call) &&
                      calls[1].arguments.front().actual == calls[0].call->getResult(0) &&
                      calls[1].arguments.front().parameter ==
                          setter.getBody().front().getArgument(prepared ? 4 : 3) &&
                      calls[1].arguments.front().alternatives.tag() ==
                          mlir::TypeID::get<ctjs::NumberAttr>(),
                  "the consuming formal keeps the producer SSA result and prepared capture offset");
            check(hostContractFingerprint(*module) == contract.moduleSha256,
                  "presence and result proofs leave source calls and operands unchanged");
            const unsigned completion = query.steps();
            check(completion < 15000, "seeded presence proof stays within its fixture work limit");
            if (completion < 15000) {
                for (unsigned budget = 0; budget < completion; ++budget) {
                    HostContractAnalysis limited(*module, contract, budget);
                    check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                              empty(*module, limited),
                          "every incomplete presence budget withholds the entire callable family");
                }
                HostContractAnalysis exact(*module, contract, completion);
                check(exact.proved() && exact.steps() == completion &&
                          exact.callables().size() == 3,
                      "the exact presence completion budget reproduces every live result edge");
            }
            ctjs::CallOp seed, lookup;
            getter.walk([&](ctjs::CallOp call) {
                if (!seed) { seed = call; }
                lookup = call;
            });
            check(seed && lookup && seed != lookup, "the live body retains its seed and lookup");
            if (!seed || !lookup || seed == lookup) { continue; }
            mlir::Builder builder(&context);
            lookup->setAttr("ctnative.map_present", builder.getBoolAttr(true));
            lookup->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            contract = requested(*module);
            check(HostContractAnalysis(*module, contract).proved(),
                  "forged presence reports do not replace the live seed/get proof");
            const auto originalKey = lookup.getArgs().front();
            lookup->setOperand(2, seed.getArgs().back());
            HostContractAnalysis stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      empty(*module, stale),
                  "changing the queried key invalidates the earlier presence fingerprint");
            HostContractAnalysis mismatch(*module, requested(*module));
            check(!mismatch.proved() && empty(*module, mismatch),
                  "a fresh fingerprint and forged presence cannot prove a different get key");
            lookup->setOperand(2, originalKey);

            const auto originalPayload = seed.getArgs().back();
            for (mlir::Attribute payload :
                 {mlir::Attribute(ctjs::BooleanAttr::get(&context, true)),
                  mlir::Attribute(ctjs::StringAttr::get(&context, "owned"))}) {
                mlir::OpBuilder at(seed);
                auto changedPayload = ctjs::ConstantOp::create(at, seed.getLoc(), payload);
                seed->setOperand(3, changedPayload.getResult());
                HostContractAnalysis old(*module, contract);
                check(!old.proved() && old.reason().contains("fingerprint") && empty(*module, old),
                      "a payload mutation cannot reuse the previous result fingerprint");
                HostContractAnalysis changed(*module, requested(*module));
                check(changed.proved() && changed.callables().size() == 3 &&
                          changed.callables()[1].arguments.size() == 1 &&
                          changed.callables()[1].arguments.front().actual ==
                              calls[0].call->getResult(0) &&
                          changed.callables()[1].arguments.front().alternatives.tag() ==
                              payload.getTypeID(),
                      "boolean/string payloads independently retype the consumer without a carrier "
                      "promise");
                seed->setOperand(3, originalPayload);
                changedPayload.erase();
            }
            seed->setOperand(3, getter.getBody().front().getArgument(0));
            HostContractAnalysis unknown(*module, requested(*module));
            check(!unknown.proved() && empty(*module, unknown),
                  "an unproved payload cannot inherit the last write's earlier primitive tag");
            seed->setOperand(3, originalPayload);
            check(HostContractAnalysis(*module, contract).proved(),
                  "restoring the live key and payload restores the independent presence proof");
            std::printf("%s Map host %s proof and all %u incomplete budgets checked\n",
                        multiple ? "per-key" : "seeded", prepared ? "prepared" : "source",
                        completion);

            const auto variant = [&](const std::string & program, bool expected,
                                     const char * message,
                                     mlir::TypeID expectedTag =
                                         mlir::TypeID::get<ctjs::NumberAttr>(),
                                     bool checkJoins = false) {
                auto changed = mlir::parseSourceString<mlir::ModuleOp>(
                    prepared ? prepare(program) : program, &context);
                check(static_cast<bool>(changed), "seeded Map presence variant parses");
                if (!changed) { return; }
                HostContractAnalysis result(*changed, requested(*changed));
                check(result.proved() == expected && !result.exhausted() &&
                          (expected || empty(*changed, result)),
                      message);
                if (expected && result.proved()) {
                    auto consumer = changed->lookupSymbol<ctjs::FuncOp>("put$3");
                    check(result.callables().size() == 3 &&
                              result.callables()[1].arguments.size() == 1 &&
                              result.callables()[1].arguments.front().alternatives ==
                                  ctcompile::ctnative::PrimitiveAlternatives::forTag(expectedTag) &&
                              result.callables()[1].arguments.front().actual ==
                                  result.callables()[0].call->getResult(0) &&
                              result.callables()[1].arguments.front().parameter ==
                                  consumer.getBody().front().getArgument(prepared ? 4 : 3),
                          "the exact result alternatives reach the consuming formal through its "
                          "original SSA edge");
                }
                if (!checkJoins || !result.proved()) { return; }
                const auto joinedContract = requested(*changed);
                const unsigned joinedCompletion = result.steps();
                check(joinedCompletion < 15000,
                      "joined payload proof stays within its fixture work limit");
                if (joinedCompletion < 15000) {
                    for (unsigned budget = 0; budget < joinedCompletion; ++budget) {
                        HostContractAnalysis limited(*changed, joinedContract, budget);
                        check(!limited.proved() && limited.exhausted() &&
                                  limited.steps() <= budget && empty(*changed, limited),
                              "every incomplete join budget withholds the entire callable family");
                    }
                    HostContractAnalysis exact(*changed, joinedContract, joinedCompletion);
                    check(exact.proved() && exact.steps() == joinedCompletion &&
                              exact.callables().size() == 3,
                          "the exact join completion budget reproduces every live result edge");
                }
                auto joinedGetter = changed->lookupSymbol<ctjs::FuncOp>("get$2");
                llvm::SmallVector<ctjs::CallOp> operations;
                joinedGetter.walk([&](ctjs::CallOp call) { operations.push_back(call); });
                check(operations.size() == 3, "live join fixture retains both sets and its get");
                if (operations.size() != 3) { return; }
                auto first = operations[0], possible = operations[1], get = operations[2];
                const auto writtenKey = possible.getArgs()[0];
                const auto writtenPayload = possible.getArgs()[1];
                const auto queriedKey = get.getArgs()[0];
                const auto assertTag = [&](mlir::TypeID tag, const char * reason) {
                    HostContractAnalysis fresh(*changed, requested(*changed));
                    check(fresh.proved() && fresh.callables().size() == 3 &&
                              fresh.callables()[1].arguments.size() == 1 &&
                              fresh.callables()[1].arguments.front().alternatives.tag() == tag,
                          reason);
                };
                for (mlir::Attribute payload :
                     {mlir::Attribute(ctjs::BooleanAttr::get(&context, true)),
                      mlir::Attribute(ctjs::StringAttr::get(&context, "joined"))}) {
                    mlir::OpBuilder at(possible);
                    auto replacement = ctjs::ConstantOp::create(at, possible.getLoc(), payload);
                    possible->setOperand(3, replacement.getResult());
                    HostContractAnalysis staleJoin(*changed, joinedContract);
                    check(!staleJoin.proved() && staleJoin.reason().contains("fingerprint") &&
                              empty(*changed, staleJoin),
                          "a changed possible payload invalidates the original join fingerprint");
                    get->setAttr("ctnative.map_present", at.getBoolAttr(true));
                    get->setAttr("ctnative.host_proved", at.getBoolAttr(true));
                    HostContractAnalysis mixed(*changed, requested(*changed));
                    check(!mixed.proved() && !mixed.exhausted() && empty(*changed, mixed),
                          "fresh forged presence cannot retain a tag across incompatible payloads");
                    possible->setOperand(2, first.getArgs()[0]);
                    assertTag(payload.getTypeID(),
                              "an exact same-key overwrite replaces rather than joins its tag");
                    possible->setOperand(2, writtenKey);
                    get->setOperand(2, writtenKey);
                    assertTag(payload.getTypeID(),
                              "the new write has its own definite payload despite an unknown join");
                    get->setOperand(2, queriedKey);
                    possible->setOperand(3, writtenPayload);
                    replacement.erase();
                    assertTag(mlir::TypeID::get<ctjs::NumberAttr>(),
                              "restoring the live payload restores the independent joined tag");
                }
                get->setOperand(2, writtenPayload);
                HostContractAnalysis absent(*changed, requested(*changed));
                check(!absent.proved() && empty(*changed, absent),
                      "a numeric dynamic write cannot prove a different literal is present");
                get->setOperand(2, queriedKey);
                assertTag(mlir::TypeID::get<ctjs::NumberAttr>(),
                          "restoring the queried key restores the joined result proof");
                std::printf("joined Map host %s proof and all %u incomplete budgets checked\n",
                            prepared ? "prepared" : "source", joinedCompletion);
            };
            constexpr llvm::StringLiteral seedLine =
                "    %seeded = ctjs.call %mapSetter(%state, %seedKey, %payload)";
            variant(replaced(source, "ctjs.call %mapGetter(%state, %probeKey)",
                             "ctjs.call %mapGetter(%state, %seedKey)"),
                    true,
                    "identical SSA keys establish the same local presence as equal constants");
            variant(replaced(source, seedLine,
                             seedLine.str() + "\n    %again = ctjs.call %mapSetter(%state, "
                                              "%seedKey, %payload)"),
                    true, "a same-key overwrite replaces the last local write fact");
            variant(replaced(source, seedLine, ""), false,
                    "a sibling's prior mutation cannot seed this method's initial presence");
            variant(replaced(source, seedLine,
                             seedLine.str() + "\n    %other = ctjs.call %mapSetter(%state, "
                                              "%payload, %payload)"),
                    true, "an independently distinct key preserves the earlier payload fact");
            variant(replaced(source, seedLine, seedLine.str() + R"MLIR(
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %deleted = ctjs.call %deleter(%state, %seedKey)
)MLIR"),
                    true, "exact deletion proves an Undefined result after invalidating presence",
                    mlir::TypeID::get<ctjs::UndefinedAttr>());
            variant(replaced(source, seedLine, seedLine.str() + R"MLIR(
    %hasKey = ctjs.constant #ctjs.string<"has">
    %hasMethod = ctjs.get_property %state[%hasKey]
    %present = ctjs.call %hasMethod(%state, %seedKey)
)MLIR"),
                    true, "a read-only has preserves the independently seeded get result");
            const std::string deleteOther = seedLine.str() + R"MLIR(
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %deleted = ctjs.call %deleter(%state, %payload)
)MLIR";
            variant(replaced(source, seedLine, deleteOther), true,
                    "deleting a distinct literal preserves the earlier entry");
            // An initial size has no nonempty proof. Keep this genuinely
            // possibly aliasing even after nonempty snapshots are understood.
            const std::string dynamicMutation = R"MLIR(
    %sizeKey = ctjs.constant #ctjs.string<"size">
    %dynamicKey = ctjs.get_property %state[%sizeKey]
)MLIR" + seedLine.str() + R"MLIR(
    %maybeAlias = ctjs.call %mapSetter(%state, %dynamicKey, %payload)
)MLIR";
            variant(replaced(source, seedLine, dynamicMutation), true,
                    "same-tag possible writes preserve presence and join their numeric payloads",
                    mlir::TypeID::get<ctjs::NumberAttr>(), !multiple);
            const auto incompatibleMutation = replaced(
                dynamicMutation,
                "    %maybeAlias = ctjs.call %mapSetter(%state, %dynamicKey, %payload)",
                "    %incompatible = ctjs.constant #ctjs.boolean<true>\n"
                "    %maybeAlias = ctjs.call %mapSetter(%state, %dynamicKey, %incompatible)");
            variant(replaced(source, seedLine, incompatibleMutation), false,
                    "possible bool/number overwrites lose the tag without losing presence");
            variant(
                replaced(source, seedLine,
                         incompatibleMutation +
                             "    %later = ctjs.call %mapSetter(%state, %dynamicKey, %payload)\n"),
                false, "a later possible numeric write cannot restore an unknown earlier tag");
            const std::string reseed =
                "    %reseed = ctjs.call %mapSetter(%state, %seedKey, %payload)\n";
            variant(replaced(source, seedLine, incompatibleMutation + reseed), true,
                    "an exact-key reseed restores its tag after an incompatible possible write");
            const auto unknownMutation = replaced(
                dynamicMutation,
                "    %maybeAlias = ctjs.call %mapSetter(%state, %dynamicKey, %payload)",
                "    %unknownGetKey = ctjs.constant #ctjs.string<\"get\">\n"
                "    %unknownGetter = ctjs.get_property %state[%unknownGetKey]\n"
                "    %unknownPayload = ctjs.call %unknownGetter(%state, %payload)\n"
                "    %maybeAlias = ctjs.call %mapSetter(%state, %dynamicKey, %unknownPayload)");
            variant(replaced(source, seedLine, unknownMutation), false,
                    "an unproved primitive payload loses a possibly overwritten entry's tag");
            variant(replaced(source, seedLine, unknownMutation + reseed), true,
                    "an exact-key reseed replaces an earlier unknown payload tag");
            variant(
                replaced(source, seedLine,
                         unknownMutation +
                             "    %later = ctjs.call %mapSetter(%state, %dynamicKey, %payload)\n"),
                false, "unknown payloads remain unknown across later possible numeric writes");
            variant(replaced(source, seedLine,
                             replaced(dynamicMutation,
                                      "%maybeAlias = ctjs.call %mapSetter(%state, "
                                      "%dynamicKey, %payload)",
                                      "%deleteKey = ctjs.constant #ctjs.string<\"delete\">\n"
                                      "    %deleter = ctjs.get_property %state[%deleteKey]\n"
                                      "    %deleted = ctjs.call %deleter(%state, %dynamicKey)")),
                    false, "a possibly aliasing delete invalidates earlier contents");
            const std::string sizeRead = R"MLIR(
    %sizeKey = ctjs.constant #ctjs.string<"size">
    %dynamicKey = ctjs.get_property %state[%sizeKey]
)MLIR";
            const std::string sizeDelete = R"MLIR(
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %deleted = ctjs.call %deleter(%state, %dynamicKey)
)MLIR";
            const auto nonempty =
                replaced(source, seedLine, seedLine.str() + sizeRead + sizeDelete);
            auto twoEntries =
                replaced(nonempty, seedLine,
                         seedLine.str() + "\n    %secondSeed = ctjs.call %mapSetter(%state, "
                                          "%payload, %payload)");
            twoEntries = replaced(twoEntries, "%probeKey = ctjs.constant #ctjs.number<0>",
                                  "%probeKey = ctjs.constant #ctjs.number<4607182418800017408>");
            variant(twoEntries, true,
                    "two independently distinct keys prove a size lower bound of two");
            variant(replaced(twoEntries,
                             "%secondSeed = ctjs.call %mapSetter(%state, %payload, %payload)",
                             "%secondSeed = ctjs.call %mapSetter(%state, %seedKey, %payload)"),
                    false, "a repeated runtime key cannot inflate the cardinality lower bound");
            auto savedTwo = replaced(twoEntries, sizeDelete, R"MLIR(
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %removeZero = ctjs.call %deleter(%state, %seedKey)
    %deleted = ctjs.call %deleter(%state, %dynamicKey)
)MLIR");
            variant(savedTwo, true, "saved size bounds survive a later cardinality decrease");
            constexpr llvm::StringLiteral sizeLine =
                "    %dynamicKey = ctjs.get_property %state[%sizeKey]\n";
            auto decreased = replaced(savedTwo, sizeLine, "");
            decreased = replaced(decreased, "    %deleted = ctjs.call",
                                 sizeLine.str() + "    %deleted = ctjs.call");
            variant(decreased, false,
                    "a size read after deletion cannot inherit the earlier cardinality");
            variant(nonempty, true, "a nonempty size snapshot cannot delete the definite zero key");
            variant(replaced(source, seedLine, seedLine.str() + sizeRead + R"MLIR(
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %emptied = ctjs.call %deleter(%state, %seedKey)
    %reseeded = ctjs.call %mapSetter(%state, %seedKey, %payload)
    %deleted = ctjs.call %deleter(%state, %dynamicKey)
)MLIR"),
                    true, "later mutations cannot change an already-read nonempty size number");
            auto positiveKey =
                replaced(nonempty, "%seedKey = ctjs.constant #ctjs.number<0>",
                         "%seedKey = ctjs.constant #ctjs.number<4607182418800017408>");
            positiveKey = replaced(positiveKey, "%probeKey = ctjs.constant #ctjs.number<0>",
                                   "%probeKey = ctjs.constant #ctjs.number<4607182418800017408>");
            variant(positiveKey, false,
                    "nonempty alone cannot distinguish a positive key from the size");
            const auto aliasingFacts = replaced(positiveKey, sizeDelete, R"MLIR(
    %possibleAlias = ctjs.call %mapSetter(%state, %dynamicKey, %payload)
    %currentSize = ctjs.get_property %state[%sizeKey]
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %deleted = ctjs.call %deleter(%state, %currentSize)
)MLIR");
            variant(aliasingFacts, false,
                    "different definite SSA keys may alias and cannot count as two entries");
            variant(replaced(twoEntries, "%seedKey = ctjs.constant #ctjs.number<0>",
                             "%seedKey = ctjs.constant #ctjs.boolean<false>"),
                    true, "independent primitive tags also prove pairwise key disjointness");
            for (const char * bits : {"9223372036854775808", "13830554455654793216",
                                      "4602678819172646912", "9221120237041090561"}) {
                auto outside =
                    replaced(nonempty, "%seedKey = ctjs.constant #ctjs.number<0>",
                             std::string("%seedKey = ctjs.constant #ctjs.number<") + bits + ">");
                outside =
                    replaced(outside, "%probeKey = ctjs.constant #ctjs.number<0>",
                             std::string("%probeKey = ctjs.constant #ctjs.number<") + bits + ">");
                variant(outside, true,
                        "negative zero, negative, subunit and NaN keys cannot equal nonempty size");
            }
            if (!multiple) {
                const auto cardinalityFixture = [&](unsigned count, unsigned probe) {
                    std::string seeds;
                    for (unsigned i = 1; i < count; ++i) {
                        seeds += "\n    %key" + std::to_string(i) +
                                 " = ctjs.constant #ctjs.number<" +
                                 std::to_string(llvm::APFloat(static_cast<double>(i))
                                                    .bitcastToAPInt()
                                                    .getZExtValue()) +
                                 ">\n    %seed" + std::to_string(i) +
                                 " = ctjs.call %mapSetter(%state, %key" + std::to_string(i) +
                                 ", %payload)\n";
                    }
                    return replaced(replaced(nonempty, seedLine, seedLine.str() + seeds),
                                    "%probeKey = ctjs.constant #ctjs.number<0>",
                                    "%probeKey = ctjs.constant #ctjs.number<" +
                                        std::to_string(llvm::APFloat(static_cast<double>(probe))
                                                           .bitcastToAPInt()
                                                           .getZExtValue()) +
                                        ">");
                };
                variant(cardinalityFixture(3, 2), true,
                        "three definite keys establish a stronger bound than nonempty");
                variant(cardinalityFixture(64, 63), true,
                        "the candidate cap includes all 64 independently distinct witnesses");
                variant(cardinalityFixture(65, 64), false,
                        "a capped subset never supplies the unexamined sixty-fifth witness");
                for (const auto & bits :
                     {std::pair{"0", "9223372036854775808"},
                      std::pair{"9221120237041090561", "9221120237041090562"}}) {
                    auto aliases = cardinalityFixture(3, 2);
                    aliases = replaced(aliases, "%seedKey = ctjs.constant #ctjs.number<0>",
                                       std::string("%seedKey = ctjs.constant #ctjs.number<") +
                                           bits.first + ">");
                    aliases = replaced(
                        aliases, "%key1 = ctjs.constant #ctjs.number<4607182418800017408>",
                        std::string("%key1 = ctjs.constant #ctjs.number<") + bits.second + ">");
                    variant(aliases, false,
                            "SameValueZero duplicates never inflate a size bound past two");
                }
                for (const auto & specimen : {nonempty, twoEntries}) {
                    auto checked = mlir::parseSourceString<mlir::ModuleOp>(
                        prepared ? prepare(specimen) : specimen, &context);
                    check(static_cast<bool>(checked), "nonempty size budget fixture parses");
                    if (!checked) { continue; }
                    const auto sizeContract = requested(*checked);
                    HostContractAnalysis complete(*checked, sizeContract);
                    const unsigned sizeCompletion = complete.steps();
                    check(complete.proved() && sizeCompletion < 15000,
                          "complete size proof stays within the fixture work limit");
                    if (!complete.proved() || sizeCompletion >= 15000) { continue; }
                    for (unsigned budget = 0; budget < sizeCompletion; ++budget) {
                        HostContractAnalysis limited(*checked, sizeContract, budget);
                        check(!limited.proved() && limited.exhausted() &&
                                  limited.steps() <= budget && empty(*checked, limited),
                              "every incomplete size budget withholds the entire callable family");
                    }
                    HostContractAnalysis exact(*checked, sizeContract, sizeCompletion);
                    check(exact.proved() && exact.steps() == sizeCompletion &&
                              exact.callables().size() == 3,
                          "the exact size completion budget reproduces every live result edge");
                    auto sizeGetter = checked->lookupSymbol<ctjs::FuncOp>("get$2");
                    llvm::SmallVector<ctjs::CallOp> operations;
                    ctjs::GetPropertyOp size;
                    sizeGetter.walk([&](ctjs::CallOp call) { operations.push_back(call); });
                    sizeGetter.walk([&](ctjs::GetPropertyOp read) {
                        auto key = read.getKey().getDefiningOp<ctjs::ConstantOp>();
                        auto name = key ? llvm::dyn_cast<ctjs::StringAttr>(key.getValue())
                                        : ctjs::StringAttr{};
                        if (name && name.getValue() == "size") { size = read; }
                    });
                    check(size && (operations.size() == 3 || operations.size() == 4),
                          "the nonempty proof keeps seed, size, delete and get operations");
                    if (!size || (operations.size() != 3 && operations.size() != 4)) { continue; }
                    auto sizeSeed = operations[0], erased = operations[operations.size() - 2];
                    auto * seedNext = sizeSeed->getNextNode();
                    sizeSeed->moveAfter(size);
                    HostContractAnalysis staleSize(*checked, sizeContract);
                    check(!staleSize.proved() && staleSize.reason().contains("fingerprint") &&
                              empty(*checked, staleSize),
                          "moving size before seed invalidates the original fingerprint");
                    size->setAttr("ctnative.nonempty_size", builder.getBoolAttr(true));
                    HostContractAnalysis beforeSeed(*checked, requested(*checked));
                    check(
                        !beforeSeed.proved() && empty(*checked, beforeSeed),
                        "a fresh fingerprint and forged size marker cannot prove initial contents");
                    sizeSeed->moveBefore(seedNext);
                    erased->setOperand(2, operations.back().getArgs()[0]);
                    HostContractAnalysis equalDelete(*checked, requested(*checked));
                    check(!equalDelete.proved() && empty(*checked, equalDelete),
                          "an equal-key delete cannot inherit disjointness from the previous "
                          "operand");
                    erased->setOperand(2, size.getResult());
                    check(HostContractAnalysis(*checked, requested(*checked)).proved(),
                          "restoring the live size order and delete key restores its independent "
                          "proof");
                    std::printf(
                        "%zu-key Map size host %s proof and all %u incomplete budgets checked\n",
                        operations.size() - 2, prepared ? "prepared" : "source", sizeCompletion);
                }
            }
            auto stringSeed = replaced(source, "%seedKey = ctjs.constant #ctjs.number<0>",
                                       "%seedKey = ctjs.constant #ctjs.string<\"seed\">");
            stringSeed = replaced(stringSeed, "%probeKey = ctjs.constant #ctjs.number<0>",
                                  "%probeKey = ctjs.constant #ctjs.string<\"seed\">");
            variant(replaced(stringSeed, seedLine, dynamicMutation), true,
                    "independent number/string tags prove a runtime mutation key is disjoint");
            // Both zero encodings and every NaN payload denote the same Map key.
            // A different payload tag after an aliasing write must replace the old
            // fact; an aliasing delete must remove it, even with unequal attributes.
            for (const auto & [seedBits, aliasBits] :
                 {std::pair{"0", "9223372036854775808"},
                  std::pair{"9221120237041090561", "9221120237041090562"}}) {
                auto equalKey = replaced(source, "%seedKey = ctjs.constant #ctjs.number<0>",
                                         std::string("%seedKey = ctjs.constant #ctjs.number<") +
                                             seedBits + ">");
                equalKey = replaced(equalKey, "%probeKey = ctjs.constant #ctjs.number<0>",
                                    std::string("%probeKey = ctjs.constant #ctjs.number<") +
                                        aliasBits + ">");
                variant(equalKey, true, "SameValueZero proves equal signed-zero and NaN keys");
                auto overwriteAlias = replaced(
                    equalKey, "    %answer = ctjs.call %mapGetter",
                    "    %booleanPayload = ctjs.constant #ctjs.boolean<true>\n"
                    "    %overwritten = ctjs.call %mapSetter(%state, %probeKey, %booleanPayload)\n"
                    "    %answer = ctjs.call %mapGetter");
                variant(overwriteAlias, true,
                        "equal zero/NaN encodings replace rather than preserve the previous tag",
                        mlir::TypeID::get<ctjs::BooleanAttr>());
                auto eraseAlias =
                    replaced(equalKey, "    %answer = ctjs.call %mapGetter",
                             "    %deleteKey = ctjs.constant #ctjs.string<\"delete\">\n"
                             "    %deleter = ctjs.get_property %state[%deleteKey]\n"
                             "    %deleted = ctjs.call %deleter(%state, %probeKey)\n"
                             "    %answer = ctjs.call %mapGetter");
                variant(eraseAlias, true,
                        "SameValueZero deletion proves Undefined despite unequal zero or NaN bits",
                        mlir::TypeID::get<ctjs::UndefinedAttr>());
            }
        }
    }
}

} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    context.getOrLoadDialect<mlir::arith::ArithDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    const auto shared = sharedMapWithPutCall(sharedMapSource(capturedGetterSource()));
    checkEntryNumericResults(context, shared);
    checkLeafObjectPayloads(context, shared);
    checkNestedMapResults(context, shared);
    checkSeededMapResults(context, shared);
    if (failures == 0) { std::puts("host contract seeded Map result proofs passed"); }
    return failures == 0 ? 0 : 1;
}
