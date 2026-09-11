#include "Harness.h"

namespace ctcompile::test::escape::arrays {

void checkBigIntPlusErrors(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %big = ctjs.constant #ctjs.bigint<\"9007199254740993\"> "
        "{storage_test_id = \"big\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string produce =
        "  %produced = ctjs.unary plus %big {storage_test_id = \"produced\"}\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct plus_row {
        contents_row contents;
        const char * discharged = "x";
    };
    const std::vector<plus_row> rows = {
        {.contents = {.what = "BigInt Plus checks both structural continuations after its error",
                      .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                      .arrays = "a:[zero] | a:[zero]",
                      .exit = "a -> {a}; a -> {a}"}},
        {.contents = {.what = "a Plus error cannot erase a later retained child",
                      .body = values + produce + "  ctjs.return %a\n",
                      .arrays = "a:[x]",
                      .exit = "a -> {a,x}"},
         .discharged = ""},
        {.contents = {.what = "a Plus error cannot erase a retained structural arm",
                      .body = values + produce + branch + overwrite + "^no:\n  ctjs.return %a\n",
                      .arrays = "a:[zero] | a:[x]",
                      .exit = "a -> {a}; a -> {a,x}"},
         .discharged = ""},
        {.contents = {.what = "the independent Plus carrier is not an original BigInt",
                      .body =
                          values + produce + "  %next = ctjs.binary add %produced, %big\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "Plus cannot supply an exact Number index after its error",
                      .body =
                          values + produce + "  %read = ctjs.get_property %a[%produced]\n" + done,
                      .failure = ArrayContentsFailure::UnknownIndex}},
        {.contents = {.what = "Plus cannot supply an exact String key after its error",
                      .body =
                          values + produce + "  ctjs.set_property %x[%produced], %zero\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "Plus cannot erase an explicit thrown child",
                      .body = values + produce + "  ctjs.throw %x\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "Plus cannot erase a handler or its unknown retained value",
                      .body = values + produce +
                              "  ctjs.push_handler ^body catch ^handler\n"
                              "^body:\n  ctjs.pop_handler\n" +
                              done + "^handler:\n  %id, %exception = ctjs.catch_land\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedControlFlow}},
    };
    unsigned rowCount = 0;
    unsigned liveStates = 0;
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const plus_row & expected) {
        checkArrayContents(module, expected.contents);
        const bool complete = expected.contents.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(module, {.what = expected.contents.what,
                                                .body = expected.contents.body,
                                                .discharged = complete ? expected.discharged : "",
                                                .complete = complete});
    };
    const auto parse = [&](const plus_row & expected) {
        return mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
    };
    const auto run = [&](const plus_row & expected) {
        if (auto module = parse(expected)) {
            check(*module, expected);
        } else {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the BigInt Plus fixture did not parse");
        }
        ++rowCount;
    };
    for (const auto & expected : rows) { run(expected); }
    for (const std::string effect :
         {"  ctjs.store_global \"held\", %x\n", "  %called = ctjs.call %p(%a)\n",
          "  \"test.effect\"(%x) : (!ctjs.value) -> ()\n"}) {
        for (const bool before : {false, true}) {
            run({.contents = {
                     .what = "independent Plus errors never authorize publication or calls",
                     .body = values + (before ? effect + produce : produce + effect) + done,
                     .failure = ArrayContentsFailure::UnsupportedOperation}});
        }
    }
    // Saved operands preserve their ORIGINAL identity when the current slot
    // changes category, including Object/opaque refusal on either incoming arm.
    for (const std::string input : {"%big", "%zero", "%x", "%p"}) {
        for (const bool reversed : {false, true}) {
            const std::string operands = reversed ? input + " : !ctjs.value), ^join(%big"
                                                  : "%big : !ctjs.value), ^join(" + input;
            const bool primitive = input == "%big" || input == "%zero";
            run({.contents = {.what = "Plus independently proves each original incoming category",
                              .body = values +
                                      "  %flag = ctjs.truthy %p\n"
                                      "  cf.cond_br %flag, ^join(" +
                                      operands +
                                      " : !ctjs.value)\n^join(%operand: !ctjs.value):\n"
                                      "  %produced = ctjs.unary plus %operand\n" +
                                      done,
                              .failure = primitive ? ArrayContentsFailure::None
                                                   : ArrayContentsFailure::UnsupportedOperation,
                              .arrays = "a:[x] | a:[x]",
                              .exit = "zero -> {}; zero -> {}"}});
        }
    }
    for (const bool savedBigInt : {false, true}) {
        const std::string saved = savedBigInt ? "%big" : "%zero";
        const std::string replacement = savedBigInt ? "%zero" : "%big";
        run({.contents = {.what = "saved own-field Plus operands survive replacement and deletion",
                          .body = values +
                                  "  %key = ctjs.constant #ctjs.string<\"value\">\n"
                                  "  ctjs.set_property %x[%key], " +
                                  saved +
                                  "\n  %saved = ctjs.get_property %x[%key]\n"
                                  "  ctjs.set_property %x[%key], " +
                                  replacement +
                                  "\n  ctjs.delete_named \"value\" from %x\n"
                                  "  cf.br ^next(%saved : !ctjs.value)\n"
                                  "^next(%operand: !ctjs.value):\n"
                                  "  %produced = ctjs.unary plus %operand\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnsupportedOperation,
                          .arrays = "a:[x]",
                          .exit = "zero -> {}",
                          .objects = "x:{}",
                          .propertyReads = savedBigInt ? "x[value]=big" : "x[value]=zero"}});
    }
    plus_row wide = rows.front();
    std::string extras;
    for (unsigned i = 0; i < 32; ++i) {
        extras += "  %extra_" + std::to_string(i) + " = ctjs.unary plus %big\n";
    }
    wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
    auto narrowModule = parse(rows.front());
    auto wideModule = parse(wide);
    if (narrowModule && wideModule) {
        const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
        const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
            fail(row{.what = "Plus errors do not prune or skip independent result snapshot charges",
                     .body = wide.contents.body,
                     .expected = ""},
                 "32 results did not charge each operation and copied origin");
        }
        check(*wideModule, wide);
    } else {
        fail(row{.what = "wide BigInt Plus snapshot", .body = wide.contents.body, .expected = ""},
             "the BigInt Plus snapshot fixture did not parse");
    }
    plus_row mutation = rows.front();
    if (auto module = parse(mutation)) {
        auto function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::UnaryOp unary;
        ctjs::CreateObjectOp child;
        ctjs::CreateArrayOp array;
        module->walk([&](ctjs::UnaryOp op) { unary = op; });
        module->walk([&](ctjs::CreateObjectOp op) { child = op; });
        module->walk([&](ctjs::CreateArrayOp op) { array = op; });
        mlir::OpBuilder builder(unary);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        child->setAttr("ctnative.confined", builder.getUnitAttr());
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*module))) {
            fail(row{.what = "BigInt Plus stale solver",
                     .body = mutation.contents.body,
                     .expected = ""},
                 "the baseline solver did not converge");
        }
        const auto inspect = [&](ArrayContentsFailure failure) {
            mutation.contents.failure = failure;
            check(*module, mutation);
            const auto verdicts = computeVerdicts(stale, function);
            const bool complete = failure == ArrayContentsFailure::None;
            if (verdicts.arrayRetentionComplete != complete ||
                verdicts.confinedStoredSites !=
                    (complete && mutation.discharged[0] != '\0' ? 1U : 0U)) {
                fail(row{.what = "live Plus origins defeat stale and forged completion",
                         .body = mutation.contents.body,
                         .expected = mutation.discharged},
                     "a stale solver authorized an unproved original operand or retained child");
            }
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        const mlir::Value input = unary.getOperand();
        for (mlir::Value invalid : {mlir::Value(child.getResult()), mlir::Value(array.getResult()),
                                    mlir::Value(function.getBody().front().getArgument(3))}) {
            unary->setOperand(0, invalid);
            inspect(ArrayContentsFailure::UnsupportedOperation);
            unary->setOperand(0, input);
            inspect(ArrayContentsFailure::None);
        }
        auto constant = input.getDefiningOp<ctjs::ConstantOp>();
        const mlir::Attribute oldValue = constant.getValue();
        for (const mlir::Attribute replacement :
             {mlir::Attribute(ctjs::NumberAttr::get(&context, 0)),
              mlir::Attribute(ctjs::StringAttr::get(&context, "1")),
              mlir::Attribute(ctjs::BigIntAttr::get(&context, "0"))}) {
            constant.setValueAttr(replacement);
            inspect(ArrayContentsFailure::None);
            constant.setValueAttr(oldValue);
            inspect(ArrayContentsFailure::None);
        }
        unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, static_cast<ctjs::UnaryKind>(255)));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Plus));
        inspect(ArrayContentsFailure::None);
        auto store = llvm::cast<ctjs::SetPropertyOp>(&function.getBody().back().front());
        const mlir::Value replacement = store.getValue();
        store->setOperand(2, child.getResult());
        mutation.contents.arrays = "a:[zero] | a:[x]";
        mutation.contents.exit = "a -> {a}; a -> {a,x}";
        mutation.discharged = "";
        inspect(ArrayContentsFailure::None);
        store->setOperand(2, replacement);
        mutation.contents = rows.front().contents;
        mutation.discharged = "x";
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = "live BigInt Plus", .body = mutation.contents.body, .expected = ""},
             "the BigInt Plus mutation fixture did not parse");
    }
    std::printf("BigInt Plus errors: %u rows, %u stale/fresh live states, one wide snapshot, "
                "%zu retention budget cutoffs\n",
                rowCount, liveStates, budgets);
}

