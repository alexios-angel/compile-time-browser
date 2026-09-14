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
    const auto run = [&](const contents_row & expected, const char * discharged = "x",
                         const char * receiverVerdict = nullptr) {
        if (auto module = parse(expected)) {
            check(*module, expected, discharged);
            if (receiverVerdict != nullptr) {
                ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
                mlir::DataFlowSolver solver;
                solver.load<mlir::dataflow::DeadCodeAnalysis>();
                solver.load<mlir::dataflow::SparseConstantPropagation>();
                solver.load<EscapeAnalysis>();
                const row r{
                    .what = expected.what, .body = expected.body, .expected = receiverVerdict};
                if (failed(solver.initializeAndRun(*module))) {
                    fail(r, "the private receiver solver did not converge");
                } else {
                    ctjs::CreateArrayOp receiver;
                    module->walk([&](ctjs::CreateArrayOp op) {
                        if (contentsLabel(op) == "a") { receiver = op; }
                    });
                    if (!receiver ||
                        verdictString(computeVerdicts(solver, function, 0), receiver) !=
                            "escapes:passed" ||
                        verdictString(computeVerdicts(solver, function), receiver) !=
                            receiverVerdict) {
                        fail(r, "the private receiver verdict or original Passed witness differs");
                    }
                }
            }
        } else {
            fail(row{.what = expected.what, .body = expected.body, .expected = ""},
                 "the dense length fixture did not parse");
        }
        ++rows;
    };
    const std::string privateValues =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %key = ctjs.constant #ctjs.string<\"length\"> {storage_test_id = \"key\"}\n"
        "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n";
    run({.what = "a private own-length receiver needs no Stored child candidate",
         .body = privateValues + read + done,
         .arrays = "a:[]",
         .exit = "length -> {}"},
        "", "confined");
    run({.what = "a returned array keeps its first Passed witness after own length",
         .body = privateValues + read + "  ctjs.return %a\n",
         .arrays = "a:[]",
         .exit = "a -> {a}"},
        "", "escapes:passed");
    run({.what = "a returned saved child does not retain its overwritten private array",
         .body = values + "  %index = ctjs.constant #ctjs.string<\"0\">\n"
                          "  %saved = ctjs.get_property %a[%index]\n"
                          "  ctjs.set_property %a[%index], %zero\n"
                          "  ctjs.return %saved\n",
         .arrays = "a:[zero]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        "", "confined");
    run({.what = "a private own-length receiver cannot hide an inactive unknown effect",
         .body = privateValues + read +
                 "  %flag = ctjs.truthy %zero\n"
                 "  cf.cond_br %flag, ^effect, ^safe\n"
                 "^effect:\n  %called = ctjs.call %p(%a)\n  ctjs.return %length\n"
                 "^safe:\n  ctjs.return %length\n",
         .failure = ArrayContentsFailure::UnsupportedOperation},
        "", "escapes:passed");
    run({.what = "an own element overwrite can be the private array's first Passed witness",
         .body = "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
                 "  %a = ctjs.create_array [%zero] {storage_test_id = \"a\"}\n" +
                 overwrite + "  ctjs.return %zero\n",
         .arrays = "a:[zero]",
         .exit = "zero -> {}"},
        "", "confined");
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
    const std::string addIndex =
        "  %index = ctjs.binary_static add %zero, %zero {storage_test_id = \"index\"}\n";
    run({.what = "bounded static Number Add selects its exact overwritten element",
         .body = values + addIndex + indexed,
         .arrays = "a:[zero]",
         .exit = "a -> {a}"});
    run({.what = "original Number Add selects its exact overwritten element without optimization",
         .body = values +
                 "  %index = ctjs.binary add %zero, %zero {storage_test_id = \"index\"}\n" +
                 indexed,
         .arrays = "a:[zero]",
         .exit = "a -> {a}"});
    run({.what = "original Number Add rejects overflow before narrowing or subtraction",
         .body = values + one +
                 "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                 "  %sum = ctjs.binary add %bound, %one\n"
                 "  %index = ctjs.binary sub %sum, %bound\n" +
                 indexed,
         .failure = ArrayContentsFailure::UnknownIndex});
    run({.what = "static Add uses the saved length before a later append",
         .body = values + read +
                 "  %index = ctjs.binary_static add %length, %zero\n"
                 "  ctjs.append %zero to %a\n"
                 "  %saved = ctjs.get_property %a[%index]\n  ctjs.return %saved\n",
         .arrays = "a:[x,zero]",
         .reads = "a[1]=zero",
         .exit = "zero -> {}"});
    run({.what = "saved Add facts survive replacement and simultaneous successor transport",
         .body = values + one +
                 "  ctjs.append %zero to %a\n"
                 "  %index = ctjs.binary_static add %zero, %one {storage_test_id = \"index\"}\n"
                 "  %keys = ctjs.create_array [%index] {storage_test_id = \"keys\"}\n"
                 "  %saved = ctjs.get_property %keys[%zero]\n"
                 "  ctjs.set_property %keys[%zero], %zero\n"
                 "  cf.br ^pair(%saved, %zero : !ctjs.value, !ctjs.value)\n"
                 "^pair(%before: !ctjs.value, %after: !ctjs.value):\n"
                 "  cf.br ^swapped(%after, %before : !ctjs.value, !ctjs.value)\n"
                 "^swapped(%newIndex: !ctjs.value, %oldIndex: !ctjs.value):\n"
                 "  %old = ctjs.get_property %a[%oldIndex]\n"
                 "  %next = ctjs.binary_static add %newIndex, %zero\n"
                 "  %child = ctjs.get_property %a[%next]\n"
                 "  ctjs.set_property %a[%next], %zero\n  ctjs.return %child\n",
         .arrays = "a:[zero,zero]; keys:[zero]",
         .reads = "keys[0]=index; a[1]=zero; a[0]=x",
         .exit = "x -> {x}"},
        "");
    for (const std::string input : {"#ctjs.number<9223372036854775808>",    // -0
                                    "#ctjs.number<4751297606873776128>"}) { // 2^32-1
        run({.what = "bounded Add endpoints retain exact offset authority without wrap",
             .body = values + "  %bound = ctjs.constant " + input +
                     "\n  %sum = ctjs.binary_static add %bound, %zero\n"
                     "  %index = ctjs.binary sub %sum, %bound\n" +
                     indexed,
             .arrays = "a:[zero]",
             .exit = "a -> {a}"});
    }
    run({.what = "static Add rejects an exact sum above the array-length bound",
         .body = values + one +
                 "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                 "  %sum = ctjs.binary_static add %bound, %one\n"
                 "  %index = ctjs.binary sub %sum, %bound\n" +
                 indexed,
         .failure = ArrayContentsFailure::UnknownIndex});
    for (const std::string input : {"#ctjs.number<4602678819172646912>",  // 0.5
                                    "#ctjs.number<13830554455654793216>", // -1
                                    "#ctjs.number<4751297606875873280>",  // 2^32
                                    "#ctjs.number<9218868437227405312>",  // infinity
                                    "#ctjs.number<9221120237041090560>",  // NaN
                                    "#ctjs.string<\"0\">", "#ctjs.bigint<\"0\">",
                                    "#ctjs.boolean<false>", "#ctjs.null", "#ctjs.undefined"}) {
        for (const bool left : {false, true}) {
            run({.what = "each static Add operand independently needs an exact bounded Number",
                 .body = values + "  %input = ctjs.constant " + input +
                         "\n  %index = ctjs.binary_static add " +
                         (left ? "%input, %zero\n" : "%zero, %input\n") + indexed,
                 .failure = ArrayContentsFailure::UnknownIndex});
            run({.what = "original Add also requires two independently bounded Number operands",
                 .body = values + "  %input = ctjs.constant " + input +
                         "\n  %index = ctjs.binary add " +
                         (left ? "%input, %zero\n" : "%zero, %input\n") + indexed,
                 .failure = ArrayContentsFailure::UnknownIndex});
        }
    }
    run({.what = "one exact Add arm cannot authorize an opaque forwarded operand",
         .body = values + addIndex +
                 "  %flag = ctjs.truthy %zero\n"
                 "  cf.cond_br %flag, ^join(%index : !ctjs.value), ^join(%p : !ctjs.value)\n"
                 "^join(%before: !ctjs.value):\n"
                 "  %next = ctjs.binary_static add %before, %zero\n"
                 "  ctjs.set_property %a[%next], %zero\n  ctjs.return %a\n",
         .failure = ArrayContentsFailure::UnknownValue});
    run({.what = "exact Add values cannot prune an inactive publication arm",
         .body = values + addIndex +
                 "  %flag = ctjs.truthy %index\n  cf.cond_br %flag, ^safe, ^effect\n"
                 "^safe:\n  ctjs.return %zero\n"
                 "^effect:\n  ctjs.store_global \"held\", %a\n  ctjs.return %zero\n",
         .failure = ArrayContentsFailure::UnsupportedOperation});
    run({.what = "exact Add transport supplies no repeated-block or induction proof",
         .body = values + one +
                 "  cf.br ^loop(%zero : !ctjs.value)\n"
                 "^loop(%before: !ctjs.value):\n"
                 "  %next = ctjs.binary_static add %before, %one\n"
                 "  cf.br ^loop(%next : !ctjs.value)\n",
         .failure = ArrayContentsFailure::UnsupportedControlFlow});
    const std::string stringOne = "  %one = ctjs.constant #ctjs.string<\"1\">\n";
    // Preserve the exact formerly refused String-offset body.
    run({.what = "the original denseIndexStringOffset releases its overwritten child",
         .body = values + stringOne + read + subtract + indexed,
         .arrays = "a:[zero]",
         .exit = "a -> {a}"});
    run({.what = "a canonical String offset two selects an earlier dense element",
         .body = values +
                 "  %one = ctjs.constant #ctjs.string<\"2\">\n"
                 "  ctjs.append %zero to %a\n" +
                 read + subtract + indexed,
         .arrays = "a:[zero,zero]",
         .exit = "a -> {a}"});
    run({.what = "String zero preserves the saved length after append",
         .body = values + "  %one = ctjs.constant #ctjs.string<\"0\">\n" + read +
                 "  ctjs.append %zero to %a\n" + subtract + indexed,
         .arrays = "a:[x,zero]",
         .exit = "a -> {a,x}"},
        "");
    run({.what = "a String-offset read still retains the original child after overwrite",
         .body = values + stringOne + read + subtract +
                 "  %saved = ctjs.get_property %a[%index]\n" + overwrite + "  ctjs.return %saved\n",
         .arrays = "a:[zero]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        "");
    run({.what = "a loaded original String offset survives replacement of its source slot",
         .body = values + stringOne + read +
                 "  %offsets = ctjs.create_array [%one] {storage_test_id = \"offsets\"}\n"
                 "  %saved = ctjs.get_property %offsets[%zero]\n"
                 "  ctjs.set_property %offsets[%zero], %x\n"
                 "  %index = ctjs.binary sub %length, %saved\n" +
                 indexed,
         .arrays = "a:[zero]; offsets:[x]",
         .reads = "offsets[0]=ctjs.constant",
         .exit = "a -> {a}"});
    for (const std::string literal : {"#ctjs.string<\"01\">", "#ctjs.bigint<\"1\">"}) {
        run({.what = "a valid forwarded String offset cannot authorize an untaken unsafe arm",
             .body = values + stringOne + read + "  %bad = ctjs.constant " + literal +
                     "\n  %flag = ctjs.truthy %zero\n"
                     "  cf.cond_br %flag, ^join(%bad : !ctjs.value), ^join(%one : !ctjs.value)\n"
                     "^join(%offset: !ctjs.value):\n"
                     "  %index = ctjs.binary sub %length, %offset\n" +
                     indexed,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    run({.what = "a computed String offset has no original literal index authority",
         .body = values + stringOne + read +
                 "  %empty = ctjs.constant #ctjs.string<\"\">\n"
                 "  %offset = ctjs.binary concat %one, %empty\n"
                 "  %index = ctjs.binary sub %length, %offset\n" +
                 indexed,
         .failure = ArrayContentsFailure::UnknownIndex});
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
    run({.what = "held lengths survive storage, reload, replacement and simultaneous transport",
         .body = values + one + read +
                 "  %snapshots = ctjs.create_array [%length] {storage_test_id = \"snapshots\"}\n"
                 "  %saved = ctjs.get_property %snapshots[%zero]\n"
                 "  ctjs.append %zero to %a\n"
                 "  %later = ctjs.get_property %a[%key] {storage_test_id = \"later\"}\n"
                 "  ctjs.set_property %snapshots[%zero], %later\n"
                 "  %fresh = ctjs.get_property %snapshots[%zero]\n"
                 "  cf.br ^pair(%saved, %fresh : !ctjs.value, !ctjs.value)\n"
                 "^pair(%before: !ctjs.value, %after: !ctjs.value):\n"
                 "  cf.br ^swapped(%after, %before : !ctjs.value, !ctjs.value)\n"
                 "^swapped(%newLength: !ctjs.value, %oldLength: !ctjs.value):\n"
                 "  %oldIndex = ctjs.binary sub %oldLength, %one\n"
                 "  %newIndex = ctjs.binary sub %newLength, %one\n"
                 "  %oldChild = ctjs.get_property %a[%oldIndex]\n"
                 "  %newChild = ctjs.get_property %a[%newIndex]\n"
                 "  ctjs.set_property %a[%oldIndex], %zero\n"
                 "  ctjs.return %a\n",
         .arrays = "a:[zero,zero]; snapshots:[later]",
         .reads = "snapshots[0]=length; snapshots[0]=later; a[0]=x; a[1]=zero",
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
                                      "#ctjs.string<\"2\">",
                                      "#ctjs.string<\"4294967294\">",
                                      "#ctjs.string<\"4294967295\">",
                                      "#ctjs.string<\"4294967296\">",
                                      "#ctjs.string<\"9007199254740993\">",
                                      "#ctjs.string<\"\">",
                                      "#ctjs.string<\" \">",
                                      "#ctjs.string<\"01\">",
                                      "#ctjs.string<\"+1\">",
                                      "#ctjs.string<\"-0\">",
                                      "#ctjs.string<\"1.0\">",
                                      "#ctjs.string<\"1e0\">",
                                      "#ctjs.string<\"0x1\">",
                                      "#ctjs.string<\"Infinity\">",
                                      "#ctjs.string<\"NaN\">",
                                      "#ctjs.bigint<\"1\">",
                                      "#ctjs.boolean<true>",
                                      "#ctjs.null",
                                      "#ctjs.undefined"}) {
        run({.what = "subtraction requires a bounded Number or canonical String offset",
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
             .failure = (producer == "ctjs.binary_static add %length, %zero" ||
                         producer == "ctjs.binary add %length, %one")
                            ? ArrayContentsFailure::MissingElement
                            : ArrayContentsFailure::UnknownIndex});
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
    const std::string shrink = "  ctjs.set_property %a[%key], %zero\n";
    // Preserve the exact formerly refused length-write body.
    run({.what = "a bounded own length write drops the original dense elements",
         .body = values + read + shrink + done,
         .arrays = "a:[]",
         .exit = "length -> {}"});
    const contents_row originalShrink{
        .what = "the original denseLengthChanged source releases only its removed child",
        .body = values + read + shrink +
                "  %result = ctjs.create_array [%a, %length] {storage_test_id = \"result\"}\n"
                "  ctjs.return %result\n",
        .arrays = "a:[]; result:[a,length]",
        .exit = "result -> {a,result}"};
    run(originalShrink);
    run({.what = "a same-length write preserves every original child",
         .body = values + one + read + "  ctjs.set_property %a[%key], %one\n  ctjs.return %a\n",
         .arrays = "a:[x]",
         .exit = "a -> {a,x}"},
        "");
    run({.what = "a bounded nonzero shrink retains the unremoved prefix",
         .body = values + one + "  ctjs.append %x to %a\n" +
                 "  ctjs.set_property %a[%key], %one\n  ctjs.return %a\n",
         .arrays = "a:[x]",
         .exit = "a -> {a,x}"},
        "");
    run({.what = "negative zero is an exact empty length",
         .body = values + "  %wanted = ctjs.constant #ctjs.number<9223372036854775808>\n" + read +
                 "  ctjs.set_property %a[%key], %wanted\n" + done,
         .arrays = "a:[]",
         .exit = "length -> {}"});
    run({.what = "a saved child remains retained after its array is truncated",
         .body = values + read + "  %saved = ctjs.get_property %a[%zero]\n" + shrink +
                 "  ctjs.return %saved\n",
         .arrays = "a:[]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        "");
    run({.what = "an exact array alias truncates only its own contents",
         .body = values + "  %b = ctjs.create_array [%x] {storage_test_id = \"b\"}\n"
                          "  %aliases = ctjs.create_array [%a] {storage_test_id = \"aliases\"}\n"
                          "  %alias = ctjs.get_property %aliases[%zero]\n"
                          "  ctjs.set_property %alias[%key], %zero\n  ctjs.return %b\n",
         .arrays = "a:[]; b:[x]; aliases:[a]",
         .reads = "aliases[0]=a",
         .exit = "b -> {b,x}"},
        "a");
    run({.what = "saved length and index Numbers survive shrink while a new read sees zero",
         .body = values + one + "  ctjs.append %zero to %a\n" + read + subtract + shrink +
                 "  %keys = ctjs.create_array [%zero, %one] {storage_test_id = \"keys\"}\n"
                 "  %saved = ctjs.get_property %keys[%index]\n"
                 "  %later = ctjs.get_property %a[%key] {storage_test_id = \"later\"}\n"
                 "  %fresh = ctjs.get_property %keys[%later]\n"
                 "  %result = ctjs.create_array [%length, %index, %later, %saved, %fresh] "
                 "{storage_test_id = \"result\"}\n"
                 "  ctjs.return %result\n",
         .arrays =
             "a:[]; keys:[zero,ctjs.constant]; result:[length,index,later,ctjs.constant,zero]",
         .reads = "keys[1]=ctjs.constant; keys[0]=zero",
         .exit = "result -> {result}"});
    run({.what = "a saved length is not retargeted when subtraction happens after shrink",
         .body = values + one + "  ctjs.append %zero to %a\n" + read +
                 "  ctjs.set_property %a[%key], %one\n" + subtract + indexed,
         .failure = ArrayContentsFailure::MissingElement});
    run({.what = "a reloaded old length cannot borrow a later post-shrink value",
         .body = values + one + "  ctjs.append %zero to %a\n" + read +
                 "  %snapshots = ctjs.create_array [%length]\n"
                 "  %saved = ctjs.get_property %snapshots[%zero]\n"
                 "  ctjs.set_property %a[%key], %one\n"
                 "  %later = ctjs.get_property %a[%key]\n"
                 "  ctjs.set_property %snapshots[%zero], %later\n"
                 "  cf.br ^next(%saved : !ctjs.value)\n"
                 "^next(%oldLength: !ctjs.value):\n"
                 "  %index = ctjs.binary sub %oldLength, %one\n" +
                 indexed,
         .failure = ArrayContentsFailure::MissingElement});
    run({.what = "a removed slot cannot be read through its saved original index",
         .body = values + one + read + subtract + shrink +
                 "  %saved = ctjs.get_property %a[%index]\n  ctjs.return %saved\n",
         .failure = ArrayContentsFailure::MissingElement});
    run({.what = "literal append after truncation establishes only its new element",
         .body = values + read + shrink +
                 "  ctjs.append %zero to %a\n"
                 "  %saved = ctjs.get_property %a[%zero]\n  ctjs.return %a\n",
         .arrays = "a:[zero]",
         .reads = "a[0]=zero",
         .exit = "a -> {a}"});
    run({.what = "a loaded original Number length is independent of its overwritten slot",
         .body = values + "  %targets = ctjs.create_array [%zero] {storage_test_id = \"targets\"}\n"
                          "  %wanted = ctjs.get_property %targets[%zero]\n"
                          "  ctjs.set_property %targets[%zero], %x\n"
                          "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
         .arrays = "a:[]; targets:[x]",
         .reads = "targets[0]=zero",
         .exit = "a -> {a}"});
    run({.what = "forwarded original array key and Number keep their exact identities",
         .body = values + "  cf.br ^next(%a, %key, %zero : !ctjs.value, !ctjs.value, !ctjs.value)\n"
                          "^next(%alias: !ctjs.value, %name: !ctjs.value, %wanted: !ctjs.value):\n"
                          "  ctjs.set_property %alias[%name], %wanted\n  ctjs.return %alias\n",
         .arrays = "a:[]",
         .exit = "a -> {a}"});
    run({.what = "one truncating arm cannot release a child retained by the other arm",
         .body = values +
                 "  %flag = ctjs.truthy %zero\n"
                 "  cf.cond_br %flag, ^left, ^right\n^left:\n" +
                 shrink + "  cf.br ^join\n^right:\n  cf.br ^join\n^join:\n  ctjs.return %a\n",
         .arrays = "a:[] | a:[x]",
         .exit = "a -> {a}; a -> {a,x}"},
        "");
    for (const std::string literal : {"#ctjs.number<4602678819172646912>",  // 0.5
                                      "#ctjs.number<13830554455654793216>", // -1
                                      "#ctjs.number<4751297606875873280>",  // 2^32
                                      "#ctjs.number<4845873199050653696>",  // 2^53
                                      "#ctjs.number<9218868437227405312>",  // infinity
                                      "#ctjs.number<18442240474082181120>", // -infinity
                                      "#ctjs.number<9221120237041090560>",  // NaN
                                      "#ctjs.string<\"0\">", "#ctjs.bigint<\"0\">",
                                      "#ctjs.boolean<false>", "#ctjs.null", "#ctjs.undefined"}) {
        run({.what = "length truncation requires an independently valid original Number",
             .body = values + "  %wanted = ctjs.constant " + literal + "\n" +
                     "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    run({.what = "a valid increasing length remains outside dense truncation",
         .body = values + "  %two = ctjs.constant #ctjs.number<4611686018427387904>\n"
                          "  ctjs.set_property %a[%key], %two\n  ctjs.return %a\n",
         .failure = ArrayContentsFailure::MissingElement});
    for (const std::string wanted : {"%p", "%x", "%length", "%index"}) {
        run({.what = "computed or opaque length targets gain no truncation authority",
             .body = values + one + read + subtract + "  ctjs.set_property %a[%key], " + wanted +
                     "\n  ctjs.return %a\n",
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    const contents_row shrinkCycle{
        .what = "truncation does not erase the historical cycle obligation",
        .body = values + "  ctjs.append %a to %a\n" + shrink + "  ctjs.return %a\n",
        .arrays = "a:[]",
        .exit = "a -> {a}",
        .writes = "ctjs.create_array[0]:a[0]=x; ctjs.append[1]:a[1]=a"};
    if (auto module = parse(shrinkCycle)) {
        checkArrayContents(*module, shrinkCycle);
        budgets += checkArrayRetention(
            *module, {.what = shrinkCycle.what, .body = shrinkCycle.body, .complete = false});
        ++rows;
    } else {
        fail(row{.what = shrinkCycle.what, .body = shrinkCycle.body, .expected = ""},
             "the historical truncation cycle did not parse");
    }
    contents_row wideShrink = originalShrink;
    std::string removed;
    for (unsigned i = 0; i < 32; ++i) { removed += "  ctjs.append %zero to %a\n"; }
    wideShrink.body.insert(wideShrink.body.find("  %length ="), removed);
    auto narrowShrinkModule = parse(originalShrink);
    auto wideShrinkModule = parse(wideShrink);
    if (narrowShrinkModule && wideShrinkModule) {
        const auto narrow =
            computeArrayContents(*narrowShrinkModule->getOps<ctjs::FuncOp>().begin());
        const auto expanded =
            computeArrayContents(*wideShrinkModule->getOps<ctjs::FuncOp>().begin());
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
            fail(row{.what = "dense shrink charges every removed element",
                     .body = wideShrink.body,
                     .expected = ""},
                 "32 extra elements did not cost one append and one removal each");
        }
        check(*wideShrinkModule, wideShrink, "x");
    } else {
        fail(row{.what = wideShrink.what, .body = wideShrink.body, .expected = ""},
             "the wide dense shrink did not parse");
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
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 96) {
            fail(row{.what = "length snapshots charge every independent result",
                     .body = wide.body,
                     .expected = ""},
                 "32 extra lengths did not charge each read, exact value and held snapshot");
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
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "01"));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "2"));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "0"));
        inspect(ArrayContentsFailure::MissingElement);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "1"));
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(offset);
        inspect(ArrayContentsFailure::None);
        binary->setOperand(0, binary.getRhs());
        inspect(ArrayContentsFailure::UnknownIndex);
        binary->setOperand(0, lhs);
        binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Add));
        inspect(ArrayContentsFailure::MissingElement);
        binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Sub));
        load->setOperand(0, function.getBody().front().getArgument(3));
        inspect(ArrayContentsFailure::UnknownArray);
        load->setOperand(0, base);
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = originalIndex.what, .body = originalIndex.body, .expected = ""},
             "the live subtracted-index fixture did not parse");
    }
    if (auto module = parse(originalShrink)) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        module->walk([&](ctjs::SetPropertyOp op) { store = op; });
        const mlir::Value target = store.getObject();
        const mlir::Value key = store.getKey();
        const mlir::Value value = store.getValue();
        auto literal = value.getDefiningOp<ctjs::ConstantOp>();
        const mlir::Attribute original = literal.getValue();
        mlir::OpBuilder builder(function);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        store->setAttr("ctnative.array_length", builder.getI64IntegerAttr(0));
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*module))) {
            fail(row{.what = originalShrink.what, .body = originalShrink.body, .expected = ""},
                 "the length-write stale solver did not converge");
        }
        const auto inspect = [&](ArrayContentsFailure failure) {
            contents_row current = originalShrink;
            current.failure = failure;
            check(*module, current, "x");
            const bool complete = failure == ArrayContentsFailure::None;
            const auto verdicts = computeVerdicts(stale, function);
            if (verdicts.arrayRetentionComplete != complete ||
                verdicts.confinedStoredSites != (complete ? 1U : 0U)) {
                fail(row{.what = current.what, .body = current.body, .expected = ""},
                     "stale solver or forged marker supplied length-write authority");
            }
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 4611686018427387904ULL));
        inspect(ArrayContentsFailure::MissingElement);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 4602678819172646912ULL));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "0"));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::BigIntAttr::get(&context, "0"));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(original);
        inspect(ArrayContentsFailure::None);
        const mlir::Value parameter = function.getBody().front().getArgument(3);
        store->setOperand(0, parameter);
        inspect(ArrayContentsFailure::UnknownArray);
        store->setOperand(0, target);
        store->setOperand(1, parameter);
        inspect(ArrayContentsFailure::UnknownIndex);
        store->setOperand(1, key);
        store->setOperand(2, parameter);
        inspect(ArrayContentsFailure::UnknownIndex);
        store->setOperand(2, value);
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = originalShrink.what, .body = originalShrink.body, .expected = ""},
             "the live length-write fixture did not parse");
    }
    std::printf("dense array length: %u rows, %u live states, two wide snapshots, "
                "%zu retention budget cutoffs\n",
                rows, liveStates, budgets);
}

} // namespace ctcompile::test::escape::arrays
