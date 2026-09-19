#include "Helpers.hpp"

namespace ctcompile::test::owned_global_shared_map {

void checkExactScalarReturns(mlir::MLIRContext & context, const std::string & distinct,
                             bool prepared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    const auto number = mlir::TypeID::get<ctjs::NumberAttr>();
    const auto null = mlir::TypeID::get<ctjs::NullAttr>();
    auto scalar =
        replaced(distinct, "    %uncheckedField = ctjs.get_property %answer[%fieldKey]\n", "");
    const std::string firstRead = prepared ? "%getterEnv, %actual)" : "%getter(%owned, %actual)";
    const std::string numberRead = prepared ? "%getterEnv, %future)" : "%getter(%owned, %future)";
    scalar = replaced(scalar, firstRead, numberRead);
    scalar = replaced(scalar, "ctjs.store_global \"trace\", %entrySame",
                      "ctjs.store_global \"trace\", %answer");
    scalar = replaced(scalar, "      scf.yield %saved : !ctjs.value", R"MLIR(
      %payloadFlag = ctjs.truthy %saved
      %selected = scf.if %payloadFlag -> (!ctjs.value) {
        scf.yield %saved : !ctjs.value
      } else {
        %fallback = ctjs.constant #ctjs.null
        scf.yield %fallback : !ctjs.value
      }
      scf.yield %selected : !ctjs.value
)MLIR");
    scalar = replaced(scalar, "%missing = ctjs.constant #ctjs.boolean<false>",
                      "%missing = ctjs.constant #ctjs.null");
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & text, std::optional<mlir::TypeID> first,
                             std::optional<mlir::TypeID> second, const char * label,
                             bool live = false, bool publish = true) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "exact scalar source/prepared fixture parses");
        if (!module) { return; }
        module->walk([&](mlir::Operation * operation) {
            operation->setAttr("ctnative.host_owner_proved", mlir::UnitAttr::get(&context));
            operation->setAttr("ctnative.returned_scalar",
                               mlir::StringAttr::get(&context, "number"));
            operation->setAttr("ctnative.map_present", mlir::UnitAttr::get(&context));
        });
        const auto contract = requested(*module);
        HostContractAnalysis host(*module, contract);
        OwnedGlobalRoots owner(*module, contract);
        check(host.proved() && owner.proved() && !host.exhausted() && !owner.exhausted(), label);
        if (!host.proved() || !owner.proved() || owner.roots().empty()) {
            std::fprintf(stderr, "exact scalar %s row %u: host=%s owner=%s\n",
                         prepared ? "prepared" : "source", rows, host.reason().str().c_str(),
                         owner.reason().str().c_str());
            return;
        }
        const auto & table = *owner.roots().front().methodTable;
        const auto & capture = *table.capturedMap;
        const auto getter = module->lookupSymbol<ctjs::FuncOp>("get$3");
        std::vector<mlir::Operation *> getters, setters;
        for (const auto & edge : table.calls) {
            (edge.function == getter ? getters : setters).push_back(edge.call);
            check(edge.capturedMap && edge.capturedMap->returnedScalars == capture.returnedScalars,
                  "every callable edge retains the same completed exact scalar evidence");
        }
        check(getters.size() == 2 && setters.size() == 3 && capture.leafReads.empty() &&
                  capture.childLeafContents && capture.childScalarContents == Alternatives{},
              "only direct scalar stores demand exact facts for the mixed getter family");
        if (getters.size() != 2 || setters.size() != 3) { return; }
        const auto firstValue = getters.front()->getResult(0);
        const auto secondValue = getters.back()->getResult(0);
        auto payload =
            capture.leafWrites.front()->getOperand(0).getDefiningOp<ctjs::CreateObjectOp>();
        const auto matches = [&](const OwnedGlobalRoots & query) {
            return query.returnedScalar(firstValue).tag() == first &&
                   query.returnedScalar(secondValue).tag() == second &&
                   (first || query.returnedScalar(firstValue) == Alternatives{}) &&
                   (second || query.returnedScalar(secondValue) == Alternatives{}) &&
                   query.returnedLeaf(firstValue) ==
                       (publish && !first ? payload : ctjs::CreateObjectOp{}) &&
                   query.returnedLeaf(secondValue) ==
                       (publish && !second ? payload : ctjs::CreateObjectOp{});
        };
        check(matches(owner), label);
        check(payload && owner.returnedScalar(payload.getResult()) == Alternatives{} &&
                  owner.returnedScalar(capture.leafWrites.front()->getOperand(2)) ==
                      Alternatives{} &&
                  owner.returnedScalar(getters.front()->getOperand(prepared ? 4u : 2u)) ==
                      Alternatives{},
              "literal and allocation values cannot borrow an exact family-call result");
        if (!publish) {
            check(capture.returnedScalars.empty() && capture.returnedLeaves.empty(),
                  "one unproved invocation discards all earlier optional result evidence");
        }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "exact scalar ownership preserves the complete source fingerprint");
        if (!live) { return; }
        const auto noResults = [&](const OwnedGlobalRoots & query) {
            return empty(*module, query) && query.returnedScalar(firstValue) == Alternatives{} &&
                   query.returnedScalar(secondValue) == Alternatives{} &&
                   !query.returnedLeaf(firstValue) && !query.returnedLeaf(secondValue);
        };
        const unsigned completion = owner.steps();
        check(completion > 2 && completion < 100000, "exact scalar proof has a bounded census");
        if (completion <= 2 || completion >= 100000) { return; }
        for (unsigned budget : {0u, 1u, completion / 2, completion - 1}) {
            OwnedGlobalRoots limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      noResults(limited),
                  "incomplete owner budgets expose no exact scalar or leaf result");
        }
        OwnedGlobalRoots exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion && matches(exact),
              "the exact budget publishes both independently checked invocation results");
        const unsigned keyOperand = prepared ? 4u : 2u;
        const auto key = getters.front()->getOperand(keyOperand);
        getters.front()->setOperand(keyOperand, getters.back()->getOperand(keyOperand));
        OwnedGlobalRoots stale(*module, contract);
        OwnedGlobalRoots changed(*module, requested(*module));
        check(!stale.proved() && stale.reason().contains("fingerprint") && noResults(stale),
              "changing the exact call invalidates all old Number-result evidence");
        check(changed.proved() && !changed.exhausted() &&
                  changed.returnedScalar(firstValue) == Alternatives{} &&
                  changed.returnedLeaf(firstValue) == payload,
              "a fresh object-returning invocation ignores forged Number reports");
        getters.front()->setOperand(keyOperand, key);
        const unsigned payloadOperand = prepared ? 5u : 3u;
        const auto input = setters.back()->getOperand(payloadOperand);
        setters.back()->setOperand(payloadOperand, getters.front()->getOperand(prepared ? 0u : 1u));
        OwnedGlobalRoots failed(*module, requested(*module));
        check(!failed.proved() && !failed.exhausted() && noResults(failed),
              "an unsupported live payload invalidates every exact scalar publication");
        setters.back()->setOperand(payloadOperand, input);
        OwnedGlobalRoots restored(*module, contract);
        check(restored.proved() && matches(restored),
              "restoring the source rederives Number and object results separately");
        std::printf("exact scalar %s: 2 live mutations, 4 incomplete budgets, %u steps\n",
                    prepared ? "prepared" : "source", completion);
    };
    variant(scalar, number, {}, "one getter invocation is Number while another owns a caller leaf",
            true);
    variant(replaced(scalar, "    ctjs.store_global \"trace\", %answer\n",
                     "    ctjs.store_global \"trace\", %answer\n"
                     "    ctjs.store_global \"trace\", %againAnswer\n"),
            number, {}, "a later object store cannot inherit the first store's Number evidence");
    for (const char * literal : {"#ctjs.number<0>", "#ctjs.number<9223372036854775808>",
                                 "#ctjs.number<9221120237041090560>", "#ctjs.boolean<false>",
                                 "#ctjs.undefined", "#ctjs.null"}) {
        variant(replaced(scalar, "%fieldValue = ctjs.constant #ctjs.number<4634204016564240384>",
                         "%fieldValue = ctjs.constant " + std::string(literal)),
                null, {}, "falsy stored values select Null instead of exact Number evidence");
    }
    variant(replaced(scalar, numberRead,
                     prepared ? "%getterEnv, %fieldKey)" : "%getter(%owned, %fieldKey)"),
            null, {}, "a missing key returns Null without borrowing another call's Number");
    variant(replaced(scalar, ", %future, %fieldValue)", ", %actual, %fieldValue)"), null, number,
            "an aliased later write changes only the actual key and its exact invocation");
    const std::string seeded =
        "    %seeded = ctjs.call %childSetter(%value, %payloadKey, %input)\n";
    const std::string erase = R"MLIR(
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %eraser = ctjs.get_property %value[%deleteKey]
    %wrongKey = ctjs.constant #ctjs.string<"wrong">
    %erased = ctjs.call %eraser(%value, %payloadKey)
)MLIR";
    const auto deleted = replaced(scalar, seeded, seeded + erase);
    variant(deleted, null, null, "deleted payloads return Null for each independent invocation");
    variant(replaced(deleted, "%eraser(%value, %payloadKey)", "%eraser(%value, %wrongKey)"), number,
            {}, "wrong-key deletion preserves exact Number and object returns");
    variant(
        replaced(deleted, erase,
                 erase + "    %restored = ctjs.call %childSetter(%value, %payloadKey, %input)\n"),
        number, {}, "reinsertion rederives the actual payload after deletion");
    auto unknown = replaced(scalar, "    %laterResult =",
                            "    %opaqueNumber = ctjs.binary add %fieldValue, %fieldValue\n"
                            "    %laterResult =");
    unknown = replaced(unknown, ", %future, %fieldValue)", ", %future, %opaqueNumber)");
    variant(unknown, {}, {},
            "an unproved truthiness branch withholds the whole optional invocation scan", false,
            false);
    check(rows == 14, "all exact scalar, falsy, alias, deletion and transactional controls ran");
}

