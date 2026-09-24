#pragma once

#include "Harness.h"

#include <type_traits>

namespace ctcompile::test::escape::arrays {

// Both families require independent original primitive operands, and share
// the same contents/retention obligations. Operator-specific mutation controls
// keep their distinct whitelists independent.
template <typename ProducerOp, typename Kind>
void checkPrimitiveBinaryProducer(mlir::MLIRContext & context, Kind producerKind) {
    constexpr bool isComparison = std::is_same_v<ProducerOp, ctjs::CompareOp>;
    using KindAttr = std::conditional_t<isComparison, ctjs::CompareKindAttr, ctjs::BinaryKindAttr>;
    const std::string spelling = [&] {
        if constexpr (isComparison) {
            return ctjs::stringifyCompareKind(producerKind).str();
        } else {
            return ctjs::stringifyBinaryKind(producerKind).str();
        }
    }();
    const bool isConcat = [&] {
        if constexpr (isComparison) {
            return false;
        } else {
            return producerKind == ctjs::BinaryKind::Concat;
        }
    }();
    const bool isMixedError = [&] {
        if constexpr (isComparison) {
            return false;
        } else {
            return producerKind == ctjs::BinaryKind::Add || producerKind == ctjs::BinaryKind::Sub ||
                   producerKind == ctjs::BinaryKind::Mul || producerKind == ctjs::BinaryKind::Div ||
                   producerKind == ctjs::BinaryKind::Mod || producerKind == ctjs::BinaryKind::Pow;
        }
    }();
    const std::string mnemonic = isComparison ? "ctjs.compare" : "ctjs.binary";
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %number = ctjs.constant #ctjs.number<17>\n"
        "  %big = ctjs.constant #ctjs.bigint<\"1\">\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const auto compare = [&](const std::string & lhs, const std::string & rhs) {
        return "  %produced = " + mnemonic + " " + spelling + " " + lhs + ", " + rhs +
               " {storage_test_id = \"produced\"}\n";
    };
    const std::string produce = compare("%number", "%zero");
    const std::string done = "  ctjs.return %zero\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n";
    const std::string branch =
        "  %flag = ctjs.truthy %produced\n  cf.cond_br %flag, ^yes, ^no\n^yes:\n";
    struct comparison_row {
        contents_row contents;
        const char * discharged = "x";
    };
    const std::vector<comparison_row> rows = {
        {.contents = {.what = "primitive comparison cannot prune either structural overwrite arm",
                      .body = values + produce + branch + overwrite + "^no:\n" + overwrite,
                      .arrays = "a:[zero] | a:[zero]",
                      .exit = "a -> {a}; a -> {a}"}},
        {.contents = {.what = "primitive binary result returns independently of both operands",
                      .body = values + produce + "  ctjs.return %produced\n",
                      .arrays = "a:[x]",
                      .exit = "produced -> {}"}},
        {.contents = {.what = "storing a comparison result retains no former child identity",
                      .body = values + produce +
                              "  ctjs.set_property %a[%zero], %produced\n  ctjs.return %a\n",
                      .arrays = "a:[produced]",
                      .exit = "a -> {a}"}},
        {.contents = {.what = "a comparison result forwards and roots independently",
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
        {.contents = {.what = "a later retained arm prevents comparison-based Stored refinement",
                      .body = values + produce + branch + overwrite + "^no:\n  ctjs.return %a\n",
                      .arrays = "a:[zero] | a:[x]",
                      .exit = "a -> {a}; a -> {a,x}"},
         .discharged = ""},
        {.contents = {.what = "literal comparison cannot conceal an unsupported structural arm",
                      .body = values + compare("%zero", "%zero") + branch + done +
                              "^no:\n  ctjs.store_global \"held\", %x\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a primitive binary result is not a literal array index",
                      .body =
                          values + produce + "  %read = ctjs.get_property %a[%produced]\n" + done,
                      .failure = ArrayContentsFailure::UnknownIndex}},
        {.contents = {.what = "a primitive binary result is not a literal own String key",
                      .body =
                          values + produce + "  ctjs.set_property %x[%produced], %zero\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a primitive comparison does not authorize an opaque return",
                      .body = values + produce + "  ctjs.return %p\n",
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "comparison cannot conceal an unsupported operand producer",
                      .body = values + "  %bad = \"test.value\"() : () -> !ctjs.value\n" +
                              compare("%bad", "%zero") + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a primitive comparison does not authorize later effects",
                      .body = values + produce +
                              "  \"test.effect\"(%produced) : (!ctjs.value) -> ()\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
    };
    std::size_t budgets = 0;
    unsigned rowCount = 0;
    const auto check = [&](mlir::ModuleOp module, const comparison_row & expected) {
        checkArrayContents(module, expected.contents);
        const bool complete = expected.contents.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(module, {.what = expected.contents.what,
                                                .body = expected.contents.body,
                                                .discharged = complete ? expected.discharged : "",
                                                .complete = complete});
    };
    const auto parse = [&](const comparison_row & expected) {
        return mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
    };
    const auto run = [&](const comparison_row & expected) {
        if (auto module = parse(expected)) {
            check(*module, expected);
        } else {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the primitive comparison fixture did not parse");
        }
        ++rowCount;
    };
    for (const auto & expected : rows) { run(expected); }
    const std::vector<std::string> origins = {
        "  %operand = ctjs.constant #ctjs.undefined\n",
        "  %operand = ctjs.constant #ctjs.null\n",
        "  %operand = ctjs.constant #ctjs.boolean<true>\n",
        "  %operand = ctjs.constant #ctjs.number<17>\n",
        "  %operand = ctjs.constant #ctjs.string<\"17.\">\n",
        "  %operand = ctjs.compare strict_eq %p, %zero\n",
        "  %operand = ctjs.compare eq %number, %zero\n",
        "  %operand = ctjs.convert to_boolean %p\n",
        "  %operand = ctjs.unary not %p\n",
        "  %operand = ctjs.unary typeof %p\n",
        "  %operand = ctjs.unary void %p\n",
        "  %operand = ctjs.binary_static add %zero, %zero\n",
        "  %operand = ctjs.unary bitnot %number\n",
    };
    for (const bool left : {false, true}) {
        const auto compareInput = [&](const std::string & input) {
            return left ? compare(input, "%number") : compare("%number", input);
        };
        for (const std::string & input : origins) {
            run({.contents = {.what = "each comparison side needs its own primitive proof",
                              .body = values + input + compareInput("%operand") +
                                      "  ctjs.return %produced\n",
                              .arrays = "a:[x]",
                              .exit = "produced -> {}"}});
        }
        for (const std::string input : {"%big", "%x", "%a", "%p"}) {
            run({.contents = {.what = "neither comparison side borrows the other primitive proof",
                              .body = values + compareInput(input) + done,
                              .failure =
                                  (isComparison || isConcat || isMixedError) && input == "%big"
                                      ? ArrayContentsFailure::None
                                      : ArrayContentsFailure::UnsupportedOperation,
                              .arrays = "a:[x]",
                              .exit = "zero -> {}"}});
        }
        for (const std::string input : {"%zero", "%big", "%x", "%a", "%p"}) {
            run({.contents = {.what = "every structural comparison input needs a primitive origin",
                              .body = values +
                                      "  %flag = ctjs.truthy %p\n"
                                      "  cf.cond_br %flag, ^join(%number : !ctjs.value), ^join(" +
                                      input + " : !ctjs.value)\n^join(%operand: !ctjs.value):\n" +
                                      compareInput("%operand") + "  ctjs.return %produced\n",
                              .failure =
                                  input == "%zero" || ((isComparison || isConcat || isMixedError) &&
                                                       input == "%big")
                                      ? ArrayContentsFailure::None
                                      : ArrayContentsFailure::UnsupportedOperation,
                              .arrays = "a:[x] | a:[x]",
                              .exit = "produced -> {}; produced -> {}"}});
        }
        // An overwritten slot's new tag never cleans the saved old origin;
        // conversely, a later BigInt/object cannot taint an earlier primitive.
        for (const std::string saved : {"%zero", "%big", "%x"}) {
            const std::string replacement = saved == "%zero" ? "%big" : "%zero";
            run({.contents = {.what = "comparison follows saved array values across tag overwrites",
                              .body = values + "  ctjs.set_property %a[%zero], " + saved +
                                      "\n  %operand = ctjs.get_property %a[%zero]\n"
                                      "  ctjs.set_property %a[%zero], " +
                                      replacement + "\n" + compareInput("%operand") +
                                      "  ctjs.return %produced\n",
                              .failure =
                                  saved == "%zero" || ((isComparison || isConcat || isMixedError) &&
                                                       saved == "%big")
                                      ? ArrayContentsFailure::None
                                      : ArrayContentsFailure::UnsupportedOperation,
                              .arrays = saved == "%zero" ? "a:[ctjs.constant]" : "a:[zero]",
                              .reads = saved == "%zero" ? "a[0]=zero" : "a[0]=ctjs.constant",
                              .exit = "produced -> {}"}});
        }
        for (const bool savedBigInt : {false, true}) {
            const std::string saved = savedBigInt ? "%big" : "%number";
            const std::string replacement = savedBigInt ? "%number" : "%big";
            run({.contents = {
                     .what = "comparison preserves saved own fields after overwrite and deletion",
                     .body = values + "  %key = ctjs.constant #ctjs.string<\"operand\">\n" +
                             "  ctjs.set_property %x[%key], " + saved +
                             "\n  %operand = ctjs.get_property %x[%key]\n"
                             "  ctjs.set_property %x[%key], " +
                             replacement + "\n  ctjs.delete_named \"operand\" from %x\n" +
                             compareInput("%operand") + "  ctjs.return %produced\n",
                     .failure = ArrayContentsFailure::UnsupportedOperation,
                     .arrays = "a:[x]",
                     .exit = "produced -> {}"}});
        }
    }
    run({.contents = {.what = "primitive comparison cannot authorize publication before it",
                      .body = values + "  ctjs.store_global \"held\", %a\n" + produce + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}});
    run({.contents = {.what = "primitive comparison cannot authorize publication after it",
                      .body = values + produce + "  ctjs.store_global \"held\", %a\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}});
    run({.contents = {.what = "primitive comparison cannot authorize a retaining call",
                      .body = values + produce + "  %call = ctjs.call %p(%x)\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}});
    run({.contents = {.what = "primitive comparison cannot authorize explicit throwing",
                      .body = values + produce + "  ctjs.throw %x\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation}});
    run({.contents = {.what = "primitive comparison cannot authorize a local exception handler",
                      .body = values + "  ctjs.push_handler ^body catch ^pad\n^body:\n" + produce +
                              "  ctjs.pop_handler\n" + overwrite +
                              "^pad:\n  %id, %exception = ctjs.catch_land\n"
                              "  ctjs.return %a\n",
                      .failure = ArrayContentsFailure::UnsupportedControlFlow}});
    // Every admitted relational result is an independent primitive origin,
    // including when consumed by a different comparison kind. This contents
    // query supplies no completion or effect contract for the guarded paths.
    for (const std::string kind : {"lt", "le", "gt", "ge"}) {
        run({.contents = {
                 .what = "relational result origins feed independently checked comparisons",
                 .body = values + "  %operand = ctjs.compare " + kind + " %number, %zero\n" +
                         compare("%operand", "%zero") + "  ctjs.return %produced\n",
                 .arrays = "a:[x]",
                 .exit = "produced -> {}"}});
    }
    for (const std::string kind : {"sub", "mul", "div", "mod", "pow", "add", "concat"}) {
        run({.contents = {
                 .what = "arithmetic result origins feed independently checked binary producers",
                 .body = values + "  %operand = ctjs.binary " + kind + " %number, %zero\n" +
                         compare("%operand", "%zero") + "  ctjs.return %produced\n",
                 .arrays = "a:[x]",
                 .exit = "produced -> {}"}});
    }
    // Add can yield either Number or String; its result is primitive without
    // a concrete tag fact. Every consumer must work with both original cases.
    for (const bool stringLeft : {false, true}) {
        const std::string operands = stringLeft ? "%text, %zero" : "%zero, %text";
        run({.contents = {.what =
                              "String-producing addition remains an independent primitive origin",
                          .body = values + "  %text = ctjs.constant #ctjs.string<\"17.\">\n" +
                                  "  %operand = ctjs.binary add " + operands + "\n" +
                                  compare("%operand", "%zero") + "  ctjs.return %produced\n",
                          .arrays = "a:[x]",
                          .exit = "produced -> {}"}});
    }
    comparison_row wide = rows.front();
    std::string extras;
    for (unsigned i = 0; i < 32; ++i) {
        extras += "  %extra_" + std::to_string(i) + " = " + mnemonic + " " + spelling +
                  " %number, %zero\n";
    }
    wide.contents.body.insert(wide.contents.body.find("  %flag ="), extras);
    auto narrowModule = parse(rows.front());
    auto wideModule = parse(wide);
    if (narrowModule && wideModule) {
        const auto narrow =
            computeArrayContents(*narrowModule->template getOps<ctjs::FuncOp>().begin());
        const auto expanded =
            computeArrayContents(*wideModule->template getOps<ctjs::FuncOp>().begin());
        const unsigned extraWork = spelling == "sub" || spelling == "add" ? 96U : 64U;
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + extraWork) {
            fail(row{.what = "comparison snapshots charge every independent primitive origin",
                     .body = wide.contents.body,
                     .expected = ""},
                 "32 extra results must charge producers, snapshots and independent arithmetic "
                 "bits");
        }
        check(*wideModule, wide);
    } else {
        fail(row{.what = "wide comparison snapshot", .body = wide.contents.body, .expected = ""},
             "the comparison snapshot fixture did not parse");
    }
    comparison_row mutation = rows.front();
    mutation.contents.what = "live comparison inputs and kinds defeat forged completion";
    unsigned liveStates = 0;
    if (auto module = parse(mutation)) {
        ctjs::FuncOp function = *module->template getOps<ctjs::FuncOp>().begin();
        ProducerOp comparison;
        ctjs::CreateObjectOp child;
        ctjs::CreateArrayOp array;
        ctjs::ConstantOp big;
        module->walk([&](ProducerOp op) { comparison = op; });
        module->walk([&](ctjs::CreateObjectOp op) { child = op; });
        module->walk([&](ctjs::CreateArrayOp op) { array = op; });
        module->walk([&](ctjs::ConstantOp op) {
            if (llvm::isa<ctjs::BigIntAttr>(op.getValue())) { big = op; }
        });
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
        const mlir::Value original[] = {comparison.getLhs(), comparison.getRhs()};
        const mlir::Value opaque = function.getBody().front().getArgument(3);
        const mlir::Value invalid[] = {big.getResult(), child.getResult(), array.getResult(),
                                       opaque};
        for (unsigned position = 0; position < 2; ++position) {
            for (mlir::Value bad : invalid) {
                comparison->setOperand(position, bad);
                inspect((isComparison || isConcat || isMixedError) && bad == big.getResult()
                            ? ArrayContentsFailure::None
                            : ArrayContentsFailure::UnsupportedOperation);
                comparison->setOperand(position, original[position]);
                inspect(ArrayContentsFailure::None);
            }
            auto constant = original[position].template getDefiningOp<ctjs::ConstantOp>();
            const mlir::Attribute oldValue = constant.getValue();
            constant.setValueAttr(big.getValue());
            inspect(isComparison || isConcat || isMixedError
                        ? (position == 1 ? ArrayContentsFailure::MissingElement
                                         : ArrayContentsFailure::None)
                        : ArrayContentsFailure::UnsupportedOperation);
            constant.setValueAttr(oldValue);
            inspect(ArrayContentsFailure::None);
        }
        if constexpr (isComparison) {
            for (const auto kind : {ctjs::CompareKind::Lt, ctjs::CompareKind::Le,
                                    ctjs::CompareKind::Gt, ctjs::CompareKind::Ge}) {
                comparison.setKindAttr(KindAttr::get(&context, kind));
                inspect(ArrayContentsFailure::None);
                comparison.setKindAttr(KindAttr::get(&context, producerKind));
                inspect(ArrayContentsFailure::None);
            }
            comparison->setOperand(0, opaque);
            comparison.setKindAttr(KindAttr::get(&context, ctjs::CompareKind::StrictEq));
            inspect(ArrayContentsFailure::None);
            comparison.setKindAttr(KindAttr::get(&context, producerKind));
            inspect(ArrayContentsFailure::UnsupportedOperation);
            comparison->setOperand(0, original[0]);
            inspect(ArrayContentsFailure::None);
        } else {
            for (const auto kind :
                 {ctjs::BinaryKind::Sub, ctjs::BinaryKind::Mul, ctjs::BinaryKind::Div,
                  ctjs::BinaryKind::Mod, ctjs::BinaryKind::Pow, ctjs::BinaryKind::Add,
                  ctjs::BinaryKind::Concat, ctjs::BinaryKind::BitAnd, ctjs::BinaryKind::BitOr,
                  ctjs::BinaryKind::BitXor, ctjs::BinaryKind::Shl, ctjs::BinaryKind::Shr,
                  ctjs::BinaryKind::UShr}) {
                comparison.setKindAttr(KindAttr::get(&context, kind));
                const bool supported =
                    kind == ctjs::BinaryKind::Sub || kind == ctjs::BinaryKind::Mul ||
                    kind == ctjs::BinaryKind::Div || kind == ctjs::BinaryKind::Mod ||
                    kind == ctjs::BinaryKind::Pow || kind == ctjs::BinaryKind::Add ||
                    kind == ctjs::BinaryKind::Concat;
                inspect(supported ? ArrayContentsFailure::None
                                  : ArrayContentsFailure::UnsupportedOperation);
                comparison.setKindAttr(KindAttr::get(&context, producerKind));
                inspect(ArrayContentsFailure::None);
            }
        }
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
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live primitive comparison fixture did not parse");
    }
    std::printf("primitive %s %s: %u rows, %u live states, one wide snapshot, "
                "%zu retention budget cutoffs\n",
                isComparison ? "comparison" : "arithmetic binary", spelling.c_str(), rowCount,
                liveStates, budgets);
}

} // namespace ctcompile::test::escape::arrays
