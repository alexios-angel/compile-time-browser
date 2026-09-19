#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::induction_detail {

void InductionCases::signedArithmetic() {
    const std::string makeSubUnit = "  %left = ctjs.unary plus %one\n"
                                    "  %magnitude = ctjs.unary plus %two\n"
                                    "  %minus = ctjs.binary sub %left, %magnitude\n";
    const auto subChild = replace(negativeChild, makeNegativeUnit, makeSubUnit);
    savedSub = replace(subChild, makeSubUnit,
                       "  %seed = ctjs.create_array [%one, %one] {storage_test_id = \"seed\"}\n"
                       "  %name = ctjs.constant #ctjs.string<\"length\">\n"
                       "  %left = ctjs.unary plus %one\n"
                       "  %magnitude = ctjs.get_property %seed[%name]\n"
                       "  %minus = ctjs.binary sub %left, %magnitude\n"
                       "  ctjs.set_property %seed[%name], %zero\n");
    for (const auto & source : {subChild, savedSub}) {
        const char * arrays = source == subChild ? "a:[one,x]" : "a:[one,x]; seed:[]";
        run({.what = "bounded Number subtraction retains its exact negative snapshot and child",
             .body = source,
             .arrays = arrays,
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "negative Sub snapshots discharge only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = arrays,
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    run({.what = "a negative Sub snapshot preserves a zero-trip saved child",
         .body = replace(subChild, "^header(%a, %zero, %zero", "^header(%a, %two, %x"),
         .arrays = "a:[one,x]",
         .exit = "x -> {x}"});
    const auto carriedSub =
        replace(carriedNegative, "unary neg %magnitude", "binary sub %zero, %magnitude");
    for (const std::string literal :
         {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
        for (const bool primitiveLeft : {false, true}) {
            const std::string sum =
                "  %minus = ctjs.binary add " +
                std::string(primitiveLeft ? "%saved, %negative" : "%negative, %saved") + "\n";
            const auto source = replace(
                negativeChild, makeNegativeUnit,
                "  %primitive = ctjs.constant " + literal +
                    " {storage_test_id = \"primitive\"}\n"
                    "  %inputs = ctjs.create_array [%primitive] {storage_test_id = \"inputs\"}\n"
                    "  %saved = ctjs.get_property %inputs[%zero]\n"
                    "  ctjs.set_property %inputs[%zero], %x\n"
                    "  %negative = ctjs.unary neg " +
                    (literal == "#ctjs.boolean<true>" ? "%two\n" : "%one\n") + sum);
            run({.what = "primitive addition retains CFG snapshots after operand replacement",
                 .body = source,
                 .arrays = "a:[one,x]; inputs:[x]",
                 .reads = "inputs[0]=primitive; a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "primitive addition releases only unreturned CFG children",
                 .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]; inputs:[x]",
                 .reads = "inputs[0]=primitive; a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
            reject("addition cannot turn its original Boolean/null into an own key",
                   replace(source, "%base[%i]", "%base[%primitive]"),
                   ArrayContentsFailure::UnknownIndex);
            run({.what = "repeated primitive addition retains its saved operand snapshot",
                 .body = replace(replace(source, sum, ""), "  %step =", sum + "  %step ="),
                 .arrays = "a:[one,x]; inputs:[x]",
                 .reads = "inputs[0]=primitive; a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            reject("primitive addition cannot borrow an unknown operand",
                   replace(source, "ctjs.constant " + literal, "ctjs.unary plus %p"),
                   ArrayContentsFailure::UnsupportedOperation);
            reject("String concatenation cannot borrow primitive addition evidence",
                   replace(source, literal, "#ctjs.string<\"0\">"));
            reject("Undefined cannot borrow primitive addition evidence",
                   replace(source, literal, "#ctjs.undefined"));
        }
    }
    const auto primitiveCarriedAdd = replace(carriedNegative, makeNegative,
                                             "  %nil = ctjs.constant #ctjs.null\n"
                                             "  %negative = ctjs.unary neg %one\n"
                                             "  %unit = ctjs.binary_static add %nil, %negative\n");
    run({.what = "primitive addition snapshots survive exact CFG backedge transport",
         .body = primitiveCarriedAdd,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    reject("primitive addition snapshots cannot change across CFG backedges",
           replace(primitiveCarriedAdd, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    for (const std::string literal :
         {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
        for (const bool primitiveLeft : {false, true}) {
            const std::string number = literal == "#ctjs.boolean<true>" ? "%two" : "%one";
            const std::string difference =
                "  %minus = ctjs.binary sub " +
                (primitiveLeft ? "%saved, " + number : number + ", %saved") + "\n";
            auto source = replace(
                subChild, makeSubUnit,
                "  %primitive = ctjs.constant " + literal +
                    " {storage_test_id = \"primitive\"}\n"
                    "  %inputs = ctjs.create_array [%primitive] {storage_test_id = \"inputs\"}\n"
                    "  %saved = ctjs.get_property %inputs[%zero]\n"
                    "  ctjs.set_property %inputs[%zero], %x\n" +
                    difference);
            if (!primitiveLeft) {
                source = replace(source, "binary sub %i, %minus", "binary add %i, %minus");
            }
            run({.what = "primitive subtraction retains CFG snapshots after operand replacement",
                 .body = source,
                 .arrays = "a:[one,x]; inputs:[x]",
                 .reads = "inputs[0]=primitive; a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "primitive subtraction releases only unreturned CFG children",
                 .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]; inputs:[x]",
                 .reads = "inputs[0]=primitive; a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
            reject("subtraction cannot turn its original Boolean/null into an own key",
                   replace(source, "%base[%i]", "%base[%primitive]"),
                   ArrayContentsFailure::UnknownIndex);
            run({.what = "repeated primitive subtraction retains its saved operand snapshot",
                 .body = replace(replace(source, difference, ""),
                                 "  %step =", difference + "  %step ="),
                 .arrays = "a:[one,x]; inputs:[x]",
                 .reads = "inputs[0]=primitive; a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            reject("primitive subtraction cannot borrow an unknown operand",
                   replace(source, "ctjs.constant " + literal, "ctjs.unary plus %p"),
                   ArrayContentsFailure::UnsupportedOperation);
            reject("Undefined cannot borrow primitive subtraction evidence",
                   replace(source, literal, "#ctjs.undefined"));
        }
    }
    const auto primitiveCarriedSub =
        replace(carriedSub, "  %unit = ctjs.binary sub %zero, %magnitude\n",
                "  %nil = ctjs.constant #ctjs.null\n"
                "  %unit = ctjs.binary sub %nil, %magnitude\n");
    run({.what = "primitive subtraction snapshots survive exact CFG backedge transport",
         .body = primitiveCarriedSub,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    reject("primitive subtraction snapshots cannot change across CFG backedges",
           replace(primitiveCarriedSub, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    run({.what = "negative Sub snapshots retain their exact final overshoot after array growth",
         .body =
             replace(replace(carriedSub, "^exit(%sum :", "^exit(%index :"), "  ctjs.return %result",
                     "  ctjs.append %zero to %a\n  ctjs.append %zero to %a\n"
                     "  %after = ctjs.get_property %a[%result]\n  ctjs.return %after"),
         .arrays = "a:[one,two,three,zero,zero]",
         .reads = "a[0]=one; a[2]=three; a[4]=zero",
         .exit = "zero -> {}"});
    for (const std::string operands : {"%left, %left", "%magnitude, %left"}) {
        reject("zero and positive Sub results cannot supply a negative stride",
               replace(subChild, "binary sub %left, %magnitude", "binary sub " + operands));
    }
    const auto leftStringSub = replace(subChild, "unary plus %one", "constant #ctjs.string<\"1\">");
    const auto bothStringSub =
        replace(leftStringSub, "unary plus %two", "constant #ctjs.string<\"2\">");
    const auto savedLeftStringSub =
        replace(savedSub, "unary plus %one", "constant #ctjs.string<\"1\">");
    const auto stringSub = replace(subChild, "unary plus %two", "constant #ctjs.string<\"2\">");
    const auto savedStringSub =
        replace(replace(savedSub, "unary plus %one", "get_property %seed[%name]"),
                "%magnitude = ctjs.get_property %seed[%name]",
                "%magnitude = ctjs.constant #ctjs.string<\"3\">");
    for (const auto & source :
         {stringSub, savedStringSub, leftStringSub, bothStringSub, savedLeftStringSub}) {
        const char * arrays = source == savedStringSub || source == savedLeftStringSub
                                  ? "a:[one,x]; seed:[]"
                                  : "a:[one,x]";
        run({.what = "canonical String operands retain negative snapshots after source shrink",
             .body = source,
             .arrays = arrays,
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "negative String subtraction snapshots discharge only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = arrays,
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    const auto carriedStringSub =
        replace(carriedSub, "ctjs.binary add %one, %one", "ctjs.constant #ctjs.string<\"2\">");
    run({.what = "negative String-offset snapshots preserve simultaneous CFG transport",
         .body = carriedStringSub,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    reject("a negative String-offset snapshot cannot change on the CFG backedge",
           replace(carriedStringSub, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    run({.what = "repeated subtraction proves original signed primitive operands",
         .body = replace(replace(stringSub, "  %minus = ctjs.binary sub %left, %magnitude\n", ""),
                         "  %step =", "  %minus = ctjs.binary sub %left, %magnitude\n  %step ="),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    reject("a negative String-offset snapshot cannot become an own array index",
           replace(stringSub, "%base[%i]", "%base[%minus]"), ArrayContentsFailure::UnknownIndex);
    for (const std::string text :
         {"02", "+2", "-2", "2.0", "2e0", " 2", "0x2", "4294967295", "NaN"}) {
        reject("negative String offsets require the existing bounded canonical decimal proof",
               replace(stringSub, "#ctjs.string<\"2\">", "#ctjs.string<\"" + text + "\">"));
        const auto left =
            replace(leftStringSub, "#ctjs.string<\"1\">", "#ctjs.string<\"" + text + "\">");
        if (text == "-2") {
            run({.what = "a canonical negative left String preserves its exact stride",
                 .body = left,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one",
                 .exit = "one -> {}"},
                "x");
        } else {
            reject("left String subtraction requires bounded canonical decimal conversion", left);
        }
    }
    run({.what = "repeated subtraction proves original signed primitive operands",
         .body =
             replace(replace(leftStringSub, "  %minus = ctjs.binary sub %left, %magnitude\n", ""),
                     "  %step =", "  %minus = ctjs.binary sub %left, %magnitude\n  %step ="),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    reject("a negative left String result cannot become an own array index",
           replace(leftStringSub, "%base[%i]", "%base[%minus]"),
           ArrayContentsFailure::UnknownIndex);
    for (const auto & [left, right] :
         {std::pair{"0", "9223372036854775808"}, std::pair{"1", "4607182418800017408"},
          std::pair{"4294967294", "4751297606871678976"}}) {
        run({.what = "canonical left String cancellation keeps the original exact zero result",
             .body = prefix + "  %left = ctjs.constant #ctjs.string<\"" + left +
                     "\">\n  %right = ctjs.constant #ctjs.number<" + right +
                     ">\n  %actual = ctjs.binary sub %left, %right "
                     "{storage_test_id = \"actual\"}\n"
                     "  %read = ctjs.get_property %a[%actual]\n  ctjs.return %actual\n",
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one",
             .exit = "actual -> {}"});
    }
    reject("negative Sub snapshots cannot become own array indices",
           replace(subChild, "%base[%i]", "%base[%minus]"), ArrayContentsFailure::UnknownIndex);
    reject("Add cannot use a negative Sub snapshot as its positive stride",
           replace(subChild, "binary sub %i, %minus", "binary add %i, %minus"));
    run({.what = "repeated subtraction proves original signed primitive operands",
         .body = replace(replace(subChild, "  %minus = ctjs.binary sub %left, %magnitude\n", ""),
                         "  %step =", "  %minus = ctjs.binary sub %left, %magnitude\n  %step ="),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    reject("a carried Sub snapshot cannot change on the backedge",
           replace(carriedSub, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    reject("a negative Sub snapshot still bounds its final update",
           replace(replace(carriedSub, "ctjs.binary add %one, %one",
                           "ctjs.constant #ctjs.number<4751297606873776128>"),
                   "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    const std::string difference =
        "  %other = ctjs.unary neg %two\n"
        "  %difference = ctjs.binary sub %minus, %one {storage_test_id = \"difference\"}\n";
    const auto negativeLeft = replace(replace(replace(savedSub, "[%one, %x]", "[%one, %one, %x]"),
                                              "  cf.br ^header", difference + "  cf.br ^header"),
                                      "binary sub %i, %minus", "binary sub %i, %difference");
    for (const std::string operands : {"%minus, %one", "%minus, %other", "%other, %minus"}) {
        const auto source = replace(replace(negativeLeft, "sub %minus, %one", "sub " + operands),
                                    "binary sub %i, %difference",
                                    operands == "%minus, %other" ? "binary add %i, %difference"
                                                                 : "binary sub %i, %difference");
        const char * reads =
            operands == "%minus, %one" ? "a[0]=one; a[2]=x" : "a[0]=one; a[1]=one; a[2]=x";
        run({.what = "negative-left Sub retains signed snapshots after source shrink",
             .body = source,
             .arrays = "a:[one,one,x]; seed:[]",
             .reads = reads,
             .exit = "x -> {x}"});
        run({.what = "negative-left Sub releases only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,one,x]; seed:[]",
             .reads = reads,
             .exit = "zero -> {}"},
            "x");
    }
    const auto cancelledSub = replace(negativeLeft, "sub %minus, %one", "sub %minus, %minus");
    run({.what = "negative Sub cancellation supplies zero without losing result identity",
         .body = replace(replace(replace(cancelledSub, "^header(%a, %zero, %zero",
                                         "^header(%a, %difference, %zero"),
                                 "binary sub %i, %difference", "binary_static add %i, %one"),
                         "ctjs.return %result", "ctjs.return %difference"),
         .arrays = "a:[one,one,x]; seed:[]",
         .reads = "a[0]=one; a[1]=one; a[2]=x",
         .exit = "difference -> {}"},
        "x");
    reject("negative Sub cancellation cannot certify progress", cancelledSub);
    reject("negative-left Sub cannot supply an own index",
           replace(negativeLeft, "%base[%i]", "%base[%difference]"),
           ArrayContentsFailure::UnknownIndex);
    run({.what = "repeated negative-left subtraction retains saved input values",
         .body =
             replace(replace(negativeLeft, difference, ""), "  %step =", difference + "  %step ="),
         .arrays = "a:[one,one,x]; seed:[]",
         .reads = "a[0]=one; a[2]=x",
         .exit = "x -> {x}"});
    for (const std::string constant :
         {"#ctjs.string<\"01\">", "#ctjs.bigint<\"1\">", "#ctjs.number<4602678819172646912>",
          "#ctjs.number<4751297606875873280>"}) {
        reject("negative-left Sub needs bounded exact Numbers or canonical String offsets",
               replace(negativeLeft, difference,
                       "  %operand = ctjs.constant " + constant + "\n" +
                           replace(difference, "sub %minus, %one", "sub %minus, %operand")));
    }
    const auto negativeStringLeft =
        replace(negativeLeft, difference,
                "  %operand = ctjs.constant #ctjs.string<\"1\">\n" +
                    replace(difference, "sub %minus, %one", "sub %minus, %operand"));
    run({.what = "a canonical String offset extends a held negative Number after source shrink",
         .body = negativeStringLeft,
         .arrays = "a:[one,one,x]; seed:[]",
         .reads = "a[0]=one; a[2]=x",
         .exit = "x -> {x}"});
    run({.what = "negative-left String offsets release only unreturned children",
         .body = replace(negativeStringLeft, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,one,x]; seed:[]",
         .reads = "a[0]=one; a[2]=x",
         .exit = "zero -> {}"},
        "x");
    reject("negative-left Sub cannot borrow an unknown operand",
           replace(negativeLeft, "sub %minus, %one", "sub %minus, %p"),
           ArrayContentsFailure::UnsupportedOperation);
    const auto maximumSub = replace(replace(negativeLeft, "binary sub %left, %magnitude",
                                            "constant #ctjs.number<13974669643728551936>"),
                                    "sub %minus, %one", "sub %minus, %zero");
    run({.what = "negative-left Sub preserves an exact boundary magnitude and origin",
         .body = replace(maximumSub, "ctjs.return %result", "ctjs.return %difference"),
         .arrays = "a:[one,one,x]; seed:[]",
         .reads = "a[0]=one",
         .exit = "difference -> {}"},
        "x");
    reject("negative-left Sub must bound its final induction update",
           replace(maximumSub, "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    reject("negative-left Sub cannot certify an overflowing magnitude",
           replace(maximumSub, "sub %minus, %zero", "sub %minus, %one"));
    reject("a canonical String offset cannot overflow a held negative magnitude",
           replace(replace(maximumSub, "  %other =",
                           "  %offset = ctjs.constant "
                           "#ctjs.string<\"1\">\n  %other ="),
                   "sub %minus, %zero", "sub %minus, %offset"));
    run({.what = "the largest canonical String offset can reach the exact signed Number boundary",
         .body = prefix + "  %negative = ctjs.constant #ctjs.number<13830554455654793216>\n"
                          "  %offset = ctjs.constant #ctjs.string<\"4294967294\">\n"
                          "  %expected = ctjs.constant #ctjs.number<13974669643728551936>\n"
                          "  %actual = ctjs.binary sub %negative, %offset "
                          "{storage_test_id = \"actual\"}\n"
                          "  %index = ctjs.binary sub %actual, %expected\n"
                          "  %read = ctjs.get_property %a[%index]\n  ctjs.return %actual\n",
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one",
         .exit = "actual -> {}"});
    run({.what = "String zero preserves negative-zero result identity and exact index zero",
         .body = prefix + "  %negativeZero = ctjs.constant #ctjs.number<9223372036854775808>\n"
                          "  %offset = ctjs.constant #ctjs.string<\"0\">\n"
                          "  %actual = ctjs.binary sub %negativeZero, %offset "
                          "{storage_test_id = \"actual\"}\n"
                          "  %read = ctjs.get_property %a[%actual]\n  ctjs.return %actual\n",
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one",
         .exit = "actual -> {}"});
    for (const std::string opcode : {"binary", "binary_static"}) {
        for (const std::string operands : {"%minus, %two", "%two, %minus"}) {
            const std::string cancel = "  %cancelled = ctjs." + opcode + " add " + operands +
                                       " {storage_test_id = \"cancelled\"}\n";
            const auto cancelled =
                replace(replace(savedSub, "  cf.br ^header", cancel + "  cf.br ^header"),
                        "binary sub %i, %minus", "binary_static add %i, %cancelled");
            run({.what = "Add cancellation preserves a held negative snapshot after source shrink",
                 .body = cancelled,
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "Add cancellation discharges only unreturned children",
                 .body = replace(cancelled, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
            run({.what = "a cancelled Number snapshot supplies an exact own index",
                 .body = replace(cancelled, "%base[%i]", "%base[%cancelled]"),
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[1]=x; a[1]=x",
                 .exit = "x -> {x}"});
            const auto repeated =
                replace(replace(cancelled, cancel, ""), "  %step =", cancel + "  %step =");
            if (opcode == "binary") {
                run({.what = "repeated dynamic Add cancellation proves its saved operands",
                     .body = repeated,
                     .arrays = "a:[one,x]; seed:[]",
                     .reads = "a[0]=one; a[1]=x",
                     .exit = "x -> {x}"});
            } else {
                reject("repeated static Add retains its independent invariant boundary", repeated);
            }
        }
    }
    const std::string cancel = "  %cancelled = ctjs.binary add %minus, %two "
                               "{storage_test_id = \"cancelled\"}\n";
    const auto cancelled = replace(replace(savedSub, "  cf.br ^header", cancel + "  cf.br ^header"),
                                   "binary sub %i, %minus", "binary_static add %i, %cancelled");
    const auto cancelledZero =
        replace(replace(replace(cancelled, "binary add %minus, %two", "binary add %minus, %one"),
                        "add %i, %cancelled", "add %i, %one"),
                "^header(%a, %zero, %zero", "^header(%a, %cancelled, %zero");
    run({.what = "exact cancellation to zero initializes induction",
         .body = cancelledZero,
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "a cancelled zero retains its result origin",
         .body = replace(cancelledZero, "ctjs.return %result", "ctjs.return %cancelled"),
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "cancelled -> {}"},
        "x");
    run({.what = "a cancelled stride preserves the original zero-trip result",
         .body = replace(cancelled, "^header(%a, %zero, %zero", "^header(%a, %magnitude, %x"),
         .arrays = "a:[one,x]; seed:[]",
         .exit = "x -> {x}"});
    for (const std::string value : {"%one", "%zero", "%minus"}) {
        reject("a zero or negative cancelled result cannot supply a positive stride",
               replace(cancelled, "binary add %minus, %two", "binary add %minus, " + value));
    }
    for (const std::string constant :
         {"#ctjs.string<\"2\">", "#ctjs.bigint<\"2\">", "#ctjs.number<4751297606875873280>"}) {
        reject("cancellation requires both original operands inside the bounded Number domain",
               replace(cancelled, "#ctjs.number<4611686018427387904>", constant));
    }
    reject("cancellation cannot borrow an unknown operand",
           replace(cancelled, "binary add %minus, %two", "binary add %minus, %p"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("a cancelled stride still bounds its final update",
           replace(replace(replace(cancelled, "#ctjs.number<4611686018427387904>",
                                   "#ctjs.number<4751297606873776128>"),
                           "[%one, %x]", "[%one, %x, %one]"),
                   "^header(%a, %zero, %zero", "^header(%a, %magnitude, %zero"));
    const auto savedNegativeAdd =
        replace(savedSub, "binary sub %left, %magnitude", "binary sub %zero, %magnitude");
    for (const std::string opcode : {"binary", "binary_static"}) {
        for (const std::string operands : {"%minus, %one", "%one, %minus", "%minus, %minus"}) {
            const std::string sum = "  %negativeSum = ctjs." + opcode + " add " + operands +
                                    " {storage_test_id = \"negativeSum\"}\n";
            const bool bothNegative = operands == "%minus, %minus";
            auto source = bothNegative ? replace(savedSub, "[%one, %x]", "[%one, %one, %x]")
                                       : savedNegativeAdd;
            source = replace(replace(source, "  cf.br ^header", sum + "  cf.br ^header"),
                             "sub %i, %minus", "sub %i, %negativeSum");
            const char * arrays = bothNegative ? "a:[one,one,x]; seed:[]" : "a:[one,x]; seed:[]";
            const char * reads = bothNegative ? "a[0]=one; a[2]=x" : "a[0]=one; a[1]=x";
            run({.what = "negative Add snapshots retain exact children after source shrink",
                 .body = source,
                 .arrays = arrays,
                 .reads = reads,
                 .exit = "x -> {x}"});
            run({.what = "negative Add snapshots release only unreturned children",
                 .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = arrays,
                 .reads = reads,
                 .exit = "zero -> {}"},
                "x");
        }
    }
    const std::string negativeSum =
        "  %negativeSum = ctjs.binary add %minus, %one {storage_test_id = \"negativeSum\"}\n";
    const auto negativeAdd =
        replace(replace(savedNegativeAdd, "  cf.br ^header", negativeSum + "  cf.br ^header"),
                "sub %i, %minus", "sub %i, %negativeSum");
    run({.what = "a negative Add snapshot preserves a zero-trip saved child",
         .body = replace(negativeAdd, "^header(%a, %zero, %zero", "^header(%a, %two, %x"),
         .arrays = "a:[one,x]; seed:[]",
         .exit = "x -> {x}"});
    reject("a negative Add snapshot cannot become an own index",
           replace(negativeAdd, "%base[%i]", "%base[%negativeSum]"),
           ArrayContentsFailure::UnknownIndex);
    reject("a negative Add snapshot cannot initialize an increasing loop",
           replace(negativeAdd, "^header(%a, %zero, %zero", "^header(%a, %negativeSum, %zero"));
    reject("a negative Add snapshot cannot supply a positive Add stride",
           replace(negativeAdd, "binary sub %i, %negativeSum", "binary add %i, %negativeSum"));
    run({.what = "repeated negative addition retains its original saved magnitude",
         .body =
             replace(replace(negativeAdd, negativeSum, ""), "  %step =", negativeSum + "  %step ="),
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    for (const std::string constant :
         {"#ctjs.string<\"1\">", "#ctjs.bigint<\"1\">", "#ctjs.number<4602678819172646912>",
          "#ctjs.number<4751297606875873280>"}) {
        reject("negative Add requires bounded exact Number operands",
               replace(negativeAdd, negativeSum,
                       "  %operand = ctjs.constant " + constant + "\n" +
                           replace(negativeSum, ", %one", ", %operand")));
    }
    reject("negative Add cannot borrow an unknown operand",
           replace(negativeAdd, "add %minus, %one", "add %minus, %p"),
           ArrayContentsFailure::UnsupportedOperation);
    const auto maximumNegativeAdd = replace(replace(negativeAdd, "binary sub %zero, %magnitude",
                                                    "constant #ctjs.number<13974669643728551936>"),
                                            "add %minus, %one", "add %minus, %zero");
    run({.what = "a negative Add at the bound retains its original result identity",
         .body = replace(maximumNegativeAdd, "ctjs.return %result", "ctjs.return %negativeSum"),
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[0]=one",
         .exit = "negativeSum -> {}"},
        "x");
    reject("negative Add still bounds the final exact induction update",
           replace(maximumNegativeAdd, "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    reject("bounded negative Add operands cannot certify an out-of-domain sum",
           replace(maximumNegativeAdd, "add %minus, %zero", "add %minus, %minus"));
    for (const std::string operation : {"div", "mod"}) {
        for (const std::string literal :
             {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
            for (const bool negative : {false, true}) {
                for (const bool commuted : {false, true}) {
                    const std::string expected =
                        literal != "#ctjs.boolean<true>" || operation == "mod" ? "0"
                        : negative ? "13830554455654793216"
                                   : "4607182418800017408";
                    const auto body = prefix + "  %input = ctjs.constant " + literal +
                                      "\n  %factor = ctjs.unary " + (negative ? "neg" : "plus") +
                                      " %one\n  %snapshot = ctjs.binary " + operation + " " +
                                      (commuted ? "%factor, %input" : "%input, %factor") +
                                      " {storage_test_id = \"snapshot\"}\n"
                                      "  %expected = ctjs.constant #ctjs.number<" +
                                      expected +
                                      ">\n  %index = ctjs.binary sub %snapshot, %expected\n"
                                      "  %read = ctjs.get_property %a[%index]\n"
                                      "  ctjs.return %snapshot\n";
                    if (commuted && literal != "#ctjs.boolean<true>") {
                        reject("Boolean/null zero divisors cannot supply finite Number snapshots",
                               body, ArrayContentsFailure::UnknownIndex);
                    } else {
                        run({.what =
                                 "primitive division and remainder retain signed result origins",
                             .body = body,
                             .arrays = "a:[one,two,three]",
                             .reads = "a[0]=one",
                             .exit = "snapshot -> {}"});
                    }
                }
            }
        }
        const auto divisor = operation == "div" ? "%one" : "%two";
        const std::string makeResult = "  %signedResult = ctjs.binary " + operation + " %minus, " +
                                       divisor + " {storage_test_id = \"signedResult\"}\n";
        const auto source =
            replace(replace(savedSub, "  cf.br ^header", makeResult + "  cf.br ^header"),
                    "sub %i, %minus", "sub %i, %signedResult");
        for (const auto & body :
             {source,
              replace(source, makeResult,
                      "  %divisor = ctjs.unary neg " + std::string(divisor) + "\n" +
                          replace(makeResult, ", " + std::string(divisor), ", %divisor"))}) {
            const auto signedBody = operation == "div" && body != source
                                        ? replace(body, "binary sub %i, %signedResult",
                                                  "binary_static add %i, %signedResult")
                                        : body;
            run({.what = "signed division and remainder keep the original snapshot and child",
                 .body = signedBody,
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "signed division and remainder release only unreturned children",
                 .body = replace(signedBody, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
        }
        reject("negative quotients and remainders cannot supply own indices",
               replace(source, "%base[%i]", "%base[%signedResult]"),
               ArrayContentsFailure::UnknownIndex);
        run({.what = "repeated division and remainder prove their invariant operands",
             .body =
                 replace(replace(source, makeResult, ""), "  %step =", makeResult + "  %step ="),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        reject("signed division and remainder require a nonzero divisor",
               replace(source, ", " + std::string(divisor) + " {storage_test_id",
                       ", %zero {storage_test_id"));
        const auto originalString =
            replace(source, makeResult,
                    "  %divisor = ctjs.constant #ctjs.string<\"1\">\n" +
                        replace(makeResult, ", " + std::string(divisor), ", %divisor"));
        if (operation == "div") {
            run({.what = "the original canonical String divisor retains its returned child",
                 .body = originalString,
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
        } else {
            reject("a canonical String divisor cannot turn a zero remainder into progress",
                   originalString);
        }
        const std::string text = operation == "div" ? "1" : "2";
        const auto stringRight =
            replace(originalString, "#ctjs.string<\"1\">", "#ctjs.string<\"" + text + "\">");
        const auto stringLeft =
            replace(replace(source, makeResult,
                            "  %text = ctjs.constant #ctjs.string<\"1\">\n" +
                                replace(makeResult, "%minus,", "%text,")),
                    "binary sub %i, %signedResult", "binary add %i, %signedResult");
        for (const auto & body : {stringRight, stringLeft}) {
            run({.what = "canonical String division and remainder retain the exact CFG child",
                 .body = body,
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "canonical String division and remainder release unreturned children",
                 .body = replace(body, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
        }
        const auto primitive =
            operation == "div"
                ? replace(originalString, "#ctjs.string<\"1\">", "#ctjs.boolean<true>")
                : replace(stringLeft, "#ctjs.string<\"1\">", "#ctjs.boolean<true>");
        run({.what = "Boolean division and remainder keep CFG snapshots and returned children",
             .body = primitive,
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "Boolean division and remainder release only unreturned CFG children",
             .body = replace(primitive, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        const std::string operand = operation == "div" ? "%divisor" : "%text";
        const auto savedPrimitive =
            replace(primitive, "  %signedResult =",
                    "  %holder = ctjs.create_array [" + operand +
                        "] {storage_test_id = \"holder\"}\n"
                        "  %savedOperand = ctjs.get_property %holder[%zero]\n"
                        "  ctjs.set_property %holder[%zero], %x\n  %signedResult =");
        run({.what = "a saved Boolean dividend or divisor survives source replacement",
             .body = replace(savedPrimitive, operation == "div" ? ", %divisor {" : "%text,",
                             operation == "div" ? ", %savedOperand {" : "%savedOperand,"),
             .arrays = "a:[one,x]; seed:[]; holder:[x]",
             .reads = "holder[0]=ctjs.constant; a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        reject("division cannot turn an original primitive into an own array key",
               replace(primitive, "%base[%i]", "%base[" + operand + "]"),
               ArrayContentsFailure::UnknownIndex);
        const auto repeated = replace(makeResult, operation == "div" ? ", %one" : "%minus,",
                                      operation == "div" ? ", %divisor" : "%text,");
        run({.what = "repeated primitive division retains its original operand proof",
             .body = replace(replace(primitive, repeated, ""), "  %step =", repeated + "  %step ="),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        for (const std::string invalid : {"#ctjs.undefined", "#ctjs.number<9218868437227405312>",
                                          "#ctjs.number<9221120237041090560>"}) {
            reject("nonfinite primitive division cannot borrow exact Boolean evidence",
                   replace(primitive, "#ctjs.boolean<true>", invalid));
        }
        for (const std::string invalid :
             {"0", "01", "+1", "-1", "1.0", "1e0", " 1", "0x1", "4294967295", "NaN"}) {
            reject("String divisors need canonical bounded nonzero decimal evidence",
                   replace(stringRight, "#ctjs.string<\"" + text + "\">",
                           "#ctjs.string<\"" + invalid + "\">"));
        }
        run({.what = "repeated String division proves its original saved operands",
             .body = replace(replace(stringRight, "  %signedResult =", "  %unused ="), "  %step =",
                             replace(makeResult, ", " + std::string(divisor), ", %divisor") +
                                 "  %step ="),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        reject("signed division and remainder cannot borrow an unknown operand",
               replace(source, ", " + std::string(divisor) + " {storage_test_id",
                       ", %p {storage_test_id"),
               ArrayContentsFailure::UnsupportedOperation);
        if (operation == "div") {
            run({.what = "a positive Number divided by a negative snapshot keeps a negative stride",
                 .body = replace(source, "div %minus, %one", "div %one, %minus"),
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            reject("signed division needs an integral quotient",
                   replace(source, "div %minus, %one", "div %minus, %two"));
            reject("canonical String division still needs an integral quotient",
                   replace(stringRight, "#ctjs.string<\"1\">", "#ctjs.string<\"2\">"));
        }
    }
}

} // namespace ctcompile::test::escape::arrays::induction_detail
