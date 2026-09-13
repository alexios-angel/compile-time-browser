#include "Harness.h"

namespace ctcompile::test::escape::arrays {

void checkBigIntUnaryProducers(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %big = ctjs.constant #ctjs.bigint<\"9007199254740993\"> "
        "{storage_test_id = \"big\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string done = "  ctjs.return %produced\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct unary_row {
        contents_row contents;
        const char * discharged = "x";
    };
    for (const auto kind : {ctjs::UnaryKind::Neg, ctjs::UnaryKind::BitNot}) {
        const std::string spelling = ctjs::stringifyUnaryKind(kind).str();
        const std::string produce =
            "  %produced = ctjs.unary " + spelling + " %big {storage_test_id = \"produced\"}\n";
        const std::vector<unary_row> rows = {
            {.contents = {.what = "a computed BigInt cannot prune either overwrite path",
                          .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                          .arrays = "a:[zero] | a:[zero]",
                          .exit = "a -> {a}; a -> {a}"}},
            {.contents = {.what = "BigInt unary result carries no operand object identity",
                          .body = values + produce + done,
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "storing an independent BigInt releases the old child",
                          .body = values + produce +
                                  "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %a\n",
                          .arrays = "a:[produced]",
                          .exit = "a -> {a}"}},
            {.contents = {.what = "a separately saved child stays retained after BigInt overwrite",
                          .body = values + "  %saved = ctjs.get_property %a[%zero]\n" + produce +
                                  "  ctjs.set_property %a[%zero], %produced\n"
                                  "  ctjs.return %saved\n",
                          .arrays = "a:[produced]",
                          .reads = "a[0]=x",
                          .exit = "x -> {x}"},
             .discharged = ""},
            {.contents = {.what = "computed BigInt category survives frame and edge transport",
                          .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                                  "  cf.br ^next(%produced : !ctjs.value)\n"
                                  "^next(%operand: !ctjs.value):\n"
                                  "  %next = ctjs.unary " +
                                  spelling +
                                  " %operand {storage_test_id = \"next\"}\n"
                                  "  ctjs.root %next in %frame\n  ctjs.frame_exit %frame\n"
                                  "  ctjs.return %next\n",
                          .arrays = "a:[x]",
                          .exit = "next -> {}"}},
            {.contents = {.what = "saved computed BigInt array origins survive Number overwrite",
                          .body = values + produce +
                                  "  ctjs.set_property %a[%zero], %produced\n"
                                  "  %saved = ctjs.get_property %a[%zero]\n"
                                  "  ctjs.set_property %a[%zero], %zero\n"
                                  "  %next = ctjs.compare eq %saved, %big "
                                  "{storage_test_id = \"next\"}\n  ctjs.return %next\n",
                          .arrays = "a:[zero]",
                          .reads = "a[0]=produced",
                          .exit = "next -> {}"}},
            {.contents = {.what = "saved computed BigInt own fields survive overwrite and deletion",
                          .body = values + produce +
                                  "  %key = ctjs.constant #ctjs.string<\"value\">\n"
                                  "  ctjs.set_property %x[%key], %produced\n"
                                  "  %saved = ctjs.get_property %x[%key]\n"
                                  "  ctjs.set_property %x[%key], %zero\n"
                                  "  ctjs.delete_named \"value\" from %x\n"
                                  "  %next = ctjs.compare eq %saved, %big "
                                  "{storage_test_id = \"next\"}\n  ctjs.return %next\n",
                          .failure = ArrayContentsFailure::UnsupportedOperation,
                          .arrays = "a:[x]",
                          .exit = "next -> {}"}},
            {.contents = {.what = "computed BigInt cannot supply a numeric array index",
                          .body = values + produce + "  %read = ctjs.get_property %a[%produced]\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownIndex}},
            {.contents = {.what = "computed BigInt cannot supply an own String key",
                          .body = values + produce + "  ctjs.set_property %x[%produced], %zero\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "computed BigInt Plus errors carry no local object identity",
                          .body = values + produce + "  %next = ctjs.unary plus %produced\n" + done,
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "an unrelated primitive does not establish opaque retention",
                          .body = values + produce + "  ctjs.return %p\n",
                          .failure = ArrayContentsFailure::UnknownValue}},
        };
        unsigned rowCount = 0;
        unsigned liveStates = 0;
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
        const auto run = [&](const unary_row & expected) {
            if (auto module = parse(expected)) {
                check(*module, expected);
            } else {
                fail(row{.what = expected.contents.what,
                         .body = expected.contents.body,
                         .expected = ""},
                     "the computed BigInt unary fixture did not parse");
            }
            ++rowCount;
        };
        for (const auto & expected : rows) { run(expected); }
        for (const std::string form : {"binary", "binary_static"}) {
            const std::vector<std::string> kinds =
                form == "binary"
                    ? std::vector<std::string>{"add", "sub", "mul", "div", "mod", "pow", "concat"}
                    : std::vector<std::string>{"add", "bitand", "bitor", "bitxor",
                                               "shl", "shr",    "ushr"};
            for (const std::string & operation : kinds) {
                for (const std::string operands :
                     {"%produced, %zero", "%zero, %produced", "%produced, %big"}) {
                    run({.contents = {
                             .what =
                                 "computed BigInt requires an independent binary category proof",
                             .body = values + produce + "  %next = ctjs." + form + " " + operation +
                                     " " + operands + "\n" + done,
                             .arrays = "a:[x]",
                             .exit = "produced -> {}"}});
                }
            }
        }
        for (const std::string comparison : {"eq", "lt", "le", "gt", "ge"}) {
            for (const bool mixed : {false, true}) {
                for (const bool left : {false, true}) {
                    const std::string other = mixed ? "%zero" : "%big";
                    const std::string operands =
                        left ? "%produced, " + other : other + ", %produced";
                    run({.contents = {.what = "both comparison operands independently prove their "
                                              "original category",
                                      .body =
                                          values + produce + "  %next = ctjs.compare " +
                                          comparison + " " + operands +
                                          " {storage_test_id = \"next\"}\n  ctjs.return %next\n",
                                      .arrays = "a:[x]",
                                      .exit = "next -> {}"}});
                }
            }
        }
        for (const std::string operation : {"not", "typeof", "void"}) {
            run({.contents = {
                     .what = "a later total unary result has its independent non-BigInt category",
                     .body = values + produce + "  %next = ctjs.unary " + operation +
                             " %produced\n  %sum = ctjs.binary add %next, %zero "
                             "{storage_test_id = \"sum\"}\n  ctjs.return %sum\n",
                     .arrays = "a:[x]",
                     .exit = "sum -> {}"}});
        }
        for (const bool reversed : {false, true}) {
            const std::string actuals =
                reversed ? "%zero : !ctjs.value), ^join(%big" : "%big : !ctjs.value), ^join(%zero";
            const std::string paths =
                values + "  %flag = ctjs.truthy %p\n  cf.cond_br %flag, ^join(" + actuals +
                " : !ctjs.value)\n^join(%operand: !ctjs.value):\n"
                "  %produced = ctjs.unary " +
                spelling + " %operand {storage_test_id = \"produced\"}\n";
            run({.contents = {.what = "one SSA producer has independent BigInt and Number "
                                      "categories on separate paths",
                              .body = paths + done,
                              .arrays = "a:[x] | a:[x]",
                              .exit = "produced -> {}; produced -> {}"}});
            run({.contents = {.what = "both path categories permit retention through later Add",
                              .body = paths + "  %next = ctjs.binary add %produced, %zero\n" + done,
                              .arrays = "a:[x] | a:[x]",
                              .exit = "produced -> {}; produced -> {}"}});
        }
        for (const std::string effect :
             {"  ctjs.store_global \"held\", %a\n", "  %called = ctjs.call %p(%a)\n",
              "  \"test.effect\"(%produced) : (!ctjs.value) -> ()\n"}) {
            run({.contents = {
                     .what =
                         "BigInt production cannot authorize later publication calls or effects",
                     .body = values + produce + effect + done,
                     .failure = ArrayContentsFailure::UnsupportedOperation}});
        }
        unary_row wide = rows.front();
        std::string extras;
        for (unsigned i = 0; i < 32; ++i) {
            extras += "  %extra_" + std::to_string(i) + " = ctjs.unary " + spelling + " %big\n";
        }
        wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
        auto narrowModule = parse(rows.front());
        auto wideModule = parse(wide);
        if (narrowModule && wideModule) {
            const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
            const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
            if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 96) {
                fail(row{.what = "BigInt snapshots charge each held result once",
                         .body = wide.contents.body,
                         .expected = ""},
                     "32 results did not charge their operations, categories and held snapshots");
            }
            check(*wideModule, wide);
        } else {
            fail(row{.what = "wide BigInt unary snapshot",
                     .body = wide.contents.body,
                     .expected = ""},
                 "the BigInt unary snapshot fixture did not parse");
        }
        unary_row mutation = rows.front();
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
            const auto inspect = [&](ArrayContentsFailure failure) {
                mutation.contents.failure = failure;
                check(*module, mutation);
                ++liveStates;
            };
            inspect(ArrayContentsFailure::None);
            const mlir::Value input = unary.getOperand();
            const mlir::Value invalidInputs[] = {child.getResult(), array.getResult(),
                                                 function.getBody().front().getArgument(3)};
            for (mlir::Value invalid : invalidInputs) {
                unary->setOperand(0, invalid);
                inspect(ArrayContentsFailure::UnsupportedOperation);
                unary->setOperand(0, input);
                inspect(ArrayContentsFailure::None);
            }
            for (const auto other : {ctjs::UnaryKind::Plus, static_cast<ctjs::UnaryKind>(255)}) {
                unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, other));
                inspect(other == ctjs::UnaryKind::Plus
                            ? ArrayContentsFailure::None
                            : ArrayContentsFailure::UnsupportedOperation);
                unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, kind));
                inspect(ArrayContentsFailure::None);
            }
            auto constant = input.getDefiningOp<ctjs::ConstantOp>();
            const mlir::Attribute oldValue = constant.getValue();
            constant.setValueAttr(ctjs::NumberAttr::get(&context, 0));
            inspect(ArrayContentsFailure::None);
            constant.setValueAttr(oldValue);
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
            fail(row{.what = "live computed BigInt unary",
                     .body = mutation.contents.body,
                     .expected = ""},
                 "the BigInt unary mutation fixture did not parse");
        }
        std::printf("BigInt unary %s: %u rows, %u live states, one wide snapshot, %zu retention "
                    "budget cutoffs\n",
                    spelling.c_str(), rowCount, liveStates, budgets);
    }
}

