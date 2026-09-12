#include "Tests.h"

namespace ctcompile::test::type_inference {
namespace {

void checkStoredMapAliases(mlir::MLIRContext & context) {
    using namespace ctcompile;
    const std::string prelude = R"mlir(
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %two = ctjs.constant #ctjs.number<4611686018427387904>
  %boolean = ctjs.constant #ctjs.boolean<true>
  %setName = ctjs.constant #ctjs.string<"set">
  %getName = ctjs.constant #ctjs.string<"get">
  %clearName = ctjs.constant #ctjs.string<"clear">
  %constructor = ctjs.load_global "Map"
  %outer = ctjs.construct %constructor(%constructor)
  %first = ctjs.construct %constructor(%constructor)
  %second = ctjs.construct %constructor(%constructor)
  %firstSetter = ctjs.get_property %first[%setName]
  %secondSetter = ctjs.get_property %second[%setName]
  %firstSeed = ctjs.call %firstSetter(%first, %one, %one)
  %secondSeed = ctjs.call %secondSetter(%second, %two, %boolean)
  %setter = ctjs.get_property %outer[%setName]
  %getter = ctjs.get_property %outer[%getName]
  %firstStored = ctjs.call %setter(%outer, %one, %first)
  %secondStored = ctjs.call %setter(%outer, %two, %second)
  %saved = ctjs.call %getter(%outer, %one)
)mlir";
    const std::string clearOuter = R"mlir(
  %clearer = ctjs.get_property %outer[%clearName]
  %cleared = ctjs.call %clearer(%outer)
)mlir";
    const std::string clearSecond = R"mlir(
  %secondAlias = ctjs.call %getter(%outer, %two)
  %clearer = ctjs.get_property %secondAlias[%clearName]
  %cleared = ctjs.call %clearer(%secondAlias)
)mlir";
    const std::string overwrite = "  %overwritten = ctjs.call %setter(%outer, %one, %second)\n";
    const std::string uncertain = "  %overwritten = ctjs.call %setter(%outer, %p, %second)\n";
    const std::string helper = R"mlir(
ctjs.func private @mutate(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value,
                          %map: !ctjs.value, %child: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %name = ctjs.constant #ctjs.string<"set">
  %method = ctjs.get_property %map[%name]
  %written = ctjs.call %method(%map, %one, %child)
  %u = ctjs.constant #ctjs.undefined
  ctjs.return %u
}
)mlir";
    const std::string summarized =
        "  %called = ctjs.call_direct @mutate(%receiver, %new_target, %callee, %outer, %second)\n";
    const std::string clearHelper = R"mlir(
