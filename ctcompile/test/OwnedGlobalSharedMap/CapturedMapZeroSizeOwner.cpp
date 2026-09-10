#include "Tests.h"

namespace ctcompile::test::owned_global_shared_map {

void checkCapturedMapZeroSizeOwner(mlir::MLIRContext & context, const std::string & source,
                                   bool prepared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    auto fixture = replaced(source, "%entryKey: !ctjs.value)",
                            "%entryKey: !ctjs.value, %choice: !ctjs.value)");
    fixture = replaced(
        fixture, "    %actual =", "    %choice = ctjs.constant #ctjs.boolean<true>\n    %actual =");
    for (const auto & [name, actual] :
         {std::pair{"putterEnv", "actual"}, {"repeatEnv", "actual"}, {"laterEnv", "future"}}) {
        const std::string prefix = prepared ? std::string("%") + name + ", " : "%owned, ";
        fixture =
            replaced(fixture, prefix + "%" + actual + ")", prefix + "%" + actual + ", %choice)");
    }
    const std::string write = "    %written = ctjs.call %setter(%state, %entryKey, %value)";
    const std::string clear = "    %cleared = ctjs.call %clearer(%state)\n";
    const std::string size = "    %zero = ctjs.get_property %state[%sizeKey]\n";
    const std::string grow = "    %grown = ctjs.call %setter(%state, %one, %value)\n";
    const std::string read = "    %loaded = ctjs.call %reader(%state, %zero)\n";
    const std::string setup =
        "    %clearKey = ctjs.constant #ctjs.string<\"clear\">\n"
        "    %clearer = ctjs.get_property %state[%clearKey]\n"
        "    %getKey = ctjs.constant #ctjs.string<\"get\">\n"
        "    %reader = ctjs.get_property %state[%getKey]\n"
        "    %deleteKey = ctjs.constant #ctjs.string<\"delete\">\n"
        "    %eraser = ctjs.get_property %state[%deleteKey]\n"
        "    %one = ctjs.constant #ctjs.number<4607182418800017408>\n"
        "    %literalZero = ctjs.constant #ctjs.number<0>\n"
        "    %negativeZero = ctjs.constant #ctjs.number<9223372036854775808>\n";
    fixture = replaced(fixture,
                       "    %size = ctjs.get_property %state[%sizeKey]\n"
                       "    ctjs.return %size\n  }\n}\n",
                       setup + clear + size + grow + read + "    ctjs.return %loaded\n  }\n}\n");
    const std::string both = "    %flag = ctjs.truthy %choice\n    scf.if %flag {\n" + clear +
                             "      scf.yield\n    } else {\n"
                             "      %again = ctjs.call %clearer(%state)\n"
                             "      %twice = ctjs.call %clearer(%state)\n"
                             "      scf.yield\n    }\n";
    const auto bothArms = replaced(fixture, clear, both);
    const auto census = [&](mlir::ModuleOp module, const OwnedGlobalRoots & query) {
        check(query.roots().size() == 1 && query.roots().front().methodTable &&
                  query.roots().front().methodTable->capturedMap,
              "exact-zero proof retains the whole method table and captured Map");
        if (query.roots().size() != 1 || !query.roots().front().methodTable ||
            !query.roots().front().methodTable->capturedMap) {
            return;
        }
        const auto & table = *query.roots().front().methodTable;
        auto setter = module.lookupSymbol<ctjs::FuncOp>("put$4");
        auto getter = module.lookupSymbol<ctjs::FuncOp>("get$3");
        std::vector<ctjs::CallOp> calls;
        std::vector<ctjs::GetPropertyOp> reads;
        getter.walk([&](ctjs::GetPropertyOp op) { reads.push_back(op); });
        setter.walk([&](ctjs::GetPropertyOp op) { reads.push_back(op); });
        setter.walk([&](ctjs::CallOp op) { calls.push_back(op); });
        bool complete = table.methods.size() == 2 && table.calls.size() == 4;
        for (const auto & edge : table.calls) {
            complete &= edge.capturedMap && edge.capturedMap->calls == calls &&
                        edge.capturedMap->reads == reads &&
                        edge.capturedMap->allocation == table.capturedMap->allocation;
            if (edge.function != setter) { continue; }
            complete &= edge.arguments.size() == 2;
            if (edge.arguments.size() == 2) {
                complete &= edge.arguments[0].alternatives ==
                                Alternatives::forTag(mlir::TypeID::get<ctjs::StringAttr>()) &&
                            edge.arguments[1].alternatives ==
                                Alternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>());
            }
        }
        check(complete, "exact-zero proof retains all reads, mutations and future callable inputs");
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & text, bool expected, const char * message) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "source/prepared exact-zero owner fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        check(query.proved() == expected && !query.exhausted() &&
                  (expected || empty(*module, query)),
              message);
        if (query.proved() != expected || query.exhausted()) {
            std::fprintf(stderr, "zero-size owner %s row %u: %s\n",
                         prepared ? "prepared" : "source", rows, query.reason().str().c_str());
        }
        if (expected && query.proved()) { census(*module, query); }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "exact-zero query preserves the source operations and snapshot position");
    };
    variant(fixture, true, "a saved size after clear remains zero after insertion of key one");
    variant(replaced(fixture, write, ""), true,
            "clear establishes exact zero without any preceding known entries");
    variant(replaced(fixture, clear, clear + "    %again = ctjs.call %clearer(%state)\n"), true,
            "repeated clear preserves exact zero at the subsequent size read");
    variant(replaced(fixture, size, "    %zero = ctjs.constant #ctjs.number<0>\n"), true,
            "the literal-zero repair independently proves the same disjoint key");
    variant(replaced(fixture, clear + size, size + clear), false,
            "a later clear cannot rewrite an earlier unknown or nonzero size snapshot");
    variant(replaced(fixture, size + grow, grow + size), false,
            "a size read after insertion cannot inherit clear's earlier empty state");
    variant(replaced(fixture, clear, ""), false,
            "unknown captured Map cardinality and a zero lower bound are not exact zero");
    variant(replaced(fixture, read,
                     "    %laterClear = ctjs.call %clearer(%state)\n"
                     "    %regrown = ctjs.call %setter(%state, %one, %value)\n" +
                         read),
            true, "known later clear and growth preserve an already saved zero value");
    variant(replaced(fixture, grow, replaced(grow, "%one", "%literalZero")), false,
            "an exact zero-key object write defeats the absent primitive result");
    variant(replaced(fixture, grow, replaced(grow, "%one", "%negativeZero")), false,
            "negative zero and a saved exact zero are the same Map key");
    variant(replaced(replaced(fixture, grow, replaced(grow, "%one", "%negativeZero")),
                     "    ctjs.return %loaded",
                     "    %same = ctjs.compare strict_eq %loaded, %value\n"
                     "    ctjs.return %same"),
            true, "the signed-zero lookup recovers its exact stored object identity");
    variant(replaced(fixture, "%clearer = ctjs.get_property %state[%clearKey]",
                     "%clearer = ctjs.get_property %written[%clearKey]"),
            false, "a method lookup alias does not authorize a different exact call receiver");
    variant(replaced(replaced(fixture, "%clearer = ctjs.get_property %state[%clearKey]",
                              "%clearer = ctjs.get_property %written[%clearKey]"),
                     "%clearer(%state)", "%clearer(%written)"),
            true, "fluent set aliases establish zero only for the same runtime Map");
    const std::string oneArm = "    %flag = ctjs.truthy %choice\n    scf.if %flag {\n" + clear +
                               "      scf.yield\n    }\n";
    const std::string mixedArms = "    %flag = ctjs.truthy %choice\n    scf.if %flag {\n" + clear +
                                  "      scf.yield\n    } else {\n"
                                  "      %deleted = ctjs.call %eraser(%state, %entryKey)\n"
                                  "      scf.yield\n    }\n";
    for (const char * flag : {"true", "false"}) {
        const auto choice = [&](const std::string & text) {
            return replaced(text, "#ctjs.boolean<true>",
                            std::string("#ctjs.boolean<") + flag + ">");
        };
        variant(choice(bothArms), true,
                "different clear operations on every arm establish exact empty cardinality");
        variant(choice(replaced(fixture, clear, oneArm)), false,
                "startup Boolean values cannot erase a future uncleared path");
        variant(choice(replaced(fixture, clear, mixedArms)), false,
                "deletion of one key cannot establish emptiness on an otherwise unknown arm");
    }
    const std::string selected = "    %flag = ctjs.truthy %choice\n"
                                 "    %zero = scf.if %flag -> (!ctjs.value) {\n"
                                 "      %left = ctjs.get_property %state[%sizeKey]\n"
                                 "      scf.yield %left : !ctjs.value\n"
                                 "    } else {\n"
                                 "      scf.yield %negativeZero : !ctjs.value\n    }\n";
    variant(replaced(fixture, size, selected), true,
            "a selected value is exact zero when both its saved-size and literal arms are zero");
    variant(
        replaced(replaced(fixture, size, selected), "scf.yield %negativeZero", "scf.yield %one"),
        false, "one nonzero yielded value defeats the exact-zero join");
    check(rows == 21, "all exact-zero owner and conservative join controls ran");

    for (const auto & [text, label] : {std::pair{fixture, "saved"}, {bothArms, "both arms"}}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "exact-zero budget fixture parses");
        if (!module) { continue; }
        auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        const unsigned completion = query.steps();
        check(query.proved() && completion < 20000, "exact-zero complete proof is bounded");
        if (!query.proved() || completion >= 20000) { continue; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            OwnedGlobalRoots limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      empty(*module, limited),
                  "every incomplete exact-zero budget withholds all published ownership");
        }
        OwnedGlobalRoots exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion,
              "the exact work budget reproduces the complete saved-zero proof");
        mlir::Builder attributes(&context);
        module->walk([&](mlir::Operation * op) {
            op->setAttr("ctnative.map_empty", attributes.getBoolAttr(true));
            op->setAttr("ctnative.map_size_zero", attributes.getBoolAttr(true));
            op->setAttr("ctnative.map_present", attributes.getUnitAttr());
        });
        contract = requested(*module);
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        ctjs::GetPropertyOp snapshot;
        ctjs::CallOp growth;
        setter.walk([&](ctjs::GetPropertyOp op) {
            auto key = op.getKey().getDefiningOp<ctjs::ConstantOp>();
            auto name = key ? llvm::dyn_cast<ctjs::StringAttr>(key.getValue()) : ctjs::StringAttr{};
            if (name && name.getValue() == "size") { snapshot = op; }
        });
        setter.walk([&](ctjs::CallOp op) {
            if (op.getArgs().size() == 2) { growth = op; }
        });
        check(snapshot && growth && snapshot->isBeforeInBlock(growth),
              "the live saved-zero fixture keeps read-before-growth order");
        if (!snapshot || !growth) { continue; }
        auto * restoreBefore = snapshot->getNextNode();
        snapshot->moveAfter(growth);
        OwnedGlobalRoots stale(*module, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*module, stale),
              "moving the actual size read invalidates its old fingerprint");
        OwnedGlobalRoots fresh(*module, requested(*module));
        check(!fresh.proved() && !fresh.exhausted() && empty(*module, fresh),
              "fresh fingerprints and forged zero reports cannot repair a post-growth read");
        snapshot->moveBefore(restoreBefore);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring read-time emptiness restores the independent saved-zero proof");
        std::printf("zero-size owner %s %s: %u rows and all %u incomplete budgets checked\n",
                    prepared ? "prepared" : "source", label, rows, completion);
    }
}

} // namespace ctcompile::test::owned_global_shared_map
