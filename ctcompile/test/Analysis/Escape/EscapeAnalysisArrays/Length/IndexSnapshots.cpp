#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::length_detail {

void LengthCases::indexSnapshots() {
    privateValues = "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
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
    branch = {.what = "length and original key transport preserve every structural frame exit",
              .body =
                  "  %frame = ctjs.frame_enter 8\n" + values + read +
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
    one = "  %one = ctjs.constant #ctjs.number<4607182418800017408>\n";
    subtract = "  %index = ctjs.binary sub %length, %one {storage_test_id = \"index\"}\n";
    indexed = "  ctjs.set_property %a[%index], %zero\n  ctjs.return %a\n";
    originalIndex = {
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
            const bool zero = input == "#ctjs.boolean<false>" || input == "#ctjs.null";
            run({.what = "each static Add operand needs an exact non-String Number conversion",
                 .body = values + "  %input = ctjs.constant " + input +
                         "\n  %index = ctjs.binary_static add " +
                         (left ? "%input, %zero\n" : "%zero, %input\n") + indexed,
                 .failure = zero ? ArrayContentsFailure::None : ArrayContentsFailure::UnknownIndex,
                 .arrays = zero ? "a:[zero]" : "",
                 .exit = zero ? "a -> {a}" : ""});
            run({.what = "original Add independently excludes String concatenation operands",
                 .body = values + "  %input = ctjs.constant " + input +
                         "\n  %index = ctjs.binary add " +
                         (left ? "%input, %zero\n" : "%zero, %input\n") + indexed,
                 .failure = zero ? ArrayContentsFailure::None : ArrayContentsFailure::UnknownIndex,
                 .arrays = zero ? "a:[zero]" : "",
                 .exit = zero ? "a -> {a}" : ""});
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
             .failure = literal == "#ctjs.string<\"01\">" ? ArrayContentsFailure::None
                                                          : ArrayContentsFailure::UnknownIndex,
             .arrays = "a:[zero] | a:[zero]",
             .exit = "a -> {a}; a -> {a}"});
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
        const auto body =
            values + "  %one = ctjs.constant " + literal + "\n" + read + subtract + indexed;
        if (literal == "#ctjs.boolean<true>" || literal == "#ctjs.string<\"01\">" ||
            literal == "#ctjs.string<\"+1\">" || literal == "#ctjs.string<\"0x1\">" ||
            literal == "#ctjs.string<\"1.0\">" || literal == "#ctjs.string<\"1e0\">") {
            run({.what = "the original primitive offset selects its exact overwritten slot",
                 .body = body,
                 .arrays = "a:[zero]",
                 .exit = "a -> {a}"});
            continue;
        }
        run({.what = "subtraction still refuses unproved or missing indices",
             .body = body,
             .failure = literal == "#ctjs.number<13830554455654793216>" ||
                                literal == "#ctjs.null" || literal == "#ctjs.string<\"\">" ||
                                literal == "#ctjs.string<\" \">" ||
                                literal == "#ctjs.string<\"-0\">"
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
}

} // namespace ctcompile::test::escape::arrays::length_detail
