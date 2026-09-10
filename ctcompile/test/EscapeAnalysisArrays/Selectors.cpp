#include "Harness.h"

namespace ctcompile::test::escape::arrays {

void checkOpaqueEntryTransport(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %key = ctjs.constant #ctjs.string<\"child\">\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string forward =
        "  cf.br ^next(%p, %q, %a : !ctjs.value, !ctjs.value, !ctjs.value)\n"
        "^next(%opaque: !ctjs.value, %other: !ctjs.value, %base: !ctjs.value):\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::vector<contents_row> rows = {
        {.what = "opaque entry aliases survive two reordered raw register vectors",
         .body = values + forward +
                 "  cf.br ^last(%other, %opaque, %base : !ctjs.value, !ctjs.value, !ctjs.value)\n"
                 "^last(%second: !ctjs.value, %first: !ctjs.value, %array: !ctjs.value):\n"
                 "  %flag = ctjs.truthy %first\n"
                 "  cf.cond_br %flag, ^yes, ^no\n^yes:\n" +
                 done + "^no:\n" + done,
         .arrays = "a:[x] | a:[x]",
         .exit = "zero -> {}; zero -> {}"},
        {.what = "a known and opaque join value may both be tested without merging heap origins",
         .body = values +
                 "  %flag = ctjs.truthy %p\n"
                 "  cf.cond_br %flag, ^join(%x : !ctjs.value), "
                 "^join(%q : !ctjs.value)\n"
                 "^join(%selected: !ctjs.value):\n"
                 "  %test = ctjs.truthy %selected\n" +
                 done,
         .arrays = "a:[x] | a:[x]",
         .exit = "zero -> {}; zero -> {}"},
        {.what = "forwarded opaque entries never become initializer contents",
         .body = values + forward + "  %bad = ctjs.create_array [%opaque]\n" + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "forwarded opaque entries never become appended contents",
         .body = values + forward + "  ctjs.append %opaque to %base\n" + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "forwarded opaque entries never become replacement contents",
         .body = values + forward + "  ctjs.set_property %base[%zero], %opaque\n" + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "forwarded opaque entries never become own-property contents",
         .body = values + forward + "  ctjs.set_property %x[%key], %opaque\n" + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "a forwarded opaque array base remains unknown",
         .body = values + forward + "  %read = ctjs.get_property %opaque[%zero]\n" + done,
         .failure = ArrayContentsFailure::UnknownArray},
        {.what = "a forwarded opaque index remains unknown",
         .body = values + forward + "  %read = ctjs.get_property %base[%opaque]\n" + done,
         .failure = ArrayContentsFailure::UnknownIndex},
        {.what = "a forwarded opaque property key remains unknown",
         .body = values + forward + "  ctjs.set_property %x[%opaque], %zero\n" + done,
         .failure = ArrayContentsFailure::UnknownPropertyKey},
        {.what = "a forwarded opaque deletion key remains unknown",
         .body = values + forward + "  ctjs.delete_property %x[%opaque]\n" + done,
         .failure = ArrayContentsFailure::UnknownPropertyKey},
        {.what = "a forwarded opaque copy source cannot lend own-data evidence",
         .body = values + forward + "  ctjs.copy_props %opaque into %x\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "a forwarded opaque copy target cannot borrow a local owner",
         .body = values + forward + "  ctjs.copy_props %x into %opaque\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "a forwarded opaque return remains outside complete retention",
         .body = values + forward + "  ctjs.return %opaque\n",
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "a forwarded opaque root remains outside the matched frame proof",
         .body = "  %frame = ctjs.frame_enter 8\n" + values + forward +
                 "  ctjs.root %opaque in %frame\n  ctjs.frame_exit %frame\n" + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "opaque forwarding does not authorize an unknown effect",
         .body = values + forward + "  \"test.effect\"(%opaque) : (!ctjs.value) -> ()\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "opaque forwarding does not authorize global publication",
         .body = values + forward + "  ctjs.store_global \"held\", %opaque\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "opaque forwarding does not authorize a call",
         .body = values + forward + "  %called = ctjs.call %opaque(%base)\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "an unsupported producer cannot mint an opaque forwarding origin",
         .body = values +
                 "  %bad = \"test.value\"() : () -> !ctjs.value\n"
                 "  cf.br ^next(%bad : !ctjs.value)\n"
                 "^next(%unused: !ctjs.value):\n" +
                 done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "an opaque branch alternative cannot borrow the known alternative's write",
         .body = values +
                 "  %flag = ctjs.truthy %p\n"
                 "  cf.cond_br %flag, ^join(%x : !ctjs.value), "
                 "^join(%q : !ctjs.value)\n"
                 "^join(%selected: !ctjs.value):\n"
                 "  ctjs.set_property %a[%zero], %selected\n" +
                 done,
         .failure = ArrayContentsFailure::UnknownValue},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const contents_row & expected) {
        checkArrayContents(module, expected);
        const bool complete = expected.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(module, {.what = expected.what,
                                                .body = expected.body,
                                                .discharged = complete ? "x" : "",
                                                .complete = complete});
    };
    for (const contents_row & expected : rows) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.body + "}\n", &context);
        if (module) {
            check(*module, expected);
        } else {
            fail(row{.what = expected.what, .body = expected.body, .expected = ""},
                 "the opaque forwarding fixture did not parse");
        }
    }

    // Extra unused entry arguments cost one seed and one opaque entry in the
    // only conditional snapshot. Pin both charges independently of whatever
    // completion budget an accidentally uncharged implementation reports.
    std::string widePrologue = kPrologue;
    std::string extraArguments;
    for (unsigned i = 0; i < 32; ++i) {
        extraArguments += ", %unused_" + std::to_string(i) + ": !ctjs.value";
    }
    widePrologue.insert(widePrologue.find(") -> !ctjs.value"), extraArguments);
    auto narrow = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + rows.front().body + "}\n", &context);
    auto wide =
        mlir::parseSourceString<mlir::ModuleOp>(widePrologue + rows.front().body + "}\n", &context);
    if (narrow && wide) {
        const auto narrowResult = computeArrayContents(*narrow->getOps<ctjs::FuncOp>().begin());
        const auto wideResult = computeArrayContents(*wide->getOps<ctjs::FuncOp>().begin());
        if (!narrowResult.complete || !wideResult.complete ||
            wideResult.work != narrowResult.work + 64) {
            fail(row{.what = "entry seeding and opaque snapshots charge every identity",
                     .body = rows.front().body,
                     .expected = ""},
                 "32 additional opaque entries did not cost 64 work units");
        }
        check(*wide, rows.front());
    } else {
        fail(row{.what = "wide opaque snapshot", .body = rows.front().body, .expected = ""},
             "the opaque snapshot charge fixture did not parse");
    }

    // Prove the live origin class after each edit, including edits to an edge
    // after a previous successful query. Input annotations never repair it.
    contents_row mutation{
        .what = "live opaque and known origins stay separate under forged completion markers",
        .body = "  %frame = ctjs.frame_enter 8\n" + values +
                "  cf.br ^next(%p, %x : !ctjs.value, !ctjs.value)\n"
                "^next(%opaque: !ctjs.value, %known: !ctjs.value):\n"
                "  ctjs.root %known in %frame\n  ctjs.frame_exit %frame\n" +
                done,
        .arrays = "a:[x]",
        .exit = "zero -> {}"};
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.body + "}\n", &context);
    unsigned liveStates = 0;
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        mlir::Block & entry = function.getBody().front();
        mlir::Block & next = function.getBody().back();
        auto branch = llvm::cast<mlir::cf::BranchOp>(entry.getTerminator());
        auto root = llvm::cast<ctjs::RootOp>(&next.front());
        const mlir::Value original = branch.getDestOperands()[1];
        mlir::OpBuilder builder(root);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        original.getDefiningOp()->setAttr("ctnative.confined", builder.getUnitAttr());
        const auto inspect = [&](ArrayContentsFailure failure) {
            mutation.failure = failure;
            check(*module, mutation);
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        root->setOperand(1, next.getArgument(0));
        inspect(ArrayContentsFailure::UnknownValue);
        root->setOperand(1, next.getArgument(1));
        inspect(ArrayContentsFailure::None);
        branch->setOperand(1, entry.getArgument(3));
        inspect(ArrayContentsFailure::UnknownValue);
        branch->setOperand(1, original);
        inspect(ArrayContentsFailure::None);
        const mlir::Value returned = next.getTerminator()->getOperand(0);
        next.getTerminator()->setOperand(0, next.getArgument(0));
        inspect(ArrayContentsFailure::UnknownValue);
        next.getTerminator()->setOperand(0, returned);
        auto published =
            ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", next.getArgument(0));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        published.erase();
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = mutation.what, .body = mutation.body, .expected = ""},
             "the live opaque forwarding fixture did not parse");
    }
    std::printf("opaque entry transport: %zu rows, %u live states, one wide snapshot, "
                "%zu retention budget cutoffs\n",
                rows.size(), liveStates, budgets);
}

