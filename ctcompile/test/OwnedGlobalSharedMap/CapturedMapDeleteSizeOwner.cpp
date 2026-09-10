#include "Tests.h"

namespace ctcompile::test::owned_global_shared_map {

void checkCapturedMapDeleteSizeOwner(mlir::MLIRContext & context, const std::string & source,
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
    const std::string setup =
        "    %clearKey = ctjs.constant #ctjs.string<\"clear\">\n"
        "    %clearer = ctjs.get_property %state[%clearKey]\n"
        "    %deleteKey = ctjs.constant #ctjs.string<\"delete\">\n"
        "    %eraser = ctjs.get_property %state[%deleteKey]\n"
        "    %getKey = ctjs.constant #ctjs.string<\"get\">\n"
        "    %reader = ctjs.get_property %state[%getKey]\n"
        "    %zero = ctjs.constant #ctjs.number<0>\n"
        "    %negativeZero = ctjs.constant #ctjs.number<9223372036854775808>\n"
        "    %one = ctjs.constant #ctjs.number<4607182418800017408>\n"
        "    %anotherOne = ctjs.constant #ctjs.number<4607182418800017408>\n"
        "    %two = ctjs.constant #ctjs.number<4611686018427387904>\n"
        "    %three = ctjs.constant #ctjs.number<4613937818241073152>\n"
        "    %nan = ctjs.constant #ctjs.number<9221120237041090560>\n"
        "    %anotherNan = ctjs.constant #ctjs.number<9221120237041090561>\n"
        "    %false = ctjs.constant #ctjs.boolean<false>\n"
        "    %stringOne = ctjs.constant #ctjs.string<\"1\">\n"
        "    %fieldKey = ctjs.constant #ctjs.string<\"value\">\n"
        "    ctjs.set_property %value[%fieldKey], %one\n";
    const std::string clear = "    %cleared = ctjs.call %clearer(%state)\n";
    const std::string seed = "    %seeded = ctjs.call %setter(%state, %one, %value)\n";
    const std::string extra = "    %extra = ctjs.call %setter(%state, %two, %value)\n";
    const std::string erase = "    %erased = ctjs.call %eraser(%state, %one) {erase}\n";
    const std::string size = "    %saved = ctjs.get_property %state[%sizeKey] {snapshot}\n";
    const std::string reset = "    %reset = ctjs.call %clearer(%state)\n";
    const std::string store = "    %stored = ctjs.call %setter(%state, %zero, %value)\n";
    const std::string read = "    %loaded = ctjs.call %reader(%state, %saved)\n"
                             "    %answer = ctjs.get_property %loaded[%fieldKey]\n"
                             "    ctjs.return %answer\n  }\n}\n";
    const auto program = [&](const std::string & body, const char * key = "%zero") {
        return replaced(fixture,
                        "    %size = ctjs.get_property %state[%sizeKey]\n"
                        "    ctjs.return %size\n  }\n}\n",
                        setup + body + reset + replaced(store, "%zero", key) + read);
    };
    const auto last = program(clear + seed + erase + size);
    const auto remaining =
        program(clear + seed + extra + replaced(erase, "%one", "%two") + size, "%one");
    const std::string both = "    %flag = ctjs.truthy %choice\n    scf.if %flag {\n" + erase +
                             "      scf.yield\n    } else {\n"
                             "      %right = ctjs.call %eraser(%state, %anotherOne)\n"
                             "      %again = ctjs.call %eraser(%state, %one)\n"
                             "      scf.yield\n    }\n";
    const auto census = [&](mlir::ModuleOp module, const OwnedGlobalRoots & query) {
        check(query.roots().size() == 1 && query.roots().front().methodTable &&
                  query.roots().front().methodTable->capturedMap,
              "delete cardinality retains a complete callable table and its owning Map");
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
        bool complete = table.methods.size() == 2 && table.calls.size() == 4 && fields.size() == 1;
        for (const auto & edge : table.calls) {
            complete &= edge.capturedMap && edge.capturedMap->calls == calls &&
                        edge.capturedMap->reads == reads && edge.capturedMap->leafReads == fields &&
                        edge.capturedMap->allocation == table.capturedMap->allocation;
            if (edge.function == setter) { complete &= edge.arguments.size() == 3; }
        }
        check(complete, "delete cardinality preserves every call and the actual own-field read");
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & text, bool expected, const char * message) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "source/prepared delete cardinality fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        check(query.proved() == expected && !query.exhausted() &&
                  (expected || empty(*module, query)),
              message);
        if (query.proved() != expected || query.exhausted()) {
            std::fprintf(stderr, "delete-size owner %s row %u: %s\n",
                         prepared ? "prepared" : "source", rows, query.reason().str().c_str());
        }
        if (expected && query.proved()) { census(*module, query); }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "delete cardinality leaves all executable source and read positions intact");
    };
    variant(last, true, "deleting the last proved literal key establishes a saved zero");
    variant(remaining, true, "deleting one of two distinct keys establishes a saved one");
    variant(program(clear + seed + replaced(erase, "%one", "%three") + size, "%one"), true,
            "deleting a proved absent key leaves the exact cardinality unchanged");
    variant(program(clear + seed + erase + replaced(erase, "%erased", "%again") + size), true,
            "repeated deletion of an absent key cannot decrement cardinality below zero");
    variant(program(clear + seed + extra + erase +
                    replaced(replaced(erase, "%erased", "%again"), "%one", "%two") + size),
            true, "deleting both distinct keys proves zero only after both operations");
    variant(program(clear + seed + size + erase, "%one"), true,
            "an immutable saved one survives deletion of its former only key");
    variant(program(clear + seed + extra + erase + size +
                        replaced(replaced(erase, "%erased", "%again"), "%one", "%two"),
                    "%one"),
            true, "a saved one between deletions retains its actual read-time value");
    variant(program(clear + seed + size + erase), false,
            "the later deletion cannot turn a pre-delete saved one into zero");
    variant(program(clear + seed + erase + size, "%one"), false,
            "the post-delete saved zero cannot inherit the pre-delete one");
    variant(program(seed + erase + size), false,
            "deleting a local key cannot exclude older unknown invocation contents");
    variant(replaced(last, size,
                     "    %evaluated = ctjs.get_property %state[%sizeKey]\n"
                     "    %saved = ctjs.constant #ctjs.number<0>\n"),
            true, "the independent literal-zero repair retains the evaluated size read");
    variant(program(clear + seed + replaced(erase, "%one", "%anotherOne") + size), true,
            "different equal literal SSA constants remove the same key");
    for (const auto & [left, right] :
         {std::pair{"%zero", "%negativeZero"}, {"%nan", "%anotherNan"}}) {
        variant(
            program(clear + replaced(seed, "%one", left) + replaced(erase, "%one", right) + size),
            true, "SameValueZero deletes signed zero and all NaN encodings as equal keys");
    }
    for (const auto & [left, right] : {std::pair{"%zero", "%false"}, {"%one", "%stringOne"}}) {
        variant(program(clear + replaced(seed, "%one", left) + replaced(extra, "%two", right) +
                            replaced(erase, "%one", left) + size,
                        "%one"),
                true,
                "coercively equal values in different primitive categories remain separate keys");
    }
    const std::string alias = "    %aliasEraser = ctjs.get_property %seeded[%deleteKey]\n"
                              "    %erased = ctjs.call %aliasEraser(%seeded, %one) {erase}\n";
    variant(program(clear + seed + alias + size), true,
            "an exact fluent set alias deletes from the same runtime Map");
    variant(program(clear + seed + replaced(alias, "%aliasEraser(%seeded", "%aliasEraser(%state") +
                    size),
            false, "method lookup aliases cannot authorize a different call receiver");
    variant(replaced(last, "%eraser(%state, %one)", "%eraser(%state, %one, %two)"), false,
            "extra delete actuals remain outside the proved standard method contract");
    variant(replaced(last, size, "    %effect = ctjs.call %this(%this)\n" + size), false,
            "unknown effects cannot preserve mutable cardinality evidence");
    const std::string partial = "    %flag = ctjs.truthy %choice\n    scf.if %flag {\n" + erase +
                                "      scf.yield\n    }\n";
    for (const char * flag : {"true", "false"}) {
        const auto choice = [&](const std::string & text) {
            return replaced(text, "#ctjs.boolean<true>",
                            std::string("#ctjs.boolean<") + flag + ">");
        };
        variant(choice(program(clear + seed + both + size)), true,
                "both nonidentical structural arms independently delete the last key");
        variant(choice(program(clear + seed + partial + size)), false,
                "a startup flag cannot eliminate a future arm that keeps the key");
    }
    const std::string unknown = "    %leftKey = ctjs.call %setter(%state, %entryKey, %value)\n"
                                "    %rightKey = ctjs.call %setter(%state, %aliasKey, %value)\n"
                                "    %deleted = ctjs.call %eraser(%state, %entryKey)\n";
    variant(program(clear + unknown + size), false,
            "deleting one formal key retains possibly equal other keys in the upper census");
    variant(program(clear + unknown + size, "%one"), false,
            "possibly equal formal keys cannot establish a remaining lower bound of one");
    variant(program(clear + unknown + size + "    %effect = ctjs.call %this(%this)\n"), false,
            "an immutable size fact does not authorize unrelated unknown calls");
    const std::string selected =
        "    %flag = ctjs.truthy %choice\n"
        "    %saved = scf.if %flag -> (!ctjs.value) {\n"
        "      %left = ctjs.get_property %state[%sizeKey]\n"
        "      scf.yield %left : !ctjs.value\n"
        "    } else {\n      scf.yield %negativeZero : !ctjs.value\n    }\n";
    variant(program(clear + seed + erase + selected), true,
            "selected post-delete zero and literal negative zero have the same exact value");
    variant(program(clear + seed + erase +
                    replaced(selected, "scf.yield %negativeZero", "scf.yield %one")),
            false, "one nonzero selection arm cannot inherit the other arm's post-delete zero");
    for (unsigned writes : {64u, 65u}) {
        std::string repeated = seed;
        for (unsigned index = 1; index < writes; ++index) {
            repeated += "    %repeat" + std::to_string(index) +
                        " = ctjs.call %setter(%state, %anotherOne, %value)\n";
        }
        variant(program(clear + repeated + erase + size), writes == 64,
                writes == 64 ? "deleting all sixty-four equal candidates proves exact zero"
                             : "deletion cannot revive a census that exceeded its complete limit");
    }
    check(rows == 31, "all independent delete-size owner source controls ran");

    const unsigned deletionRows = rows;
    const auto branch = [](const std::string & left, const std::string & right) {
        return "    %flag = ctjs.truthy %choice\n    scf.if %flag {\n" + left +
               "      scf.yield\n    } else {\n" + right + "      scf.yield\n    }\n";
    };
    const std::string differentDeletes =
        branch("      %leftDelete = ctjs.call %eraser(%state, %one)\n",
               "      %rightDelete = ctjs.call %eraser(%state, %two) {join_erase}\n");
    const std::string differentSets =
        branch("      %leftSet = ctjs.call %setter(%state, %one, %value)\n",
               "      %rightSet = ctjs.call %setter(%state, %three, %value)\n");
    const std::string joinBody = clear + seed + extra + differentDeletes;
    const auto joinedOne = program(joinBody + size, "%one");
    for (const char * flag : {"true", "false"}) {
        const auto choice = [&](const std::string & text) {
            return replaced(text, "#ctjs.boolean<true>",
                            std::string("#ctjs.boolean<") + flag + ">");
        };
        variant(choice(joinedOne), true,
                "different surviving branch keys independently establish exact size one");
        variant(choice(program(clear + differentSets + size, "%one")), true,
                "different singleton setters preserve exact size without common membership");
        variant(choice(program(clear + seed + extra +
                                   replaced(differentDeletes,
                                            "      %rightDelete = ctjs.call %eraser(%state, %two) "
                                            "{join_erase}\n",
                                            "") +
                                   size,
                               "%one")),
                false, "a startup arm cannot erase an unequal future cardinality");
        variant(choice(program(seed + extra + differentDeletes + size, "%one")), false,
                "equal tracked branch entries cannot exclude unknown incoming Map contents");
    }
    variant(program(joinBody + size), false,
            "an empty definite-key intersection does not make the joined Map empty");
    variant(replaced(joinedOne, reset + replaced(store, "%zero", "%one"), ""), false,
            "exact joined size cannot fabricate a common member at the saved numeric key");
    variant(program(clear + seed + extra + size + differentDeletes, "%two"), true,
            "a pre-join saved two retains its read-time value after either deletion");
    variant(program(clear + seed + extra + size + differentDeletes, "%one"), false,
            "a pre-join snapshot cannot borrow the later exact one");
    const std::string grow = "    %grew = ctjs.call %setter(%state, %three, %value)\n";
    variant(program(joinBody + size + grow, "%one"), true,
            "an immutable joined size survives later insertion and table reset");
    variant(program(joinBody + grow + size, "%one"), false,
            "a new size read after growth cannot reuse the earlier joined cardinality");
    const std::string uncertainDelete =
        "    %aliasEraser = ctjs.get_property %seeded[%deleteKey]\n"
        "    %aliasDelete = ctjs.call %aliasEraser(%seeded, %one)\n";
    variant(program(joinBody + uncertainDelete + size, "%one"), false,
            "an alias deletion invalidates a mutable joined size on the same runtime Map");
    variant(program(joinBody + size + uncertainDelete, "%one"), true,
            "an alias deletion does not alter an already saved joined scalar");
    variant(program(joinBody + replaced(seed, "%seeded", "%uncertainSet") + size, "%one"), false,
            "a post-join write at a possibly present key invalidates exact size");
    variant(program(joinBody + "    %effect = ctjs.call %this(%this)\n" + size, "%one"), false,
            "an unknown effect cannot preserve joined mutable cardinality");
    const std::string inner = "      %innerFlag = ctjs.truthy %choice\n      scf.if %innerFlag {\n"
                              "        %innerLeft = ctjs.call %setter(%state, %two, %value)\n"
                              "        scf.yield\n      } else {\n"
                              "        %innerRight = ctjs.call %setter(%state, %three, %value)\n"
                              "        scf.yield\n      }\n";
    variant(
        program(clear +
                    branch("      %outerLeft = ctjs.call %setter(%state, %one, %value)\n", inner) +
                    size,
                "%one"),
        true, "nested structural joins independently retain the same exact one");
    variant(program(clear +
                        branch("      %outerLeft = ctjs.call %setter(%state, %one, %value)\n",
                               replaced(inner,
                                        "        %innerRight = ctjs.call %setter(%state, "
                                        "%three, %value)\n",
                                        "")) +
                        size,
                    "%one"),
            false, "an empty inner arm prevents an exact outer cardinality");
    for (unsigned writes : {64u, 65u}) {
        std::string left;
        std::string right;
        for (unsigned index = 0; index < writes; ++index) {
            const auto suffix = std::to_string(index);
            left += "      %left" + suffix + " = ctjs.call %setter(%state, %one, %value)\n";
            if (index < 64) {
                right += "      %right" + suffix + " = ctjs.call %setter(%state, %three, %value)\n";
            }
        }
        variant(program(clear + branch(left, right) + size, "%one"), writes == 64,
                writes == 64
                    ? "two independently bounded sixty-four-candidate arms retain exact size"
                    : "one arm over its complete census budget cannot borrow its sibling's size");
    }
    const unsigned joinRows = rows - deletionRows;
    check(joinRows == 22, "all independent exact-join owner controls ran");
    rows = deletionRows;

    const std::string absentDelete =
        "    %absentDelete = ctjs.call %eraser(%state, %three) {mutation}\n";
    const std::string inserted =
        "    %inserted = ctjs.call %setter(%state, %three, %value) {mutation}\n";
    const std::string overwrite =
        "    %overwrite = ctjs.call %setter(%state, %anotherOne, %value)\n";
    const std::string common =
        clear + seed +
        branch("      %leftSet = ctjs.call %setter(%state, %two, %value)\n",
               "      %rightSet = ctjs.call %setter(%state, %three, %value)\n");
    for (const char * flag : {"true", "false"}) {
        const auto choice = [&](const std::string & text) {
            return replaced(text, "#ctjs.boolean<true>",
                            std::string("#ctjs.boolean<") + flag + ">");
        };
        variant(choice(program(joinBody + inserted + size, "%two")), true,
                "a definitely absent insertion increases either joined cardinality to two");
        variant(choice(program(joinBody + absentDelete + size, "%one")), true,
                "deleting a definitely absent key preserves either joined cardinality");
        variant(choice(program(common + overwrite + size, "%two")), true,
                "overwriting a common present key preserves the independently joined size");
        variant(choice(program(common + erase + size, "%one")), true,
                "deleting a common present key decrements the independently joined size");
    }
    variant(program(joinBody + inserted + absentDelete + size, "%one"), true,
            "inserting then deleting a new key restores the joined cardinality");
    variant(program(joinBody + absentDelete +
                        replaced(absentDelete, "%absentDelete", "%againAbsent") + size,
                    "%one"),
            true, "repeated known-absent deletion never decrements the joined cardinality");
    variant(program(joinBody + inserted + replaced(inserted, "%inserted", "%overwritten") + size,
                    "%two"),
            true, "a proved overwrite of the new common member cannot count as another insertion");
    variant(program(joinBody + size + inserted, "%one"), true,
            "a saved pre-insertion joined size retains its original one");
    variant(program(joinBody + inserted + size + absentDelete, "%two"), true,
            "a saved post-insertion size retains two through a later known deletion");
    variant(program(joinBody + size + inserted, "%two"), false,
            "later insertion cannot retroactively increase a saved joined scalar");
    variant(program(joinBody + inserted + size + absentDelete, "%one"), false,
            "later deletion cannot retroactively decrease a saved joined scalar");
    variant(program(joinBody + replaced(inserted, "%three", "%one") + size, "%two"), false,
            "a possibly present insertion cannot choose the increasing branch effect");
    variant(program(joinBody + replaced(absentDelete, "%three", "%one") + size), false,
            "a possibly present deletion cannot choose the decreasing branch effect");
    variant(program(common + replaced(erase, "%one", "%entryKey") + size, "%two"), true,
            "a String formal is independently disjoint from every numeric candidate");
    variant(program(clear +
                        branch(replaced(seed, "%one", "%entryKey"),
                               replaced(extra, "%two", "%aliasKey")) +
                        replaced(inserted, "%three", "%entryKey") + size,
                    "%one"),
            false, "unrelated same-category formals cannot prove a joined overwrite");
    const std::string fluent =
        "    %aliasSetter = ctjs.get_property %seeded[%setKey]\n"
        "    %inserted = ctjs.call %aliasSetter(%seeded, %three, %value) {mutation}\n";
    variant(program(joinBody + fluent + size, "%two"), true,
            "the actual fluent Map alias receives the independently proved insertion effect");
    variant(
        program(joinBody + replaced(fluent, "%aliasSetter(%seeded", "%aliasSetter(%state") + size,
                "%two"),
        false, "a fluent method lookup does not authorize a mismatched receiver");
    variant(program(joinBody + inserted + "    %effect = ctjs.call %this(%this)\n" + size, "%two"),
            false, "an unknown effect cannot preserve newly updated mutable cardinality");
    for (unsigned candidates : {64u, 65u}) {
        std::string mutations = inserted;
        for (unsigned index = 3; index < candidates; ++index) {
            mutations += "    %overwrite" + std::to_string(index) +
                         " = ctjs.call %setter(%state, %three, %value)\n";
        }
        variant(program(joinBody + mutations + size, "%two"), candidates == 64,
                candidates == 64
                    ? "known mutations preserve cardinality within the complete candidate limit"
                    : "known overwrites cannot extend cardinality past the candidate limit");
    }
    for (unsigned candidates : {31u, 33u}) {
        std::string left;
        std::string right;
        for (unsigned index = 0; index < candidates; ++index) {
            left += "      %left" + std::to_string(index) +
                    " = ctjs.call %setter(%state, %one, %value)\n";
            right += "      %right" + std::to_string(index) +
                     " = ctjs.call %setter(%state, %two, %value)\n";
        }
        variant(program(clear + branch(left, right) + absentDelete + size, "%one"),
                candidates == 31,
                candidates == 31
                    ? "a bounded branch union proves the later deleted key absent"
                    : "a discarded branch union cannot authorize a supposedly absent deletion");
    }
    const unsigned mutationRows = rows - deletionRows;
    check(mutationRows == 26, "all after-join known-mutation owner controls ran");
    rows = deletionRows;

    for (const auto & [body, expectedKey] : {std::pair{joinBody + inserted + size, "%two"},
                                             {joinBody + absentDelete + size, "%one"}}) {
        auto mutated =
            mlir::parseSourceString<mlir::ModuleOp>(program(body, expectedKey), &context);
        check(static_cast<bool>(mutated), "joined mutation live-control fixture parses");
        if (!mutated) { continue; }
        auto contract = requested(*mutated);
        OwnedGlobalRoots complete(*mutated, contract);
        const unsigned completion = complete.steps();
        check(complete.proved() && completion < 30000,
              "the known-mutation owner proof has a bounded completion");
        if (!complete.proved() || completion < 2 || completion >= 30000) { continue; }
        std::vector<unsigned> budgets{0, 1, completion / 2, completion - 1};
        for (unsigned budget = 2; budget < completion; budget *= 2) { budgets.push_back(budget); }
        llvm::sort(budgets);
        budgets.erase(std::unique(budgets.begin(), budgets.end()), budgets.end());
        for (unsigned budget : budgets) {
            OwnedGlobalRoots limited(*mutated, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      empty(*mutated, limited),
                  "incomplete mutation proof work publishes no partial ownership");
        }
        check(OwnedGlobalRoots(*mutated, contract, completion).proved(),
              "the exact known-mutation budget reproduces complete ownership");
        auto setter = mutated->lookupSymbol<ctjs::FuncOp>("put$4");
        ctjs::CallOp mutation;
        ctjs::ConstantOp one;
        setter.walk([&](ctjs::CallOp op) {
            if (op->hasAttr("mutation")) { mutation = op; }
        });
        setter.walk([&](ctjs::ConstantOp op) {
            const auto number = llvm::dyn_cast<ctjs::NumberAttr>(op.getValue());
            if (number && number.getBits() == 4607182418800017408ULL) { one = op; }
        });
        check(mutation && one, "known mutation retains its actual key and receiver operands");
        if (!mutation || !one) { continue; }
        mlir::Builder attrs(&context);
        mutated->walk([&](mlir::Operation * op) {
            op->setAttr("ctnative.map_exact_size", attrs.getI32IntegerAttr(2));
            op->setAttr("ctnative.map_present", attrs.getUnitAttr());
        });
        contract = requested(*mutated);
        const auto refuse = [&] {
            OwnedGlobalRoots stale(*mutated, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      empty(*mutated, stale),
                  "live known-mutation edits invalidate their original fingerprint");
            OwnedGlobalRoots fresh(*mutated, requested(*mutated));
            check(!fresh.proved() && !fresh.exhausted() && empty(*mutated, fresh),
                  "fresh fingerprints and forged reports cannot prove changed key relations");
        };
        const auto key = mutation.getArgs().front();
        mutation->setOperand(2, one.getResult());
        refuse();
        mutation->setOperand(2, key);
        check(OwnedGlobalRoots(*mutated, contract).proved(),
              "restoring the independently absent key restores the mutation proof");
        const auto receiver = mutation.getReceiver();
        mutation->setOperand(1, setter.getBody().front().getArgument(0));
        refuse();
        mutation->setOperand(1, receiver);
        check(OwnedGlobalRoots(*mutated, contract).proved(),
              "restoring the actual runtime Map restores known mutation cardinality");
        std::printf("known-mutation owner %s %s: %u rows, four live edits, %zu cutoffs / %u work\n",
                    prepared ? "prepared" : "source", expectedKey, mutationRows, budgets.size(),
                    completion);
    }

    auto joinedModule = mlir::parseSourceString<mlir::ModuleOp>(joinedOne, &context);
    check(static_cast<bool>(joinedModule), "joined size budget and live-edit fixture parses");
    if (joinedModule) {
        auto joinedContract = requested(*joinedModule);
        OwnedGlobalRoots complete(*joinedModule, joinedContract);
        const unsigned completion = complete.steps();
        check(complete.proved() && completion < 30000, "the joined cardinality proof is bounded");
        if (complete.proved() && completion > 1 && completion < 30000) {
            std::vector<unsigned> budgets{0, 1, completion / 2, completion - 1};
            for (unsigned budget = 2; budget < completion; budget *= 2) {
                budgets.push_back(budget);
            }
            llvm::sort(budgets);
            budgets.erase(std::unique(budgets.begin(), budgets.end()), budgets.end());
            for (unsigned budget : budgets) {
                OwnedGlobalRoots limited(*joinedModule, joinedContract, budget);
                check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                          empty(*joinedModule, limited),
                      "incomplete join work cannot publish a partial ownership or size proof");
            }
            OwnedGlobalRoots exact(*joinedModule, joinedContract, completion);
            check(exact.proved() && exact.steps() == completion,
                  "the exact charged join budget reproduces complete ownership");
            ctjs::GetPropertyOp snapshot;
            ctjs::CallOp rightDeletion;
            ctjs::ConstantOp wrong;
            mlir::scf::IfOp joined;
            auto setter = joinedModule->lookupSymbol<ctjs::FuncOp>("put$4");
            setter.walk([&](ctjs::GetPropertyOp op) {
                if (op->hasAttr("snapshot")) { snapshot = op; }
            });
            setter.walk([&](ctjs::CallOp op) {
                if (op->hasAttr("join_erase")) { rightDeletion = op; }
            });
            setter.walk([&](mlir::scf::IfOp op) { joined = op; });
            setter.walk([&](ctjs::ConstantOp op) {
                auto number = llvm::dyn_cast<ctjs::NumberAttr>(op.getValue());
                if (number && number.getBits() == 4613937818241073152ULL) { wrong = op; }
            });
            check(snapshot && rightDeletion && joined && wrong,
                  "join mutations keep their actual branch, snapshot and deletion");
            if (snapshot && rightDeletion && joined && wrong) {
                mlir::Builder attrs(&context);
                joinedModule->walk([&](mlir::Operation * op) {
                    op->setAttr("ctnative.map_exact_size", attrs.getI32IntegerAttr(1));
                    op->setAttr("ctnative.map_present", attrs.getUnitAttr());
                });
                joinedContract = requested(*joinedModule);
                const auto refused = [&] {
                    OwnedGlobalRoots stale(*joinedModule, joinedContract);
                    check(!stale.proved() && stale.reason().contains("fingerprint") &&
                              empty(*joinedModule, stale),
                          "a live join edit invalidates the former ownership fingerprint");
                    OwnedGlobalRoots fresh(*joinedModule, requested(*joinedModule));
                    check(!fresh.proved() && !fresh.exhausted() && empty(*joinedModule, fresh),
                          "fresh fingerprints and forged size reports cannot hide unequal arms");
                };
                snapshot->moveBefore(joined);
                refused();
                snapshot->moveAfter(joined);
                check(OwnedGlobalRoots(*joinedModule, joinedContract).proved(),
                      "restoring the actual post-join read recovers exact one");
                const auto key = rightDeletion.getArgs().front();
                rightDeletion->setOperand(2, wrong.getResult());
                refused();
                rightDeletion->setOperand(2, key);
                check(OwnedGlobalRoots(*joinedModule, joinedContract).proved(),
                      "restoring both equal-cardinality arms recovers the proof");
                const auto receiver = rightDeletion.getReceiver();
                rightDeletion->setOperand(1, setter.getBody().front().getArgument(0));
                refused();
                rightDeletion->setOperand(1, receiver);
                check(OwnedGlobalRoots(*joinedModule, joinedContract).proved(),
                      "restoring the same runtime Map on both arms recovers the proof");
            }
            std::printf("joined-size owner %s: %u rows, six live edits, %zu cutoffs / %u work\n",
                        prepared ? "prepared" : "source", joinRows, budgets.size(), completion);
        }
    }

    for (const auto & [text, label] : {std::pair{last, "last key"}, {remaining, "one of two"}}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "delete-size live mutation fixture parses");
        if (!module) { continue; }
        auto contract = requested(*module);
        OwnedGlobalRoots complete(*module, contract);
        const unsigned completion = complete.steps();
        check(complete.proved() && completion < 20000, "delete-size complete proof is bounded");
        if (!complete.proved() || completion >= 20000) { continue; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            OwnedGlobalRoots limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      empty(*module, limited),
                  "every incomplete delete-size budget withholds the complete owner proof");
        }
        OwnedGlobalRoots exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion,
              "the exact charged deletion budget reproduces complete ownership");
        ctjs::GetPropertyOp snapshot;
        ctjs::CallOp deletion;
        ctjs::ConstantOp wrong;
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        setter.walk([&](ctjs::GetPropertyOp op) {
            if (op->hasAttr("snapshot")) { snapshot = op; }
        });
        setter.walk([&](ctjs::CallOp op) {
            if (op->hasAttr("erase")) { deletion = op; }
        });
        setter.walk([&](ctjs::ConstantOp op) {
            auto number = llvm::dyn_cast<ctjs::NumberAttr>(op.getValue());
            if (number && number.getBits() == 4613937818241073152ULL) { wrong = op; }
        });
        check(snapshot && deletion && wrong, "live deletion controls keep their marked operations");
        if (!snapshot || !deletion || !wrong) { continue; }
        mlir::Builder attrs(&context);
        module->walk([&](mlir::Operation * op) {
            op->setAttr("ctnative.map_exact_size", attrs.getI32IntegerAttr(0));
            op->setAttr("ctnative.map_present", attrs.getUnitAttr());
        });
        contract = requested(*module);
        const auto refused = [&] {
            OwnedGlobalRoots stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      empty(*module, stale),
                  "live deletion edits invalidate the original owner fingerprint");
            OwnedGlobalRoots fresh(*module, requested(*module));
            check(!fresh.proved() && !fresh.exhausted() && empty(*module, fresh),
                  "fresh fingerprints and forged reports cannot repair changed deletion evidence");
        };
        snapshot->moveBefore(deletion);
        refused();
        snapshot->moveAfter(deletion);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring the snapshot after deletion restores the current proof");
        const auto key = deletion.getArgs().front();
        deletion->setOperand(2, wrong.getResult());
        refused();
        deletion->setOperand(2, key);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring the actual deleted key restores exact cardinality");
        const auto receiver = deletion.getReceiver();
        deletion->setOperand(1, setter.getBody().front().getArgument(0));
        refused();
        deletion->setOperand(1, receiver);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring the actual runtime Map restores deletion's ownership proof");
        std::printf("delete-size owner %s %s: %u rows, six live edits and all %u incomplete "
                    "budgets checked\n",
                    prepared ? "prepared" : "source", label, rows, completion);
    }
}

} // namespace ctcompile::test::owned_global_shared_map
