#include "Tests.h"

namespace ctcompile::test::type_inference {

void checkStaleFieldEffects(mlir::MLIRContext & context) {
    const auto checkBoth = [&](mlir::ModuleOp module, const char * what, bool assigned) {
        const char * expected =
            assigned ? "!ctnative.num<i32>" : "!ctnative.opt<!ctnative.num<i32>>";
        check(module, what, expected);
        const auto stale =
            ctcompile::ctnative::queryNativeObjectFieldPresence(checkedField(module));
        mlir::OwningOpRef<mlir::ModuleOp> fresh{module.clone()};
        check(*fresh, what, expected);
        const auto rebuilt =
            ctcompile::ctnative::queryNativeObjectFieldPresence(checkedField(*fresh));
        if (stale.assigned != assigned || rebuilt.assigned != assigned || stale.exhausted ||
            rebuilt.exhausted) {
            std::printf("FAIL %s: stale/fresh field effect proof disagrees\n", what);
            ++failures;
        }
    };

    // The unused lookup still executes. Changing its property name cannot
    // retain the old map_method authority just because no call follows it.
    const std::string body = kIdentityMapPrelude + identityFieldStore() + identityMapSet("%put") +
                             identityMapGet() + identityFieldRead("%saved");
    auto unused = mlir::parseSourceString<mlir::ModuleOp>(
        prologue() + body + "  ctjs.return %observed\n}\n", &context);
    if (!unused) {
        std::printf("FAIL unused Map method effect fixture did not parse\n");
        ++failures;
    } else {
        ctcompile::ctjs::GetPropertyOp lookup;
        ctcompile::ctjs::ConstantOp changedKey;
        unused->walk([&](ctcompile::ctjs::GetPropertyOp get) {
            auto key = get.getKey().getDefiningOp<ctcompile::ctjs::ConstantOp>();
            auto name = key ? llvm::dyn_cast<ctcompile::ctjs::StringAttr>(key.getValue())
                            : ctcompile::ctjs::StringAttr{};
            if (name && name.getValue() == "clear") {
                lookup = get;
                changedKey = key;
            }
        });
        if (!lookup || !lookup->use_empty() || !lookup->hasAttr("ctnative.map_method")) {
            std::printf("FAIL unused Map method effect fixture lost its marked lookup\n");
            ++failures;
        } else {
            checkBoth(*unused, "unused standard Map lookup before mutation", true);
            const auto name = changedKey.getValue();
            changedKey->setAttr("value", ctcompile::ctjs::StringAttr::get(&context, "unknown"));
            checkBoth(*unused, "unused Map lookup rederives its changed property name", false);
            changedKey->setAttr("value", name);
            checkBoth(*unused, "unused Map lookup restores its standard method", true);
        }
    }

    // A marked zero-argument constructor executes between initialization and
    // the read. Mutate both operands so new_target still equals callee: that
    // syntactic equality alone is not the identity of the standard Map.
    const std::string constructorBody = kIdentityMapPrelude + identityFieldStore() +
                                        identityMapSet("%put") + identityMapGet() +
                                        R"mlir(
  %effect = ctjs.construct %constructor(%constructor)
      {ctnative.map_group = 4 : i64, ctnative.map_site, changed_constructor}
)mlir" + identityFieldRead("%saved");
    auto constructor = mlir::parseSourceString<mlir::ModuleOp>(
        prologue() + constructorBody + "  ctjs.return %observed\n}\n", &context);
    if (!constructor) {
        std::printf("FAIL stale Map constructor effect fixture did not parse\n");
        ++failures;
    } else {
        ctcompile::ctjs::ConstructOp changed;
        constructor->walk([&](ctcompile::ctjs::ConstructOp made) {
            if (made->hasAttr("changed_constructor")) { changed = made; }
        });
        if (!changed || !changed.getArgs().empty() || !changed->hasAttr("ctnative.map_site")) {
            std::printf("FAIL stale Map constructor effect fixture lost its marked constructor\n");
            ++failures;
        } else {
            checkBoth(*constructor, "standard Map constructor before live mutation", true);
            const auto callee = changed.getCallee();
            const auto unknown =
                changed->getParentOfType<ctcompile::ctjs::FuncOp>().getBody().front().getArgument(
                    3);
            changed->setOperand(0, unknown);
            changed->setOperand(1, unknown);
            checkBoth(*constructor, "stale Map site rejects a changed unknown constructor", false);
            changed->setOperand(0, callee);
            changed->setOperand(1, callee);
            checkBoth(*constructor, "Map site restores its live standard constructor", true);
        }
    }

