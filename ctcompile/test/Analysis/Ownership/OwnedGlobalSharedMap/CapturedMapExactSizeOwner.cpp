#include "Tests.h"

namespace ctcompile::test::owned_global_shared_map {

void checkCapturedMapExactSizeOwner(mlir::MLIRContext & context, const std::string & source,
                                    bool prepared) {
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    auto fixture =
        replaced(source, "%entryKey: !ctjs.value)",
                 "%entryKey: !ctjs.value, %aliasKey: !ctjs.value, %choice: !ctjs.value)");
    fixture = replaced(fixture, "    %actual =",
                       "    %other = ctjs.constant #ctjs.string<\"z\">\n"
                       "    %choice = ctjs.constant #ctjs.boolean<true>\n    %actual =");
    for (const auto & [name, actual] :
         {std::pair{"putterEnv", "actual"}, {"repeatEnv", "actual"}, {"laterEnv", "future"}}) {
        const std::string prefix = prepared ? std::string("%") + name + ", " : "%owned, ";
        fixture = replaced(fixture, prefix + "%" + actual + ")",
                           prefix + "%" + actual + ", %other, %choice)");
    }
    const std::string write = "    %written = ctjs.call %setter(%state, %entryKey, %value)";
    const std::string clear = "    %cleared = ctjs.call %clearer(%state)\n";
    const std::string seed = "    %seeded = ctjs.call %setter(%state, %one, %value)\n";
    const std::string size = "    %saved = ctjs.get_property %state[%sizeKey] {snapshot}\n";
    const std::string extra = "    %extra = ctjs.call %setter(%state, %two, %value)\n";
    const std::string read = "    %loaded = ctjs.call %reader(%state, %saved)\n"
                             "    %answer = ctjs.get_property %loaded[%fieldKey]\n"
                             "    ctjs.return %answer\n  }\n}\n";
    const std::string setup =
        "    %clearKey = ctjs.constant #ctjs.string<\"clear\">\n"
        "    %clearer = ctjs.get_property %state[%clearKey]\n"
        "    %getKey = ctjs.constant #ctjs.string<\"get\">\n"
        "    %reader = ctjs.get_property %state[%getKey]\n"
        "    %one = ctjs.constant #ctjs.number<4607182418800017408>\n"
        "    %anotherOne = ctjs.constant #ctjs.number<4607182418800017408>\n"
        "    %two = ctjs.constant #ctjs.number<4611686018427387904>\n"
        "    %three = ctjs.constant #ctjs.number<4613937818241073152>\n"
        "    %zero = ctjs.constant #ctjs.number<0>\n"
        "    %negativeZero = ctjs.constant #ctjs.number<9223372036854775808>\n"
        "    %nan = ctjs.constant #ctjs.number<9221120237041090560>\n"
        "    %anotherNan = ctjs.constant #ctjs.number<9221120237041090561>\n"
        "    %false = ctjs.constant #ctjs.boolean<false>\n"
        "    %stringOne = ctjs.constant #ctjs.string<\"1\">\n"
        "    %fieldKey = ctjs.constant #ctjs.string<\"value\">\n"
        "    ctjs.set_property %value[%fieldKey], %one\n";
    fixture = replaced(fixture,
                       "    %size = ctjs.get_property %state[%sizeKey]\n"
                       "    ctjs.return %size\n  }\n}\n",
                       setup + clear + seed + size + read);
    const std::string both = "    %flag = ctjs.truthy %choice\n    scf.if %flag {\n" + clear +
                             seed +
                             "      scf.yield\n    } else {\n"
                             "      %again = ctjs.call %clearer(%state)\n"
                             "      %right = ctjs.call %setter(%state, %anotherOne, %value)\n"
                             "      %twice = ctjs.call %setter(%state, %one, %value)\n"
                             "      scf.yield\n    }\n";
    const auto bothArms = replaced(fixture, clear + seed, both);
    const auto twoEntries = replaced(fixture, seed + size, seed + extra + size);
    const auto census = [&](mlir::ModuleOp module, const OwnedGlobalRoots & query) {
        check(query.roots().size() == 1 && query.roots().front().methodTable &&
                  query.roots().front().methodTable->capturedMap,
              "exact-size proof retains its complete owner and Map family");
        if (query.roots().size() != 1 || !query.roots().front().methodTable ||
            !query.roots().front().methodTable->capturedMap) {
            return;
        }
        const auto & table = *query.roots().front().methodTable;
        auto setter = module.lookupSymbol<ctjs::FuncOp>("put$4");
        auto getter = module.lookupSymbol<ctjs::FuncOp>("get$3");
        std::vector<ctjs::CallOp> calls;
        std::vector<ctjs::GetPropertyOp> reads;
        std::vector<ctjs::GetPropertyOp> fields;
        getter.walk([&](ctjs::GetPropertyOp op) { reads.push_back(op); });
        setter.walk([&](ctjs::GetPropertyOp op) {
            auto key = op.getKey().getDefiningOp<ctjs::ConstantOp>();
            auto name = key ? llvm::dyn_cast<ctjs::StringAttr>(key.getValue()) : ctjs::StringAttr{};
            if (name && name.getValue() == "value") {
                fields.push_back(op);
            } else {
                reads.push_back(op);
            }
        });
        setter.walk([&](ctjs::CallOp op) { calls.push_back(op); });
        bool complete = table.methods.size() == 2 && table.calls.size() == 4;
        for (const auto & edge : table.calls) {
            complete &= edge.capturedMap && edge.capturedMap->calls == calls &&
                        edge.capturedMap->reads == reads && edge.capturedMap->leafReads == fields &&
                        edge.capturedMap->allocation == table.capturedMap->allocation;
            if (edge.function == setter) { complete &= edge.arguments.size() == 3; }
        }
        check(complete, "exact-size proof keeps every current call, read and future argument");
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & text, bool expected, const char * message) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "source/prepared exact-size owner fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        check(query.proved() == expected && !query.exhausted() &&
                  (expected || empty(*module, query)),
              message);
        if (query.proved() != expected || query.exhausted()) {
            std::fprintf(stderr, "exact-size owner %s row %u: %s\n",
                         prepared ? "prepared" : "source", rows, query.reason().str().c_str());
        }
        if (expected && query.proved()) { census(*module, query); }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "exact-size proof does not rewrite the evaluated read or source operations");
    };
    variant(fixture, true, "clear followed by one key gives an exact saved one");
    variant(replaced(fixture, write, ""), true,
            "exact cardinality after clear does not depend on earlier tracked entries");
    variant(replaced(fixture, size,
                     "    %evaluated = ctjs.get_property %state[%sizeKey]\n"
                     "    %saved = ctjs.constant #ctjs.number<4607182418800017408>\n"),
            true, "the literal-one repair preserves the evaluated size read");
    variant(replaced(fixture, seed, seed + replaced(extra, "%two", "%anotherOne")), true,
            "equal literal keys from different SSA constants count once");
    variant(replaced(fixture, size, size + extra), true,
            "a saved exact one survives growth to two keys");
    variant(replaced(fixture, size,
                     size + "    %again = ctjs.call %clearer(%state)\n"
                            "    %reset = ctjs.call %setter(%state, %one, %value)\n"),
            true, "an immutable saved one survives clearing and reseeding the Map");
    variant(replaced(fixture, size,
                     size + "    %again = ctjs.call %clearer(%state)\n"
                            "    %reset = ctjs.call %setter(%state, %three, %value)\n"),
            false, "preserving a size snapshot does not preserve deleted membership");
    variant(replaced(fixture, clear, ""), false,
            "a positive lower bound cannot exclude older captured Map entries");
    variant(replaced(fixture, clear + seed + size, size + clear + seed), false,
            "later clear and writes cannot establish an earlier size snapshot");
    variant(replaced(fixture, seed + size, size + seed), false,
            "an empty read before the write does not acquire the later exact one");
    variant(twoEntries, true, "two distinct complete keys give an exact saved two");
    variant(replaced(twoEntries, "%extra = ctjs.call %setter(%state, %two, %value)",
                     "%extra = ctjs.call %setter(%state, %three, %value)"),
            false, "exact size two does not make key two present among keys one and three");
    const std::string reset = "    %again = ctjs.call %clearer(%state)\n"
                              "    %reset = ctjs.call %setter(%state, %one, %value)\n";
    for (const auto & [left, right] :
         {std::pair{"%zero", "%negativeZero"}, {"%nan", "%anotherNan"}}) {
        auto duplicate =
            replaced(fixture, seed, replaced(seed, "%one", left) + replaced(extra, "%two", right));
        variant(replaced(duplicate, size, size + reset), true,
                "SameValueZero duplicate encodings preserve an exact saved one");
    }
    for (const auto & [left, right] : {std::pair{"%zero", "%false"}, {"%one", "%stringOne"}}) {
        auto distinct =
            replaced(fixture, seed, replaced(seed, "%one", left) + replaced(extra, "%two", right));
        variant(replaced(distinct, size, size + replaced(reset, "%one", "%two")), true,
                "equal coercions in distinct primitive categories still count as two Map keys");
    }
    variant(replaced(replaced(fixture, "%clearer = ctjs.get_property %state[%clearKey]",
                              "%clearer = ctjs.get_property %written[%clearKey]"),
                     "%clearer(%state)", "%clearer(%written)"),
            true, "the fluent alias clears the actual receiver's complete cardinality");
    variant(replaced(fixture, "%clearer = ctjs.get_property %state[%clearKey]",
                     "%clearer = ctjs.get_property %written[%clearKey]"),
            false, "an alias method lookup alone cannot authorize a different exact receiver");
    const std::string partial = "    %flag = ctjs.truthy %choice\n    scf.if %flag {\n" + extra +
                                "      scf.yield\n    }\n";
    const std::string uncertain = "    %firstKey = ctjs.call %setter(%state, %entryKey, %value)\n"
                                  "    %secondKey = ctjs.call %setter(%state, %aliasKey, %value)\n";
    for (const char * flag : {"true", "false"}) {
        const auto choice = [&](const std::string & text) {
            return replaced(text, "#ctjs.boolean<true>",
                            std::string("#ctjs.boolean<") + flag + ">");
        };
        variant(choice(bothArms), true,
                "both structural paths independently clear and leave the same single key");
        variant(choice(replaced(fixture, size, replaced(partial, "%two", "%three") + size)), false,
                "a conditional second key prevents exact-one evidence on the join");
        variant(choice(replaced(bothArms,
                                "      %right = ctjs.call %setter(%state, %anotherOne, %value)\n"
                                "      %twice = ctjs.call %setter(%state, %one, %value)\n",
                                "")),
                false, "one empty structural arm cannot inherit the other arm's exact one");
    }
    variant(replaced(fixture, seed, uncertain + replaced(seed, "%one", "%two")), false,
            "independent same-category formal keys may coincide and cannot prove exact two");
    variant(replaced(replaced(fixture, seed, uncertain + replaced(seed, "%one", "%two")), size,
                     "    %saved = ctjs.constant #ctjs.number<4611686018427387904>\n"),
            true,
            "an independently present literal-two key repairs the uncertain-cardinality read");
    const std::string selected = "    %flag = ctjs.truthy %choice\n"
                                 "    %saved = scf.if %flag -> (!ctjs.value) {\n"
                                 "      %left = ctjs.get_property %state[%sizeKey]\n"
                                 "      scf.yield %left : !ctjs.value\n"
                                 "    } else {\n      scf.yield %anotherOne : !ctjs.value\n    }\n";
    variant(replaced(fixture, size, selected), true,
            "a selected saved size and independent literal both establish one");
    variant(replaced(replaced(fixture, size, selected), "scf.yield %anotherOne", "scf.yield %two"),
            false, "a selected two cannot borrow the other arm's exact-one evidence");
    for (unsigned writes : {64u, 65u}) {
        std::string repeated = seed;
        for (unsigned index = 1; index < writes; ++index) {
            repeated += "    %repeat" + std::to_string(index) +
                        " = ctjs.call %setter(%state, %anotherOne, %value)\n";
        }
        variant(replaced(fixture, seed, repeated), writes == 64,
                writes == 64 ? "the complete sixty-four-candidate census proves one equal key"
                             : "sixty-five candidates refuse rather than truncate the upper bound");
    }
    check(rows == 30, "all exact-size owner cardinality and conservative controls ran");

    for (const auto & [text, label] :
         {std::pair{fixture, "one"}, {twoEntries, "two"}, {bothArms, "both arms"}}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "exact-size budget and live-mutation fixture parses");
        if (!module) { continue; }
        auto contract = requested(*module);
        OwnedGlobalRoots complete(*module, contract);
        const unsigned completion = complete.steps();
        check(complete.proved() && completion < 20000, "the exact-size proof has bounded work");
        if (!complete.proved() || completion >= 20000) { continue; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            OwnedGlobalRoots limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      empty(*module, limited),
                  "every incomplete exact-size budget withholds all ownership records");
        }
        OwnedGlobalRoots exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion,
              "the exact work budget reproduces complete current cardinality evidence");
        mlir::Builder attributes(&context);
        ctjs::GetPropertyOp snapshot;
        ctjs::CallOp lookup;
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        setter.walk([&](ctjs::GetPropertyOp op) {
            if (op->hasAttr("snapshot")) { snapshot = op; }
        });
        setter.walk([&](ctjs::CallOp op) {
            if (op.getArgs().size() == 1) { lookup = op; }
        });
        module->walk([&](mlir::Operation * op) {
            op->setAttr("ctnative.map_exact_size", attributes.getI32IntegerAttr(1));
            op->setAttr("ctnative.map_present", attributes.getUnitAttr());
        });
        contract = requested(*module);
        check(snapshot && lookup, "live exact-size control retains its read and later lookup");
        if (!snapshot || !lookup) { continue; }
        auto * restoreBefore = snapshot->getNextNode();
        auto * first = setter.getBody().front().front().getNextNode();
        // Keep definitions in scope: moving before the first clear changes
        // the evaluated state without altering the live Map or key operands.
        setter.walk([&](ctjs::CallOp op) {
            if (op.getArgs().empty() && first->getBlock() == op->getBlock() &&
                first->isBeforeInBlock(op)) {
                first = op;
            }
        });
        if (std::string(label) == "both arms") {
            auto branch = llvm::cast<mlir::scf::IfOp>(snapshot->getPrevNode());
            first = branch;
        }
        snapshot->moveBefore(first);
        OwnedGlobalRoots stale(*module, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*module, stale),
              "moving the evaluated size invalidates the original fingerprint");
        OwnedGlobalRoots fresh(*module, requested(*module));
        check(!fresh.proved() && !fresh.exhausted() && empty(*module, fresh),
              "fresh fingerprints and forged exact-size reports cannot prove the earlier read");
        snapshot->moveBefore(restoreBefore);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring the read position restores independent exact cardinality");
        const auto key = lookup.getArgs().front();
        auto wrong = snapshot.getKey();
        lookup->setOperand(2, wrong);
        OwnedGlobalRoots wrongKey(*module, requested(*module));
        check(!wrongKey.proved() && !wrongKey.exhausted() && empty(*module, wrongKey),
              "a different live lookup key cannot inherit an old exact-size result");
        lookup->setOperand(2, key);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring the saved lookup operand restores the live proof");
        std::printf("exact-size owner %s %s: %u rows and all %u incomplete budgets checked\n",
                    prepared ? "prepared" : "source", label, rows, completion);
    }
}

} // namespace ctcompile::test::owned_global_shared_map