void checkSelectorProducers(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string compare =
        "  %same = ctjs.compare strict_eq %p, %q {storage_test_id = \"same\"}\n";
    const std::string boolean =
        "  %boolean = ctjs.convert to_boolean %same {storage_test_id = \"boolean\"}\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %boolean\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct selector_row {
        contents_row contents;
        const char * discharged = "x";
    };
    const std::vector<selector_row> rows = {
        {.contents = {.what = "source selector producers discharge only after both overwrites",
                      .body =
                          values + compare + boolean + branch + overwrite + "^no:\n" + overwrite,
                      .arrays = "a:[zero] | a:[zero]",
                      .exit = "a -> {a}; a -> {a}"}},
        {.contents = {.what = "strict comparison observes object identity without retaining it",
                      .body = values + "  %same = ctjs.compare strict_eq %x, %p "
                                       "{storage_test_id = \"same\"}\n  ctjs.return %same\n",
                      .arrays = "a:[x]",
                      .exit = "same -> {}"}},
        {.contents = {.what = "ToBoolean returns a primitive without proving an opaque input",
                      .body = values + "  %boolean = ctjs.convert to_boolean %p "
                                       "{storage_test_id = \"boolean\"}\n  ctjs.return %boolean\n",
                      .arrays = "a:[x]",
                      .exit = "boolean -> {}"}},
        {.contents = {.what = "a stored comparison Boolean carries no operand heap origin",
                      .body = values + compare +
                              "  ctjs.set_property %a[%zero], %same\n  ctjs.return %a\n",
                      .arrays = "a:[same]",
                      .exit = "a -> {a}"}},
        {.contents = {.what = "a stored ToBoolean result carries no local operand heap origin",
                      .body = values +
                              "  %boolean = ctjs.convert to_boolean %x "
                              "{storage_test_id = \"boolean\"}\n"
                              "  ctjs.set_property %a[%zero], %boolean\n  ctjs.return %a\n",
                      .arrays = "a:[boolean]",
                      .exit = "a -> {a}"}},
        {.contents = {.what = "the derived Boolean may root while opaque registers only forward",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values + compare + boolean +
                              "  cf.br ^next(%p, %boolean : !ctjs.value, !ctjs.value)\n"
                              "^next(%opaque: !ctjs.value, %test: !ctjs.value):\n"
                              "  ctjs.root %test in %frame\n  ctjs.frame_exit %frame\n"
                              "  ctjs.return %test\n",
                      .arrays = "a:[x]",
                      .exit = "boolean -> {}"}},
        {.contents = {.what = "a selector on known and opaque joins keeps operand origins apart",
                      .body = values + compare +
                              "  %flag = ctjs.truthy %same\n"
                              "  cf.cond_br %flag, ^join(%x : !ctjs.value), "
                              "^join(%p : !ctjs.value)\n"
                              "^join(%selected: !ctjs.value):\n"
                              "  %boolean = ctjs.convert to_boolean %selected\n" +
                              done,
                      .arrays = "a:[x] | a:[x]",
                      .exit = "zero -> {}; zero -> {}"}},
        {.contents = {.what = "the final selector arm preserves its retained child",
                      .body = values + compare + boolean + branch + overwrite +
                              "^no:\n  ctjs.return %a\n",
                      .arrays = "a:[zero] | a:[x]",
                      .exit = "a -> {a}; a -> {a,x}"},
         .discharged = ""},
        {.contents = {.what = "self equality cannot prune an unsupported structural arm",
                      .body = values + "  %same = ctjs.compare strict_eq %x, %x\n" + boolean +
                              branch + done + "^no:\n  ctjs.store_global \"held\", %a\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a comparison Boolean is not an exact array index",
                      .body = values + compare + "  %read = ctjs.get_property %a[%same]\n" + done,
                      .failure = ArrayContentsFailure::UnknownIndex}},
        {.contents = {.what = "a converted Boolean is not an exact own String key",
                      .body = values + compare + boolean +
                              "  ctjs.set_property %x[%boolean], %zero\n" + done,
                      .failure = ArrayContentsFailure::UnknownPropertyKey}},
        {.contents = {.what = "a comparison Boolean cannot become a container",
                      .body =
                          values + compare + "  %read = ctjs.get_property %same[%zero]\n" + done,
                      .failure = ArrayContentsFailure::UnknownArray}},
        {.contents = {.what = "a converted Boolean cannot become a copy endpoint",
                      .body = values + compare + boolean + "  ctjs.copy_props %boolean into %x\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a comparison never authorizes returning its opaque operand",
                      .body = values + compare + "  ctjs.return %p\n",
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "ToBoolean never authorizes storing its opaque operand",
                      .body = values +
                              "  %boolean = ctjs.convert to_boolean %p\n"
                              "  ctjs.append %p to %a\n" +
                              done,
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "ToBoolean never authorizes rooting its opaque operand",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values +
                              "  %boolean = ctjs.convert to_boolean %p\n"
                              "  ctjs.root %p in %frame\n  ctjs.frame_exit %frame\n" +
                              done,
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "a primitive Boolean does not authorize unknown effects",
                      .body = values + compare + boolean +
                              "  \"test.effect\"(%boolean) : (!ctjs.value) -> ()\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "safe selectors cannot conceal an unsupported operand producer",
                      .body = values +
                              "  %bad = \"test.value\"() : () -> !ctjs.value\n"
                              "  %same = ctjs.compare strict_eq %bad, %p\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const selector_row & expected) {
        checkArrayContents(module, expected.contents);
        const bool complete = expected.contents.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(module, {.what = expected.contents.what,
                                                .body = expected.contents.body,
                                                .discharged = complete ? expected.discharged : "",
                                                .complete = complete});
    };
    const auto parseAndCheck = [&](const selector_row & expected) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
        if (module) {
            check(*module, expected);
        } else {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the selector producer fixture did not parse");
        }
    };
    for (const auto & expected : rows) { parseAndCheck(expected); }
    // Inspect every other enum kind, including apparently harmless primitive
    // cases: this increment proves no loose equality or coercion behavior.
    const std::vector<std::string> comparisons = {"eq", "lt", "le", "gt", "ge"};
    for (const auto & kind : comparisons) {
        parseAndCheck(
            {.contents = {.what = "all coercing comparison kinds refuse local and opaque values",
                          .body = values + "  %bad = ctjs.compare " + kind + " %x, %p\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}});
    }
    const std::vector<std::string> conversions = {"to_number", "to_string", "to_primitive",
                                                  "to_property_key", "to_object"};
    for (const auto & kind : conversions) {
        parseAndCheck(
            {.contents = {.what = "all non-Boolean conversions refuse opaque values",
                          .body = values + "  %bad = ctjs.convert " + kind + " %p\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}});
    }

    // Every additional primitive origin costs its producer visit and its
    // copied entry in the one path snapshot, even when no later use needs it.
    selector_row wide = rows.front();
    std::string extras;
    for (unsigned i = 0; i < 32; ++i) {
        extras +=
            "  %extra_" + std::to_string(i) +
            (i % 2 == 0 ? " = ctjs.compare strict_eq %p, %q\n" : " = ctjs.convert to_boolean %p\n");
    }
    wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
    auto narrowModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + rows.front().contents.body + "}\n", &context);
    auto wideModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + wide.contents.body + "}\n", &context);
    if (narrowModule && wideModule) {
        const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
        const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
            fail(row{.what = "selector producers and snapshots charge every primitive origin",
                     .body = wide.contents.body,
                     .expected = ""},
                 "32 extra Boolean origins did not cost 64 work units");
        }
        check(*wideModule, wide);
    } else {
        fail(row{.what = "wide selector snapshot", .body = wide.contents.body, .expected = ""},
             "the selector snapshot charge fixture did not parse");
    }

    selector_row mutation = rows.front();
    mutation.contents.what = "live selector kinds and uses defeat stale forged completion markers";
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.contents.body + "}\n", &context);
    unsigned liveStates = 0;
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::CompareOp comparison;
        ctjs::ConvertOp conversion;
        ctjs::CreateObjectOp child;
        module->walk([&](ctjs::CompareOp op) { comparison = op; });
        module->walk([&](ctjs::ConvertOp op) { conversion = op; });
        module->walk([&](ctjs::CreateObjectOp op) { child = op; });
        mlir::OpBuilder builder(comparison);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        child->setAttr("ctnative.confined", builder.getUnitAttr());
        const auto inspect = [&](ArrayContentsFailure failure) {
            mutation.contents.failure = failure;
            check(*module, mutation);
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        comparison.setKindAttr(ctjs::CompareKindAttr::get(&context, ctjs::CompareKind::Eq));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        comparison.setKindAttr(ctjs::CompareKindAttr::get(&context, ctjs::CompareKind::StrictEq));
        inspect(ArrayContentsFailure::None);
        conversion.setKindAttr(ctjs::ConvertKindAttr::get(&context, ctjs::ConvertKind::ToNumber));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        conversion.setKindAttr(ctjs::ConvertKindAttr::get(&context, ctjs::ConvertKind::ToBoolean));
        inspect(ArrayContentsFailure::None);
        comparison->setOperand(0, child.getResult());
        inspect(ArrayContentsFailure::None);
        mlir::Block & last = function.getBody().back();
        auto store = llvm::cast<ctjs::SetPropertyOp>(&last.front());
        const mlir::Value replacement = store.getValue();
        store->setOperand(2, child.getResult());
        mutation.contents.arrays = "a:[zero] | a:[x]";
        mutation.contents.exit = "a -> {a}; a -> {a,x}";
        mutation.discharged = "";
        inspect(ArrayContentsFailure::None);
        store->setOperand(2, function.getBody().front().getArgument(3));
        inspect(ArrayContentsFailure::UnknownValue);
        store->setOperand(2, replacement);
        mutation.contents.arrays = rows.front().contents.arrays;
        mutation.contents.exit = rows.front().contents.exit;
        mutation.discharged = "x";
        inspect(ArrayContentsFailure::None);
        builder.setInsertionPoint(last.getTerminator());
        auto published = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held",
                                                     function.getBody().front().getArgument(3));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        published.erase();
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live selector producer fixture did not parse");
    }
    std::printf("selector producers: %zu rows, %u live states, one wide snapshot, "
                "%zu retention budget cutoffs\n",
                rows.size() + comparisons.size() + conversions.size(), liveStates, budgets);
}

void checkLogicalNegation(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string negate = "  %negated = ctjs.unary not %p {storage_test_id = \"negated\"}\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %negated\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct negation_row {
        contents_row contents;
        const char * discharged = "x";
    };
    const std::vector<negation_row> rows = {
        {.contents = {.what = "negation releases a stored child only after both overwrites",
                      .body = values + negate + branch + overwrite + "^no:\n" + overwrite,
                      .arrays = "a:[zero] | a:[zero]",
                      .exit = "a -> {a}; a -> {a}"}},
        {.contents = {.what = "negating a local object returns a primitive without its origin",
                      .body = values + "  %negated = ctjs.unary not %x "
                                       "{storage_test_id = \"negated\"}\n"
                                       "  ctjs.return %negated\n",
                      .arrays = "a:[x]",
                      .exit = "negated -> {}"}},
        {.contents = {.what = "negating an opaque entry returns an independent Boolean",
                      .body = values + negate + "  ctjs.return %negated\n",
                      .arrays = "a:[x]",
                      .exit = "negated -> {}"}},
        {.contents = {.what = "a stored negation carries no operand heap origin",
                      .body = values + negate +
                              "  ctjs.set_property %a[%zero], %negated\n  ctjs.return %a\n",
                      .arrays = "a:[negated]",
                      .exit = "a -> {a}"}},
        {.contents = {.what = "a forwarded negation may root while its opaque input only forwards",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values + negate +
                              "  cf.br ^next(%p, %negated : !ctjs.value, !ctjs.value)\n"
                              "^next(%opaque: !ctjs.value, %test: !ctjs.value):\n"
                              "  ctjs.root %test in %frame\n  ctjs.frame_exit %frame\n"
                              "  ctjs.return %test\n",
                      .arrays = "a:[x]",
                      .exit = "negated -> {}"}},
        {.contents = {.what = "negating a local or opaque join does not merge their origins",
                      .body = values + "  %flag = ctjs.truthy %p\n"
                                       "  cf.cond_br %flag, ^join(%x : !ctjs.value), "
                                       "^join(%p : !ctjs.value)\n"
                                       "^join(%selected: !ctjs.value):\n"
                                       "  %negated = ctjs.unary not %selected "
                                       "{storage_test_id = \"negated\"}\n"
                                       "  ctjs.return %negated\n",
                      .arrays = "a:[x] | a:[x]",
                      .exit = "negated -> {}; negated -> {}"}},
        {.contents = {.what = "negation does not erase a saved child's retention identity",
                      .body = values + "  %saved = ctjs.get_property %a[%zero]\n"
                                       "  %negated = ctjs.unary not %saved\n"
                                       "  ctjs.set_property %a[%zero], %negated\n"
                                       "  ctjs.return %saved\n",
                      .arrays = "a:[ctjs.unary]",
                      .reads = "a[0]=x",
                      .exit = "x -> {x}"},
         .discharged = ""},
        {.contents = {.what = "negating zero cannot prune an unsupported untaken arm",
                      .body = values + "  %negated = ctjs.unary not %zero\n" + branch + done +
                              "^no:\n  ctjs.store_global \"held\", %a\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a negation Boolean is not an exact array index",
                      .body = values + negate + "  %read = ctjs.get_property %a[%negated]\n" + done,
                      .failure = ArrayContentsFailure::UnknownIndex}},
        {.contents = {.what = "a negation Boolean is not an own String key",
                      .body = values + negate + "  ctjs.set_property %x[%negated], %zero\n" + done,
                      .failure = ArrayContentsFailure::UnknownPropertyKey}},
        {.contents = {.what = "a negation Boolean is not a copy endpoint",
                      .body = values + negate + "  ctjs.copy_props %negated into %x\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "negation does not authorize returning its opaque operand",
                      .body = values + negate + "  ctjs.return %p\n",
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "negation does not authorize storing its opaque operand",
                      .body = values + negate + "  ctjs.append %p to %a\n" + done,
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "negation does not authorize rooting its opaque operand",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values + negate +
                              "  ctjs.root %p in %frame\n  ctjs.frame_exit %frame\n" + done,
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "negation cannot conceal an unsupported operand producer",
                      .body = values +
                              "  %bad = \"test.value\"() : () -> !ctjs.value\n"
                              "  %negated = ctjs.unary not %bad\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a negation result does not authorize unknown effects",
                      .body = values + negate +
                              "  \"test.effect\"(%negated) : (!ctjs.value) -> ()\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a negation Boolean cannot become a container",
                      .body =
                          values + negate + "  %read = ctjs.get_property %negated[%zero]\n" + done,
                      .failure = ArrayContentsFailure::UnknownArray}},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const negation_row & expected) {
        checkArrayContents(module, expected.contents);
        const bool complete = expected.contents.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(module, {.what = expected.contents.what,
                                                .body = expected.contents.body,
                                                .discharged = complete ? expected.discharged : "",
                                                .complete = complete});
    };
    const auto parse = [&](const negation_row & expected) {
        return mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
    };
    const auto parseAndCheck = [&](const negation_row & expected) {
        if (auto module = parse(expected)) {
            check(*module, expected);
        } else {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the logical negation fixture did not parse");
        }
    };
    for (const auto & expected : rows) { parseAndCheck(expected); }
    // Preserve all five historical controls. TypeOf/Void now have independent
    // primitive-result proofs; no arithmetic kind borrows the Boolean proof.
    const std::vector<std::string> otherKinds = {"neg", "plus", "bitnot", "typeof", "void"};
    for (const auto & kind : otherKinds) {
        const bool total = kind == "typeof" || kind == "void";
        parseAndCheck({.contents = {.what = "other unary kinds require their own result proof",
                                    .body = values + "  %bad = ctjs.unary " + kind + " %p\n" + done,
                                    .failure = total ? ArrayContentsFailure::None
                                                     : ArrayContentsFailure::UnsupportedOperation,
                                    .arrays = total ? "a:[x]" : "",
                                    .exit = total ? "zero -> {}" : ""}});
    }

    negation_row wide = rows.front();
    std::string extras;
    for (unsigned i = 0; i < 32; ++i) {
        extras += "  %extra_" + std::to_string(i) + " = ctjs.unary not %p\n";
    }
    wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
    auto narrowModule = parse(rows.front());
    auto wideModule = parse(wide);
    if (narrowModule && wideModule) {
        const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
        const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
            fail(row{.what = "negation snapshots charge every primitive origin",
                     .body = wide.contents.body,
                     .expected = ""},
                 "32 extra negations did not cost one producer and one snapshot visit each");
        }
        check(*wideModule, wide);
    } else {
        fail(row{.what = "wide negation snapshot", .body = wide.contents.body, .expected = ""},
             "the logical negation snapshot fixture did not parse");
    }

    negation_row mutation = rows.front();
    mutation.contents.what = "live negation edits defeat stale forged completion markers";
    unsigned liveStates = 0;
    if (auto module = parse(mutation)) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::UnaryOp unary;
        ctjs::CreateObjectOp child;
        module->walk([&](ctjs::UnaryOp op) { unary = op; });
        module->walk([&](ctjs::CreateObjectOp op) { child = op; });
        mlir::OpBuilder builder(unary);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        child->setAttr("ctnative.confined", builder.getUnitAttr());
        const auto inspect = [&](ArrayContentsFailure failure) {
            mutation.contents.failure = failure;
            check(*module, mutation);
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Neg));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Not));
        inspect(ArrayContentsFailure::None);
        unary->setOperand(0, child.getResult());
        inspect(ArrayContentsFailure::None);
        mlir::Block & last = function.getBody().back();
        auto store = llvm::cast<ctjs::SetPropertyOp>(&last.front());
        const mlir::Value replacement = store.getValue();
        store->setOperand(2, child.getResult());
        mutation.contents.arrays = "a:[zero] | a:[x]";
        mutation.contents.exit = "a -> {a}; a -> {a,x}";
        mutation.discharged = "";
        inspect(ArrayContentsFailure::None);
        store->setOperand(2, function.getBody().front().getArgument(3));
        inspect(ArrayContentsFailure::UnknownValue);
        store->setOperand(2, replacement);
        mutation.contents.arrays = rows.front().contents.arrays;
        mutation.contents.exit = rows.front().contents.exit;
        mutation.discharged = "x";
        inspect(ArrayContentsFailure::None);
        builder.setInsertionPoint(last.getTerminator());
        auto published = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held",
                                                     function.getBody().front().getArgument(3));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        published.erase();
        inspect(ArrayContentsFailure::None);
        unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::TypeOf));
        inspect(ArrayContentsFailure::None);
        unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Not));
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live logical negation fixture did not parse");
    }
    std::printf("logical negation: %zu rows, %u live states, one wide snapshot, "
                "%zu retention budget cutoffs\n",
                rows.size() + otherKinds.size(), liveStates, budgets);
}