ctjs.func private @clearChild(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value,
                              %map: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %name = ctjs.constant #ctjs.string<"clear">
  %method = ctjs.get_property %map[%name]
  %cleared = ctjs.call %method(%map)
  %u = ctjs.constant #ctjs.undefined
  ctjs.return %u
}
)mlir";
    const std::string summarizedClear =
        "  %called = ctjs.call_direct @clearChild(%receiver, %new_target, %callee, %second)\n";
    struct aliasRow {
        const char * what;
        std::string effect;
        const char * tag;
        bool fresh = false;
    };
    const std::vector<aliasRow> rows = {
        {"a definite nested get aliases its stored child", "", "number"},
        {"saved child identity survives outer replacement and clear", overwrite + clearOuter,
         "number"},
        {"clearing another fresh child preserves saved first-child contents", clearSecond,
         "number"},
        {"clearing the original child invalidates its saved alias", R"mlir(
  %clearer = ctjs.get_property %first[%clearName]
  %cleared = ctjs.call %clearer(%first)
)mlir",
         ""},
        {"possible outer overwrite preserves an earlier saved child", uncertain, "number"},
        {"possible outer overwrite cannot grant a later get an old origin", uncertain, "", true},
        {"exact outer overwrite selects the replacement origin",
         "  %written = ctjs.call %secondSetter(%second, %one, %boolean)\n" + overwrite, "bool",
         true},
        {"summarized outer writes preserve an earlier saved child", summarized, "number"},
        {"summarized writes clear a later get's stored-origin fact", summarized, "", true},
        {"a formal child mutation conservatively invalidates its schema family", summarizedClear,
         ""},
        {"branch-local mutations of other owners preserve saved child contents",
         "  %condition = ctjs.truthy %p\n  scf.if %condition {\n" + clearSecond + "  } else {\n" +
             clearOuter + "  }\n",
         "number"},
        {"distinct branch payload origins cannot become one definite get alias",
         "  %condition = ctjs.truthy %p\n  scf.if %condition {\n" + overwrite + "  } else {\n  }\n",
         "", true},
        {"equal branch payload origins preserve the definite get alias", R"mlir(
  %condition = ctjs.truthy %p
  scf.if %condition {
    %written = ctjs.call %setter(%outer, %one, %first)
  } else {
    %again = ctjs.call %setter(%outer, %one, %first)
  }
)mlir",
         "number", true},
    };
    for (const auto & row : rows) {
        const std::string receiver = row.fresh ? "%fresh" : "%saved";
        const auto source = prologue() + prelude + row.effect +
                            (row.fresh ? "  %fresh = ctjs.call %getter(%outer, %one)\n" : "") +
                            "  %read = ctjs.get_property " + receiver + "[%getName]\n" +
                            "  %observed = ctjs.call %read(" + receiver + ", %one) {check}\n" +
                            "  ctjs.return %observed\n}\n" +
                            (row.effect == summarized        ? helper
                             : row.effect == summarizedClear ? clearHelper
                                                             : "");
        auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
        if (!module) {
            std::printf("FAIL %s: stored-alias fixture did not parse\n", row.what);
            ++failures;
            continue;
        }
        for (const bool clone : {false, true}) {
            mlir::OwningOpRef<mlir::ModuleOp> fresh;
            auto current = *module;
            if (clone) {
                fresh = mlir::OwningOpRef<mlir::ModuleOp>{module->clone()};
                current = *fresh;
            }
            mlir::Operation * observed = nullptr;
            std::vector<mlir::Operation *> before;
            std::vector<std::vector<mlir::Value>> operands;
            mlir::Builder attrs(&context);
            current.walk([&](mlir::Operation * op) {
                before.push_back(op);
                operands.emplace_back(op->operand_begin(), op->operand_end());
                op->setAttr(ctnative::kNativeMapPresent, attrs.getUnitAttr());
                op->setAttr(ctnative::kNativeMapReadType, attrs.getStringAttr("forged"));
                if (op->hasAttr("check")) { observed = op; }
            });
            ctnative::prepareNativeMaps(current);
            auto tag = observed
                           ? observed->getAttrOfType<mlir::StringAttr>(ctnative::kNativeMapReadType)
                           : mlir::StringAttr{};
            const bool present = *row.tag != '\0';
            if (!observed || ctnative::nativeMapAction(observed) != "get" ||
                observed->hasAttr(ctnative::kNativeMapPresent) != present ||
                (tag ? tag.getValue() : llvm::StringRef{}) != row.tag) {
                std::printf("FAIL %s: %s stored alias presence/type differs from %s\n", row.what,
                            clone ? "fresh" : "reused", present ? row.tag : "unproved");
                ++failures;
            }
            std::vector<mlir::Operation *> after;
            current.walk([&](mlir::Operation * op) { after.push_back(op); });
            bool intact = before == after;
            for (size_t index = 0; intact && index < before.size(); ++index) {
                intact &= llvm::equal(before[index]->getOperands(), operands[index]);
            }
            if (!intact) {
                std::printf("FAIL %s: stored-alias preparation changed executable source\n",
                            row.what);
                ++failures;
            }
        }
    }
    std::printf("stored Map aliases: %zu rows, reused/fresh modules and forged reports\n",
                rows.size());
}

} // namespace

