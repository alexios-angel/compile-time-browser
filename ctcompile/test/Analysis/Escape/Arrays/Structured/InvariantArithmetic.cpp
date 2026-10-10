#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::structured_detail {

void StructuredCases::invariantArithmetic() {
    const auto dynamic = replace(original, "binary_static add %i, %one", "binary add %i, %one");
    rows.push_back({.what = "structured dynamic Add retains the exact returned child",
                    .body = dynamic,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    reject("structured dynamic Add cannot concatenate a String stride",
           replace(dynamic, "#ctjs.number<4607182418800017408>", "#ctjs.string<\"1\">"));
    reject("structured dynamic Add cannot borrow an unknown stride",
           replace(dynamic, "binary add %i, %one", "binary add %i, %p"));
    reject("structured dynamic Add still excludes zero strides",
           replace(dynamic, "binary add %i, %one", "binary add %i, %zero"));
    const auto commuted = replace(dynamic, "binary add %i, %one", "binary add %one, %i");
    rows.push_back({.what = "structured commuted Add preserves reordered induction transport",
                    .body = commuted,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "structured commuted Add discharges unreturned children",
                    .body = replace(commuted, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    const auto commutedStride = replace(carriedStride, "add %i, %d", "add %d, %i");
    rows.push_back({.what = "structured commuted static Add preserves a held positive stride",
                    .body = commutedStride,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    reject("a commuted structured stride cannot change on its backedge",
           replace(commutedStride, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    reject("commuted structured Add cannot concatenate its initial String index",
           replace(commuted, "#ctjs.number<0>", "#ctjs.string<\"0\">"));
    reject("commuted structured Add still requires its exact induction formal",
           replace(commuted, "add %one, %i", "add %one, %last"));
    const auto invariantProduct = replace(carriedUnit, "    %step = ctjs.binary_static add %i, %d",
                                          "    %product = ctjs.binary mul %d, %one\n"
                                          "    %step = ctjs.binary add %i, %product");
    for (const auto & source :
         {invariantProduct, replace(invariantProduct, "mul %d, %one", "mul %one, %d"),
          replace(invariantProduct, "mul %d, %one", "mul %unit, %d")}) {
        rows.push_back({.what = "product latches preserve reordered structured operand transport",
                        .body = source,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "invariant products release only unreturned structured children",
                        .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
    }
    reject("a structured product operand cannot change even to the same Number value",
           replace(invariantProduct, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    for (const std::string operands : {"%i, %one", "%one, %i", "%p, %one", "%one, %p"}) {
        reject("both structured product operands need invariant bounded values",
               replace(invariantProduct, "mul %d, %one", "mul " + operands));
    }
    rows.push_back({.what = "nested structured products prove original saved operands",
                    .body = replace(invariantProduct, "    %product = ctjs.binary mul %d, %one",
                                    "    %nested = ctjs.binary mul %d, %one\n"
                                    "    %product = ctjs.binary mul %one, %nested"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    for (const std::string literal :
         {"#ctjs.number<0>", "#ctjs.string<\"01\">", "#ctjs.bigint<\"1\">"}) {
        if (literal == "#ctjs.string<\"01\">") {
            rows.push_back(
                {.what =
                     "bounded decimal conversion preserves original reads and retained children",
                 .body = replace(invariantProduct, makeUnit,
                                 "  %unit = ctjs.constant " + literal + "\n"),
                 .arrays = "a:[x,y]",
                 .reads = "a[0]=x; a[1]=y",
                 .exit = "y -> {y}"});
        } else {
            reject(
                "structured product strides need exact nonzero primitive conversion",
                replace(invariantProduct, makeUnit, "  %unit = ctjs.constant " + literal + "\n"));
        }
    }
    const auto divisionProduct =
        replace(invariantProduct, makeUnit,
                "  %two = ctjs.constant #ctjs.number<4611686018427387904>\n" + makeUnit);
    for (const std::string operation : {"div", "mod"}) {
        const auto expression = operation + " %d, " + (operation == "div" ? "%one" : "%two");
        const auto source = replace(divisionProduct, "mul %d, %one", expression);
        for (const auto & body :
             {source,
              replace(replace(source, makeUnit, "  %unit = ctjs.constant #ctjs.string<\"-1\">\n"),
                      "binary add %i, %product", "binary sub %i, %product")}) {
            rows.push_back({.what = "invariant division preserves returned structured children",
                            .body = body,
                            .arrays = "a:[x,y]",
                            .reads = "a[0]=x; a[1]=y",
                            .exit = "y -> {y}"});
            rows.push_back({.what = "invariant division discharges only unreturned children",
                            .body = replace(body, "ctjs.return %result", "ctjs.return %zero"),
                            .arrays = "a:[x,y]",
                            .reads = "a[0]=x; a[1]=y",
                            .exit = "zero -> {}"});
        }
        reject("structured division requires original operand transport",
               replace(source, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
        for (const std::string operands : {"%d, %zero", "%p, %one", "%one, %i"}) {
            reject("structured division needs unchanged inputs and a nonzero divisor",
                   replace(source, expression, operation + " " + operands));
        }
        const auto nested = replace(source, "    %product = ctjs.binary " + expression,
                                    "    %nested = ctjs.binary div %d, %one\n"
                                    "    %product = ctjs.binary " +
                                        operation + " %nested, %two");
        if (operation == "div") {
            reject("a nested fractional quotient cannot certify integer induction", nested);
        } else {
            rows.push_back({.what = "nested division and remainder preserve exact signed values",
                            .body = nested,
                            .arrays = "a:[x,y]",
                            .reads = "a[0]=x; a[1]=y",
                            .exit = "y -> {y}"});
        }
    }
    reject("structured fractional quotients cannot certify integer induction",
           replace(divisionProduct, "mul %d, %one", "div %d, %two"));
    reject("structured zero remainders cannot certify termination",
           replace(divisionProduct, "mul %d, %one", "mod %d, %one"));
    for (const std::string expression :
         {"add %d, %zero", "add %zero, %d", "sub %d, %zero", "sub %two, %d", "sub %zero, %d"}) {
        auto source = replace(divisionProduct, "mul %d, %one", expression);
        if (expression == "sub %zero, %d") {
            source = replace(source, "binary add %i, %product", "binary sub %i, %product");
        }
        rows.push_back(
            {.what = "invariant Add/Sub preserves original signed operands and transport",
             .body = source,
             .arrays = "a:[x,y]",
             .reads = "a[0]=x; a[1]=y",
             .exit = "y -> {y}"});
        reject("Add/Sub operands retain their original backedge identity",
               replace(source, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
        reject("Add/Sub cannot borrow a changing operand",
               replace(source, expression, replace(expression, "%d", "%i")));
        reject("Add/Sub cannot borrow an unknown operand",
               replace(source, expression, replace(expression, "%d", "%p")));
        const auto string =
            replace(source, makeUnit, "  %unit = ctjs.constant #ctjs.string<\"1\">\n");
        if (expression.starts_with("add")) {
            reject("invariant Add cannot borrow numeric String conversion", string);
        } else {
            rows.push_back({.what = "invariant Sub converts each original canonical String",
                            .body = string,
                            .arrays = "a:[x,y]",
                            .reads = "a[0]=x; a[1]=y",
                            .exit = "y -> {y}"});
            rows.push_back(
                {.what =
                     "bounded decimal conversion preserves original reads and retained children",
                 .body = replace(string, "#ctjs.string<\"1\">", "#ctjs.string<\"01\">"),
                 .arrays = "a:[x,y]",
                 .reads = "a[0]=x; a[1]=y",
                 .exit = "y -> {y}"});
        }
    }
    reject("invariant addition cannot exceed the exact magnitude bound",
           replace(replace(invariantProduct, "mul %d, %one", "add %d, %one"), makeUnit,
                   "  %unit = ctjs.constant #ctjs.number<4751297606873776128>\n"));
    reject("invariant subtraction cannot certify a zero stride",
           replace(invariantProduct, "mul %d, %one", "sub %d, %d"));
    const auto invariantBits =
        replace(invariantProduct, "binary mul %d, %one", "binary_static bitand %d, %one");
    for (const std::string expression : {"bitand %d, %one", "bitor %d, %zero", "bitxor %d, %zero",
                                         "shl %d, %zero", "shr %d, %zero", "ushr %d, %zero"}) {
        const auto source = replace(invariantBits, "bitand %d, %one", expression);
        rows.push_back({.what = "bitwise latches preserve reordered structured operand transport",
                        .body = source,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "invariant bitwise latches discharge only unreturned children",
                        .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
        reject("structured bitwise latches preserve original backedge identity",
               replace(source, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
        reject("structured bitwise latches cannot borrow a changing index",
               replace(source, expression, replace(expression, "%d", "%i")));
    }
    const auto signedBits =
        replace(replace(invariantBits, makeUnit, "  %unit = ctjs.constant #ctjs.string<\"-1\">\n"),
                "bitand %d, %one", "shr %d, %d");
    for (const auto & source :
         {replace(signedBits, "binary add %i, %product", "binary sub %i, %product"),
          replace(signedBits, "shr %d, %d", "ushr %d, %d")}) {
        rows.push_back({.what = "structured signed shifts preserve String values and masked counts",
                        .body = source,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
    }
    const auto nestedBits =
        replace(invariantBits, "    %product = ctjs.binary_static bitand %d, %one",
                "    %inner = ctjs.binary sub %d, %zero\n"
                "    %product = ctjs.binary_static bitand %inner, %one");
    rows.push_back({.what = "structured bitwise latches retain two exact invariant layers",
                    .body = nestedBits,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "deeper structured bitwise latches preserve returned children",
                    .body = replace(nestedBits, "    %inner = ctjs.binary sub %d, %zero",
                                    "    %deep = ctjs.unary plus %d\n"
                                    "    %inner = ctjs.binary sub %deep, %zero"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    reject("structured bitwise latches cannot borrow a repeated property read",
           replace(nestedBits, "ctjs.binary sub %d, %zero", "ctjs.get_property %base[%zero]"));
    reject("structured bitwise latches cannot certify a zero stride",
           replace(invariantBits, "bitand %d, %one", "bitxor %d, %d"));
    reject("structured unsigned shifts still bound the final index",
           replace(replace(signedBits, "shr %d, %d", "ushr %d, %zero"), "%index = %zero",
                   "%index = %one"));
    for (const std::string literal : {"#ctjs.string<\"01\">", "#ctjs.bigint<\"1\">",
                                      "#ctjs.undefined", "#ctjs.number<4602678819172646912>"}) {
        if (literal == "#ctjs.string<\"01\">") {
            rows.push_back(
                {.what =
                     "bounded decimal conversion preserves original reads and retained children",
                 .body =
                     replace(invariantBits, makeUnit, "  %unit = ctjs.constant " + literal + "\n"),
                 .arrays = "a:[x,y]",
                 .reads = "a[0]=x; a[1]=y",
                 .exit = "y -> {y}"});
        } else {
            reject("structured bitwise latches require bounded original Number conversion",
                   replace(invariantBits, makeUnit, "  %unit = ctjs.constant " + literal + "\n"));
        }
    }
    const auto invariantPower = replace(invariantProduct, "mul %d, %one", "pow %d, %one");
    for (const auto & source :
         {invariantPower, replace(invariantPower, "pow %d, %one", "pow %one, %d"),
          replace(invariantPower, "pow %d, %one", "pow %unit, %d"),
          replace(
              replace(invariantPower, makeUnit, "  %unit = ctjs.constant #ctjs.string<\"-1\">\n"),
              "binary add %i, %product", "binary sub %i, %product")}) {
        rows.push_back({.what = "power latches preserve reordered structured operand transport",
                        .body = source,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "invariant powers discharge only unreturned structured children",
                        .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
    }
    reject("a structured power operand retains its original backedge identity",
           replace(invariantPower, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    for (const std::string operands :
         {"%i, %one", "%one, %i", "%p, %one", "%one, %p", "%zero, %one"}) {
        reject("structured powers need unchanged exact operands and a positive stride",
               replace(invariantPower, "pow %d, %one", "pow " + operands));
    }
    rows.push_back({.what = "nested structured powers prove their original invariant exponent",
                    .body = replace(invariantPower, "    %product = ctjs.binary pow %d, %one",
                                    "    %nested = ctjs.binary pow %d, %one\n"
                                    "    %product = ctjs.binary pow %one, %nested"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back(
        {.what = "bounded decimal conversion preserves original reads and retained children",
         .body =
             replace(invariantPower, makeUnit, "  %unit = ctjs.constant #ctjs.string<\"01\">\n"),
         .arrays = "a:[x,y]",
         .reads = "a[0]=x; a[1]=y",
         .exit = "y -> {y}"});
    reject("an invariant structured power still bounds the final index update",
           replace(replace(invariantPower, makeUnit,
                           "  %unit = ctjs.constant #ctjs.number<4751297606873776128>\n"),
                   "%index = %zero", "%index = %one"));
    for (const std::string unary : {"plus", "neg", "bitnot"}) {
        const auto source =
            replace(carriedUnit, "    %step = ctjs.binary_static add %i, %d",
                    "    %converted = ctjs.unary " + unary + " %d\n    %step = ctjs.binary " +
                        (unary == "plus" ? "add" : "sub") + " %i, %converted");
        rows.push_back({.what = "unary latches preserve reordered structured operand transport",
                        .body = source,
                        .arrays = "a:[x,y]",
                        .reads = unary == "bitnot" ? "a[0]=x" : "a[0]=x; a[1]=y",
                        .exit = unary == "bitnot" ? "x -> {x}" : "y -> {y}"});
        rows.push_back({.what = "invariant unary latches release only unreturned children",
                        .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]",
                        .reads = unary == "bitnot" ? "a[0]=x" : "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
        reject("a structured unary operand cannot change even to the same Number value",
               replace(source, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
        reject("a structured unary latch cannot borrow a changing induction operand",
               replace(source, unary + " %d", unary + " %i"));
        rows.push_back({.what = "nested structured unary conversions preserve saved primitives",
                        .body = replace(source, "    %converted = ctjs.unary " + unary + " %d",
                                        "    %repeated = ctjs.unary plus %d\n"
                                        "    %converted = ctjs.unary " +
                                            unary + " %repeated"),
                        .arrays = "a:[x,y]",
                        .reads = unary == "bitnot" ? "a[0]=x" : "a[0]=x; a[1]=y",
                        .exit = unary == "bitnot" ? "x -> {x}" : "y -> {y}"});
    }
    const auto nested = replace(invariantProduct, "    %product = ctjs.binary mul %d, %one",
                                "    %inner = ctjs.unary plus %d\n"
                                "    %product = ctjs.binary mul %inner, %one");
    for (const std::string expression :
         {"ctjs.unary plus %d", "ctjs.binary mul %d, %one", "ctjs.binary div %d, %one",
          "ctjs.binary pow %d, %one", "ctjs.binary add %d, %zero", "ctjs.binary sub %d, %zero"}) {
        const auto source = replace(nested, "ctjs.unary plus %d", expression);
        rows.push_back({.what = "mixed nested operations retain reordered structured snapshots",
                        .body = source,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "nested structured operations release only unreturned children",
                        .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
        reject("nested operands retain their original structured backedge identity",
               replace(source, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    }
    for (const std::string expression :
         {"ctjs.unary plus %i", "ctjs.unary plus %p", "ctjs.get_property %base[%zero]",
          "ctjs.binary div %d, %zero", "ctjs.binary mul %d, %zero"}) {
        reject("nested structured induction needs exact invariant operations and a positive stride",
               replace(nested, "ctjs.unary plus %d", expression));
    }
    const auto deeper =
        replace(nested, "    %inner = ctjs.unary plus %d",
                "    %deeper = ctjs.unary plus %d\n    %inner = ctjs.unary plus %deeper");
    rows.push_back({.what = "deeper structured induction retains the returned child",
                    .body = deeper,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "deeper structured induction releases only unreturned children",
                    .body = replace(deeper, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    reject("deeper structured induction cannot borrow a replaced snapshot",
           replace(deeper, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    for (const std::string literal : {"#ctjs.boolean<true>", "#ctjs.string<\"1\">"}) {
        for (const std::string unary : {"plus", "neg"}) {
            const auto source = replace(original, "    %step = ctjs.binary_static add %i, %one",
                                        "    %literal = ctjs.constant " + literal +
                                            "\n    %converted = ctjs.unary " + unary +
                                            " %literal\n    %step = ctjs.binary " +
                                            (unary == "neg" ? "sub" : "add") + " %i, %converted");
            rows.push_back({.what = "structured unary primitive latches keep original identities",
                            .body = source,
                            .arrays = "a:[x,y]",
                            .reads = "a[0]=x; a[1]=y",
                            .exit = "y -> {y}"});
            reject("structured unary parameters cannot borrow literal conversion",
                   replace(source, unary + " %literal", unary + " %p"));
            reject("a structured unary zero cannot certify progress",
                   replace(source, literal, "#ctjs.null"));
        }
    }
    for (const std::string literal : {"#ctjs.boolean<false>", "#ctjs.null", "#ctjs.string<\"0\">",
                                      "#ctjs.string<\"-2\">", "#ctjs.string<\"4294967294\">"}) {
        const bool subtract =
            literal != "#ctjs.string<\"-2\">" && literal != "#ctjs.string<\"4294967294\">";
        const std::string update = subtract ? "sub" : "add";
        const auto source =
            replace(original, "    %step = ctjs.binary_static add %i, %one",
                    "    %literal = ctjs.constant " + literal +
                        "\n    %converted = ctjs.unary bitnot %literal\n    %step = ctjs.binary " +
                        update + " %i, %converted");
        rows.push_back({.what = "structured literal BitNot latches preserve the returned child",
                        .body = source,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "structured literal BitNot releases only unreturned children",
                        .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
        reject("a structured BitNot parameter cannot borrow literal conversion",
               replace(source, "bitnot %literal", "bitnot %p"));
        rows.push_back({.what = "nested structured BitNot proves its original literal conversion",
                        .body = replace(source, "    %converted = ctjs.unary bitnot %literal",
                                        "    %computed = ctjs.unary plus %literal\n"
                                        "    %converted = ctjs.unary bitnot %computed"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        for (const std::string refused :
             {"#ctjs.string<\"-1\">", "#ctjs.string<\"4294967295\">", "#ctjs.string<\"00\">",
              "#ctjs.string<\"4294967296\">", "#ctjs.undefined"}) {
            if (subtract &&
                (refused == "#ctjs.string<\"00\">" || refused == "#ctjs.string<\"4294967296\">")) {
                rows.push_back({.what = "bounded decimal conversion preserves original reads and "
                                        "retained children",
                                .body = replace(source, literal, refused),
                                .arrays = "a:[x,y]",
                                .reads = "a[0]=x; a[1]=y",
                                .exit = "y -> {y}"});
            } else {
                reject("structured BitNot requires exact bounded nonzero literal conversion",
                       replace(source, literal, refused));
            }
        }
    }
    const std::string makeBoolean =
        "  %unit = ctjs.constant #ctjs.boolean<true> {storage_test_id = \"unit\"}\n";
    const auto boolean = replace(replace(carriedUnit, makeUnit, makeBoolean),
                                 "binary_static add %i, %d", "binary add %d, %i");
    rows.push_back({.what = "Boolean Add preserves reordered structured backedge transport",
                    .body = boolean,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "Boolean structured latches release only unreturned children",
                    .body = replace(boolean, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    rows.push_back({.what = "body-local Boolean literals independently prove Add progress",
                    .body = replace(original, "    %step = ctjs.binary_static add %i, %one",
                                    makeBoolean + "    %step = ctjs.binary add %i, %unit"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    reject("Boolean structured strides cannot change even to Number one",
           replace(boolean, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    for (const std::string constant :
         {"#ctjs.boolean<false>", "#ctjs.null", "#ctjs.undefined", "#ctjs.string<\"1\">"}) {
        reject("structured Add requires positive nonconcatenating primitive strides",
               replace(boolean, "#ctjs.boolean<true>", constant));
    }
    const auto alternateBoolean =
        replace(replace(alternateUnit, "ctjs.unary plus %one", "ctjs.constant #ctjs.boolean<true>"),
                "binary_static add %i, %unit", "binary add %i, %unit");
    rows.push_back({.what = "structured predecessors retain separate Boolean and Number strides",
                    .body = alternateBoolean,
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
                    .exit = "y -> {y}; y -> {y}"});
    reject("a structured unknown predecessor cannot borrow a Boolean stride",
           replace(alternateBoolean, "scf.yield %one :", "scf.yield %p :"));
    const std::string savedBoolean =
        "  %truth = ctjs.constant #ctjs.boolean<true> {storage_test_id = \"truth\"}\n"
        "  %seed = ctjs.create_array [%truth] {storage_test_id = \"seed\"}\n"
        "  %unit = ctjs.get_property %seed[%zero]\n"
        "  ctjs.set_property %seed[%zero], %zero\n";
    rows.push_back({.what = "structured Boolean latches retain the value before source replacement",
                    .body = replace(boolean, makeBoolean, savedBoolean),
                    .arrays = "a:[x,y]; seed:[zero]",
                    .reads = "seed[0]=truth; a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    reject("a repeated structured Boolean read needs an independent invariant proof",
           replace(replace(replace(computedUnit, makeUnit, savedBoolean),
                           "binary_static add %i, %unit", "binary add %i, %repeated"),
                   "    %step =", "    %repeated = ctjs.get_property %seed[%zero]\n    %step ="));
    const std::string negativeLiteral = "#ctjs.number<13830554455654793216>";
    const std::string subtract =
        "  %minus = ctjs.constant " + negativeLiteral + "\n" +
        replace(original, "binary_static add %i, %one", "binary sub %i, %minus");
    const std::string subtractNegated =
        replace(replace(subtract, "  %minus = ctjs.constant " + negativeLiteral + "\n", ""),
                "    %step =", "    %minus = ctjs.unary neg %one\n    %step =");
    for (const std::string & source : {subtract, subtractNegated}) {
        rows.push_back({.what = "structured negative subtraction preserves reordered aliases",
                        .body = source,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "structured negative subtraction discharges unreturned children",
                        .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
        rows.push_back({.what = "structured negative subtraction preserves zero-trip starts",
                        .body = replace(replace(source, "  ctjs.append %y to %a\n", ""),
                                        "%index = %zero", "%index = %one"),
                        .arrays = "a:[x]",
                        .exit = "zero -> {}"});
    }
    const auto carriedSubtract =
        replace(replace(carriedUnit, makeUnit,
                        "  %unit = ctjs.constant #ctjs.number<13835058055282163712>\n"),
                "binary_static add %i, %d", "binary sub %i, %d");
    rows.push_back({.what = "structured held negative strides preserve exact condition transport",
                    .body = carriedSubtract,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    reject("a structured negative stride must remain unchanged on the backedge",
           replace(carriedSubtract, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    reject("structured Sub cannot commute its induction operand",
           replace(subtract, "sub %i, %minus", "sub %minus, %i"));
    rows.push_back({.what = "structured Sub converts its original negative String latch",
                    .body = replace(subtract, negativeLiteral, "#ctjs.string<\"-1\">"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    reject("structured Sub cannot borrow a BigInt stride",
           replace(subtract, negativeLiteral, "#ctjs.bigint<\"-1\">"));
    reject("structured Sub cannot borrow an unknown stride",
           replace(subtract, "sub %i, %minus", "sub %i, %p"));
    rows.push_back({.what = "a structured subtraction proves its invariant negative stride",
                    .body = replace(subtractNegated, "unary neg %one", "binary sub %zero, %one"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    reject("structured negative subtraction still bounds its final update",
           replace(replace(subtract, negativeLiteral, "#ctjs.number<13974669643728551936>"),
                   "%index = %zero", "%index = %one"));
    makeNegative = "  %magnitude = ctjs.binary add %one, %one\n"
                   "  %unit = ctjs.unary neg %magnitude\n";
    carriedNegative = replace(replace(carriedUnit, makeUnit, makeNegative),
                              "binary_static add %i, %d", "binary sub %i, %d");
    rows.push_back({.what = "a held Neg stride survives reordered structured transport",
                    .body = carriedNegative,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    rows.push_back({.what = "structured predecessor Neg strides preserve separate child identities",
                    .body = replace(carriedNegative, makeNegative,
                                    "  %unit = scf.if %flag -> (!ctjs.value) {\n"
                                    "    %magnitude = ctjs.binary add %one, %one\n"
                                    "    %negative = ctjs.unary neg %magnitude\n"
                                    "    scf.yield %negative : !ctjs.value\n"
                                    "  } else {\n"
                                    "    %negative = ctjs.unary neg %one\n"
                                    "    scf.yield %negative : !ctjs.value\n  }\n"),
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[0]=x; a[1]=y",
                    .exit = "x -> {x}; y -> {y}"});
    const auto negativeStrings = replace(carriedNegative, makeNegative,
                                         "  %text = scf.if %flag -> (!ctjs.value) {\n"
                                         "    %left = ctjs.constant #ctjs.string<\"-1\">\n"
                                         "    scf.yield %left : !ctjs.value\n"
                                         "  } else {\n"
                                         "    %right = ctjs.constant #ctjs.string<\"-2\">\n"
                                         "    scf.yield %right : !ctjs.value\n  }\n"
                                         "  %unit = ctjs.unary plus %text\n");
    rows.push_back({.what = "negative String predecessors preserve separate structured strides",
                    .body = negativeStrings,
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[0]=x",
                    .exit = "y -> {y}; x -> {x}"});
    rows.push_back({.what = "negative String structured strides release only unreturned children",
                    .body = replace(negativeStrings, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[0]=x",
                    .exit = "zero -> {}; zero -> {}"});
    reject("negative String snapshots cannot change across structured yields",
           replace(negativeStrings, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    rows.push_back(
        {.what = "bounded decimal conversion preserves original reads and retained children",
         .body = replace(negativeStrings, "#ctjs.string<\"-2\">", "#ctjs.string<\"-02\">"),
         .arrays = "a:[x,y] | a:[x,y]",
         .reads = "a[0]=x; a[1]=y; a[0]=x",
         .exit = "y -> {y}; x -> {x}"});
    reject("unknown structured String inputs cannot borrow another predecessor's conversion",
           replace(negativeStrings, "scf.yield %right :", "scf.yield %p :"),
           ArrayContentsFailure::UnsupportedOperation);
    rows.push_back({.what = "repeated structured unary latches retain each saved String operand",
                    .body = replace(replace(negativeStrings, "    %step =",
                                            "    %repeated = ctjs.unary plus %text\n    %step ="),
                                    "sub %i, %d", "sub %i, %repeated"),
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[0]=x",
                    .exit = "y -> {y}; x -> {x}"});
    const auto directStrings =
        replace(replace(negativeStrings, "  %unit = ctjs.unary plus %text\n", ""), "%delta = %unit",
                "%delta = %text");
    rows.push_back({.what = "String latches keep each structured predecessor's original value",
                    .body = directStrings,
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[0]=x",
                    .exit = "y -> {y}; x -> {x}"});
    reject("String latches cannot change on a structured backedge",
           replace(directStrings, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    rows.push_back(
        {.what = "bounded decimal conversion preserves original reads and retained children",
         .body = replace(directStrings, "#ctjs.string<\"-2\">", "#ctjs.string<\"-02\">"),
         .arrays = "a:[x,y] | a:[x,y]",
         .reads = "a[0]=x; a[1]=y; a[0]=x",
         .exit = "y -> {y}; x -> {x}"});
    savedNegative = replace(replace(savedUnit, "  %unit = ctjs.get_property %seed[%name]",
                                    "  %magnitude = ctjs.get_property %seed[%name]\n"
                                    "  %unit = ctjs.unary neg %magnitude"),
                            "binary_static add %i, %unit", "binary sub %i, %unit");
    rows.push_back({.what = "a structured Neg stride retains its source length before shrink",
                    .body = savedNegative,
                    .arrays = "a:[x,y]; seed:[]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "structured Neg stride snapshots discharge unreturned children",
                    .body = replace(savedNegative, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]; seed:[]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    rows.push_back({.what = "a zero-trip structured Neg stride keeps its original result",
                    .body = replace(replace(savedNegative, "  ctjs.append %y to %a\n", ""),
                                    "%index = %zero", "%index = %one"),
                    .arrays = "a:[x]; seed:[]",
                    .exit = "zero -> {}"});
    reject("a held Neg stride must remain identical across structured yields",
           replace(carriedNegative, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    const auto directNegative = replace(replace(computedUnit, makeUnit, makeNegative),
                                        "binary_static add %i, %unit", "binary sub %i, %unit");
    rows.push_back(
        {.what = "a repeated structured Neg retains its invariant computed operand",
         .body = replace(replace(directNegative, "  %unit = ctjs.unary neg %magnitude\n", ""),
                         "    %step =", "    %unit = ctjs.unary neg %magnitude\n    %step ="),
         .arrays = "a:[x,y]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"});
    reject("a structured negative snapshot cannot become an own array index",
           replace(savedNegative, "%base[%i]", "%base[%unit]"), ArrayContentsFailure::UnknownIndex);
    reject("structured Add cannot borrow a negative magnitude as a positive step",
           replace(carriedNegative, "binary sub %i, %d", "binary add %i, %d"));
    for (const std::string constant :
         {"#ctjs.number<0>", "#ctjs.number<13830554455654793216>",
          "#ctjs.number<4602678819172646912>", "#ctjs.number<4751297606875873280>",
          "#ctjs.number<9218868437227405312>", "#ctjs.number<9221120237041090560>",
          "#ctjs.string<\"1\">", "#ctjs.bigint<\"1\">"}) {
        const auto body =
            replace(carriedNegative, "ctjs.binary add %one, %one", "ctjs.constant " + constant);
        if (constant == "#ctjs.string<\"1\">") {
            rows.push_back({.what = "canonical String Neg preserves its structured unit stride",
                            .body = body,
                            .arrays = "a:[x,y]",
                            .reads = "a[0]=x; a[1]=y",
                            .exit = "y -> {y}"});
        } else {
            reject("structured Neg requires an independently bounded positive Number producer",
                   body);
        }
    }
    reject("structured Neg of an unknown producer cannot supply a stride",
           replace(carriedNegative, "unary neg %magnitude", "unary neg %p"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("a structured Neg stride still bounds its final exact update",
           replace(replace(carriedNegative, "ctjs.binary add %one, %one",
                           "ctjs.constant #ctjs.number<4751297606873776128>"),
                   "%index = %zero", "%index = %one"));
}

} // namespace ctcompile::test::escape::arrays::structured_detail
