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