void checkBigIntMixedSubErrors(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %big = ctjs.constant #ctjs.bigint<\"9007199254740993\"> "
        "{storage_test_id = \"big\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string produce =
        "  %produced = ctjs.binary sub %big, %zero {storage_test_id = \"produced\"}\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct mixed_row {
        contents_row contents;
        const char * discharged = "x";
    };
    const std::vector<mixed_row> rows = {
        {.contents = {.what =
                          "mixed BigInt Sub checks both structural continuations after its error",
                      .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                      .arrays = "a:[zero] | a:[zero]",
                      .exit = "a -> {a}; a -> {a}"}},
        {.contents = {.what = "a Sub error cannot erase a later retained child",
                      .body = values + produce + "  ctjs.return %a\n",
                      .arrays = "a:[x]",
                      .exit = "a -> {a,x}"},
         .discharged = ""},
        {.contents = {.what = "a Sub error cannot erase a retained structural arm",
                      .body = values + produce + branch + overwrite + "^no:\n  ctjs.return %a\n",
                      .arrays = "a:[zero] | a:[x]",
                      .exit = "a -> {a}; a -> {a,x}"},
         .discharged = ""},
        {.contents = {.what = "the independent Undefined Sub carrier cannot authorize BigInt Add",
                      .body =
                          values + produce + "  %next = ctjs.binary add %produced, %big\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "Sub cannot supply an exact Number index after its error",
                      .body =
                          values + produce + "  %read = ctjs.get_property %a[%produced]\n" + done,
                      .failure = ArrayContentsFailure::UnknownIndex}},
        {.contents = {.what = "Sub cannot supply an exact String key after its error",
                      .body =
                          values + produce + "  ctjs.set_property %x[%produced], %zero\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "Sub cannot erase an explicit thrown child",
                      .body = values + produce + "  ctjs.throw %x\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "Sub cannot erase a handler or its unknown retained value",
                      .body = values + produce +
                              "  ctjs.push_handler ^body catch ^handler\n"
                              "^body:\n  ctjs.pop_handler\n" +
                              done + "^handler:\n  %id, %exception = ctjs.catch_land\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedControlFlow}},
    };
    unsigned rowCount = 0;
    unsigned liveStates = 0;
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const mixed_row & expected) {
        checkArrayContents(module, expected.contents);
        const bool complete = expected.contents.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(module, {.what = expected.contents.what,
                                                .body = expected.contents.body,
                                                .discharged = complete ? expected.discharged : "",
                                                .complete = complete});
    };
    const auto parse = [&](const mixed_row & expected) {
        return mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
    };
    const auto run = [&](const mixed_row & expected) {
        if (auto module = parse(expected)) {
            check(*module, expected);
        } else {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the mixed BigInt Sub fixture did not parse");
        }
        ++rowCount;
    };
    for (const auto & expected : rows) { run(expected); }
    for (const std::string effect :
         {"  ctjs.store_global \"held\", %x\n", "  %called = ctjs.call %p(%a)\n",
          "  \"test.effect\"(%x) : (!ctjs.value) -> ()\n"}) {
        for (const bool before : {false, true}) {
            run({.contents = {.what = "independent Sub errors never authorize publication or calls",
                              .body =
                                  values + (before ? effect + produce : produce + effect) + done,
                              .failure = ArrayContentsFailure::UnsupportedOperation}});
        }
    }
    // Saved operands preserve their ORIGINAL identity when the current slot
    // changes category, including Object/opaque refusal on either incoming arm.
    for (const std::string input : {"%big", "%zero", "%x", "%p"}) {
        for (const bool reversed : {false, true}) {
            const std::string operands = reversed ? input + " : !ctjs.value), ^join(%big"
                                                  : "%big : !ctjs.value), ^join(" + input;
            const bool primitive = input == "%big" || input == "%zero";
            run({.contents = {.what = "Sub independently proves each original incoming category",
                              .body = values +
                                      "  %flag = ctjs.truthy %p\n"
                                      "  cf.cond_br %flag, ^join(" +
                                      operands +
                                      " : !ctjs.value)\n^join(%operand: !ctjs.value):\n"
                                      "  %produced = ctjs.binary sub %operand, %zero\n" +
                                      done,
                              .failure = primitive ? ArrayContentsFailure::None
                                                   : ArrayContentsFailure::UnsupportedOperation,
                              .arrays = "a:[x] | a:[x]",
                              .exit = "zero -> {}; zero -> {}"}});
        }
    }
    for (const bool savedBigInt : {false, true}) {
        const std::string saved = savedBigInt ? "%big" : "%zero";
        const std::string replacement = savedBigInt ? "%zero" : "%big";
        run({.contents = {.what = "saved own-field Sub operands survive replacement and deletion",
                          .body = values +
                                  "  %key = ctjs.constant #ctjs.string<\"value\">\n"
                                  "  ctjs.set_property %x[%key], " +
                                  saved +
                                  "\n  %saved = ctjs.get_property %x[%key]\n"
                                  "  ctjs.set_property %x[%key], " +
                                  replacement +
                                  "\n  ctjs.delete_named \"value\" from %x\n"
                                  "  cf.br ^next(%saved : !ctjs.value)\n"
                                  "^next(%operand: !ctjs.value):\n"
                                  "  %produced = ctjs.binary sub %operand, %zero\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnsupportedOperation,
                          .arrays = "a:[x]",
                          .exit = "zero -> {}",
                          .objects = "x:{}",
                          .propertyReads = savedBigInt ? "x[value]=big" : "x[value]=zero"}});
    }
    // Each side has its own original category. In particular, a computed
    // primitive need not prove Number: mixed Sub rejects every non-BigInt
    // primitive before conversion and gives an independent Undefined carrier.
    for (const bool left : {false, true}) {
        const auto subtract = [&](const std::string & input) {
            return "  %produced = ctjs.binary sub " + (left ? input + ", %big" : "%big, " + input) +
                   " {storage_test_id = \"produced\"}\n";
        };
        for (const std::string input :
             {"  %input = ctjs.constant #ctjs.undefined\n", "  %input = ctjs.constant #ctjs.null\n",
              "  %input = ctjs.constant #ctjs.boolean<false>\n",
              "  %input = ctjs.constant #ctjs.string<\"2\">\n", "  %input = ctjs.unary typeof %p\n",
              "  %input = ctjs.unary void %p\n", "  %input = ctjs.convert to_boolean %p\n",
              "  %input = ctjs.binary add %zero, %zero\n", "  %input = ctjs.unary plus %big\n",
              "  %input = ctjs.binary sub %big, %zero\n"}) {
            run({.contents = {.what = "mixed Sub independently accepts either primitive operand",
                              .body =
                                  values + input + subtract("%input") + "  ctjs.return %produced\n",
                              .arrays = "a:[x]",
                              .exit = "produced -> {}"}});
        }
        for (const std::string saved : {"%big", "%zero", "%x"}) {
            const std::string replacement = saved == "%big" ? "%zero" : "%big";
            run({.contents = {.what = "saved Sub operands retain their original array category",
                              .body = values + "  ctjs.set_property %a[%zero], " + saved +
                                      "\n  %saved = ctjs.get_property %a[%zero]\n"
                                      "  ctjs.set_property %a[%zero], " +
                                      replacement + "\n" + subtract("%saved") + done,
                              .failure = saved == "%x" ? ArrayContentsFailure::UnsupportedOperation
                                                       : ArrayContentsFailure::None,
                              .arrays = saved == "%big" ? "a:[zero]" : "a:[big]",
                              .reads = saved == "%big" ? "a[0]=big" : "a[0]=zero",
                              .exit = "zero -> {}"}});
        }
    }
    run({.contents = {.what = "the independent Sub carrier survives frame and edge transport",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                              "  cf.br ^next(%produced : !ctjs.value)\n"
                              "^next(%result: !ctjs.value):\n"
                              "  ctjs.root %result in %frame\n  ctjs.frame_exit %frame\n"
                              "  ctjs.return %result\n",
                      .arrays = "a:[x]",
                      .exit = "produced -> {}"}});
    for (const bool reversed : {false, true}) {
        const std::string big = "%big, %big : !ctjs.value, !ctjs.value";
        const std::string mixed = "%big, %zero : !ctjs.value, !ctjs.value";
        const std::string paths = values + "  %flag = ctjs.truthy %p\n  cf.cond_br %flag, ^join(" +
                                  (reversed ? mixed : big) + "), ^join(" +
                                  (reversed ? big : mixed) +
                                  ")\n^join(%left: !ctjs.value, %right: !ctjs.value):\n"
                                  "  %produced = ctjs.binary sub %left, %right "
                                  "{storage_test_id = \"produced\"}\n";
        run({.contents = {.what = "one Sub result keeps independent BigInt and failure categories",
                          .body = paths + "  ctjs.return %produced\n",
                          .arrays = "a:[x] | a:[x]",
                          .exit = "produced -> {}; produced -> {}"}});
        for (const std::string other : {"%big", "%zero"}) {
            run({.contents = {.what = "neither Sub path category authorizes mixed later Add",
                              .body = paths + "  %next = ctjs.binary add %produced, " + other +
                                      "\n" + done,
                              .failure = ArrayContentsFailure::UnsupportedOperation}});
        }
    }
    mixed_row wide = rows.front();
    std::string extras;
    for (unsigned i = 0; i < 32; ++i) {
        extras += "  %extra_" + std::to_string(i) + " = ctjs.binary sub %big, %zero\n";
    }
    wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
    auto narrowModule = parse(rows.front());
    auto wideModule = parse(wide);
    if (narrowModule && wideModule) {
        const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
        const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
            fail(row{.what = "Sub errors do not prune or skip independent result snapshot charges",
                     .body = wide.contents.body,
                     .expected = ""},
                 "32 results did not charge each operation and copied origin");
        }
        check(*wideModule, wide);
    } else {
        fail(row{.what = "wide mixed BigInt Sub snapshot",
                 .body = wide.contents.body,
                 .expected = ""},
             "the mixed BigInt Sub snapshot fixture did not parse");
    }
    mixed_row mutation = rows.front();
    if (auto module = parse(mutation)) {
        auto function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::BinaryOp binary;
        ctjs::CreateObjectOp child;
        ctjs::CreateArrayOp array;
        module->walk([&](ctjs::BinaryOp op) { binary = op; });
        module->walk([&](ctjs::CreateObjectOp op) { child = op; });
        module->walk([&](ctjs::CreateArrayOp op) { array = op; });
        mlir::OpBuilder builder(binary);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        child->setAttr("ctnative.confined", builder.getUnitAttr());
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*module))) {
            fail(row{.what = "mixed BigInt Sub stale solver",
                     .body = mutation.contents.body,
                     .expected = ""},
                 "the baseline solver did not converge");
        }
        const auto inspect = [&](ArrayContentsFailure failure) {
            mutation.contents.failure = failure;
            check(*module, mutation);
            const auto verdicts = computeVerdicts(stale, function);
            const bool complete = failure == ArrayContentsFailure::None;
            if (verdicts.arrayRetentionComplete != complete ||
                verdicts.confinedStoredSites !=
                    (complete && mutation.discharged[0] != '\0' ? 1U : 0U)) {
                fail(row{.what = "live mixed Sub origins defeat stale and forged completion",
                         .body = mutation.contents.body,
                         .expected = mutation.discharged},
                     "a stale solver authorized an unproved original operand or retained child");
            }
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        const mlir::Value inputs[] = {binary.getLhs(), binary.getRhs()};
        for (unsigned position = 0; position < 2; ++position) {
            for (mlir::Value invalid :
                 {mlir::Value(child.getResult()), mlir::Value(array.getResult()),
                  mlir::Value(function.getBody().front().getArgument(3))}) {
                binary->setOperand(position, invalid);
                inspect(ArrayContentsFailure::UnsupportedOperation);
                binary->setOperand(position, inputs[position]);
                inspect(ArrayContentsFailure::None);
            }
        }
        auto constant = inputs[0].getDefiningOp<ctjs::ConstantOp>();
        const mlir::Attribute oldValue = constant.getValue();
        for (const mlir::Attribute replacement :
             {mlir::Attribute(ctjs::UndefinedAttr::get(&context)),
              mlir::Attribute(ctjs::NullAttr::get(&context)),
              mlir::Attribute(ctjs::BooleanAttr::get(&context, false)),
              mlir::Attribute(ctjs::NumberAttr::get(&context, 0)),
              mlir::Attribute(ctjs::StringAttr::get(&context, "1")),
              mlir::Attribute(ctjs::BigIntAttr::get(&context, "0"))}) {
            constant.setValueAttr(replacement);
            inspect(ArrayContentsFailure::None);
            constant.setValueAttr(oldValue);
            inspect(ArrayContentsFailure::None);
        }
        for (const auto kind : {ctjs::BinaryKind::Add, ctjs::BinaryKind::Mul, ctjs::BinaryKind::Div,
                                ctjs::BinaryKind::Mod, ctjs::BinaryKind::Pow,
                                ctjs::BinaryKind::UShr, static_cast<ctjs::BinaryKind>(255)}) {
            binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, kind));
            inspect(kind == ctjs::BinaryKind::Mul || kind == ctjs::BinaryKind::Div ||
                            kind == ctjs::BinaryKind::Mod
                        ? ArrayContentsFailure::None
                        : ArrayContentsFailure::UnsupportedOperation);
            binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Sub));
            inspect(ArrayContentsFailure::None);
        }
        binary->setOperand(0, inputs[1]);
        binary->setOperand(1, inputs[0]);
        inspect(ArrayContentsFailure::None);
        binary->setOperand(0, inputs[0]);
        binary->setOperand(1, inputs[1]);
        inspect(ArrayContentsFailure::None);
        auto store = llvm::cast<ctjs::SetPropertyOp>(&function.getBody().back().front());
        const mlir::Value replacement = store.getValue();
        store->setOperand(2, child.getResult());
        mutation.contents.arrays = "a:[zero] | a:[x]";
        mutation.contents.exit = "a -> {a}; a -> {a,x}";
        mutation.discharged = "";
        inspect(ArrayContentsFailure::None);
        store->setOperand(2, replacement);
        mutation.contents = rows.front().contents;
        mutation.discharged = "x";
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = "live mixed BigInt Sub", .body = mutation.contents.body, .expected = ""},
             "the mixed BigInt Sub mutation fixture did not parse");
    }
    std::printf("mixed BigInt Sub errors: %u rows, %u stale/fresh live states, one wide snapshot, "
                "%zu retention budget cutoffs\n",
                rowCount, liveStates, budgets);
}

