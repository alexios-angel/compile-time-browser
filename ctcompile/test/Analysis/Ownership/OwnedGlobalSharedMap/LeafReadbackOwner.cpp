#include "Tests.h"

namespace ctcompile::test::owned_global_shared_map {

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

    const auto stringLoaded =
        replaced(loaded, "#ctjs.number<4607182418800017408>", "#ctjs.string<\"saved field\">");
    const auto stringSavedAfter = replaced(
        replaced(savedAfter, "#ctjs.number<4607182418800017408>", "#ctjs.string<\"saved field\">"),
        "#ctjs.number<4611686018427387904>", "#ctjs.string<\"changed field\">");
    variant(stringLoaded, true, "an initialized owning String field has its own source read proof");
    variant(replaced(stringLoaded, "#ctjs.string<\"saved field\">", "#ctjs.string<\"\">"), true,
            "an empty String field is present rather than absent or Null");
    variant(stringSavedAfter, true,
            "a String field read through a saved object survives Map overwrite and deletion");
    variant(replaced(stringSavedAfter, "ctjs.set_property %value[%fieldKey], %two",
                     "ctjs.set_property %saved[%fieldKey], %two"),
            true, "String field aliases retain the exact current source allocation");
    const std::string loadedRead = "    %loaded = ctjs.get_property %saved[%fieldKey]\n";
    const std::string laterNumber =
        "    %laterNumber = ctjs.constant #ctjs.number<4611686018427387904>\n"
        "    ctjs.set_property %saved[%fieldKey], %laterNumber\n";
    const auto savedStringBeforeNumber =
        replaced(stringLoaded, loadedRead, loadedRead + laterNumber);
    variant(savedStringBeforeNumber, true,
            "a later Number overwrite does not retag the earlier String source read");
    variant(replaced(stringLoaded, loadedRead, laterNumber + loadedRead), true,
            "a mixed store census can prove ownership independently of native field admission");
    for (const char * absent : {"#ctjs.null", "#ctjs.undefined"}) {
        variant(replaced(stringLoaded, loadedRead,
                         "    %absent = ctjs.constant " + std::string(absent) +
                             "\n    ctjs.set_property %saved[%fieldKey], %absent\n" + loadedRead),
                true, "an explicit absent field value keeps definite own-field initialization");
    }
    variant(replaced(stringLoaded, "    ctjs.set_property %value[%fieldKey], %one\n", ""), false,
            "a String schema cannot initialize an unwritten current receiver");
    variant(replaced(stringLoaded, "%saved[%fieldKey]", "%saved[%getKey]"), false,
            "an initialized String field cannot prove a different property read");
    variant(replaced(stringLoaded, "    ctjs.set_property %value[%fieldKey], %one\n",
                     "    %other = ctjs.create_object\n"
                     "    ctjs.set_property %other[%fieldKey], %one\n"),
            false, "a String write to another allocation cannot establish this read's presence");
    variant(
        replaced(stringLoaded, loadedRead, "    %unknown = ctjs.call %this(%this)\n" + loadedRead),
        false, "unknown effects invalidate the complete String source field proof");
    variant(replaced(stringLoaded, "#ctjs.string<\"value\">", "#ctjs.string<\"__proto__\">"), false,
            "a prototype mutation cannot be admitted as an ordinary String field");
    check(rows == 26, "all String readback, mixed-storage and independent presence controls ran");

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

    for (const auto & [text, label] :
         {std::pair{same, "identity"},
          {savedAfter, "saved field"},
          {stringSavedAfter, "saved String field"},
          {savedStringBeforeNumber, "String before Number overwrite"}}) {
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

} // namespace ctcompile::test::owned_global_shared_map
