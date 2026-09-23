#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::induction_detail {

void InductionCases::powersAndProducts() {
    const std::string power = "  %power = ctjs.binary pow %minus, %one\n";
    const auto powerChild = replace(replace(savedSub, "  cf.br ^header", power + "  cf.br ^header"),
                                    "sub %i, %minus", "sub %i, %power");
    run({.what = "power one keeps the original negative length snapshot through CFG transport",
         .body = powerChild,
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "power zero releases only unreturned children",
         .body = replace(replace(replace(powerChild, "pow %minus, %one", "pow %minus, %zero"),
                                 "binary sub %i, %power", "binary_static add %i, %power"),
                         "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    reject("an even negative-one power cannot supply a negative stride",
           replace(powerChild, "pow %minus, %one", "pow %minus, %two"));
    for (const std::string exponent : {"%minus", "%three", "%magnitude"}) {
        auto source = replace(powerChild, "pow %minus, %one", "pow %minus, " + exponent);
        if (exponent == "%magnitude") {
            source = replace(source, "binary sub %i, %power", "binary_static add %i, %power");
        }
        run({.what = "negative-one powers preserve exact parity and the saved length snapshot",
             .body = source,
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "negative-one powers release only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    const auto signedUnitPower = replace(powerChild, "pow %minus, %one", "pow %minus, %magnitude");
    for (const std::string literal :
         {"#ctjs.number<4751297606873776128>", "#ctjs.number<13974669643728551936>"}) {
        run({.what = "negative-one powers preserve parity at both signed domain endpoints",
             .body = replace(signedUnitPower, "  %power = ctjs.binary pow %minus, %magnitude",
                             "  %endpoint = ctjs.constant " + literal +
                                 "\n  %power = ctjs.binary pow %minus, %endpoint"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
    }
    for (const std::string literal :
         {"#ctjs.string<\"2\">", "#ctjs.bigint<\"2\">", "#ctjs.number<4602678819172646912>",
          "#ctjs.number<4751297606875873280>", "#ctjs.number<13974669643730649088>",
          "#ctjs.number<9218868437227405312>", "#ctjs.number<18442240474082181120>",
          "#ctjs.number<9221120237041090560>"}) {
        reject("negative-one powers must supply a bounded negative Number stride",
               replace(signedUnitPower, "  %power = ctjs.binary pow %minus, %magnitude",
                       "  %exponent = ctjs.constant " + literal +
                           "\n  %power = ctjs.binary pow %minus, %exponent"));
    }
    reject("general integer powers still need an independent exact Number proof",
           replace(replace(powerChild, "pow %minus, %one", "pow %minus, %three"),
                   "binary sub %left, %magnitude", "unary neg %magnitude"));
    reject("power snapshots cannot borrow an unknown exponent",
           replace(powerChild, "pow %minus, %one", "pow %minus, %p"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("negative powers cannot supply an own array index",
           replace(powerChild, "%base[%i]", "%base[%power]"), ArrayContentsFailure::UnknownIndex);
    run({.what = "a repeated power retains its original invariant Number operands",
         .body = replace(replace(powerChild, power, ""), "  %step =", power + "  %step ="),
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    const auto unitPower = replace(replace(powerChild, "pow %minus, %one", "pow %one, %minus"),
                                   "binary sub %i, %power", "binary_static add %i, %power");
    for (const std::string literal :
         {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
        for (const bool primitiveBase : {false, true}) {
            const bool negative = !primitiveBase && literal == "#ctjs.boolean<true>";
            const std::string makePower =
                "  %power = ctjs.binary pow " +
                std::string(primitiveBase ? "%primitive, %minus" : "%minus, %primitive") + "\n";
            const auto source =
                replace(negative ? powerChild : unitPower,
                        negative ? power : "  %power = ctjs.binary pow %one, %minus\n",
                        "  %primitive = ctjs.constant " + literal + "\n" + makePower);
            if (primitiveBase && literal != "#ctjs.boolean<true>") {
                reject("false/null to a negative exponent cannot prove finite CFG progress",
                       source);
                continue;
            }
            run({.what = "primitive powers retain signed CFG snapshots after source shrink",
                 .body = source,
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "primitive powers release only unreturned CFG children",
                 .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
            reject("power conversion cannot turn its original primitive into an own key",
                   replace(source, "%base[%i]", "%base[%primitive]"),
                   ArrayContentsFailure::UnknownIndex);
            run({.what = "repeated primitive powers preserve invariant operand snapshots",
                 .body =
                     replace(replace(source, makePower, ""), "  %step =", makePower + "  %step ="),
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            reject("primitive power identities still require both original operands",
                   replace(source, "ctjs.constant " + literal, "ctjs.unary plus %p"),
                   ArrayContentsFailure::UnsupportedOperation);
            reject("Undefined cannot borrow a primitive power snapshot",
                   replace(source, literal, "#ctjs.undefined"));
        }
    }
    const auto primitiveCarriedPower =
        replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n",
                "  %primitive = ctjs.constant #ctjs.boolean<true>\n"
                "  %negative = ctjs.unary neg %magnitude\n"
                "  %unit = ctjs.binary pow %negative, %primitive\n");
    run({.what = "primitive power snapshots survive exact CFG backedge transport",
         .body = primitiveCarriedPower,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    reject("primitive power snapshots cannot change across CFG backedges",
           replace(primitiveCarriedPower, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    for (const bool stringBase : {false, true}) {
        const std::string makePower = "  %power = ctjs.binary pow " +
                                      std::string(stringBase ? "%text, %minus" : "%minus, %text") +
                                      "\n";
        const auto source =
            replace(stringBase ? unitPower : powerChild,
                    stringBase ? "  %power = ctjs.binary pow %one, %minus\n" : power,
                    "  %text = ctjs.constant #ctjs.string<\"1\">\n" + makePower);
        run({.what = "canonical String powers retain original CFG children after source shrink",
             .body = source,
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "canonical String powers release only unreturned CFG children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        run({.what = "a repeated String power proves its original invariant operands",
             .body = replace(replace(source, makePower, ""), "  %step =", makePower + "  %step ="),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        for (const std::string invalid :
             {"01", "+1", "-1", "1.0", "1e0", " 1", "0x1", "4294967295", "NaN"}) {
            const auto body =
                replace(source, "#ctjs.string<\"1\">", "#ctjs.string<\"" + invalid + "\">");
            if (invalid == "-1" && !stringBase) {
                run({.what = "a negative String exponent preserves the exact negative unit stride",
                     .body = body,
                     .arrays = "a:[one,x]; seed:[]",
                     .reads = "a[0]=one; a[1]=x",
                     .exit = "x -> {x}"});
            } else {
                if (invalid == "01" || invalid == "+1" || invalid == " 1" ||
                    (!stringBase && invalid == "4294967295")) {
                    run({.what = "bounded decimal conversion preserves original reads and retained "
                                 "children",
                         .body = body,
                         .arrays = "a:[one,x]; seed:[]",
                         .reads = "a[0]=one; a[1]=x",
                         .exit = "x -> {x}"});
                } else {
                    reject("String powers require bounded conversion and a progressing stride",
                           body);
                }
            }
        }
    }
    for (const std::string exponent : {"%minus", "%magnitude", "%three"}) {
        const auto source = replace(unitPower, "pow %one, %minus", "pow %one, " + exponent);
        run({.what = "positive-one powers retain bounded signed exponent snapshots after shrink",
             .body = source,
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "positive-one powers release only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    for (const std::string literal :
         {"#ctjs.number<4751297606873776128>", "#ctjs.number<13974669643728551936>"}) {
        run({.what = "positive-one powers accept both signed Number domain endpoints",
             .body = replace(unitPower, "binary sub %left, %magnitude", "constant " + literal),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
    }
    for (const std::string literal : {"#ctjs.number<0>", "#ctjs.number<9223372036854775808>"}) {
        for (const std::string exponent : {"%magnitude", "%three"}) {
            const auto source = replace(
                replace(replace(powerChild, "binary sub %left, %magnitude", "constant " + literal),
                        "pow %minus, %one", "pow %minus, " + exponent),
                "binary sub %i, %power", "binary_static add %i, %one");
            run({.what = "even and odd zero powers initialize finite loops without losing children",
                 .body = replace(source, "^header(%a, %zero, %zero", "^header(%a, %power, %zero"),
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
        }
    }
    for (const std::string literal :
         {"#ctjs.string<\"2\">", "#ctjs.bigint<\"2\">", "#ctjs.number<4602678819172646912>",
          "#ctjs.number<4751297606875873280>", "#ctjs.number<13974669643730649088>",
          "#ctjs.number<9218868437227405312>", "#ctjs.number<18442240474082181120>",
          "#ctjs.number<9221120237041090560>"}) {
        const auto source =
            replace(unitPower, "binary sub %left, %magnitude", "constant " + literal);
        if (literal == "#ctjs.string<\"2\">") {
            run({.what = "the original canonical String exponent retains its returned child",
                 .body = source,
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            continue;
        }
        reject("positive-one powers require exact bounded finite exponent evidence", source);
    }
    reject("a positive-one base cannot bypass an unknown exponent",
           replace(unitPower, "pow %one, %minus", "pow %one, %p"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("zero to a negative exponent cannot certify a bounded start",
           replace(replace(unitPower, "pow %one, %minus", "pow %zero, %minus"),
                   "^header(%a, %zero, %zero", "^header(%a, %power, %zero"));
    run({.what = "a repeated unit power proves its saved signed exponent",
         .body = replace(replace(unitPower, "  %power = ctjs.binary pow %one, %minus\n", ""),
                         "  %step =", "  %power = ctjs.binary pow %one, %minus\n  %step ="),
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    const std::string factor = "  %factor = ctjs.unary plus %one\n";
    const std::string product = "  %product = ctjs.binary mul %minus, %factor "
                                "{storage_test_id = \"product\"}\n";
    const auto productChild =
        replace(replace(savedSub, "  cf.br ^header", factor + product + "  cf.br ^header"),
                "sub %i, %minus", "sub %i, %product");
    const auto stringProduct =
        replace(productChild, factor, "  %factor = ctjs.constant #ctjs.string<\"1\">\n");
    for (const std::string literal :
         {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
        for (const bool negative : {false, true}) {
            for (const bool commuted : {false, true}) {
                const std::string expected = literal != "#ctjs.boolean<true>" ? "0"
                                             : negative ? "13830554455654793216"
                                                        : "4607182418800017408";
                run({.what = "Boolean/null products keep exact signed Number magnitudes",
                     .body = prefix + "  %input = ctjs.constant " + literal +
                             "\n  %factor = ctjs.unary " + (negative ? "neg" : "plus") +
                             " %one\n  %snapshot = ctjs.binary mul " +
                             (commuted ? "%factor, %input" : "%input, %factor") +
                             "\n  %expected = ctjs.constant #ctjs.number<" + expected +
                             ">\n  %index = ctjs.binary sub %snapshot, %expected\n"
                             "  %read = ctjs.get_property %a[%index]\n  ctjs.return %read\n",
                     .arrays = "a:[one,two,three]",
                     .reads = "a[0]=one",
                     .exit = "one -> {}"});
            }
        }
    }
    const auto booleanProduct =
        replace(productChild, factor, "  %factor = ctjs.constant #ctjs.boolean<true>\n");
    for (const auto & source :
         {booleanProduct, replace(booleanProduct, "mul %minus, %factor", "mul %factor, %minus")}) {
        run({.what = "Boolean products preserve signed snapshots through CFG transport",
             .body = source,
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "Boolean products discharge only unreturned CFG children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    reject("a product cannot turn its original Boolean factor into an own index",
           replace(booleanProduct, "%base[%i]", "%base[%factor]"),
           ArrayContentsFailure::UnknownIndex);
    run({.what = "a repeated Boolean product proves its original invariant operands",
         .body = replace(replace(booleanProduct, product, ""), "  %step =", product + "  %step ="),
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    reject("an Undefined factor cannot borrow Boolean product evidence",
           replace(booleanProduct, "#ctjs.boolean<true>", "#ctjs.undefined"));
    for (const auto & source :
         {productChild, replace(productChild, "mul %minus, %factor", "mul %factor, %minus"),
          stringProduct, replace(stringProduct, "mul %minus, %factor", "mul %factor, %minus"),
          replace(productChild, "binary sub %left, %magnitude",
                  "constant #ctjs.number<13830554455654793216>")}) {
        run({.what = "signed products retain the negative snapshot after source shrink",
             .body = source,
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "signed products discharge only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    const auto savedStringProduct =
        replace(stringProduct, product,
                "  %holder = ctjs.create_array [%factor] {storage_test_id = \"holder\"}\n"
                "  %savedFactor = ctjs.get_property %holder[%zero]\n"
                "  ctjs.set_property %holder[%zero], %x\n"
                "  %product = ctjs.binary mul %minus, %savedFactor\n");
    run({.what = "a saved String factor keeps its exact value after source replacement",
         .body = savedStringProduct,
         .arrays = "a:[one,x]; seed:[]; holder:[x]",
         .reads = "holder[0]=ctjs.constant; a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "a saved String product releases only unreturned children",
         .body = replace(savedStringProduct, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]; seed:[]; holder:[x]",
         .reads = "holder[0]=ctjs.constant; a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    run({.what = "a saved Boolean factor keeps its read-time value after replacement",
         .body = replace(savedStringProduct, "#ctjs.string<\"1\">", "#ctjs.boolean<true>"),
         .arrays = "a:[one,x]; seed:[]; holder:[x]",
         .reads = "holder[0]=ctjs.constant; a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "a repeated String product proves its original invariant operands",
         .body = replace(replace(stringProduct, product, ""), "  %step =", product + "  %step ="),
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    const auto positiveProduct =
        replace(replace(productChild, factor, "  %factor = ctjs.unary neg %one\n"),
                "binary sub %i, %product", "binary_static add %i, %product");
    run({.what = "two negative factors supply a positive stride and own index",
         .body = replace(positiveProduct, "%base[%i]", "%base[%product]"),
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[1]=x; a[1]=x",
         .exit = "x -> {x}"});
    for (const std::string zero : {"#ctjs.number<0>", "#ctjs.number<9223372036854775808>",
                                   "#ctjs.string<\"0\">", "#ctjs.boolean<false>", "#ctjs.null"}) {
        const auto zeroProduct = replace(
            replace(replace(productChild, factor, "  %factor = ctjs.constant " + zero + "\n"),
                    "binary sub %i, %product", "binary_static add %i, %one"),
            "^header(%a, %zero, %zero", "^header(%a, %product, %zero");
        run({.what = "signed zero products supply index zero while retaining their origin",
             .body = replace(zeroProduct, "ctjs.return %result", "ctjs.return %product"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "product -> {}"},
            "x");
    }
    run({.what = "a signed product preserves a zero-trip saved child",
         .body = replace(productChild, "^header(%a, %zero, %zero", "^header(%a, %two, %x"),
         .arrays = "a:[one,x]; seed:[]",
         .exit = "x -> {x}"});
    reject("a negative product cannot supply an own array index",
           replace(productChild, "%base[%i]", "%base[%product]"),
           ArrayContentsFailure::UnknownIndex);
    run({.what = "a repeated signed product proves its original invariant operands",
         .body = replace(replace(productChild, product, ""), "  %step =", product + "  %step ="),
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    for (const std::string constant :
         {"#ctjs.number<0>", "#ctjs.number<4602678819172646912>",
          "#ctjs.number<4751297606875873280>", "#ctjs.string<\"01\">", "#ctjs.string<\"-1\">",
          "#ctjs.string<\"1.0\">", "#ctjs.string<\"4294967295\">", "#ctjs.bigint<\"1\">"}) {
        if (constant == "#ctjs.string<\"01\">" || constant == "#ctjs.string<\"4294967295\">") {
            run({.what =
                     "bounded decimal conversion preserves original reads and retained children",
                 .body =
                     replace(productChild, factor, "  %factor = ctjs.constant " + constant + "\n"),
                 .arrays = "a:[one,x]; seed:[]",
                 .reads =
                     constant == "#ctjs.string<\"4294967295\">" ? "a[0]=one" : "a[0]=one; a[1]=x",
                 .exit = constant == "#ctjs.string<\"4294967295\">" ? "one -> {}" : "x -> {x}"},
                constant == "#ctjs.string<\"4294967295\">" ? "x" : "");
        } else {
            reject("signed multiplication needs exact bounded operands and nonzero progress",
                   replace(productChild, factor, "  %factor = ctjs.constant " + constant + "\n"));
        }
    }
    reject("a product cannot borrow an unknown operand",
           replace(productChild, "mul %minus, %factor", "mul %minus, %p"),
           ArrayContentsFailure::UnsupportedOperation);
    const auto maximumProduct = replace(
        productChild, factor, "  %factor = ctjs.constant #ctjs.number<4751297606873776128>\n");
    reject("a signed product must bound its final induction update",
           replace(maximumProduct, "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    reject("two bounded signed factors cannot certify an overflowing product",
           replace(maximumProduct, "binary sub %left, %magnitude", "binary sub %zero, %magnitude"));
    for (const std::string unary : {"plus", "neg"}) {
        const std::string operation = unary == "plus" ? "sub" : "add";
        const std::string stringOperation = unary == "plus" ? "add" : "sub";
        const std::string makeString = "  %text = ctjs.constant #ctjs.string<\"1\">\n"
                                       "  %minus = ctjs.unary " +
                                       unary + " %text\n";
        const auto stringChild =
            replace(replace(negativeChild, makeNegativeUnit, makeString), "binary sub %i, %minus",
                    "binary " + stringOperation + " %i, %minus");
        const auto savedStringChild =
            replace(stringChild, "  %minus = ctjs.unary " + unary + " %text\n",
                    "  %seed = ctjs.create_array [%text] {storage_test_id = \"seed\"}\n"
                    "  %saved = ctjs.get_property %seed[%zero]\n"
                    "  ctjs.set_property %seed[%zero], %x\n"
                    "  %minus = ctjs.unary " +
                        unary + " %saved\n");
        for (const auto & source : {stringChild, savedStringChild}) {
            const bool saved = source == savedStringChild;
            const char * arrays = saved ? "a:[one,x]; seed:[x]" : "a:[one,x]";
            const char * reads =
                saved ? "seed[0]=ctjs.constant; a[0]=one; a[1]=x" : "a[0]=one; a[1]=x";
            run({.what = "canonical String unary snapshots retain original CFG children",
                 .body = source,
                 .arrays = arrays,
                 .reads = reads,
                 .exit = "x -> {x}"});
            run({.what = "canonical String unary snapshots discharge only unreturned children",
                 .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = arrays,
                 .reads = reads,
                 .exit = "zero -> {}"},
                "x");
        }
        for (const std::string text :
             {"01", "+1", "-1", "1.0", "1e0", " 1", "0x1", "4294967295", "NaN"}) {
            if (text == "01" || text == "+1" || text == " 1" || text == "4294967295") {
                run({.what = "bounded decimal conversion preserves original reads and retained "
                             "children",
                     .body = replace(stringChild, "#ctjs.string<\"1\">",
                                     "#ctjs.string<\"" + text + "\">"),
                     .arrays = "a:[one,x]",
                     .reads = text == "4294967295" ? "a[0]=one" : "a[0]=one; a[1]=x",
                     .exit = text == "4294967295" ? "one -> {}" : "x -> {x}"},
                    text == "4294967295" ? "x" : "");
            } else {
                reject(
                    "unary String snapshots require the existing bounded canonical decimal proof",
                    replace(stringChild, "#ctjs.string<\"1\">", "#ctjs.string<\"" + text + "\">"));
            }
        }
        run({.what = "repeated String Plus/Neg uses its original literal stride",
             .body =
                 replace(replace(stringChild, "  %minus = ctjs.unary " + unary + " %text\n", ""),
                         "  %step =", "  %minus = ctjs.unary " + unary + " %text\n  %step ="),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        if (unary == "neg") {
            reject("a negated String snapshot is never an own array index",
                   replace(stringChild, "%base[%i]", "%base[%minus]"),
                   ArrayContentsFailure::UnknownIndex);
        }
        const std::string makeSigned = "  %signed = ctjs.unary " + unary + " %minus\n";
        const auto signedChild =
            replace(replace(negativeChild, "  cf.br ^header", makeSigned + "  cf.br ^header"),
                    "binary sub %i, %minus", "binary " + operation + " %i, %signed");
        const auto literalChild =
            replace(signedChild, "ctjs.unary neg %magnitude", "ctjs.constant " + negativeLiteral);
        run({.what = "signed unary literals retain the returned child after CFG induction",
             .body = literalChild,
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "signed unary literals discharge only unreturned CFG children",
             .body = replace(literalChild, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        for (const auto & [input, negated] :
             {std::pair{"9223372036854775808", "0"},
              std::pair{"13830554455654793216", "4607182418800017408"},
              std::pair{"13835058055282163712", "4611686018427387904"},
              std::pair{"13974669643728551936", "4751297606873776128"}}) {
            run({.what =
                     "signed unary literals retain exact magnitude and original result identity",
                 .body = prefix + "  %input = ctjs.constant #ctjs.number<" + input +
                         ">\n"
                         "  %expected = ctjs.constant #ctjs.number<" +
                         (unary == "plus" ? input : negated) +
                         ">\n"
                         "  %actual = ctjs.unary " +
                         unary +
                         " %input {storage_test_id = \"actual\"}\n"
                         "  %index = ctjs.binary sub %actual, %expected\n"
                         "  %read = ctjs.get_property %a[%index]\n  ctjs.return %actual\n",
                 .arrays = "a:[one,two,three]",
                 .reads = "a[0]=one",
                 .exit = "actual -> {}"});
        }
        for (const std::string constant :
             {"#ctjs.number<0>", "#ctjs.number<9223372036854775808>",
              "#ctjs.number<13826050856027422720>", "#ctjs.number<13974669643730649088>",
              "#ctjs.number<18442240474082181120>", "#ctjs.number<9221120237041090560>",
              "#ctjs.string<\"-1\">", "#ctjs.bigint<\"-1\">"}) {
            const auto body = replace(literalChild, negativeLiteral, constant);
            if (constant == "#ctjs.string<\"-1\">") {
                run({.what = "negative canonical String unary strides retain original children",
                     .body = body,
                     .arrays = "a:[one,x]",
                     .reads = "a[0]=one; a[1]=x",
                     .exit = "x -> {x}"});
            } else {
                reject("signed unary strides require exact nonzero bounded conversion", body);
            }
        }
        run({.what = "signed unary literals retain their fixed stride on the CFG backedge",
             .body = replace(replace(literalChild, makeSigned, ""),
                             "  %step =", makeSigned + "  %step ="),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        if (unary == "plus") {
            reject("a signed unary literal's negative magnitude is never an own array index",
                   replace(literalChild, "%base[%i]", "%base[%signed]"),
                   ArrayContentsFailure::UnknownIndex);
        }
        run({.what = "signed unary snapshots retain the exact returned child",
             .body = signedChild,
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "signed unary snapshots discharge only unreturned children",
             .body = replace(signedChild, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        run({.what = "a zero-trip signed unary stride preserves its saved child",
             .body = replace(signedChild, "^header(%a, %zero, %zero", "^header(%a, %two, %x"),
             .arrays = "a:[one,x]",
             .exit = "x -> {x}"});
        run({.what = "Plus and Neg preserve a negated zero as a nonnegative start",
             .body = replace(signedChild, "  cf.br ^header(%a, %zero, %zero",
                             "  %negativeZero = ctjs.unary neg %zero\n"
                             "  %start = ctjs.unary " +
                                 unary + " %negativeZero\n  cf.br ^header(%a, %start, %zero"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        const auto carriedSigned =
            replace(replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n",
                            "  %negative = ctjs.unary neg %magnitude\n"
                            "  %unit = ctjs.unary " +
                                unary + " %negative\n"),
                    "binary sub %i, %d", "binary " + operation + " %i, %d");
        run({.what = "signed unary strides preserve exact carried values and final overshoot",
             .body = replace(replace(carriedSigned, "^exit(%sum :", "^exit(%index :"),
                             "  ctjs.return %result",
                             "  ctjs.append %zero to %a\n  ctjs.append %zero to %a\n"
                             "  %after = ctjs.get_property %a[%result]\n  ctjs.return %after"),
             .arrays = "a:[one,two,three,zero,zero]",
             .reads = "a[0]=one; a[2]=three; a[4]=zero",
             .exit = "zero -> {}"});
        reject("signed unary strides cannot change on the backedge",
               replace(carriedSigned, "^header(%base, %step, %added, %d",
                       "^header(%base, %step, %added, %one"));
        reject("signed unary strides cannot borrow the opposite sign",
               replace(signedChild, "binary " + operation + " %i, %signed",
                       "binary " + std::string{unary == "plus" ? "add" : "sub"} + " %i, %signed"));
        reject("a held negative snapshot cannot initialize an increasing loop",
               replace(signedChild, "^header(%a, %zero, %zero", "^header(%a, %minus, %zero"));
        run({.what = "repeated signed unary latches keep invariant negative snapshots",
             .body = replace(replace(signedChild, makeSigned, ""),
                             "  %step =", makeSigned + "  %step ="),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        reject("signed unary snapshots cannot borrow an unknown input",
               replace(signedChild, makeSigned, "  %signed = ctjs.unary " + unary + " %p\n"),
               ArrayContentsFailure::UnsupportedOperation);
        if (unary == "plus") {
            reject("Plus preserves a negative snapshot without making it an own index",
                   replace(signedChild, "%base[%i]", "%base[%signed]"),
                   ArrayContentsFailure::UnknownIndex);
        } else {
            run({.what = "Neg restores an exact positive own index from its held magnitude",
                 .body = replace(signedChild, "%base[%i]", "%base[%signed]"),
                 .arrays = "a:[one,x]",
                 .reads = "a[1]=x; a[1]=x",
                 .exit = "x -> {x}"});
        }
        for (const std::string constant :
             {"#ctjs.number<0>", "#ctjs.number<9223372036854775808>",
              "#ctjs.number<4602678819172646912>", "#ctjs.number<4751297606875873280>",
              "#ctjs.number<9218868437227405312>", "#ctjs.number<9221120237041090560>",
              "#ctjs.string<\"1\">", "#ctjs.bigint<\"1\">"}) {
            const auto body = replace(signedChild, "#ctjs.number<4607182418800017408>", constant);
            if (constant == "#ctjs.string<\"1\">") {
                run({.what = "canonical String unary chains preserve their held signed Number",
                     .body = body,
                     .arrays = "a:[one,x]",
                     .reads = "a[0]=one; a[1]=x",
                     .exit = "x -> {x}"});
            } else {
                reject("signed unary strides require an exact nonzero bounded Number producer",
                       body);
            }
        }
        reject("signed unary strides still bound their final exact update",
               replace(replace(signedChild, "  %magnitude = ctjs.unary plus %one",
                               "  %maximum = ctjs.constant #ctjs.number<4751297606873776128>\n"
                               "  %magnitude = ctjs.unary plus %maximum"),
                       "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    }
}

} // namespace ctcompile::test::escape::arrays::induction_detail
