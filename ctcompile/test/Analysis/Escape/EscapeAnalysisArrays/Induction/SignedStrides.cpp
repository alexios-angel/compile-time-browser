#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::induction_detail {

void InductionCases::signedStrides() {
    const std::string makeBoolean =
        "  %unit = ctjs.constant #ctjs.boolean<true> {storage_test_id = \"unit\"}\n";
    for (const std::string opcode : {"binary_static", "binary"}) {
        for (const std::string operands : {"%i, %unit", "%unit, %i"}) {
            const auto boolean = makeBoolean + replace(savedChild, "binary_static add %i, %one",
                                                       opcode + " add " + operands);
            run({.what = "Boolean Add latches retain the original returned child in either order",
                 .body = boolean,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "Boolean Add latches release only unreturned children",
                 .body = replace(boolean, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
            reject("Boolean latch conversion leaves the original property key unchanged",
                   replace(boolean, "%base[%i]", "%base[%unit]"),
                   ArrayContentsFailure::UnknownIndex);
            for (const std::string constant :
                 {"#ctjs.boolean<false>", "#ctjs.null", "#ctjs.undefined", "#ctjs.string<\"1\">"}) {
                reject("zero, unknown and concatenating latches cannot borrow Boolean progress",
                       replace(boolean, "#ctjs.boolean<true>", constant));
            }
        }
    }
    const auto carriedBoolean = replace(replace(carriedUnit, makeUnit, makeBoolean),
                                        "binary_static add %i, %d", "binary add %d, %i");
    run({.what = "Boolean strides survive original CFG header and backedge transport",
         .body = carriedBoolean,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    reject("a Boolean stride cannot change even to the equivalent Number one",
           replace(carriedBoolean, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    const auto alternateBoolean =
        replace(replace(alternateUnit, makeUnit, makeBoolean), "binary_static add %i, %chosen",
                "binary add %i, %chosen");
    run({.what = "each CFG predecessor independently proves its Boolean or Number stride",
         .body = alternateBoolean,
         .arrays = "a:[one,two,three] | a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three; a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}; added -> {}"});
    reject("an unknown predecessor cannot borrow another predecessor's Boolean stride",
           replace(alternateBoolean, "^entry(%one :", "^entry(%p :"));
    const std::string savedBoolean =
        "  %truth = ctjs.constant #ctjs.boolean<true> {storage_test_id = \"truth\"}\n"
        "  %seed = ctjs.create_array [%truth] {storage_test_id = \"seed\"}\n"
        "  %unit = ctjs.get_property %seed[%zero]\n"
        "  ctjs.set_property %seed[%zero], %zero\n";
    run({.what = "a saved Boolean stride survives replacement in its source array",
         .body = replace(carriedBoolean, makeBoolean, savedBoolean),
         .arrays = "a:[one,two,three]; seed:[zero]",
         .reads = "seed[0]=truth; a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    reject("a repeated Boolean producer needs its own invariant proof",
           replace(replace(replace(computedUnit, makeUnit, savedBoolean),
                           "binary_static add %i, %unit", "binary add %i, %repeated"),
                   "  %step =", "  %repeated = ctjs.get_property %seed[%zero]\n  %step ="));
    for (const std::string opcode : {"binary_static", "binary"}) {
        const auto commuted =
            replace(savedChild, "binary_static add %i, %one", opcode + " add %one, %i");
        run({.what = "commuted Number Add retains the exact returned child",
             .body = commuted,
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "commuted Number Add discharges only unreturned children",
             .body = replace(commuted, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        run({.what = "commuted Number Add preserves a zero-trip saved child",
             .body = replace(commuted, "^header(%a, %zero, %zero", "^header(%a, %two, %x"),
             .arrays = "a:[one,x]",
             .exit = "x -> {x}"});
        const auto carried =
            replace(carriedStride, "binary_static add %i, %d", opcode + " add %d, %i");
        run({.what = "commuted held strides preserve exact backedge transport and overshoot",
             .body = carried,
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one; a[2]=three",
             .exit = "added -> {}"});
        reject("a commuted carried stride must remain unchanged",
               replace(carried, "^header(%base, %step, %added, %d",
                       "^header(%base, %step, %added, %one"));
        reject("two induction operands cannot supply an invariant positive stride",
               replace(commuted, "add %one, %i", "add %i, %i"));
        reject("commuted Add still requires the original induction operand",
               replace(commuted, "add %one, %i", "add %one, %s"));
        reject("commuted Add cannot borrow a String stride",
               replace(commuted, "#ctjs.number<4607182418800017408>", "#ctjs.string<\"1\">"));
        reject("commuted Add cannot borrow a BigInt stride",
               replace(commuted, "#ctjs.number<4607182418800017408>", "#ctjs.bigint<\"1\">"));
        reject("commuted Add still bounds its final update",
               replace(replace(maxStride, "binary_static add %i, %max", opcode + " add %max, %i"),
                       "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    }
    negativeLiteral = "#ctjs.number<13830554455654793216>";
    subtract = "  %minus = ctjs.constant " + negativeLiteral + "\n" +
               replace(savedChild, "binary_static add %i, %one", "binary sub %i, %minus");
    const std::string subtractNegated =
        replace(replace(subtract, "  %minus = ctjs.constant " + negativeLiteral + "\n", ""),
                "  %step =", "  %minus = ctjs.unary neg %one\n  %step =");
    for (const std::string & source : {subtract, subtractNegated}) {
        run({.what = "subtracting an original negative Number retains the exact returned child",
             .body = source,
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "negative Number subtraction discharges only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        run({.what = "negative subtraction preserves a zero-trip saved child",
             .body = replace(source, "^header(%a, %zero, %zero", "^header(%a, %two, %x"),
             .arrays = "a:[one,x]",
             .exit = "x -> {x}"});
    }
    const auto carriedSubtract =
        replace(replace(carriedUnit, makeUnit,
                        "  %unit = ctjs.constant #ctjs.number<13835058055282163712>\n"),
                "binary_static add %i, %d", "binary sub %i, %d");
    run({.what = "held negative strides preserve exact backedge transport and overshoot",
         .body = carriedSubtract,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    run({.what = "negative subtraction preserves its exact final index after array growth",
         .body = replace(replace(carriedSubtract, "^exit(%sum :", "^exit(%index :"),
                         "  ctjs.return %result",
                         "  ctjs.append %zero to %a\n  ctjs.append %zero to %a\n"
                         "  %after = ctjs.get_property %a[%result]\n  ctjs.return %after"),
         .arrays = "a:[one,two,three,zero,zero]",
         .reads = "a[0]=one; a[2]=three; a[4]=zero",
         .exit = "zero -> {}"});
    reject("a negative carried stride must remain unchanged",
           replace(carriedSubtract, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    reject("subtraction cannot commute its induction operand",
           replace(subtract, "sub %i, %minus", "sub %minus, %i"));
    reject("subtraction cannot borrow an opaque negative stride",
           replace(subtract, "sub %i, %minus", "sub %i, %p"));
    run({.what = "a repeated subtraction proves its fixed negative stride",
         .body = replace(subtractNegated, "unary neg %one", "binary sub %zero, %one"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "repeated original String Neg supplies its invariant literal stride",
         .body =
             replace(subtractNegated, "#ctjs.number<4607182418800017408>", "#ctjs.string<\"1\">"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    for (const std::string constant :
         {"#ctjs.number<0>", "#ctjs.number<4607182418800017408>",
          "#ctjs.number<13826050856027422720>", "#ctjs.number<13974669643730649088>",
          "#ctjs.number<18442240474082181120>", "#ctjs.number<9221120237041090560>",
          "#ctjs.string<\"-1\">", "#ctjs.bigint<\"-1\">"}) {
        const auto body = replace(subtract, negativeLiteral, constant);
        if (constant == "#ctjs.string<\"-1\">") {
            run({.what = "original negative String Sub latches preserve returned children",
                 .body = body,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
        } else {
            reject("subtraction requires a bounded negative integral conversion", body);
        }
    }
    const auto stringSubtract = replace(subtract, negativeLiteral, "#ctjs.string<\"-1\">");
    run({.what = "String Sub latches discharge only unreturned children",
         .body = replace(stringSubtract, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    run({.what = "body-local String literals are invariant Sub latches",
         .body =
             replace(replace(stringSubtract, "  %minus = ctjs.constant #ctjs.string<\"-1\">\n", ""),
                     "  %step =", "  %minus = ctjs.constant #ctjs.string<\"-1\">\n  %step ="),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "held String Sub latches preserve exact backedge transport",
         .body =
             replace(carriedSubtract, "#ctjs.number<13835058055282163712>", "#ctjs.string<\"-2\">"),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    for (const std::string text : {"-0", "-01", "-1.0", " -1", "-1 ", "-4294967296"}) {
        reject("String Sub latches require bounded canonical decimal spelling",
               replace(stringSubtract, "#ctjs.string<\"-1\">", "#ctjs.string<\"" + text + "\">"));
    }
    reject("String Sub latches preserve original property keys",
           replace(stringSubtract, "%base[%i]", "%base[%minus]"),
           ArrayContentsFailure::UnknownIndex);
    reject("String latches cannot borrow Sub conversion for Add concatenation",
           replace(stringSubtract, "sub %i, %minus", "add %i, %minus"));
    reject("String Sub still bounds its exact final update",
           replace(replace(stringSubtract, "#ctjs.string<\"-1\">", "#ctjs.string<\"-4294967295\">"),
                   "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    reject("negative subtraction still bounds the final update before replay",
           replace(replace(subtract, negativeLiteral, "#ctjs.number<13974669643728551936>"),
                   "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    reject("a negative stride is not a nonnegative own array index",
           replace(subtract, "%base[%i]", "%base[%minus]"), ArrayContentsFailure::UnknownIndex);
    makeNegative = "  %magnitude = ctjs.binary add %one, %one\n"
                   "  %unit = ctjs.unary neg %magnitude\n";
    carriedNegative = replace(replace(carriedUnit, makeUnit, makeNegative),
                              "binary_static add %i, %d", "binary sub %i, %d");
    run({.what = "Neg of a held bounded Number preserves its exact CFG stride",
         .body = carriedNegative,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    run({.what = "a held Neg stride preserves its final overshoot after array growth",
         .body = replace(replace(carriedNegative, "^exit(%sum :", "^exit(%index :"),
                         "  ctjs.return %result",
                         "  ctjs.append %zero to %a\n  ctjs.append %zero to %a\n"
                         "  %after = ctjs.get_property %a[%result]\n  ctjs.return %after"),
         .arrays = "a:[one,two,three,zero,zero]",
         .reads = "a[0]=one; a[2]=three; a[4]=zero",
         .exit = "zero -> {}"});
    run({.what = "a held Neg stride keeps its source length snapshot after shrink",
         .body = replace(carriedNegative, makeNegative,
                         "  %seed = ctjs.create_array [%one, %one] {storage_test_id = \"seed\"}\n"
                         "  %name = ctjs.constant #ctjs.string<\"length\">\n"
                         "  %magnitude = ctjs.get_property %seed[%name]\n"
                         "  %unit = ctjs.unary neg %magnitude\n"
                         "  ctjs.set_property %seed[%name], %zero\n"),
         .arrays = "a:[one,two,three]; seed:[]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    makeNegativeUnit = "  %magnitude = ctjs.unary plus %one\n"
                       "  %minus = ctjs.unary neg %magnitude\n";
    negativeChild =
        replace(replace(savedChild, "  cf.br ^header", makeNegativeUnit + "  cf.br ^header"),
                "binary_static add %i, %one", "binary sub %i, %minus");
    run({.what = "a computed Neg stride retains the original final child",
         .body = negativeChild,
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "a computed Neg stride discharges only the unreturned child",
         .body = replace(negativeChild, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    run({.what = "a zero-trip Neg stride preserves the original saved child",
         .body = replace(negativeChild, "^header(%a, %zero, %zero", "^header(%a, %two, %x"),
         .arrays = "a:[one,x]",
         .exit = "x -> {x}"});
    reject("a held Neg stride must remain identical across the CFG backedge",
           replace(carriedNegative, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    run({.what = "a repeated Neg retains its invariant computed Number operand",
         .body = replace(replace(negativeChild, "  %minus = ctjs.unary neg %magnitude\n", ""),
                         "  %step =", "  %minus = ctjs.unary neg %magnitude\n  %step ="),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    reject("a negative snapshot is not an own array index",
           replace(negativeChild, "%base[%i]", "%base[%minus]"),
           ArrayContentsFailure::UnknownIndex);
    reject("Add cannot borrow a negative magnitude as a positive stride",
           replace(negativeChild, "binary sub %i, %minus", "binary add %i, %minus"));
    for (const std::string constant :
         {"#ctjs.number<0>", "#ctjs.number<13830554455654793216>",
          "#ctjs.number<4602678819172646912>", "#ctjs.number<4751297606875873280>",
          "#ctjs.number<9218868437227405312>", "#ctjs.number<9221120237041090560>",
          "#ctjs.string<\"1\">", "#ctjs.bigint<\"1\">"}) {
        const auto body = replace(negativeChild, "#ctjs.number<4607182418800017408>", constant);
        if (constant == "#ctjs.string<\"1\">") {
            run({.what = "canonical String Plus then Neg supplies an exact negative stride",
                 .body = body,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
        } else {
            reject("a computed Neg stride requires a bounded strictly positive Number input", body);
        }
    }
    reject("Neg of an unknown producer cannot supply a bounded stride",
           replace(negativeChild, "unary neg %magnitude", "unary neg %p"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("a held Neg stride cannot overflow on its final update",
           replace(replace(negativeChild, "  %magnitude = ctjs.unary plus %one",
                           "  %maximum = ctjs.constant #ctjs.number<4751297606873776128>\n"
                           "  %magnitude = ctjs.unary plus %maximum"),
                   "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    const std::string negativeText = "  %text = ctjs.constant #ctjs.string<\"-1\">\n";
    for (const auto & [operation, expected] :
         {std::pair{"unary plus %text", "13830554455654793216"},
          std::pair{"unary neg %text", "4607182418800017408"},
          std::pair{"unary bitnot %text", "0"},
          std::pair{"binary sub %text, %zero", "13830554455654793216"},
          std::pair{"binary sub %zero, %text", "4607182418800017408"},
          std::pair{"binary sub %text, %text", "0"},
          std::pair{"binary mul %text, %one", "13830554455654793216"},
          std::pair{"binary mul %one, %text", "13830554455654793216"},
          std::pair{"binary div %text, %one", "13830554455654793216"},
          std::pair{"binary div %one, %text", "13830554455654793216"},
          std::pair{"binary mod %text, %one", "0"},
          std::pair{"binary mod %zero, %text", "0"},
          std::pair{"binary pow %text, %three", "13830554455654793216"},
          std::pair{"binary pow %text, %two", "4607182418800017408"},
          std::pair{"binary pow %one, %text", "4607182418800017408"},
          std::pair{"binary_static bitand %text, %one", "4607182418800017408"},
          std::pair{"binary_static bitor %zero, %text", "13830554455654793216"},
          std::pair{"binary_static bitxor %text, %text", "0"},
          std::pair{"binary_static shl %text, %one", "13835058055282163712"},
          std::pair{"binary_static shr %text, %one", "13830554455654793216"},
          std::pair{"binary_static ushr %text, %text", "4607182418800017408"}}) {
        run({.what = "negative canonical String conversion preserves exact operator results",
             .body = prefix + negativeText + "  %actual = ctjs." + operation +
                     " {storage_test_id = \"actual\"}\n"
                     "  %expected = ctjs.constant #ctjs.number<" +
                     expected +
                     ">\n  %index = ctjs.binary sub %actual, %expected\n"
                     "  %read = ctjs.get_property %a[%index]\n  ctjs.return %actual\n",
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one",
             .exit = "actual -> {}"});
    }
    const auto savedNegativeText = replace(
        negativeChild, makeNegativeUnit,
        negativeText + "  %inputs = ctjs.create_array [%text] {storage_test_id = \"inputs\"}\n"
                       "  %saved = ctjs.get_property %inputs[%zero]\n"
                       "  ctjs.set_property %inputs[%zero], %x\n"
                       "  %minus = ctjs.unary plus %saved\n");
    run({.what = "negative String conversion uses the saved input before container mutation",
         .body = savedNegativeText,
         .arrays = "a:[one,x]; inputs:[x]",
         .reads = "inputs[0]=ctjs.constant; a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "negative String conversion discharges only unreturned saved children",
         .body = replace(savedNegativeText, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]; inputs:[x]",
         .reads = "inputs[0]=ctjs.constant; a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    const auto carriedNegativeText =
        replace(carriedNegative, makeNegative, negativeText + "  %unit = ctjs.unary plus %text\n");
    run({.what = "converted negative Strings preserve simultaneous CFG transport",
         .body = carriedNegativeText,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    reject("a converted negative String cannot change on the CFG backedge",
           replace(carriedNegativeText, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    run({.what = "a repeated negative String conversion retains its saved primitive operand",
         .body = replace(replace(savedNegativeText, "  %minus = ctjs.unary plus %saved\n", ""),
                         "  %step =", "  %minus = ctjs.unary plus %saved\n  %step ="),
         .arrays = "a:[one,x]; inputs:[x]",
         .reads = "inputs[0]=ctjs.constant; a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    reject("the original negative String keeps its property key after conversion",
           replace(savedNegativeText, "%base[%i]", "%base[%saved]"),
           ArrayContentsFailure::UnknownIndex);
    reject("negative String Add retains concatenation rather than a Number magnitude",
           replace(savedNegativeText, "unary plus %saved", "binary add %saved, %zero"));
    reject("negative String conversion cannot borrow an unknown saved input",
           replace(savedNegativeText, "unary plus %saved", "unary plus %p"),
           ArrayContentsFailure::UnsupportedOperation);
    for (const std::string text :
         {"-0", "-01", "--1", "-+1", "-1.0", "-1e0", " -1", "-1 ", "-0x1", "-4294967296"}) {
        reject(
            "negative String conversion requires bounded canonical decimal digits",
            replace(savedNegativeText, "#ctjs.string<\"-1\">", "#ctjs.string<\"" + text + "\">"));
    }
    run({.what = "negative canonical String conversion includes the exact magnitude bound",
         .body = prefix + "  %text = ctjs.constant #ctjs.string<\"-4294967295\">\n"
                          "  %expected = ctjs.constant #ctjs.number<13974669643728551936>\n"
                          "  %actual = ctjs.unary plus %text {storage_test_id = \"actual\"}\n"
                          "  %index = ctjs.binary sub %actual, %expected\n"
                          "  %read = ctjs.get_property %a[%index]\n  ctjs.return %actual\n",
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one",
         .exit = "actual -> {}"});
    for (const std::string literal :
         {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
        for (const std::string kind : {"plus", "neg", "bitnot"}) {
            const bool truth = literal == "#ctjs.boolean<true>";
            const std::string expected =
                kind == "bitnot" ? (truth ? "13835058055282163712" : "13830554455654793216")
                : !truth         ? "0"
                : kind == "plus" ? "4607182418800017408"
                                 : "13830554455654793216";
            run({.what = "original Boolean/null unary results keep their exact Number magnitude",
                 .body = prefix + "  %input = ctjs.constant " + literal +
                         "\n  %snapshot = ctjs.unary " + kind +
                         " %input\n  %expected = ctjs.constant #ctjs.number<" + expected +
                         ">\n  %index = ctjs.binary sub %snapshot, %expected\n"
                         "  %read = ctjs.get_property %a[%index]\n  ctjs.return %read\n",
                 .arrays = "a:[one,two,three]",
                 .reads = "a[0]=one",
                 .exit = "one -> {}"});
        }
    }
    for (const auto & [literal, kind] :
         {std::pair{"#ctjs.boolean<true>", "plus"}, std::pair{"#ctjs.boolean<true>", "neg"},
          std::pair{"#ctjs.boolean<false>", "bitnot"}, std::pair{"#ctjs.null", "bitnot"}}) {
        const std::string operation = "  %minus = ctjs.unary " + std::string{kind} + " %input\n";
        const auto source =
            replace(negativeChild, makeNegativeUnit,
                    "  %input = ctjs.constant " + std::string{literal} + "\n" + operation);
        const auto body = std::string{kind} == "plus"
                              ? replace(source, "binary sub %i, %minus", "binary add %i, %minus")
                              : source;
        run({.what = "original Boolean/null unary strides retain the final CFG child",
             .body = body,
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "Boolean/null unary strides discharge only unreturned children",
             .body = replace(body, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        const auto repeated =
            replace(replace(body, operation, ""), "  %step =", operation + "  %step =");
        run({.what = "repeated primitive unary uses its original literal stride",
             .body = repeated,
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        reject("a converted primitive does not turn its original Boolean/null into an index",
               replace(body, "%base[%i]", "%base[%input]"), ArrayContentsFailure::UnknownIndex);
        reject("Undefined unary conversion supplies no bounded Number fact",
               replace(body, literal, "#ctjs.undefined"));
    }
    const std::string makeComplement = "  %operand = ctjs.unary plus %zero\n"
                                       "  %minus = ctjs.unary bitnot %operand\n";
    const auto complementChild = replace(negativeChild, makeNegativeUnit, makeComplement);
    const auto savedComplement =
        replace(complementChild, makeComplement,
                "  %seed = ctjs.create_array [] {storage_test_id = \"seed\"}\n"
                "  %name = ctjs.constant #ctjs.string<\"length\">\n"
                "  %operand = ctjs.get_property %seed[%name]\n"
                "  %minus = ctjs.unary bitnot %operand\n"
                "  ctjs.append %one to %seed\n");
    for (const auto & source : {complementChild, savedComplement}) {
        const char * arrays = source == complementChild ? "a:[one,x]" : "a:[one,x]; seed:[one]";
        run({.what = "BitNot keeps its exact signed Number snapshot after source growth",
             .body = source,
             .arrays = arrays,
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "BitNot snapshots discharge only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = arrays,
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    const auto carriedComplement = replace(carriedNegative, makeNegative,
                                           "  %magnitude = ctjs.unary plus %one\n"
                                           "  %unit = ctjs.unary bitnot %magnitude\n");
    run({.what = "BitNot preserves its exact negative magnitude through CFG transport",
         .body = carriedComplement,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    reject("a BitNot snapshot cannot change across the backedge",
           replace(carriedComplement, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    run({.what = "a repeated BitNot retains its saved Number operand",
         .body = replace(replace(complementChild, "  %minus = ctjs.unary bitnot %operand\n", ""),
                         "  %step =", "  %minus = ctjs.unary bitnot %operand\n  %step ="),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    reject("negative BitNot facts are never own indices",
           replace(complementChild, "%base[%i]", "%base[%minus]"),
           ArrayContentsFailure::UnknownIndex);
    reject("BitNot cannot supply a bounded Number from an unknown input",
           replace(complementChild, "bitnot %operand", "bitnot %p"),
           ArrayContentsFailure::UnsupportedOperation);
    for (const std::string constant :
         {"#ctjs.string<\"0\">", "#ctjs.string<\"00\">", "#ctjs.string<\"-1\">",
          "#ctjs.string<\"1.0\">", "#ctjs.string<\"4294967295\">", "#ctjs.bigint<\"0\">",
          "#ctjs.number<4602678819172646912>", "#ctjs.number<4751297606875873280>",
          "#ctjs.number<13974669643730649088>", "#ctjs.number<9218868437227405312>",
          "#ctjs.number<9221120237041090560>"}) {
        const auto body =
            replace(complementChild, "ctjs.unary plus %zero", "ctjs.constant " + constant);
        if (constant == "#ctjs.string<\"0\">") {
            run({.what = "the original canonical String BitNot keeps its negative CFG stride",
                 .body = body,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "canonical String BitNot discharges only unreturned CFG children",
                 .body = replace(body, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
            run({.what = "repeated canonical String BitNot uses its original literal stride",
                 .body = replace(replace(body, "  %minus = ctjs.unary bitnot %operand\n", ""),
                                 "  %step =", "  %minus = ctjs.unary bitnot %operand\n  %step ="),
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
        } else {
            reject("BitNot requires bounded Numbers or canonical original Strings", body);
        }
    }
    const auto carriedStringComplement =
        replace(carriedComplement, "ctjs.unary plus %one", "ctjs.constant #ctjs.string<\"1\">");
    run({.what = "canonical String BitNot survives reordered CFG transport",
         .body = carriedStringComplement,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    reject("a String BitNot snapshot cannot change across the CFG backedge",
           replace(carriedStringComplement, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    // Subtracting the expected signed result must produce index zero. This
    // checks the full magnitude at both ToInt32 boundaries, not just its sign.
    for (const auto & [input, output] :
         {std::pair{"0", "13830554455654793216"},
          std::pair{"9223372036854775808", "13830554455654793216"},
          std::pair{"13830554455654793216", "0"},
          std::pair{"13835058055282163712", "4607182418800017408"},
          std::pair{"4746794007244308480", "13970166044103278592"},
          std::pair{"4746794007248502784", "4746794007244308480"},
          std::pair{"4751297606873776128", "0"},
          std::pair{"13970166044103278592", "4746794007244308480"},
          std::pair{"13974669643728551936", "13835058055282163712"}}) {
        run({.what = "BitNot preserves exact ToInt32 boundary values and its result identity",
             .body = prefix + "  %operand = ctjs.constant #ctjs.number<" + input +
                     ">\n"
                     "  %expected = ctjs.constant #ctjs.number<" +
                     output +
                     ">\n"
                     "  %actual = ctjs.unary bitnot %operand {storage_test_id = \"actual\"}\n"
                     "  %index = ctjs.binary sub %actual, %expected\n"
                     "  %read = ctjs.get_property %a[%index]\n  ctjs.return %actual\n",
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one",
             .exit = "actual -> {}"});
    }
    for (const std::string kind : {"bitand", "bitor", "bitxor", "shl", "shr"}) {
        const std::string right = kind == "bitand" ? "%operand" : "%zero";
        const std::string operation =
            "  %minus = ctjs.binary_static " + kind + " %operand, " + right + "\n";
        const auto snapshot =
            replace(negativeChild, makeNegativeUnit,
                    "  %seed = ctjs.create_array [%one] {storage_test_id = \"seed\"}\n"
                    "  %name = ctjs.constant #ctjs.string<\"length\">\n"
                    "  %magnitude = ctjs.get_property %seed[%name]\n"
                    "  %operand = ctjs.unary neg %magnitude\n" +
                        operation + "  ctjs.set_property %seed[%name], %zero\n");
        run({.what = "signed bitwise snapshots retain their child after source length shrink",
             .body = snapshot,
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "signed bitwise snapshots discharge only unreturned children",
             .body = replace(snapshot, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        const auto carried = replace(carriedNegative, makeNegative,
                                     "  %operand = ctjs.unary neg %two\n" +
                                         replace(operation, "%minus =", "%unit ="));
        run({.what = "signed bitwise magnitudes survive exact CFG transport",
             .body = carried,
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one; a[2]=three",
             .exit = "added -> {}"});
        reject("signed bitwise snapshots must remain identical on the backedge",
               replace(carried, "^header(%base, %step, %added, %d",
                       "^header(%base, %step, %added, %one"));
        run({.what = "repeated bitwise producers preserve the original pre-shrink operand",
             .body =
                 replace(replace(snapshot, operation, ""), "  %step =", operation + "  %step ="),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        reject("negative bitwise facts never become own indices",
               replace(snapshot, "%base[%i]", "%base[%minus]"), ArrayContentsFailure::UnknownIndex);
        reject("signed bitwise snapshots cannot borrow an unknown operand",
               replace(snapshot, kind + " %operand", kind + " %p"),
               ArrayContentsFailure::UnknownValue);
        for (const std::string input :
             {"#ctjs.string<\"-1\">", "#ctjs.bigint<\"-1\">", "#ctjs.number<4602678819172646912>",
              "#ctjs.number<4751297606875873280>", "#ctjs.number<13974669643730649088>",
              "#ctjs.number<9218868437227405312>", "#ctjs.number<9221120237041090560>"}) {
            const auto body =
                replace(snapshot, "ctjs.unary neg %magnitude", "ctjs.constant " + input);
            if (input == "#ctjs.string<\"-1\">") {
                run({.what = "negative canonical Strings share exact signed bitwise conversion",
                     .body = body,
                     .arrays = "a:[one,x]; seed:[]",
                     .reads = "a[0]=one; a[1]=x",
                     .exit = "x -> {x}"});
            } else {
                reject("signed bitwise inputs require exact bounded primitive conversion", body);
            }
        }
    }
    struct bitwise_case {
        const char * kind;
        const char * left;
        const char * right;
        const char * expected;
    };
    for (const auto & [kind, left, right, expected] :
         {bitwise_case{"bitand", "13830554455654793216", "13835058055282163712",
                       "13835058055282163712"}, // -1 & -2 = -2
          bitwise_case{"bitand", "13974669643728551936", "13830554455654793216",
                       "4607182418800017408"}, // -UINT32_MAX & -1 = 1
          bitwise_case{"bitand", "4746794007248502784", "4751297606873776128",
                       "13970166044103278592"}, // 2^31 & UINT32_MAX = -2^31
          bitwise_case{"bitor", "13970166044103278592", "4746794007244308480",
                       "13830554455654793216"}, // -2^31 | INT32_MAX = -1
          bitwise_case{"bitor", "13974669643728551936", "0", "4607182418800017408"},
          bitwise_case{"bitxor", "13830554455654793216", "13835058055282163712",
                       "4607182418800017408"}, // -1 ^ -2 = 1
          bitwise_case{"bitxor", "13970166044103278592", "4751297606873776128",
                       "4746794007244308480"},
          bitwise_case{"bitxor", "9223372036854775808", "0", "0"},
          bitwise_case{"shl", "13830554455654793216", "13853072453791645696",
                       "13830554455654793216"}, // -1 << -32 = -1
          bitwise_case{"shl", "13830554455654793216", "13830554455654793216",
                       "13970166044103278592"}, // -1 << -1 = -2^31
          bitwise_case{"shl", "13970166044103278592", "4607182418800017408", "0"},
          bitwise_case{"shl", "13974669643728551936", "13852790978814935040",
                       "4611686018427387904"}, // -UINT32_MAX << -31 = 2
          bitwise_case{"shl", "4751297606873776128", "4607182418800017408", "13835058055282163712"},
          bitwise_case{"shr", "13830554455654793216", "13830554455654793216",
                       "13830554455654793216"}, // -1 >> -1 = -1
          bitwise_case{"shr", "13837309855095848960", "4607182418800017408",
                       "13835058055282163712"}, // -3 >> 1 = -2
          bitwise_case{"shr", "4746794007248502784", "4629700416936869888",
                       "13970166044103278592"}, // 2^31 >> 32 = -2^31
          bitwise_case{"shr", "4751297606873776128", "0", "13830554455654793216"},
          bitwise_case{"shr", "13974669643728551936", "13853072453791645696",
                       "4607182418800017408"}, // -UINT32_MAX >> -32 = 1
          bitwise_case{"shr", "9223372036854775808", "13830554455654793216", "0"},
          bitwise_case{"ushr", "13830554455654793216", "0", "4751297606873776128"},
          bitwise_case{"ushr", "13830554455654793216", "13830554455654793216",
                       "4607182418800017408"}, // -1 >>> -1 = 1
          bitwise_case{"ushr", "13970166044103278592", "13853072453791645696",
                       "4746794007248502784"}, // -2^31 >>> -32 = 2^31
          bitwise_case{"ushr", "13974669643728551936", "13852790978814935040", "0"},
          bitwise_case{"ushr", "9223372036854775808", "13830554455654793216", "0"}}) {
        run({.what = "signed bitwise snapshots retain exact ToInt32 bits and result identity",
             .body = prefix + "  %left = ctjs.constant #ctjs.number<" + left +
                     ">\n  %right = ctjs.constant #ctjs.number<" + right +
                     ">\n  %expected = ctjs.constant #ctjs.number<" + expected +
                     ">\n  %actual = ctjs.binary_static " + kind +
                     " %left, %right {storage_test_id = \"actual\"}\n"
                     "  %index = ctjs.binary sub %actual, %expected\n"
                     "  %read = ctjs.get_property %a[%index]\n  ctjs.return %actual\n",
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one",
             .exit = "actual -> {}"});
    }
    struct primitive_bitwise_case {
        const char * kind;
        const char * trueLeft;
        const char * zeroLeft;
        const char * trueRight;
        const char * zeroRight;
    };
    for (const auto & [kind, trueLeft, zeroLeft, trueRight, zeroRight] :
         {primitive_bitwise_case{"bitand", "4607182418800017408", "0", "4607182418800017408", "0"},
          primitive_bitwise_case{"bitor", "13830554455654793216", "13830554455654793216",
                                 "13830554455654793216", "13830554455654793216"},
          primitive_bitwise_case{"bitxor", "13835058055282163712", "13830554455654793216",
                                 "13835058055282163712", "13830554455654793216"},
          primitive_bitwise_case{"shl", "13970166044103278592", "0", "13835058055282163712",
                                 "13830554455654793216"},
          primitive_bitwise_case{"shr", "0", "0", "13830554455654793216", "13830554455654793216"},
          primitive_bitwise_case{"ushr", "0", "0", "4746794007244308480", "4751297606873776128"}}) {
        for (const std::string literal :
             {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
            for (const bool commuted : {false, true}) {
                const std::string expected = literal == "#ctjs.boolean<true>"
                                                 ? (commuted ? trueRight : trueLeft)
                                                 : (commuted ? zeroRight : zeroLeft);
                run({.what =
                         "Boolean/null bitwise operands retain exact signed and unsigned results",
                     .body = prefix + "  %input = ctjs.constant " + literal +
                             "\n  %negative = ctjs.unary neg %one\n"
                             "  %actual = ctjs.binary_static " +
                             kind + " " + (commuted ? "%negative, %input" : "%input, %negative") +
                             " {storage_test_id = \"actual\"}\n"
                             "  %expected = ctjs.constant #ctjs.number<" +
                             expected +
                             ">\n  %index = ctjs.binary sub %actual, %expected\n"
                             "  %read = ctjs.get_property %a[%index]\n  ctjs.return %actual\n",
                     .arrays = "a:[one,two,three]",
                     .reads = "a[0]=one",
                     .exit = "actual -> {}"});
            }
        }
    }
    for (const auto & [literal, kind] :
         {std::pair{"#ctjs.boolean<true>", "bitand"}, std::pair{"#ctjs.boolean<false>", "bitor"},
          std::pair{"#ctjs.null", "bitxor"}}) {
        const std::string input = "  %input = ctjs.constant " + std::string(literal) + "\n";
        const std::string operation =
            "  %unit = ctjs.binary_static " + std::string(kind) + " %input, %one\n";
        const auto source = replace(computedUnitChild, makeUnit, input + operation);
        const auto saved =
            replace(source, operation,
                    "  %holder = ctjs.create_array [%input] {storage_test_id = \"holder\"}\n"
                    "  %savedOperand = ctjs.get_property %holder[%zero]\n"
                    "  ctjs.set_property %holder[%zero], %x\n" +
                        replace(operation, "%input,", "%savedOperand,"));
        run({.what = "saved primitive bitwise operands survive replacement and retain CFG children",
             .body = saved,
             .arrays = "a:[one,x]; holder:[x]",
             .reads = "holder[0]=ctjs.constant; a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "primitive bitwise snapshots discharge only unreturned CFG children",
             .body = replace(saved, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; holder:[x]",
             .reads = "holder[0]=ctjs.constant; a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        const auto carried = replace(carriedUnit, makeUnit, input + operation);
        run({.what = "primitive bitwise snapshots survive exact CFG backedge transport",
             .body = carried,
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one; a[1]=two; a[2]=three",
             .exit = "added -> {}"});
        reject("primitive bitwise snapshots cannot change across CFG backedges",
               replace(carried, "^header(%base, %step, %added, %d",
                       "^header(%base, %step, %added, %zero"));
        reject("primitive bitwise conversion cannot turn its original operand into an own key",
               replace(saved, "%base[%i]", "%base[%savedOperand]"),
               ArrayContentsFailure::UnknownIndex);
        run({.what = "repeated primitive bitwise producers retain their original operand",
             .body = replace(replace(source, operation, ""), "  %step =", operation + "  %step ="),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        reject("primitive bitwise operands cannot borrow an unknown value",
               replace(source, operation, replace(operation, "%input,", "%p,")),
               ArrayContentsFailure::UnknownValue);
        reject("Undefined cannot borrow primitive bitwise Number evidence",
               replace(source, literal, "#ctjs.undefined"));
    }
    const std::string unsignedOperation = "  %count = ctjs.unary neg %one\n"
                                          "  %unit = ctjs.binary_static ushr %operand, %count\n";
    const auto unsignedCarried =
        replace(carriedUnit, makeUnit, "  %operand = ctjs.unary neg %one\n" + unsignedOperation);
    run({.what = "unsigned shifts preserve negative operands and counts through CFG transport",
         .body = unsignedCarried,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    const auto unsignedSaved =
        replace(replace(savedUnit, "  %unit = ctjs.get_property %seed[%name]\n",
                        "  %magnitude = ctjs.get_property %seed[%name]\n"
                        "  %operand = ctjs.unary neg %magnitude\n" +
                            unsignedOperation),
                "ctjs.append %one to %seed", "ctjs.set_property %seed[%name], %zero");
    run({.what = "unsigned shifts retain signed read-time Numbers after source shrink",
         .body = unsignedSaved,
         .arrays = "a:[one,two,three]; seed:[]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    reject("an unsigned shift snapshot cannot change on a CFG backedge",
           replace(unsignedCarried, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %two"));
    reject("an unsigned shift cannot borrow an unknown count",
           replace(unsignedCarried, "ushr %operand, %count", "ushr %operand, %p"),
           ArrayContentsFailure::UnknownValue);
    const auto stringShift =
        replace(unsignedCarried, "ctjs.unary neg %one",
                "ctjs.constant #ctjs.string<\"4294967294\"> {storage_test_id = \"text\"}");
    run({.what = "unsigned conversion preserves the original String through CFG transport",
         .body = replace(stringShift, "ctjs.return %result", "ctjs.return %operand"),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "text -> {}"});
    const auto stringCount =
        replace(replace(unsignedCarried, "  %count = ctjs.unary neg %one",
                        "  %count = ctjs.constant #ctjs.string<\"31\">"),
                "  %operand = ctjs.unary neg %one",
                "  %operand = ctjs.unary neg %one {storage_test_id = \"negative\"}");
    run({.what = "a String shift count preserves the original signed Number identity",
         .body = replace(stringCount, "ctjs.return %result", "ctjs.return %operand"),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "negative -> {}"});
    reject("String shift counts require canonical decimal evidence",
           replace(stringCount, "#ctjs.string<\"31\">", "#ctjs.string<\"031\">"));
}

} // namespace ctcompile::test::escape::arrays::induction_detail
