#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::length_detail {

void LengthCases::shrinkArithmetic() {
    shrink = "  ctjs.set_property %a[%key], %zero\n";
    // Preserve the exact formerly refused length-write body.
    run({.what = "a bounded own length write drops the original dense elements",
         .body = values + read + shrink + done,
         .arrays = "a:[]",
         .exit = "length -> {}"});
    originalShrink = {
        .what = "the original denseLengthChanged source releases only its removed child",
        .body = values + read + shrink +
                "  %result = ctjs.create_array [%a, %length] {storage_test_id = \"result\"}\n"
                "  ctjs.return %result\n",
        .arrays = "a:[]; result:[a,length]",
        .exit = "result -> {a,result}"};
    run(originalShrink);
    computedShrink = {
        .what = "a held length subtraction releases exactly its removed child",
        .body = values + one + read + subtract +
                "  ctjs.set_property %a[%key], %index\n"
                "  %result = ctjs.create_array [%a, %length] {storage_test_id = \"result\"}\n"
                "  ctjs.return %result\n",
        .arrays = "a:[]; result:[a,length]",
        .exit = "result -> {a,result}"};
    run(computedShrink);
    literalShrink = {
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
    for (const std::string left : {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
        for (const std::string right :
             {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
            const auto body = values + "  %left = ctjs.constant " + left +
                              "\n  %right = ctjs.constant " + right +
                              "\n  %index = ctjs.binary add %left, %right "
                              "{storage_test_id = \"index\"}\n"
                              "  ctjs.set_property %a[%key], %index\n"
                              "  %result = ctjs.create_array [%a, %index] "
                              "{storage_test_id = \"result\"}\n"
                              "  ctjs.return %result\n";
            if (left == "#ctjs.boolean<true>" && right == "#ctjs.boolean<true>") {
                run({.what = "primitive addition cannot introduce holes by growing length",
                     .body = body,
                     .failure = ArrayContentsFailure::MissingElement});
                continue;
            }
            const bool retained = left == "#ctjs.boolean<true>" || right == "#ctjs.boolean<true>";
            run({.what = "primitive addition preserves zero/unit length and result identity",
                 .body = body,
                 .arrays = retained ? "a:[x]; result:[a,index]" : "a:[]; result:[a,index]",
                 .exit = retained ? "result -> {a,result,x}" : "result -> {a,result}"},
                retained ? "" : "x");
        }
    }
    for (const std::string operands : {"%truth, %bound", "%bound, %truth"}) {
        run({.what = "primitive addition cannot cancel an out-of-bound sum",
             .body = values +
                     "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                     "  %truth = ctjs.constant #ctjs.boolean<true>\n"
                     "  %sum = ctjs.binary add " +
                     operands + "\n  %index = ctjs.binary sub %sum, %sum\n" + indexed,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    for (const std::string left : {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
        for (const std::string right :
             {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
            const auto body = values + "  %left = ctjs.constant " + left +
                              "\n  %right = ctjs.constant " + right +
                              "\n  %index = ctjs.binary sub %left, %right "
                              "{storage_test_id = \"index\"}\n"
                              "  ctjs.set_property %a[%key], %index\n"
                              "  %result = ctjs.create_array [%a, %index] "
                              "{storage_test_id = \"result\"}\n"
                              "  ctjs.return %result\n";
            if (left != "#ctjs.boolean<true>" && right == "#ctjs.boolean<true>") {
                run({.what = "negative primitive differences cannot become array lengths",
                     .body = body,
                     .failure = ArrayContentsFailure::UnknownIndex});
                continue;
            }
            const bool retained = left == "#ctjs.boolean<true>" && right != "#ctjs.boolean<true>";
            run({.what = "primitive subtraction preserves zero/unit length and result identity",
                 .body = body,
                 .arrays = retained ? "a:[x]; result:[a,index]" : "a:[]; result:[a,index]",
                 .exit = retained ? "result -> {a,result,x}" : "result -> {a,result}"},
                retained ? "" : "x");
        }
    }
    for (const std::string operands : {"%truth, %negative", "%negative, %truth"}) {
        run({.what = "primitive subtraction cannot cancel an out-of-bound signed result",
             .body = values +
                     "  %bound = ctjs.constant #ctjs.number<4751297606873776128>\n"
                     "  %negative = ctjs.unary neg %bound\n"
                     "  %truth = ctjs.constant #ctjs.boolean<true>\n"
                     "  %difference = ctjs.binary sub " +
                     operands + "\n  %index = ctjs.binary sub %difference, %difference\n" + indexed,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    productShrink = {
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
    quotientShrink = {
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
    for (const std::string literal :
         {"#ctjs.number<4611686018427387904>", "#ctjs.number<4613937818241073152>",
          "#ctjs.number<4751297606873776128>", "#ctjs.string<\"1\">", "#ctjs.string<\"2\">",
          "#ctjs.string<\"3\">", "#ctjs.string<\"4294967294\">"}) {
        auto source = powerShrink.body;
        source.replace(source.find("  %index ="), 0,
                       "  %exponent = ctjs.constant " + literal + "\n");
        source.replace(source.find("pow %negativeZero, %one"), 23, "pow %negativeZero, %exponent");
        run({.what = "even and odd bounded zero powers preserve the original signed length result",
             .body = source,
             .arrays = "a:[]; result:[a,index]",
             .exit = "result -> {a,result}"});
    }
    for (const auto & [base, exponent] :
         {std::pair{"0", "3"}, std::pair{"1", "4294967294"}, std::pair{"4294967294", "0"},
          std::pair{"4294967294", "1"}}) {
        const std::string expected = std::string_view(base) == "0"       ? "0"
                                     : std::string_view(exponent) == "1" ? "4751297606871678976"
                                                                         : "4607182418800017408";
        run({.what = "canonical String powers retain exact zero and unit identities at bounds",
             .body = values + "  %base = ctjs.constant #ctjs.string<\"" + base +
                     "\">\n  %exponent = ctjs.constant #ctjs.string<\"" + exponent +
                     "\">\n  %expected = ctjs.constant #ctjs.number<" + expected +
                     ">\n  %power = ctjs.binary pow %base, %exponent\n"
                     "  %index = ctjs.binary sub %power, %expected\n" +
                     indexed,
             .arrays = "a:[zero]",
             .exit = "a -> {a}"});
    }
    run({.what = "String power preserves the saved exponent's bytes after array replacement",
         .body = values +
                 "  %text = ctjs.constant #ctjs.string<\"2\"> {storage_test_id = \"text\"}\n"
                 "  %inputs = ctjs.create_array [%text] {storage_test_id = \"inputs\"}\n"
                 "  %saved = ctjs.get_property %inputs[%zero]\n"
                 "  ctjs.set_property %inputs[%zero], %x\n"
                 "  cf.br ^next(%saved : !ctjs.value)\n"
                 "^next(%exponent: !ctjs.value):\n"
                 "  %index = ctjs.binary pow %zero, %exponent\n"
                 "  ctjs.set_property %a[%index], %zero\n"
                 "  ctjs.return %exponent\n",
         .arrays = "a:[zero]; inputs:[x]",
         .reads = "inputs[0]=text",
         .exit = "text -> {}"});
    for (const std::string base : {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
        for (const std::string exponent :
             {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
            const bool retained =
                base == "#ctjs.boolean<true>" || exponent != "#ctjs.boolean<true>";
            run({.what = "primitive power zero/unit identities preserve length and result origin",
                 .body = values + "  %base = ctjs.constant " + base +
                         "\n  %exponent = ctjs.constant " + exponent +
                         "\n  %index = ctjs.binary pow %base, %exponent "
                         "{storage_test_id = \"index\"}\n"
                         "  ctjs.set_property %a[%key], %index\n"
                         "  %result = ctjs.create_array [%a, %index] "
                         "{storage_test_id = \"result\"}\n"
                         "  ctjs.return %result\n",
                 .arrays = retained ? "a:[x]; result:[a,index]" : "a:[]; result:[a,index]",
                 .exit = retained ? "result -> {a,result,x}" : "result -> {a,result}"},
                retained ? "" : "x");
        }
    }
    run({.what = "primitive power preserves a saved exponent after source replacement",
         .body = values +
                 "  %truth = ctjs.constant #ctjs.boolean<true> {storage_test_id = \"truth\"}\n"
                 "  %inputs = ctjs.create_array [%truth] {storage_test_id = \"inputs\"}\n"
                 "  %saved = ctjs.get_property %inputs[%zero]\n"
                 "  ctjs.set_property %inputs[%zero], %x\n"
                 "  cf.br ^next(%saved : !ctjs.value)\n"
                 "^next(%exponent: !ctjs.value):\n"
                 "  %index = ctjs.binary pow %zero, %exponent\n"
                 "  ctjs.set_property %a[%index], %zero\n"
                 "  ctjs.return %exponent\n",
         .arrays = "a:[zero]; inputs:[x]",
         .reads = "inputs[0]=truth",
         .exit = "truth -> {}"});

    remainderShrink = {
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
    shiftShrink = {
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
    signedShiftShrink = shiftShrink;
    signedShiftShrink.what = "signed shift supplies a non-growing length and keeps its origin";
    signedShiftShrink.body.replace(signedShiftShrink.body.find("ushr"), 4, "shr");
    run(signedShiftShrink);
    leftShiftShrink = shiftShrink;
    leftShiftShrink.what = "left shift supplies a non-growing length and keeps its origin";
    leftShiftShrink.body.replace(leftShiftShrink.body.find("ushr"), 4, "shl");
    run(leftShiftShrink);
    maskShrink = shiftShrink;
    maskShrink.what = "a bounded mask supplies a non-growing length and keeps its origin";
    maskShrink.body.replace(maskShrink.body.find("ushr"), 4, "bitand");
    run(maskShrink);
    orShrink = maskShrink;
    orShrink.what = "a zero OR supplies a non-growing length and keeps its origin";
    orShrink.body.replace(orShrink.body.find("bitand %negativeZero, %one"), 26,
                          "bitor %negativeZero, %zero");
    run(orShrink);
    xorShrink = orShrink;
    xorShrink.what = "a zero XOR supplies a non-growing length and keeps its origin";
    xorShrink.body.replace(xorShrink.body.find("bitor"), 5, "bitxor");
    run(xorShrink);
    heldOffsetShrink = {
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
    unaryShrink = {
        .what = "unary Plus preserves an original bounded Number shrink target",
        .body = values + read +
                "  %wanted = ctjs.unary plus %zero\n"
                "  ctjs.set_property %a[%key], %wanted\n"
                "  %result = ctjs.create_array [%a, %length] {storage_test_id = \"result\"}\n"
                "  ctjs.return %result\n",
        .arrays = "a:[]; result:[a,length]",
        .exit = "result -> {a,result}"};
    run(unaryShrink);
    negatedShrink = {
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
    for (const std::string operation :
         {"unary bitnot %text", "binary sub %text, %text", "binary mul %text, %zero",
          "binary div %zero, %text", "binary mod %text, %one"}) {
        run({.what = "negative String conversion supplies an exact empty length",
             .body = values + one +
                     "  %text = ctjs.constant #ctjs.string<\"-1\">\n"
                     "  %wanted = ctjs." +
                     operation + "\n  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
             .arrays = "a:[]",
             .exit = "a -> {a}"});
    }
    run({.what = "a negative String intermediate outside the bound cannot recover an exact length",
         .body = values + one +
                 "  %text = ctjs.constant #ctjs.string<\"-4294967295\">\n"
                 "  %outside = ctjs.binary sub %text, %one\n"
                 "  %recovered = ctjs.binary sub %outside, %text\n"
                 "  %wanted = ctjs.binary add %recovered, %one\n"
                 "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
         .failure = ArrayContentsFailure::UnknownIndex});
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
            if ((literal == "#ctjs.number<13830554455654793216>" ||
                 literal == "#ctjs.string<\"-1\">") &&
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
            if (literal == "#ctjs.string<\"0\">" &&
                (producer == "ctjs.binary sub %input, %zero" ||
                 producer == "ctjs.binary mul %input, %zero" ||
                 producer == "ctjs.binary_static ushr %input, %input" ||
                 producer == "ctjs.unary plus %input" || producer == "ctjs.unary neg %input")) {
                run({.what = "canonical String numeric conversion supplies an exact empty length",
                     .body = body,
                     .arrays = "a:[]",
                     .exit = "a -> {a}"});
                continue;
            }
            if ((literal == "#ctjs.boolean<false>" || literal == "#ctjs.null") &&
                (producer == "ctjs.unary plus %input" || producer == "ctjs.unary neg %input" ||
                 producer == "ctjs.binary add %input, %zero" ||
                 producer == "ctjs.binary sub %input, %zero" ||
                 producer == "ctjs.binary mul %input, %zero" ||
                 producer == "ctjs.binary_static ushr %input, %input")) {
                run({.what = "Boolean/null numeric conversion supplies an exact empty length",
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
}

} // namespace ctcompile::test::escape::arrays::length_detail
