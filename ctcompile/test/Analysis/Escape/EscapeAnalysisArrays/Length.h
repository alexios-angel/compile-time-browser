#pragma once

#include "Harness.h"

namespace ctcompile::test::escape::arrays {

inline void checkDenseArrayLength(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %key = ctjs.constant #ctjs.string<\"length\"> {storage_test_id = \"key\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string read =
        "  %length = ctjs.get_property %a[%key] {storage_test_id = \"length\"}\n";
    const std::string overwrite = "  ctjs.set_property %a[%zero], %zero\n";
    const std::string done = "  ctjs.return %length\n";
    unsigned rows = 0;
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const contents_row & expected,
                           const char * discharged) {
        checkArrayContents(module, expected);
        const bool complete = expected.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(module, {.what = expected.what,
                                                .body = expected.body,
                                                .discharged = complete ? discharged : "",
                                                .complete = complete});
    };
    const auto parse = [&](const contents_row & expected) {
        return mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.body + "}\n", &context);
    };
    const auto run = [&](const contents_row & expected, const char * discharged = "x") {
        if (auto module = parse(expected)) {
            check(*module, expected, discharged);
        } else {
            fail(row{.what = expected.what, .body = expected.body, .expected = ""},
                 "the dense length fixture did not parse");
        }
        ++rows;
    };
    run({.what = "an original dense length has no element or array identity",
         .body = values + read + done,
         .arrays = "a:[x]",
         .exit = "length -> {}"});
    run({.what = "saved length survives later append and overwrite",
         .body = values + read + "  ctjs.append %x to %a\n" + overwrite +
                 "  %later = ctjs.get_property %a[%key] {storage_test_id = \"later\"}\n"
                 "  %result = ctjs.create_array [%length, %later] {storage_test_id = \"result\"}\n"
                 "  ctjs.return %result\n",
         .arrays = "a:[zero,x]; result:[length,later]",
         .exit = "result -> {result}"});
    run({.what = "returning the saved child after length still retains that original child",
         .body = values + read + "  %saved = ctjs.get_property %a[%zero]\n" + overwrite +
                 "  ctjs.return %saved\n",
         .arrays = "a:[zero]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        "");
    run({.what = "length reads of an empty literal also have an independent origin",
         .body = values +
                 "  %empty = ctjs.create_array [] {storage_test_id = \"empty\"}\n"
                 "  %length = ctjs.get_property %empty[%key] "
                 "{storage_test_id = \"length\"}\n" +
                 done,
         .arrays = "a:[x]; empty:[]",
         .exit = "length -> {}"});
    run({.what = "array-loaded original length keys survive replacement of their source slot",
         .body = values +
                 "  %keys = ctjs.create_array [%key] {storage_test_id = \"keys\"}\n"
                 "  %loaded = ctjs.get_property %keys[%zero]\n"
                 "  ctjs.set_property %keys[%zero], %x\n"
                 "  %length = ctjs.get_property %a[%loaded] "
                 "{storage_test_id = \"length\"}\n" +
                 done,
         .arrays = "a:[x]; keys:[x]",
         .reads = "keys[0]=key",
         .exit = "length -> {}"});
    run({.what = "a length saved in an array keeps its admitted primitive origin",
         .body = values + read +
                 "  %saved = ctjs.create_array [%length] {storage_test_id = \"saved\"}\n"
                 "  %loaded = ctjs.get_property %saved[%zero]\n"
                 "  ctjs.set_property %saved[%zero], %x\n"
                 "  %result = ctjs.unary neg %loaded {storage_test_id = \"result\"}\n"
                 "  ctjs.return %result\n",
         .arrays = "a:[x]; saved:[x]",
         .reads = "saved[0]=length",
         .exit = "result -> {}"});
    run({.what = "loaded aliases read their original array length without erasing another array",
         .body = values + "  %b = ctjs.create_array [%x] {storage_test_id = \"b\"}\n"
                          "  %aliases = ctjs.create_array [%a] {storage_test_id = \"aliases\"}\n"
                          "  %alias = ctjs.get_property %aliases[%zero]\n"
                          "  %length = ctjs.get_property %alias[%key]\n"
                          "  ctjs.set_property %alias[%zero], %zero\n"
                          "  ctjs.return %b\n",
         .arrays = "a:[zero]; b:[x]; aliases:[a]",
         .reads = "aliases[0]=a",
         .exit = "b -> {b,x}"},
        "a");
    for (const std::string producer : {"ctjs.unary neg %length", "ctjs.binary add %length, %zero",
                                       "ctjs.compare lt %length, %zero"}) {
        run({.what = "length supplies an independently admitted primitive operand",
             .body = values + read + "  %result = " + producer +
                     " {storage_test_id = \"result\"}\n  ctjs.return %result\n",
             .arrays = "a:[x]",
             .exit = "result -> {}"});
    }
    const contents_row branch{
        .what = "length and original key transport preserve every structural frame exit",
        .body = "  %frame = ctjs.frame_enter 8\n" + values + read +
                "  %flag = ctjs.truthy %zero\n"
                "  cf.cond_br %flag, ^left(%length : !ctjs.value), ^right(%key : !ctjs.value)\n"
                "^left(%saved: !ctjs.value):\n"
                "  ctjs.root %saved in %frame\n  ctjs.frame_exit %frame\n"
                "  ctjs.return %saved\n"
                "^right(%forwarded: !ctjs.value):\n"
                "  %later = ctjs.get_property %a[%forwarded] {storage_test_id = \"later\"}\n"
                "  ctjs.root %later in %frame\n  ctjs.frame_exit %frame\n"
                "  ctjs.return %later\n",
        .arrays = "a:[x] | a:[x]",
        .exit = "length -> {}; later -> {}"};
    run(branch);
    for (const std::string effect :
         {"  %called = ctjs.call %p(%a)\n", "  ctjs.store_global \"held\", %a\n",
          "  ctjs.set_proto %p on %a\n", "  ctjs.define_accessor \"length\" on %a get %p set %q\n",
          "  ctjs.delete_named \"0\" from %a\n",
          "  %one = ctjs.constant #ctjs.number<4607182418800017408>\n"
          "  ctjs.set_property %a[%one], %zero\n"}) {
        contents_row unsupported = branch;
        unsupported.what = "even a literal-false length arm must prove its later effects";
        unsupported.body.insert(unsupported.body.find("  ctjs.root %saved"), effect);
        unsupported.failure = effect.find("ctjs.set_property") != std::string::npos
                                  ? ArrayContentsFailure::MissingElement
                                  : ArrayContentsFailure::UnsupportedOperation;
        run(unsupported);
    }
    for (const auto & [operation, failure] :
         {std::pair{"  %bad = ctjs.get_property %p[%key]\n", ArrayContentsFailure::UnknownArray},
          std::pair{"  %bad = ctjs.get_property %x[%key]\n", ArrayContentsFailure::MissingProperty},
          std::pair{"  %bad = ctjs.get_property %a[%p]\n", ArrayContentsFailure::UnknownIndex},
          std::pair{"  %bad = ctjs.get_property %a[%x]\n", ArrayContentsFailure::UnknownIndex},
          std::pair{"  %bad = ctjs.get_property %a[%length]\n",
                    ArrayContentsFailure::MissingElement},
          std::pair{"  ctjs.set_property %a[%key], %zero\n", ArrayContentsFailure::UnknownIndex},
          std::pair{"  ctjs.set_property %x[%key], %zero\n",
                    ArrayContentsFailure::UnsupportedOperation},
          std::pair{"  %child = ctjs.get_property %a[%zero]\n"
                    "  %bad = ctjs.unary neg %child\n",
                    ArrayContentsFailure::UnsupportedOperation},
          std::pair{"  %computed = ctjs.binary concat %key, %key\n"
                    "  %bad = ctjs.get_property %a[%computed]\n",
                    ArrayContentsFailure::UnknownIndex}}) {
        run({.what = "length does not authorize arbitrary property reads, keys or writes",
             .body = values + read + operation + done,
             .failure = failure});
    }
    for (const std::string spelling : {"Length", "length ", "__proto__"}) {
        run({.what = "only the original exact length spelling is admitted",
             .body = values + "  %other = ctjs.constant #ctjs.string<\"" + spelling +
                     "\">\n  %length = ctjs.get_property %a[%other]\n" + done,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    const std::string one = "  %one = ctjs.constant #ctjs.number<4607182418800017408>\n";
    const std::string subtract =
        "  %index = ctjs.binary sub %length, %one {storage_test_id = \"index\"}\n";
    const std::string indexed = "  ctjs.set_property %a[%index], %zero\n  ctjs.return %a\n";
    const contents_row originalIndex{
        .what = "the original denseLengthIndexed subtraction selects its exact overwritten slot",
        .body = values + one + read + subtract + indexed,
        .arrays = "a:[zero]",
        .exit = "a -> {a}"};
    run(originalIndex);
    run({.what = "a second bounded subtraction uses the original exact Number result",
         .body = values + one + "  ctjs.append %zero to %a\n" + read + subtract +
                 "  %first = ctjs.binary sub %index, %one\n"
                 "  ctjs.set_property %a[%first], %zero\n  ctjs.return %a\n",
         .arrays = "a:[zero,zero]",
         .exit = "a -> {a}"});
    run({.what = "a length-derived element read still retains its saved original child",
         .body = values + one + read + subtract + "  %saved = ctjs.get_property %a[%index]\n" +
                 overwrite + "  ctjs.return %saved\n",
         .arrays = "a:[zero]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        "");
    run({.what = "a saved length uses its original size after a later append",
         .body = values + one + read + "  ctjs.append %x to %a\n" + subtract + indexed,
         .arrays = "a:[zero,x]",
         .exit = "a -> {a,x}"},
        "");
    run({.what = "a new length read observes the appended element",
         .body = values + one + "  ctjs.append %x to %a\n" + read + subtract + indexed,
         .arrays = "a:[x,zero]",
         .exit = "a -> {a,x}"},
        "");
    run({.what = "a saved length itself is an exact index after append",
         .body = values + read +
                 "  ctjs.append %zero to %a\n"
                 "  %saved = ctjs.get_property %a[%length]\n"
                 "  ctjs.return %saved\n",
         .arrays = "a:[x,zero]",
         .reads = "a[1]=zero",
         .exit = "zero -> {}"});
    run({.what = "a saved subtracted index keeps its Number origin after slot replacement",
         .body = values + one + read + subtract +
                 "  %keys = ctjs.create_array [%index] {storage_test_id = \"keys\"}\n"
                 "  %saved = ctjs.get_property %keys[%zero]\n"
                 "  ctjs.set_property %keys[%zero], %x\n"
                 "  ctjs.set_property %a[%saved], %zero\n  ctjs.return %a\n",
         .arrays = "a:[zero]; keys:[x]",
         .reads = "keys[0]=index",
         .exit = "a -> {a}"});
    run({.what = "forwarded length and literal offset keep both original Number identities",
         .body = "  %frame = ctjs.frame_enter 8\n" + values + one + read +
                 "  cf.br ^next(%length, %one : !ctjs.value, !ctjs.value)\n"
                 "^next(%before: !ctjs.value, %offset: !ctjs.value):\n"
                 "  %index = ctjs.binary sub %before, %offset\n"
                 "  ctjs.root %index in %frame\n"
                 "  ctjs.set_property %a[%index], %zero\n"
                 "  ctjs.frame_exit %frame\n  ctjs.return %a\n",
         .arrays = "a:[zero]",
         .exit = "a -> {a}"});
    const contents_row indexedBranch{
        .what = "the same length operation has separate exact values on each structural path",
        .body = values + one +
                "  %flag = ctjs.truthy %zero\n  cf.cond_br %flag, ^left, ^right\n"
                "^left:\n  cf.br ^join\n"
                "^right:\n  ctjs.append %x to %a\n  cf.br ^join\n^join:\n" +
                read + subtract + indexed,
        .arrays = "a:[zero] | a:[x,zero]",
        .exit = "a -> {a}; a -> {a,x}"};
    run(indexedBranch, "");
    for (const std::string literal : {"#ctjs.number<4602678819172646912>",  // 0.5
                                      "#ctjs.number<13830554455654793216>", // -1
                                      "#ctjs.number<4611686018427387904>",  // 2, underflow
                                      "#ctjs.number<4751297606875873280>",  // 2^32
                                      "#ctjs.number<4845873199050653696>",  // 2^53
                                      "#ctjs.number<9218868437227405312>",  // infinity
                                      "#ctjs.number<18442240474082181120>", // -infinity
                                      "#ctjs.number<9221120237041090560>",  // NaN
                                      "#ctjs.string<\"1\">", "#ctjs.bigint<\"1\">",
                                      "#ctjs.boolean<true>", "#ctjs.null", "#ctjs.undefined"}) {
        run({.what = "subtraction requires a bounded original integral Number offset",
             .body =
                 values + "  %one = ctjs.constant " + literal + "\n" + read + subtract + indexed,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    run({.what = "negative zero is an exact zero offset without changing the saved length",
         .body = values + "  %one = ctjs.constant #ctjs.number<9223372036854775808>\n" + read +
                 "  ctjs.append %zero to %a\n" + subtract + indexed,
         .arrays = "a:[x,zero]",
         .exit = "a -> {a,x}"},
        "");
    for (const std::string producer :
         {"ctjs.binary sub %one, %one", "ctjs.binary sub %zero, %one",
          "ctjs.binary sub %one, %length", "ctjs.binary add %length, %one",
          "ctjs.binary mul %length, %zero", "ctjs.binary_static add %length, %zero"}) {
        run({.what = "other arithmetic does not borrow length-subtraction index authority",
             .body = values + one + read + "  %index = " + producer + "\n" + indexed,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    run({.what = "an independently primitive computed offset supplies no exact Number value",
         .body = values + one + read +
                 "  %offset = ctjs.binary sub %one, %zero\n"
                 "  %index = ctjs.binary sub %length, %offset\n" +
                 indexed,
         .failure = ArrayContentsFailure::UnknownIndex});
    run({.what = "an opaque subtraction operand cannot gain authority from a known length",
         .body = values + read + "  %index = ctjs.binary sub %length, %p\n" + indexed,
         .failure = ArrayContentsFailure::UnsupportedOperation});
    run({.what = "a zero length cannot underflow into an array index",
         .body = values + one +
                 "  %empty = ctjs.create_array []\n"
                 "  %length = ctjs.get_property %empty[%key]\n" +
                 subtract + indexed,
         .failure = ArrayContentsFailure::UnknownIndex});
    for (const std::string alternative : {"%one", "%zero", "%key"}) {
        run({.what = "one length-valued edge cannot authorize another edge's category or value",
             .body = values + one + read +
                     "  %flag = ctjs.truthy %zero\n"
                     "  cf.cond_br %flag, ^join(%length : !ctjs.value), ^join(" +
                     alternative +
                     " : !ctjs.value)\n^join(%before: !ctjs.value):\n"
                     "  %index = ctjs.binary sub %before, %one\n" +
                     indexed,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    contents_row wide = branch;
    std::string extra;
    for (unsigned i = 0; i < 32; ++i) {
        extra += "  %extra_" + std::to_string(i) + " = ctjs.get_property %a[%key]\n";
    }
    wide.body.insert(wide.body.find("  %flag ="), extra);
    auto narrowModule = parse(branch);
    auto wideModule = parse(wide);
    if (narrowModule && wideModule) {
        const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
        const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 128) {
            fail(row{.what = "length snapshots charge every independent result",
                     .body = wide.body,
                     .expected = ""},
                 "32 extra length results did not charge both origin and Number snapshots");
        }
        check(*wideModule, wide, "x");
    } else {
        fail(row{.what = wide.what, .body = wide.body, .expected = ""},
             "the wide length snapshot did not parse");
    }
    contents_row mutation{.what = "live length origins defeat stale and forged completion",
                          .body = values + read +
                                  "  %result = ctjs.unary neg %length "
                                  "{storage_test_id = \"result\"}\n" +
                                  overwrite + "  ctjs.return %result\n",
                          .arrays = "a:[zero]",
                          .exit = "result -> {}"};
    unsigned liveStates = 0;
    if (auto module = parse(mutation)) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::GetPropertyOp load;
        ctjs::UnaryOp unary;
        module->walk([&](ctjs::GetPropertyOp op) { load = op; });
        module->walk([&](ctjs::UnaryOp op) { unary = op; });
        const mlir::Value base = load.getObject();
        const mlir::Value key = load.getKey();
        auto literal = key.getDefiningOp<ctjs::ConstantOp>();
        const mlir::Value parameter = function.getBody().front().getArgument(3);
        mlir::OpBuilder builder(load);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*module))) {
            fail(row{.what = mutation.what, .body = mutation.body, .expected = ""},
                 "the length stale solver did not converge");
        }
        const auto inspect = [&](ArrayContentsFailure failure) {
            mutation.failure = failure;
            check(*module, mutation, "x");
            const bool complete = failure == ArrayContentsFailure::None;
            const auto current = computeVerdicts(stale, function);
            if (current.arrayRetentionComplete != complete ||
                current.confinedStoredSites != (complete ? 1U : 0U)) {
                fail(row{.what = mutation.what, .body = mutation.body, .expected = ""},
                     "stale solver or forged marker supplied length authority");
            }
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        load->setOperand(0, parameter);
        inspect(ArrayContentsFailure::UnknownArray);
        load->setOperand(0, base);
        load->setOperand(1, parameter);
        inspect(ArrayContentsFailure::UnknownIndex);
        load->setOperand(1, key);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "0"));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "Length"));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "length"));
        inspect(ArrayContentsFailure::None);
        unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, static_cast<ctjs::UnaryKind>(99)));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Neg));
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = mutation.what, .body = mutation.body, .expected = ""},
             "the live length fixture did not parse");
    }
    if (auto module = parse(originalIndex)) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::BinaryOp binary;
        ctjs::GetPropertyOp load;
        module->walk([&](ctjs::BinaryOp op) { binary = op; });
        module->walk([&](ctjs::GetPropertyOp op) { load = op; });
        auto literal = binary.getRhs().getDefiningOp<ctjs::ConstantOp>();
        const mlir::Attribute offset = literal.getValue();
        const mlir::Value lhs = binary.getLhs();
        const mlir::Value base = load.getObject();
        mlir::OpBuilder builder(function);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        binary->setAttr("ctnative.array_index", builder.getI64IntegerAttr(0));
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*module))) {
            fail(row{.what = originalIndex.what, .body = originalIndex.body, .expected = ""},
                 "the subtracted-index stale solver did not converge");
        }
        const auto inspect = [&](ArrayContentsFailure failure) {
            contents_row current = originalIndex;
            current.failure = failure;
            check(*module, current, "x");
            const bool complete = failure == ArrayContentsFailure::None;
            const auto verdicts = computeVerdicts(stale, function);
            if (verdicts.arrayRetentionComplete != complete ||
                verdicts.confinedStoredSites != (complete ? 1U : 0U)) {
                fail(row{.what = current.what, .body = current.body, .expected = ""},
                     "stale solver or forged marker supplied subtracted-index authority");
            }
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 0));
        inspect(ArrayContentsFailure::MissingElement);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 4602678819172646912ULL));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 9221120237041090560ULL));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::BigIntAttr::get(&context, "1"));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "1"));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(offset);
        inspect(ArrayContentsFailure::None);
        binary->setOperand(0, binary.getRhs());
        inspect(ArrayContentsFailure::UnknownIndex);
        binary->setOperand(0, lhs);
        binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Add));
        inspect(ArrayContentsFailure::UnknownIndex);
        binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Sub));
        load->setOperand(0, function.getBody().front().getArgument(3));
        inspect(ArrayContentsFailure::UnknownArray);
        load->setOperand(0, base);
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = originalIndex.what, .body = originalIndex.body, .expected = ""},
             "the live subtracted-index fixture did not parse");
    }
    std::printf("dense array length: %u rows, %u live states, one wide snapshot, "
                "%zu retention budget cutoffs\n",
                rows, liveStates, budgets);
}

} // namespace ctcompile::test::escape::arrays
