#include "Tests.h"

namespace ctcompile::test::host_contract_seeded_maps {

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

} // namespace ctcompile::test::host_contract_seeded_maps
