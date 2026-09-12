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
    std::vector<std::string> nullableLegacy{priorSeed};
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
    nullableLegacy.push_back(lateSeed);
    for (const std::string action : {"delete", "clear"}) {
        const auto erasure = "    %eraseKey = ctjs.constant #ctjs.string<\"" + action +
                             "\">\n    %eraser = ctjs.get_property %value[%eraseKey]\n"
                             "    %erased = ctjs.call %eraser(%value" +
                             (action == "delete" ? ", %seedKey)\n" : ")\n");
        nullableLegacy.push_back(replaced(cross, seed, seed + erasure));
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
    nullableLegacy.push_back(aliasClear);
    refuse(replaced(nested, "    %written = ctjs.call %setter(%state, %entryKey, %value)\n", ""),
           "an unseeded prior-invocation child has no exact current allocation origin");
    refuse(replaced(nested, "%saved = ctjs.call %reader(%state, %entryKey)",
                    "%different = ctjs.constant #ctjs.number<0>\n"
                    "    %saved = ctjs.call %reader(%state, %different)"),
           "an outer read of another key cannot inherit the stored child's identity");
    refuse(replaced(nested, "%value = ctjs.construct %childConstructor(%childConstructor)",
                    "%value = ctjs.construct %childConstructor(%childConstructor, %entryKey)"),
           "a child constructor iterable is outside the empty Map ownership proof");
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    const auto withoutScalarRead = [&](const std::string & program) {
        return replaced(program, "    %observed = ctjs.load_global \"trace\"", "");
    };
    auto prior = replaced(conditional, "    %childSetter = ctjs.get_property", R"MLIR(
    %priorReader = ctjs.get_property %saved[%readKey]
    %prior = ctjs.call %priorReader(%saved, %entryKey)
    %childSetter = ctjs.get_property)MLIR");
    const std::string selection = R"MLIR(
    %undefined = ctjs.constant #ctjs.undefined
    %isAbsent = ctjs.compare strict_eq %prior, %undefined
    %absentFlag = ctjs.truthy %isAbsent
    %selected = scf.if %absentFlag -> (!ctjs.value) {
      scf.yield %loaded : !ctjs.value
    } else {
      scf.yield %prior : !ctjs.value
    }
    ctjs.return %selected)MLIR";
    prior = replaced(prior, "    ctjs.return %loaded", selection);
    // Both the child key and stored category come from complete actual censuses.
    prior =
        replaced(prior, "%entryKey: !ctjs.value)", "%entryKey: !ctjs.value, %input: !ctjs.value)");
    prior = replaced(prior, "    %actual =",
                     "    %input = ctjs.constant #ctjs.number<4607182418800017408>\n"
                     "    %actual =");
    prior = replaced(prior, "    %payload = ctjs.constant #ctjs.number<4607182418800017408>\n", "");
    prior = replaced(prior, "%childSetter(%saved, %entryKey, %payload)",
                     "%childSetter(%saved, %entryKey, %input)");
    for (const auto & [name, actual] :
         {std::pair{"putterEnv", "actual"}, {"repeatEnv", "actual"}, {"laterEnv", "future"}}) {
        const std::string prefix = lifted ? std::string("%") + name + ", " : "%owned, ";
        prior = replaced(prior, prefix + "%" + actual + ")", prefix + "%" + actual + ", %input)");
    }
    unsigned nullableRows = 0;
    const auto nullable = [&](const std::string & program, bool expected, const char * label,
                              bool reverse = false, bool exhaustive = false, int scalarRead = -1) {
        ++nullableRows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "source/prepared nullable child fixture parses");
        if (!module) { return; }
        auto getter = module->lookupSymbol<ctjs::FuncOp>("get$3");
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        if (reverse) {
            getter->moveAfter(setter);
            auto factory = module->lookupSymbol<ctjs::FuncOp>("make$2");
            ctjs::CreateClosureOp getClosure;
            ctjs::SetPropertyOp getWrite, putWrite;
            factory.walk([&](ctjs::SetPropertyOp write) {
                auto closure = write.getValue().getDefiningOp<ctjs::CreateClosureOp>();
                if (!closure) { return; }
                if (closure.getFunction() == 3) {
                    getClosure = closure;
                    getWrite = write;
                } else if (closure.getFunction() == 4) {
                    putWrite = write;
                }
            });
            check(getClosure && getWrite && putWrite, "both sibling publication orders exist");
            if (!getClosure || !getWrite || !putWrite) { return; }
            getClosure->moveAfter(putWrite);
            getWrite->moveAfter(getClosure);
        }
        auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        check(query.proved() == expected && !query.exhausted() &&
                  (expected || empty(*module, query)),
              label);
        if (query.proved() != expected || query.exhausted()) {
            std::fprintf(stderr, "nullable child owner %s row %u: %s\n",
                         lifted ? "prepared" : "source", nullableRows,
                         query.reason().str().c_str());
        }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "nullable child proof preserves every source operation");
        if (!expected || !query.proved() || query.roots().empty()) { return; }
        const auto & table = *query.roots().front().methodTable;
        const auto & capture = *table.capturedMap;
        check(capture.childMapContents && !capture.childMaps.empty() &&
                  !capture.returnedChildMaps.empty() && capture.childEntries.empty() &&
                  capture.childScalarContents ==
                      Alternatives::forTag(mlir::TypeID::get<ctjs::NumberAttr>()),
              "homogeneous nullable contents supply no child membership or allocation identity");
        for (const auto & edge : table.calls) {
            check(edge.capturedMap &&
                      edge.capturedMap->childScalarContents == capture.childScalarContents &&
                      edge.capturedMap->childEntries.empty() &&
                      edge.capturedMap->calls == capture.calls,
                  "every call retains the same complete scalar child mutation census");
        }
        if (scalarRead >= 0) {
            check(query.scalarReads().size() == static_cast<unsigned>(scalarRead),
                  "singleton absence filtering independently controls the saved scalar read");
        }
        if (!exhaustive) { return; }
        const unsigned completion = query.steps();
        check(completion < 20000, "nullable child owner proof remains bounded");
        if (completion >= 20000) { return; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            OwnedGlobalRoots limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      empty(*module, limited),
                  "every incomplete nullable child proof withholds all owning records");
        }
        check(OwnedGlobalRoots(*module, contract, completion).proved(),
              "the exact nullable child budget proves the complete family");
        module->walk([&](mlir::Operation * operation) {
            operation->setAttr("ctnative.host_owner_proved", mlir::UnitAttr::get(&context));
            operation->setAttr("ctnative.map_present", mlir::UnitAttr::get(&context));
            operation->setAttr("ctnative.map_read_type", mlir::StringAttr::get(&context, "number"));
        });
        contract = requested(*module);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "forged reports cannot replace the independent nullable child proof");
        ctjs::CallOp childSet;
        setter.walk([&](ctjs::CallOp call) {
            if (call.getArgs().size() == 2 && llvm::isa<mlir::BlockArgument>(call.getArgs()[1])) {
                childSet = call;
            }
        });
        check(static_cast<bool>(childSet), "nullable fixture retains its formal child payload");
        if (!childSet) { return; }
        const auto payload = childSet.getArgs()[1];
        for (mlir::Value changed : {mlir::Value(childSet.getReceiver()),
                                    mlir::Value(setter.getBody().front().getArgument(0))}) {
            childSet->setOperand(3, changed);
            OwnedGlobalRoots stale(*module, contract);
            OwnedGlobalRoots fresh(*module, requested(*module));
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      empty(*module, stale) && !fresh.proved() && !fresh.exhausted() &&
                      empty(*module, fresh),
                  "a cyclic or opaque live child write defeats stale and forged scalar evidence");
            childSet->setOperand(3, payload);
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring the live scalar write restores its complete owner");
        }
        std::printf("nullable child owner %s: all %u incomplete budgets and live writes checked\n",
                    lifted ? "prepared" : "source", completion);
    };
    nullable(prior, true, "a prior nullable child read narrows to Number on strict absence", false,
             true, 1);
    nullable(replaced(prior, "strict_eq %prior, %undefined", "strict_eq %undefined, %prior"), true,
             "strict absence narrows the value on either side of equality", false, false, 1);
    auto inverted = replaced(prior, "%absentFlag = ctjs.truthy %isAbsent",
                             "%present = ctjs.unary not %isAbsent\n"
                             "    %absentFlag = ctjs.truthy %present");
    const std::string arms = "      scf.yield %loaded : !ctjs.value\n"
                             "    } else {\n      scf.yield %prior : !ctjs.value";
    const std::string swapped = "      scf.yield %prior : !ctjs.value\n"
                                "    } else {\n      scf.yield %loaded : !ctjs.value";
    nullable(replaced(inverted, arms, swapped), true,
             "an inverted strict absence guard preserves the original scalar result", false, false,
             1);
    nullable(replaced(prior, arms, swapped), true,
             "an absent result remains owned without a definite scalar read", false, false, 0);
    nullable(replaced(prior, "%undefined = ctjs.constant #ctjs.undefined",
                      "%undefined = ctjs.constant #ctjs.null"),
             true, "Null equality cannot remove Undefined from a nullable Number read", false,
             false, 0);
    for (const auto & program : nullableLegacy) {
        nullable(
            program, true,
            "the original seeded, late and deleted child sources retain only nullable contents",
            false, false, 0);
    }
    nullable(withoutScalarRead(unseeded), false,
             "no scalar writes means no prior child scalar category authority");
    const auto removal = replaced(prior, "    %childSetter = ctjs.get_property", R"MLIR(
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %removed = ctjs.call %deleter(%state, %entryKey)
    %childSetter = ctjs.get_property)MLIR");
    nullable(removal, true,
             "a saved child survives outer removal and fresh recreation in later calls");
    nullable(replaced(prior, "%found = ctjs.call %hasMethod(%state, %entryKey)",
                      "%wrong = ctjs.constant #ctjs.string<\"wrong\">\n"
                      "    %found = ctjs.call %hasMethod(%state, %wrong)"),
             false, "scalar contents do not prove that an outer lookup contains a child");
    const std::string sibling = R"MLIR(
    %outerKey = ctjs.constant #ctjs.string<"x">
    %hasKey = ctjs.constant #ctjs.string<"has">
    %hasMethod = ctjs.get_property %state[%hasKey]
    %found = ctjs.call %hasMethod(%state, %outerKey)
    %flag = ctjs.truthy %found
    scf.if %flag {
      %readKey = ctjs.constant #ctjs.string<"get">
      %reader = ctjs.get_property %state[%readKey]
      %child = ctjs.call %reader(%state, %outerKey)
      %setKey = ctjs.constant #ctjs.string<"set">
      %setter = ctjs.get_property %child[%setKey]
      %bad = BAD_VALUE
      %changed = ctjs.call %setter(%child, %outerKey, %bad)
      scf.yield
    }
)MLIR";
    for (const char * payload : {"ctjs.constant #ctjs.boolean<true>", "ctjs.create_object",
                                 "ctjs.load_global \"external\""}) {
        const auto poisoned = replaced(prior, "    %size = ctjs.get_property %state[%key]",
                                       replaced(sibling, "BAD_VALUE", payload) +
                                           "    %size = ctjs.get_property %state[%key]");
        for (bool reverse : {false, true}) {
            nullable(poisoned, false,
                     "an incompatible or opaque sibling write defeats nullable authority in "
                     "either complete family order",
                     reverse);
        }
    }
    nullable(prior, true, "the homogeneous proof is independent of sibling order", true);
    auto impossible =
        replaced(withoutScalarRead(prior), "%undefined = ctjs.constant #ctjs.undefined",
                 "%undefined = ctjs.constant #ctjs.null");
    impossible = replaced(impossible, "      scf.yield %loaded : !ctjs.value",
                          "      %opaque = ctjs.load_global \"external\"\n"
                          "      scf.yield %loaded : !ctjs.value");
    nullable(impossible, false, "an impossible filtered arm still checks every structural effect");
    check(nullableRows == 21, "all nullable child category, absence and sibling controls ran");
}

