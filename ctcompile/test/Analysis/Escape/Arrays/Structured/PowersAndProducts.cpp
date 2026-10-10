#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::structured_detail {

void StructuredCases::powersAndProducts() {
    const std::string makePower = "  %exponent = ctjs.unary neg %magnitude\n"
                                  "  %unit = ctjs.binary pow %one, %exponent\n";
    const auto carriedPower =
        replace(replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n", makePower),
                "binary sub %i, %d", "binary_static add %i, %d");
    rows.push_back({.what = "positive-one powers survive reordered structured transport",
                    .body = carriedPower,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "structured positive-one powers release only unreturned children",
                    .body = replace(carriedPower, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    reject("a power result must remain unchanged across structured backedges",
           replace(carriedPower, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    reject("positive-one powers cannot borrow another structured arm's Number exponent",
           replace(carriedPower, "  %exponent = ctjs.unary neg %magnitude\n",
                   "  %exponent = scf.if %flag -> (!ctjs.value) {\n"
                   "    %negative = ctjs.unary neg %magnitude\n"
                   "    scf.yield %negative : !ctjs.value\n"
                   "  } else {\n    scf.yield %p : !ctjs.value\n  }\n"),
           ArrayContentsFailure::UnsupportedOperation);
    const auto negativeOnePower = replace(carriedPower, "  %unit = ctjs.binary pow %one, %exponent",
                                          "  %negativeOne = ctjs.unary neg %one\n"
                                          "  %unit = ctjs.binary pow %negativeOne, %exponent");
    for (const auto & source :
         {negativeOnePower,
          replace(replace(negativeOnePower, "unary neg %magnitude", "unary neg %one"),
                  "binary_static add %i, %d", "binary sub %i, %d")}) {
        rows.push_back({.what = "negative-one powers retain parity through structured transport",
                        .body = source,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "structured negative-one powers release only unreturned children",
                        .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
    }
    reject("negative-one power snapshots cannot change on a structured backedge",
           replace(negativeOnePower, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    reject("negative-one powers need an exact exponent on every structured predecessor",
           replace(negativeOnePower, "  %exponent = ctjs.unary neg %magnitude\n",
                   "  %exponent = scf.if %flag -> (!ctjs.value) {\n"
                   "    %negative = ctjs.unary neg %magnitude\n"
                   "    scf.yield %negative : !ctjs.value\n"
                   "  } else {\n    scf.yield %p : !ctjs.value\n  }\n"),
           ArrayContentsFailure::UnsupportedOperation);
    const std::string makeProduct = "  %negative = ctjs.unary neg %magnitude\n"
                                    "  %unit = ctjs.binary mul %negative, %one\n";
    const auto carriedProduct =
        replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n", makeProduct);
    rows.push_back({.what = "signed products survive reordered structured backedge transport",
                    .body = carriedProduct,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    const auto stringProduct =
        replace(carriedProduct, "  %unit = ctjs.binary mul %negative, %one\n",
                "  %factor = ctjs.constant #ctjs.string<\"1\">\n"
                "  %unit = ctjs.binary mul %factor, %negative\n");
    const auto booleanProduct =
        replace(stringProduct, "  %factor = ctjs.constant #ctjs.string<\"1\">\n",
                "  %factor = scf.if %flag -> (!ctjs.value) {\n"
                "    %truth = ctjs.constant #ctjs.boolean<true>\n"
                "    scf.yield %truth : !ctjs.value\n"
                "  } else {\n    scf.yield %one : !ctjs.value\n  }\n");
    rows.push_back({.what = "Boolean and Number products prove each structured predecessor",
                    .body = booleanProduct,
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[0]=x",
                    .exit = "x -> {x}; x -> {x}"});
    rows.push_back({.what = "Boolean products release only unreturned structured children",
                    .body = replace(booleanProduct, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[0]=x",
                    .exit = "zero -> {}; zero -> {}"});
    reject("Boolean products cannot change across structured backedges",
           replace(booleanProduct, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    reject("a Boolean product cannot authorize an unknown structured predecessor",
           replace(booleanProduct, "scf.yield %one :", "scf.yield %p :"),
           ArrayContentsFailure::UnsupportedOperation);
    rows.push_back(
        {.what = "String products preserve signed snapshots through structured transport",
         .body = stringProduct,
         .arrays = "a:[x,y]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"});
    rows.push_back({.what = "structured String products release only unreturned children",
                    .body = replace(stringProduct, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x",
                    .exit = "zero -> {}"});
    reject("String products cannot change across structured backedges",
           replace(stringProduct, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    reject("String products need a canonical factor on every structured predecessor",
           replace(stringProduct, "  %factor = ctjs.constant #ctjs.string<\"1\">\n",
                   "  %factor = scf.if %flag -> (!ctjs.value) {\n"
                   "    scf.yield %one : !ctjs.value\n"
                   "  } else {\n    scf.yield %p : !ctjs.value\n  }\n"),
           ArrayContentsFailure::UnsupportedOperation);
    rows.push_back({.what = "a signed product retains its exact final overshoot after growth",
                    .body = replace(replace(carriedProduct, "  ctjs.append %y to %a\n",
                                            "  ctjs.append %y to %a\n  ctjs.append %y to %a\n"),
                                    "  ctjs.return %result",
                                    "  ctjs.append %zero to %finalArray\n"
                                    "  ctjs.append %zero to %finalArray\n"
                                    "  %after = ctjs.get_property %finalArray[%finalIndex]\n"
                                    "  ctjs.return %after"),
                    .arrays = "a:[x,y,y,zero,zero]",
                    .reads = "a[0]=x; a[2]=y; a[4]=zero",
                    .exit = "zero -> {}"});
    const auto savedProduct =
        replace(replace(savedNegative, "  %unit = ctjs.unary neg %magnitude\n",
                        "  %negative = ctjs.unary neg %magnitude\n"),
                "  ctjs.set_property %seed[%name], %zero\n",
                "  ctjs.set_property %seed[%name], %zero\n"
                "  %unit = ctjs.binary mul %one, %negative\n");
    rows.push_back({.what = "structured products keep the negative snapshot after source shrink",
                    .body = savedProduct,
                    .arrays = "a:[x,y]; seed:[]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "signed product snapshots discharge unreturned structured children",
                    .body = replace(savedProduct, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]; seed:[]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    reject("a signed product stride must remain unchanged across structured yields",
           replace(carriedProduct, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    rows.push_back(
        {.what = "a repeated structured product proves its original invariant operands",
         .body = replace(replace(savedProduct, "  %unit = ctjs.binary mul %one, %negative\n", ""),
                         "    %step =", "    %unit = ctjs.binary mul %one, %negative\n    %step ="),
         .arrays = "a:[x,y]; seed:[]",
         .reads = "a[0]=x; a[1]=y",
         .exit = "y -> {y}"});
    rows.push_back(
        {.what = "structured products convert the original canonical negative String",
         .body = replace(carriedProduct, "unary neg %magnitude", "constant #ctjs.string<\"-2\">"),
         .arrays = "a:[x,y]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"});
    rows.push_back(
        {.what = "bounded decimal conversion preserves original reads and retained children",
         .body = replace(carriedProduct, "unary neg %magnitude", "constant #ctjs.string<\"-02\">"),
         .arrays = "a:[x,y]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"});
    reject("structured multiplication must prove every predecessor's signed Number",
           replace(carriedProduct, "  %negative = ctjs.unary neg %magnitude\n",
                   "  %negative = scf.if %flag -> (!ctjs.value) {\n"
                   "    %n = ctjs.unary neg %magnitude\n"
                   "    scf.yield %n : !ctjs.value\n"
                   "  } else {\n    scf.yield %p : !ctjs.value\n  }\n"),
           ArrayContentsFailure::UnsupportedOperation);
    for (const std::string unary : {"plus", "neg"}) {
        const std::string operation = unary == "plus" ? "sub" : "add";
        const std::string makeSigned = "  %unit = ctjs.unary " + unary + " %negative\n";
        const auto savedSigned =
            replace(replace(replace(savedNegative, "  %unit = ctjs.unary neg %magnitude",
                                    "  %negative = ctjs.unary neg %magnitude"),
                            "  ctjs.set_property %seed[%name], %zero\n",
                            "  ctjs.set_property %seed[%name], %zero\n" + makeSigned),
                    "binary sub %i, %unit", "binary " + operation + " %i, %unit");
        rows.push_back({.what = "signed unary snapshots keep source length before shrink",
                        .body = savedSigned,
                        .arrays = "a:[x,y]; seed:[]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "signed structured snapshots discharge only unreturned children",
                        .body = replace(savedSigned, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]; seed:[]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
        rows.push_back({.what = "zero-trip signed structured strides preserve their saved result",
                        .body = replace(replace(savedSigned, "  ctjs.append %y to %a\n", ""),
                                        "%index = %zero", "%index = %one"),
                        .arrays = "a:[x]; seed:[]",
                        .exit = "zero -> {}"});
        const auto carriedSigned =
            replace(replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n",
                            "  %negative = ctjs.unary neg %magnitude\n" + makeSigned),
                    "binary sub %i, %d", "binary " + operation + " %i, %d");
        const auto carriedString =
            replace(replace(carriedNegative, "ctjs.binary add %one, %one",
                            "ctjs.constant #ctjs.string<\"2\">"),
                    "unary neg %magnitude", "unary " + unary + " %magnitude");
        const auto stringBody =
            unary == "plus" ? replace(carriedString, "binary sub %i, %d", "binary add %i, %d")
                            : carriedString;
        rows.push_back({.what = "canonical String unary snapshots survive structured transport",
                        .body = stringBody,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x",
                        .exit = "x -> {x}"});
        rows.push_back({.what = "canonical String unary snapshots discharge structured children",
                        .body = replace(stringBody, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x",
                        .exit = "zero -> {}"});
        reject("canonical String unary snapshots cannot change on a structured backedge",
               replace(stringBody, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
        reject("unary String conversion must prove every structured predecessor",
               replace(stringBody, "  %magnitude = ctjs.constant #ctjs.string<\"2\">\n",
                       "  %magnitude = scf.if %flag -> (!ctjs.value) {\n"
                       "    %text = ctjs.constant #ctjs.string<\"2\">\n"
                       "    scf.yield %text : !ctjs.value\n"
                       "  } else {\n    scf.yield %p : !ctjs.value\n  }\n"),
               ArrayContentsFailure::UnsupportedOperation);
        const auto carriedLiteral = replace(carriedSigned, "ctjs.unary neg %magnitude",
                                            "ctjs.constant #ctjs.number<13835058055282163712>");
        rows.push_back({.what = "signed unary literals survive reordered structured transport",
                        .body = carriedLiteral,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x",
                        .exit = "x -> {x}"});
        rows.push_back({.what = "signed unary literals discharge unreturned structured children",
                        .body = replace(carriedLiteral, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x",
                        .exit = "zero -> {}"});
        reject("signed unary literal strides must remain unchanged across structured yields",
               replace(carriedLiteral, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
        reject("signed unary literals cannot borrow another structured arm's exact input",
               replace(carriedLiteral,
                       "  %negative = ctjs.constant #ctjs.number<13835058055282163712>\n",
                       "  %negative = scf.if %flag -> (!ctjs.value) {\n"
                       "    %n = ctjs.constant #ctjs.number<13835058055282163712>\n"
                       "    scf.yield %n : !ctjs.value\n"
                       "  } else {\n    scf.yield %p : !ctjs.value\n  }\n"),
               ArrayContentsFailure::UnsupportedOperation);
        rows.push_back({.what = "signed unary strides survive reordered structured transport",
                        .body = carriedSigned,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x",
                        .exit = "x -> {x}"});
        rows.push_back({.what = "signed unary predecessor snapshots retain separate magnitudes",
                        .body = replace(carriedSigned,
                                        "  %magnitude = ctjs.binary add %one, %one\n"
                                        "  %negative = ctjs.unary neg %magnitude\n",
                                        "  %negative = scf.if %flag -> (!ctjs.value) {\n"
                                        "    %magnitude = ctjs.binary add %one, %one\n"
                                        "    %n = ctjs.unary neg %magnitude\n"
                                        "    scf.yield %n : !ctjs.value\n"
                                        "  } else {\n    %n = ctjs.unary neg %one\n"
                                        "    scf.yield %n : !ctjs.value\n  }\n"),
                        .arrays = "a:[x,y] | a:[x,y]",
                        .reads = "a[0]=x; a[0]=x; a[1]=y",
                        .exit = "x -> {x}; y -> {y}"});
        reject("signed structured strides must remain identical on the backedge",
               replace(carriedSigned, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
        reject("signed structured strides cannot borrow the opposite sign",
               replace(carriedSigned, "binary " + operation + " %i, %d",
                       "binary " + std::string{unary == "plus" ? "add" : "sub"} + " %i, %d"));
        rows.push_back({.what = "repeated structured signed unary latches keep saved operands",
                        .body = replace(replace(savedSigned, makeSigned, ""),
                                        "    %step =", "  " + makeSigned + "    %step ="),
                        .arrays = "a:[x,y]; seed:[]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        reject("signed structured snapshots cannot borrow an unknown input",
               replace(savedSigned, makeSigned, "  %unit = ctjs.unary " + unary + " %p\n"),
               ArrayContentsFailure::UnsupportedOperation);
        reject("signed structured strides still bound their final exact update",
               replace(replace(carriedSigned, "ctjs.binary add %one, %one",
                               "ctjs.constant #ctjs.number<4751297606873776128>"),
                       "%index = %zero", "%index = %one"));
    }
}

} // namespace ctcompile::test::escape::arrays::structured_detail