void checkBigIntBinaryProducers(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %lhs = ctjs.constant #ctjs.bigint<\"9007199254740993\"> "
        "{storage_test_id = \"lhs\"}\n"
        "  %rhs = ctjs.constant #ctjs.bigint<\"2\"> {storage_test_id = \"rhs\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string done = "  ctjs.return %produced\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct binary_row {
        contents_row contents;
        const char * discharged = "x";
    };
    struct binary_kind {
        ctjs::BinaryKind kind;
        bool isStatic;
    };
    for (const auto & [kind, isStatic] :
         {binary_kind{ctjs::BinaryKind::Add, false}, binary_kind{ctjs::BinaryKind::Sub, false},
          binary_kind{ctjs::BinaryKind::Mul, false}, binary_kind{ctjs::BinaryKind::Div, false},
          binary_kind{ctjs::BinaryKind::Mod, false}, binary_kind{ctjs::BinaryKind::Pow, false},
          binary_kind{ctjs::BinaryKind::Add, true}, binary_kind{ctjs::BinaryKind::BitAnd, true},
          binary_kind{ctjs::BinaryKind::BitOr, true}, binary_kind{ctjs::BinaryKind::BitXor, true},
          binary_kind{ctjs::BinaryKind::Shl, true}, binary_kind{ctjs::BinaryKind::Shr, true}}) {
        const std::string form = isStatic ? "binary_static" : "binary";
        const std::string spelling = ctjs::stringifyBinaryKind(kind).str();
        const auto binary = [&](const std::string & lhs, const std::string & rhs) {
            return "  %produced = ctjs." + form + " " + spelling + " " + lhs + ", " + rhs +
                   " {storage_test_id = \"produced\"}\n";
        };
        const std::string produce = binary("%lhs", "%rhs");
        const std::vector<binary_row> rows = {
            {.contents = {.what = "computed BigInt arithmetic cannot select a structural path",
                          .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                          .arrays = "a:[zero] | a:[zero]",
                          .exit = "a -> {a}; a -> {a}"}},
            {.contents = {.what = "BigInt binary results carry neither operand identity",
                          .body = values + produce + done,
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "storing a computed BigInt releases the old child",
                          .body = values + produce +
                                  "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %a\n",
                          .arrays = "a:[produced]",
                          .exit = "a -> {a}"}},
            {.contents = {.what = "a saved child remains retained after BigInt arithmetic",
                          .body =
                              values + "  %saved = ctjs.get_property %a[%zero]\n" + produce +
                              "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %saved\n",
                          .arrays = "a:[produced]",
                          .reads = "a[0]=x",
                          .exit = "x -> {x}"},
             .discharged = ""},
            {.contents = {.what = "computed BigInt cannot serve as a numeric array index",
                          .body = values + produce + "  %read = ctjs.get_property %a[%produced]\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownIndex}},
            {.contents = {.what = "computed BigInt cannot serve as an own String key",
                          .body = values + produce + "  ctjs.set_property %x[%produced], %zero\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "binary BigInt category survives exact frame and edge transport",
                          .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                                  "  cf.br ^next(%produced : !ctjs.value)\n"
                                  "^next(%operand: !ctjs.value):\n"
                                  "  %next = ctjs." +
                                  form + " " + spelling +
                                  " %operand, %rhs {storage_test_id = \"next\"}\n"
                                  "  ctjs.root %next in %frame\n  ctjs.frame_exit %frame\n"
                                  "  ctjs.return %next\n",
                          .arrays = "a:[x]",
                          .exit = "next -> {}"}},
        };
        unsigned rowCount = 0;
        unsigned liveStates = 0;
        std::size_t budgets = 0;
        const auto check = [&](mlir::ModuleOp module, const binary_row & expected) {
            checkArrayContents(module, expected.contents);
            const bool complete = expected.contents.failure == ArrayContentsFailure::None;
            budgets +=
                checkArrayRetention(module, {.what = expected.contents.what,
                                             .body = expected.contents.body,
                                             .discharged = complete ? expected.discharged : "",
                                             .complete = complete});
        };
        const auto parse = [&](const binary_row & expected) {
            return mlir::parseSourceString<mlir::ModuleOp>(
                std::string{kPrologue} + expected.contents.body + "}\n", &context);
        };
        const auto run = [&](const binary_row & expected) {
            if (auto module = parse(expected)) {
                check(*module, expected);
            } else {
                fail(row{.what = expected.contents.what,
                         .body = expected.contents.body,
                         .expected = ""},
                     "the computed BigInt binary fixture did not parse");
            }
            ++rowCount;
        };
        for (const auto & expected : rows) { run(expected); }
        for (const bool left : {false, true}) {
            const auto operate = [&](const std::string & input) {
                return left ? binary(input, "%rhs") : binary("%lhs", input);
            };
            for (const std::string input : {"%zero", "%p", "%x", "%a"}) {
                run({.contents = {.what = "each BigInt binary operand needs independent provenance",
                                  .body = values + operate(input) + done,
                                  .failure = input == "%zero" ? ArrayContentsFailure::None
                                             : isStatic && input == "%p"
                                                 ? ArrayContentsFailure::UnknownValue
                                                 : ArrayContentsFailure::UnsupportedOperation,
                                  .arrays = "a:[x]",
                                  .exit = "produced -> {}"}});
            }
            for (const std::string attribute :
                 {"#ctjs.undefined", "#ctjs.null", "#ctjs.boolean<true>", "#ctjs.string<\"2\">"}) {
                run({.contents = {
                         .what = "mixed primitive arithmetic needs an independent operation proof",
                         .body = values + "  %input = ctjs.constant " + attribute + "\n" +
                                 operate("%input") + done,
                         .arrays = "a:[x]",
                         .exit = "produced -> {}"}});
            }
            for (const std::string operation : {"add", "sub", "mul", "div", "mod", "pow"}) {
                run({.contents = {
                         .what = "computed binary operands retain their original BigInt category",
                         .body = values + "  %input = ctjs.binary " + operation + " %lhs, %rhs\n" +
                                 operate("%input") + done,
                         .arrays = "a:[x]",
                         .exit = "produced -> {}"}});
            }
            for (const std::string operation : {"add", "bitand", "bitor", "bitxor", "shl", "shr"}) {
                run({.contents = {
                         .what =
                             "static BigInt operands retain their independent original category",
                         .body = values + "  %input = ctjs.binary_static " + operation +
                                 " %lhs, %rhs\n" + operate("%input") + done,
                         .arrays = "a:[x]",
                         .exit = "produced -> {}"}});
            }
            for (const std::string operation : {"neg", "bitnot"}) {
                run({.contents = {.what = "computed unary operands keep BigInt binary provenance",
                                  .body = values + "  %input = ctjs.unary " + operation +
                                          " %lhs\n" + operate("%input") + done,
                                  .arrays = "a:[x]",
                                  .exit = "produced -> {}"}});
            }
            run({.contents = {.what =
                                  "saved computed array operands survive later category changes",
                              .body = values + "  %input = ctjs." + form + " " + spelling +
                                      " %lhs, %rhs {storage_test_id = \"input\"}\n"
                                      "  ctjs.set_property %a[%zero], %input\n"
                                      "  %saved = ctjs.get_property %a[%zero]\n"
                                      "  ctjs.set_property %a[%zero], %zero\n" +
                                      operate("%saved") + done,
                              .arrays = "a:[zero]",
                              .reads = "a[0]=input",
                              .exit = "produced -> {}"}});
            run({.contents = {.what = "saved computed own fields survive overwrite and deletion",
                              .body = values + "  %input = ctjs." + form + " " + spelling +
                                      " %lhs, %rhs {storage_test_id = \"input\"}\n"
                                      "  %key = ctjs.constant #ctjs.string<\"operand\">\n"
                                      "  ctjs.set_property %x[%key], %input\n"
                                      "  %saved = ctjs.get_property %x[%key]\n"
                                      "  ctjs.set_property %x[%key], %zero\n"
                                      "  ctjs.delete_named \"operand\" from %x\n" +
                                      operate("%saved") + done,
                              .failure = ArrayContentsFailure::UnsupportedOperation,
                              .arrays = "a:[x]",
                              .exit = "produced -> {}"}});
        }
        for (const std::string consumerForm : {"binary", "binary_static"}) {
            const std::vector<std::string> operations =
                consumerForm == "binary"
                    ? std::vector<std::string>{"add", "sub", "mul", "div", "mod", "pow", "concat"}
                    : std::vector<std::string>{"add", "bitand", "bitor", "bitxor",
                                               "shl", "shr",    "ushr"};
            for (const std::string & operation : operations) {
                for (const std::string operands :
                     {"%produced, %zero", "%zero, %produced", "%produced, %rhs"}) {
                    run({.contents = {
                             .what = "binary BigInt consumers prove retention independently",
                             .body = values + produce + "  %next = ctjs." + consumerForm + " " +
                                     operation + " " + operands +
                                     " {storage_test_id = \"next\"}\n  ctjs.return %next\n",
                             .arrays = "a:[x]",
                             .exit = "next -> {}"}});
                }
            }
        }
        for (const std::string operation : {"eq", "lt", "le", "gt", "ge"}) {
            run({.contents = {
                     .what = "binary BigInt categories feed independent comparisons",
                     .body = values + produce + "  %next = ctjs.compare " + operation +
                             " %produced, %rhs {storage_test_id = \"next\"}\n  ctjs.return %next\n",
                     .arrays = "a:[x]",
                     .exit = "next -> {}"}});
        }
        run({.contents = {.what = "computed binary BigInt Plus errors carry no local identity",
                          .body = values + produce + "  %next = ctjs.unary plus %produced\n" + done,
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}});
        for (const std::string operation : {"div", "mod", "pow"}) {
            const std::string operand = operation == "pow" ? "-1" : "0";
            run({.contents = {
                     .what = "BigInt exceptional exits establish retention but not completion",
                     .body = values + produce + "  %exceptional = ctjs.constant #ctjs.bigint<\"" +
                             operand + "\">\n  %next = ctjs.binary " + operation +
                             " %produced, %exceptional\n" + done,
                     .arrays = "a:[x]",
                     .exit = "produced -> {}"}});
        }
        if (!isStatic && (kind == ctjs::BinaryKind::Div || kind == ctjs::BinaryKind::Mod)) {
            for (const std::string divisor : {"0", "-2", "9007199254740993"}) {
                const std::string divided = values + "  %divisor = ctjs.constant #ctjs.bigint<\"" +
                                            divisor + "\">\n" + binary("%lhs", "%divisor");
                run({.contents = {
                         .what =
                             "zero signed and wide divisors supply only result category evidence",
                         .body = divided + done,
                         .arrays = "a:[x]",
                         .exit = "produced -> {}"}});
                for (const std::string effect :
                     {"  ctjs.store_global \"held\", %a\n", "  %called = ctjs.call %p(%a)\n",
                      "  \"test.effect\"(%produced) : (!ctjs.value) -> ()\n"}) {
                    run({.contents = {
                             .what = "even zero divisors cannot hide later unsupported effects",
                             .body = divided + effect + done,
                             .failure = ArrayContentsFailure::UnsupportedOperation}});
                }
            }
            for (const std::string digits : {"-9", "0"}) {
                run({.contents = {.what =
                                      "signed and zero dividends keep independent BigInt origins",
                                  .body = values + "  %input = ctjs.constant #ctjs.bigint<\"" +
                                          digits + "\">\n" + binary("%input", "%rhs") + done,
                                  .arrays = "a:[x]",
                                  .exit = "produced -> {}"}});
            }
            for (const std::string exponent : {"0", "-1", "9007199254740993"}) {
                run({.contents = {.what = "computed exponents retain independent result categories",
                                  .body = values + produce +
                                          "  %exponent = ctjs.constant #ctjs.bigint<\"" + exponent +
                                          "\">\n  %next = ctjs.binary pow %produced, %exponent\n" +
                                          done,
                                  .arrays = "a:[x]",
                                  .exit = "produced -> {}"}});
            }
        }
        if (!isStatic && kind == ctjs::BinaryKind::Pow) {
            for (const std::string exponent :
                 {"0", "-1", "4294967295", "4294967296", "9007199254740993"}) {
                const std::string powered = values + "  %exponent = ctjs.constant #ctjs.bigint<\"" +
                                            exponent + "\">\n" + binary("%lhs", "%exponent");
                run({.contents = {
                         .what = "negative capped and wide exponents prove only result categories",
                         .body = powered + done,
                         .arrays = "a:[x]",
                         .exit = "produced -> {}"}});
                for (const std::string effect :
                     {"  ctjs.store_global \"held\", %a\n", "  %called = ctjs.call %p(%a)\n",
                      "  \"test.effect\"(%produced) : (!ctjs.value) -> ()\n"}) {
                    run({.contents = {
                             .what = "throwing exponents cannot hide later publication or effects",
                             .body = powered + effect + done,
                             .failure = ArrayContentsFailure::UnsupportedOperation}});
                }
            }
            for (const std::string digits : {"-3", "-1", "0", "1"}) {
                run({.contents = {.what =
                                      "small or signed bases confer no value or completion fact",
                                  .body = values + "  %input = ctjs.constant #ctjs.bigint<\"" +
                                          digits + "\">\n" + binary("%input", "%rhs") + done,
                                  .arrays = "a:[x]",
                                  .exit = "produced -> {}"}});
            }
        }
        if (isStatic && (kind == ctjs::BinaryKind::Shl || kind == ctjs::BinaryKind::Shr)) {
            for (const std::string count : {"0", "-2", "9007199254740993", "-9007199254740993"}) {
                const std::string shifted = values + "  %count = ctjs.constant #ctjs.bigint<\"" +
                                            count + "\">\n" + binary("%lhs", "%count");
                run({.contents = {
                         .what = "signed shift counts supply only normal-result category evidence",
                         .body = shifted + done,
                         .arrays = "a:[x]",
                         .exit = "produced -> {}"}});
                run({.contents = {
                         .what = "even throwing shift counts cannot hide later unsupported effects",
                         .body = shifted + "  ctjs.store_global \"held\", %a\n" + done,
                         .failure = ArrayContentsFailure::UnsupportedOperation}});
            }
            for (const std::string digits : {"-9", "0"}) {
                run({.contents = {.what =
                                      "negative and zero shift inputs retain their BigInt category",
                                  .body = values + "  %input = ctjs.constant #ctjs.bigint<\"" +
                                          digits + "\">\n" + binary("%input", "%rhs") + done,
                                  .arrays = "a:[x]",
                                  .exit = "produced -> {}"}});
            }
        }
        for (const bool reversed : {false, true}) {
            const std::string big = "%lhs, %rhs : !ctjs.value, !ctjs.value";
            const std::string number = "%zero, %zero : !ctjs.value, !ctjs.value";
            const std::string paths =
                values +
                "  %flag = ctjs.truthy %p\n"
                "  cf.cond_br %flag, ^join(" +
                (reversed ? number : big) + "), ^join(" + (reversed ? big : number) +
                ")\n^join(%left: !ctjs.value, %right: !ctjs.value):\n" + binary("%left", "%right");
            run({.contents = {
                     .what = "one binary SSA result has separate Number and BigInt path categories",
                     .body = paths + done,
                     .arrays = "a:[x] | a:[x]",
                     .exit = "produced -> {}; produced -> {}"}});
            run({.contents = {.what = "each path independently proves later mixed Add retention",
                              .body = paths + "  %next = ctjs.binary add %produced, %zero\n" + done,
                              .arrays = "a:[x] | a:[x]",
                              .exit = "produced -> {}; produced -> {}"}});
        }
        for (const std::string effect :
             {"  ctjs.store_global \"held\", %a\n", "  %called = ctjs.call %p(%a)\n",
              "  \"test.effect\"(%produced) : (!ctjs.value) -> ()\n"}) {
            run({.contents = {.what =
                                  "BigInt arithmetic cannot authorize publication calls or effects",
                              .body = values + produce + effect + done,
                              .failure = ArrayContentsFailure::UnsupportedOperation}});
        }
        run({.contents = {.what = "BigInt arithmetic does not authorize a thrown object",
                          .body = values + produce + "  ctjs.throw %x\n",
                          .failure = ArrayContentsFailure::UnsupportedOperation}});
        run({.contents = {.what = "BigInt arithmetic supplies no handler or completion proof",
                          .body = values + "  ctjs.push_handler ^body catch ^pad\n^body:\n" +
                                  produce + "  ctjs.pop_handler\n" + overwrite +
                                  "^pad:\n  %id, %exception = ctjs.catch_land\n  ctjs.return %a\n",
                          .failure = ArrayContentsFailure::UnsupportedControlFlow}});
        binary_row wide = rows.front();
        std::string extras;
        for (unsigned i = 0; i < 32; ++i) {
            extras += "  %extra_" + std::to_string(i) + " = ctjs." + form + " " + spelling +
                      " %lhs, %rhs\n";
        }
        wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
        auto narrowModule = parse(rows.front());
        auto wideModule = parse(wide);
        if (narrowModule && wideModule) {
            const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
            const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
            if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 96) {
                fail(row{.what = "BigInt binary snapshots charge each held result once",
                         .body = wide.contents.body,
                         .expected = ""},
                     "32 binary results did not charge operations, categories and held snapshots");
            }
            check(*wideModule, wide);
        } else {
            fail(row{.what = "wide computed BigInt binary",
                     .body = wide.contents.body,
                     .expected = ""},
                 "the wide computed BigInt binary fixture did not parse");
        }
        binary_row mutation = rows.front();
        if (auto module = parse(mutation)) {
            auto function = *module->getOps<ctjs::FuncOp>().begin();
            mlir::Operation * producer = nullptr;
            ctjs::CreateObjectOp child;
            ctjs::CreateArrayOp array;
            module->walk([&](ctjs::BinaryOp op) {
                if (!isStatic) { producer = op.getOperation(); }
            });
            module->walk([&](ctjs::BinaryStaticOp op) {
                if (isStatic) { producer = op.getOperation(); }
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
                fail(row{.what = "computed BigInt stale solver",
                         .body = mutation.contents.body,
                         .expected = ""},
                     "the baseline solver did not converge");
            }
            const auto inspect = [&](ArrayContentsFailure failure) {
                mutation.contents.failure = failure;
                check(*module, mutation);
                const auto retained = computeVerdicts(stale, function);
                const bool complete = failure == ArrayContentsFailure::None;
                if (retained.arrayRetentionComplete != complete ||
                    retained.confinedStoredSites !=
                        (complete && mutation.discharged[0] != '\0' ? 1U : 0U)) {
                    fail(row{.what = "computed BigInt stale versus fresh evidence",
                             .body = mutation.contents.body,
                             .expected = mutation.discharged},
                         "a stale solver or forged marker supplied category authority");
                }
                ++liveStates;
            };
            inspect(ArrayContentsFailure::None);
            for (unsigned position = 0; position < 2; ++position) {
                const mlir::Value saved = producer->getOperand(position);
                const mlir::Value invalidInputs[] = {child.getResult(), array.getResult(),
                                                     function.getBody().front().getArgument(3)};
                for (mlir::Value input : invalidInputs) {
                    producer->setOperand(position, input);
                    inspect(isStatic && input == function.getBody().front().getArgument(3)
                                ? ArrayContentsFailure::UnknownValue
                                : ArrayContentsFailure::UnsupportedOperation);
                    producer->setOperand(position, saved);
                    inspect(ArrayContentsFailure::None);
                }
                auto constant = saved.getDefiningOp<ctjs::ConstantOp>();
                const auto oldValue = constant.getValue();
                constant.setValueAttr(ctjs::NumberAttr::get(&context, 0));
                inspect(ArrayContentsFailure::None);
                constant.setValueAttr(oldValue);
                inspect(ArrayContentsFailure::None);
            }
            const std::vector<ctjs::BinaryKind> otherKinds =
                isStatic ? std::vector<ctjs::BinaryKind>{ctjs::BinaryKind::Sub,
                                                         ctjs::BinaryKind::Mul,
                                                         ctjs::BinaryKind::Div,
                                                         ctjs::BinaryKind::Mod,
                                                         ctjs::BinaryKind::Pow,
                                                         ctjs::BinaryKind::Concat,
                                                         ctjs::BinaryKind::Shl,
                                                         ctjs::BinaryKind::Shr,
                                                         ctjs::BinaryKind::UShr,
                                                         static_cast<ctjs::BinaryKind>(255)}
                         : std::vector<ctjs::BinaryKind>{ctjs::BinaryKind::Div,
                                                         ctjs::BinaryKind::Mod,
                                                         ctjs::BinaryKind::Pow,
                                                         ctjs::BinaryKind::Concat,
                                                         ctjs::BinaryKind::BitAnd,
                                                         ctjs::BinaryKind::BitOr,
                                                         ctjs::BinaryKind::BitXor,
                                                         ctjs::BinaryKind::Shl,
                                                         ctjs::BinaryKind::Shr,
                                                         ctjs::BinaryKind::UShr,
                                                         static_cast<ctjs::BinaryKind>(255)};
            const auto setKind = [&](ctjs::BinaryKind value) {
                const auto attribute = ctjs::BinaryKindAttr::get(&context, value);
                // Preserve the deliberately invalid enum in its typed property.
                // Generic setAttr validates inherent attributes and can replace
                // this malformed test value with a null property instead.
                if (isStatic) {
                    llvm::cast<ctjs::BinaryStaticOp>(producer).setKindAttr(attribute);
                } else {
                    llvm::cast<ctjs::BinaryOp>(producer).setKindAttr(attribute);
                }
            };
            for (const auto other : otherKinds) {
                setKind(other);
                inspect(
                    (isStatic &&
                     (other == ctjs::BinaryKind::Shl || other == ctjs::BinaryKind::Shr ||
                      other == ctjs::BinaryKind::UShr)) ||
                            (!isStatic &&
                             (other == ctjs::BinaryKind::Div || other == ctjs::BinaryKind::Mod ||
                              other == ctjs::BinaryKind::Pow || other == ctjs::BinaryKind::Concat))
                        ? ArrayContentsFailure::None
                        : ArrayContentsFailure::UnsupportedOperation);
                setKind(kind);
                inspect(ArrayContentsFailure::None);
            }
            if (!isStatic && (kind == ctjs::BinaryKind::Div || kind == ctjs::BinaryKind::Mod)) {
                auto divisor = producer->getOperand(1).getDefiningOp<ctjs::ConstantOp>();
                const auto original = divisor.getValue();
                for (const std::string digits : {"0", "-2", "9007199254740993"}) {
                    divisor.setValueAttr(ctjs::BigIntAttr::get(&context, digits));
                    inspect(ArrayContentsFailure::None);
                    divisor.setValueAttr(original);
                    inspect(ArrayContentsFailure::None);
                }
            }
            if (!isStatic && kind == ctjs::BinaryKind::Pow) {
                auto exponent = producer->getOperand(1).getDefiningOp<ctjs::ConstantOp>();
                const auto original = exponent.getValue();
                for (const std::string digits :
                     {"0", "-1", "4294967295", "4294967296", "9007199254740993"}) {
                    exponent.setValueAttr(ctjs::BigIntAttr::get(&context, digits));
                    inspect(ArrayContentsFailure::None);
                    exponent.setValueAttr(original);
                    inspect(ArrayContentsFailure::None);
                }
            }
            if (isStatic && (kind == ctjs::BinaryKind::Shl || kind == ctjs::BinaryKind::Shr)) {
                auto count = producer->getOperand(1).getDefiningOp<ctjs::ConstantOp>();
                const auto original = count.getValue();
                for (const std::string digits :
                     {"0", "-2", "9007199254740993", "-9007199254740993"}) {
                    count.setValueAttr(ctjs::BigIntAttr::get(&context, digits));
                    inspect(ArrayContentsFailure::None);
                    count.setValueAttr(original);
                    inspect(ArrayContentsFailure::None);
                }
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
            fail(row{.what = "live computed BigInt binary",
                     .body = mutation.contents.body,
                     .expected = ""},
                 "the computed BigInt binary mutation fixture did not parse");
        }
        std::printf("BigInt %s %s: %u rows, %u stale/fresh live states, one wide snapshot, %zu "
                    "retention "
                    "budget cutoffs\n",
                    form.c_str(), spelling.c_str(), rowCount, liveStates, budgets);
    }
}

} // namespace ctcompile::test::escape::arrays
