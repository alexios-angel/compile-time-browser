#include "Tests.h"

namespace ctcompile::test::owned_global_shared_map {

void checkObjectKeyArguments(mlir::MLIRContext & context, const std::string & source,
                             bool prepared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto evidence = [&](mlir::ModuleOp module, const HostContractAnalysis & host,
                              const OwnedGlobalRoots & owner, unsigned calls) {
        check(owner.roots().size() == 1 && owner.roots().front().methodTable &&
                  owner.roots().front().methodTable->capturedMap,
              "object arguments retain one ordinary published Map owner");
        if (owner.roots().size() != 1 || !owner.roots().front().methodTable ||
            !owner.roots().front().methodTable->capturedMap) {
            return;
        }
        const auto & table = *owner.roots().front().methodTable;
        auto getter = module.lookupSymbol<ctjs::FuncOp>("get$3");
        auto entry = module.lookupSymbol<ctjs::FuncOp>("script$0");
        const auto parameter = getter.getBody().front().getArgument(prepared ? 4 : 3);
        bool complete = table.methods.size() == 1 && table.calls.size() == calls &&
                        host.callables().size() == calls &&
                        table.capturedMap->parameters.size() == 1 &&
                        table.capturedMap->leafObjects.empty();
        mlir::Operation * previous = nullptr;
        for (const auto & edge : table.calls) {
            const auto * checked = host.callable(edge.call);
            complete &= checked && edge.arguments.size() == 1 && edge.capturedMap &&
                        (!previous || previous->isBeforeInBlock(edge.call));
            previous = edge.call;
            if (!checked || edge.arguments.size() != 1 || checked->arguments.size() != 1 ||
                !edge.capturedMap || edge.capturedMap->parameters.size() != 1) {
                complete = false;
                continue;
            }
            const auto & argument = edge.arguments.front();
            auto object = argument.object;
            const auto & family = edge.capturedMap->parameters.front();
            complete &= object && object->getParentOp() == entry &&
                        object.getResult() == argument.actual &&
                        object->isBeforeInBlock(edge.call) && argument.parameter == parameter &&
                        argument.actual == edge.call->getOperand(prepared ? 4 : 2) &&
                        argument.alternatives == Alternatives{} &&
                        checked->arguments.front().object == object &&
                        checked->arguments.front().actual == argument.actual &&
                        family.function == getter && family.objectKeys == std::vector{parameter} &&
                        family.alternatives == std::vector{Alternatives{}} &&
                        edge.capturedMap->allocation == table.capturedMap->allocation &&
                        edge.capturedMap->parameters == table.capturedMap->parameters;
        }
        check(complete, "each object actual retains its own allocation and formal without a "
                        "primitive category or method-local payload identity");
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & text, bool expected, const char * message,
                             unsigned calls = 1) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "object-key source/prepared fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
        HostContractAnalysis host(*module, contract);
        OwnedGlobalRoots owner(*module, contract);
        check(host.proved() == expected && owner.proved() == expected && !host.exhausted() &&
                  !owner.exhausted(),
              message);
        if (host.proved() != expected || owner.proved() != expected) {
            std::fprintf(stderr, "object-key %s row %u: host=%s owner=%s\n",
                         prepared ? "prepared" : "source", rows, host.reason().str().c_str(),
                         owner.reason().str().c_str());
        }
        if (expected && host.proved() && owner.proved()) {
            evidence(*module, host, owner, calls);
        } else if (!expected) {
            check(host.callables().empty() && empty(*module, owner),
                  "an unproved object argument exposes no partial callable or owner");
        }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "object-key queries preserve every allocation and call");
    };
    const std::string allocation = "    %actual = ctjs.create_object\n";
    const std::string observation = "    ctjs.store_global \"trace\", %answer\n";
    const std::string has = "    %found = ctjs.call %method(%state, %entryKey)\n";
    const auto repeat = [&](const std::string & actual, const std::string & definition) {
        return replaced(source, observation,
                        "    %again = ctjs.get_property %owned[%key]\n" + definition +
                            (prepared ? "    %againEnv = ctjs.load_upvalue %again[0]\n"
                                        "    %againResult = ctjs.call_direct @get$3(%owned, %u, "
                                        "%again, %againEnv, "
                                      : "    %againResult = ctjs.call %again(%owned, ") +
                            actual + ")\n" + observation);
    };
    variant(source, true, "an empty fresh object can be borrowed by captured Map.has");
    variant(repeat("%actual", ""), true, "repeated calls retain the same actual identity", 2);
    variant(repeat("%other", "    %other = ctjs.create_object\n"), true,
            "distinct empty actuals keep separate identities in one complete object census", 2);
    variant(repeat("%other", "    %other = ctjs.constant #ctjs.number<0>\n"), false,
            "a later Number actual cannot borrow an earlier object's argument proof");
    variant(replaced(repeat("%other", "    %other = ctjs.create_object\n"), allocation,
                     "    %actual = ctjs.constant #ctjs.number<0>\n"),
            false, "an earlier Number actual cannot authorize a later object actual");
    variant(replaced(source, allocation, allocation + "    ctjs.set_property %actual[%key], %u\n"),
            false, "an initialized own field is outside the empty-object argument proof");
    variant(
        replaced(source, observation, "    ctjs.set_property %actual[%key], %u\n" + observation),
        false, "a later object mutation participates in the complete use census");
    variant(
        replaced(source, allocation, allocation + "    ctjs.store_global \"namedKey\", %actual\n"),
        false, "a named global object needs an independent ordinary owner");
    variant(replaced(source, allocation,
                     allocation + "    %outer = ctjs.create_object\n"
                                  "    ctjs.set_property %outer[%key], %actual\n"),
            false, "an object retained inside another object needs its own ownership proof");
    variant(replaced(source, allocation, "    %actual = ctjs.load_global \"external\"\n"), false,
            "an unknown external object cannot inherit the fresh actual's identity");
    variant(replaced(source, has, "    %read = ctjs.get_property %entryKey[%key]\n" + has), false,
            "borrowing an object as a key does not prove its own fields");
    variant(replaced(source, has, "    ctjs.set_property %entryKey[%key], %state\n" + has), false,
            "a method cannot mutate or attach its Map to the borrowed object");
    variant(replaced(source, "ctjs.return %found", "ctjs.return %entryKey"), false,
            "a borrowed key cannot become an owned public result");
    for (const char * method : {"get", "delete"}) {
        variant(replaced(source, "#ctjs.string<\"has\">",
                         std::string("#ctjs.string<\"") + method + "\">"),
                true, "a captured Map can read or delete the exact empty object key");
    }
    for (const bool key : {false, true}) {
        auto storing = replaced(source, "#ctjs.string<\"has\">", "#ctjs.string<\"set\">");
        storing = replaced(storing, has,
                           "    %zero = ctjs.constant #ctjs.number<0>\n"
                           "    %stored = ctjs.call %method(%state, " +
                               std::string(key ? "%entryKey, %zero" : "%zero, %entryKey") +
                               ")\n    %found = ctjs.constant #ctjs.boolean<false>\n");
        variant(storing, true, "checked empty objects may be retained as Map keys or payloads");
    }
    const std::string retain = "    %setKey = ctjs.constant #ctjs.string<\"set\">\n"
                               "    %setter = ctjs.get_property %state[%setKey]\n"
                               "    %one = ctjs.constant #ctjs.number<4607182418800017408>\n"
                               "    %stored = ctjs.call %setter(%state, %entryKey, %one)\n";
    variant(replaced(source, has, retain + has), true,
            "retaining an object key preserves its identity for the later has");
    variant(replaced(replaced(source, has, retain + has), "#ctjs.string<\"has\">",
                     "#ctjs.string<\"get\">"),
            true, "a read after set keeps the exact object key's scalar payload");
    variant(replaced(repeat("%actual", ""), has, retain + has), true,
            "later calls may overwrite a retained key without creating an ownership cycle", 2);
    variant(replaced(repeat("%other", "    %other = ctjs.create_object\n"), has, retain + has),
            true, "separate object actuals remain distinct retained Map keys", 2);
    variant(replaced(source, has,
                     "    %unknown = ctjs.load_global \"unknown\"\n"
                     "    %escaped = ctjs.call %unknown(%state, %entryKey)\n" +
                         has),
            false, "unknown calls cannot retain or mutate a borrowed key");
    if (!prepared) {
        variant(replaced(source, "ctjs.call %getter(%owned, %actual)", "ctjs.call %getter(%owned)"),
                false, "a missing object argument cannot supply a formal identity");
        variant(replaced(source, "ctjs.call %getter(%owned, %actual)",
                         "ctjs.call %getter(%owned, %actual, %actual)"),
                false, "a surplus object argument cannot extend the complete source signature");
    }
    auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    check(static_cast<bool>(module), "object-key budget and live-mutation fixture parses");
    if (!module) { return; }
    const auto contract = requested(*module);
    HostContractAnalysis host(*module, contract);
    OwnedGlobalRoots owner(*module, contract);
    if (!host.proved() || !owner.proved()) { return; }
    auto edge = owner.roots().front().methodTable->calls.front();
    auto object = edge.arguments.front().object;
    mlir::Builder attributes(&context);
    (*module)->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
    object->setAttr("ctnative.object_schema", attributes.getStringAttr("empty"));
    const auto forgedContract = requested(*module);
    check(OwnedGlobalRoots(*module, forgedContract).proved(),
          "forged object schema reports do not replace independent actual ownership");
    const auto refuseMutation = [&] {
        OwnedGlobalRoots stale(*module, forgedContract);
        HostContractAnalysis freshHost(*module, requested(*module));
        OwnedGlobalRoots fresh(*module, requested(*module));
        check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*module, stale) &&
                  !freshHost.proved() && freshHost.callables().empty() && !fresh.proved() &&
                  empty(*module, fresh),
              "stale or forged object reports cannot repair changed actual ownership");
    };
    edge.call->setOperand(prepared ? 4 : 2, edge.read.getObject());
    refuseMutation();
    edge.call->setOperand(prepared ? 4 : 2, object.getResult());
    auto * next = object->getNextNode();
    object->moveAfter(edge.call);
    refuseMutation();
    object->moveBefore(next);
    if (prepared) {
        const auto environment = edge.call->getOperand(3);
        edge.call->setOperand(3, object.getResult());
        edge.call->setOperand(4, environment);
        refuseMutation();
        edge.call->setOperand(3, environment);
        edge.call->setOperand(4, object.getResult());
    }
    object->removeAttr("ctnative.object_schema");
    check(OwnedGlobalRoots(*module, contract).proved(),
          "restoring the exact actual and source order restores independent ownership");
    const unsigned completion = owner.steps();
    check(completion < 10000, "the object actual ownership proof remains bounded");
    if (completion >= 10000) { return; }
    for (unsigned budget = 0; budget < completion; ++budget) {
        OwnedGlobalRoots limited(*module, contract, budget);
        check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                  empty(*module, limited),
              "every incomplete object argument budget withholds the entire owning family");
    }
    OwnedGlobalRoots exact(*module, contract, completion);
    check(exact.proved() && exact.steps() == completion,
          "the exact object argument budget reproduces the complete source owner");
    if (exact.proved()) { evidence(*module, host, exact, 1); }
    std::printf("object-key owner %s: %u rows and all %u incomplete budgets checked\n",
                prepared ? "prepared" : "source", rows, completion);
}

} // namespace ctcompile::test::owned_global_shared_map
