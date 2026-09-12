#include "Tests.h"

namespace ctcompile::test::owned_global_shared_map {
namespace {

void checkChildMapOwner(mlir::MLIRContext & context, const std::string & source, bool lifted) {
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    auto nested = replaced(source, "    %value = ctjs.create_object", R"MLIR(
    %childConstructor = ctjs.load_global "Map"
    %value = ctjs.construct %childConstructor(%childConstructor)
)MLIR");
    nested = replaced(nested, "    %written = ctjs.call %setter(%state, %entryKey, %value)", R"MLIR(
    %written = ctjs.call %setter(%state, %entryKey, %value)
    %readKey = ctjs.constant #ctjs.string<"get">
    %reader = ctjs.get_property %state[%readKey]
    %saved = ctjs.call %reader(%state, %entryKey)
    %childSetter = ctjs.get_property %saved[%setKey]
    %payload = ctjs.constant #ctjs.number<4607182418800017408>
    %childWritten = ctjs.call %childSetter(%saved, %entryKey, %payload)
    %childReader = ctjs.get_property %childWritten[%readKey]
    %loaded = ctjs.call %childReader(%childWritten, %entryKey)
)MLIR");
    nested = replaced(nested,
                      "    %size = ctjs.get_property %state[%sizeKey]\n"
                      "    ctjs.return %size",
                      "    %size = ctjs.get_property %saved[%sizeKey]\n"
                      "    ctjs.return %loaded");
    const auto retained = replaced(nested, "    %childSetter = ctjs.get_property", R"MLIR(
    %clearKey = ctjs.constant #ctjs.string<"clear">
    %clearer = ctjs.get_property %state[%clearKey]
    %cleared = ctjs.call %clearer(%state)
    %childSetter = ctjs.get_property)MLIR");
    const auto conditionalize = [&](std::string program) {
        program = replaced(program,
                           "    %childConstructor = ctjs.load_global \"Map\"\n"
                           "    %value = ctjs.construct %childConstructor(%childConstructor)\n",
                           "");
        program = replaced(program, "    %written = ctjs.call %setter(%state, %entryKey, %value)",
                           R"MLIR(
    %hasKey = ctjs.constant #ctjs.string<"has">
    %hasMethod = ctjs.get_property %state[%hasKey]
    %found = ctjs.call %hasMethod(%state, %entryKey)
    %condition = ctjs.truthy %found
    scf.if %condition {
      scf.yield
    } else {
      %childConstructor = ctjs.load_global "Map"
      %value = ctjs.construct %childConstructor(%childConstructor)
      %written = ctjs.call %setter(%state, %entryKey, %value)
      scf.yield
    }
)MLIR");
        return replaced(program, "    ctjs.store_global \"trace\", %answer",
                        "    ctjs.store_global \"trace\", %putResult\n"
                        "    %observed = ctjs.load_global \"trace\"");
    };
    const auto conditional = conditionalize(nested);
    for (const auto & [program, cleared, guarded] : {std::tuple{nested, false, false},
                                                     {retained, true, false},
                                                     {conditional, false, true},
                                                     {conditionalize(retained), true, true}}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "source/prepared nested child owner fixture parses");
        if (!module) { continue; }
        auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        check(query.proved() && query.roots().size() == 1,
              "a captured outer Map retains a distinct fresh child and its saved readback");
        if (!query.proved() || query.roots().empty()) {
            std::fprintf(stderr, "child Map owner %s %s %s: %s\n", lifted ? "prepared" : "source",
                         guarded ? "conditional" : "fresh", cleared ? "cleared" : "seeded",
                         query.reason().str().c_str());
            continue;
        }
        const auto & table = *query.roots().front().methodTable;
        const auto & capture = *table.capturedMap;
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        ctjs::ConstructOp child;
        setter.walk([&](ctjs::ConstructOp made) { child = made; });
        check(child && capture.childMaps == std::vector{child} && capture.allocation != child &&
                  capture.childMapContents && capture.returnedChildMaps.size() == 1 &&
                  capture.returnedChildMaps.front() != child.getResult() &&
                  capture.leafObjects.empty() &&
                  capture.calls.size() == 4u + (cleared ? 1u : 0u) + (guarded ? 1u : 0u),
              "the owner separates the child constructor, universal kind and "
              "returned owner");
        if (guarded) {
            check(query.scalarReads().size() == 1 &&
                      query.scalarReads().front().alternatives.tag() ==
                          mlir::TypeID::get<ctjs::NumberAttr>(),
                  "a conditional returned child acquires Number from its own "
                  "following set");
        }
        for (const auto & edge : table.calls) {
            check(edge.capturedMap && edge.capturedMap->childMaps == capture.childMaps &&
                      edge.capturedMap->childMapContents == capture.childMapContents &&
                      edge.capturedMap->returnedChildMaps == capture.returnedChildMaps &&
                      edge.capturedMap->reads == capture.reads &&
                      edge.capturedMap->calls == capture.calls,
                  "all published calls retain the same complete child construction and effects");
        }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "nested ownership preserves each source allocation, receiver and call argument");
        const unsigned completion = query.steps();
        check(completion < 15000, "nested child owner proof stays within the fixture work bound");
        if (!cleared && completion < 15000) {
            for (unsigned budget = 0; budget < completion; ++budget) {
                OwnedGlobalRoots limited(*module, contract, budget);
                check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                          empty(*module, limited),
                      "every incomplete child owner budget withholds the complete owning family");
            }
            OwnedGlobalRoots exact(*module, contract, completion);
            check(exact.proved() && exact.steps() == completion,
                  "the exact child owner budget reproduces the source ownership graph");
        }
        mlir::Builder attributes(&context);
        module->walk([&](mlir::Operation * operation) {
            operation->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.map_present", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.map_group", attributes.getI64IntegerAttr(0));
        });
        contract = requested(*module);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "nested ownership is independently rederived despite forged native reports");
        ctjs::CallOp outerSet, childSet, childRead;
        for (ctjs::CallOp call : capture.calls) {
            auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (call.getArgs().size() == 2) {
                if (call.getArgs()[1] == child.getResult()) {
                    outerSet = call;
                } else {
                    childSet = call;
                }
            } else if (read && childSet && call.getReceiver() == childSet.getResult()) {
                childRead = call;
            }
        }
        check(outerSet && childSet && childRead,
              "nested fixture retains the original outer and child mutation/read positions");
        if (!outerSet || !childSet || !childRead) { continue; }
        const auto mutate = [&](mlir::Operation * operation, unsigned operand, mlir::Value value) {
            const auto saved = operation->getOperand(operand);
            operation->setOperand(operand, value);
            OwnedGlobalRoots stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      empty(*module, stale),
                  "a changed child source invalidates the original owner fingerprint");
            OwnedGlobalRoots fresh(*module, requested(*module));
            check(!fresh.proved() && !fresh.exhausted() && empty(*module, fresh),
                  "fresh fingerprints and forged Map facts cannot authorize an unsafe child edge");
            operation->setOperand(operand, saved);
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring the original child source restores the independent owner proof");
        };
        mutate(child, 1, setter.getBody().front().getArgument(0));
        mutate(outerSet, 2, child.getResult());
        mutate(outerSet, 3, outerSet.getReceiver());
        mutate(childSet, 3, childSet.getReceiver());
        mutate(childSet, 3, outerSet.getReceiver());
        mutate(llvm::cast<ctjs::ReturnOp>(setter.getBody().front().getTerminator()), 0,
               childSet.getReceiver());
        std::printf("child Map owner %s %s %s: %u steps%s\n", lifted ? "prepared" : "source",
                    guarded ? "conditional" : "fresh", cleared ? "cleared" : "seeded", completion,
                    cleared ? "" : ", all incomplete budgets checked");
    }
    auto branched = replaced(nested, "    %childReader = ctjs.get_property", R"MLIR(
    %second = ctjs.construct %childConstructor(%childConstructor)
    %condition = ctjs.truthy %entryKey
    scf.if %condition {
      %secondSetter = ctjs.get_property %second[%setKey]
      %different = ctjs.constant #ctjs.boolean<true>
      %secondWritten = ctjs.call %secondSetter(%second, %entryKey, %different)
      %clearKey = ctjs.constant #ctjs.string<"clear">
      %clearer = ctjs.get_property %state[%clearKey]
      %cleared = ctjs.call %clearer(%state)
      scf.yield
    } else {
      scf.yield
    }
    %childReader = ctjs.get_property)MLIR");
    branched = replaced(branched, "    ctjs.store_global \"trace\", %answer",
                        "    ctjs.store_global \"trace\", %putResult\n"
                        "    %observed = ctjs.load_global \"trace\"");
    auto branchModule = mlir::parseSourceString<mlir::ModuleOp>(branched, &context);
    check(static_cast<bool>(branchModule),
          "source/prepared independent child branch fixture parses");
    if (branchModule) {
        const auto contract = requested(*branchModule);
        OwnedGlobalRoots query(*branchModule, contract);
        check(
            query.proved() && query.roots().size() == 1 && query.scalarReads().size() == 1 &&
                query.scalarReads().front().alternatives.tag() ==
                    mlir::TypeID::get<ctjs::NumberAttr>(),
            "other child and outer mutations on one arm preserve the first child's Number payload");
        if (query.proved() && !query.roots().empty()) {
            const auto & capture = *query.roots().front().methodTable->capturedMap;
            check(capture.childMaps.size() == 2 && capture.calls.size() == 6 &&
                      capture.childMaps[0] != capture.childMaps[1],
                  "both child origins and inactive-arm calls remain in the complete owner census");
            OwnedGlobalRoots limited(*branchModule, contract, query.steps() - 1);
            check(!limited.proved() && limited.exhausted() && empty(*branchModule, limited),
                  "an incomplete multiple-origin branch proof withholds every owning edge");
        }
    }
    const auto refuse = [&](const std::string & program, const char * message) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "source/prepared child owner refusal parses");
        if (!module) { return; }
        OwnedGlobalRoots query(*module, requested(*module));
        check(!query.proved() && !query.exhausted() && empty(*module, query), message);
    };
    const auto branchOrigins =
        replaced(nested, "    %written = ctjs.call %setter(%state, %entryKey, %value)",
                 R"MLIR(
    %second = ctjs.construct %childConstructor(%childConstructor)
    %condition = ctjs.truthy %entryKey
    scf.if %condition {
      %written = ctjs.call %setter(%state, %entryKey, %value)
      scf.yield
    } else {
      %otherWritten = ctjs.call %setter(%state, %entryKey, %second)
      scf.yield
    }
)MLIR");
    auto repeated = replaced(conditional, "    %childReader = ctjs.get_property", R"MLIR(
    %again = ctjs.call %reader(%state, %entryKey)
    %childReader = ctjs.get_property)MLIR");
    repeated = replaced(repeated, "%childReader = ctjs.get_property %childWritten[%readKey]",
                        "%childReader = ctjs.get_property %again[%readKey]");
    repeated = replaced(repeated, "%loaded = ctjs.call %childReader(%childWritten, %entryKey)",
                        "%loaded = ctjs.call %childReader(%again, %entryKey)");
    const auto replacement = replaced(conditional, "    %childReader = ctjs.get_property", R"MLIR(
    %replacementConstructor = ctjs.load_global "Map"
    %replacement = ctjs.construct %replacementConstructor(%replacementConstructor)
    %replaced = ctjs.call %setter(%state, %entryKey, %replacement)
    %childReader = ctjs.get_property)MLIR");
    for (const auto & program : {branchOrigins, repeated, replacement}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "source/prepared returned-child identity control parses");
        if (!module) { continue; }
        const auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        check(query.proved() && query.roots().size() == 1,
              "returned children retain identity across unequal branches, repeat "
              "gets and replacement");
        if (!query.proved() || query.roots().empty()) {
            std::fprintf(stderr, "returned child owner %s: %s\n", lifted ? "prepared" : "source",
                         query.reason().str().c_str());
            continue;
        }
        const auto & capture = *query.roots().front().methodTable->capturedMap;
        check(capture.childMapContents && !capture.returnedChildMaps.empty() &&
                  hostContractFingerprint(*module) == contract.moduleSha256,
              "every returned-child role is rederived without choosing a "
              "constructor identity");
        OwnedGlobalRoots limited(*module, contract, query.steps() - 1);
        check(!limited.proved() && limited.exhausted() && empty(*module, limited),
              "an incomplete returned-child owner proof withholds every family edge");
    }
    auto unseeded = replaced(conditional,
                             "    %childSetter = ctjs.get_property %saved[%setKey]\n"
                             "    %payload = ctjs.constant #ctjs.number<4607182418800017408>\n"
                             "    %childWritten = ctjs.call %childSetter(%saved, %entryKey, "
                             "%payload)\n",
                             "");
    unseeded = replaced(unseeded, "%childReader = ctjs.get_property %childWritten[%readKey]",
                        "%childReader = ctjs.get_property %saved[%readKey]");
    unseeded = replaced(unseeded, "%loaded = ctjs.call %childReader(%childWritten, %entryKey)",
                        "%loaded = ctjs.call %childReader(%saved, %entryKey)");
    refuse(unseeded, "child kind and outer membership cannot grant a prior child's contents");
    auto priorSeed =
        replaced(unseeded, "      %written = ctjs.call %setter(%state, %entryKey, %value)",
                 R"MLIR(
      %childSetter = ctjs.get_property %value[%setKey]
      %payload = ctjs.constant #ctjs.number<4607182418800017408>
      %childWritten = ctjs.call %childSetter(%value, %entryKey, %payload)
      %written = ctjs.call %setter(%state, %entryKey, %value)
)MLIR");
    refuse(priorSeed, "a constructor site's seeded contents do not identify an earlier child");
    const std::string seed = "    %seeded = ctjs.call %childSetter(%value, %seedKey, %payload)\n";
    auto cross = replaced(source, "    %value = ctjs.create_object", R"MLIR(
    %constructor = ctjs.load_global "Map"
    %value = ctjs.construct %constructor(%constructor)
    %seedKey = ctjs.constant #ctjs.string<"value">
    %childSetter = ctjs.get_property %value[%setKey]
    %payload = ctjs.constant #ctjs.number<4607182418800017408>
)MLIR" + seed);
    cross = replaced(cross,
                     "    %key = ctjs.constant #ctjs.string<\"size\">\n"
                     "    %size = ctjs.get_property %state[%key]\n"
                     "    ctjs.return %size",
                     R"MLIR(
    %entryKey = ctjs.constant #ctjs.string<"x">
    %hasKey = ctjs.constant #ctjs.string<"has">
    %hasMethod = ctjs.get_property %state[%hasKey]
    %found = ctjs.call %hasMethod(%state, %entryKey)
    %condition = ctjs.truthy %found
    %answer = scf.if %condition -> (!ctjs.value) {
      %readKey = ctjs.constant #ctjs.string<"get">
      %reader = ctjs.get_property %state[%readKey]
      %saved = ctjs.call %reader(%state, %entryKey)
      %innerKey = ctjs.constant #ctjs.string<"value">
      %childReader = ctjs.get_property %saved[%readKey]
      %loaded = ctjs.call %childReader(%saved, %innerKey)
      scf.yield %loaded : !ctjs.value
    } else {
      %zero = ctjs.constant #ctjs.number<0>
      scf.yield %zero : !ctjs.value
    }
    ctjs.return %answer
)MLIR");
    cross = replaced(cross, "    ctjs.store_global \"trace\", %answer",
                     "    ctjs.store_global \"trace\", %answer\n"
                     "    %observed = ctjs.load_global \"trace\"");
    auto crossModule = mlir::parseSourceString<mlir::ModuleOp>(cross, &context);
    check(static_cast<bool>(crossModule), "source/prepared cross-invocation child owner parses");
    if (crossModule) {
        auto contract = requested(*crossModule);
        OwnedGlobalRoots query(*crossModule, contract);
        check(query.proved() && query.roots().size() == 1 && query.scalarReads().size() == 1 &&
                  query.scalarReads().front().alternatives.tag() ==
                      mlir::TypeID::get<ctjs::NumberAttr>(),
              "a separate getter rederives Number from every child's preserved literal entry");
        if (!query.proved() || query.roots().empty()) {
            std::fprintf(stderr, "cross child owner %s: %s\n", lifted ? "prepared" : "source",
                         query.reason().str().c_str());
        } else {
            const auto & capture = *query.roots().front().methodTable->capturedMap;
            check(capture.childEntries.size() == 1 && capture.returnedChildMaps.size() == 1 &&
                      capture.childEntries.front().alternatives.tag() ==
                          mlir::TypeID::get<ctjs::NumberAttr>() &&
                      hostContractFingerprint(*crossModule) == contract.moduleSha256,
                  "the owner preserves source publication and keeps entry evidence separate "
                  "from returned identity");
            for (const unsigned budget : {0u, query.steps() / 2, query.steps() - 1}) {
                OwnedGlobalRoots limited(*crossModule, contract, budget);
                check(!limited.proved() && limited.exhausted() && empty(*crossModule, limited) &&
                          limited.steps() <= budget,
                      "incomplete cross-invocation proof withholds every owning edge");
            }
            check(OwnedGlobalRoots(*crossModule, contract, query.steps()).proved(),
                  "the exact cross-invocation budget proves the complete family");
            mlir::Builder attributes(&context);
            crossModule->walk([&](mlir::Operation * operation) {
                operation->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
                operation->setAttr("ctnative.map_present", attributes.getBoolAttr(true));
                operation->setAttr("ctnative.map_read_type", attributes.getStringAttr("number"));
            });
            check(OwnedGlobalRoots(*crossModule, requested(*crossModule)).proved(),
                  "forged reports never replace independent child-entry ownership proof");
        }
    }
    refuse(replaced(cross, seed, ""),
           "a separate getter cannot infer membership from an unseeded publication");
    auto lateSeed = replaced(cross, seed, "");
    lateSeed = replaced(lateSeed, "    %written = ctjs.call %setter(%state, %entryKey, %value)",
                        "    %written = ctjs.call %setter(%state, %entryKey, %value)\n" + seed);
    refuse(lateSeed, "entry initialization must precede publication into the owning outer Map");
    for (const std::string action : {"delete", "clear"}) {
        const auto erasure = "    %eraseKey = ctjs.constant #ctjs.string<\"" + action +
                             "\">\n    %eraser = ctjs.get_property %value[%eraseKey]\n"
                             "    %erased = ctjs.call %eraser(%value" +
                             (action == "delete" ? ", %seedKey)\n" : ")\n");
        refuse(replaced(cross, seed, seed + erasure),
               "a destructive child mutation revokes the complete-family entry invariant");
    }
    refuse(replaced(cross, seed,
                    seed + "    %mixed = ctjs.constant #ctjs.boolean<true>\n"
                           "    %changed = ctjs.call %childSetter(%value, %seedKey, %mixed)\n"),
           "mixed child payload categories revoke the getter's Number authority");
    const auto poison = [&](const std::string & payload) {
        return replaced(conditional, "    %size = ctjs.get_property %state[%key]",
                        R"MLIR(
    %poisonKey = ctjs.constant #ctjs.string<"set">
    %poisonSetter = ctjs.get_property %state[%poisonKey]
    %poisonEntry = ctjs.constant #ctjs.string<"poison">
)MLIR" + payload + R"MLIR(
    %poisoned = ctjs.call %poisonSetter(%state, %poisonEntry, %poisonValue)
    %size = ctjs.get_property %state[%key]
)MLIR");
    };
    refuse(poison("    %poisonValue = ctjs.constant #ctjs.number<0>\n"),
           "a sibling scalar insertion revokes the complete outer child-kind proof");
    refuse(poison("    %poisonValue = ctjs.create_object\n"),
           "a sibling leaf insertion revokes the complete outer child-kind proof");
    auto possibleAlias = replaced(conditional, "    %readKey = ctjs.constant", R"MLIR(
    %otherKey = ctjs.constant #ctjs.string<"other">
    %otherHas = ctjs.call %hasMethod(%state, %otherKey)
    %otherCondition = ctjs.truthy %otherHas
    scf.if %otherCondition {
      scf.yield
    } else {
      %otherConstructor = ctjs.load_global "Map"
      %otherChild = ctjs.construct %otherConstructor(%otherConstructor)
      %otherWritten = ctjs.call %setter(%state, %otherKey, %otherChild)
      scf.yield
    }
    %readKey = ctjs.constant)MLIR");
    possibleAlias = replaced(possibleAlias, "    %childReader = ctjs.get_property", R"MLIR(
    %other = ctjs.call %reader(%state, %otherKey)
    %otherSetter = ctjs.get_property %other[%setKey]
    %different = ctjs.constant #ctjs.boolean<true>
    %otherMutation = ctjs.call %otherSetter(%other, %entryKey, %different)
    %childReader = ctjs.get_property)MLIR");
    refuse(possibleAlias, "unknown returned children can alias and revoke a saved Number payload");
    auto aliasClear =
        replaced(possibleAlias, "    %otherSetter = ctjs.get_property %other[%setKey]",
                 "    %clearKey = ctjs.constant #ctjs.string<\"clear\">\n"
                 "    %otherSetter = ctjs.get_property %other[%clearKey]");
    aliasClear = replaced(aliasClear,
                          "%otherMutation = ctjs.call %otherSetter(%other, %entryKey, %different)",
                          "%otherMutation = ctjs.call %otherSetter(%other)");
    refuse(aliasClear, "clearing a possible returned-child alias revokes saved membership");
    refuse(replaced(nested, "    %written = ctjs.call %setter(%state, %entryKey, %value)\n", ""),
           "an unseeded prior-invocation child has no exact current allocation origin");
    refuse(replaced(nested, "%saved = ctjs.call %reader(%state, %entryKey)",
                    "%different = ctjs.constant #ctjs.number<0>\n"
                    "    %saved = ctjs.call %reader(%state, %different)"),
           "an outer read of another key cannot inherit the stored child's identity");
    refuse(replaced(nested, "%value = ctjs.construct %childConstructor(%childConstructor)",
                    "%value = ctjs.construct %childConstructor(%childConstructor, %entryKey)"),
           "a child constructor iterable is outside the empty Map ownership proof");
}

} // namespace

void checkLeafOwner(mlir::MLIRContext & context, const std::string & source, bool lifted) {
    checkChildMapOwner(context, source, lifted);
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
    const auto string =
        replaced(numeric, "#ctjs.number<4607182418800017408>", "#ctjs.string<\"owned\">");
    for (const auto & [program, hasField] :
         {std::pair{source, false}, {numeric, true}, {string, true}}) {
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

} // namespace ctcompile::test::owned_global_shared_map
