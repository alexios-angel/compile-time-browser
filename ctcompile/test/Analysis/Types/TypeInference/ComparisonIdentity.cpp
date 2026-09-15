#include "Tests.h"

namespace ctcompile::test::type_inference {

void checkComparisonIdentityPreparation(mlir::ModuleOp module, const char * what) {
    using namespace ctcompile;
    std::vector<mlir::Operation *> operations;
    std::vector<std::vector<mlir::Value>> operands;
    module.walk([&](mlir::Operation * op) {
        operations.push_back(op);
        operands.emplace_back(op->operand_begin(), op->operand_end());
    });
    ctnative::prepareNativeObjectIdentities(module);
    std::vector<mlir::Operation *> after;
    module.walk([&](mlir::Operation * op) { after.push_back(op); });
    if (after != operations) {
        std::printf("FAIL %s: identity preparation changed source operations\n", what);
        ++ctbrowser_test_failures;
        return;
    }
    std::vector<std::pair<int64_t, int64_t>> groups;
    unsigned allocations = 0;
    bool typed = false;
    for (size_t index = 0; index < operations.size(); ++index) {
        auto * op = operations[index];
        if (!llvm::equal(op->getOperands(), operands[index])) {
            std::printf("FAIL %s: identity preparation changed a source operand\n", what);
            ++ctbrowser_test_failures;
        }
        if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(op)) {
            ++allocations;
            auto expected = op->getAttrOfType<mlir::BoolAttr>("test_identity");
            if (!expected || op->hasAttr(ctnative::kNativeObjectIdentity) != expected.getValue()) {
                std::printf("FAIL %s: fresh allocation %u has the wrong identity proof\n", what,
                            allocations);
                ++ctbrowser_test_failures;
            }
            typed |= expected && expected.getValue() && op->hasAttr("check");
        } else if (op->hasAttr(ctnative::kNativeObjectIdentity)) {
            std::printf("FAIL %s: a non-allocation producer retained a forged identity\n", what);
            ++ctbrowser_test_failures;
        }
        if (auto expected = op->getAttrOfType<mlir::IntegerAttr>("test_field_family")) {
            const int64_t group = ctnative::nativeObjectFieldGroup(op);
            if ((group >= 0) != (expected.getInt() >= 0)) {
                std::printf("FAIL %s: scalar field has the wrong family proof\n", what);
                ++ctbrowser_test_failures;
            }
            if (expected.getInt() >= 0 && group >= 0) {
                for (const auto & [source, previous] : groups) {
                    if ((source == expected.getInt()) != (previous == group)) {
                        std::printf("FAIL %s: comparison changed scalar field family identity\n",
                                    what);
                        ++ctbrowser_test_failures;
                    }
                }
                groups.emplace_back(expected.getInt(), group);
            }
        }
    }
    if (allocations == 0) {
        std::printf("FAIL %s: identity fixture contains no fresh allocation\n", what);
        ++ctbrowser_test_failures;
    }
    if (typed) { check(module, what, "!ctnative.object_identity"); }
}

void forgeComparisonIdentityReports(mlir::ModuleOp module) {
    auto * context = module.getContext();
    module.walk([&](mlir::Operation * op) {
        op->setAttr(ctcompile::ctnative::kNativeObjectIdentity, mlir::UnitAttr::get(context));
        op->setAttr(ctcompile::ctnative::kNativeObjectFieldGroup,
                    mlir::IntegerAttr::get(mlir::IntegerType::get(context, 64), 777));
    });
}

