#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::length_detail {

void LengthCases::arithmetic() {
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
        run({.what = "division needs an exact integral quotient and an in-bounds index",
             .body = values + one + "  %divisor = ctjs.constant " + literal +
                     "\n  %index = ctjs.binary div %one, %divisor\n" + indexed,
             .failure = literal == "#ctjs.string<\"1\">" || literal == "#ctjs.boolean<true>"
                            ? ArrayContentsFailure::MissingElement
                            : ArrayContentsFailure::UnknownIndex});
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
        const auto body = values + "  %divisor = ctjs.constant " + literal +
                          "\n  %index = ctjs.binary mod %zero, %divisor\n" + indexed;
        if (literal == "#ctjs.string<\"1\">" || literal == "#ctjs.boolean<true>") {
            run({.what = "the original primitive remainder supplies exact index zero",
                 .body = body,
                 .arrays = "a:[zero]",
                 .exit = "a -> {a}"});
            continue;
        }
        run({.what = "remainder needs an exact nonzero bounded divisor",
             .body = body,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    for (const std::string operation : {"div", "mod"}) {
        run({.what = "String arithmetic preserves saved bytes after replacement and transport",
             .body = values +
                     "  %text = ctjs.constant #ctjs.string<\"2\"> {storage_test_id = \"text\"}\n"
                     "  %inputs = ctjs.create_array [%text] {storage_test_id = \"inputs\"}\n"
                     "  %saved = ctjs.get_property %inputs[%zero]\n"
                     "  ctjs.set_property %inputs[%zero], %x\n"
                     "  cf.br ^next(%saved : !ctjs.value)\n"
                     "^next(%divisor: !ctjs.value):\n"
                     "  %index = ctjs.binary " +
                     operation +
                     " %zero, %divisor\n  ctjs.set_property %a[%index], %zero\n"
                     "  ctjs.return %divisor\n",
             .arrays = "a:[zero]; inputs:[x]",
             .reads = "inputs[0]=text",
             .exit = "text -> {}"});
        run({.what = "String zero keeps its original arithmetic result with a negative divisor",
             .body = values +
                     "  %text = ctjs.constant #ctjs.string<\"0\">\n"
                     "  %negative = ctjs.constant #ctjs.number<13830554455654793216>\n"
                     "  %index = ctjs.binary " +
                     operation +
                     " %text, %negative {storage_test_id = \"index\"}\n"
                     "  ctjs.set_property %a[%index], %zero\n"
                     "  ctjs.return %index\n",
             .arrays = "a:[zero]",
             .exit = "index -> {}"});
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
    for (const auto & [input, expected] :
         {std::pair{"0", "13830554455654793216"}, std::pair{"2147483647", "13970166044103278592"},
          std::pair{"2147483648", "4746794007244308480"},
          std::pair{"4294967294", "4607182418800017408"}}) {
        run({.what = "canonical String BitNot preserves exact signed ToInt32 boundary results",
             .body = values + "  %input = ctjs.constant #ctjs.string<\"" + input +
                     "\"> {storage_test_id = \"input\"}\n"
                     "  %saved = ctjs.create_array [%input] {storage_test_id = \"saved\"}\n"
                     "  %original = ctjs.get_property %saved[%zero]\n"
                     "  ctjs.set_property %saved[%zero], %zero\n"
                     "  %expected = ctjs.constant #ctjs.number<" +
                     expected +
                     ">\n  %bits = ctjs.unary bitnot %original\n"
                     "  %index = ctjs.binary sub %bits, %expected\n"
                     "  ctjs.set_property %a[%key], %index\n"
                     "  ctjs.return %original\n",
             .arrays = "a:[]; saved:[zero]",
             .reads = "saved[0]=input",
             .exit = "input -> {}"});
    }
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
        const std::string lhs = kind == "shr" || kind == "ushr"        ? "2147483648"
                                : kind == "bitand" || kind == "bitxor" ? "3"
                                : kind == "shl"                        ? "1"
                                                                       : "0";
        const std::string rhs = kind == "shr" || kind == "ushr" ? "31"
                                : kind == "shl"                 ? "32"
                                : kind == "bitxor"              ? "2"
                                                                : "1";
        const std::string expected = kind == "shr" ? "13830554455654793216" : "4607182418800017408";
        run({.what = "canonical String bitwise operands keep exact signed and masked results",
             .body = values + "  %lhs = ctjs.constant #ctjs.string<\"" + lhs +
                     "\">\n  %rhs = ctjs.constant #ctjs.string<\"" + rhs +
                     "\">\n  %expected = ctjs.constant #ctjs.number<" + expected +
                     ">\n  %bits = ctjs.binary_static " + kind +
                     " %lhs, %rhs\n"
                     "  %index = ctjs.binary sub %bits, %expected\n" +
                     indexed,
             .arrays = "a:[zero]",
             .exit = "a -> {a}"});
    }
    for (const std::string kind : {"ushr", "shr", "shl", "bitand", "bitor", "bitxor"}) {
        for (const std::string literal :
             {"#ctjs.number<4602678819172646912>",  // 0.5
              "#ctjs.number<13830554455654793216>", // -1
              "#ctjs.number<4751297606875873280>",  // 2^32
              "#ctjs.number<13974669643730649088>", // -2^32
              "#ctjs.number<9218868437227405312>",  // infinity
              "#ctjs.number<9221120237041090560>",  // NaN
              "#ctjs.string<\"0\">", "#ctjs.string<\"00\">", "#ctjs.string<\"-1\">",
              "#ctjs.string<\"1.0\">", "#ctjs.string<\"4294967295\">", "#ctjs.bigint<\"0\">",
              "#ctjs.boolean<false>", "#ctjs.null", "#ctjs.undefined"}) {
            for (const std::string operands : {"%input, %zero", "%zero, %input"}) {
                const bool exactZero =
                    literal == "#ctjs.string<\"0\">" || literal == "#ctjs.string<\"00\">" ||
                    literal == "#ctjs.boolean<false>" || literal == "#ctjs.null" ||
                    ((literal == "#ctjs.number<13830554455654793216>" ||
                      literal == "#ctjs.string<\"-1\">" || literal == "#ctjs.string<\"1.0\">" ||
                      literal == "#ctjs.string<\"4294967295\">") &&
                     (kind == "bitand" || ((kind == "shl" || kind == "shr" || kind == "ushr") &&
                                           operands == "%zero, %input")));
                run({.what = "only independently bounded bitwise inputs supply an exact index",
                     .body = values + "  %input = ctjs.constant " + literal +
                             "\n  %index = ctjs.binary_static " + kind + " " + operands + "\n" +
                             indexed,
                     .failure = exactZero ? ArrayContentsFailure::None
                                : literal == "#ctjs.string<\"1.0\">"
                                    ? ArrayContentsFailure::MissingElement
                                    : ArrayContentsFailure::UnknownIndex,
                     .arrays = exactZero ? "a:[zero]" : "",
                     .exit = exactZero ? "a -> {a}" : ""});
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
}

} // namespace ctcompile::test::escape::arrays::length_detail
