#include "Harness.h"

namespace ctcompile::test::escape::arrays {

void checkStringBigIntConcatenation(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %text = ctjs.constant #ctjs.string<\"saved:\"> {storage_test_id = \"text\"}\n"
        "  %big = ctjs.constant #ctjs.bigint<\"9007199254740993\"> "
        "{storage_test_id = \"big\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string done = "  ctjs.return %produced\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct string_row {
        contents_row contents;
        const char * discharged = "x";
    };
    for (const auto kind : {ctjs::BinaryKind::Add, ctjs::BinaryKind::Concat}) {
        const bool concat = kind == ctjs::BinaryKind::Concat;
        const std::string spelling = ctjs::stringifyBinaryKind(kind).str();
        const auto binary = [&](const std::string & lhs, const std::string & rhs) {
            return "  %produced = ctjs.binary " + spelling + " " + lhs + ", " + rhs +
                   " {storage_test_id = \"produced\"}\n";
        };
        const std::string produce = binary("%text", "%big");
        const std::vector<string_row> rows = {
            {.contents = {.what = "String BigInt conversion cannot select a structural arm",
                          .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                          .arrays = "a:[zero] | a:[zero]",
                          .exit = "a -> {a}; a -> {a}"}},
            {.contents = {.what = "String BigInt results do not retain either input object",
                          .body = values + produce + done,
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "a String result replaces the stored child identity",
                          .body = values + produce +
                                  "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %a\n",
                          .arrays = "a:[produced]",
                          .exit = "a -> {a}"}},
            {.contents = {.what = "a saved child remains retained beside String BigInt results",
                          .body =
                              values + "  %saved = ctjs.get_property %a[%zero]\n" + produce +
                              "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %saved\n",
                          .arrays = "a:[produced]",
                          .reads = "a[0]=x",
                          .exit = "x -> {x}"},
             .discharged = ""},
            {.contents = {.what = "String BigInt categories survive frame and edge transport",
                          .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                                  "  cf.br ^next(%produced : !ctjs.value)\n"
                                  "^next(%input: !ctjs.value):\n"
                                  "  %next = ctjs.binary add %big, %input "
                                  "{storage_test_id = \"next\"}\n"
                                  "  ctjs.root %next in %frame\n  ctjs.frame_exit %frame\n"
                                  "  ctjs.return %next\n",
                          .arrays = "a:[x]",
                          .exit = "next -> {}"}},
            {.contents = {.what = "String BigInt category supplies no concrete array index",
                          .body = values + produce + "  %read = ctjs.get_property %a[%produced]\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownIndex}},
            {.contents = {.what = "String BigInt category supplies no concrete object key",
                          .body = values + produce + "  ctjs.set_property %x[%produced], %zero\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
        };
        unsigned rowCount = 0;
        unsigned liveStates = 0;
        std::size_t budgets = 0;
        const auto parse = [&](const string_row & expected) {
            return mlir::parseSourceString<mlir::ModuleOp>(
                std::string{kPrologue} + expected.contents.body + "}\n", &context);
        };
        const auto check = [&](mlir::ModuleOp module, const string_row & expected) {
            checkArrayContents(module, expected.contents);
            const bool complete = expected.contents.failure == ArrayContentsFailure::None;
            budgets +=
                checkArrayRetention(module, {.what = expected.contents.what,
                                             .body = expected.contents.body,
                                             .discharged = complete ? expected.discharged : "",
                                             .complete = complete});
        };
        const auto run = [&](const string_row & expected) {
            if (auto module = parse(expected)) {
                check(*module, expected);
            } else {
                fail(row{.what = expected.contents.what,
                         .body = expected.contents.body,
                         .expected = ""},
                     "the String BigInt fixture did not parse");
            }
            ++rowCount;
        };
        for (const auto & expected : rows) { run(expected); }
        for (const bool left : {false, true}) {
            const auto withBig = [&](const std::string & input) {
                return left ? binary(input, "%big") : binary("%big", input);
            };
            for (const std::string input : {"  %input = ctjs.constant #ctjs.string<\"\">\n",
                                            "  %input = ctjs.unary typeof %p\n",
                                            "  %input = ctjs.binary concat %big, %zero\n",
                                            "  %input = ctjs.binary add %text, %zero\n",
                                            "  %input = ctjs.binary add %big, %text\n"}) {
                run({.contents = {
                         .what = "each original computed String category independently qualifies",
                         .body = values + input + withBig("%input") + done,
                         .arrays = "a:[x]",
                         .exit = "produced -> {}"}});
            }
            for (const std::string input :
                 {"  %input = ctjs.constant #ctjs.undefined\n",
                  "  %input = ctjs.constant #ctjs.null\n",
                  "  %input = ctjs.constant #ctjs.boolean<true>\n",
                  "  %input = ctjs.constant #ctjs.number<17>\n",
                  "  %input = ctjs.compare strict_eq %p, %zero\n",
                  "  %input = ctjs.convert to_boolean %p\n", "  %input = ctjs.unary not %p\n",
                  "  %input = ctjs.unary void %p\n", "  %input = ctjs.unary neg %zero\n",
                  "  %input = ctjs.binary_static add %zero, %zero\n",
                  "  %input = ctjs.binary add %zero, %zero\n"}) {
                run({.contents = {
                         .what = "primitive provenance alone cannot prove String for mixed Add",
                         .body = values + input + withBig("%input") + done,
                         .failure = concat ? ArrayContentsFailure::None
                                           : ArrayContentsFailure::UnsupportedOperation,
                         .arrays = "a:[x]",
                         .exit = "produced -> {}"}});
                run({.contents = {
                         .what = "a separately proved String accepts every non-BigInt primitive",
                         .body = values + input +
                                 (left ? binary("%input", "%text") : binary("%text", "%input")) +
                                 done,
                         .arrays = "a:[x]",
                         .exit = "produced -> {}"}});
            }
            for (const std::string input : {"%p", "%x", "%a"}) {
                run({.contents = {.what = "String conversion cannot borrow another operand's proof",
                                  .body = values + withBig(input) + done,
                                  .failure = ArrayContentsFailure::UnsupportedOperation}});
                run({.contents = {
                         .what = "a known String does not authorize object or opaque conversion",
                         .body = values + (left ? binary(input, "%text") : binary("%text", input)) +
                                 done,
                         .failure = ArrayContentsFailure::UnsupportedOperation}});
            }
            for (const std::string saved : {"%text", "%zero", "%big", "%x"}) {
                const bool supported = saved != "%x" && (concat || saved != "%zero");
                run({.contents = {
                         .what = "saved array origins survive String Number and object overwrite",
                         .body = values + "  ctjs.set_property %a[%zero], " + saved +
                                 "\n  %input = ctjs.get_property %a[%zero]\n"
                                 "  ctjs.set_property %a[%zero], %zero\n" +
                                 withBig("%input") + done,
                         .failure = supported ? ArrayContentsFailure::None
                                              : ArrayContentsFailure::UnsupportedOperation,
                         .arrays = "a:[zero]",
                         .reads = saved == "%text"  ? "a[0]=text"
                                  : saved == "%big" ? "a[0]=big"
                                                    : "a[0]=zero",
                         .exit = "produced -> {}"}});
                run({.contents = {.what = "saved own fields survive category changes and deletion",
                                  .body = values +
                                          "  %key = ctjs.constant #ctjs.string<\"operand\">\n"
                                          "  ctjs.set_property %x[%key], " +
                                          saved +
                                          "\n  %input = ctjs.get_property %x[%key]\n"
                                          "  ctjs.set_property %x[%key], %text\n"
                                          "  ctjs.delete_named \"operand\" from %x\n" +
                                          withBig("%input") + done,
                                  .failure = ArrayContentsFailure::UnsupportedOperation,
                                  .arrays = "a:[x]",
                                  .exit = "produced -> {}"}});
            }
        }
        for (const std::string input : {"%text", "%zero", "%big"}) {
            run({.contents = {
                     .what = "one Add result has separate String Number and BigInt path categories",
                     .body = values +
                             "  %flag = ctjs.truthy %p\n"
                             "  cf.cond_br %flag, ^join(%text, %zero : !ctjs.value, !ctjs.value), "
                             "^join(" +
                             input + ", " + input +
                             " : !ctjs.value, !ctjs.value)\n"
                             "^join(%left: !ctjs.value, %right: !ctjs.value):\n"
                             "  %input = ctjs.binary add %left, %right\n" +
                             binary("%input", "%big") + done,
                     .failure = !concat && input == "%zero"
                                    ? ArrayContentsFailure::UnsupportedOperation
                                    : ArrayContentsFailure::None,
                     .arrays = "a:[x] | a:[x]",
                     .exit = "produced -> {}; produced -> {}"}});
        }
        for (const std::string effect :
             {"  ctjs.store_global \"held\", %x\n", "  %call = ctjs.call %p(%x)\n",
              "  \"test.effect\"(%produced) : (!ctjs.value) -> ()\n"}) {
            run({.contents = {
                     .what =
                         "String BigInt conversion never authorizes publication calls or effects",
                     .body = values + produce + effect + done,
                     .failure = ArrayContentsFailure::UnsupportedOperation}});
        }
        run({.contents = {.what = "String BigInt conversion retains explicit throw refusal",
                          .body = values + produce + "  ctjs.throw %x\n",
                          .failure = ArrayContentsFailure::UnsupportedOperation}});
        run({.contents = {.what =
                              "String BigInt conversion supplies no handler or completion proof",
                          .body = values + "  ctjs.push_handler ^body catch ^pad\n^body:\n" +
                                  produce + "  ctjs.pop_handler\n" + overwrite +
                                  "^pad:\n  %id, %exception = ctjs.catch_land\n  ctjs.return %a\n",
                          .failure = ArrayContentsFailure::UnsupportedControlFlow}});
        string_row wide = rows.front();
        std::string extras;
        for (unsigned i = 0; i < 32; ++i) {
            extras +=
                "  %extra_" + std::to_string(i) + " = ctjs.binary " + spelling + " %text, %big\n";
        }
        wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
        auto narrowModule = parse(rows.front());
        auto wideModule = parse(wide);
        if (narrowModule && wideModule) {
            const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
            const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
            if (!narrow.complete || !expanded.complete ||
                expanded.work != narrow.work + (concat ? 64 : 128)) {
                fail(row{.what = "String Add snapshots independently charge result origins and "
                                 "categories",
                         .body = wide.contents.body,
                         .expected = ""},
                     "String snapshot accounting changed");
            }
            check(*wideModule, wide);
        } else {
            fail(row{.what = "wide String BigInt snapshot",
                     .body = wide.contents.body,
                     .expected = ""},
                 "the wide String BigInt fixture did not parse");
        }
        string_row mutation = rows.front();
        mutation.contents.what = "live String categories and concrete indices remain independent";
        mutation.contents.body = values + binary("%text", "%zero") +
                                 "  %next = ctjs.binary add %produced, %big\n" + branch +
                                 overwrite + "^no:\n" + overwrite;
        if (auto module = parse(mutation)) {
            ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
            ctjs::BinaryOp producer;
            ctjs::CreateObjectOp child;
            ctjs::CreateArrayOp array;
            module->walk([&](ctjs::BinaryOp op) {
                if (op->getAttrOfType<mlir::StringAttr>("storage_test_id")) { producer = op; }
            });
            module->walk([&](ctjs::CreateObjectOp op) { child = op; });
            module->walk([&](ctjs::CreateArrayOp op) { array = op; });
            mlir::OpBuilder builder(producer);
            function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
            function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
            child->setAttr("ctnative.confined", builder.getUnitAttr());
            mlir::DataFlowSolver stale;
            stale.load<mlir::dataflow::DeadCodeAnalysis>();
            stale.load<mlir::dataflow::SparseConstantPropagation>();
            stale.load<EscapeAnalysis>();
            if (failed(stale.initializeAndRun(*module))) {
                fail(row{.what = "String BigInt stale solver",
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
                    verdicts.confinedStoredSites != (complete ? 1U : 0U)) {
                    fail(row{.what =
                                 "live String categories defeat stale solvers and forged markers",
                             .body = mutation.contents.body,
                             .expected = ""},
                         "a cached String category survived live mutation");
                }
                ++liveStates;
            };
            inspect(ArrayContentsFailure::None);
            for (unsigned position = 0; position < 2; ++position) {
                const mlir::Value saved = producer->getOperand(position);
                const mlir::Value invalidInputs[] = {child.getResult(), array.getResult(),
                                                     function.getBody().front().getArgument(3)};
                for (const mlir::Value invalid : invalidInputs) {
                    producer->setOperand(position, invalid);
                    inspect(ArrayContentsFailure::UnsupportedOperation);
                    producer->setOperand(position, saved);
                    inspect(ArrayContentsFailure::None);
                }
                auto constant = saved.getDefiningOp<ctjs::ConstantOp>();
                const auto old = constant.getValue();
                for (const mlir::Attribute replacement :
                     {mlir::Attribute(ctjs::NumberAttr::get(&context, 0)),
                      mlir::Attribute(ctjs::BigIntAttr::get(&context, "2")),
                      mlir::Attribute(ctjs::StringAttr::get(&context, ""))}) {
                    constant.setValueAttr(replacement);
                    inspect(position == 1 && !llvm::isa<ctjs::NumberAttr>(replacement)
                                ? ArrayContentsFailure::UnknownIndex
                            : !concat && position == 0 && !llvm::isa<ctjs::StringAttr>(replacement)
                                ? ArrayContentsFailure::UnsupportedOperation
                                : ArrayContentsFailure::None);
                    constant.setValueAttr(old);
                    inspect(ArrayContentsFailure::None);
                }
            }
            for (const auto other : {ctjs::BinaryKind::Add, ctjs::BinaryKind::Concat,
                                     ctjs::BinaryKind::Sub, ctjs::BinaryKind::Mul,
                                     ctjs::BinaryKind::Pow, static_cast<ctjs::BinaryKind>(255)}) {
                producer.setKindAttr(ctjs::BinaryKindAttr::get(&context, other));
                inspect(other == ctjs::BinaryKind::Add || other == ctjs::BinaryKind::Concat
                            ? ArrayContentsFailure::None
                            : ArrayContentsFailure::UnsupportedOperation);
                producer.setKindAttr(ctjs::BinaryKindAttr::get(&context, kind));
                inspect(ArrayContentsFailure::None);
            }
        } else {
            fail(row{.what = "live String BigInt categories",
                     .body = mutation.contents.body,
                     .expected = ""},
                 "the live String BigInt fixture did not parse");
        }
        std::printf("String BigInt %s: %u rows, %u stale/fresh live states, one wide snapshot, %zu "
                    "retention budget cutoffs\n",
                    spelling.c_str(), rowCount, liveStates, budgets);
    }
}

void checkBigIntComparison(mlir::MLIRContext & context, ctjs::CompareKind producerKind) {
    const std::string spelling = ctjs::stringifyCompareKind(producerKind).str();
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %lhs = ctjs.constant #ctjs.bigint<\"9007199254740993\"> "
        "{storage_test_id = \"lhs\"}\n"
        "  %rhs = ctjs.constant #ctjs.bigint<\"9007199254740992\"> "
        "{storage_test_id = \"rhs\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const auto compare = [&](const std::string & lhs, const std::string & rhs) {
        return "  %produced = ctjs.compare " + spelling + " " + lhs + ", " + rhs +
               " {storage_test_id = \"produced\"}\n";
    };
    const std::string produce = compare("%lhs", "%rhs");
    const std::string done = "  ctjs.return %produced\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct bigint_row {
        contents_row contents;
        const char * discharged = "x";
    };
    const std::vector<bigint_row> rows = {
        {.contents = {.what = "BigInt comparison cannot prune either structural overwrite arm",
                      .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                      .arrays = "a:[zero] | a:[zero]",
                      .exit = "a -> {a}; a -> {a}"}},
        {.contents = {.what = "BigInt comparison returns an independent Boolean",
                      .body = values + produce + done,
                      .arrays = "a:[x]",
                      .exit = "produced -> {}"}},
        {.contents = {.what = "a stored BigInt comparison result retains no child identity",
                      .body = values + produce +
                              "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %a\n",
                      .arrays = "a:[produced]",
                      .exit = "a -> {a}"}},
        {.contents = {.what = "saved children retain their identity after BigInt comparison",
                      .body = values + "  %saved = ctjs.get_property %a[%zero]\n" + produce +
                              "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %saved\n",
                      .arrays = "a:[produced]",
                      .reads = "a[0]=x",
                      .exit = "x -> {x}"},
         .discharged = ""},
        {.contents = {.what = "a BigInt comparison result forwards and roots independently",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                              "  cf.br ^next(%produced : !ctjs.value)\n"
                              "^next(%result: !ctjs.value):\n"
                              "  ctjs.root %result in %frame\n  ctjs.frame_exit %frame\n"
                              "  ctjs.return %result\n",
                      .arrays = "a:[x]",
                      .exit = "produced -> {}"}},
        {.contents = {.what = "a BigInt comparison result is not a literal array index",
                      .body =
                          values + produce + "  %read = ctjs.get_property %a[%produced]\n" + done,
                      .failure = ArrayContentsFailure::UnknownIndex}},
        {.contents = {.what = "a BigInt comparison result is not a literal own String key",
                      .body =
                          values + produce + "  ctjs.set_property %x[%produced], %zero\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "even equal BigInts cannot hide publication on an untaken arm",
                      .body = values + compare("%lhs", "%lhs") + branch + done +
                              "^no:\n  ctjs.store_global \"held\", %x\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "BigInt comparison cannot authorize an opaque return",
                      .body = values + produce + "  ctjs.return %p\n",
                      .failure = ArrayContentsFailure::UnknownValue}},
    };
    unsigned rowCount = 0;
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const bigint_row & expected) {
        checkArrayContents(module, expected.contents);
        const bool complete = expected.contents.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(module, {.what = expected.contents.what,
                                                .body = expected.contents.body,
                                                .discharged = complete ? expected.discharged : "",
                                                .complete = complete});
    };
    const auto parse = [&](const bigint_row & expected) {
        return mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
    };
    const auto run = [&](const bigint_row & expected) {
        if (auto module = parse(expected)) {
            check(*module, expected);
        } else {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the BigInt comparison fixture did not parse");
        }
        ++rowCount;
    };
    for (const auto & expected : rows) { run(expected); }
    const std::vector<std::string> primitiveOrigins = {
        "  %operand = ctjs.constant #ctjs.undefined\n",
        "  %operand = ctjs.constant #ctjs.null\n",
        "  %operand = ctjs.constant #ctjs.boolean<true>\n",
        "  %operand = ctjs.constant #ctjs.number<0>\n",
        "  %operand = ctjs.constant #ctjs.string<\"9007199254740993\">\n",
        "  %operand = ctjs.compare strict_eq %lhs, %rhs\n",
        "  %operand = ctjs.constant #ctjs.string<\"\">\n",
        "  %operand = ctjs.constant #ctjs.string<\"1.5\">\n",
        "  %operand = ctjs.constant #ctjs.string<\"invalid\">\n",
        "  %operand = ctjs.constant #ctjs.string<\"0x10\">\n",
        "  %operand = ctjs.convert to_boolean %p\n",
        "  %operand = ctjs.unary not %p\n",
        "  %operand = ctjs.unary typeof %p\n",
        "  %operand = ctjs.unary void %p\n",
        "  %operand = ctjs.unary plus %zero\n",
        "  %operand = ctjs.binary div %zero, %zero\n",
        "  %operand = ctjs.binary add %lhs, %rhs\n",
        "  %operand = ctjs.binary_static shl %lhs, %rhs\n",
        "  %text = ctjs.constant #ctjs.string<\"\">\n"
        "  %operand = ctjs.binary add %text, %lhs\n",
        "  %operand = ctjs.binary concat %zero, %lhs\n",
    };
    for (const bool left : {false, true}) {
        const auto compareInput = [&](const std::string & input) {
            return left ? compare(input, "%rhs") : compare("%lhs", input);
        };
        // A single computed producer can yield Number, String or BigInt on
        // separate paths; all are primitive, without granting a result value.
        const std::string threeCategories =
            values + "  %text = ctjs.constant #ctjs.string<\"1.5\">\n"
                     "  %flag = ctjs.truthy %p\n"
                     "  cf.cond_br %flag, ^numbers, ^other\n^numbers:\n"
                     "  cf.br ^join(%zero, %zero : !ctjs.value, !ctjs.value)\n^other:\n"
                     "  cf.cond_br %flag, ^strings, ^bigints\n^strings:\n"
                     "  cf.br ^join(%text, %lhs : !ctjs.value, !ctjs.value)\n^bigints:\n"
                     "  cf.br ^join(%lhs, %rhs : !ctjs.value, !ctjs.value)\n"
                     "^join(%left: !ctjs.value, %right: !ctjs.value):\n"
                     "  %operand = ctjs.binary add %left, %right\n";
        run({.contents = {.what = "mixed comparison accepts independently proved path categories",
                          .body = threeCategories + compareInput("%operand") + done,
                          .arrays = "a:[x] | a:[x] | a:[x]",
                          .exit = "produced -> {}; produced -> {}; produced -> {}"}});
        run({.contents = {.what =
                              "mixed comparison and later Sub have independent retention proofs",
                          .body = threeCategories + compareInput("%operand") +
                                  "  %bad = ctjs.binary sub %operand, %zero\n" + done,
                          .arrays = "a:[x] | a:[x] | a:[x]",
                          .exit = "produced -> {}; produced -> {}; produced -> {}"}});
        run({.contents = {.what =
                              "mixed comparison and later Mul have independent retention proofs",
                          .body = threeCategories + compareInput("%operand") +
                                  "  %bad = ctjs.binary mul %operand, %zero\n" + done,
                          .arrays = "a:[x] | a:[x] | a:[x]",
                          .exit = "produced -> {}; produced -> {}; produced -> {}"}});
        run({.contents = {.what =
                              "mixed comparison and later Div have independent retention proofs",
                          .body = threeCategories + compareInput("%operand") +
                                  "  %bad = ctjs.binary div %operand, %zero\n" + done,
                          .arrays = "a:[x] | a:[x] | a:[x]",
                          .exit = "produced -> {}; produced -> {}; produced -> {}"}});
        run({.contents = {.what =
                              "mixed comparison and later Mod have independent retention proofs",
                          .body = threeCategories + compareInput("%operand") +
                                  "  %bad = ctjs.binary mod %operand, %zero\n" + done,
                          .arrays = "a:[x] | a:[x] | a:[x]",
                          .exit = "produced -> {}; produced -> {}; produced -> {}"}});
        run({.contents = {.what = "mixed comparison cannot authorize mixed Pow conversion",
                          .body = threeCategories + compareInput("%operand") +
                                  "  %bad = ctjs.binary pow %operand, %zero\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}});
        for (const std::string saved : {"%lhs", "%zero", "%text", "%x"}) {
            run({.contents = {
                     .what = "both saved comparison operands survive independent slot mutations",
                     .body = values +
                             "  %text = ctjs.constant #ctjs.string<\"1.5\">\n"
                             "  %key = ctjs.constant #ctjs.string<\"operand\">\n"
                             "  ctjs.set_property %x[%key], " +
                             saved +
                             "\n  %operand = ctjs.get_property %x[%key]\n"
                             "  ctjs.set_property %a[%zero], %rhs\n"
                             "  %savedBig = ctjs.get_property %a[%zero]\n"
                             "  ctjs.set_property %a[%zero], %zero\n"
                             "  ctjs.set_property %x[%key], %zero\n"
                             "  ctjs.delete_named \"operand\" from %x\n" +
                             (left ? compare("%operand", "%savedBig")
                                   : compare("%savedBig", "%operand")) +
                             done,
                     .failure = ArrayContentsFailure::UnsupportedOperation,
                     .arrays = "a:[zero]",
                     .reads = "a[0]=rhs",
                     .exit = "produced -> {}"}});
        }
        if (producerKind != ctjs::CompareKind::Eq) {
            for (const std::string producer : {
                     "  %operand = ctjs.binary add %lhs, %rhs\n",
                     "  %operand = ctjs.unary neg %lhs\n",
                 }) {
                run({.contents = {.what = "computed BigInts require independent category evidence",
                                  .body = values + producer + compareInput("%operand") + done,
                                  .arrays = "a:[x]",
                                  .exit = "produced -> {}"}});
            }
            run({.contents = {.what =
                                  "a saved BigInt array value survives a later Number overwrite",
                              .body = values +
                                      "  ctjs.set_property %a[%zero], %lhs\n"
                                      "  %operand = ctjs.get_property %a[%zero]\n"
                                      "  ctjs.set_property %a[%zero], %zero\n" +
                                      compareInput("%operand") + done,
                              .arrays = "a:[zero]",
                              .reads = "a[0]=lhs",
                              .exit = "produced -> {}"}});
            run({.contents = {.what =
                                  "a saved BigInt own field survives a changed tag and deletion",
                              .body = values +
                                      "  %key = ctjs.constant #ctjs.string<\"operand\">\n"
                                      "  ctjs.set_property %x[%key], %lhs\n"
                                      "  %operand = ctjs.get_property %x[%key]\n"
                                      "  ctjs.set_property %x[%key], %zero\n"
                                      "  ctjs.delete_named \"operand\" from %x\n" +
                                      compareInput("%operand") + done,
                              .failure = ArrayContentsFailure::UnsupportedOperation,
                              .arrays = "a:[x]",
                              .exit = "produced -> {}"}});
        }
        for (const std::string & input : primitiveOrigins) {
            run({.contents = {
                     .what = "each mixed BigInt operand independently proves a primitive category",
                     .body = values + input + compareInput("%operand") + done,
                     .arrays = "a:[x]",
                     .exit = "produced -> {}"}});
        }
        for (const std::string input : {"%lhs", "%zero", "%x", "%a", "%p"}) {
            const bool primitive = input == "%lhs" || input == "%zero";
            run({.contents = {
                     .what = "every incoming BigInt comparison operand must independently qualify",
                     .body = values +
                             "  %flag = ctjs.truthy %p\n"
                             "  cf.cond_br %flag, ^join(%rhs : !ctjs.value), ^join(" +
                             input + " : !ctjs.value)\n^join(%operand: !ctjs.value):\n" +
                             compareInput("%operand") + done,
                     .failure = primitive ? ArrayContentsFailure::None
                                          : ArrayContentsFailure::UnsupportedOperation,
                     .arrays = "a:[x] | a:[x]",
                     .exit = "produced -> {}; produced -> {}"}});
            run({.contents = {.what = "BigInt comparison keeps saved array origins after overwrite",
                              .body = values + "  ctjs.set_property %a[%zero], " + input +
                                      "\n  %operand = ctjs.get_property %a[%zero]\n"
                                      "  ctjs.set_property %a[%zero], %rhs\n" +
                                      compareInput("%operand") + done,
                              .failure = primitive ? ArrayContentsFailure::None
                                         : input == "%p"
                                             ? ArrayContentsFailure::UnknownValue
                                             : ArrayContentsFailure::UnsupportedOperation,
                              .arrays = "a:[rhs]",
                              .reads = input == "%zero" ? "a[0]=zero" : "a[0]=lhs",
                              .exit = "produced -> {}"}});
        }
        for (const std::string input : {"%lhs", "%zero", "%x"}) {
            run({.contents = {
                     .what =
                         "BigInt comparison keeps saved own fields after overwrite and deletion",
                     .body = values +
                             "  %key = ctjs.constant #ctjs.string<\"operand\">\n"
                             "  ctjs.set_property %x[%key], " +
                             input +
                             "\n  %operand = ctjs.get_property %x[%key]\n"
                             "  ctjs.set_property %x[%key], %rhs\n"
                             "  ctjs.delete_named \"operand\" from %x\n" +
                             compareInput("%operand") + done,
                     .failure = ArrayContentsFailure::UnsupportedOperation,
                     .arrays = "a:[x]",
                     .exit = "produced -> {}"}});
        }
    }
    for (const std::string effect : {
             "  ctjs.store_global \"held\", %a\n",
             "  %called = ctjs.call %p(%a)\n",
             "  \"test.effect\"(%produced) : (!ctjs.value) -> ()\n",
         }) {
        run({.contents = {
                 .what = "BigInt comparison cannot authorize publication calls or unknown effects",
                 .body = values + produce + effect + done,
                 .failure = ArrayContentsFailure::UnsupportedOperation}});
    }
    run({.contents = {.what = "BigInt comparison cannot authorize an explicit throw",
                      .body = values + produce + "  ctjs.throw %x\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation}});
    run({.contents = {.what = "BigInt comparison cannot authorize a local exception handler",
                      .body = values + "  ctjs.push_handler ^body catch ^pad\n^body:\n" + produce +
                              "  ctjs.pop_handler\n" + overwrite +
                              "^pad:\n  %id, %exception = ctjs.catch_land\n  ctjs.return %a\n",
                      .failure = ArrayContentsFailure::UnsupportedControlFlow}});
    bigint_row wide = rows.front();
    std::string extras;
    for (unsigned i = 0; i < 32; ++i) {
        extras += "  %extra_" + std::to_string(i) + " = ctjs.compare " + spelling + " %lhs, %rhs\n";
    }
    wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
    auto narrowModule = parse(rows.front());
    auto wideModule = parse(wide);
    if (narrowModule && wideModule) {
        const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
        const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
            fail(row{.what = "BigInt comparison charges each independent result and snapshot",
                     .body = wide.contents.body,
                     .expected = ""},
                 "32 extra results did not cost one producer and one snapshot each");
        }
        check(*wideModule, wide);
    } else {
        fail(row{.what = "wide BigInt comparison snapshot",
                 .body = wide.contents.body,
                 .expected = ""},
             "the wide BigInt comparison fixture did not parse");
    }
    bigint_row mutation = rows.front();
    mutation.contents.what = "live BigInt comparison origins defeat forged completion";
    unsigned liveStates = 0;
    if (auto module = parse(mutation)) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::CompareOp comparison;
        ctjs::CreateObjectOp child;
        ctjs::CreateArrayOp array;
        ctjs::ConstantOp zero;
        module->walk([&](ctjs::CompareOp op) { comparison = op; });
        module->walk([&](ctjs::CreateObjectOp op) { child = op; });
        module->walk([&](ctjs::CreateArrayOp op) { array = op; });
        module->walk([&](ctjs::ConstantOp op) {
            if (llvm::isa<ctjs::NumberAttr>(op.getValue())) { zero = op; }
        });
        mlir::OpBuilder builder(comparison);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        child->setAttr("ctnative.confined", builder.getUnitAttr());
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*module))) {
            fail(row{.what = "mixed BigInt comparison stale solver",
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
                fail(row{.what = "live comparison categories defeat cached and forged evidence",
                         .body = mutation.contents.body,
                         .expected = mutation.discharged},
                     "a stale solver authorized an unproved original operand");
            }
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        for (unsigned position = 0; position < 2; ++position) {
            const mlir::Value original = comparison->getOperand(position);
            const mlir::Value invalid[] = {zero.getResult(), child.getResult(), array.getResult(),
                                           function.getBody().front().getArgument(3)};
            for (mlir::Value bad : invalid) {
                comparison->setOperand(position, bad);
                inspect(bad == zero.getResult() ? ArrayContentsFailure::None
                                                : ArrayContentsFailure::UnsupportedOperation);
                comparison->setOperand(position, original);
                inspect(ArrayContentsFailure::None);
            }
            auto constant = original.getDefiningOp<ctjs::ConstantOp>();
            const mlir::Attribute oldValue = constant.getValue();
            for (const mlir::Attribute replacement :
                 {mlir::Attribute(ctjs::UndefinedAttr::get(&context)),
                  mlir::Attribute(ctjs::NullAttr::get(&context)),
                  mlir::Attribute(ctjs::BooleanAttr::get(&context, true)),
                  mlir::Attribute(zero.getValue()),
                  mlir::Attribute(ctjs::StringAttr::get(&context, "1.5")),
                  mlir::Attribute(ctjs::BigIntAttr::get(&context, "1"))}) {
                constant.setValueAttr(replacement);
                inspect(ArrayContentsFailure::None);
                constant.setValueAttr(oldValue);
                inspect(ArrayContentsFailure::None);
            }
        }
        for (const auto kind :
             {ctjs::CompareKind::StrictEq, ctjs::CompareKind::Eq, ctjs::CompareKind::Lt,
              ctjs::CompareKind::Le, ctjs::CompareKind::Gt, ctjs::CompareKind::Ge}) {
            comparison.setKindAttr(ctjs::CompareKindAttr::get(&context, kind));
            inspect(ArrayContentsFailure::None);
        }
        comparison.setKindAttr(ctjs::CompareKindAttr::get(&context, producerKind));
        inspect(ArrayContentsFailure::None);
        if (producerKind != ctjs::CompareKind::Eq) {
            comparison.setKindAttr(
                ctjs::CompareKindAttr::get(&context, static_cast<ctjs::CompareKind>(255)));
            inspect(ArrayContentsFailure::UnsupportedOperation);
            comparison.setKindAttr(ctjs::CompareKindAttr::get(&context, producerKind));
            inspect(ArrayContentsFailure::None);
        }
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
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live BigInt comparison fixture did not parse");
    }
    std::printf("BigInt comparison %s: %u rows, %u stale/fresh live states, one wide snapshot, "
                "%zu retention budget cutoffs\n",
                spelling.c_str(), rowCount, liveStates, budgets);
}

} // namespace ctcompile::test::escape::arrays
