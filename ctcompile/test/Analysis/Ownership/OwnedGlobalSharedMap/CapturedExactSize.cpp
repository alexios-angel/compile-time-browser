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

    // Imported early returns retain their original entry-frame exits in both
    // scalar SCF arms. This witness needs no cross-invocation child invariant.
    const auto enterFrame = [](std::string text, llvm::StringRef function, llvm::StringRef name) {
        const auto start = text.find('\n', text.find("ctjs.func private @" + function.str()));
        check(start != std::string::npos, "frame fixture retains its source method");
        if (start != std::string::npos) {
            text.insert(start + 1, "    %" + name.str() + " = ctjs.frame_enter 8\n");
        }
        return text;
    };
    const auto framed = enterFrame(fixture, "put$4", "frame");
    const std::string exit = "    ctjs.frame_exit %frame\n";
    const std::string returned = "    ctjs.return %answer";
    const auto straight =
        replaced(framed, returned, "    ctjs.root %answer in %frame\n" + exit + returned);
    const std::string leftExit = "      ctjs.frame_exit %frame {left_exit}\n";
    const std::string rightExit = "      ctjs.frame_exit %frame {right_exit}\n";
    const std::string selectedReturn =
        "    %finishFlag = ctjs.truthy %choice\n"
        "    %selected = scf.if %finishFlag -> (!ctjs.value) {\n"
        "      ctjs.root %answer in %frame\n" +
        leftExit + "      scf.yield %answer : !ctjs.value\n    } else {\n" + rightExit +
        "      scf.yield %one : !ctjs.value\n    }\n"
        "    ctjs.return %selected";
    const auto twoReturns = replaced(framed, returned, selectedReturn);
    variant(straight, true, "one original frame is rooted and exited before the scalar return");
    variant(twoReturns, true, "both scalar return arms exit the same original entry frame");
    variant(replaced(twoReturns, "      scf.yield %answer : !ctjs.value",
                     "      scf.yield %one : !ctjs.value"),
            true, "frame validation does not depend on either arm's scalar result identity");
    variant(framed, false, "a live entry frame must exit before its return");
    variant(replaced(twoReturns, leftExit, ""), false,
            "a missing then-arm exit cannot borrow the else-arm exit");
    variant(replaced(twoReturns, rightExit, ""), false,
            "a missing else-arm exit cannot borrow the then-arm exit");
    variant(replaced(twoReturns, leftExit, leftExit + leftExit), false,
            "a return arm cannot exit the original frame twice");
    variant(replaced(straight, exit, exit + exit), false,
            "a top-level return cannot exit the original frame twice");
    variant(replaced(twoReturns, "    ctjs.return %selected", exit + "    ctjs.return %selected"),
            false, "a scalar join of exited arms cannot exit the frame again");
    variant(replaced(twoReturns, rightExit, rightExit + "      ctjs.root %one in %frame\n"), false,
            "a root cannot use a frame after its return-arm exit");
    variant(replaced(straight, exit, exit + "    %late = ctjs.call %clearer(%state)\n"), false,
            "effects cannot follow an original frame exit");
    variant(replaced(straight, "    %frame = ctjs.frame_enter 8\n",
                     "    %frame = ctjs.frame_enter 8\n"
                     "    %second = ctjs.frame_enter 8\n"),
            false, "a second entry frame cannot hide behind a valid original exit");
    variant(replaced(twoReturns, rightExit, "      %nested = ctjs.frame_enter 8\n" + rightExit),
            false, "a conditional frame entry cannot replace the original entry frame");
    variant(replaced(straight, "ctjs.frame_enter 8", "ctjs.frame_enter -1"), false,
            "a negative frame register count cannot acquire an ownership proof");
    check(rows == 44, "all exact-size and original-frame lifecycle controls ran");

    const std::string maybeSeed = "    %seedFlag = ctjs.truthy %choice\n"
                                  "    scf.if %seedFlag {\n" +
                                  seed + "      scf.yield\n    }\n";
    const std::string observe = "    %hasKey = ctjs.constant #ctjs.string<\"has\">\n"
                                "    %hasMethod = ctjs.get_property %state[%hasKey]\n"
                                "    %observed = ctjs.call %hasMethod(%state, %one)\n";
    const std::string negate = "    %inverted = ctjs.unary not %observed\n"
                               "    %guard = ctjs.truthy %inverted\n";
    const std::string leafRead = "      %loaded = ctjs.call %reader(%state, %one)\n"
                                 "      %answer = ctjs.get_property %loaded[%fieldKey]\n";
    const std::string scalarYield = "      scf.yield %one : !ctjs.value\n";
    const std::string readYield = leafRead + "      scf.yield %answer : !ctjs.value\n";
    const auto selection = [&](bool readOnTrue) {
        return "    %selected = scf.if %guard -> (!ctjs.value) {\n" +
               (readOnTrue ? readYield : scalarYield) + "    } else {\n" +
               (readOnTrue ? scalarYield : readYield) +
               "    }\n"
               "    ctjs.return %selected\n  }\n}\n";
    };
    const auto inverted = replaced(fixture, clear + seed + size + read,
                                   clear + maybeSeed + observe + negate + selection(false));
    variant(inverted, true, "the false arm of not-has restores only the observed key's presence");
    variant(replaced(inverted, negate, "    %guard = ctjs.truthy %observed\n"), false,
            "a false has arm cannot inherit the sibling's positive membership");
    const auto direct = replaced(replaced(inverted, negate, "    %guard = ctjs.truthy %observed\n"),
                                 selection(false), selection(true));
    variant(direct, true, "the positive has arm retains its existing membership proof");
    variant(replaced(direct, "    %guard = ctjs.truthy %observed\n",
                     "    %firstNot = ctjs.unary not %observed\n"
                     "    %secondNot = ctjs.unary not %firstNot\n"
                     "    %guard = ctjs.truthy %secondNot\n"),
            true, "two nested not operations restore positive guard polarity");
    variant(replaced(inverted, negate,
                     "    %firstNot = ctjs.unary not %observed\n"
                     "    %secondNot = ctjs.unary not %firstNot\n"
                     "    %thirdNot = ctjs.unary not %secondNot\n"
                     "    %guard = ctjs.truthy %thirdNot\n"),
            true, "three nested not operations retain inverse guard polarity");
    variant(replaced(inverted, "%hasMethod(%state, %one)", "%hasMethod(%state, %two)"), false,
            "an inverted guard for another key cannot authorize the object read");
    variant(replaced(inverted, negate, "    %lateClear = ctjs.call %clearer(%state)\n" + negate),
            false, "an inverted saved has cannot survive a later clear");
    variant(
        replaced(inverted, leafRead, "      %lateClear = ctjs.call %clearer(%state)\n" + leafRead),
        false, "a mutation in the proved arm revokes its acquired membership");

    // CFG-to-SCF places Data's remaining effects in the opposite arm of
    // if (!has) return. Both structural paths retain one common frame exit.
    const std::string continuation = "    scf.if %guard {\n"
                                     "      scf.yield\n    } else {\n" +
                                     leafRead + "      scf.yield\n    }\n" + exit +
                                     "    ctjs.return %one\n  }\n}\n";
    const auto continued =
        enterFrame(replaced(inverted, selection(false), continuation), "put$4", "frame");
    variant(continued, true, "an inverted early-return guard proves its structural continuation");
    variant(replaced(continued, negate, "    %guard = ctjs.truthy %observed\n"), false,
            "the opposite structural continuation cannot borrow a returned arm's membership");
    variant(replaced(continued, "    scf.if %guard {\n",
                     "    scf.if %guard {\n      ctjs.frame_exit %frame\n"),
            false, "a frame exit in only one arm cannot authorize the common continuation");

    const std::string selectedKey = "    %keyFlag = ctjs.truthy %choice\n"
                                    "    %selectedKey = scf.if %keyFlag -> (!ctjs.value) {\n"
                                    "      scf.yield %stringOne : !ctjs.value\n"
                                    "    } else {\n      scf.yield %false : !ctjs.value\n    }\n"
                                    "    %inverted = ctjs.unary not %selectedKey\n"
                                    "    %guard = ctjs.truthy %inverted\n";
    const std::string filteredRead =
        "      %otherKey = ctjs.call %setter(%state, %selectedKey, %one)\n" +
        replaced(readYield, "%state, %one", "%state, %stringOne");
    const std::string filteredBody = "    %selected = scf.if %guard -> (!ctjs.value) {\n" +
                                     filteredRead + "    } else {\n" + scalarYield +
                                     "    }\n    ctjs.return %selected\n  }\n}\n";
    const auto filtered =
        replaced(fixture, clear + seed + size + read,
                 clear + replaced(seed, "%one", "%stringOne") + selectedKey + filteredBody);
    variant(filtered, true,
            "an inverted scalar guard distinguishes the falsy Boolean key from a String key");
    variant(
        replaced(filtered, "%guard = ctjs.truthy %inverted", "%guard = ctjs.truthy %selectedKey"),
        false, "the truthy String key may overwrite the independently retained object");
    check(rows == 57, "all polarity, structural continuation and scalar filtering controls ran");

    for (const auto & [text, label] :
         {std::pair{inverted, "inverted"}, {continued, "continued"}, {filtered, "filtered"}}) {
        auto guarded = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(guarded), "source/prepared guard budget fixture parses");
        if (!guarded) { continue; }
        const auto contract = requested(*guarded);
        OwnedGlobalRoots complete(*guarded, contract);
        const unsigned completion = complete.steps();
        check(complete.proved() && completion < 20000,
              "guard ownership has a complete bounded proof");
        if (!complete.proved() || completion >= 20000) { continue; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            OwnedGlobalRoots limited(*guarded, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      empty(*guarded, limited),
                  "every incomplete guard budget withholds all ownership records");
        }
        OwnedGlobalRoots exact(*guarded, contract, completion);
        check(exact.proved() && exact.steps() == completion,
              "the exact guard budget reproduces its complete current proof");
        auto function = guarded->lookupSymbol<ctjs::FuncOp>("put$4");
        ctjs::UnaryOp negation;
        ctjs::ConstantOp wrong;
        function.walk([&](ctjs::UnaryOp op) { negation = op; });
        function.walk([&](ctjs::ConstantOp op) {
            if (auto boolean = llvm::dyn_cast<ctjs::BooleanAttr>(op.getValue());
                boolean && !boolean.getValue()) {
                wrong = op;
            }
        });
        check(negation && wrong, "guard mutation retains its source negation and unrelated false");
        if (!negation || !wrong) { continue; }
        const auto operand = negation.getOperand();
        negation->setAttr("ctnative.map_present", mlir::UnitAttr::get(&context));
        negation->setOperand(0, wrong.getResult());
        OwnedGlobalRoots stale(*guarded, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*guarded, stale),
              "a changed guard operand invalidates the original source fingerprint");
        OwnedGlobalRoots fresh(*guarded, requested(*guarded));
        check(!fresh.proved() && !fresh.exhausted() && empty(*guarded, fresh),
              "a fresh fingerprint and forged membership report cannot replace the live guard");
        negation->setOperand(0, operand);
        negation->removeAttr("ctnative.map_present");
        check(OwnedGlobalRoots(*guarded, contract).proved(),
              "restoring the original guard restores its independent ownership proof");
        std::printf("guard owner %s %s: all %u incomplete budgets and live mutation checked\n",
                    prepared ? "prepared" : "source", label, completion);
    }

    auto foreign = enterFrame(straight, "get$3", "otherFrame");
    foreign = replaced(foreign, "    ctjs.return %size",
                       "    ctjs.frame_exit %otherFrame\n    ctjs.return %size");
    auto module = mlir::parseSourceString<mlir::ModuleOp>(foreign, &context);
    check(static_cast<bool>(module), "foreign-frame live-edit control parses");
    if (!module) { return; }
    const auto contract = requested(*module);
    check(OwnedGlobalRoots(*module, contract).proved(),
          "each source method independently owns and exits its original frame");
    auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
    auto getter = module->lookupSymbol<ctjs::FuncOp>("get$3");
    ctjs::FrameEnterOp original, other;
    ctjs::FrameExitOp leaving;
    ctjs::RootOp rooted;
    setter.walk([&](ctjs::FrameEnterOp op) { original = op; });
    getter.walk([&](ctjs::FrameEnterOp op) { other = op; });
    setter.walk([&](ctjs::FrameExitOp op) { leaving = op; });
    setter.walk([&](ctjs::RootOp op) { rooted = op; });
    check(original && other && leaving && rooted,
          "foreign-frame control keeps both entries and the original root and exit");
    if (!original || !other || !leaving || !rooted) { return; }
    mlir::Builder attributes(&context);
    leaving->setAttr("ctnative.host_frame_proved", attributes.getUnitAttr());
    for (mlir::Operation * changed : {leaving.getOperation(), rooted.getOperation()}) {
        changed->setOperand(0, other.getContext());
        OwnedGlobalRoots stale(*module, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*module, stale),
              "a foreign frame operand invalidates the original source fingerprint");
        OwnedGlobalRoots fresh(*module, requested(*module));
        check(!fresh.proved() && !fresh.exhausted() && empty(*module, fresh),
              "fresh fingerprints and forged frame reports cannot authorize a foreign frame");
        changed->setOperand(0, original.getContext());
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring the original frame operand restores its independent proof");
    }
    const auto count = original->getAttr("reg_count");
    original->removeAttr("reg_count");
    OwnedGlobalRoots malformed(*module, requested(*module));
    check(!malformed.proved() && !malformed.exhausted() && empty(*module, malformed),
          "a malformed live frame entry cannot inherit its earlier frame proof");
    original->setAttr("reg_count", count);
    check(OwnedGlobalRoots(*module, contract).proved(),
          "restoring the entry register count restores the source proof");
    const auto type = original->getResult(0).getType();
    original->getResult(0).setType(ctjs::ValueType::get(&context));
    OwnedGlobalRoots wrongType(*module, requested(*module));
    check(!wrongType.proved() && !wrongType.exhausted() && empty(*module, wrongType),
          "a live frame result with the wrong type cannot acquire frame authority");
    original->getResult(0).setType(type);
    check(OwnedGlobalRoots(*module, contract).proved(),
          "restoring the original context type restores the source proof");
    const auto rootedValue = rooted->getOperand(1);
    rooted->setOperand(1, original.getContext());
    OwnedGlobalRoots rootedFrame(*module, requested(*module));
    check(!rootedFrame.proved() && !rootedFrame.exhausted() && empty(*module, rootedFrame),
          "a frame token cannot masquerade as a rooted JavaScript value");
    rooted->setOperand(1, rootedValue);
    check(OwnedGlobalRoots(*module, contract).proved(),
          "restoring the rooted value restores the frame proof");
}

} // namespace ctcompile::test::owned_global_shared_map
