#include "Tests.h"

namespace ctcompile::test::type_inference {

void checkMapDeleteSizePresence(mlir::MLIRContext & context) {
    using namespace ctcompile;
    const std::string prelude = R"mlir(
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %anotherOne = ctjs.constant #ctjs.number<4607182418800017408>
  %two = ctjs.constant #ctjs.number<4611686018427387904>
  %three = ctjs.constant #ctjs.number<4613937818241073152>
  %zero = ctjs.constant #ctjs.number<0>
  %negativeZero = ctjs.constant #ctjs.number<9223372036854775808>
  %nan = ctjs.constant #ctjs.number<9221120237041090560>
  %anotherNan = ctjs.constant #ctjs.number<9221120237041090561>
  %false = ctjs.constant #ctjs.boolean<false>
  %stringOne = ctjs.constant #ctjs.string<"1">
  %setName = ctjs.constant #ctjs.string<"set">
  %getName = ctjs.constant #ctjs.string<"get">
  %clearName = ctjs.constant #ctjs.string<"clear">
  %sizeName = ctjs.constant #ctjs.string<"size">
  %deleteName = ctjs.constant #ctjs.string<"delete">
  %constructor = ctjs.load_global "Map"
  %map = ctjs.construct %constructor(%constructor)
  %setter = ctjs.get_property %map[%setName]
  %getter = ctjs.get_property %map[%getName]
  %clearer = ctjs.get_property %map[%clearName]
  %eraser = ctjs.get_property %map[%deleteName]
)mlir";
    const std::string clear = "  %cleared = ctjs.call %clearer(%map) {mutate_clear}\n";
    const std::string seed = "  %seeded = ctjs.call %setter(%map, %one, %one) {seed}\n";
    const std::string extra = "  %extra = ctjs.call %setter(%map, %two, %one) {extra}\n";
    const std::string erase = "  %erased = ctjs.call %eraser(%map, %one) {erase}\n";
    const std::string size = "  %saved = ctjs.get_property %map[%sizeName] {snapshot}\n";
    const std::string reset = "  %reset = ctjs.call %clearer(%map) {reset}\n";
    const std::string store = "  %stored = ctjs.call %setter(%map, %saved, %one) {mutate_store}\n";
    const std::string read = "  %observed = ctjs.call %getter(%map, %zero) {check}\n";
    const auto replace = [](std::string text, llvm::StringRef from, llvm::StringRef to) {
        const auto position = text.find(from.str());
        if (position == std::string::npos) {
            std::printf("FAIL delete-size fixture replacement did not match\n");
            ++failures;
            return text;
        }
        text.replace(position, from.size(), to.str());
        return text;
    };
    const std::string both = "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + erase +
                             "  } else {\n    %right = ctjs.call %eraser(%map, %anotherOne)\n"
                             "    %again = ctjs.call %eraser(%map, %one)\n  }\n";
    const std::string selected = "  %bit = ctjs.truthy %p\n"
                                 "  %saved = scf.if %bit -> (!ctjs.value) {\n"
                                 "    %left = ctjs.get_property %map[%sizeName]\n"
                                 "    scf.yield %left : !ctjs.value\n"
                                 "  } else {\n    scf.yield %negativeZero : !ctjs.value\n  }\n";
    const auto branch = [](const std::string & left, const std::string & right) {
        return "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + left + "  } else {\n" + right +
               "  }\n";
    };
    const std::string differentDeletes =
        branch("    %leftDelete = ctjs.call %eraser(%map, %one)\n",
               "    %rightDelete = ctjs.call %eraser(%map, %two) {join_erase}\n");
    const std::string differentSets =
        branch("    %leftSet = ctjs.call %setter(%map, %one, %one)\n",
               "    %rightSet = ctjs.call %setter(%map, %three, %one)\n");
    const std::string joined = clear + seed + extra + differentDeletes;
    const std::string grow = "  %grew = ctjs.call %setter(%map, %three, %one)\n";
    const std::string aliasDelete = "  %aliasEraser = ctjs.get_property %seeded[%deleteName]\n"
                                    "  %aliasDelete = ctjs.call %aliasEraser(%seeded, %one)\n";
    const auto suffix = reset + store + read;
    const auto oneSuffix = reset + store + replace(read, "%zero", "%one");
    struct presenceRow {
        const char * what;
        std::string body;
        bool present;
        bool supported = true;
    };
    std::vector<presenceRow> rows = {
        {"deleting the last key saves exact zero", clear + seed + erase + size + suffix, true},
        {"deleting one of two keys saves exact one",
         clear + seed + extra + replace(erase, "%one", "%two") + size + oneSuffix, true},
        {"deleting a distinct absent key preserves exact one",
         clear + seed + replace(erase, "%one", "%three") + size + oneSuffix, true},
        {"repeated deletion cannot decrement an already empty Map",
         clear + seed + erase + replace(erase, "%erased", "%again") + size + suffix, true},
        {"deleting both keys establishes zero after both operations",
         clear + seed + extra + erase +
             replace(replace(erase, "%erased", "%again"), "%one", "%two") + size + suffix,
         true},
        {"a saved one survives subsequent deletion", clear + seed + size + erase + oneSuffix, true},
        {"a saved size between deletes survives the later deletion",
         clear + seed + extra + erase + size +
             replace(replace(erase, "%erased", "%again"), "%one", "%two") + oneSuffix,
         true},
        {"a later delete cannot establish an earlier zero", clear + seed + size + erase + suffix,
         false},
        {"a post-delete zero cannot inherit the old cardinality one",
         clear + seed + erase + size + oneSuffix, false},
        {"deleting a tracked key does not exclude unknown initial keys",
         seed + erase + size + suffix, false},
        {"the literal-zero repair retains the actual evaluated size",
         clear + seed + erase + replace(size, "%saved", "%evaluated") +
             "  %saved = ctjs.constant #ctjs.number<0>\n" + suffix,
         true},
        {"equal independent literal values identify the deleted key",
         clear + seed + replace(erase, "%one", "%anotherOne") + size + suffix, true},
        {"negative zero removes positive zero in SameValueZero",
         clear + replace(seed, "%one, %one", "%zero, %one") +
             replace(erase, "%one", "%negativeZero") + size + suffix,
         true},
        {"different NaN encodings identify one erased key",
         clear + replace(seed, "%one, %one", "%nan, %one") + replace(erase, "%one", "%anotherNan") +
             size + suffix,
         true},
        {"Boolean false remains after deleting Number zero",
         clear + replace(seed, "%one, %one", "%zero, %one") + replace(extra, "%two", "%false") +
             replace(erase, "%one", "%zero") + size + oneSuffix,
         true},
        {"String one remains after deleting Number one",
         clear + seed + replace(extra, "%two", "%stringOne") + erase + size + oneSuffix, true},
        {"every structural arm deletes the same last key", clear + seed + both + size + suffix,
         true},
        {"one surviving structural arm prevents exact zero",
         clear + seed + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + erase + "  }\n" + size +
             suffix,
         false},
        {"different singleton survivors independently join their exact counts",
         clear + seed + extra +
             replace(replace(both, "%anotherOne", "%two"),
                     "    %again = ctjs.call %eraser(%map, %one)\n", "") +
             size + oneSuffix,
         true},
        {"both selected arms establish zero after deletion",
         clear + seed + erase + selected + suffix, true},
        {"one selected one cannot borrow a deleted Map's zero",
         clear + seed + erase + replace(selected, "scf.yield %negativeZero", "scf.yield %one") +
             suffix,
         false},
        {"a fluent alias deletes from the actual Map instance",
         clear + seed +
             "  %aliasEraser = ctjs.get_property %seeded[%deleteName]\n"
             "  %erased = ctjs.call %aliasEraser(%seeded, %one)\n" +
             size + suffix,
         true},
        {"an alias deletion after the final store destroys current membership",
         clear + seed + erase + size + reset + store +
             "  %aliasEraser = ctjs.get_property %stored[%deleteName]\n"
             "  %erasedLater = ctjs.call %aliasEraser(%stored, %zero)\n" +
             read,
         false},
        {"a saved zero survives later growth and another deletion",
         clear + seed + erase + size + extra +
             replace(replace(erase, "%erased", "%again"), "%one", "%two") + suffix,
         true},
    };
    for (unsigned writes : {64u, 65u}) {
        std::string repeated = seed;
        for (unsigned index = 1; index < writes; ++index) {
            repeated += "  %repeat" + std::to_string(index) +
                        " = ctjs.call %setter(%map, %anotherOne, %one)\n";
        }
        rows.push_back({writes == 64 ? "deletion removes all sixty-four equal candidates"
                                     : "deletion cannot repair an overflowed complete census",
                        clear + repeated + erase + size + suffix, writes == 64});
    }
    const size_t deletionRows = rows.size();
    const std::vector<presenceRow> joinRows = {
        {"post-join size is exact despite disjoint singleton survivors", joined + size + oneSuffix,
         true},
        {"post-join size is exact despite disjoint singleton setters",
         clear + differentSets + size + oneSuffix, true},
        {"unequal survivor cardinalities cannot select the startup arm",
         clear + seed + extra +
             replace(differentDeletes,
                     "    %rightDelete = ctjs.call %eraser(%map, %two) {join_erase}\n", "") +
             size + oneSuffix,
         false},
        {"equal tracked survivor counts cannot exclude unknown incoming contents",
         seed + extra + differentDeletes + size + oneSuffix, false},
        {"an empty intersection of branch keys is not an empty Map", joined + size + suffix, false},
        {"exact size does not create common membership at the saved key",
         joined + size + replace(read, "%zero", "%saved"), false},
        {"exact size does not make either disjoint survivor definitely present",
         joined + size + replace(read, "%zero", "%two"), false},
        {"a snapshot before the join remains exact two after either deletion",
         clear + seed + extra + size + differentDeletes + reset + store +
             replace(read, "%zero", "%two"),
         true},
        {"an earlier snapshot cannot borrow the joined cardinality",
         clear + seed + extra + size + differentDeletes + oneSuffix, false},
        {"an immutable post-join snapshot survives later growth", joined + size + grow + oneSuffix,
         true},
        {"a post-growth snapshot cannot reuse the former joined size",
         joined + grow + size + oneSuffix, false},
        {"a fluent alias reads the same instance's joined size",
         joined + replace(size, "%map", "%seeded") + oneSuffix, true},
        {"alias deletion invalidates mutable joined size", joined + aliasDelete + size + oneSuffix,
         false},
        {"alias deletion preserves an earlier immutable joined snapshot",
         joined + size + aliasDelete + oneSuffix, true},
        {"a post-join write at a possibly present key invalidates exact size",
         joined + replace(seed, "%seeded", "%uncertainSet") + size + oneSuffix, false},
        {"unknown effects invalidate mutable joined cardinality",
         joined + "  %effect = ctjs.call %p(%receiver)\n" + size + oneSuffix, false, false},
        {"unknown effects keep saved-key proof conservative even after an exact snapshot",
         joined + size + "  %effect = ctjs.call %p(%receiver)\n" + oneSuffix, false, false},
        {"a clear after storing at the saved key invalidates current membership",
         joined + size + reset + store + "  %lateClear = ctjs.call %clearer(%map)\n" +
             replace(read, "%zero", "%one"),
         false},
    };
    rows.insert(rows.end(), joinRows.begin(), joinRows.end());
    const std::string inner =
        "    %innerBit = ctjs.truthy %q\n    scf.if %innerBit {\n"
        "      %innerLeft = ctjs.call %setter(%map, %two, %one)\n"
        "    } else {\n      %innerRight = ctjs.call %setter(%map, %three, %one)\n    }\n";
    rows.push_back({"nested disjoint singleton joins preserve exact one",
                    clear +
                        branch("    %outerLeft = ctjs.call %setter(%map, %one, %one)\n", inner) +
                        size + oneSuffix,
                    true});
    rows.push_back(
        {"an empty inner branch prevents an outer exact cardinality",
         clear +
             branch("    %outerLeft = ctjs.call %setter(%map, %one, %one)\n",
                    replace(inner, "      %innerRight = ctjs.call %setter(%map, %three, %one)\n",
                            "")) +
             size + oneSuffix,
         false});
    for (unsigned writes : {64u, 65u}) {
        std::string left;
        std::string right;
        for (unsigned index = 0; index < writes; ++index) {
            const auto label = std::to_string(index);
            left += "    %left" + label + " = ctjs.call %setter(%map, %one, %one)\n";
            if (index < 64) {
                right += "    %right" + label + " = ctjs.call %setter(%map, %three, %one)\n";
            }
        }
        rows.push_back(
            {writes == 64
                 ? "equal cardinality survives union-census overflow when each arm is bounded"
                 : "a single over-budget arm withholds joined exact cardinality",
             clear + branch(left, right) + size + oneSuffix, writes == 64});
    }
    const size_t joinedRows = rows.size() - deletionRows;
    const std::string inserted = "  %inserted = ctjs.call %setter(%map, %three, %one) {mutation}\n";
    const std::string absentDelete =
        "  %absentDelete = ctjs.call %eraser(%map, %three) {mutation}\n";
    const std::string common = clear + seed +
                               branch("    %leftSet = ctjs.call %setter(%map, %two, %one)\n",
                                      "    %rightSet = ctjs.call %setter(%map, %three, %one)\n");
    const auto twoSuffix = reset + store + replace(read, "%zero", "%two");
    const std::vector<presenceRow> mutationRows = {
        {"definitely absent post-join insertion increases exact cardinality",
         joined + inserted + size + twoSuffix, true},
        {"definitely absent post-join deletion preserves exact cardinality",
         joined + absentDelete + size + oneSuffix, true},
        {"overwriting a common member preserves the independently joined size",
         common + replace(seed, "%seeded", "%overwrite") + size + twoSuffix, true},
        {"deleting a common member decrements the independently joined size",
         common + erase + size + oneSuffix, true},
        {"insert then delete restores the original joined cardinality",
         joined + inserted + absentDelete + size + oneSuffix, true},
        {"repeated absent deletes cannot decrement the joined size",
         joined + absentDelete + replace(absentDelete, "%absentDelete", "%againAbsent") + size +
             oneSuffix,
         true},
        {"overwriting the newly inserted common key does not increase size again",
         joined + inserted + replace(inserted, "%inserted", "%overwrite") + size + twoSuffix, true},
        {"saved joined size remains one across a later insertion",
         joined + size + inserted + oneSuffix, true},
        {"saved post-insertion size remains two across a later deletion",
         joined + inserted + size + absentDelete + twoSuffix, true},
        {"later insertion cannot change the value of an earlier snapshot",
         joined + size + inserted + twoSuffix, false},
        {"later deletion cannot change the value of an earlier snapshot",
         joined + inserted + size + absentDelete + oneSuffix, false},
        {"a possible overwrite cannot choose the increasing insertion effect",
         joined + replace(inserted, "%three", "%one") + size + twoSuffix, false},
        {"a possible deletion cannot choose the decreasing cardinality effect",
         joined + replace(absentDelete, "%three", "%one") + size + suffix, false},
        {"a fluent alias applies a known insertion to the actual Map instance",
         joined +
             "  %aliasSetter = ctjs.get_property %seeded[%setName]\n"
             "  %inserted = ctjs.call %aliasSetter(%seeded, %three, %one)\n" +
             size + twoSuffix,
         true},
        {"a fluent alias applies a known deletion to the actual Map instance",
         joined + replace(aliasDelete, "%one", "%three") + size + oneSuffix, true},
        {"unknown effects cannot retain an updated mutable size",
         joined + inserted + "  %effect = ctjs.call %p(%receiver)\n" + size + twoSuffix, false,
         false},
        {"known cardinality does not supply a common member at the saved key",
         joined + inserted + size + replace(read, "%zero", "%saved"), false},
        {"known insertion supplies membership independently of the joined size",
         joined + inserted + size + replace(read, "%zero", "%three"), true},
        {"known deletion removes membership independently of the saved cardinality",
         joined + inserted + size + absentDelete + replace(read, "%zero", "%three"), false},
        {"unknown incoming keys cannot borrow a later mutation's exact effect",
         seed + extra + differentDeletes + inserted + size + twoSuffix, false},
    };
    rows.insert(rows.end(), mutationRows.begin(), mutationRows.end());
    for (unsigned candidates : {64u, 65u}) {
        std::string mutations = inserted;
        for (unsigned index = 3; index < candidates; ++index) {
            mutations += "  %overwrite" + std::to_string(index) +
                         " = ctjs.call %setter(%map, %three, %one)\n";
        }
        rows.push_back({candidates == 64
                            ? "known mutations remain within the complete candidate limit"
                            : "known overwrites cannot bypass the complete candidate limit",
                        joined + mutations + size + twoSuffix, candidates == 64});
    }
    for (unsigned candidates : {31u, 33u}) {
        std::string left;
        std::string right;
        for (unsigned index = 0; index < candidates; ++index) {
            left +=
                "    %left" + std::to_string(index) + " = ctjs.call %setter(%map, %one, %one)\n";
            right +=
                "    %right" + std::to_string(index) + " = ctjs.call %setter(%map, %two, %one)\n";
        }
        rows.push_back(
            {candidates == 31 ? "a bounded branch union proves the later deleted key absent"
                              : "a discarded branch union cannot prove later deletion absent",
             clear + branch(left, right) + absentDelete + size + oneSuffix, candidates == 31});
    }
    const auto marked = [](mlir::ModuleOp module, llvm::StringRef name) {
        mlir::Operation * result = nullptr;
        module.walk([&](mlir::Operation * op) {
            if (op->hasAttr(name)) { result = op; }
        });
        return result;
    };
    const auto verify = [&](mlir::ModuleOp module, const char * what, bool expected,
                            bool mixed = true, bool supported = true) {
        for (bool clone : {false, true}) {
            mlir::OwningOpRef<mlir::ModuleOp> fresh;
            auto current = module;
            if (clone) {
                fresh = mlir::OwningOpRef<mlir::ModuleOp>{module.clone()};
                current = *fresh;
            }
            mlir::Builder attrs(&context);
            current.walk([&](mlir::Operation * op) {
                op->setAttr("ctnative.map_exact_size", attrs.getI32IntegerAttr(0));
                if (op->hasAttr("check")) {
                    op->setAttr(ctnative::kNativeMapPresent, attrs.getUnitAttr());
                    op->setAttr(ctnative::kNativeMapReadType, attrs.getStringAttr("number"));
                }
            });
            std::vector<mlir::Operation *> before;
            std::vector<std::vector<mlir::Value>> operands;
            current.walk([&](mlir::Operation * op) {
                before.push_back(op);
                operands.emplace_back(op->operand_begin(), op->operand_end());
            });
            ctnative::prepareNativeMaps(current);
            auto * observed = marked(current, "check");
            if (!observed || ctnative::nativeMapAction(observed) != (supported ? "get" : "") ||
                observed->hasAttr(ctnative::kNativeMapPresent) != expected) {
                std::printf("FAIL %s: %s delete-size membership differs from %d\n", what,
                            clone ? "fresh" : "reused", static_cast<int>(expected));
                ++failures;
            }
            std::vector<mlir::Operation *> after;
            current.walk([&](mlir::Operation * op) { after.push_back(op); });
            bool intact = before == after;
            for (size_t index = 0; intact && index < before.size(); ++index) {
                intact &= llvm::equal(before[index]->getOperands(), operands[index]);
            }
            if (!intact) {
                std::printf("FAIL %s: delete-size preparation changed executable source\n", what);
                ++failures;
            }
            if (!supported && observed && observed->hasAttr(ctnative::kNativeMapReadType)) {
                std::printf("FAIL %s: unsupported Map kept forged read-type evidence\n", what);
                ++failures;
            }
            check(current, what,
                  !supported ? "!ctnative.boxed"
                  : !mixed   ? "!ctnative.opt<!ctnative.num<i32>>"
                  : expected
                      ? "!ctnative.num<f64>"
                      : "!ctnative.opt<!ctnative.variant<!ctnative.bool, !ctnative.num<i32>>>");
        }
    };
    const std::string mixedSuffix = "  %booleanWrite = ctjs.call %setter(%map, %three, %false)\n";
    for (const presenceRow & r : rows) {
        for (bool mixed : {false, true}) {
            auto module = mlir::parseSourceString<mlir::ModuleOp>(
                prologue() + prelude + r.body + (mixed ? mixedSuffix : "") +
                    "  ctjs.return %observed\n}\n",
                &context);
            if (!module) {
                std::printf("FAIL %s: delete-size presence fixture did not parse\n", r.what);
                ++failures;
                continue;
            }
            // A homogeneous local Number read retains its optional public
            // type; the later mixed store independently requests presence.
            verify(*module, r.what, mixed && r.present, mixed, r.supported);
        }
    }
    auto joinedModule =
        mlir::parseSourceString<mlir::ModuleOp>(prologue() + prelude + joined + size + oneSuffix +
                                                    mixedSuffix + "  ctjs.return %observed\n}\n",
                                                &context);
    if (!joinedModule) {
        std::printf("FAIL live joined-size fixture did not parse\n");
        ++failures;
    } else {
        auto * snapshot = marked(*joinedModule, "snapshot");
        auto * deletion = marked(*joinedModule, "join_erase");
        auto * resetting = marked(*joinedModule, "reset");
        auto * observed = marked(*joinedModule, "check");
        mlir::scf::IfOp branchOp;
        ctjs::ConstantOp wrong;
        joinedModule->walk([&](mlir::scf::IfOp op) { branchOp = op; });
        joinedModule->walk([&](ctjs::ConstantOp op) {
            auto number = llvm::dyn_cast<ctjs::NumberAttr>(op.getValue());
            if (number && number.getBits() == 4613937818241073152ULL) { wrong = op; }
        });
        if (!snapshot || !deletion || !resetting || !observed || !branchOp || !wrong) {
            std::printf("FAIL live joined-size fixture lost a marked source operation\n");
            ++failures;
        } else {
            verify(*joinedModule, "live exact joined size", true);
            snapshot->moveBefore(branchOp);
            verify(*joinedModule, "moving snapshot before join retains the old two", false);
            snapshot->moveAfter(branchOp);
            verify(*joinedModule, "restoring snapshot after join rederives one", true);
            const auto key = deletion->getOperand(2);
            deletion->setOperand(2, wrong.getResult());
            verify(*joinedModule, "one changed branch cannot inherit the sibling's exact size",
                   false);
            deletion->setOperand(2, key);
            verify(*joinedModule, "restoring equal branch cardinalities recovers one", true);
            snapshot->moveAfter(resetting);
            verify(*joinedModule, "reading after reset cannot reuse a previous join's size", false);
            snapshot->moveBefore(resetting);
            verify(*joinedModule, "restoring actual read time restores exact one", true);
            const auto lookup = observed->getOperand(2);
            observed->setOperand(2, wrong.getResult());
            verify(*joinedModule, "changed lookup cannot inherit saved-size membership", false);
            observed->setOperand(2, lookup);
            verify(*joinedModule, "restoring actual lookup key recovers membership", true);
            std::printf("joined-size Map presence: %zu source/mixed rows and eight live edits, "
                        "reused/fresh modules\n",
                        joinedRows * 2);
        }
    }
    for (const auto & [body, suffixBody] : {std::pair{joined + inserted + size, twoSuffix},
                                            {joined + absentDelete + size, oneSuffix}}) {
        auto mutated = mlir::parseSourceString<mlir::ModuleOp>(
            prologue() + prelude + body + suffixBody + mixedSuffix + "  ctjs.return %observed\n}\n",
            &context);
        if (!mutated) {
            std::printf("FAIL live joined-mutation fixture did not parse\n");
            ++failures;
            continue;
        }
        auto * mutation = marked(*mutated, "mutation");
        auto * snapshot = marked(*mutated, "snapshot");
        auto * resetting = marked(*mutated, "reset");
        ctjs::ConstantOp one;
        ctjs::ConstantOp three;
        mutated->walk([&](ctjs::ConstantOp op) {
            const auto number = llvm::dyn_cast<ctjs::NumberAttr>(op.getValue());
            if (number && number.getBits() == 4607182418800017408ULL) { one = op; }
            if (number && number.getBits() == 4613937818241073152ULL) { three = op; }
        });
        if (!mutation || !snapshot || !resetting || !one || !three) {
            std::printf("FAIL live joined-mutation fixture lost a marked source operation\n");
            ++failures;
            continue;
        }
        verify(*mutated, "live known mutation updates the independently joined size", true);
        const auto key = mutation->getOperand(2);
        mutation->setOperand(2, one.getResult());
        verify(*mutated, "a changed possibly present key invalidates old mutation facts", false);
        mutation->setOperand(2, key);
        verify(*mutated, "restoring the definitely absent key restores the exact effect", true);
        snapshot->moveAfter(resetting);
        verify(*mutated, "a snapshot after reset cannot borrow an earlier mutation's size", false);
        snapshot->moveBefore(resetting);
        verify(*mutated, "restoring the actual read position restores known cardinality", true);
        const auto readKey = marked(*mutated, "check")->getOperand(2);
        marked(*mutated, "check")->setOperand(2, three.getResult());
        verify(*mutated, "changing the later lookup cannot inherit saved-key membership", false);
        marked(*mutated, "check")->setOperand(2, readKey);
        verify(*mutated, "restoring the actual lookup key restores membership", true);
    }
    std::printf("known-mutation Map presence: %zu source/mixed rows and twelve live edits, "
                "reused/fresh modules\n",
                (mutationRows.size() + 4) * 2);
    auto module = mlir::parseSourceString<mlir::ModuleOp>(prologue() + prelude + clear + seed +
                                                              erase + size + suffix + mixedSuffix +
                                                              "  ctjs.return %observed\n}\n",
                                                          &context);
    if (!module) {
        std::printf("FAIL live delete-size fixture did not parse\n");
        ++failures;
        return;
    }
    auto * snapshot = marked(*module, "snapshot");
    auto * seeded = marked(*module, "seed");
    auto * deletion = marked(*module, "erase");
    auto * resetting = marked(*module, "reset");
    auto * storing = marked(*module, "mutate_store");
    auto * observed = marked(*module, "check");
    if (!snapshot || !seeded || !deletion || !resetting || !storing || !observed) {
        std::printf("FAIL live delete-size fixture lost its marked operations\n");
        ++failures;
        return;
    }
    verify(*module, "current post-delete zero snapshot", true);
    snapshot->moveBefore(deletion);
    verify(*module, "moving size before deletion keeps the old one", false);
    snapshot->moveAfter(deletion);
    verify(*module, "restoring read after deletion recovers zero", true);
    deletion->moveAfter(storing);
    verify(*module, "a later deletion cannot justify an earlier zero", false);
    deletion->moveBefore(snapshot);
    verify(*module, "restoring deletion before size recovers zero", true);
    const auto erasedKey = deletion->getOperand(2);
    deletion->setOperand(2, observed->getOperand(2));
    verify(*module, "deleting absent zero leaves one present at the size read", false);
    deletion->setOperand(2, erasedKey);
    verify(*module, "restoring the deleted key recovers zero", true);
    const auto saved = storing->getOperand(2);
    storing->setOperand(2, erasedKey);
    verify(*module, "a store at one cannot borrow saved-zero equality", false);
    storing->setOperand(2, saved);
    verify(*module, "restoring the store key recovers exact zero", true);
    const auto zero = observed->getOperand(2);
    observed->setOperand(2, erasedKey);
    verify(*module, "a lookup at one cannot borrow saved-zero membership", false);
    observed->setOperand(2, zero);
    verify(*module, "restoring the literal lookup key recovers membership", true);
    resetting->moveAfter(storing);
    verify(*module, "later clear destroys membership despite an immutable saved zero", false);
    resetting->moveBefore(storing);
    verify(*module, "restoring clear before the final store recovers membership", true);
    std::printf("delete-size Map presence: %zu source/mixed rows and twelve live edits, "
                "reused/fresh modules\n",
                rows.size() * 2);
}

} // namespace ctcompile::test::type_inference
