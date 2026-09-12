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
          std::pair{"  %bad = ctjs.get_property %a[%length]\n", ArrayContentsFailure::UnknownIndex},
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
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
            fail(row{.what = "length snapshots charge every independent result",
                     .body = wide.body,
                     .expected = ""},
                 "32 extra length results did not cost one producer and one snapshot each");
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
    std::printf("dense array length: %u rows, %u live states, one wide snapshot, "
                "%zu retention budget cutoffs\n",
                rows, liveStates, budgets);
}

} // namespace ctcompile::test::escape::arrays