void checkTotalUnaryProducers(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct unary_row {
        contents_row contents;
        const char * discharged = "x";
    };
    for (const std::string kind : {"typeof", "void"}) {
        const std::string operation = "  %produced = ctjs.unary " + kind;
        const std::string produce = operation + " %p {storage_test_id = \"produced\"}\n";
        const ctjs::UnaryKind originalKind =
            kind == "typeof" ? ctjs::UnaryKind::TypeOf : ctjs::UnaryKind::Void;
        const std::vector<unary_row> rows = {
            {.contents = {.what = "total unary results do not prune either overwrite arm",
                          .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                          .arrays = "a:[zero] | a:[zero]",
                          .exit = "a -> {a}; a -> {a}"}},
            {.contents = {.what = "an opaque operand produces an independent primitive terminal",
                          .body = values + produce + "  ctjs.return %produced\n",
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "a local operand is not retained by a unary result",
                          .body = values + operation +
                                  " %x {storage_test_id = \"produced\"}\n"
                                  "  ctjs.return %produced\n",
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "stored total unary results carry no operand object identity",
                          .body = values + produce +
                                  "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %a\n",
                          .arrays = "a:[produced]",
                          .exit = "a -> {a}"}},
            {.contents = {.what = "a forwarded primitive may root while its opaque operand cannot",
                          .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                                  "  cf.br ^next(%p, %produced : !ctjs.value, !ctjs.value)\n"
                                  "^next(%opaque: !ctjs.value, %result: !ctjs.value):\n"
                                  "  ctjs.root %result in %frame\n  ctjs.frame_exit %frame\n"
                                  "  ctjs.return %result\n",
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "total unary observes a local or opaque join without aliasing it",
                          .body = values +
                                  "  %flag = ctjs.truthy %p\n"
                                  "  cf.cond_br %flag, ^join(%x : !ctjs.value), "
                                  "^join(%p : !ctjs.value)\n"
                                  "^join(%selected: !ctjs.value):\n" +
                                  operation +
                                  " %selected {storage_test_id = \"produced\"}\n"
                                  "  ctjs.return %produced\n",
                          .arrays = "a:[x] | a:[x]",
                          .exit = "produced -> {}; produced -> {}"}},
            {.contents = {.what = "a saved child still retains its original identity",
                          .body = values + "  %saved = ctjs.get_property %a[%zero]\n" + operation +
                                  " %saved {storage_test_id = \"produced\"}\n"
                                  "  ctjs.set_property %a[%zero], %produced\n"
                                  "  ctjs.return %saved\n",
                          .arrays = "a:[produced]",
                          .reads = "a[0]=x",
                          .exit = "x -> {x}"},
             .discharged = ""},
            {.contents = {.what = "a unary result is not an exact Number array index",
                          .body = values + produce + "  %read = ctjs.get_property %a[%produced]\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownIndex}},
            {.contents = {.what = "a unary String or Undefined is not a proved own String key",
                          .body = values + produce + "  ctjs.set_property %x[%produced], %zero\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownPropertyKey}},
            {.contents = {.what = "a unary result is not an own-data copy endpoint",
                          .body = values + produce + "  ctjs.copy_props %produced into %x\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a unary result is not a proved container",
                          .body = values + produce +
                                  "  %read = ctjs.get_property %produced[%zero]\n" + done,
                          .failure = ArrayContentsFailure::UnknownArray}},
            {.contents = {.what = "total unary does not authorize returning its opaque operand",
                          .body = values + produce + "  ctjs.return %p\n",
                          .failure = ArrayContentsFailure::UnknownValue}},
            {.contents = {.what = "total unary does not authorize storing its opaque operand",
                          .body = values + produce + "  ctjs.append %p to %a\n" + done,
                          .failure = ArrayContentsFailure::UnknownValue}},
            {.contents = {.what = "total unary does not authorize rooting its opaque operand",
                          .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                                  "  ctjs.root %p in %frame\n  ctjs.frame_exit %frame\n" + done,
                          .failure = ArrayContentsFailure::UnknownValue}},
            {.contents = {.what = "discarding an unsupported operand producer cannot hide it",
                          .body = values + "  %bad = \"test.value\"() : () -> !ctjs.value\n" +
                                  operation + " %bad\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a total result cannot authorize a later unknown effect",
                          .body = values + produce +
                                  "  \"test.effect\"(%produced) : (!ctjs.value) -> ()\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "void or typeof cannot erase an already-evaluated publication",
                          .body = values + "  ctjs.store_global \"held\", %x\n" + produce + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "total primitive truthiness cannot prune an unsupported arm",
                          .body = values + operation + " %zero\n" + branch + done +
                                  "^no:\n  ctjs.store_global \"held\", %a\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a later retained arm prevents Stored refinement",
                          .body =
                              values + produce + branch + overwrite + "^no:\n  ctjs.return %a\n",
                          .arrays = "a:[zero] | a:[x]",
                          .exit = "a -> {a}; a -> {a,x}"},
             .discharged = ""},
            {.contents = {.what = "a literal operand does not infer the result property key",
                          .body = values + operation +
                                  " %zero\n"
                                  "  ctjs.set_property %x[%produced], %zero\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownPropertyKey}},
        };
        std::size_t budgets = 0;
        const auto check = [&](mlir::ModuleOp module, const unary_row & expected) {
            checkArrayContents(module, expected.contents);
            const bool complete = expected.contents.failure == ArrayContentsFailure::None;
            budgets +=
                checkArrayRetention(module, {.what = expected.contents.what,
                                             .body = expected.contents.body,
                                             .discharged = complete ? expected.discharged : "",
                                             .complete = complete});
        };
        const auto parse = [&](const unary_row & expected) {
            return mlir::parseSourceString<mlir::ModuleOp>(
                std::string{kPrologue} + expected.contents.body + "}\n", &context);
        };
        for (const auto & expected : rows) {
            if (auto module = parse(expected)) {
                check(*module, expected);
            } else {
                fail(row{.what = expected.contents.what,
                         .body = expected.contents.body,
                         .expected = ""},
                     "the total unary fixture did not parse");
            }
        }
        unary_row wide = rows.front();
        std::string extras;
        for (unsigned i = 0; i < 32; ++i) {
            extras += "  %extra_" + std::to_string(i) + " = ctjs.unary " + kind + " %p\n";
        }
        wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
        auto narrowModule = parse(rows.front());
        auto wideModule = parse(wide);
        if (narrowModule && wideModule) {
            const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
            const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
            if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
                fail(row{.what = "total unary snapshots charge every primitive origin",
                         .body = wide.contents.body,
                         .expected = ""},
                     "32 extra unary results did not cost one producer and one snapshot each");
            }
            check(*wideModule, wide);
        } else {
            fail(row{.what = "wide total unary snapshot",
                     .body = wide.contents.body,
                     .expected = ""},
                 "the total unary snapshot fixture did not parse");
        }

        unary_row mutation = rows.front();
        mutation.contents.what = "live total unary edits defeat stale forged completion markers";
        unsigned liveStates = 0;
        if (auto module = parse(mutation)) {
            ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
            ctjs::UnaryOp unary;
            ctjs::CreateObjectOp child;
            module->walk([&](ctjs::UnaryOp op) { unary = op; });
            module->walk([&](ctjs::CreateObjectOp op) { child = op; });
            mlir::OpBuilder builder(unary);
            function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
            function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
            child->setAttr("ctnative.confined", builder.getUnitAttr());
            const auto inspect = [&](ArrayContentsFailure failure) {
                mutation.contents.failure = failure;
                check(*module, mutation);
                ++liveStates;
            };
            inspect(ArrayContentsFailure::None);
            for (const auto coercing :
                 {ctjs::UnaryKind::Neg, ctjs::UnaryKind::Plus, ctjs::UnaryKind::BitNot}) {
                unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, coercing));
                inspect(ArrayContentsFailure::UnsupportedOperation);
            }
            unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, originalKind));
            inspect(ArrayContentsFailure::None);
            unary->setOperand(0, child.getResult());
            inspect(ArrayContentsFailure::None);
            mlir::Block & last = function.getBody().back();
            auto store = llvm::cast<ctjs::SetPropertyOp>(&last.front());
            const mlir::Value replacement = store.getValue();
            store->setOperand(2, child.getResult());
            mutation.contents.arrays = "a:[zero] | a:[x]";
            mutation.contents.exit = "a -> {a}; a -> {a,x}";
            mutation.discharged = "";
            inspect(ArrayContentsFailure::None);
            store->setOperand(2, function.getBody().front().getArgument(3));
            inspect(ArrayContentsFailure::UnknownValue);
            store->setOperand(2, replacement);
            mutation.contents.arrays = rows.front().contents.arrays;
            mutation.contents.exit = rows.front().contents.exit;
            mutation.discharged = "x";
            inspect(ArrayContentsFailure::None);
            builder.setInsertionPoint(last.getTerminator());
            auto published = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held",
                                                         function.getBody().front().getArgument(3));
            inspect(ArrayContentsFailure::UnsupportedOperation);
            published.erase();
            inspect(ArrayContentsFailure::None);
            unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Not));
            inspect(ArrayContentsFailure::None);
        } else {
            fail(
                row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
                "the live total unary fixture did not parse");
        }
        std::printf("total unary %s: %zu rows, %u live states, one wide snapshot, "
                    "%zu retention budget cutoffs\n",
                    kind.c_str(), rows.size(), liveStates, budgets);
    }
}

} // namespace ctcompile::test::escape::arrays
