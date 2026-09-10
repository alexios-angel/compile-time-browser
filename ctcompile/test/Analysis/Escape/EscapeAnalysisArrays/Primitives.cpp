#include "Harness.h"

namespace ctcompile::test::escape::arrays {

void checkStaticBinaryProducers(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %big = ctjs.constant #ctjs.bigint<\"1\">\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct binary_row {
        contents_row contents;
        const char * discharged = "x";
    };
    for (const auto kind : {ctjs::BinaryKind::Add, ctjs::BinaryKind::BitAnd,
                            ctjs::BinaryKind::BitOr, ctjs::BinaryKind::BitXor,
                            ctjs::BinaryKind::Shl, ctjs::BinaryKind::Shr, ctjs::BinaryKind::UShr}) {
        const std::string spelling = ctjs::stringifyBinaryKind(kind).str();
        const std::string operation = "  %produced = ctjs.binary_static " + spelling;
        const std::string produce = operation + " %zero, %zero {storage_test_id = \"produced\"}\n";
        const std::vector<binary_row> rows = {
            {.contents = {.what = "static Number results do not prune either overwrite arm",
                          .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                          .arrays = "a:[zero] | a:[zero]",
                          .exit = "a -> {a}; a -> {a}"}},
            {.contents = {.what = "static conversion of local objects retains neither operand",
                          .body = values + operation +
                                  " %x, %a {storage_test_id = \"produced\"}\n"
                                  "  ctjs.return %produced\n",
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "stored static Number results carry no operand identity",
                          .body = values + produce +
                                  "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %a\n",
                          .arrays = "a:[produced]",
                          .exit = "a -> {a}"}},
            {.contents = {.what = "a static result may forward and root independently",
                          .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                                  "  cf.br ^next(%p, %produced : !ctjs.value, !ctjs.value)\n"
                                  "^next(%opaque: !ctjs.value, %result: !ctjs.value):\n"
                                  "  ctjs.root %result in %frame\n  ctjs.frame_exit %frame\n"
                                  "  ctjs.return %result\n",
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "a saved child still retains its original identity",
                          .body = values + "  %saved = ctjs.get_property %a[%zero]\n" + operation +
                                  " %saved, %zero {storage_test_id = \"produced\"}\n"
                                  "  ctjs.set_property %a[%zero], %produced\n"
                                  "  ctjs.return %saved\n",
                          .arrays = "a:[produced]",
                          .reads = "a[0]=x",
                          .exit = "x -> {x}"},
             .discharged = ""},
            {.contents = {.what = "a static operation cannot exclude BigInt on an opaque lhs",
                          .body = values + operation + " %p, %zero\n" + done,
                          .failure = ArrayContentsFailure::UnknownValue}},
            {.contents = {.what = "a static operation cannot exclude BigInt on an opaque rhs",
                          .body = values + operation + " %zero, %p\n" + done,
                          .failure = ArrayContentsFailure::UnknownValue}},
            {.contents = {.what = "mixed BigInt lhs can throw before static conversion",
                          .body = values + operation + " %big, %zero\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "mixed BigInt rhs can throw before static conversion",
                          .body = values + operation + " %zero, %big\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "two BigInts need their independent static result category",
                          .body = values + operation + " %big, %big\n" + done,
                          .failure =
                              kind == ctjs::BinaryKind::Add || kind == ctjs::BinaryKind::BitAnd ||
                                      kind == ctjs::BinaryKind::BitOr ||
                                      kind == ctjs::BinaryKind::BitXor ||
                                      kind == ctjs::BinaryKind::Shl || kind == ctjs::BinaryKind::Shr
                                  ? ArrayContentsFailure::None
                                  : ArrayContentsFailure::UnsupportedOperation,
                          .arrays = "a:[x]",
                          .exit = "zero -> {}"}},
            {.contents = {.what = "a forwarded BigInt arm refuses the complete transaction",
                          .body = values +
                                  "  %flag = ctjs.truthy %p\n"
                                  "  cf.cond_br %flag, ^join(%zero : !ctjs.value), "
                                  "^join(%big : !ctjs.value)\n"
                                  "^join(%operand: !ctjs.value):\n" +
                                  operation + " %operand, %zero\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a later retained structural arm prevents Stored refinement",
                          .body =
                              values + produce + branch + overwrite + "^no:\n  ctjs.return %a\n",
                          .arrays = "a:[zero] | a:[x]",
                          .exit = "a -> {a}; a -> {a,x}"},
             .discharged = ""},
            {.contents = {.what = "a known numeric value cannot prune an unsupported arm",
                          .body = values + produce + branch + done +
                                  "^no:\n  ctjs.store_global \"held\", %x\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a static Number result is not an exact array index",
                          .body = values + produce + "  %read = ctjs.get_property %a[%produced]\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownIndex}},
            {.contents = {.what = "a static Number result is not an exact own String key",
                          .body = values + produce + "  ctjs.set_property %x[%produced], %zero\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownPropertyKey}},
            {.contents = {.what = "a static result cannot authorize returning an opaque value",
                          .body = values + produce + "  ctjs.return %p\n",
                          .failure = ArrayContentsFailure::UnknownValue}},
            {.contents = {.what = "static conversion cannot conceal an unsupported producer",
                          .body = values + "  %bad = \"test.value\"() : () -> !ctjs.value\n" +
                                  operation + " %zero, %bad\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "static results do not authorize a later unknown effect",
                          .body = values + produce +
                                  "  \"test.effect\"(%produced) : (!ctjs.value) -> ()\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "the generic binary family cannot borrow static conversion",
                          .body = values + "  %produced = ctjs.binary add %x, %zero\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
        };
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
                     "the static binary fixture did not parse");
            }
        };
        for (const auto & expected : rows) { run(expected); }

        // Each already-admitted primitive origin excludes BigInt independently.
        // Values loaded from containers must follow the original saved value,
        // not the container's newer contents or the get_property operation.
        const std::vector<std::string> origins = {
            "  %operand = ctjs.constant #ctjs.undefined\n",
            "  %operand = ctjs.constant #ctjs.null\n",
            "  %operand = ctjs.constant #ctjs.boolean<true>\n",
            "  %operand = ctjs.constant #ctjs.string<\"4294967297\">\n",
            "  %operand = ctjs.compare strict_eq %p, %zero\n",
            "  %operand = ctjs.convert to_boolean %p\n",
            "  %operand = ctjs.unary not %p\n",
            "  %operand = ctjs.unary typeof %p\n",
            "  %operand = ctjs.unary void %p\n",
            "  %operand = ctjs.binary_static " + spelling + " %zero, %zero\n",
        };
        for (const std::string & input : origins) {
            run({.contents = {.what = "a separately proved primitive origin excludes BigInt",
                              .body = values + input + operation +
                                      " %operand, %zero {storage_test_id = \"produced\"}\n"
                                      "  ctjs.return %produced\n",
                              .arrays = "a:[x]",
                              .exit = "produced -> {}"}});
        }
        for (const bool savedBigInt : {false, true}) {
            const std::string saved = savedBigInt ? "%big" : "%zero";
            const std::string replaced = savedBigInt ? "%zero" : "%big";
            run({.contents = {.what = "saved reads preserve their original BigInt exclusion",
                              .body = values + "  ctjs.set_property %a[%zero], " + saved +
                                      "\n  %operand = ctjs.get_property %a[%zero]\n"
                                      "  ctjs.set_property %a[%zero], " +
                                      replaced + "\n" + operation +
                                      " %operand, %zero {storage_test_id = \"produced\"}\n"
                                      "  ctjs.return %produced\n",
                              .failure = savedBigInt ? ArrayContentsFailure::UnsupportedOperation
                                                     : ArrayContentsFailure::None,
                              .arrays = "a:[ctjs.constant]",
                              .reads = "a[0]=zero",
                              .exit = "produced -> {}"}});
        }

        binary_row wide = rows.front();
        std::string extras;
        for (unsigned i = 0; i < 32; ++i) {
            extras += "  %extra_" + std::to_string(i) + " = ctjs.binary_static " + spelling +
                      " %zero, %zero\n";
        }
        wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
        auto narrowModule = parse(rows.front());
        auto wideModule = parse(wide);
        if (narrowModule && wideModule) {
            const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
            const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
            if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
                fail(row{.what = "static binary snapshots charge every primitive origin",
                         .body = wide.contents.body,
                         .expected = ""},
                     "32 extra results did not cost one producer and one snapshot each");
            }
            check(*wideModule, wide);
        } else {
            fail(row{.what = "wide static binary snapshot",
                     .body = wide.contents.body,
                     .expected = ""},
                 "the static binary snapshot fixture did not parse");
        }

        binary_row mutation = rows.front();
        mutation.contents.what = "live static operand and kind edits defeat forged completion";
        unsigned liveStates = 0;
        if (auto module = parse(mutation)) {
            ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
            ctjs::BinaryStaticOp binary;
            ctjs::CreateObjectOp child;
            ctjs::ConstantOp big;
            module->walk([&](ctjs::BinaryStaticOp op) { binary = op; });
            module->walk([&](ctjs::CreateObjectOp op) { child = op; });
            module->walk([&](ctjs::ConstantOp op) {
                if (llvm::isa<ctjs::BigIntAttr>(op.getValue())) { big = op; }
            });
            mlir::OpBuilder builder(binary);
            function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
            function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
            child->setAttr("ctnative.confined", builder.getUnitAttr());
            const auto inspect = [&](ArrayContentsFailure failure) {
                mutation.contents.failure = failure;
                check(*module, mutation);
                ++liveStates;
            };
            inspect(ArrayContentsFailure::None);
            const mlir::Value number = binary.getLhs();
            for (unsigned position : {0U, 1U}) {
                binary->setOperand(position, big.getResult());
                inspect(ArrayContentsFailure::UnsupportedOperation);
                binary->setOperand(position, function.getBody().front().getArgument(3));
                inspect(ArrayContentsFailure::UnknownValue);
                binary->setOperand(position, child.getResult());
                inspect(ArrayContentsFailure::None);
                binary->setOperand(position, number);
            }
            for (const auto invalid :
                 {ctjs::BinaryKind::Sub, ctjs::BinaryKind::Mul, ctjs::BinaryKind::Div,
                  ctjs::BinaryKind::Mod, ctjs::BinaryKind::Pow, ctjs::BinaryKind::Concat}) {
                binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, invalid));
                inspect(ArrayContentsFailure::UnsupportedOperation);
            }
            binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, kind));
            inspect(ArrayContentsFailure::None);
            auto constant = number.getDefiningOp<ctjs::ConstantOp>();
            const mlir::Attribute oldValue = constant.getValue();
            constant.setValueAttr(big.getValue());
            // This shared constant is also the later array index. Exact BigInt
            // arithmetic succeeds independently; it never authorizes that key.
            inspect(kind == ctjs::BinaryKind::Add || kind == ctjs::BinaryKind::BitAnd ||
                            kind == ctjs::BinaryKind::BitOr || kind == ctjs::BinaryKind::BitXor ||
                            kind == ctjs::BinaryKind::Shl || kind == ctjs::BinaryKind::Shr
                        ? ArrayContentsFailure::UnknownIndex
                        : ArrayContentsFailure::UnsupportedOperation);
            constant.setValueAttr(oldValue);
            inspect(ArrayContentsFailure::None);
            mlir::Block & last = function.getBody().back();
            auto store = llvm::cast<ctjs::SetPropertyOp>(&last.front());
            store->setOperand(2, child.getResult());
            mutation.contents.arrays = "a:[zero] | a:[x]";
            mutation.contents.exit = "a -> {a}; a -> {a,x}";
            mutation.discharged = "";
            inspect(ArrayContentsFailure::None);
            store->setOperand(2, number);
            mutation.contents.arrays = rows.front().contents.arrays;
            mutation.contents.exit = rows.front().contents.exit;
            mutation.discharged = "x";
            inspect(ArrayContentsFailure::None);
        } else {
            fail(
                row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
                "the live static binary fixture did not parse");
        }
        std::printf("static binary %s: %zu rows, %u live states, one wide snapshot, "
                    "%zu retention budget cutoffs\n",
                    spelling.c_str(), rows.size() + origins.size() + 2, liveStates, budgets);
    }
}