void checkBigIntMixedMulDivModErrors(mlir::MLIRContext & context, ctjs::BinaryKind operation) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %big = ctjs.constant #ctjs.bigint<\"9007199254740993\"> "
        "{storage_test_id = \"big\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string produce =
        "  %produced = ctjs.binary mul %big, %zero {storage_test_id = \"produced\"}\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct mixed_row {
        contents_row contents;
        const char * discharged = "x";
    };
    const std::vector<mixed_row> rows = {
        {.contents = {.what =
                          "mixed BigInt Mul checks both structural continuations after its error",
                      .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                      .arrays = "a:[zero] | a:[zero]",
                      .exit = "a -> {a}; a -> {a}"}},
        {.contents = {.what = "a Mul error cannot erase a later retained child",
                      .body = values + produce + "  ctjs.return %a\n",
                      .arrays = "a:[x]",
                      .exit = "a -> {a,x}"},
         .discharged = ""},
        {.contents = {.what = "a Mul error cannot erase a retained structural arm",
                      .body = values + produce + branch + overwrite + "^no:\n  ctjs.return %a\n",
                      .arrays = "a:[zero] | a:[x]",
                      .exit = "a -> {a}; a -> {a,x}"},
         .discharged = ""},
        {.contents = {.what = "the independent Undefined Mul carrier cannot authorize BigInt Add",
                      .body =
                          values + produce + "  %next = ctjs.binary add %produced, %big\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "Mul cannot supply an exact Number index after its error",
                      .body =
                          values + produce + "  %read = ctjs.get_property %a[%produced]\n" + done,
                      .failure = ArrayContentsFailure::UnknownIndex}},
        {.contents = {.what = "Mul cannot supply an exact String key after its error",
                      .body =
                          values + produce + "  ctjs.set_property %x[%produced], %zero\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "Mul cannot erase an explicit thrown child",
                      .body = values + produce + "  ctjs.throw %x\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "Mul cannot erase a handler or its unknown retained value",
                      .body = values + produce +
                              "  ctjs.push_handler ^body catch ^handler\n"
                              "^body:\n  ctjs.pop_handler\n" +
                              done + "^handler:\n  %id, %exception = ctjs.catch_land\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedControlFlow}},
    };
    unsigned rowCount = 0;
    unsigned liveStates = 0;
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const mixed_row & expected) {
        checkArrayContents(module, expected.contents);
        const bool complete = expected.contents.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(module, {.what = expected.contents.what,
                                                .body = expected.contents.body,
                                                .discharged = complete ? expected.discharged : "",
                                                .complete = complete});
    };
    const auto parse = [&](const mixed_row & expected) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
        // Preserve every historical Mul fixture; run the same independent
        // operand/retention matrix for Div/Mod by changing only those operations.
        if (module && operation != ctjs::BinaryKind::Mul) {
            module->walk([&](ctjs::BinaryOp binary) {
                if (binary.getKind() == ctjs::BinaryKind::Mul) {
                    binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, operation));
                }
            });
        }
        return module;
    };
    const auto run = [&](const mixed_row & expected) {
        if (auto module = parse(expected)) {
            check(*module, expected);
        } else {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the mixed BigInt Mul fixture did not parse");
        }
        ++rowCount;
    };
    for (const auto & expected : rows) { run(expected); }
    for (const std::string effect :
         {"  ctjs.store_global \"held\", %x\n", "  %called = ctjs.call %p(%a)\n",
          "  \"test.effect\"(%x) : (!ctjs.value) -> ()\n"}) {
        for (const bool before : {false, true}) {
            run({.contents = {.what = "independent Mul errors never authorize publication or calls",
                              .body =
                                  values + (before ? effect + produce : produce + effect) + done,
                              .failure = ArrayContentsFailure::UnsupportedOperation}});
        }
    }
    // Saved operands preserve their ORIGINAL identity when the current slot
    // changes category, including Object/opaque refusal on either incoming arm.
    for (const std::string input : {"%big", "%zero", "%x", "%p"}) {
        for (const bool reversed : {false, true}) {
            const std::string operands = reversed ? input + " : !ctjs.value), ^join(%big"
                                                  : "%big : !ctjs.value), ^join(" + input;
            const bool primitive = input == "%big" || input == "%zero";
            run({.contents = {.what = "Mul independently proves each original incoming category",
                              .body = values +
                                      "  %flag = ctjs.truthy %p\n"
                                      "  cf.cond_br %flag, ^join(" +
                                      operands +
                                      " : !ctjs.value)\n^join(%operand: !ctjs.value):\n"
                                      "  %produced = ctjs.binary mul %operand, %zero\n" +
                                      done,
                              .failure = primitive ? ArrayContentsFailure::None
                                                   : ArrayContentsFailure::UnsupportedOperation,
                              .arrays = "a:[x] | a:[x]",
                              .exit = "zero -> {}; zero -> {}"}});
        }
    }
    for (const bool savedBigInt : {false, true}) {
        const std::string saved = savedBigInt ? "%big" : "%zero";
        const std::string replacement = savedBigInt ? "%zero" : "%big";
        run({.contents = {.what = "saved own-field Mul operands survive replacement and deletion",
                          .body = values +
                                  "  %key = ctjs.constant #ctjs.string<\"value\">\n"
                                  "  ctjs.set_property %x[%key], " +
                                  saved +
                                  "\n  %saved = ctjs.get_property %x[%key]\n"
                                  "  ctjs.set_property %x[%key], " +
                                  replacement +
                                  "\n  ctjs.delete_named \"value\" from %x\n"
                                  "  cf.br ^next(%saved : !ctjs.value)\n"
                                  "^next(%operand: !ctjs.value):\n"
                                  "  %produced = ctjs.binary mul %operand, %zero\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnsupportedOperation,
                          .arrays = "a:[x]",
                          .exit = "zero -> {}",
                          .objects = "x:{}",
                          .propertyReads = savedBigInt ? "x[value]=big" : "x[value]=zero"}});
    }
    // Each side has its own original category. In particular, a computed
    // primitive need not prove Number: mixed Mul rejects every non-BigInt
    // primitive before conversion and gives an independent Undefined carrier.
    for (const bool left : {false, true}) {
        const auto multiply = [&](const std::string & input) {
            return "  %produced = ctjs.binary mul " + (left ? input + ", %big" : "%big, " + input) +
                   " {storage_test_id = \"produced\"}\n";
        };
        for (const std::string input :
             {"  %input = ctjs.constant #ctjs.undefined\n", "  %input = ctjs.constant #ctjs.null\n",
              "  %input = ctjs.constant #ctjs.boolean<false>\n",
              "  %input = ctjs.constant #ctjs.string<\"2\">\n", "  %input = ctjs.unary typeof %p\n",
              "  %input = ctjs.unary void %p\n", "  %input = ctjs.convert to_boolean %p\n",
              "  %input = ctjs.binary add %zero, %zero\n", "  %input = ctjs.unary plus %big\n",
              "  %input = ctjs.binary mul %big, %zero\n"}) {
            run({.contents = {.what = "mixed Mul independently accepts either primitive operand",
                              .body =
                                  values + input + multiply("%input") + "  ctjs.return %produced\n",
                              .arrays = "a:[x]",
                              .exit = "produced -> {}"}});
        }
        for (const std::string saved : {"%big", "%zero", "%x"}) {
            const std::string replacement = saved == "%big" ? "%zero" : "%big";
            run({.contents = {.what = "saved Mul operands retain their original array category",
                              .body = values + "  ctjs.set_property %a[%zero], " + saved +
                                      "\n  %saved = ctjs.get_property %a[%zero]\n"
                                      "  ctjs.set_property %a[%zero], " +
                                      replacement + "\n" + multiply("%saved") + done,
                              .failure = saved == "%x" ? ArrayContentsFailure::UnsupportedOperation
                                                       : ArrayContentsFailure::None,
                              .arrays = saved == "%big" ? "a:[zero]" : "a:[big]",
                              .reads = saved == "%big" ? "a[0]=big" : "a[0]=zero",
                              .exit = "zero -> {}"}});
        }
    }
    run({.contents = {.what = "the independent Mul carrier survives frame and edge transport",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                              "  cf.br ^next(%produced : !ctjs.value)\n"
                              "^next(%result: !ctjs.value):\n"
                              "  ctjs.root %result in %frame\n  ctjs.frame_exit %frame\n"
                              "  ctjs.return %result\n",
                      .arrays = "a:[x]",
                      .exit = "produced -> {}"}});
    for (const bool reversed : {false, true}) {
        const std::string big = "%big, %big : !ctjs.value, !ctjs.value";
        const std::string mixed = "%big, %zero : !ctjs.value, !ctjs.value";
        const std::string paths = values + "  %flag = ctjs.truthy %p\n  cf.cond_br %flag, ^join(" +
                                  (reversed ? mixed : big) + "), ^join(" +
                                  (reversed ? big : mixed) +
                                  ")\n^join(%left: !ctjs.value, %right: !ctjs.value):\n"
                                  "  %produced = ctjs.binary mul %left, %right "
                                  "{storage_test_id = \"produced\"}\n";
        run({.contents = {.what = "one Mul result keeps independent BigInt and failure categories",
                          .body = paths + "  ctjs.return %produced\n",
                          .arrays = "a:[x] | a:[x]",
                          .exit = "produced -> {}; produced -> {}"}});
        for (const std::string other : {"%big", "%zero"}) {
            run({.contents = {.what = "neither Mul path category authorizes mixed later Add",
                              .body = paths + "  %next = ctjs.binary add %produced, " + other +
                                      "\n" + done,
                              .failure = ArrayContentsFailure::UnsupportedOperation}});
        }
    }
    mixed_row wide = rows.front();
    std::string extras;
    for (unsigned i = 0; i < 32; ++i) {
        extras += "  %extra_" + std::to_string(i) + " = ctjs.binary mul %big, %zero\n";
    }
    wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
    auto narrowModule = parse(rows.front());
    auto wideModule = parse(wide);
    if (narrowModule && wideModule) {
        const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
        const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
            fail(row{.what = "Mul errors do not prune or skip independent result snapshot charges",
                     .body = wide.contents.body,
                     .expected = ""},
                 "32 results did not charge each operation and copied origin");
        }
        check(*wideModule, wide);
    } else {
        fail(row{.what = "wide mixed BigInt Mul snapshot",
                 .body = wide.contents.body,
                 .expected = ""},
             "the mixed BigInt Mul snapshot fixture did not parse");
    }
    mixed_row mutation = rows.front();
    if (auto module = parse(mutation)) {
        auto function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::BinaryOp binary;
        ctjs::CreateObjectOp child;
        ctjs::CreateArrayOp array;
        module->walk([&](ctjs::BinaryOp op) { binary = op; });
        module->walk([&](ctjs::CreateObjectOp op) { child = op; });
        module->walk([&](ctjs::CreateArrayOp op) { array = op; });
        mlir::OpBuilder builder(binary);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        child->setAttr("ctnative.confined", builder.getUnitAttr());
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*module))) {
            fail(row{.what = "mixed BigInt Mul stale solver",
                     .body = mutation.contents.body,
                     .expected = ""},
                 "the baseline solver did not converge");
        }
        const auto inspect = [&](ArrayContentsFailure failure) {
            mutation.contents.failure = failure;
            check(*module, mutation);
            const auto verdicts = computeVerdicts(stale, function);
            const bool complete = failure == ArrayContentsFailure::None;
            if (verdicts.arrayRetentionComplete != complete ||
                verdicts.confinedStoredSites !=
                    (complete && mutation.discharged[0] != '\0' ? 1U : 0U)) {
                fail(row{.what = "live mixed Mul origins defeat stale and forged completion",
                         .body = mutation.contents.body,
                         .expected = mutation.discharged},
                     "a stale solver authorized an unproved original operand or retained child");
            }
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        const mlir::Value inputs[] = {binary.getLhs(), binary.getRhs()};
        for (unsigned position = 0; position < 2; ++position) {
            for (mlir::Value invalid :
                 {mlir::Value(child.getResult()), mlir::Value(array.getResult()),
                  mlir::Value(function.getBody().front().getArgument(3))}) {
                binary->setOperand(position, invalid);
                inspect(ArrayContentsFailure::UnsupportedOperation);
                binary->setOperand(position, inputs[position]);
                inspect(ArrayContentsFailure::None);
            }
        }
        auto constant = inputs[0].getDefiningOp<ctjs::ConstantOp>();
        const mlir::Attribute oldValue = constant.getValue();
        for (const mlir::Attribute replacement :
             {mlir::Attribute(ctjs::UndefinedAttr::get(&context)),
              mlir::Attribute(ctjs::NullAttr::get(&context)),
              mlir::Attribute(ctjs::BooleanAttr::get(&context, false)),
              mlir::Attribute(ctjs::NumberAttr::get(&context, 0)),
              mlir::Attribute(ctjs::StringAttr::get(&context, "1")),
              mlir::Attribute(ctjs::BigIntAttr::get(&context, "0"))}) {
            constant.setValueAttr(replacement);
            inspect(ArrayContentsFailure::None);
            constant.setValueAttr(oldValue);
            inspect(ArrayContentsFailure::None);
        }
        for (const auto kind :
             {ctjs::BinaryKind::Add, ctjs::BinaryKind::Div, ctjs::BinaryKind::Mod,
              ctjs::BinaryKind::Pow, ctjs::BinaryKind::UShr, static_cast<ctjs::BinaryKind>(255)}) {
            binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, kind));
            inspect(kind == ctjs::BinaryKind::Div || kind == ctjs::BinaryKind::Mod
                        ? ArrayContentsFailure::None
                        : ArrayContentsFailure::UnsupportedOperation);
            binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, operation));
            inspect(ArrayContentsFailure::None);
        }
        binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Sub));
        inspect(ArrayContentsFailure::None);
        binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, operation));
        inspect(ArrayContentsFailure::None);
        binary->setOperand(0, inputs[1]);
        binary->setOperand(1, inputs[0]);
        inspect(ArrayContentsFailure::None);
        binary->setOperand(0, inputs[0]);
        binary->setOperand(1, inputs[1]);
        inspect(ArrayContentsFailure::None);
        auto store = llvm::cast<ctjs::SetPropertyOp>(&function.getBody().back().front());
        const mlir::Value replacement = store.getValue();
        store->setOperand(2, child.getResult());
        mutation.contents.arrays = "a:[zero] | a:[x]";
        mutation.contents.exit = "a -> {a}; a -> {a,x}";
        mutation.discharged = "";
        inspect(ArrayContentsFailure::None);
        store->setOperand(2, replacement);
        mutation.contents = rows.front().contents;
        mutation.discharged = "x";
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = "live mixed BigInt Mul", .body = mutation.contents.body, .expected = ""},
             "the mixed BigInt Mul mutation fixture did not parse");
    }
    std::printf("mixed BigInt %s errors: %u rows, %u stale/fresh live states, one wide snapshot, "
                "%zu retention budget cutoffs\n",
                operation == ctjs::BinaryKind::Mul   ? "Mul"
                : operation == ctjs::BinaryKind::Div ? "Div"
                                                     : "Mod",
                rowCount, liveStates, budgets);
}

} // namespace ctcompile::test::escape::arrays