void checkMapZeroSizePresence(mlir::MLIRContext & context) {
    checkStoredMapAliases(context);
    using namespace ctcompile;
    const std::string prelude = R"mlir(
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %two = ctjs.constant #ctjs.number<4611686018427387904>
  %literalZero = ctjs.constant #ctjs.number<0>
  %negativeZero = ctjs.constant #ctjs.number<9223372036854775808>
  %nil = ctjs.constant #ctjs.undefined
  %setName = ctjs.constant #ctjs.string<"set">
  %getName = ctjs.constant #ctjs.string<"get">
  %clearName = ctjs.constant #ctjs.string<"clear">
  %sizeName = ctjs.constant #ctjs.string<"size">
  %constructor = ctjs.load_global "Map"
  %map = ctjs.construct %constructor(%constructor)
  %setter = ctjs.get_property %map[%setName]
  %getter = ctjs.get_property %map[%getName]
  %clearer = ctjs.get_property %map[%clearName]
)mlir";
    const std::string seed = "  %seeded = ctjs.call %setter(%map, %one, %one)\n";
    const std::string clear = "  %cleared = ctjs.call %clearer(%map) {mutate_clear}\n";
    const std::string size = "  %zero = ctjs.get_property %map[%sizeName] {snapshot}\n";
    const std::string store = "  %stored = ctjs.call %setter(%map, %zero, %one) {mutate_store}\n";
    const std::string read = "  %observed = ctjs.call %getter(%map, %literalZero) {check}\n";
    const auto replace = [](std::string text, llvm::StringRef from, llvm::StringRef to) {
        const auto position = text.find(from.str());
        if (position == std::string::npos) {
            std::printf("FAIL exact-zero fixture replacement did not match\n");
            ++failures;
            return text;
        }
        text.replace(position, from.size(), to.str());
        return text;
    };
    const auto both = "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + clear +
                      "  } else {\n    %again = ctjs.call %clearer(%map)\n"
                      "    %twice = ctjs.call %clearer(%map)\n  }\n";
    const std::string branchEntries =
        "  %bit = ctjs.truthy %p\n  scf.if %bit {\n"
        "    %left = ctjs.call %setter(%map, %one, %one)\n"
        "  } else {\n    %right = ctjs.call %setter(%map, %two, %one)\n  }\n";
    const std::string selected = "  %bit = ctjs.truthy %p\n"
                                 "  %zero = scf.if %bit -> (!ctjs.value) {\n"
                                 "    %left = ctjs.get_property %map[%sizeName]\n"
                                 "    scf.yield %left : !ctjs.value\n"
                                 "  } else {\n    scf.yield %negativeZero : !ctjs.value\n  }\n";
    struct presenceRow {
        const char * what;
        std::string body;
        bool present;
    };
    const std::vector<presenceRow> rows = {
        {"saved size after clear is the literal zero Map key", seed + clear + size + store + read,
         true},
        {"negative zero is the same key as a saved empty size",
         seed + clear + size + store + replace(read, "%literalZero", "%negativeZero"), true},
        {"clearing without known entries still establishes exact empty size",
         clear + size + store + read, true},
        {"an earlier nonempty size does not become zero after clear",
         seed + size + clear + store + read, false},
        {"a post-growth size does not retain a prior empty-instance fact",
         clear + seed + size + store + read, false},
        {"no known-entry fact does not establish exact cardinality", size + store + read, false},
        {"empty intersection of different branch entries is not an empty Map",
         branchEntries + size + store + read, false},
        {"clearing on every structural arm establishes exact zero",
         seed + both + size + store + read, true},
        {"a single clearing arm retains an unknown nonempty path",
         seed + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + clear + "  }\n" + size + store +
             read,
         false},
        {"a saved zero survives later clear and growth",
         clear + size + seed + "  %again = ctjs.call %clearer(%map)\n" + store + read, true},
        {"a later clear removes a zero-key membership fact",
         clear + size + store + "  %again = ctjs.call %clearer(%map)\n" + read, false},
        {"zero snapshots from a size and a literal join as zero", clear + selected + store + read,
         true},
        {"a selected nonzero literal prevents an exact-zero join",
         clear + replace(selected, "scf.yield %negativeZero", "scf.yield %one") + store + read,
         false},
        {"a saved zero remains distinct from a nonzero key",
         clear + size + store + replace(read, "%literalZero", "%one"), false},
    };
    const auto marked = [](mlir::ModuleOp module, llvm::StringRef name) {
        mlir::Operation * result = nullptr;
        module.walk([&](mlir::Operation * op) {
            if (op->hasAttr(name)) { result = op; }
        });
        return result;
    };
    const auto verify = [&](mlir::ModuleOp module, const char * what, bool expected,
                            bool mixed = true) {
        // The same live preparation is run on a reused module and an exact
        // fresh clone. Previously attached reports are deliberately hostile.
        for (bool clone : {false, true}) {
            mlir::OwningOpRef<mlir::ModuleOp> fresh;
            auto current = module;
            if (clone) {
                fresh = mlir::OwningOpRef<mlir::ModuleOp>{module.clone()};
                current = *fresh;
            }
            std::vector<mlir::Operation *> before;
            std::vector<std::vector<mlir::Value>> operands;
            current.walk([&](mlir::Operation * op) {
                before.push_back(op);
                operands.emplace_back(op->operand_begin(), op->operand_end());
            });
            ctnative::prepareNativeMaps(current);
            auto * observed = marked(current, "check");
            if (!observed || ctnative::nativeMapAction(observed) != "get" ||
                observed->hasAttr(ctnative::kNativeMapPresent) != expected) {
                std::printf("FAIL %s: %s current Map membership differs from %d\n", what,
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
                std::printf("FAIL %s: exact-zero preparation changed executable source\n", what);
                ++failures;
            }
            check(current, what,
                  !mixed ? "!ctnative.opt<!ctnative.num<i32>>"
                  : expected
                      ? "!ctnative.num<f64>"
                      : "!ctnative.opt<!ctnative.variant<!ctnative.bool, !ctnative.num<i32>>>");
        }
    };
    // The original homogeneous Number source retains its optional result:
    // preparation currently requests public presence only for published or
    // mixed reads. Adding a later Boolean write independently requests the
    // mixed read proof without changing any read-time operation or operand.
    const std::string mixedSuffix = "  %boolean = ctjs.constant #ctjs.boolean<false>\n"
                                    "  %booleanWrite = ctjs.call %setter(%map, %two, %boolean)\n";
    for (const presenceRow & r : rows) {
        auto homogeneous = mlir::parseSourceString<mlir::ModuleOp>(
            prologue() + prelude + r.body + "  ctjs.return %observed\n}\n", &context);
        if (!homogeneous) {
            std::printf("FAIL %s: homogeneous zero-size fixture did not parse\n", r.what);
            ++failures;
            continue;
        }
        verify(*homogeneous, r.what, false, false);
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            prologue() + prelude + r.body + mixedSuffix + "  ctjs.return %observed\n}\n", &context);
        if (!module) {
            std::printf("FAIL %s: zero-size presence fixture did not parse\n", r.what);
            ++failures;
            continue;
        }
        mlir::Builder attrs(&context);
        module->walk([&](mlir::Operation * op) {
            op->setAttr("ctnative.map_empty", attrs.getBoolAttr(true));
            op->setAttr("ctnative.map_size_zero", attrs.getBoolAttr(true));
            if (op->hasAttr("check")) {
                op->setAttr(ctnative::kNativeMapPresent, attrs.getUnitAttr());
                op->setAttr(ctnative::kNativeMapReadType, attrs.getStringAttr("number"));
            }
        });
        verify(*module, r.what, r.present);
    }

    auto module = mlir::parseSourceString<mlir::ModuleOp>(prologue() + prelude + seed + clear +
                                                              size + store + read + mixedSuffix +
                                                              "  ctjs.return %observed\n}\n",
                                                          &context);
    if (!module) {
        std::printf("FAIL live exact-zero presence fixture did not parse\n");
        ++failures;
        return;
    }
    auto * snapshot = marked(*module, "snapshot");
    auto * clearing = marked(*module, "mutate_clear");
    auto * storing = marked(*module, "mutate_store");
    if (!snapshot || !clearing || !storing) {
        std::printf("FAIL live exact-zero presence fixture lost its source positions\n");
        ++failures;
        return;
    }
    verify(*module, "live zero-size source before mutation", true);
    snapshot->moveBefore(clearing);
    verify(*module, "moving size before clear invalidates its prior zero evidence", false);
    snapshot->moveAfter(clearing);
    verify(*module, "restoring size after clear recovers current zero evidence", true);
    const auto key = storing->getOperand(2);
    auto setter = llvm::cast<ctjs::CallOp>(storing);
    auto * one = setter.getArgs()[1].getDefiningOp();
    storing->setOperand(2, one->getResult(0));
    verify(*module, "a live nonzero store key cannot inherit saved-zero membership", false);
    storing->setOperand(2, key);
    verify(*module, "restoring the exact saved key recovers membership", true);
    auto * observed = marked(*module, "check");
    clearing->moveAfter(storing);
    verify(*module, "moving clear after the set invalidates prior membership", false);
    clearing->moveBefore(snapshot);
    verify(*module, "restoring clear before the read recovers the proof", true);
    const auto readKey = observed->getOperand(2);
    observed->setOperand(2, one->getResult(0));
    verify(*module, "a live nonzero lookup key cannot borrow zero membership", false);
    observed->setOperand(2, readKey);
    verify(*module, "restoring the lookup key restores independently derived presence", true);
    std::printf("exact-zero Map presence: %zu source/mixed rows and eight live edits, reused/fresh "
                "modules\n",
                rows.size() * 2);
}