void checkArithmeticUnaryProducers(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %number = ctjs.constant #ctjs.number<33>\n"
        "  %big = ctjs.constant #ctjs.bigint<\"1\">\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct arithmetic_row {
        contents_row contents;
        const char * discharged = "x";
    };
    for (const auto kind : {ctjs::UnaryKind::Neg, ctjs::UnaryKind::Plus, ctjs::UnaryKind::BitNot}) {
        const std::string spelling = ctjs::stringifyUnaryKind(kind).str();
        const std::string operation = "  %produced = ctjs.unary " + spelling;
        const std::string produce = operation + " %number {storage_test_id = \"produced\"}\n";
        const std::vector<arithmetic_row> rows = {
            {.contents = {.what = "arithmetic Number results cannot prune either overwrite arm",
                          .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                          .arrays = "a:[zero] | a:[zero]",
                          .exit = "a -> {a}; a -> {a}"}},
            {.contents = {.what = "arithmetic returns a primitive without an operand alias",
                          .body = values + produce + "  ctjs.return %produced\n",
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "stored arithmetic Number results carry no object identity",
                          .body = values + produce +
                                  "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %a\n",
                          .arrays = "a:[produced]",
                          .exit = "a -> {a}"}},
            {.contents = {.what = "a primitive result forwards and roots independently",
                          .body = "  %frame = ctjs.frame_enter 8\n" + values + produce +
                                  "  cf.br ^next(%p, %produced : !ctjs.value, !ctjs.value)\n"
                                  "^next(%opaque: !ctjs.value, %result: !ctjs.value):\n"
                                  "  ctjs.root %result in %frame\n  ctjs.frame_exit %frame\n"
                                  "  ctjs.return %result\n",
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}},
            {.contents = {.what = "a saved child remains retained after a primitive overwrite",
                          .body = values + "  %saved = ctjs.get_property %a[%zero]\n" + produce +
                                  "  ctjs.set_property %a[%zero], %produced\n"
                                  "  ctjs.return %saved\n",
                          .arrays = "a:[produced]",
                          .reads = "a[0]=x",
                          .exit = "x -> {x}"},
             .discharged = ""},
            {.contents = {.what = "an opaque input cannot establish primitive numeric coercion",
                          .body = values + operation + " %p\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents =
                 {.what = "BigInt unary results and independent Plus errors retain no local edge",
                  .body = values + operation + " %big\n" + done,
                  .arrays = "a:[x]",
                  .exit = "zero -> {}"}},
            {.contents = {.what = "fresh objects can reenter source unary conversion",
                          .body = values + operation + " %x\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "fresh arrays do not supply primitive unary inputs",
                          .body = values + operation + " %a\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a local input alias still can reenter numeric conversion",
                          .body = values + "  %saved = ctjs.get_property %a[%zero]\n" + operation +
                                  " %saved\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a later retained arm prevents Stored refinement",
                          .body =
                              values + produce + branch + overwrite + "^no:\n  ctjs.return %a\n",
                          .arrays = "a:[zero] | a:[x]",
                          .exit = "a -> {a}; a -> {a,x}"},
             .discharged = ""},
            {.contents = {.what = "known arithmetic cannot hide an unsupported structural arm",
                          .body = values + produce + branch + done +
                                  "^no:\n  ctjs.store_global \"held\", %x\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "a Number origin is not a literal array index",
                          .body = values + produce + "  %read = ctjs.get_property %a[%produced]\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownIndex}},
            {.contents = {.what = "a Number origin is not a literal own String key",
                          .body = values + produce + "  ctjs.set_property %x[%produced], %zero\n" +
                                  done,
                          .failure = ArrayContentsFailure::UnknownPropertyKey}},
            {.contents = {.what = "an independent Number cannot authorize an opaque return",
                          .body = values + produce + "  ctjs.return %p\n",
                          .failure = ArrayContentsFailure::UnknownValue}},
            {.contents = {.what = "arithmetic cannot conceal an unsupported operand producer",
                          .body = values + "  %bad = \"test.value\"() : () -> !ctjs.value\n" +
                                  operation + " %bad\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
            {.contents = {.what = "an independent Number does not authorize later effects",
                          .body = values + produce +
                                  "  \"test.effect\"(%produced) : (!ctjs.value) -> ()\n" + done,
                          .failure = ArrayContentsFailure::UnsupportedOperation}},
        };
        std::size_t budgets = 0;
        const auto check = [&](mlir::ModuleOp module, const arithmetic_row & expected) {
            checkArrayContents(module, expected.contents);
            const bool complete = expected.contents.failure == ArrayContentsFailure::None;
            budgets +=
                checkArrayRetention(module, {.what = expected.contents.what,
                                             .body = expected.contents.body,
                                             .discharged = complete ? expected.discharged : "",
                                             .complete = complete});
        };
        const auto parse = [&](const arithmetic_row & expected) {
            return mlir::parseSourceString<mlir::ModuleOp>(
                std::string{kPrologue} + expected.contents.body + "}\n", &context);
        };
        const auto run = [&](const arithmetic_row & expected) {
            if (auto module = parse(expected)) {
                check(*module, expected);
            } else {
                fail(row{.what = expected.contents.what,
                         .body = expected.contents.body,
                         .expected = ""},
                     "the arithmetic unary fixture did not parse");
            }
        };
        for (const auto & expected : rows) { run(expected); }
        const std::vector<std::string> origins = {
            "  %operand = ctjs.constant #ctjs.undefined\n",
            "  %operand = ctjs.constant #ctjs.null\n",
            "  %operand = ctjs.constant #ctjs.boolean<true>\n",
            "  %operand = ctjs.constant #ctjs.string<\"4294967297.\">\n",
            "  %operand = ctjs.compare strict_eq %p, %zero\n",
            "  %operand = ctjs.convert to_boolean %p\n",
            "  %operand = ctjs.unary not %p\n",
            "  %operand = ctjs.unary typeof %p\n",
            "  %operand = ctjs.unary void %p\n",
            "  %operand = ctjs.binary_static add %zero, %zero\n",
            "  %operand = ctjs.unary " + spelling + " %number\n",
        };
        for (const std::string & input : origins) {
            run({.contents = {.what = "every admitted primitive producer excludes object coercion",
                              .body = values + input + operation +
                                      " %operand {storage_test_id = \"produced\"}\n"
                                      "  ctjs.return %produced\n",
                              .arrays = "a:[x]",
                              .exit = "produced -> {}"}});
        }
        // Path identity and saved SSA values, not a container's current tag,
        // establish which value a coercion actually receives.
        for (const std::string input : {"%zero", "%big", "%x", "%p"}) {
            const bool primitive = input == "%zero" || input == "%big";
            run({.contents = {.what = "every incoming unary operand must be a known primitive",
                              .body = values +
                                      "  %flag = ctjs.truthy %p\n"
                                      "  cf.cond_br %flag, ^join(%number : !ctjs.value), ^join(" +
                                      input + " : !ctjs.value)\n^join(%operand: !ctjs.value):\n" +
                                      operation +
                                      " %operand {storage_test_id = \"produced\"}\n"
                                      "  ctjs.return %produced\n",
                              .failure = primitive ? ArrayContentsFailure::None
                                                   : ArrayContentsFailure::UnsupportedOperation,
                              .arrays = "a:[x] | a:[x]",
                              .exit = "produced -> {}; produced -> {}"}});
        }
        for (const bool savedBigInt : {false, true}) {
            const std::string saved = savedBigInt ? "%big" : "%zero";
            const std::string replacement = savedBigInt ? "%zero" : "%big";
            run({.contents = {.what = "unary conversion follows saved values across tag overwrites",
                              .body = values + "  ctjs.set_property %a[%zero], " + saved +
                                      "\n  %operand = ctjs.get_property %a[%zero]\n"
                                      "  ctjs.set_property %a[%zero], " +
                                      replacement + "\n" + operation +
                                      " %operand {storage_test_id = \"produced\"}\n"
                                      "  ctjs.return %produced\n",
                              .arrays = savedBigInt ? "a:[zero]" : "a:[ctjs.constant]",
                              .reads = savedBigInt ? "a[0]=ctjs.constant" : "a[0]=zero",
                              .exit = "produced -> {}"}});
        }
        arithmetic_row wide = rows.front();
        std::string extras;
        for (unsigned i = 0; i < 32; ++i) {
            extras += "  %extra_" + std::to_string(i) + " = ctjs.unary " + spelling + " %number\n";
        }
        wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
        auto narrowModule = parse(rows.front());
        auto wideModule = parse(wide);
        if (narrowModule && wideModule) {
            const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
            const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
            if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
                fail(row{.what = "arithmetic snapshots charge every primitive origin",
                         .body = wide.contents.body,
                         .expected = ""},
                     "32 extra results did not cost one producer and one snapshot each");
            }
            check(*wideModule, wide);
        } else {
            fail(
                row{.what = "wide arithmetic snapshot", .body = wide.contents.body, .expected = ""},
                "the arithmetic snapshot fixture did not parse");
        }
        arithmetic_row mutation = rows.front();
        mutation.contents.what = "live unary tags defeat stale and forged completion";
        unsigned liveStates = 0;
        if (auto module = parse(mutation)) {
            ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
            ctjs::UnaryOp unary;
            ctjs::CreateObjectOp child;
            ctjs::CreateArrayOp array;
            ctjs::ConstantOp big;
            module->walk([&](ctjs::UnaryOp op) { unary = op; });
            module->walk([&](ctjs::CreateObjectOp op) { child = op; });
            module->walk([&](ctjs::CreateArrayOp op) { array = op; });
            module->walk([&](ctjs::ConstantOp op) {
                if (llvm::isa<ctjs::BigIntAttr>(op.getValue())) { big = op; }
            });
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
            const mlir::Value number = unary.getOperand();
            const mlir::Value invalid[] = {big.getResult(), child.getResult(), array.getResult(),
                                           function.getBody().front().getArgument(3)};
            for (mlir::Value bad : invalid) {
                unary->setOperand(0, bad);
                inspect(bad == big.getResult() ? ArrayContentsFailure::None
                                               : ArrayContentsFailure::UnsupportedOperation);
                unary->setOperand(0, number);
                inspect(ArrayContentsFailure::None);
            }
            auto constant = number.getDefiningOp<ctjs::ConstantOp>();
            const mlir::Attribute oldValue = constant.getValue();
            constant.setValueAttr(big.getValue());
            inspect(ArrayContentsFailure::None);
            constant.setValueAttr(oldValue);
            inspect(ArrayContentsFailure::None);
            for (const auto admitted :
                 {ctjs::UnaryKind::Not, ctjs::UnaryKind::TypeOf, ctjs::UnaryKind::Void,
                  ctjs::UnaryKind::Neg, ctjs::UnaryKind::Plus, ctjs::UnaryKind::BitNot}) {
                unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, admitted));
                inspect(ArrayContentsFailure::None);
            }
            unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, kind));
            mlir::Block & last = function.getBody().back();
            auto store = llvm::cast<ctjs::SetPropertyOp>(&last.front());
            const mlir::Value replacement = store.getValue();
            store->setOperand(2, child.getResult());
            mutation.contents.arrays = "a:[zero] | a:[x]";
            mutation.contents.exit = "a -> {a}; a -> {a,x}";
            mutation.discharged = "";
            inspect(ArrayContentsFailure::None);
            store->setOperand(2, replacement);
            mutation.contents.arrays = rows.front().contents.arrays;
            mutation.contents.exit = rows.front().contents.exit;
            mutation.discharged = "x";
            inspect(ArrayContentsFailure::None);
        } else {
            fail(
                row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
                "the live arithmetic unary fixture did not parse");
        }
        std::printf("arithmetic unary %s: %zu rows, %u live states, one wide snapshot, "
                    "%zu retention budget cutoffs\n",
                    spelling.c_str(), rows.size() + origins.size() + 6, liveStates, budgets);
    }
}

} // namespace ctcompile::test::escape::arrays
