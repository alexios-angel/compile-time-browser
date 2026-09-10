#include "Tests.h"

namespace ctcompile::test::host_contract_seeded_maps {

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

} // namespace ctcompile::test::host_contract_seeded_maps
