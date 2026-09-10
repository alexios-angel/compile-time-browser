#include "Tests.h"

namespace ctcompile::test::host_contract_seeded_maps {

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
    variant(replaced(loaded, "#ctjs.number<4607182418800017408>", "#ctjs.string<\"owned\">"), true,
            "String readback proves its source origin independently of native field storage");
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

} // namespace ctcompile::test::host_contract_seeded_maps