void checkMixedChildReadbacks(mlir::MLIRContext & context, const std::string & source,
                              bool prepared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    auto fixture =
        replaced(source, "%entryKey: !ctjs.value)", "%entryKey: !ctjs.value, %input: !ctjs.value)");
    fixture = replaced(fixture, "    %putResult =", R"MLIR(
    %payload = ctjs.create_object
    %fieldKey = ctjs.constant #ctjs.string<"value">
    %fieldValue = ctjs.constant #ctjs.number<4634204016564240384>
    ctjs.set_property %payload[%fieldKey], %fieldValue
    %putResult =)MLIR");
    for (unsigned call = 0; call < 2; ++call) {
        fixture = replaced(fixture, ", %actual)", ", %actual, %payload)");
    }
    fixture = replaced(fixture, ", %future)", ", %future, %fieldValue)");
    fixture = replaced(fixture, "    %value = ctjs.create_object", R"MLIR(
    %childConstructor = ctjs.load_global "Map"
    %value = ctjs.construct %childConstructor(%childConstructor)
    %payloadKey = ctjs.constant #ctjs.string<"payload">
    %childSetter = ctjs.get_property %value[%setKey]
    %seeded = ctjs.call %childSetter(%value, %payloadKey, %input)
)MLIR");
    fixture = replaced(fixture,
                       "    %key = ctjs.constant #ctjs.string<\"size\">\n"
                       "    %size = ctjs.get_property %state[%key]\n"
                       "    ctjs.return %size",
                       R"MLIR(
    %outerKey = ctjs.constant #ctjs.string<"x">
    %hasKey = ctjs.constant #ctjs.string<"has">
    %hasMethod = ctjs.get_property %state[%hasKey]
    %found = ctjs.call %hasMethod(%state, %outerKey)
    %condition = ctjs.truthy %found
    %answer = scf.if %condition -> (!ctjs.value) {
      %readKey = ctjs.constant #ctjs.string<"get">
      %reader = ctjs.get_property %state[%readKey]
      %child = ctjs.call %reader(%state, %outerKey)
      %payloadKey = ctjs.constant #ctjs.string<"payload">
      %childReader = ctjs.get_property %child[%readKey]
      %saved = ctjs.call %childReader(%child, %payloadKey)
      %expected = ctjs.constant #ctjs.number<4634204016564240384>
      %matches = ctjs.compare strict_eq %saved, %expected
      scf.yield %matches : !ctjs.value
    } else {
      %missing = ctjs.constant #ctjs.boolean<false>
      scf.yield %missing : !ctjs.value
    }
    ctjs.return %answer
)MLIR");
    const auto numberFirst =
        replaced(replaced(fixture, ", %actual, %payload)", ", %actual, %fieldValue)"),
                 ", %future, %fieldValue)", ", %future, %payload)");
    const std::string comparison = "      %matches = ctjs.compare strict_eq %saved, %expected\n";
    const std::string fieldRead = "      %field = ctjs.constant #ctjs.string<\"value\">\n"
                                  "      %loaded = ctjs.get_property %saved[%field]\n"
                                  "      %matches = ctjs.compare strict_eq %loaded, %expected\n";
    unsigned rows = 0;
    const auto variant = [&](const std::string & text, bool expected, const char * label,
                             unsigned expectedCalls = 4, unsigned expectedLeaves = 0) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "source/prepared mixed child readback fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
        HostContractAnalysis host(*module, contract);
        OwnedGlobalRoots owner(*module, contract);
        check(host.proved() == expected && owner.proved() == expected && !host.exhausted() &&
                  !owner.exhausted(),
              label);
        if (host.proved() != expected || owner.proved() != expected) {
            std::fprintf(stderr, "mixed child %s row %u: host=%s owner=%s\n",
                         prepared ? "prepared" : "source", rows, host.reason().str().c_str(),
                         owner.reason().str().c_str());
        }
        if (!expected) {
            check(host.callables().empty() && empty(*module, owner),
                  "an unsafe child readback publishes no partial family");
        } else if (host.proved() && owner.proved() && !owner.roots().empty()) {
            const auto & table = *owner.roots().front().methodTable;
            const auto & capture = *table.capturedMap;
            auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
            auto parameter = setter.getBody().front().getArgument(prepared ? 5 : 4);
            const auto family = llvm::find_if(
                capture.parameters, [&](const auto & item) { return item.function == setter; });
            bool complete = owner.roots().size() == 1 && table.methods.size() == 2 &&
                            table.calls.size() == expectedCalls && capture.childMapContents &&
                            capture.childLeafContents &&
                            capture.childScalarContents == Alternatives{} &&
                            capture.childEntries.empty() && capture.childMaps.size() == 1 &&
                            capture.leafObjects.empty() && capture.leafWrites.size() == 1 &&
                            capture.returnedLeaves.size() == expectedLeaves &&
                            family != capture.parameters.end();
            if (family != capture.parameters.end()) {
                complete &= family->objectKeys == std::vector{parameter} &&
                            family->alternatives.size() == 2 &&
                            family->alternatives.back() == Alternatives{};
            }
            unsigned setters = 0, objects = 0;
            for (const auto & edge : table.calls) {
                complete &= edge.capturedMap.has_value();
                if (!edge.capturedMap) { continue; }
                complete &= edge.capturedMap->childLeafContents &&
                            edge.capturedMap->childScalarContents == Alternatives{} &&
                            edge.capturedMap->childEntries.empty() &&
                            edge.capturedMap->childMaps == capture.childMaps &&
                            edge.capturedMap->calls == capture.calls &&
                            edge.capturedMap->leafWrites == capture.leafWrites &&
                            edge.capturedMap->leafReads == capture.leafReads &&
                            edge.capturedMap->returnedLeaves == capture.returnedLeaves;
                if (edge.function != setter) { continue; }
                ++setters;
                complete &= edge.arguments.size() == 2;
                if (edge.arguments.size() != 2) { continue; }
                const auto & argument = edge.arguments.back();
                objects += static_cast<unsigned>(static_cast<bool>(argument.object));
                complete &= argument.parameter == parameter &&
                            argument.actual == edge.call->getOperand(prepared ? 5 : 3) &&
                            argument.alternatives == Alternatives{};
            }
            check(complete && setters == 3 && objects == 2,
                  "mixed child contents retain exact actuals and effects without scalar, key "
                  "presence or allocation identity authority");
            for (const auto & leaf : capture.returnedLeaves) {
                auto object = leaf.object;
                check(leaf.call && object && !capture.leafWrites.empty() &&
                          object.getResult() == capture.leafWrites.front()->getOperand(0) &&
                          owner.returnedLeaf(leaf.call->getResult(0)) == leaf.object,
                      "each exact returned leaf names the initialized caller payload");
            }
        }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "mixed child ownership preserves every source operation");
    };
    variant(fixture, true,
            "a mixed child read can compare with a scalar after an object-first census");
    variant(numberFirst, true, "a Number-first census retains the same mixed child proof");
    variant(replaced(fixture, "strict_eq %saved, %expected", "strict_eq %expected, %saved"), true,
            "strict identity checks either operand without narrowing a mixed payload");
    variant(replaced(fixture, "strict_eq %saved, %expected", "strict_eq %saved, %saved"), true,
            "a saved mixed value retains its identity for a repeated read operand");
    variant(replaced(fixture, "    ctjs.return %size\n  }\n}\n", R"MLIR(
    %readKey = ctjs.constant #ctjs.string<"get">
    %localReader = ctjs.get_property %value[%readKey]
    %localSaved = ctjs.call %localReader(%value, %payloadKey)
    %same = ctjs.compare strict_eq %localSaved, %input
    ctjs.return %same
  }
}
)MLIR"),
            true, "a current child read preserves strict identity with its caller payload");
    variant(replaced(fixture, comparison, "      %matches = ctjs.unary not %saved\n"), true,
            "Boolean negation observes a mixed child without primitive coercion");
    variant(replaced(fixture, comparison, R"MLIR(
      %flag = ctjs.truthy %saved
      %selected = scf.if %flag -> (!ctjs.value) {
        scf.yield %saved : !ctjs.value
      } else {
        scf.yield %expected : !ctjs.value
      }
      %matches = ctjs.compare strict_eq %selected, %expected
)MLIR"),
            true, "truthiness and branch transport preserve closed child alternatives");
    const auto missing =
        replaced(fixture, "#ctjs.string<\"payload\">", "#ctjs.string<\"missing\">");
    variant(missing, true, "the child category proof permits equality on an absent key");
    variant(replaced(fixture, comparison, fieldRead), false,
            "outer membership does not prove a mixed child payload is an object receiver");
    variant(replaced(missing, comparison, fieldRead), false,
            "a missing child key cannot borrow another payload's initialized field");
    variant(replaced(fixture, "%found = ctjs.call %hasMethod(%state, %outerKey)",
                     "%wrongKey = ctjs.constant #ctjs.string<\"wrong\">\n"
                     "    %found = ctjs.call %hasMethod(%state, %wrongKey)"),
            false, "child categories do not establish the outer lookup's membership");
    for (const char * cycle : {"%value", "%state"}) {
        variant(replaced(fixture, "%childSetter(%value, %payloadKey, %input)",
                         "%childSetter(%value, %payloadKey, " + std::string(cycle) + ")"),
                false, "a child cannot retain itself or its owning outer Map");
    }
    variant(replaced(fixture, comparison,
                     "      %escaped = ctjs.call %this(%this, %saved)\n" + comparison),
            false, "a mixed child read cannot escape to an unknown consumer");
    variant(replaced(fixture, "ctjs.set_property %payload[%fieldKey], %fieldValue",
                     "ctjs.set_property %payload[%fieldKey], %payload"),
            false, "a caller object cycle invalidates the complete child family");
    variant(replaced(fixture, "scf.yield %matches : !ctjs.value", "scf.yield %saved : !ctjs.value"),
            true, "a checked child leaf may return without primitive or output-carrier authority",
            4, 1);
    check(rows == 16, "all mixed child category, receiver and unsafe-use controls ran");

    const auto returned =
        replaced(fixture, "scf.yield %matches : !ctjs.value", "scf.yield %saved : !ctjs.value");
    const std::string laterGet =
        "    %againGetter = ctjs.get_property %owned[%key]\n" +
        std::string(prepared ? "    %againEnv = ctjs.load_upvalue %againGetter[0]\n"
                               "    %againAnswer = ctjs.call_direct @get$3(%owned, %u, "
                               "%againGetter, %againEnv)\n"
                             : "    %againAnswer = ctjs.call %againGetter(%owned)\n");
    const auto returnedIdentity =
        replaced(returned, "    ctjs.store_global \"trace\", %answer\n",
                 "    %entrySame = ctjs.compare strict_eq %answer, %fieldValue\n"
                 "    %entryNot = ctjs.unary not %answer\n"
                 "    %entryFlag = ctjs.truthy %answer\n" +
                     laterGet + "    ctjs.store_global \"trace\", %entrySame\n");
    variant(returnedIdentity, true,
            "a returned child owner permits non-coercing entry observations", 5);
    variant(
        replaced(returnedIdentity, "strict_eq %answer, %fieldValue", "strict_eq %answer, %payload"),
        true, "returned child identity can be compared with the original caller object", 5);
    variant(replaced(replaced(returnedIdentity, ", %actual, %payload)", ", %actual, %fieldValue)"),
                     ", %future, %fieldValue)", ", %future, %payload)"),
            true, "Number-first writes preserve the same owning returned-child proof", 5);
    const auto unguarded = replaced(returnedIdentity, "    %entrySame =",
                                    "    %uncheckedField = ctjs.get_property %answer[%fieldKey]\n"
                                    "    %entrySame =");
    variant(unguarded, true,
            "the exact x payload survives another child's scalar write before the field read", 5,
            2);
    variant(replaced(fixture, "ctjs.return %size\n  }\n}\n", "ctjs.return %input\n  }\n}\n"), false,
            "a direct caller-formal return still fails its complete use census");
    const std::string returnArgument =
        "    %returnedPutter = ctjs.get_property %owned[%putKey]\n" +
        std::string(prepared ? "    %returnedEnv = ctjs.load_upvalue %returnedPutter[0]\n"
                               "    %returnedCall = ctjs.call_direct @put$4(%owned, %u, "
                               "%returnedPutter, %returnedEnv, %actual, %answer)\n"
                             : "    %returnedCall = ctjs.call %returnedPutter(%owned, %actual, "
                               "%answer)\n");
    variant(replaced(returnedIdentity, laterGet, returnArgument + laterGet), false,
            "an owning result cannot provide its own family parameter or child-write proof");
    check(rows == 22, "all returned-child identity and independent refusal controls ran");

    const std::string observation = R"MLIR(
    %guard = ctjs.truthy %entrySame
    %observed = scf.if %guard -> (!ctjs.value) {
      %fieldResult = ctjs.get_property %answer[%fieldKey]
      scf.yield %fieldResult : !ctjs.value
    } else {
      scf.yield %fieldValue : !ctjs.value
    }
    ctjs.store_global "trace", %observed
)MLIR";
    const auto guarded = replaced(
        replaced(returnedIdentity, "strict_eq %answer, %fieldValue", "strict_eq %answer, %payload"),
        "    ctjs.store_global \"trace\", %entrySame\n", observation);
    variant(guarded, true, "a strict caller identity guard establishes the returned own field", 5);
    variant(replaced(guarded, "strict_eq %answer, %payload", "strict_eq %payload, %answer"), true,
            "the reversed strict identity guard establishes the same returned "
            "own field",
            5);
    variant(replaced(replaced(guarded, "%guard = ctjs.truthy %entrySame",
                              "%negated = ctjs.unary not %entrySame\n"
                              "    %guard = ctjs.truthy %negated"),
                     "      %fieldResult = ctjs.get_property %answer[%fieldKey]\n"
                     "      scf.yield %fieldResult : !ctjs.value\n"
                     "    } else {\n"
                     "      scf.yield %fieldValue : !ctjs.value",
                     "      scf.yield %fieldValue : !ctjs.value\n"
                     "    } else {\n"
                     "      %fieldResult = ctjs.get_property %answer[%fieldKey]\n"
                     "      scf.yield %fieldResult : !ctjs.value"),
            true, "negated strict identity establishes the own field only on the false arm", 5);
    variant(replaced(guarded, "%guard = ctjs.truthy %entrySame", "%guard = ctjs.truthy %answer"),
            true, "the actual returned leaf proves this receiver independently of truthiness", 5,
            2);
    variant(replaced(guarded, "get_property %answer[%fieldKey]",
                     "get_property %againAnswer[%fieldKey]"),
            true, "both unchanged lookups independently return the initialized caller leaf", 5, 2);
    variant(replaced(guarded, "get_property %answer[%fieldKey]", "get_property %answer[%key]"),
            false, "an identity guard does not establish an uninitialized own field");
    variant(replaced(guarded, "strict_eq %answer, %payload", "strict_eq %answer, %fieldValue"),
            true, "the exact returned leaf retains its field under a false scalar comparison", 5,
            2);
    check(rows == 29, "all guarded returned-field source and prepared controls ran");

    auto distinct = replaced(unguarded, "%actual = ctjs.constant #ctjs.string<\"x\">",
                             "%actual = ctjs.create_object");
    distinct = replaced(distinct, "%future = ctjs.constant #ctjs.string<\"y\">",
                        "%future = ctjs.create_object");
    const std::string getterSignature =
        "@get$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value" +
        std::string(prepared ? ", %state: !ctjs.value" : "");
    distinct = replaced(distinct, getterSignature, getterSignature + ", %outerKey: !ctjs.value");
    distinct = replaced(distinct, "    %outerKey = ctjs.constant #ctjs.string<\"x\">\n", "");
    distinct = replaced(distinct, prepared ? "%getterEnv)" : "%getter(%owned)",
                        prepared ? "%getterEnv, %actual)" : "%getter(%owned, %actual)");
    distinct = replaced(distinct, prepared ? "%againEnv)" : "%againGetter(%owned)",
                        prepared ? "%againEnv, %actual)" : "%againGetter(%owned, %actual)");
    const std::string seeded =
        "    %seeded = ctjs.call %childSetter(%value, %payloadKey, %input)\n";
    const std::string sizeBranch = R"MLIR(
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %eraser = ctjs.get_property %value[%deleteKey]
    %wrongKey = ctjs.constant #ctjs.string<"wrong">
    %erased = ctjs.call %eraser(%value, %wrongKey)
    %branchSizeKey = ctjs.constant #ctjs.string<"size">
    %branchSize = ctjs.get_property %value[%branchSizeKey]
    %zero = ctjs.constant #ctjs.number<0>
    %empty = ctjs.compare strict_eq %zero, %branchSize
    %emptyFlag = ctjs.truthy %empty
    scf.if %emptyFlag {
      %restored = ctjs.call %childSetter(%value, %payloadKey, %input)
      scf.yield
    } else {
      scf.yield
    }
)MLIR";
    const auto wrongDelete = replaced(distinct, seeded, seeded + sizeBranch);
    const auto emptyChild =
        replaced(wrongDelete, "%eraser(%value, %wrongKey)", "%eraser(%value, %payloadKey)");
    unsigned invocationRows = 0;
    const auto invocationVariant = [&](const std::string & text, bool expected, const char * label,
                                       bool live = false) {
        ++invocationRows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "object-key invocation source/prepared fixture parses");
        if (!module) { return; }
        module->walk([&](mlir::Operation * operation) {
            operation->setAttr("ctnative.host_owner_proved", mlir::UnitAttr::get(&context));
            operation->setAttr("ctnative.map_present", mlir::UnitAttr::get(&context));
            operation->setAttr("ctnative.object_origin", mlir::StringAttr::get(&context, "same"));
            operation->setAttr("ctnative.map_size",
                               mlir::IntegerAttr::get(mlir::IntegerType::get(&context, 64), 0));
        });
        const auto contract = requested(*module);
        HostContractAnalysis host(*module, contract);
        OwnedGlobalRoots owner(*module, contract);
        check(host.proved() == expected && owner.proved() == expected && !host.exhausted() &&
                  !owner.exhausted(),
              label);
        if (host.proved() != expected || owner.proved() != expected) {
            std::fprintf(stderr, "object-key invocation %s row %u: host=%s owner=%s\n",
                         prepared ? "prepared" : "source", invocationRows,
                         host.reason().str().c_str(), owner.reason().str().c_str());
        }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "invocation key and size queries preserve their complete source");
        if (!expected) {
            check(host.callables().empty() && empty(*module, owner),
                  "unproved invocation results publish no partial owner or callable");
            return;
        }
        if (!host.proved() || !owner.proved() || owner.roots().empty()) { return; }
        const auto & capture = *owner.roots().front().methodTable->capturedMap;
        auto payload =
            capture.leafWrites.front()->getOperand(0).getDefiningOp<ctjs::CreateObjectOp>();
        const auto returnedPayload = [&](const OwnedGlobalRoots & query) {
            return llvm::all_of(capture.returnedLeaves, [&](const auto & leaf) {
                return leaf.object == payload &&
                       query.returnedLeaf(leaf.call->getResult(0)) == payload;
            });
        };
        check(payload && capture.returnedLeaves.size() == 2 && returnedPayload(owner) &&
                  !owner.returnedLeaf(payload.getResult()),
              "two entry lookups return the payload despite distinct equal-shaped keys");
        if (!live) { return; }
        const unsigned completion = owner.steps();
        check(completion > 2 && completion < 100000,
              "object-key and size invocation proof completes within its ordinary budget");
        if (completion <= 2 || completion >= 100000) { return; }
        for (unsigned budget : {0u, 1u, completion / 2, completion - 1}) {
            OwnedGlobalRoots limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      empty(*module, limited) &&
                      llvm::all_of(capture.returnedLeaves,
                                   [&](const auto & leaf) {
                                       return !limited.returnedLeaf(leaf.call->getResult(0));
                                   }),
                  "incomplete invocation budgets withhold every owner and exact result");
        }
        OwnedGlobalRoots exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion && returnedPayload(exact),
              "the exact invocation budget publishes both independently returned leaves");
        std::vector<mlir::Operation *> setters;
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        for (const auto & edge : host.callables()) {
            if (edge.function == setter) { setters.push_back(edge.call); }
        }
        check(setters.size() == 3, "the invocation sequence retains all three child constructors");
        if (setters.size() != 3) { return; }
        const unsigned keyOperand = prepared ? 4u : 2u;
        const auto original = setters.back()->getOperand(keyOperand);
        setters.back()->setOperand(keyOperand, setters.front()->getOperand(keyOperand));
        OwnedGlobalRoots stale(*module, contract);
        HostContractAnalysis freshHost(*module, requested(*module));
        OwnedGlobalRoots fresh(*module, requested(*module));
        check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*module, stale) &&
                  !freshHost.proved() && !freshHost.exhausted() && freshHost.callables().empty() &&
                  !fresh.proved() && !fresh.exhausted() && empty(*module, fresh) &&
                  llvm::all_of(capture.returnedLeaves,
                               [&](const auto & leaf) {
                                   return !stale.returnedLeaf(leaf.call->getResult(0)) &&
                                          !fresh.returnedLeaf(leaf.call->getResult(0));
                               }),
              "stale and forged disjointness cannot hide an alias's later scalar replacement");
        setters.back()->setOperand(keyOperand, original);
        OwnedGlobalRoots restored(*module, contract);
        check(restored.proved() && returnedPayload(restored),
              "restoring the distinct actual rederives both exact caller-leaf returns");
        std::printf("object-key invocation %s: 1 live mutation, 4 incomplete budgets, %u steps\n",
                    prepared ? "prepared" : "source", completion);
    };
    invocationVariant(distinct, true,
                      "separate empty caller allocations retain independent child Maps", true);
    invocationVariant(replaced(distinct, "%future = ctjs.create_object",
                               "%future = ctjs.constant #ctjs.string<\"other\">"),
                      true, "a proved primitive key remains distinct from the caller object");
    invocationVariant(replaced(distinct, ", %future, %fieldValue)", ", %actual, %fieldValue)"),
                      false, "an actual alias replaces the original child with its scalar payload");
    invocationVariant(replaced(distinct, "%future = ctjs.create_object",
                               "%future = ctjs.load_global \"external\""),
                      false,
                      "a different global SSA value supplies no independent object identity");
    const auto recreated =
        replaced(replaced(distinct,
                          prepared ? "%repeatEnv, %actual, %payload)"
                                   : "%repeatPutter(%owned, %actual, %payload)",
                          prepared ? "%repeatEnv, %actual, %fieldValue)"
                                   : "%repeatPutter(%owned, %actual, %fieldValue)"),
                 ", %future, %fieldValue)", ", %actual, %payload)");
    invocationVariant(replaced(recreated, "    %future = ctjs.create_object\n", ""), true,
                      "a new invocation of one child constructor restores the caller object");
    invocationVariant(wrongDelete, true,
                      "wrong-key deletion keeps size positive and the exact payload alive", true);
    invocationVariant(emptyChild, true,
                      "deleting the actual child key proves zero size and selects reinsertion");
    invocationVariant(
        replaced(wrongDelete, "strict_eq %zero, %branchSize", "strict_eq %branchSize, %zero"), true,
        "a reversed zero-size comparison retains the same read-time proof");
    invocationVariant(
        replaced(replaced(wrongDelete, "%emptyFlag = ctjs.truthy %empty",
                          "%notEmpty = ctjs.unary not %empty\n"
                          "    %emptyFlag = ctjs.truthy %notEmpty"),
                 "      %restored = ctjs.call %childSetter(%value, %payloadKey, %input)\n"
                 "      scf.yield\n    } else {\n      scf.yield",
                 "      scf.yield\n    } else {\n"
                 "      %restored = ctjs.call %childSetter(%value, %payloadKey, %input)\n"
                 "      scf.yield"),
        true, "negation selects the opposite arm of the independently proved size comparison");
    invocationVariant(replaced(wrongDelete, "    %zero =",
                               "    %cleared = ctjs.call %eraser(%value, %payloadKey)\n"
                               "    %zero ="),
                      false, "a later deletion cannot rewrite an earlier positive size read");
    invocationVariant(
        replaced(wrongDelete,
                 "      %restored = ctjs.call %childSetter(%value, %payloadKey, %input)",
                 "      %unknown = ctjs.call %this(%this)\n"
                 "      %restored = ctjs.call %childSetter(%value, %payloadKey, %input)"),
        false, "an inactive zero-size arm still needs complete independent effects");
    check(invocationRows == 11, "all invocation object-key and size-branch controls ran");
    checkExactScalarReturns(context, distinct, prepared);

    for (const auto & liveFixture : {returnedIdentity, guarded, unguarded}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(liveFixture, &context);
        check(static_cast<bool>(module), "returned-child live source fixture parses");
        if (!module) { return; }
        module->walk([&](mlir::Operation * operation) {
            operation->setAttr("ctnative.host_owner_proved", mlir::UnitAttr::get(&context));
            operation->setAttr("ctnative.map_present", mlir::UnitAttr::get(&context));
            operation->setAttr("ctnative.object_origin",
                               mlir::StringAttr::get(&context, "payload"));
        });
        const auto contract = requested(*module);
        HostContractAnalysis host(*module, contract);
        OwnedGlobalRoots owner(*module, contract);
        check(host.proved() && owner.proved(),
              "owning return source proof is independent of forged reports");
        if (!host.proved() || !owner.proved()) { return; }
        auto entry = module->lookupSymbol<ctjs::FuncOp>("script$0");
        const auto & capture = *owner.roots().front().methodTable->capturedMap;
        const auto entryReads = llvm::count_if(capture.leafReads, [&](ctjs::GetPropertyOp read) {
            return read->getParentOfType<ctjs::FuncOp>() == entry;
        });
        check(entryReads == (liveFixture == returnedIdentity ? 0 : 1),
              "only guarded or exact-return entry fields enter the complete family evidence");
        auto getter = module->lookupSymbol<ctjs::FuncOp>("get$3");
        std::vector<mlir::Value> calls;
        for (const auto & edge : host.callables()) {
            if (edge.function == getter) { calls.push_back(edge.call->getResult(0)); }
        }
        auto payload =
            capture.leafWrites.front()->getOperand(0).getDefiningOp<ctjs::CreateObjectOp>();
        const auto expectedLeaf = liveFixture == unguarded ? payload : ctjs::CreateObjectOp{};
        check(payload && calls.size() == 2 &&
                  capture.returnedLeaves.size() == (expectedLeaf ? 2u : 0u) &&
                  llvm::all_of(
                      calls,
                      [&](mlir::Value call) { return owner.returnedLeaf(call) == expectedLeaf; }) &&
                  !owner.returnedLeaf(payload.getResult()),
              "exact result evidence names both actual returns but never the allocation value");
        const unsigned completion = owner.steps();
        check(completion > 2 && completion < 100000, "owning return proof has a bounded census");
        if (completion <= 2 || completion >= 100000) { return; }
        for (unsigned budget : {0u, 1u, completion / 2, completion - 1}) {
            OwnedGlobalRoots limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      empty(*module, limited) &&
                      llvm::all_of(calls,
                                   [&](mlir::Value call) { return !limited.returnedLeaf(call); }),
                  "incomplete owning return work publishes no partial ownership");
        }
        OwnedGlobalRoots exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion &&
                  llvm::all_of(
                      calls,
                      [&](mlir::Value call) { return exact.returnedLeaf(call) == expectedLeaf; }),
              "the exact owning return budget preserves its complete family");
        ctjs::CompareOp compare;
        ctjs::UnaryOp negate;
        ctjs::TruthyOp truthy;
        entry.walk([&](ctjs::CompareOp operation) { compare = operation; });
        entry.walk([&](ctjs::UnaryOp operation) { negate = operation; });
        entry.walk([&](ctjs::TruthyOp operation) { truthy = operation; });
        check(calls.size() == 2 && compare && negate && truthy,
              "owning return mutation targets retain two calls and the entry observations");
        if (calls.size() != 2 || !compare || !negate || !truthy) { return; }
        auto scoped = llvm::cast<ctjs::ReturnOp>(getter.getBody().front().getTerminator());
        unsigned mutations = 0;
        const auto mutation = [&](mlir::Operation * operation, mlir::Value invalid,
                                  bool expected = false) {
            ++mutations;
            const auto original = operation->getOperand(0);
            operation->setOperand(0, invalid);
            OwnedGlobalRoots stale(*module, contract);
            HostContractAnalysis freshHost(*module, requested(*module));
            OwnedGlobalRoots fresh(*module, requested(*module));
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      empty(*module, stale) &&
                      llvm::all_of(calls,
                                   [&](mlir::Value call) { return !stale.returnedLeaf(call); }),
                  "a changed source never retains stale owner or exact-result evidence");
            check(freshHost.proved() == expected && !freshHost.exhausted() &&
                      fresh.proved() == expected && !fresh.exhausted(),
                  "only an in-scope later getter can independently justify the changed guard");
            if (expected) {
                check(llvm::all_of(
                          calls,
                          [&](mlir::Value call) { return fresh.returnedLeaf(call) == payload; }),
                      "the changed truthy guard retains both exact caller-leaf returns");
            } else {
                check(freshHost.callables().empty() && empty(*module, fresh) &&
                          llvm::all_of(calls,
                                       [&](mlir::Value call) { return !fresh.returnedLeaf(call); }),
                      "out-of-scope values publish no callable, owner or exact-result evidence");
            }
            operation->setOperand(0, original);
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring the in-scope owning result restores its source proof");
        };
        for (mlir::Operation * operation :
             {compare.getOperation(), negate.getOperation(), truthy.getOperation()}) {
            for (mlir::Value invalid : std::vector<mlir::Value>{calls.back(), scoped.getValue()}) {
                mutation(operation, invalid,
                         liveFixture == guarded && operation == truthy.getOperation() &&
                             invalid == calls.back());
            }
        }
        if (liveFixture == guarded) {
            ctjs::GetPropertyOp field;
            entry.walk([&](ctjs::GetPropertyOp read) {
                if (read->getParentOp() != entry) { field = read; }
            });
            check(static_cast<bool>(field), "guarded mutation fixture has its nested field read");
            if (!field) { return; }
            auto branch = field->getParentOfType<mlir::scf::IfOp>();
            auto * yielded = branch.getElseRegion().front().getTerminator();
            mutation(yielded, field.getResult());
            mutation(yielded, branch.getResult(0));
        }
        std::printf("mixed child returns %s (%s): %u live mutations, 4 incomplete budgets, "
                    "%u steps\n",
                    prepared ? "prepared" : "source",
                    liveFixture == guarded     ? "guarded"
                    : liveFixture == unguarded ? "exact"
                                               : "identity",
                    mutations, completion);
    }

    auto module = mlir::parseSourceString<mlir::ModuleOp>(fixture, &context);
    check(static_cast<bool>(module), "mixed child live proof fixture parses");
    if (!module) { return; }
    module->walk([&](mlir::Operation * operation) {
        operation->setAttr("ctnative.host_owner_proved", mlir::UnitAttr::get(&context));
        operation->setAttr("ctnative.map_present", mlir::UnitAttr::get(&context));
        operation->setAttr("ctnative.map_read_type", mlir::StringAttr::get(&context, "object"));
        operation->setAttr("ctnative.object_origin", mlir::StringAttr::get(&context, "payload"));
    });
    const auto contract = requested(*module);
    OwnedGlobalRoots owner(*module, contract);
    check(owner.proved(), "forged reports leave the independent mixed child proof reproducible");
    if (!owner.proved()) { return; }
    const unsigned completion = owner.steps();
    check(completion > 2 && completion < 30000, "mixed child proof has a bounded complete census");
    if (completion <= 2 || completion >= 30000) { return; }
    for (unsigned budget : {0u, 1u, 2u, completion / 2, completion - 1}) {
        OwnedGlobalRoots limited(*module, contract, budget);
        check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                  empty(*module, limited),
              "an incomplete child write census publishes no category or owner records");
    }
    OwnedGlobalRoots exact(*module, contract, completion);
    check(exact.proved() && exact.steps() == completion,
          "the exact mixed child budget publishes the complete source family");
    auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
    ctjs::CallOp childWrite;
    setter.walk([&](ctjs::CallOp operation) {
        if (operation.getArgs().size() == 2 &&
            operation.getArgs()[1] == setter.getBody().front().getArgument(prepared ? 5 : 4)) {
            childWrite = operation;
        }
    });
    check(static_cast<bool>(childWrite), "the mixed census retains its original child write");
    if (!childWrite) { return; }
    unsigned mutations = 0;
    const auto mutation = [&](mlir::Operation * operation, unsigned operand, mlir::Value value) {
        ++mutations;
        const auto saved = operation->getOperand(operand);
        operation->setOperand(operand, value);
        OwnedGlobalRoots stale(*module, contract);
        HostContractAnalysis freshHost(*module, requested(*module));
        OwnedGlobalRoots fresh(*module, requested(*module));
        check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*module, stale) &&
                  !freshHost.proved() && !freshHost.exhausted() && freshHost.callables().empty() &&
                  !fresh.proved() && !fresh.exhausted() && empty(*module, fresh),
              "stale and forged reports cannot authorize cyclic or opaque live child writes");
        operation->setOperand(operand, saved);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring the child write restores independent family ownership");
    };
    mutation(childWrite, 3, childWrite.getReceiver());
    mutation(childWrite, 3, setter.getBody().front().getArgument(0));
    mutation(childWrite, 1, setter.getBody().front().getArgument(0));
    std::printf("mixed child %s: %u rows, %u live mutations, 5 incomplete budgets, %u steps\n",
                prepared ? "prepared" : "source", rows, mutations, completion);
}

} // namespace ctcompile::test::owned_global_shared_map
