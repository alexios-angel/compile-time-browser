#include "Tests.h"

namespace ctcompile::test::owned_global_shared_map {
namespace {

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
            true, "a checked child leaf may return without primitive or output-carrier authority");
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

} // namespace

void checkLeafReadbackOwner(mlir::MLIRContext & context, const std::string & source, bool lifted) {
    checkMixedChildReadbacks(context, source, lifted);
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
