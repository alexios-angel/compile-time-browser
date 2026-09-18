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
        run({.what = "subtraction still refuses unproved or missing indices",
             .body =
                 values + "  %one = ctjs.constant " + literal + "\n" + read + subtract + indexed,
             .failure = literal == "#ctjs.number<13830554455654793216>"
                            ? ArrayContentsFailure::MissingElement
                            : ArrayContentsFailure::UnknownIndex});
    }
    run({.what = "negative zero is an exact zero offset without changing the saved length",
         .body = values + "  %one = ctjs.constant #ctjs.number<9223372036854775808>\n" + read +
                 "  ctjs.append %zero to %a\n" + subtract + indexed,
         .arrays = "a:[x,zero]",
         .exit = "a -> {a,x}"},
        "");
    for (const std::string producer :
         {"ctjs.binary sub %one, %one", "ctjs.binary sub %one, %length"}) {
        run({.what = "an original bounded Number subtraction selects its exact overwritten slot",
             .body = values + one + read + "  %index = " + producer + "\n" + indexed,
             .arrays = "a:[zero]",
             .exit = "a -> {a}"});
    }
    for (const std::string producer :
         {"ctjs.binary sub %zero, %one", "ctjs.binary add %length, %one",
          "ctjs.binary_static add %length, %zero"}) {
        run({.what = "other arithmetic does not borrow length-subtraction index authority",
             .body = values + one + read + "  %index = " + producer + "\n" + indexed,
             .failure = (producer == "ctjs.binary_static add %length, %zero" ||
                         producer == "ctjs.binary add %length, %one")
                            ? ArrayContentsFailure::MissingElement
                            : ArrayContentsFailure::UnknownIndex});
    }
    run({.what = "bounded Number multiplication selects its exact overwritten slot",
         .body = values + one + read + "  %index = ctjs.binary mul %length, %zero\n" + indexed,
         .arrays = "a:[zero]",
         .exit = "a -> {a}"});
    run({.what = "a stored product keeps its read-time Number after replacement and transport",
         .body = values + one + read +
                 "  %product = ctjs.binary mul %one, %length {storage_test_id = \"product\"}\n"
                 "  %saved = ctjs.create_array [%product] {storage_test_id = \"saved\"}\n"
                 "  %loaded = ctjs.get_property %saved[%zero]\n"
                 "  ctjs.set_property %saved[%zero], %x\n"
                 "  ctjs.append %zero to %a\n"
                 "  cf.br ^next(%loaded, %zero : !ctjs.value, !ctjs.value)\n"
                 "^next(%before: !ctjs.value, %after: !ctjs.value):\n"
                 "  cf.br ^swapped(%after, %before : !ctjs.value, !ctjs.value)\n"
                 "^swapped(%replacement: !ctjs.value, %original: !ctjs.value):\n"
                 "  %index = ctjs.binary sub %original, %one\n"
                 "  ctjs.set_property %a[%index], %replacement\n  ctjs.return %a\n",
         .arrays = "a:[zero,zero]; saved:[x]",
         .reads = "saved[0]=product",
         .exit = "a -> {a}"});
    run({.what = "a product index cannot release a returned saved child",
         .body = values + read +
                 "  %index = ctjs.binary mul %length, %zero\n"
                 "  %saved = ctjs.get_property %a[%index]\n" +
                 overwrite + "  ctjs.return %saved\n",
         .arrays = "a:[zero]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        "");
    run({.what = "the maximum bounded Number product remains exact before subtraction",
         .body = values + one +
                 "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                 "  %product = ctjs.binary mul %bound, %one\n"
                 "  %index = ctjs.binary sub %product, %bound\n" +
                 indexed,
         .arrays = "a:[zero]",
         .exit = "a -> {a}"});
    run({.what = "the maximum Number product is a length but never an own element index",
         .body = values + one +
                 "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                 "  %index = ctjs.binary mul %bound, %one\n" +
                 indexed,
         .failure = ArrayContentsFailure::UnknownIndex});
    for (const std::string multiplier : {"%two", "%bound"}) {
        run({.what = "an out-of-range product cannot lend Number evidence to a later zero product",
             .body = values +
                     "  %two = ctjs.constant #ctjs.number<4611686018427387904>\n"
                     "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                     "  %product = ctjs.binary mul %bound, " +
                     multiplier + "\n  %index = ctjs.binary mul %zero, %product\n" + indexed,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    run({.what = "a structural multiplication arm cannot borrow another arm's Number",
         .body = values + one + read +
                 "  %flag = ctjs.truthy %zero\n"
                 "  cf.cond_br %flag, ^join(%length : !ctjs.value), ^join(%p : !ctjs.value)\n"
                 "^join(%input: !ctjs.value):\n"
                 "  %index = ctjs.binary mul %input, %zero\n" +
                 indexed,
         .failure = ArrayContentsFailure::UnsupportedOperation});
    run({.what = "a stored quotient keeps its read-time Number after replacement and transport",
         .body = values + one + read +
                 "  %quotient = ctjs.binary div %one, %length {storage_test_id = \"quotient\"}\n"
                 "  %saved = ctjs.create_array [%quotient] {storage_test_id = \"saved\"}\n"
                 "  %loaded = ctjs.get_property %saved[%zero]\n"
                 "  ctjs.set_property %saved[%zero], %x\n"
                 "  ctjs.append %zero to %a\n"
                 "  cf.br ^next(%loaded, %zero : !ctjs.value, !ctjs.value)\n"
                 "^next(%before: !ctjs.value, %after: !ctjs.value):\n"
                 "  cf.br ^swapped(%after, %before : !ctjs.value, !ctjs.value)\n"
                 "^swapped(%replacement: !ctjs.value, %original: !ctjs.value):\n"
                 "  %index = ctjs.binary sub %original, %one\n"
                 "  ctjs.set_property %a[%index], %replacement\n  ctjs.return %a\n",
         .arrays = "a:[zero,zero]; saved:[x]",
         .reads = "saved[0]=quotient",
         .exit = "a -> {a}"});
    run({.what = "forwarded original Numbers divide exactly after their source slot changes",
         .body = values + one +
                 "  %inputs = ctjs.create_array [%one] {storage_test_id = \"inputs\"}\n"
                 "  %saved = ctjs.get_property %inputs[%zero]\n"
                 "  ctjs.set_property %inputs[%zero], %x\n"
                 "  cf.br ^next(%saved, %zero : !ctjs.value, !ctjs.value)\n"
                 "^next(%divisor: !ctjs.value, %numerator: !ctjs.value):\n"
                 "  %index = ctjs.binary div %numerator, %divisor\n" +
                 indexed,
         .arrays = "a:[zero]; inputs:[x]",
         .reads = "inputs[0]=ctjs.constant",
         .exit = "a -> {a}"});
    run({.what = "a quotient index cannot release a returned saved child",
         .body = values + read +
                 "  %index = ctjs.binary div %zero, %length\n"
                 "  %saved = ctjs.get_property %a[%index]\n" +
                 overwrite + "  ctjs.return %saved\n",
         .arrays = "a:[zero]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        "");
    run({.what = "the maximum bounded quotient remains exact before subtraction",
         .body = values + one +
                 "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                 "  %quotient = ctjs.binary div %bound, %one\n"
                 "  %index = ctjs.binary sub %quotient, %bound\n" +
                 indexed,
         .arrays = "a:[zero]",
         .exit = "a -> {a}"});
    run({.what = "an exact nonzero quotient by a larger divisor supplies its bounded offset",
         .body = values +
                 "  %four = ctjs.constant #ctjs.number<4616189618054758400>\n"
                 "  %two = ctjs.constant #ctjs.number<4611686018427387904>\n"
                 "  %quotient = ctjs.binary div %four, %two\n"
                 "  %index = ctjs.binary sub %quotient, %two\n" +
                 indexed,
         .arrays = "a:[zero]",
         .exit = "a -> {a}"});
    run({.what = "the maximum quotient is a length but never an own element index",
         .body = values + one +
                 "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                 "  %index = ctjs.binary div %bound, %one\n" +
                 indexed,
         .failure = ArrayContentsFailure::UnknownIndex});
    for (const std::string literal : {"#ctjs.number<0>",
                                      "#ctjs.number<9223372036854775808>", // -0
                                      "#ctjs.number<4611686018427387904>", // fractional 1/2
                                      "#ctjs.number<4602678819172646912>", // 0.5
                                      "#ctjs.number<4751297606875873280>", // 2^32
                                      "#ctjs.number<9218868437227405312>", // infinity
                                      "#ctjs.number<9221120237041090560>", // NaN
                                      "#ctjs.string<\"1\">", "#ctjs.bigint<\"1\">",
                                      "#ctjs.boolean<true>", "#ctjs.null", "#ctjs.undefined"}) {
        run({.what = "division needs an exact integral Number quotient without coercion",
             .body = values + one + "  %divisor = ctjs.constant " + literal +
                     "\n  %index = ctjs.binary div %one, %divisor\n" + indexed,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    run({.what = "an out-of-range numerator cannot lend Number evidence to a later division",
         .body = values + one +
                 "  %bound = ctjs.constant #ctjs.number<4751297606875873280>\n"
                 "  %quotient = ctjs.binary div %bound, %one\n"
                 "  %index = ctjs.binary div %zero, %quotient\n" +
                 indexed,
         .failure = ArrayContentsFailure::UnknownIndex});
    run({.what = "a structural division arm cannot borrow another arm's Number",
         .body = values + read +
                 "  %flag = ctjs.truthy %zero\n"
                 "  cf.cond_br %flag, ^join(%length : !ctjs.value), ^join(%p : !ctjs.value)\n"
                 "^join(%divisor: !ctjs.value):\n"
                 "  %index = ctjs.binary div %zero, %divisor\n" +
                 indexed,
         .failure = ArrayContentsFailure::UnsupportedOperation});
    run({.what = "a stored remainder keeps its read-time Number after replacement and transport",
         .body = values + one + read +
                 "  %two = ctjs.constant #ctjs.number<4611686018427387904>\n"
                 "  %remainder = ctjs.binary mod %length, %two {storage_test_id = \"remainder\"}\n"
                 "  %saved = ctjs.create_array [%remainder] {storage_test_id = \"saved\"}\n"
                 "  %loaded = ctjs.get_property %saved[%zero]\n"
                 "  ctjs.set_property %saved[%zero], %x\n"
                 "  ctjs.append %zero to %a\n"
                 "  cf.br ^next(%loaded, %zero : !ctjs.value, !ctjs.value)\n"
                 "^next(%before: !ctjs.value, %after: !ctjs.value):\n"
                 "  cf.br ^swapped(%after, %before : !ctjs.value, !ctjs.value)\n"
                 "^swapped(%replacement: !ctjs.value, %original: !ctjs.value):\n"
                 "  %index = ctjs.binary sub %original, %one\n"
                 "  ctjs.set_property %a[%index], %replacement\n  ctjs.return %a\n",
         .arrays = "a:[zero,zero]; saved:[x]",
         .reads = "saved[0]=remainder",
         .exit = "a -> {a}"});
    run({.what = "forwarded original Numbers give an exact remainder after their slot changes",
         .body = values + one +
                 "  %inputs = ctjs.create_array [%one] {storage_test_id = \"inputs\"}\n"
                 "  %saved = ctjs.get_property %inputs[%zero]\n"
                 "  ctjs.set_property %inputs[%zero], %x\n"
                 "  cf.br ^next(%saved, %one : !ctjs.value, !ctjs.value)\n"
                 "^next(%divisor: !ctjs.value, %numerator: !ctjs.value):\n"
                 "  %index = ctjs.binary mod %numerator, %divisor\n" +
                 indexed,
         .arrays = "a:[zero]; inputs:[x]",
         .reads = "inputs[0]=ctjs.constant",
         .exit = "a -> {a}"});
    run({.what = "a remainder index cannot release a returned saved child",
         .body = values + read +
                 "  %index = ctjs.binary mod %length, %length\n"
                 "  %saved = ctjs.get_property %a[%index]\n" +
                 overwrite + "  ctjs.return %saved\n",
         .arrays = "a:[zero]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        "");
    for (const std::string divisor : {"%two", "%near"}) {
        run({.what = "a nonzero bounded remainder stays exact at the maximum Number endpoint",
             .body = values + one +
                     "  %two = ctjs.constant #ctjs.number<4611686018427387904>\n"
                     "  %near = ctjs.constant #ctjs.number<4751297606871678976>\n"
                     "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                     "  %remainder = ctjs.binary mod %bound, " +
                     divisor + "\n  %index = ctjs.binary sub %remainder, %one\n" + indexed,
             .arrays = "a:[zero]",
             .exit = "a -> {a}"});
    }
    for (const std::string literal : {"#ctjs.number<0>",
                                      "#ctjs.number<9223372036854775808>", // -0
                                      "#ctjs.number<4602678819172646912>", // 0.5
                                      "#ctjs.number<4751297606875873280>", // 2^32
                                      "#ctjs.number<9218868437227405312>", // infinity
                                      "#ctjs.number<9221120237041090560>", // NaN
                                      "#ctjs.string<\"1\">", "#ctjs.bigint<\"1\">",
                                      "#ctjs.boolean<true>", "#ctjs.null", "#ctjs.undefined"}) {
        run({.what = "remainder needs an exact nonzero bounded divisor without coercion",
             .body = values + "  %divisor = ctjs.constant " + literal +
                     "\n  %index = ctjs.binary mod %zero, %divisor\n" + indexed,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    for (const std::string operation : {"div", "mod"}) {
        run({.what = "zero with a negative divisor keeps its original result and exact index",
             .body = values +
                     "  %divisor = ctjs.constant #ctjs.number<13830554455654793216>\n"
                     "  %index = ctjs.binary " +
                     operation + " %zero, %divisor\n" + indexed,
             .arrays = "a:[zero]",
             .exit = "a -> {a}"});
    }
    run({.what = "a remainder keeps the positive dividend sign despite a negative divisor",
         .body = values + one +
                 "  %divisor = ctjs.constant #ctjs.number<13835058055282163712>\n"
                 "  %remainder = ctjs.binary mod %one, %divisor\n"
                 "  %index = ctjs.binary sub %remainder, %one\n" +
                 indexed,
         .arrays = "a:[zero]",
         .exit = "a -> {a}"});
    run({.what = "an out-of-range numerator cannot lend evidence to a later zero remainder",
         .body = values + one +
                 "  %bound = ctjs.constant #ctjs.number<4751297606875873280>\n"
                 "  %remainder = ctjs.binary mod %bound, %one\n"
                 "  %index = ctjs.binary mod %remainder, %one\n" +
                 indexed,
         .failure = ArrayContentsFailure::UnknownIndex});
    run({.what = "a structural remainder arm cannot borrow another arm's Number",
         .body = values + read +
                 "  %flag = ctjs.truthy %zero\n"
                 "  cf.cond_br %flag, ^join(%length : !ctjs.value), ^join(%p : !ctjs.value)\n"
                 "^join(%divisor: !ctjs.value):\n"
                 "  %index = ctjs.binary mod %zero, %divisor\n" +
                 indexed,
         .failure = ArrayContentsFailure::UnsupportedOperation});
    for (const std::string kind : {"ushr", "shr", "shl", "bitand", "bitor", "bitxor"}) {
        const std::string unchanged = kind == "bitand" ? "%one" : "%zero";
        const std::string cleared = kind == "bitand" || kind == "bitor" ? "%zero" : "%length";
        const std::string input = kind == "bitor" || kind == "shl" ? "%zero" : "%length";
        run({.what = "a stored bitwise result keeps its Number after replacement and transport",
             .body = values + one + read + "  %shifted = ctjs.binary_static " + kind +
                     " %length, " + unchanged +
                     " {storage_test_id = \"shifted\"}\n"
                     "  %saved = ctjs.create_array [%shifted] {storage_test_id = \"saved\"}\n"
                     "  %loaded = ctjs.get_property %saved[%zero]\n"
                     "  ctjs.set_property %saved[%zero], %x\n"
                     "  ctjs.append %zero to %a\n"
                     "  cf.br ^next(%loaded, %zero : !ctjs.value, !ctjs.value)\n"
                     "^next(%before: !ctjs.value, %after: !ctjs.value):\n"
                     "  cf.br ^swapped(%after, %before : !ctjs.value, !ctjs.value)\n"
                     "^swapped(%replacement: !ctjs.value, %original: !ctjs.value):\n"
                     "  %index = ctjs.binary sub %original, %one\n"
                     "  ctjs.set_property %a[%index], %replacement\n  ctjs.return %a\n",
             .arrays = "a:[zero,zero]; saved:[x]",
             .reads = "saved[0]=shifted",
             .exit = "a -> {a}"});
        run({.what = "a bitwise index cannot release a returned saved child",
             .body = values + read + "  %index = ctjs.binary_static " + kind + " " + input + ", " +
                     cleared + "\n  %saved = ctjs.get_property %a[%index]\n" + overwrite +
                     "  ctjs.return %saved\n",
             .arrays = "a:[zero]",
             .reads = "a[0]=x",
             .exit = "x -> {x}"},
            "");
    }
    for (const auto & [count, result] :
         {std::pair{"0", "4751297606873776128"},                      // 0 -> 2^32-1
          std::pair{"9223372036854775808", "4751297606873776128"},    // -0 -> 2^32-1
          std::pair{"4607182418800017408", "4746794007244308480"},    // 1 -> 2^31-1
          std::pair{"4629418941960159232", "4607182418800017408"},    // 31 -> 1
          std::pair{"4629700416936869888", "4751297606873776128"},    // 32 -> 2^32-1
          std::pair{"4629841154425225216", "4746794007244308480"},    // 33 -> 2^31-1
          std::pair{"4751297606873776128", "4607182418800017408"}}) { // 2^32-1 -> 1
        run({.what = "unsigned right shift masks its count and preserves the high-bit operand",
             .body = values +
                     "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                     "  %count = ctjs.constant #ctjs.number<" +
                     count + ">\n  %expected = ctjs.constant #ctjs.number<" + result +
                     ">\n  %shifted = ctjs.binary_static ushr %bound, %count\n"
                     "  %index = ctjs.binary sub %shifted, %expected\n" +
                     indexed,
             .arrays = "a:[zero]",
             .exit = "a -> {a}"});
    }
    run({.what = "the maximum unsigned result remains outside own element indices",
         .body = values +
                 "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                 "  %index = ctjs.binary_static ushr %bound, %zero\n" +
                 indexed,
         .failure = ArrayContentsFailure::UnknownIndex});
    for (const auto & [count, result] :
         {std::pair{"0", "4746794007244308480"},                   // 0 -> 2^31-1
          std::pair{"9223372036854775808", "4746794007244308480"}, // -0 -> 2^31-1
          std::pair{"4607182418800017408", "4742290407612743680"}, // 1 -> 2^30-1
          std::pair{"4629418941960159232", "0"},                   // 31 -> 0
          std::pair{"4629700416936869888", "4746794007244308480"}, // 32 -> 2^31-1
          std::pair{"4629841154425225216", "4742290407612743680"}, // 33 -> 2^30-1
          std::pair{"4751297606873776128", "0"}}) {                // 2^32-1 -> 0
        run({.what = "signed right shift masks its count at the maximum nonnegative input",
             .body = values +
                     "  %bound = ctjs.constant #ctjs.number<4746794007244308480>\n"
                     "  %count = ctjs.constant #ctjs.number<" +
                     count + ">\n  %expected = ctjs.constant #ctjs.number<" + result +
                     ">\n  %shifted = ctjs.binary_static shr %bound, %count\n"
                     "  %index = ctjs.binary sub %shifted, %expected\n" +
                     indexed,
             .arrays = "a:[zero]",
             .exit = "a -> {a}"});
    }
    for (const std::string bound : {"4746794007248502784",             // 2^31
                                    "4751297606873776128"}) {          // 2^32-1
        for (const std::string count : {"0", "4629418941960159232"}) { // 0, 31
            run({.what = "a signed high-bit input cannot supply a nonnegative array index",
                 .body = values + "  %bound = ctjs.constant #ctjs.number<" + bound +
                         ">\n  %count = ctjs.constant #ctjs.number<" + count +
                         ">\n  %held = ctjs.binary_static ushr %bound, %zero\n"
                         "  %index = ctjs.binary_static shr %held, %count\n" +
                         indexed,
                 .failure = ArrayContentsFailure::UnknownIndex});
        }
    }
    for (const auto & [mask, result] :
         {std::pair{"0", "0"},                                        // 0 -> 0
          std::pair{"9223372036854775808", "0"},                      // -0 -> 0
          std::pair{"4607182418800017408", "4607182418800017408"},    // 1 -> 1
          std::pair{"4746794007244308480", "4746794007244308480"}}) { // 2^31-1 -> 2^31-1
        for (const std::string operands : {"%bound, %mask", "%mask, %bound"}) {
            run({.what = "either bounded Number mask clears the signed high bit exactly",
                 .body = values +
                         "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                         "  %mask = ctjs.constant #ctjs.number<" +
                         mask + ">\n  %expected = ctjs.constant #ctjs.number<" + result +
                         ">\n  %masked = ctjs.binary_static bitand " + operands +
                         "\n  %index = ctjs.binary sub %masked, %expected\n" + indexed,
                 .arrays = "a:[zero]",
                 .exit = "a -> {a}"});
        }
    }
    for (const std::string mask : {"4746794007248502784", "4751297606873776128"}) {
        run({.what = "a mask with a signed high-bit result supplies no nonnegative index",
             .body = values +
                     "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                     "  %mask = ctjs.constant #ctjs.number<" +
                     mask + ">\n  %index = ctjs.binary_static bitand %bound, %mask\n" + indexed,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    for (const std::string kind : {"bitor", "bitxor"}) {
        for (const auto & [mask, difference] :
             {std::pair{"0", "4746794007244308480"},
              std::pair{"9223372036854775808", "4746794007244308480"},
              std::pair{"4607182418800017408", "4746794007240114176"},
              std::pair{"4746794007244308480", "0"}}) {
            for (const std::string operands : {"%bound, %mask", "%mask, %bound"}) {
                run({.what = "bounded OR and XOR preserve exact low bits in either operand order",
                     .body = values +
                             "  %bound = ctjs.constant #ctjs.number<4746794007244308480>\n"
                             "  %mask = ctjs.constant #ctjs.number<" +
                             mask + ">\n  %expected = ctjs.constant #ctjs.number<" +
                             (kind == "bitor" ? "4746794007244308480" : difference) +
                             ">\n  %masked = ctjs.binary_static " + kind + " " + operands +
                             "\n  %index = ctjs.binary sub %masked, %expected\n" + indexed,
                     .arrays = "a:[zero]",
                     .exit = "a -> {a}"});
            }
        }
        for (const std::string mask : {"0", "4746794007244308480"}) {
            run({.what = "OR and XOR with a signed high-bit result supply no nonnegative index",
                 .body = values +
                         "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                         "  %mask = ctjs.constant #ctjs.number<" +
                         mask + ">\n  %index = ctjs.binary_static " + kind + " %bound, %mask\n" +
                         indexed,
                 .failure = ArrayContentsFailure::UnknownIndex});
        }
    }
    for (const auto & [mask, result] :
         {std::pair{"4746794007248502784", "4746794007244308480"}, // 2^31 -> 2^31-1
          std::pair{"4751297606873776128", "0"}}) {                // 2^32-1 -> 0
        for (const std::string operands : {"%bound, %mask", "%mask, %bound"}) {
            run({.what = "XOR cancels matching high bits while retaining exact low bits",
                 .body = values +
                         "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                         "  %mask = ctjs.constant #ctjs.number<" +
                         mask + ">\n  %expected = ctjs.constant #ctjs.number<" + result +
                         ">\n  %masked = ctjs.binary_static bitxor " + operands +
                         "\n  %index = ctjs.binary sub %masked, %expected\n" + indexed,
                 .arrays = "a:[zero]",
                 .exit = "a -> {a}"});
        }
    }
    for (const auto & [count, result] :
         {std::pair{"0", "4611686018427387904"},                   // 0 -> 2
          std::pair{"9223372036854775808", "4611686018427387904"}, // -0 -> 2
          std::pair{"4607182418800017408", "4616189618054758400"}, // 1 -> 4
          std::pair{"4629418941960159232", "0"},                   // 31 -> 0
          std::pair{"4629700416936869888", "4611686018427387904"}, // 32 -> 2
          std::pair{"4629841154425225216", "4616189618054758400"}, // 33 -> 4
          std::pair{"4751297606873776128", "0"}}) {                // 2^32-1 -> 0
        run({.what = "left shift masks its count and truncates overflow to 32 bits",
             .body = values +
                     "  %bound = ctjs.constant #ctjs.number<4611686018427387904>\n"
                     "  %count = ctjs.constant #ctjs.number<" +
                     count + ">\n  %expected = ctjs.constant #ctjs.number<" + result +
                     ">\n  %shifted = ctjs.binary_static shl %bound, %count\n"
                     "  %index = ctjs.binary sub %shifted, %expected\n" +
                     indexed,
             .arrays = "a:[zero]",
             .exit = "a -> {a}"});
    }
    for (const auto & [bound, result] :
         {std::pair{"4746794007248502784", "0"},                      // 2^31 -> 0
          std::pair{"4746794007250599936", "4611686018427387904"}}) { // 2^31+1 -> 2
        run({.what = "left shift clears an original held high bit and preserves lower bits",
             .body = values + one + "  %bound = ctjs.constant #ctjs.number<" + bound +
                     ">\n  %expected = ctjs.constant #ctjs.number<" + result +
                     ">\n  %held = ctjs.binary_static ushr %bound, %zero\n"
                     "  %shifted = ctjs.binary_static shl %held, %one\n"
                     "  %index = ctjs.binary sub %shifted, %expected\n" +
                     indexed,
             .arrays = "a:[zero]",
             .exit = "a -> {a}"});
    }
    for (const auto & [bound, count] :
         {std::pair{"4607182418800017408", "4629418941960159232"},    // 1 << 31
          std::pair{"4611686018427387904", "4629137466983448576"},    // 2 << 30
          std::pair{"4746794007248502784", "0"},                      // 2^31 << 0
          std::pair{"4751297606873776128", "4607182418800017408"}}) { // 2^32-1 << 1
        run({.what = "left shift with a signed high-bit result supplies no nonnegative index",
             .body = values + "  %bound = ctjs.constant #ctjs.number<" + bound +
                     ">\n  %count = ctjs.constant #ctjs.number<" + count +
                     ">\n  %index = ctjs.binary_static shl %bound, %count\n" + indexed,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    for (const std::string kind : {"ushr", "shr", "shl", "bitand", "bitor", "bitxor"}) {
        for (const std::string literal :
             {"#ctjs.number<4602678819172646912>",  // 0.5
              "#ctjs.number<13830554455654793216>", // -1
              "#ctjs.number<4751297606875873280>",  // 2^32
              "#ctjs.number<13974669643730649088>", // -2^32
              "#ctjs.number<9218868437227405312>",  // infinity
              "#ctjs.number<9221120237041090560>",  // NaN
              "#ctjs.string<\"0\">", "#ctjs.bigint<\"0\">", "#ctjs.boolean<false>", "#ctjs.null",
              "#ctjs.undefined"}) {
            for (const std::string operands : {"%input, %zero", "%zero, %input"}) {
                const bool signedZero =
                    literal == "#ctjs.number<13830554455654793216>" &&
                    (kind == "bitand" || ((kind == "shl" || kind == "shr" || kind == "ushr") &&
                                          operands == "%zero, %input"));
                run({.what = "only independently bounded bitwise inputs supply an exact index",
                     .body = values + "  %input = ctjs.constant " + literal +
                             "\n  %index = ctjs.binary_static " + kind + " " + operands + "\n" +
                             indexed,
                     .failure = signedZero ? ArrayContentsFailure::None
                                           : ArrayContentsFailure::UnknownIndex,
                     .arrays = signedZero ? "a:[zero]" : "",
                     .exit = signedZero ? "a -> {a}" : ""});
            }
        }
        for (const std::string operands : {"%input, %zero", "%zero, %input"}) {
            run({.what = "a bitwise arm cannot borrow another arm's Number",
                 .body = values + read +
                         "  %flag = ctjs.truthy %zero\n"
                         "  cf.cond_br %flag, ^join(%zero : !ctjs.value), ^join(%p : !ctjs.value)\n"
                         "^join(%input: !ctjs.value):\n"
                         "  %index = ctjs.binary_static " +
                         kind + " " + operands + "\n" + indexed,
                 .failure = ArrayContentsFailure::UnknownValue});
        }
    }
    run({.what = "an original bounded Number subtraction supplies its exact computed offset",
         .body = values + one + read +
                 "  %offset = ctjs.binary sub %one, %zero\n"
                 "  %index = ctjs.binary sub %length, %offset\n" +
                 indexed,
         .arrays = "a:[zero]",
         .exit = "a -> {a}"});
    for (const std::string literal : {"#ctjs.number<0>",
                                      "#ctjs.number<9223372036854775808>",    // -0
                                      "#ctjs.number<4751297606873776128>"}) { // 2^32-1
        run({.what = "original bounded Number subtraction preserves both endpoint values",
             .body = values + "  %bound = ctjs.constant " + literal +
                     "\n  %index = ctjs.binary sub %bound, %bound\n" + indexed,
             .arrays = "a:[zero]",
             .exit = "a -> {a}"});
    }
    run({.what = "the maximum array length remains outside own element indices",
         .body = values +
                 "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                 "  %index = ctjs.binary sub %bound, %zero\n" +
                 indexed,
         .failure = ArrayContentsFailure::UnknownIndex});
    run({.what = "a loaded original Number keeps its value after replacement and transport",
         .body = values + one +
                 "  %inputs = ctjs.create_array [%one] {storage_test_id = \"inputs\"}\n"
                 "  %saved = ctjs.get_property %inputs[%zero]\n"
                 "  ctjs.set_property %inputs[%zero], %x\n"
                 "  cf.br ^pair(%saved, %zero : !ctjs.value, !ctjs.value)\n"
                 "^pair(%before: !ctjs.value, %after: !ctjs.value):\n"
                 "  cf.br ^swapped(%after, %before : !ctjs.value, !ctjs.value)\n"
                 "^swapped(%newInput: !ctjs.value, %oldInput: !ctjs.value):\n"
                 "  %index = ctjs.binary sub %oldInput, %one\n"
                 "  ctjs.set_property %a[%index], %newInput\n  ctjs.return %a\n",
         .arrays = "a:[zero]; inputs:[x]",
         .reads = "inputs[0]=ctjs.constant",
         .exit = "a -> {a}"});
    run({.what = "a literal Number subtraction cannot release a returned saved child",
         .body = values + one +
                 "  %index = ctjs.binary sub %one, %one\n"
                 "  %saved = ctjs.get_property %a[%index]\n" +
                 overwrite + "  ctjs.return %saved\n",
         .arrays = "a:[zero]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        "");
    for (const std::string operation : {"ctjs.binary", "ctjs.binary_static"}) {
        run({.what = "a held bounded Number sum supplies its exact subtraction offset",
             .body = values + one + read + "  %offset = " + operation +
                     " add %one, %zero\n  %index = ctjs.binary sub %length, %offset\n" + indexed,
             .arrays = "a:[zero]",
             .exit = "a -> {a}"});
    }
    run({.what = "a saved own length offset survives append, slot replacement and transport",
         .body = values + read +
                 "  %offsets = ctjs.create_array [%zero] {storage_test_id = \"offsets\"}\n"
                 "  %offset = ctjs.get_property %offsets[%key] {storage_test_id = \"offset\"}\n"
                 "  %snapshots = ctjs.create_array [%offset] {storage_test_id = \"snapshots\"}\n"
                 "  %saved = ctjs.get_property %snapshots[%zero]\n"
                 "  ctjs.set_property %snapshots[%zero], %x\n"
                 "  ctjs.append %zero to %offsets\n"
                 "  cf.br ^next(%saved : !ctjs.value)\n^next(%before: !ctjs.value):\n"
                 "  %index = ctjs.binary sub %length, %before\n" +
                 indexed,
         .arrays = "a:[zero]; offsets:[zero,zero]; snapshots:[x]",
         .reads = "snapshots[0]=offset",
         .exit = "a -> {a}"});
    run({.what = "a held offset read retains the saved child after overwrite",
         .body = values + read +
                 "  %index = ctjs.binary sub %length, %length\n"
                 "  %saved = ctjs.get_property %a[%index]\n" +
                 overwrite + "  ctjs.return %saved\n",
         .arrays = "a:[zero]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        "");
    run({.what = "distinct held offsets retain every structural overwrite alternative",
         .body = values + read +
                 "  ctjs.append %zero to %a\n"
                 "  %later = ctjs.get_property %a[%key]\n"
                 "  %flag = ctjs.truthy %zero\n"
                 "  cf.cond_br %flag, ^join(%length : !ctjs.value), ^join(%later : !ctjs.value)\n"
                 "^join(%offset: !ctjs.value):\n"
                 "  %index = ctjs.binary sub %later, %offset\n" +
                 indexed,
         .arrays = "a:[x,zero] | a:[zero,zero]",
         .exit = "a -> {a,x}; a -> {a}"},
        "");
    run({.what = "a later held offset cannot underflow a saved earlier length",
         .body = values + read +
                 "  ctjs.append %zero to %a\n"
                 "  %later = ctjs.get_property %a[%key]\n"
                 "  %index = ctjs.binary sub %length, %later\n" +
                 indexed,
         .failure = ArrayContentsFailure::UnknownIndex});
    run({.what = "an opaque structural offset cannot borrow the other arm's held Number",
         .body = values + read +
                 "  %flag = ctjs.truthy %zero\n"
                 "  cf.cond_br %flag, ^join(%length : !ctjs.value), ^join(%p : !ctjs.value)\n"
                 "^join(%offset: !ctjs.value):\n"
                 "  %index = ctjs.binary sub %length, %offset\n" +
                 indexed,
         .failure = ArrayContentsFailure::UnsupportedOperation});
    run({.what = "an opaque subtraction operand cannot gain authority from a known length",
         .body = values + read + "  %index = ctjs.binary sub %length, %p\n" + indexed,
         .failure = ArrayContentsFailure::UnsupportedOperation});
    run({.what = "a zero length cannot underflow into an array index",
         .body = values + one +
                 "  %empty = ctjs.create_array []\n"
                 "  %length = ctjs.get_property %empty[%key]\n" +
                 subtract + indexed,
         .failure = ArrayContentsFailure::UnknownIndex});
    run({.what = "a literal Number and own length each prove their structural subtraction arm",
         .body = values + one + read +
                 "  %flag = ctjs.truthy %zero\n"
                 "  cf.cond_br %flag, ^join(%length : !ctjs.value), ^join(%one : !ctjs.value)\n"
                 "^join(%before: !ctjs.value):\n"
                 "  %index = ctjs.binary sub %before, %one\n" +
                 indexed,
         .arrays = "a:[zero] | a:[zero]",
         .exit = "a -> {a}; a -> {a}"});
    for (const std::string alternative : {"%zero", "%key", "%p", "%x"}) {
        run({.what = "one length-valued edge cannot authorize another edge's category or value",
             .body = values + one + read +
                     "  %flag = ctjs.truthy %zero\n"
                     "  cf.cond_br %flag, ^join(%length : !ctjs.value), ^join(" +
                     alternative +
                     " : !ctjs.value)\n^join(%before: !ctjs.value):\n"
                     "  %index = ctjs.binary sub %before, %one\n" +
                     indexed,
             .failure = alternative == "%p" || alternative == "%x"
                            ? ArrayContentsFailure::UnsupportedOperation
                            : ArrayContentsFailure::UnknownIndex});
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
    const contents_row computedShrink{
        .what = "a held length subtraction releases exactly its removed child",
        .body = values + one + read + subtract +
                "  ctjs.set_property %a[%key], %index\n"
                "  %result = ctjs.create_array [%a, %length] {storage_test_id = \"result\"}\n"
                "  ctjs.return %result\n",
        .arrays = "a:[]; result:[a,length]",
        .exit = "result -> {a,result}"};
    run(computedShrink);
    const contents_row literalShrink{
        .what = "original bounded Number subtraction supplies a non-growing length",
        .body = values + one + read +
                "  %input = ctjs.constant #ctjs.number<4607182418800017408>\n"
                "  %index = ctjs.binary sub %input, %one\n"
                "  ctjs.set_property %a[%key], %index\n"
                "  %result = ctjs.create_array [%a, %length] {storage_test_id = \"result\"}\n"
                "  ctjs.return %result\n",
        .arrays = "a:[]; result:[a,length]",
        .exit = "result -> {a,result}"};
    run(literalShrink);
    const contents_row productShrink{
        .what = "a signed zero product supplies a non-growing length and keeps its origin",
        .body = values + one + read +
                "  %negativeZero = ctjs.constant #ctjs.number<9223372036854775808>\n"
                "  %index = ctjs.binary mul %one, %negativeZero {storage_test_id = \"index\"}\n"
                "  ctjs.set_property %a[%key], %index\n"
                "  %result = ctjs.create_array [%a, %index] {storage_test_id = \"result\"}\n"
                "  ctjs.return %result\n",
        .arrays = "a:[]; result:[a,index]",
        .exit = "result -> {a,result}"};
    run(productShrink);
    const contents_row quotientShrink{
        .what = "a signed zero quotient supplies a non-growing length and keeps its origin",
        .body = values + one + read +
                "  %negativeZero = ctjs.constant #ctjs.number<9223372036854775808>\n"
                "  %index = ctjs.binary div %negativeZero, %one {storage_test_id = \"index\"}\n"
                "  ctjs.set_property %a[%key], %index\n"
                "  %result = ctjs.create_array [%a, %index] {storage_test_id = \"result\"}\n"
                "  ctjs.return %result\n",
        .arrays = "a:[]; result:[a,index]",
        .exit = "result -> {a,result}"};
    run(quotientShrink);
    contents_row powerShrink = quotientShrink;
    powerShrink.what = "power one preserves signed zero length and original result identity";
    powerShrink.body.replace(powerShrink.body.find("binary div"), 10, "binary pow");
    run(powerShrink);
    for (const std::string exponent :
         {"4611686018427387904", "4613937818241073152", "4751297606873776128"}) {
        auto source = powerShrink.body;
        source.replace(source.find("  %index ="), 0,
                       "  %exponent = ctjs.constant #ctjs.number<" + exponent + ">\n");
        source.replace(source.find("pow %negativeZero, %one"), 23, "pow %negativeZero, %exponent");
        run({.what = "even and odd bounded zero powers preserve the original signed length result",
             .body = source,
             .arrays = "a:[]; result:[a,index]",
             .exit = "result -> {a,result}"});
    }

    const contents_row remainderShrink{
        .what = "a signed zero remainder supplies a non-growing length and keeps its origin",
        .body = values + one + read +
                "  %negativeZero = ctjs.constant #ctjs.number<9223372036854775808>\n"
                "  %index = ctjs.binary mod %negativeZero, %one {storage_test_id = \"index\"}\n"
                "  ctjs.set_property %a[%key], %index\n"
                "  %result = ctjs.create_array [%a, %index] {storage_test_id = \"result\"}\n"
                "  ctjs.return %result\n",
        .arrays = "a:[]; result:[a,index]",
        .exit = "result -> {a,result}"};
    run(remainderShrink);
    const contents_row shiftShrink{
        .what = "unsigned shift supplies a non-growing length and keeps the original result",
        .body = values + one + read +
                "  %negativeZero = ctjs.constant #ctjs.number<9223372036854775808>\n"
                "  %index = ctjs.binary_static ushr %negativeZero, %one "
                "{storage_test_id = \"index\"}\n"
                "  ctjs.set_property %a[%key], %index\n"
                "  %result = ctjs.create_array [%a, %index] {storage_test_id = \"result\"}\n"
                "  ctjs.return %result\n",
        .arrays = "a:[]; result:[a,index]",
        .exit = "result -> {a,result}"};
    run(shiftShrink);
    contents_row signedShiftShrink = shiftShrink;
    signedShiftShrink.what = "signed shift supplies a non-growing length and keeps its origin";
    signedShiftShrink.body.replace(signedShiftShrink.body.find("ushr"), 4, "shr");
    run(signedShiftShrink);
    contents_row leftShiftShrink = shiftShrink;
    leftShiftShrink.what = "left shift supplies a non-growing length and keeps its origin";
    leftShiftShrink.body.replace(leftShiftShrink.body.find("ushr"), 4, "shl");
    run(leftShiftShrink);
    contents_row maskShrink = shiftShrink;
    maskShrink.what = "a bounded mask supplies a non-growing length and keeps its origin";
    maskShrink.body.replace(maskShrink.body.find("ushr"), 4, "bitand");
    run(maskShrink);
    contents_row orShrink = maskShrink;
    orShrink.what = "a zero OR supplies a non-growing length and keeps its origin";
    orShrink.body.replace(orShrink.body.find("bitand %negativeZero, %one"), 26,
                          "bitor %negativeZero, %zero");
    run(orShrink);
    contents_row xorShrink = orShrink;
    xorShrink.what = "a zero XOR supplies a non-growing length and keeps its origin";
    xorShrink.body.replace(xorShrink.body.find("bitor"), 5, "bitxor");
    run(xorShrink);
    const contents_row heldOffsetShrink{
        .what = "a held bounded Number offset supplies an exact non-growing shrink",
        .body = values + one + read +
                "  %offset = ctjs.binary add %one, %zero\n"
                "  %index = ctjs.binary sub %length, %offset\n"
                "  ctjs.set_property %a[%key], %index\n"
                "  %result = ctjs.create_array [%a, %length] {storage_test_id = \"result\"}\n"
                "  ctjs.return %result\n",
        .arrays = "a:[]; result:[a,length]",
        .exit = "result -> {a,result}"};
    run(heldOffsetShrink);
    const contents_row unaryShrink{
        .what = "unary Plus preserves an original bounded Number shrink target",
        .body = values + read +
                "  %wanted = ctjs.unary plus %zero\n"
                "  ctjs.set_property %a[%key], %wanted\n"
                "  %result = ctjs.create_array [%a, %length] {storage_test_id = \"result\"}\n"
                "  ctjs.return %result\n",
        .arrays = "a:[]; result:[a,length]",
        .exit = "result -> {a,result}"};
    run(unaryShrink);
    const contents_row negatedShrink{
        .what = "unary Neg keeps zero length evidence and the original signed value",
        .body = values + read +
                "  %wanted = ctjs.unary neg %zero {storage_test_id = \"wanted\"}\n"
                "  ctjs.set_property %a[%key], %wanted\n"
                "  %result = ctjs.create_array [%a, %wanted] {storage_test_id = \"result\"}\n"
                "  ctjs.return %result\n",
        .arrays = "a:[]; result:[a,wanted]",
        .exit = "result -> {a,result}"};
    run(negatedShrink);
    run({.what = "unary Neg keeps a held zero after replacement and successor transport",
         .body = values + one + read + subtract +
                 "  %targets = ctjs.create_array [%index] {storage_test_id = \"targets\"}\n"
                 "  %saved = ctjs.get_property %targets[%zero]\n"
                 "  ctjs.set_property %targets[%zero], %x\n"
                 "  cf.br ^next(%saved : !ctjs.value)\n^next(%before: !ctjs.value):\n"
                 "  %wanted = ctjs.unary neg %before\n"
                 "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
         .arrays = "a:[]; targets:[x]",
         .reads = "targets[0]=index",
         .exit = "a -> {a}"});
    run({.what = "a negated saved empty length supplies an index after append",
         .body = values + "  %empty = ctjs.create_array [] {storage_test_id = \"empty\"}\n"
                          "  %length = ctjs.get_property %empty[%key]\n"
                          "  %index = ctjs.unary neg %length\n"
                          "  ctjs.append %x to %empty\n"
                          "  %saved = ctjs.get_property %empty[%index]\n"
                          "  ctjs.set_property %empty[%index], %zero\n  ctjs.return %saved\n",
         .arrays = "a:[x]; empty:[zero]",
         .reads = "empty[0]=x",
         .exit = "x -> {x}"},
        "");
    run({.what = "a held negated zero remains an exact subtraction offset",
         .body = values + read +
                 "  %offset = ctjs.unary neg %zero {storage_test_id = \"offset\"}\n"
                 "  %offsets = ctjs.create_array [%offset] {storage_test_id = \"offsets\"}\n"
                 "  %saved = ctjs.get_property %offsets[%zero]\n"
                 "  ctjs.set_property %offsets[%zero], %x\n"
                 "  ctjs.append %zero to %a\n"
                 "  %index = ctjs.binary sub %length, %saved\n"
                 "  %loaded = ctjs.get_property %a[%index]\n  ctjs.return %loaded\n",
         .arrays = "a:[x,zero]; offsets:[x]",
         .reads = "offsets[0]=offset; a[1]=zero",
         .exit = "zero -> {}"});
    for (const std::string literal : {"#ctjs.number<0>", "#ctjs.number<9223372036854775808>"}) {
        run({.what = "negating either zero sign twice retains an exact own index",
             .body = values + "  %input = ctjs.constant " + literal +
                     "\n  %negated = ctjs.unary neg %input\n"
                     "  %index = ctjs.unary neg %negated\n" +
                     indexed,
             .arrays = "a:[zero]",
             .exit = "a -> {a}"});
    }
    for (const std::string alternative : {"%length", "%p", "%key"}) {
        run({.what = "negated zero cannot authorize another structural arm's value or category",
             .body = values + read +
                     "  %flag = ctjs.truthy %zero\n"
                     "  cf.cond_br %flag, ^join(%zero : !ctjs.value), ^join(" +
                     alternative +
                     " : !ctjs.value)\n^join(%input: !ctjs.value):\n"
                     "  %wanted = ctjs.unary neg %input\n"
                     "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
             .failure = alternative == "%p" ? ArrayContentsFailure::UnsupportedOperation
                                            : ArrayContentsFailure::UnknownIndex});
    }
    run({.what = "negated zero still requires a present dense element",
         .body = privateValues + "  %index = ctjs.unary neg %zero\n"
                                 "  %saved = ctjs.get_property %a[%index]\n"
                                 "  ctjs.return %saved\n",
         .failure = ArrayContentsFailure::MissingElement});
    run({.what = "unary Plus keeps a loaded computed Number after replacement and transport",
         .body = values + one + read + subtract +
                 "  %targets = ctjs.create_array [%index] {storage_test_id = \"targets\"}\n"
                 "  %saved = ctjs.get_property %targets[%zero]\n"
                 "  ctjs.set_property %targets[%zero], %x\n"
                 "  cf.br ^next(%saved : !ctjs.value)\n^next(%before: !ctjs.value):\n"
                 "  %wanted = ctjs.unary plus %before\n"
                 "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
         .arrays = "a:[]; targets:[x]",
         .reads = "targets[0]=index",
         .exit = "a -> {a}"});
    run({.what = "unary Plus keeps the saved length before an append for an exact element read",
         .body = values + read +
                 "  %index = ctjs.unary plus %length\n"
                 "  ctjs.append %zero to %a\n"
                 "  %saved = ctjs.get_property %a[%index]\n  ctjs.return %saved\n",
         .arrays = "a:[x,zero]",
         .reads = "a[1]=zero",
         .exit = "zero -> {}"});
    run({.what = "unary Plus preserves each structural alternative's exact target",
         .body = values + one + read + subtract +
                 "  %flag = ctjs.truthy %zero\n"
                 "  cf.cond_br %flag, ^join(%index : !ctjs.value), ^join(%length : !ctjs.value)\n"
                 "^join(%before: !ctjs.value):\n"
                 "  %wanted = ctjs.unary plus %before\n"
                 "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
         .arrays = "a:[] | a:[x]",
         .exit = "a -> {a}; a -> {a,x}"},
        "");
    run({.what = "unary Plus cannot borrow a bounded Number from another structural arm",
         .body = values + read +
                 "  %flag = ctjs.truthy %zero\n"
                 "  cf.cond_br %flag, ^join(%length : !ctjs.value), ^join(%p : !ctjs.value)\n"
                 "^join(%before: !ctjs.value):\n"
                 "  %wanted = ctjs.unary plus %before\n"
                 "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
         .failure = ArrayContentsFailure::UnsupportedOperation});
    run({.what = "unary Plus preserves negative zero as an empty dense length",
         .body = values + "  %negative = ctjs.constant #ctjs.number<9223372036854775808>\n"
                          "  %wanted = ctjs.unary plus %negative\n"
                          "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
         .arrays = "a:[]",
         .exit = "a -> {a}"});
    run({.what = "a canonical String offset produces an exact Number shrink target",
         .body = values + "  %one = ctjs.constant #ctjs.string<\"1\">\n" + read + subtract +
                 "  ctjs.set_property %a[%key], %index\n  ctjs.return %a\n",
         .arrays = "a:[]",
         .exit = "a -> {a}"});
    run({.what = "a computed nonzero shrink preserves the complete unremoved prefix",
         .body = values + one + "  ctjs.append %zero to %a\n" + read + subtract +
                 "  ctjs.set_property %a[%key], %index\n  ctjs.return %a\n",
         .arrays = "a:[x]",
         .exit = "a -> {a,x}"},
        "");
    run({.what = "a returned saved child remains retained after computed shrink",
         .body = values + one + read + subtract +
                 "  %saved = ctjs.get_property %a[%zero]\n"
                 "  ctjs.set_property %a[%key], %index\n  ctjs.return %saved\n",
         .arrays = "a:[]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        "");
    run({.what = "a held own length preserves its original dense contents",
         .body = values + one + read + subtract +
                 "  ctjs.set_property %a[%key], %length\n  ctjs.return %a\n",
         .arrays = "a:[x]",
         .exit = "a -> {a,x}"},
        "");
    for (const std::string operation : {"ctjs.binary", "ctjs.binary_static"}) {
        run({.what = "bounded original Number addition supplies an exact shrink target",
             .body = values + "  %wanted = " + operation +
                     " add %zero, %zero\n"
                     "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
             .arrays = "a:[]",
             .exit = "a -> {a}"});
    }
    run({.what = "a saved computed target survives replacement and successor transport",
         .body = values + one + read + subtract +
                 "  %targets = ctjs.create_array [%index] {storage_test_id = \"targets\"}\n"
                 "  %saved = ctjs.get_property %targets[%zero]\n"
                 "  ctjs.set_property %targets[%zero], %x\n"
                 "  cf.br ^next(%saved : !ctjs.value)\n^next(%wanted: !ctjs.value):\n"
                 "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
         .arrays = "a:[]; targets:[x]",
         .reads = "targets[0]=index",
         .exit = "a -> {a}"});
    run({.what = "a replaced computed target cannot lend its former exact Number",
         .body = values + one + read + subtract +
                 "  %targets = ctjs.create_array [%index]\n"
                 "  ctjs.set_property %targets[%zero], %x\n"
                 "  %wanted = ctjs.get_property %targets[%zero]\n"
                 "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
         .failure = ArrayContentsFailure::UnknownIndex});
    run({.what = "a saved length cannot regrow an array after a computed shrink",
         .body = values + one + read + subtract +
                 "  ctjs.set_property %a[%key], %index\n"
                 "  ctjs.set_property %a[%key], %length\n  ctjs.return %a\n",
         .failure = ArrayContentsFailure::MissingElement});
    run({.what = "a computed increasing length remains outside dense truncation",
         .body = values + one + read +
                 "  %wanted = ctjs.binary add %length, %one\n"
                 "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
         .failure = ArrayContentsFailure::MissingElement});
    run({.what = "distinct computed branch targets preserve every structural result",
         .body = values + one + read + subtract +
                 "  %flag = ctjs.truthy %zero\n"
                 "  cf.cond_br %flag, ^join(%index : !ctjs.value), ^join(%length : !ctjs.value)\n"
                 "^join(%wanted: !ctjs.value):\n"
                 "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
         .arrays = "a:[] | a:[x]",
         .exit = "a -> {a}; a -> {a,x}"},
        "");
    run({.what = "an opaque alternative cannot borrow another path's computed length",
         .body = values + one + read + subtract +
                 "  %flag = ctjs.truthy %zero\n"
                 "  cf.cond_br %flag, ^join(%index : !ctjs.value), ^join(%p : !ctjs.value)\n"
                 "^join(%wanted: !ctjs.value):\n"
                 "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
         .failure = ArrayContentsFailure::UnknownIndex});
    for (const std::string literal : {"#ctjs.number<4602678819172646912>",  // 0.5
                                      "#ctjs.number<13830554455654793216>", // -1
                                      "#ctjs.number<4751297606875873280>",  // 2^32
                                      "#ctjs.number<9218868437227405312>",  // infinity
                                      "#ctjs.number<9221120237041090560>",  // NaN
                                      "#ctjs.string<\"0\">", "#ctjs.bigint<\"0\">",
                                      "#ctjs.boolean<false>", "#ctjs.null", "#ctjs.undefined"}) {
        for (const std::string producer :
             {"ctjs.binary add %input, %zero", "ctjs.binary sub %input, %zero",
              "ctjs.binary mul %input, %zero", "ctjs.binary div %input, %input",
              "ctjs.binary mod %input, %input", "ctjs.binary_static ushr %input, %input",
              "ctjs.unary plus %input", "ctjs.unary neg %input"}) {
            const auto body = values + "  %input = ctjs.constant " + literal +
                              "\n  %wanted = " + producer +
                              "\n  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n";
            if (literal == "#ctjs.number<13830554455654793216>" &&
                (producer == "ctjs.binary mul %input, %zero" ||
                 producer == "ctjs.binary div %input, %input" ||
                 producer == "ctjs.binary mod %input, %input" ||
                 producer == "ctjs.binary_static ushr %input, %input" ||
                 producer == "ctjs.unary neg %input")) {
                const bool retained = producer == "ctjs.binary div %input, %input" ||
                                      producer == "ctjs.binary_static ushr %input, %input" ||
                                      producer == "ctjs.unary neg %input";
                run({.what = "signed Number arithmetic preserves the exact zero or unit length",
                     .body = body,
                     .arrays = retained ? "a:[x]" : "a:[]",
                     .exit = retained ? "a -> {a,x}" : "a -> {a}"},
                    retained ? "" : "x");
                continue;
            }
            if (literal == "#ctjs.string<\"0\">" && producer == "ctjs.binary sub %input, %zero") {
                run({.what = "canonical left String subtraction supplies an exact empty length",
                     .body = body,
                     .arrays = "a:[]",
                     .exit = "a -> {a}"});
                continue;
            }
            run({.what = "a computed shrink needs exact Number operands without coercion",
                 .body = body,
                 .failure = ArrayContentsFailure::UnknownIndex});
        }
    }
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
    run({.what = "unfolded source negative zero clears length without changing saved values",
         .body = values + read +
                 "  %wanted = ctjs.unary neg %zero\n"
                 "  ctjs.set_property %a[%key], %wanted\n" +
                 done,
         .arrays = "a:[]",
         .exit = "length -> {}"});
    for (const std::string literal : {"#ctjs.string<\"0\">", "#ctjs.bigint<\"0\">"}) {
        run({.what = "negated coercive zero cannot borrow Number length authority",
             .body = values + "  %input = ctjs.constant " + literal +
                     "\n  %wanted = ctjs.unary neg %input\n" + read +
                     "  ctjs.set_property %a[%key], %wanted\n" + done,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
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
    for (const std::string wanted : {"%p", "%x"}) {
        run({.what = "opaque length targets gain no truncation authority",
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
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "1"));
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "01"));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 4751297606873776128ULL));
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 4751297606875873280ULL));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(offset);
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
    for (const contents_row & source :
         {originalShrink, computedShrink, literalShrink, productShrink, quotientShrink,
          remainderShrink, shiftShrink, signedShiftShrink, leftShiftShrink, maskShrink, orShrink,
          xorShrink, heldOffsetShrink, unaryShrink, negatedShrink}) {
        auto module = parse(source);
        if (!module) {
            fail(row{.what = source.what, .body = source.body, .expected = ""},
                 "the live length-write fixture did not parse");
            continue;
        }
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        module->walk([&](ctjs::SetPropertyOp op) { store = op; });
        const mlir::Value target = store.getObject();
        const mlir::Value key = store.getKey();
        const mlir::Value value = store.getValue();
        auto binary = value.getDefiningOp<ctjs::BinaryOp>();
        const auto binaryKind = binary ? binary.getKindAttr() : ctjs::BinaryKindAttr{};
        auto shift = value.getDefiningOp<ctjs::BinaryStaticOp>();
        const auto shiftKind = shift ? shift.getKindAttr() : ctjs::BinaryKindAttr{};
        auto unary = value.getDefiningOp<ctjs::UnaryOp>();
        const auto unaryKind = unary ? unary.getKindAttr() : ctjs::UnaryKindAttr{};
        auto literal = (shift    ? shift.getRhs()
                        : binary ? binary.getRhs()
                        : unary  ? unary.getOperand()
                                 : value)
                           .getDefiningOp<ctjs::ConstantOp>();
        if (binary) {
            if (auto offset = binary.getRhs().getDefiningOp<ctjs::BinaryOp>()) {
                literal = offset.getLhs().getDefiningOp<ctjs::ConstantOp>();
            }
        }
        const mlir::Attribute original = literal.getValue();
        mlir::OpBuilder builder(function);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        store->setAttr("ctnative.array_length", builder.getI64IntegerAttr(0));
        if (binary) { binary->setAttr("ctnative.array_length", builder.getI64IntegerAttr(0)); }
        if (shift) { shift->setAttr("ctnative.array_length", builder.getI64IntegerAttr(0)); }
        if (unary) { unary->setAttr("ctnative.array_length", builder.getI64IntegerAttr(0)); }
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*module))) {
            fail(row{.what = source.what, .body = source.body, .expected = ""},
                 "the length-write stale solver did not converge");
        }
        const auto inspect = [&](ArrayContentsFailure failure, bool retained = false) {
            contents_row current = source;
            current.failure = failure;
            if (retained) {
                current.arrays = shift   ? "a:[x]; result:[a,index]"
                                 : unary ? "a:[x]; result:[a,wanted]"
                                         : "a:[x]; result:[a,length]";
                current.exit = "result -> {a,result,x}";
            }
            check(*module, current, retained ? "" : "x");
            const bool complete = failure == ArrayContentsFailure::None;
            const auto verdicts = computeVerdicts(stale, function);
            if (verdicts.arrayRetentionComplete != complete ||
                verdicts.confinedStoredSites != (complete && !retained ? 1U : 0U)) {
                fail(row{.what = current.what, .body = current.body, .expected = ""},
                     "stale solver or forged marker supplied length-write authority");
            }
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 4611686018427387904ULL));
        inspect((binary && binary.getKind() == ctjs::BinaryKind::Sub) ||
                        (unary && unary.getKind() == ctjs::UnaryKind::Neg)
                    ? ArrayContentsFailure::UnknownIndex
                : (shift && shiftKind.getValue() != ctjs::BinaryKind::BitOr &&
                   shiftKind.getValue() != ctjs::BinaryKind::BitXor) ||
                        (binary && (binary.getKind() == ctjs::BinaryKind::Div ||
                                    binary.getKind() == ctjs::BinaryKind::Mod))
                    ? ArrayContentsFailure::None
                    : ArrayContentsFailure::MissingElement);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 4602678819172646912ULL));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 13830554455654793216ULL));
        // A negative Sub offset, including saved Add, proves growth, never holes or release.
        // A nonzero negative divisor keeps these zero results exact.
        // A signed mask or shift count also keeps the original zero result.
        // Negating the original -1 retains the full unit-length array.
        const bool negatedUnit = unary && unary.getKind() == ctjs::UnaryKind::Neg;
        inspect(binary && binary.getKind() == ctjs::BinaryKind::Sub
                    ? ArrayContentsFailure::MissingElement
                : negatedUnit ||
                        (binary && (binary.getKind() == ctjs::BinaryKind::Div ||
                                    binary.getKind() == ctjs::BinaryKind::Mod)) ||
                        (shift && (shiftKind.getValue() == ctjs::BinaryKind::Shl ||
                                   shiftKind.getValue() == ctjs::BinaryKind::Shr ||
                                   shiftKind.getValue() == ctjs::BinaryKind::UShr ||
                                   shiftKind.getValue() == ctjs::BinaryKind::BitAnd))
                    ? ArrayContentsFailure::None
                    : ArrayContentsFailure::UnknownIndex,
                negatedUnit);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 9221120237041090560ULL));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::StringAttr::get(&context, binary ? "0.5" : "0"));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::BigIntAttr::get(&context, "0"));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(original);
        inspect(ArrayContentsFailure::None);
        const mlir::Value parameter = function.getBody().front().getArgument(3);
        if (shift) {
            const mlir::Value lhs = shift.getLhs();
            shift->setOperand(0, parameter);
            inspect(ArrayContentsFailure::UnknownValue);
            shift->setOperand(0, lhs);
            shift.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Add));
            inspect(ArrayContentsFailure::None,
                    shiftKind.getValue() != ctjs::BinaryKind::BitOr &&
                        shiftKind.getValue() != ctjs::BinaryKind::BitXor);
            shift.setKindAttr(shiftKind);
            inspect(ArrayContentsFailure::None);
            if (shiftKind.getValue() == ctjs::BinaryKind::Shl) {
                auto input = lhs.getDefiningOp<ctjs::ConstantOp>();
                const mlir::Attribute originalInput = input.getValue();
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4607182418800017408ULL));
                inspect(ArrayContentsFailure::MissingElement);
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 4629418941960159232ULL));
                inspect(ArrayContentsFailure::UnknownIndex);
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4611686018427387904ULL));
                inspect(ArrayContentsFailure::None);
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 4629700416936869888ULL));
                inspect(ArrayContentsFailure::MissingElement);
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4746794007248502784ULL));
                inspect(ArrayContentsFailure::UnknownIndex);
                literal.setValueAttr(original);
                inspect(ArrayContentsFailure::None);
                input.setValueAttr(originalInput);
                inspect(ArrayContentsFailure::None);
            }
            if (shiftKind.getValue() == ctjs::BinaryKind::Shr) {
                auto input = lhs.getDefiningOp<ctjs::ConstantOp>();
                const mlir::Attribute originalInput = input.getValue();
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4746794007244308480ULL));
                inspect(ArrayContentsFailure::MissingElement);
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 4629418941960159232ULL));
                inspect(ArrayContentsFailure::None);
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4746794007248502784ULL));
                inspect(ArrayContentsFailure::UnknownIndex);
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4751297606873776128ULL));
                inspect(ArrayContentsFailure::UnknownIndex);
                input.setValueAttr(originalInput);
                literal.setValueAttr(original);
                inspect(ArrayContentsFailure::None);
            }
            if (shiftKind.getValue() == ctjs::BinaryKind::BitAnd) {
                auto input = lhs.getDefiningOp<ctjs::ConstantOp>();
                const mlir::Attribute originalInput = input.getValue();
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4746794007248502784ULL));
                inspect(ArrayContentsFailure::None);
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 4746794007248502784ULL));
                inspect(ArrayContentsFailure::UnknownIndex);
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 4751297606873776128ULL));
                inspect(ArrayContentsFailure::UnknownIndex);
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4751297606873776128ULL));
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 4746794007244308480ULL));
                inspect(ArrayContentsFailure::MissingElement);
                input.setValueAttr(originalInput);
                literal.setValueAttr(original);
                inspect(ArrayContentsFailure::None);
            }
            if (shiftKind.getValue() == ctjs::BinaryKind::BitOr ||
                shiftKind.getValue() == ctjs::BinaryKind::BitXor) {
                const bool isXor = shiftKind.getValue() == ctjs::BinaryKind::BitXor;
                auto input = lhs.getDefiningOp<ctjs::ConstantOp>();
                const mlir::Attribute originalInput = input.getValue();
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4746794007248502784ULL));
                inspect(ArrayContentsFailure::UnknownIndex);
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 4746794007248502784ULL));
                inspect(isXor ? ArrayContentsFailure::None : ArrayContentsFailure::UnknownIndex);
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 4751297606873776128ULL));
                inspect(isXor ? ArrayContentsFailure::MissingElement
                              : ArrayContentsFailure::UnknownIndex);
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4751297606873776128ULL));
                inspect(isXor ? ArrayContentsFailure::None : ArrayContentsFailure::UnknownIndex);
                input.setValueAttr(originalInput);
                literal.setValueAttr(original);
                inspect(ArrayContentsFailure::None);
            }
        }
        if (binary) {
            const mlir::Value lhs = binary.getLhs();
            binary->setOperand(0, parameter);
            inspect(ArrayContentsFailure::UnsupportedOperation);
            binary->setOperand(0, lhs);
            if (binaryKind.getValue() == ctjs::BinaryKind::Div ||
                binaryKind.getValue() == ctjs::BinaryKind::Mod) {
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 0));
                inspect(ArrayContentsFailure::UnknownIndex);
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 9223372036854775808ULL));
                inspect(ArrayContentsFailure::UnknownIndex);
                literal.setValueAttr(original);
                inspect(ArrayContentsFailure::None);
            }
            binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Mul));
            inspect(ArrayContentsFailure::None, binaryKind.getValue() == ctjs::BinaryKind::Sub);
            binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Div));
            inspect(binaryKind.getValue() == ctjs::BinaryKind::Mul
                        ? ArrayContentsFailure::UnknownIndex
                        : ArrayContentsFailure::None,
                    binaryKind.getValue() == ctjs::BinaryKind::Sub);
            binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Mod));
            inspect(binaryKind.getValue() == ctjs::BinaryKind::Mul
                        ? ArrayContentsFailure::UnknownIndex
                        : ArrayContentsFailure::None);
            binary.setKindAttr(binaryKind);
            inspect(ArrayContentsFailure::None);
        }
        if (unary) {
            const mlir::Value operand = unary.getOperand();
            unary->setOperand(0, parameter);
            inspect(ArrayContentsFailure::UnsupportedOperation);
            unary->setOperand(0, operand);
            unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Not));
            inspect(ArrayContentsFailure::UnknownIndex);
            unary.setKindAttr(unaryKind);
            inspect(ArrayContentsFailure::None);
        }
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
    }
    std::printf("dense array length: %u rows, %u live states, two wide snapshots, "
                "%zu retention budget cutoffs\n",
                rows, liveStates, budgets);
}

} // namespace ctcompile::test::escape::arrays
