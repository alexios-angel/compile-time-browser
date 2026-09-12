#include "Tests.h"

namespace ctcompile::test::owned_global_shared_map {

void checkRetainedObjectKeyFamily(mlir::MLIRContext & context, const std::string & source,
                                  bool prepared) {
    using ctcompile::ctnative::PrimitiveAlternatives;
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto variant = [&](const std::string & text, bool expected, const char * message) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "retained sibling key fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
        HostContractAnalysis host(*module, contract);
        OwnedGlobalRoots owner(*module, contract);
        check(host.proved() == expected && owner.proved() == expected, message);
        if (host.proved() != expected || owner.proved() != expected) {
            std::fprintf(stderr, "retained sibling key %s: host=%s owner=%s\n",
                         prepared ? "prepared" : "source", host.reason().str().c_str(),
                         owner.reason().str().c_str());
        }
        if (!expected) {
            check(host.callables().empty() && empty(*module, owner),
                  "an unsafe sibling exposes no partial key owner");
        } else if (owner.proved()) {
            const auto & table = *owner.roots().front().methodTable;
            check(table.methods.size() == 2 && table.calls.size() == 2 && table.capturedMap &&
                      table.capturedMap->parameters.size() == 2,
                  "the complete two-method family owns one captured Map");
            if (table.calls.size() == 2) {
                const auto & first = table.calls[0].arguments;
                const auto & second = table.calls[1].arguments;
                check(first.size() == 1 && second.size() == 1 && first[0].object &&
                          second[0].object && first[0].alternatives == PrimitiveAlternatives{} &&
                          second[0].alternatives == PrimitiveAlternatives{},
                      "both siblings independently prove their actual object identity");
            }
        }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "retained key analysis preserves every source operation");
    };
    variant(source, true, "one local empty object may reach both exact captured-Map siblings");
    const std::string observation = "    ctjs.store_global \"trace\", %answer\n";
    variant(
        replaced(source, observation, "    ctjs.set_property %actual[%key], %u\n" + observation),
        false, "a later key write invalidates the complete family owner");
    variant(replaced(source, observation,
                     "    ctjs.store_global \"escapedKey\", %actual\n" + observation),
            false, "a named object still requires a separate global owner");
    variant(replaced(source, "%state, %entryKey, %value)", "%state, %entryKey, %entryKey)"), true,
            "the complete family may retain the checked empty object as both key and payload");
    variant(replaced(source, "    ctjs.return %u\n  }\n}\n",
                     "    ctjs.set_property %entryKey[%setKey], %state\n"
                     "    ctjs.return %u\n  }\n}\n"),
            false, "a sibling cannot create a key-to-Map ownership cycle");
    auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    if (!module) { return; }
    const auto contract = requested(*module);
    OwnedGlobalRoots owner(*module, contract);
    if (!owner.proved()) { return; }
    const auto calls = owner.roots().front().methodTable->calls;
    auto second = calls[1];
    const unsigned actualIndex = prepared ? 4u : 2u;
    const auto actual = second.call->getOperand(actualIndex);
    second.call->setOperand(actualIndex, second.read.getObject());
    check(!OwnedGlobalRoots(*module, contract).proved() &&
              !OwnedGlobalRoots(*module, requested(*module)).proved(),
          "stale facts and fresh fingerprints cannot substitute the owning table for a key");
    second.call->setOperand(actualIndex, actual);
    const unsigned completion = owner.steps();
    for (unsigned budget = 0; budget < completion; budget += std::max(1u, completion / 24)) {
        OwnedGlobalRoots limited(*module, contract, budget);
        check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                  empty(*module, limited),
              "incomplete sibling census budgets withhold the entire owning family");
    }
    check(!OwnedGlobalRoots(*module, contract, completion - 1).proved() &&
              OwnedGlobalRoots(*module, contract, completion).proved(),
          "the exact sibling owner completion budget is required");
    std::printf("retained sibling key %s: 5 rows, live edit and budget boundary %u checked\n",
                prepared ? "prepared" : "source", completion);
}

} // namespace ctcompile::test::owned_global_shared_map