void checkMapExactSizePresence(mlir::MLIRContext & context) {
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
  %constructor = ctjs.load_global "Map"
  %map = ctjs.construct %constructor(%constructor)
  %setter = ctjs.get_property %map[%setName]
  %getter = ctjs.get_property %map[%getName]
  %clearer = ctjs.get_property %map[%clearName]
)mlir";
    const std::string clear = "  %cleared = ctjs.call %clearer(%map) {mutate_clear}\n";
    const std::string seed = "  %seeded = ctjs.call %setter(%map, %one, %one) {seed}\n";
    const std::string extra = "  %extra = ctjs.call %setter(%map, %two, %one) {extra}\n";
    const std::string size = "  %saved = ctjs.get_property %map[%sizeName] {snapshot}\n";
    const std::string reset = "  %reset = ctjs.call %clearer(%map) {reset}\n";
    const std::string store = "  %stored = ctjs.call %setter(%map, %saved, %one) {mutate_store}\n";
    const std::string read = "  %observed = ctjs.call %getter(%map, %one) {check}\n";
    const auto replace = [](std::string text, llvm::StringRef from, llvm::StringRef to) {
        const auto position = text.find(from.str());
        if (position == std::string::npos) {
            std::printf("FAIL exact-size fixture replacement did not match\n");
            ++failures;
            return text;
        }
        text.replace(position, from.size(), to.str());
        return text;
    };
    const std::string both = "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + seed +
                             "  } else {\n    %right = ctjs.call %setter(%map, %anotherOne, %one)\n"
                             "    %again = ctjs.call %setter(%map, %one, %one)\n  }\n";
    const std::string selected = "  %bit = ctjs.truthy %p\n"
                                 "  %saved = scf.if %bit -> (!ctjs.value) {\n"
                                 "    %left = ctjs.get_property %map[%sizeName]\n"
                                 "    scf.yield %left : !ctjs.value\n"
                                 "  } else {\n    scf.yield %anotherOne : !ctjs.value\n  }\n";
    const auto suffix = reset + store + read;
    struct presenceRow {
        const char * what;
        std::string body;
        bool present;
    };
    const std::vector<presenceRow> rows = {
        {"a size saved after clear and one insertion is exactly one", clear + seed + size + suffix,
         true},
        {"a literal repair keeps the evaluated size read",
         clear + seed + replace(size, "%saved", "%evaluated") +
             "  %saved = ctjs.constant #ctjs.number<4607182418800017408>\n" + suffix,
         true},
        {"different SSA literals for one equal key count once",
         clear + seed + replace(extra, "%two", "%anotherOne") + size + suffix, true},
        {"a saved one survives later growth and clearing", clear + seed + size + extra + suffix,
         true},
        {"two distinct complete keys give exactly two",
         clear + seed + extra + size + reset + store + replace(read, "%one", "%two"), true},
        {"two entries do not retain the earlier exact one", clear + seed + extra + size + suffix,
         false},
        {"an earlier size read cannot learn from a later insertion", clear + size + seed + suffix,
         false},
        {"a later clear cannot establish the earlier exact size", size + clear + seed + suffix,
         false},
        {"uncleared initial contents do not follow from a positive lower bound",
         seed + size + suffix, false},
        {"positive and negative zero count as one SameValueZero key",
         clear + replace(seed, "%one, %one", "%zero, %one") +
             replace(extra, "%two", "%negativeZero") + size + suffix,
         true},
        {"different NaN bit patterns count as one SameValueZero key",
         clear + replace(seed, "%one, %one", "%nan, %one") + replace(extra, "%two", "%anotherNan") +
             size + suffix,
         true},
        {"Boolean false and numeric zero remain distinct keys",
         clear + replace(seed, "%one, %one", "%zero, %one") + replace(extra, "%two", "%false") +
             size + reset + store + replace(read, "%one", "%two"),
         true},
        {"String one and Number one remain distinct keys",
         clear + seed + replace(extra, "%two", "%stringOne") + size + reset + store +
             replace(read, "%one", "%two"),
         true},
        {"every structural arm leaves the same exact one key", clear + both + size + suffix, true},
        {"one empty structural arm prevents exact one",
         clear + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + seed + "  }\n" + size + suffix,
         false},
        {"a conditional second key invalidates the complete cardinality join",
         clear + seed + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + extra + "  }\n" + size +
             suffix,
         false},
        {"different singleton keys independently join their exact-one cardinalities",
         clear +
             replace(replace(both, "%anotherOne", "%three"),
                     "    %again = ctjs.call %setter(%map, %one, %one)\n", "") +
             size + suffix,
         true},
        {"selected size and literal arms independently establish one",
         clear + seed + selected + suffix, true},
        {"one selected two arm cannot inherit the other arm's exact one",
         clear + seed + replace(selected, "scf.yield %anotherOne", "scf.yield %two") + suffix,
         false},
        {"a fluent set alias denotes the exact Map whose size is read",
         clear + seed + replace(size, "%map", "%seeded") + suffix, true},
        {"a live clear through a fluent alias removes later membership",
         clear + seed + size + reset + store +
             "  %aliasClearer = ctjs.get_property %stored[%clearName]\n"
             "  %aliasClear = ctjs.call %aliasClearer(%stored)\n" +
             read,
         false},
        {"a known saved two used as a key joins its literal counterpart",
         clear + seed + extra + size + reset + store +
             "  %later = ctjs.get_property %map[%sizeName]\n" + replace(read, "%one", "%two"),
         true},
    };
    const auto marked = [](mlir::ModuleOp module, llvm::StringRef name) {
        mlir::Operation * result = nullptr;
        module.walk([&](mlir::Operation * op) {
            if (op->hasAttr(name)) { result = op; }
        });
        return result;
    };
    const auto verify = [&](mlir::ModuleOp module, const char * what, bool expected,
                            bool mixed = true) {
        for (bool clone : {false, true}) {
            mlir::OwningOpRef<mlir::ModuleOp> fresh;
            auto current = module;
            if (clone) {
                fresh = mlir::OwningOpRef<mlir::ModuleOp>{module.clone()};
                current = *fresh;
            }
            mlir::Builder attrs(&context);
            current.walk([&](mlir::Operation * op) {
                op->setAttr("ctnative.map_exact_size", attrs.getI32IntegerAttr(1));
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
            if (!observed || ctnative::nativeMapAction(observed) != "get" ||
                observed->hasAttr(ctnative::kNativeMapPresent) != expected) {
                std::printf("FAIL %s: %s exact-size membership differs from %d\n", what,
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
                std::printf("FAIL %s: exact-size preparation changed executable source\n", what);
                ++failures;
            }
            check(current, what,
                  !mixed ? "!ctnative.opt<!ctnative.num<i32>>"
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
                std::printf("FAIL %s: exact-size presence fixture did not parse\n", r.what);
                ++failures;
                continue;
            }
            // A homogeneous local Number read retains its optional public
            // type; the later mixed store independently requests presence.
            verify(*module, r.what, mixed && r.present, mixed);
        }
    }
    auto module = mlir::parseSourceString<mlir::ModuleOp>(prologue() + prelude + clear + seed +
                                                              size + extra + suffix + mixedSuffix +
                                                              "  ctjs.return %observed\n}\n",
                                                          &context);
    if (!module) {
        std::printf("FAIL live exact-size presence fixture did not parse\n");
        ++failures;
        return;
    }
    auto * snapshot = marked(*module, "snapshot");
    auto * seeded = marked(*module, "seed");
    auto * growth = marked(*module, "extra");
    auto * resetting = marked(*module, "reset");
    auto * storing = marked(*module, "mutate_store");
    auto * observed = marked(*module, "check");
    if (!snapshot || !seeded || !growth || !resetting || !storing || !observed) {
        std::printf("FAIL live exact-size presence fixture lost its marked operations\n");
        ++failures;
        return;
    }
    verify(*module, "current saved one before growth", true);
    snapshot->moveAfter(growth);
    verify(*module, "moving the read after growth yields two rather than one", false);
    snapshot->moveBefore(growth);
    verify(*module, "restoring the read before growth recovers exact one", true);
    snapshot->moveBefore(seeded);
    verify(*module, "moving the read before the first insertion yields zero", false);
    snapshot->moveAfter(seeded);
    verify(*module, "restoring the first insertion before size recovers one", true);
    resetting->moveAfter(storing);
    verify(*module, "a live later clear removes the saved-key membership", false);
    resetting->moveBefore(storing);
    verify(*module, "restoring clear before the store recovers membership", true);
    const auto saved = storing->getOperand(2);
    const auto two = growth->getOperand(2);
    storing->setOperand(2, two);
    verify(*module, "a changed live store key cannot inherit snapshot equality", false);
    storing->setOperand(2, saved);
    verify(*module, "restoring the saved store key recovers its independently proved equality",
           true);
    const auto one = observed->getOperand(2);
    observed->setOperand(2, two);
    verify(*module, "a changed live read key cannot borrow saved-one membership", false);
    observed->setOperand(2, one);
    verify(*module, "restoring the read key recovers exact-one membership", true);
    std::printf("exact-size Map presence: %zu source/mixed rows and ten live edits, reused/fresh "
                "modules\n",
                rows.size() * 2);
}

} // namespace ctcompile::test::type_inference