    // Prototype/accessor mutations elsewhere invalidate the closed scalar
    // environment even if a later allocation could rebuild a local fact.
    const std::string environmentBody = R"mlir(
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %nil = ctjs.constant #ctjs.undefined
  %key = ctjs.constant #ctjs.string<"value">
  %otherKey = ctjs.constant #ctjs.string<"ordinary"> {changed_key}
  %other = ctjs.create_object
  ctjs.set_property %other[%otherKey], %nil
  %fresh = ctjs.create_object {ctnative.object_identity}
  ctjs.set_property %fresh[%key], %one {ctnative.object_field_group = 7 : i64}
  %observed = ctjs.get_property %fresh[%key]
      {check, ctnative.object_field_group = 7 : i64}
)mlir";
    auto environment = mlir::parseSourceString<mlir::ModuleOp>(
        prologue() + environmentBody + "  ctjs.return %observed\n}\n", &context);
    if (!environment) {
        std::printf("FAIL scalar field environment mutation fixture did not parse\n");
        ++failures;
        return;
    }
    ctcompile::ctjs::ConstantOp changedKey;
    ctcompile::ctjs::SetPropertyOp ordinary;
    environment->walk([&](ctcompile::ctjs::ConstantOp key) {
        if (key->hasAttr("changed_key")) { changedKey = key; }
    });
    environment->walk([&](ctcompile::ctjs::SetPropertyOp set) {
        if (!set->hasAttr("ctnative.object_field_group")) { ordinary = set; }
    });
    if (!changedKey || !ordinary) {
        std::printf("FAIL scalar field environment fixture lost its exact mutations\n");
        ++failures;
        return;
    }
    checkBoth(*environment, "closed scalar environment before mutation", true);
    const auto originalKey = changedKey.getValue();
    changedKey->setAttr("value", ctcompile::ctjs::StringAttr::get(&context, "__proto__"));
    checkBoth(*environment, "scalar field environment rederives a prototype write", false);
    changedKey->setAttr("value", originalKey);
    checkBoth(*environment, "scalar field environment restores an ordinary field write", true);
    mlir::OpBuilder before(ordinary);
    auto accessor = ctcompile::ctjs::DefineAccessorOp::create(
        before, ordinary.getLoc(), ordinary.getObject(), "value", ordinary.getValue(),
        ordinary.getValue());
    checkBoth(*environment, "scalar field environment rejects a new accessor definition", false);
    accessor.erase();
    checkBoth(*environment, "scalar field environment restores after accessor removal", true);

    // Unknown code can install an inherited setter before a later object is
    // allocated. A fresh allocation does not restore that closed environment.
    const std::string beforeFresh = R"mlir(
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %nil = ctjs.constant #ctjs.undefined
  %key = ctjs.constant #ctjs.string<"value">
  %bit = ctjs.truthy %q
)mlir";
    const std::string freshField = R"mlir(
  %fresh = ctjs.create_object {ctnative.object_identity, mutation_target}
  ctjs.set_property %fresh[%key], %one {ctnative.object_field_group = 7 : i64}
)mlir";
    const std::string freshRead = R"mlir(
  %observed = ctjs.get_property %fresh[%key]
      {check, ctnative.object_field_group = 7 : i64}
)mlir";
    const std::string unknownCall = "  %unknown = ctjs.call %p(%nil) {unknown_effect}\n";
    const std::string unknownConstructor = "  %unknown = ctjs.construct %p(%p) {unknown_effect}\n";
    struct effectRow {
        const char * what;
        std::string body;
        bool intoThen = false;
    };
    const std::vector<effectRow> effectRows = {
        {"unknown call before fresh allocation",
         beforeFresh + freshField + freshRead + unknownCall},
        {"unknown constructor before fresh allocation",
         beforeFresh + freshField + freshRead + unknownConstructor},
        {"unknown branch effect before later fresh allocation",
         beforeFresh + "  scf.if %bit {\n  } {branch_target}\n" + freshField + freshRead +
             unknownCall,
         true},
        {"safe sibling after an unknown effect in the other arm",
         beforeFresh + freshField + "  scf.if %bit {\n" + unknownCall + "  } else {\n" + freshRead +
             "  } {branch_target}\n"},
    };
    for (const effectRow & r : effectRows) {
        auto source = mlir::parseSourceString<mlir::ModuleOp>(
            prologue() + r.body + "  ctjs.return %one\n}\n", &context);
        if (!source) {
            std::printf("FAIL %s: unknown-effect fixture did not parse\n", r.what);
            ++failures;
            continue;
        }
        mlir::Operation * effect = nullptr;
        mlir::Operation * target = nullptr;
        mlir::scf::IfOp branch;
        source->walk([&](mlir::Operation * op) {
            if (op->hasAttr("unknown_effect")) { effect = op; }
            if (op->hasAttr("mutation_target")) { target = op; }
            if (op->hasAttr("branch_target")) { branch = llvm::cast<mlir::scf::IfOp>(op); }
        });
        if (branch) {
            target =
                r.intoThen ? branch.getThenRegion().front().getTerminator() : branch.getOperation();
        }
        if (!effect || !target || !effect->getNextNode()) {
            std::printf("FAIL %s: unknown-effect fixture lost its source positions\n", r.what);
            ++failures;
            continue;
        }
        auto * restoreBefore = effect->getNextNode();
        checkBoth(*source, (std::string{r.what} + " before live mutation").c_str(), true);
        effect->moveBefore(target);
        checkBoth(*source, (std::string{r.what} + " after effect moved earlier").c_str(), false);
        effect->moveBefore(restoreBefore);
        checkBoth(*source, (std::string{r.what} + " after exact source restoration").c_str(), true);
    }
}

// Comparison-only identities need their own complete producer/use census.
// These expectations are attached to source allocations, never inferred from
// field schema numbers or another comparison operand's type. Preparation may
// add annotations but must preserve every allocation, scalar write and operand.

} // namespace ctcompile::test::type_inference