void checkCallerPayloadOwner(mlir::MLIRContext & context, const std::string & source,
                             bool prepared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    auto fixture =
        replaced(source, "%entryKey: !ctjs.value)", "%entryKey: !ctjs.value, %value: !ctjs.value)");
    fixture = replaced(fixture, "    %value = ctjs.create_object\n", "");
    const std::string initialization =
        "    %payload = ctjs.create_object\n"
        "    %fieldKey = ctjs.constant #ctjs.string<\"value\">\n"
        "    %fieldValue = ctjs.constant #ctjs.number<4634204016564240384>\n"
        "    ctjs.set_property %payload[%fieldKey], %fieldValue\n";
    fixture = replaced(fixture, "    %putResult =", initialization + "    %putResult =");
    for (unsigned call = 0; call < 2; ++call) {
        fixture = replaced(fixture, ", %actual)", ", %actual, %payload)");
    }
    fixture = replaced(fixture, ", %future)", ", %future, %payload)");
    const std::string observation = "    ctjs.store_global \"trace\", %answer\n";
    const std::string inspected = "    %loaded = ctjs.get_property %payload[%fieldKey]\n"
                                  "    %same = ctjs.compare strict_eq %payload, %payload\n";
    const auto observed = replaced(fixture, observation, inspected + observation);
    unsigned rows = 0;
    const auto variant = [&](const std::string & text, bool expected, const char * message,
                             unsigned objects = 3) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "source/prepared caller payload fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
        HostContractAnalysis host(*module, contract);
        OwnedGlobalRoots owner(*module, contract);
        check(host.proved() == expected && owner.proved() == expected && !host.exhausted() &&
                  !owner.exhausted(),
              message);
        if (host.proved() != expected || owner.proved() != expected) {
            std::fprintf(stderr, "caller payload %s row %u: host=%s owner=%s\n",
                         prepared ? "prepared" : "source", rows, host.reason().str().c_str(),
                         owner.reason().str().c_str());
        }
        if (!expected) {
            check(host.callables().empty() && empty(*module, owner),
                  "an unsafe caller payload publishes no partial family");
        } else if (host.proved() && owner.proved()) {
            const auto & table = *owner.roots().front().methodTable;
            const auto & capture = *table.capturedMap;
            auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
            const auto parameter = setter.getBody().front().getArgument(prepared ? 5 : 4);
            unsigned actualObjects = 0, calls = 0;
            bool complete = table.methods.size() == 2 && table.calls.size() == 4 &&
                            capture.childScalarContents == Alternatives{} &&
                            capture.leafObjects.empty();
            for (const auto & edge : table.calls) {
                if (edge.function != setter) { continue; }
                ++calls;
                complete &= edge.arguments.size() == 2 && edge.capturedMap.has_value();
                if (edge.arguments.size() != 2 || !edge.capturedMap) { continue; }
                const auto & argument = edge.arguments.back();
                auto made = argument.object;
                actualObjects += static_cast<unsigned>(static_cast<bool>(made));
                complete &= argument.parameter == parameter &&
                            argument.actual == edge.call->getOperand(prepared ? 5 : 3) &&
                            argument.alternatives == Alternatives{} &&
                            (!made || made.getResult() == argument.actual);
                const auto family = llvm::find_if(
                    capture.parameters, [&](const auto & item) { return item.function == setter; });
                complete &= family != capture.parameters.end() &&
                            family->objectKeys == std::vector{parameter} &&
                            family->alternatives.size() == 2 &&
                            family->alternatives.back() == Alternatives{};
            }
            check(complete && calls == 3 && actualObjects == objects,
                  "caller objects keep exact actual/formal identities without scalar or local "
                  "leaf authority across the complete invocation family");
        }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "caller ownership preserves every field write and invocation");
    };
    variant(fixture, true, "a caller-owned scalar field can be retained as a Map payload");
    variant(observed, true, "a checked caller alias permits initialized field and identity reads");
    variant(replaced(observed, observation,
                     "    ctjs.set_property %payload[%fieldKey], %u\n" + observation),
            true, "a later scalar write participates in the complete caller field census");
    const auto distinct = replaced(fixture, "    %laterPutter =",
                                   "    %different = ctjs.create_object\n"
                                   "    ctjs.set_property %different[%fieldKey], %fieldValue\n"
                                   "    %laterPutter =");
    variant(replaced(distinct, ", %future, %payload)", ", %future, %different)"), true,
            "distinct caller payloads retain separate allocations in the same family");
    variant(replaced(observed, ", %actual, %payload)", ", %actual, %fieldValue)"), true,
            "a Number call before object calls grants no primitive formal authority", 2);
    variant(replaced(observed, ", %future, %payload)", ", %future, %fieldValue)"), true,
            "a Number call after object calls grants no primitive formal authority", 2);
    for (const char * value : {"%payload", "%owned"}) {
        variant(replaced(fixture, "ctjs.set_property %payload[%fieldKey], %fieldValue",
                         std::string("ctjs.set_property %payload[%fieldKey], ") + value),
                false, "a caller field cannot retain itself or the method table");
    }
    variant(replaced(fixture, "#ctjs.string<\"value\">", "#ctjs.string<\"__proto__\">"), false,
            "a prototype field does not become an own scalar field");
    variant(replaced(observed, "%payload[%fieldKey]\n", "%payload[%putKey]\n"), false,
            "an uninitialized field read has no caller own-data authority");
    variant(replaced(observed, "ctjs.set_property %payload[%fieldKey]",
                     "ctjs.set_property %payload[%this]"),
            false, "an unknown field key cannot claim ordinary scalar initialization");
    variant(replaced(fixture, observation, "    %escaped = ctjs.call %payload(%u)\n" + observation),
            false, "a checked payload cannot be called as an opaque function");
    variant(
        replaced(fixture, "    ctjs.return %size\n  }\n}\n", "    ctjs.return %value\n  }\n}\n"),
        false, "a payload formal still cannot escape through an owning result");
    auto module = mlir::parseSourceString<mlir::ModuleOp>(observed, &context);
    check(static_cast<bool>(module), "caller payload mutation fixture parses");
    if (!module) { return; }
    OwnedGlobalRoots owner(*module, requested(*module));
    if (!owner.proved()) { return; }
    ctjs::SetPropertyOp field;
    auto entry = module->lookupSymbol<ctjs::FuncOp>("script$0");
    entry.walk([&](ctjs::SetPropertyOp write) {
        if (write.getObject().getDefiningOp<ctjs::CreateObjectOp>() &&
            write.getValue().getDefiningOp<ctjs::ConstantOp>()) {
            field = write;
        }
    });
    check(static_cast<bool>(field), "the caller proof retains its actual source scalar write");
    if (!field) { return; }
    ctjs::GetPropertyOp read;
    ctjs::CompareOp compare;
    entry.walk([&](ctjs::GetPropertyOp operation) {
        if (operation.getObject() == field.getObject()) { read = operation; }
    });
    entry.walk([&](ctjs::CompareOp operation) { compare = operation; });
    check(read && compare, "the caller keeps its field read and strict identity operation");
    if (!read || !compare) { return; }
    mlir::OpBuilder attributes(&context);
    attributes.setInsertionPoint(entry.getBody().front().getTerminator());
    auto lateKey = ctjs::ConstantOp::create(
        attributes, field.getLoc(), field.getKey().getDefiningOp<ctjs::ConstantOp>().getValue());
    field->setAttr("ctnative.object_schema", attributes.getStringAttr("scalar"));
    (*module)->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
    const auto contract = requested(*module);
    const auto mutation = [&](mlir::Operation * operation, unsigned operand, mlir::Value value) {
        const auto saved = operation->getOperand(operand);
        operation->setOperand(operand, value);
        OwnedGlobalRoots stale(*module, contract);
        HostContractAnalysis freshHost(*module, requested(*module));
        OwnedGlobalRoots fresh(*module, requested(*module));
        check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*module, stale) &&
                  !freshHost.proved() && freshHost.callables().empty() && !fresh.proved() &&
                  empty(*module, fresh),
              "stale and forged reports cannot authorize a caller cycle or out-of-scope operand");
        operation->setOperand(operand, saved);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring the caller operand restores independent ownership");
    };
    mutation(field, 2, field.getObject());
    mutation(field, 1, lateKey.getResult());
    mutation(read, 1, lateKey.getResult());
    mutation(compare, 1, lateKey.getResult());
    OwnedGlobalRoots restored(*module, contract);
    check(restored.proved(), "restoring the caller field restores independent ownership");
    const unsigned completion = restored.steps();
    check(completion > 2 && completion < 20000, "caller ownership stays within its work bound");
    if (completion <= 2 || completion >= 20000) { return; }
    for (unsigned budget : {0u, 1u, 2u, completion / 2, completion - 1}) {
        OwnedGlobalRoots limited(*module, contract, budget);
        check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                  empty(*module, limited),
              "an incomplete caller use census withholds the entire owning family");
    }
    OwnedGlobalRoots exact(*module, contract, completion);
    check(exact.proved() && exact.steps() == completion,
          "the exact caller proof budget reproduces the complete family");
    std::printf("caller payload %s: %u rows, 5 incomplete budgets, %u steps\n",
                prepared ? "prepared" : "source", rows, completion);
}

} // namespace

void checkLeafOwner(mlir::MLIRContext & context, const std::string & source, bool lifted) {
    checkCallerPayloadOwner(context, source, lifted);
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
            capture.childScalarContents == Alternatives{} && capture.leafObjects.size() == 1 &&
            capture.leafWrites.size() == (hasField ? 1u : 0u) && capture.leafReads.empty();
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