void checkComparisonIdentityRows(mlir::MLIRContext & context) {
    const std::string prefix = R"mlir(
  %nil = ctjs.constant #ctjs.undefined
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %wide = ctjs.constant #ctjs.number<4609434218613702656>
  %field = ctjs.constant #ctjs.string<"value">
)mlir";
    const std::string fresh = "  %object = ctjs.create_object {test_identity = true, check}\n";
    const std::string refused = "  %object = ctjs.create_object {test_identity = false}\n";
    const std::string equal = "  %comparison = ctjs.compare strict_eq %object, %object\n";
    const std::string field =
        "  ctjs.set_property %object[%field], %one {test_field_family = 0 : i64}\n";
    const std::string refusedField =
        "  ctjs.set_property %object[%field], %one {test_field_family = -1 : i64}\n";
    const auto function = [&](const std::string & body, llvm::StringRef returned = "%comparison") {
        return prologue() + prefix + body + "  ctjs.return " + returned.str() + "\n}\n";
    };
    struct identityRow {
        const char * what;
        std::string source;
    };
    const std::vector<identityRow> rows{
        {"strict self-comparison proves a fresh identity", function(fresh + equal)},
        {"both strict comparison operands retain independent fresh identities",
         function(fresh + R"mlir(
  %other = ctjs.create_object {test_identity = true}
  %comparison = ctjs.compare strict_eq %object, %other
  %reversed = ctjs.compare strict_eq %other, %object
)mlir")},
        {"strict comparison does not merge equal field layouts or numeric writes",
         function(fresh + field + R"mlir(
  %other = ctjs.create_object {test_identity = true}
  ctjs.set_property %other[%field], %wide {test_field_family = 1 : i64}
  %first = ctjs.get_property %object[%field] {test_field_family = 0 : i64}
  %second = ctjs.get_property %other[%field] {test_field_family = 1 : i64}
  %comparison = ctjs.compare strict_eq %object, %other
)mlir")},
        {"a boxed comparison operand is not an origin for the independent fresh operand",
         function(fresh + "  %comparison = ctjs.compare strict_eq %object, %p\n")},
        {"root bookkeeping preserves the fresh strict-comparison proof",
         function("  %frame = ctjs.frame_enter 5\n" + fresh + "  ctjs.root %object in %frame\n" +
                  equal + "  ctjs.frame_exit %frame\n")},
        {"unrelated unknown code cannot change fieldless identity",
         function("  %ignored = ctjs.call %p(%nil)\n" + fresh + equal)},
        {"a strict-comparison field family rechecks unknown calls in its environment",
         function("  %ignored = ctjs.call %p(%nil)\n" + refused + refusedField + equal)},
        {"a strict-comparison field family rechecks unknown constructors in its environment",
         function("  %ignored = ctjs.construct %p(%p)\n" + refused + refusedField + equal)},
        {"an unobserved fresh object is not a comparison candidate", function(refused, "%nil")},
        {"ordinary fields alone do not invent a comparison candidate",
         function(refused + refusedField, "%nil")},
        {"loose equality alone does not prove comparison-only identity",
         function(refused + "  %comparison = ctjs.compare eq %object, %object\n")},
        {"relational comparison alone does not prove comparison-only identity",
         function(refused + "  %comparison = ctjs.compare lt %object, %object\n")},
        {"one strict use does not hide a loose equality use",
         function(refused + equal + "  %coercive = ctjs.compare eq %object, %p\n")},
        {"one strict use does not hide a relational use",
         function(refused + equal + "  %coercive = ctjs.compare ge %object, %p\n")},
        {"one strict use does not hide arithmetic coercion",
         function(refused + equal + "  %coercive = ctjs.binary add %object, %one\n")},
        {"one strict use does not hide truthiness outside the comparison census",
         function(refused + equal + "  %truth = ctjs.truthy %object\n")},
        {"one strict use does not hide unary coercion",
         function(refused + equal + "  %coercive = ctjs.unary plus %object\n")},
        {"unknown call arguments reject the whole comparison family",
         function(refused + equal + "  %escaped = ctjs.call %p(%nil, %object)\n")},
        {"unknown call receivers reject the whole comparison family",
         function(refused + equal + "  %escaped = ctjs.call %p(%object)\n")},
        {"using the fresh object as a callee is not identity observation",
         function(refused + equal + "  %escaped = ctjs.call %object(%nil)\n")},
        {"constructor arguments reject the whole comparison family",
         function(refused + equal + "  %escaped = ctjs.construct %p(%p, %object)\n")},
        {"public return publication rejects the whole comparison family",
         function(refused + equal, "%object")},
        {"storing an object in an array is an outgoing ownership edge",
         function(refused + equal + "  %escaped = ctjs.create_array [%object]\n")},
        {"storing an object in an ordinary property is an outgoing ownership edge",
         function(refused + equal + "  ctjs.set_property %p[%field], %object\n")},
        {"dynamic fields reject the whole comparison family",
         function(refused + equal +
                  "  ctjs.set_property %object[%p], %one {test_field_family = -1 : i64}\n")},
        {"prototype setters reject the whole comparison family", function(refused + equal + R"mlir(
  %prototype = ctjs.constant #ctjs.string<"__proto__">
  ctjs.set_property %object[%prototype], %nil {test_field_family = -1 : i64}
)mlir")},
        {"both structured alternatives keep their fresh allocation proofs", function(fresh + R"mlir(
  %other = ctjs.create_object {test_identity = true}
  %bit = ctjs.truthy %p
  %selected = scf.if %bit -> !ctjs.value {
    scf.yield %object : !ctjs.value
  } else {
    scf.yield %other : !ctjs.value
  }
  %comparison = ctjs.compare strict_eq %selected, %object
)mlir")},
        {"an execute-region yield cannot hide object publication from the use census",
         function(refused + equal + R"mlir(
  %escaped = scf.execute_region -> !ctjs.value {
    scf.yield %object : !ctjs.value
  }
  ctjs.store_global "escaped_identity", %escaped
)mlir")},
        {"an untracked execute-region result is not a proved comparison alias",
         function(refused + equal + R"mlir(
  %alias = scf.execute_region -> !ctjs.value {
    scf.yield %object : !ctjs.value
  }
  %observed = ctjs.compare strict_eq %alias, %object
)mlir")},
        {"both tracked if-yields preserve a same-origin strict-comparison alias",
         function(fresh + R"mlir(
  %bit = ctjs.truthy %p
  %alias = scf.if %bit -> !ctjs.value {
    scf.yield %object : !ctjs.value
  } else {
    scf.yield %object : !ctjs.value
  }
  %comparison = ctjs.compare strict_eq %alias, %object
)mlir")},
        {"tracked if-yields expose publication to the complete object use census",
         function(refused + equal + R"mlir(
  %bit = ctjs.truthy %p
  %escaped = scf.if %bit -> !ctjs.value {
    scf.yield %object : !ctjs.value
  } else {
    scf.yield %object : !ctjs.value
  }
  ctjs.store_global "escaped_identity", %escaped
)mlir")},
        {"unknown incoming parameters cannot acquire a fresh allocation's proof",
         function(refused + R"mlir(
  %bit = ctjs.truthy %q
  %selected = scf.if %bit -> !ctjs.value {
    scf.yield %object : !ctjs.value
  } else {
    scf.yield %p : !ctjs.value
  }
  %comparison = ctjs.compare strict_eq %selected, %object
)mlir")},
        {"a primitive incoming alternative does not become an object identity",
         function(refused + R"mlir(
  %bit = ctjs.truthy %p
  %selected = scf.if %bit -> !ctjs.value {
    scf.yield %object : !ctjs.value
  } else {
    scf.yield %nil : !ctjs.value
  }
  %comparison = ctjs.compare strict_eq %selected, %object
)mlir")},
        {"an unknown call result does not become an object identity", function(refused + R"mlir(
  %unknown = ctjs.call %p(%nil)
  %bit = ctjs.truthy %q
  %selected = scf.if %bit -> !ctjs.value {
    scf.yield %object : !ctjs.value
  } else {
    scf.yield %unknown : !ctjs.value
  }
  %comparison = ctjs.compare strict_eq %selected, %object
)mlir")},
        {"an unknown constructor result does not become an object identity",
         function(refused + R"mlir(
  %unknown = ctjs.construct %p(%p)
  %bit = ctjs.truthy %q
  %selected = scf.if %bit -> !ctjs.value {
    scf.yield %object : !ctjs.value
  } else {
    scf.yield %unknown : !ctjs.value
  }
  %comparison = ctjs.compare strict_eq %selected, %object
)mlir")},
        {"a closed exact direct call preserves an owning comparison alias", function(fresh + R"mlir(
  %alias = ctjs.call_direct @identity(%nil, %nil, %nil, %object)
  %comparison = ctjs.compare strict_eq %alias, %object
)mlir") + R"mlir(
ctjs.func private @identity(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value, %value: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  ctjs.return %value
}
)mlir"},
    };
    for (const auto & r : rows) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(r.source, &context);
        if (!module) {
            std::printf("FAIL %s: comparison identity fixture did not parse\n", r.what);
            ++ctbrowser_test_failures;
            continue;
        }
        checkComparisonIdentityPreparation(*module, r.what);
        forgeComparisonIdentityReports(*module);
        checkComparisonIdentityPreparation(*module, r.what);
        auto freshModule = mlir::OwningOpRef<mlir::ModuleOp>(module->clone());
        forgeComparisonIdentityReports(*freshModule);
        checkComparisonIdentityPreparation(*freshModule, r.what);
    }
    std::printf("comparison identity: %zu source rows, clean/forged/fresh preparation\n",
                rows.size());
}

void checkComparisonIdentityMutations(mlir::MLIRContext & context) {
    using namespace ctcompile;
    const auto expect = [&](mlir::ModuleOp module, bool accepted) {
        module.walk([&](mlir::Operation * op) {
            if (op->hasAttr("test_identity") && !op->hasAttr("environment_only")) {
                op->setAttr("test_identity", mlir::BoolAttr::get(&context, accepted));
            }
            if (op->hasAttr("test_field_family")) {
                op->setAttr("test_field_family",
                            mlir::IntegerAttr::get(mlir::IntegerType::get(&context, 64),
                                                   accepted ? 0 : -1));
            }
        });
    };
    unsigned states = 0;
    const auto liveAndFresh = [&](mlir::ModuleOp module, const char * what, bool accepted) {
        ++states;
        expect(module, accepted);
        forgeComparisonIdentityReports(module);
        // Clone before the old module is rechecked: both queries see forged
        // prior reports, including deliberately invalid live SSA/arity below.
        auto fresh = mlir::OwningOpRef<mlir::ModuleOp>(module.clone());
        checkComparisonIdentityPreparation(module, what);
        checkComparisonIdentityPreparation(*fresh, what);
    };
    auto module = mlir::parseSourceString<mlir::ModuleOp>(R"mlir(
module {
  ctjs.func @caller(%receiver: !ctjs.value, %new_target: !ctjs.value,
                    %callee: !ctjs.value, %p: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %field = ctjs.constant #ctjs.string<"value">
    %object = ctjs.create_object {test_identity = true, check}
    ctjs.set_property %object[%field], %one {test_field_family = 0 : i64}
    %alias = ctjs.call_direct @identity(%nil, %nil, %nil, %object, %one)
    %comparison = ctjs.compare strict_eq %alias, %object
    ctjs.return %comparison
  }
  ctjs.func private @identity(%receiver: !ctjs.value, %new_target: !ctjs.value,
                              %callee: !ctjs.value, %value: !ctjs.value,
                              %unused: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.return %value
  }
}
)mlir",
                                                          &context);
    if (!module) {
        std::printf("FAIL comparison identity mutation fixture did not parse\n");
        ++ctbrowser_test_failures;
        return;
    }
    auto caller = module->lookupSymbol<ctjs::FuncOp>("caller");
    auto helper = module->lookupSymbol<ctjs::FuncOp>("identity");
    ctjs::CallDirectOp call;
    ctjs::CompareOp comparison;
    ctjs::SetPropertyOp store;
    ctjs::CreateObjectOp object;
    caller.walk([&](ctjs::CallDirectOp found) { call = found; });
    caller.walk([&](ctjs::CompareOp found) { comparison = found; });
    caller.walk([&](ctjs::SetPropertyOp found) { store = found; });
    caller.walk([&](ctjs::CreateObjectOp found) { object = found; });
    if (!caller || !helper || !call || !comparison || !store || !object) {
        std::printf("FAIL comparison identity mutation fixture lost source operations\n");
        ++ctbrowser_test_failures;
        return;
    }
    liveAndFresh(*module, "closed comparison alias before live mutations", true);

    const auto strict = comparison.getKindAttr();
    comparison.setKindAttr(ctjs::CompareKindAttr::get(&context, ctjs::CompareKind::Eq));
    liveAndFresh(*module, "changing the only strict use removes the candidate", false);
    comparison.setKindAttr(strict);
    liveAndFresh(*module, "restoring the strict comparison restores the proof", true);

    const std::vector<mlir::Value> actuals(call->operand_begin(), call->operand_end());
    call->eraseOperands(4, 1);
    liveAndFresh(*module, "missing later actual cannot preserve a compared object argument", false);
    call->setOperands(actuals);
    liveAndFresh(*module, "restoring missing actual restores exact closed flow", true);
    std::vector<mlir::Value> surplus = actuals;
    surplus.push_back(actuals.front());
    call->setOperands(surplus);
    liveAndFresh(*module, "surplus actual cannot preserve a compared object argument", false);
    call->setOperands(actuals);
    liveAndFresh(*module, "restoring surplus actual restores exact closed flow", true);

    const auto target = call.getCalleeAttr();
    call.setCalleeAttr(mlir::FlatSymbolRefAttr::get(&context, "missing"));
    liveAndFresh(*module, "a changed unresolved direct target invalidates stale identity", false);
    call.setCalleeAttr(target);
    liveAndFresh(*module, "restoring the direct target rebuilds the identity proof", true);

    const auto visibility = helper->getAttr("sym_visibility");
    helper->setAttr("sym_visibility", mlir::StringAttr::get(&context, "public"));
    liveAndFresh(*module, "public helper visibility invalidates its old closed census", false);
    helper->setAttr("sym_visibility", visibility);
    liveAndFresh(*module, "restoring private visibility rebuilds the closed census", true);

    mlir::OpBuilder before(comparison);
    auto extra = llvm::cast<ctjs::CallDirectOp>(before.insert(call->clone()));
    extra->setOperand(3, caller.getBody().front().getArgument(3));
    liveAndFresh(*module, "one new unknown caller invalidates the complete producer census", false);
    extra.erase();
    liveAndFresh(*module, "removing the unknown caller restores all fresh producers", true);

    auto published = ctjs::StoreGlobalOp::create(before, comparison.getLoc(), "escaped_identity",
                                                 object.getResult());
    liveAndFresh(*module, "a new publication rejects an otherwise proved comparison family", false);
    published.erase();
    liveAndFresh(*module, "removing publication restores the family", true);

    const auto field = store.getKey();
    store->setOperand(1, caller.getBody().front().getArgument(3));
    liveAndFresh(*module, "a changed dynamic field key invalidates stale field groups", false);
    store->setOperand(1, field);
    liveAndFresh(*module, "restoring the ordinary field key rebuilds the family", true);

    auto key = field.getDefiningOp<ctjs::ConstantOp>();
    const auto ordinary = key.getValue();
    key.setValueAttr(ctjs::StringAttr::get(&context, "__proto__"));
    liveAndFresh(*module, "a prototype field spelling invalidates stale field groups", false);
    key.setValueAttr(ordinary);
    liveAndFresh(*module, "restoring the field spelling rebuilds the family", true);

    // A use moved before its allocation, or into a sibling region, has no
    // source origin. A clone of the invalid live IR must refuse just as the
    // already-prepared module does, without relying on parser verification.
    const std::string scoped = R"mlir(
ctjs.func @scoped(%receiver: !ctjs.value, %new_target: !ctjs.value,
                  %callee: !ctjs.value, %p: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %nil = ctjs.constant #ctjs.undefined
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %field = ctjs.constant #ctjs.string<"value">
  %condition = ctjs.truthy %p
  scf.if %condition {
    %object = ctjs.create_object {test_identity = true, check}
    ctjs.set_property %object[%field], %one {test_field_family = 0 : i64}
    %comparison = ctjs.compare strict_eq %object, %object
  } else {
  }
  ctjs.return %nil
}
)mlir";
    for (bool sibling : {false, true}) {
        auto source = mlir::parseSourceString<mlir::ModuleOp>(scoped, &context);
        if (!source) {
            std::printf("FAIL comparison identity source-scope fixture did not parse\n");
            ++ctbrowser_test_failures;
            continue;
        }
        ctjs::CreateObjectOp scopedObject;
        ctjs::CompareOp scopedComparison;
        mlir::scf::IfOp branch;
        source->walk([&](ctjs::CreateObjectOp found) { scopedObject = found; });
        source->walk([&](ctjs::CompareOp found) { scopedComparison = found; });
        source->walk([&](mlir::scf::IfOp found) { branch = found; });
        if (!scopedObject || !scopedComparison || !branch) {
            std::printf("FAIL comparison identity scope fixture lost source positions\n");
            ++ctbrowser_test_failures;
            continue;
        }
        liveAndFresh(*source, "scoped comparison before live source mutation", true);
        auto * restoreBefore = scopedComparison->getNextNode();
        if (sibling) {
            scopedComparison->moveBefore(branch.getElseRegion().front().getTerminator());
        } else {
            scopedComparison->moveBefore(scopedObject);
        }
        liveAndFresh(*source,
                     sibling ? "comparison moved into a sibling region loses its source origin"
                             : "comparison moved before allocation loses its source origin",
                     false);
        scopedComparison->moveBefore(restoreBefore);
        liveAndFresh(*source, "restoring exact comparison source scope rebuilds the proof", true);
    }
    // The field environment is its own source proof. A property operation on
    // an unrelated allocation cannot use a same-schema origin from another
    // region. Fieldless identities do not depend on that environment at all.
    for (bool withField : {false, true}) {
        const std::string environment = prologue() + R"mlir(
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %field = ctjs.constant #ctjs.string<"value">
  %object = ctjs.create_object {test_identity = true, check}
)mlir" +
                                        (withField ? "  ctjs.set_property %object[%field], %one "
                                                     "{test_field_family = 0 : i64}\n"
                                                   : "") +
                                        R"mlir(
  %comparison = ctjs.compare strict_eq %object, %object
  %condition = ctjs.truthy %p
  scf.if %condition {
    %other = ctjs.create_object {test_identity = false, environment_only}
    %read = ctjs.get_property %other[%field] {environment_read}
  } else {
  }
  ctjs.return %comparison
}
)mlir";
        for (bool sibling : {false, true}) {
            auto source = mlir::parseSourceString<mlir::ModuleOp>(environment, &context);
            if (!source) {
                std::printf("FAIL comparison field-environment scope fixture did not parse\n");
                ++ctbrowser_test_failures;
                continue;
            }
            mlir::Operation * other = nullptr;
            mlir::Operation * read = nullptr;
            mlir::scf::IfOp branch;
            source->walk([&](mlir::Operation * op) {
                if (op->hasAttr("environment_only")) { other = op; }
                if (op->hasAttr("environment_read")) { read = op; }
                if (auto found = llvm::dyn_cast<mlir::scf::IfOp>(op)) { branch = found; }
            });
            if (!other || !read || !branch) {
                std::printf("FAIL comparison field-environment fixture lost source positions\n");
                ++ctbrowser_test_failures;
                continue;
            }
            liveAndFresh(*source, "independent field environment before source mutation", true);
            auto * restoreBefore = read->getNextNode();
            read->moveBefore(sibling ? branch.getElseRegion().front().getTerminator() : other);
            // Only the source proof is queried on malformed IR. The general
            // sparse type solver is entitled to require verified SSA.
            source->walk([](mlir::Operation * op) { op->removeAttr("check"); });
            liveAndFresh(*source,
                         withField ? "unrelated malformed property origin blocks field identity"
                                   : "fieldless identity needs no unrelated property-origin proof",
                         !withField);
            read->moveBefore(restoreBefore);
            source->walk([&](mlir::Operation * op) {
                if (op->hasAttr("test_identity") && !op->hasAttr("environment_only")) {
                    op->setAttr("check", mlir::UnitAttr::get(&context));
                }
            });
            liveAndFresh(*source, "restoring unrelated property scope rebuilds field environment",
                         true);
        }
    }
    std::printf("comparison identity: %u live/fresh mutation states\n", states);
}

} // namespace ctcompile::test::type_inference
