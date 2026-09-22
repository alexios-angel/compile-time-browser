#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::induction_detail {

void InductionCases::overwritesAndTransport() {
    run({.what = "zero/+1 induction records every distinct element under strict own length",
         .body = original,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    run({.what = "zero-length induction skips its certified body without an element read",
         .body = replace(original, "[%one, %two, %three]", "[]"),
         .arrays = "a:[]",
         .exit = "zero -> {}"});
    run({.what = "one-element induction terminates before an inherited index could be read",
         .body = replace(original, "[%one, %two, %three]", "[%one]"),
         .arrays = "a:[one]",
         .reads = "a[0]=one",
         .exit = "added -> {}"});
    savedChild =
        replace(replace(replace(replace(original, "  %a =",
                                        "  %x = ctjs.create_object "
                                        "{storage_test_id = \"x\"}\n  %a ="),
                                "[%one, %two, %three]", "[%one, %x]"),
                        "  %added = ctjs.binary add %s, %read {storage_test_id = \"added\"}\n", ""),
                "^header(%base, %step, %added", "^header(%base, %step, %read");
    run({.what = "a saved final child keeps its identity through every induction iteration",
         .body = savedChild,
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "unreturned children remain confined after all loop reads",
         .body = replace(savedChild, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    const std::string overwritten =
        replace(savedChild, "  %step =", "  ctjs.set_property %base[%i], %zero\n  %step =");
    const auto masked =
        replace(replace(savedChild, "ctjs.return %result", "ctjs.return %a"), "  %read =",
                "  %position = ctjs.binary_static bitand %i, %one\n"
                "  ctjs.set_property %base[%position], %zero\n  %read =");
    const auto remainder =
        replace(masked, "ctjs.binary_static bitand %i, %one", "ctjs.binary mod %i, %two");
    for (const auto & expression :
         {"%position = ctjs.binary mod %i, %two", "%position = ctjs.binary mod %i, %three",
          "%offset = ctjs.binary add %two, %i\n"
          "  %position = ctjs.binary mod %offset, %two"}) {
        run({.what = "Number remainder bounds preserve exact writes across wraps",
             .body = replace(remainder, "%position = ctjs.binary mod %i, %two", expression),
             .arrays = "a:[zero,zero]",
             .reads = "a[0]=zero; a[1]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "negative zero retains the own zero key through remainder",
         .body = replace(remainder, "#ctjs.number<0>", "#ctjs.number<9223372036854775808>"),
         .arrays = "a:[zero,zero]",
         .reads = "a[0]=zero; a[1]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "remainder replay keeps child snapshots made before an overwrite",
         .body = replace(overwritten, "  ctjs.set_property %base[%i], %zero",
                         "  %position = ctjs.binary mod %i, %two\n"
                         "  ctjs.set_property %base[%position], %zero"),
         .arrays = "a:[zero,zero]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "remainder enclosures do not erase unvisited children",
         .body = replace(remainder, "mod %i, %two", "mod %i, %one"),
         .arrays = "a:[zero,x]",
         .reads = "a[0]=zero; a[1]=x",
         .exit = "a -> {a,x}"});
    const auto remainderReload = replace(replace(remainder, "[%one, %x]", "[%x, %x, %two, %zero]"),
                                         "%position = ctjs.binary mod %i, %two",
                                         "%divisor = ctjs.get_property %base[%two]\n"
                                         "  %position = ctjs.binary mod %i, %divisor");
    run({.what = "remainder divisors reload only outside the complete overwrite enclosure",
         .body = remainderReload,
         .arrays = "a:[zero,zero,two,zero]",
         .reads = "a[2]=two; a[0]=zero; a[2]=two; a[1]=zero; a[2]=two; a[2]=two; "
                  "a[2]=two; a[3]=zero",
         .exit = "a -> {a}"},
        "x");
    reject("remainder bounds reject overlapping divisor reloads",
           replace(replace(remainderReload, "[%x, %x, %two, %zero]", "[%two, %x, %zero, %zero]"),
                   "%divisor = ctjs.get_property %base[%two]",
                   "%divisor = ctjs.get_property %base[%zero]"));
    reject(
        "later stores invalidate earlier remainder divisor reloads",
        replace(remainderReload, "  %step =", "  ctjs.set_property %base[%two], %zero\n  %step ="));
    for (const auto & operands : {"%i, %zero", "%i, %i", "%two, %i"}) {
        reject("remainder requires a nonzero invariant divisor in its original operand order",
               replace(remainder, "mod %i, %two", "mod " + std::string(operands)));
    }
    const auto signedRemainder = replace(remainder, "%position = ctjs.binary mod %i, %two",
                                         "%negative = ctjs.unary neg %i\n"
                                         "  %part = ctjs.binary mod %negative, %two\n"
                                         "  %position = ctjs.binary add %part, %one");
    for (const auto & divisor : {"%two", "%three"}) {
        run({.what = "signed remainder bounds retain negative dividend magnitudes",
             .body = replace(signedRemainder, "mod %negative, %two",
                             "mod %negative, " + std::string(divisor)),
             .arrays = "a:[zero,zero]",
             .reads = "a[0]=one; a[1]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "negative divisors preserve dividend sign and exact visits",
         .body = replace(signedRemainder, "%part = ctjs.binary mod %negative, %two",
                         "%divisor = ctjs.unary neg %two\n"
                         "  %part = ctjs.binary mod %negative, %divisor"),
         .arrays = "a:[zero,zero]",
         .reads = "a[0]=one; a[1]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "negating a signed remainder restores ascending own positions",
         .body = replace(signedRemainder, "ctjs.binary add %part, %one", "ctjs.unary neg %part"),
         .arrays = "a:[zero,zero]",
         .reads = "a[0]=zero; a[1]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto crossingRemainder =
        replace(replace(signedRemainder, "[%one, %x]", "[%x, %x, %x, %zero]"), "ctjs.unary neg %i",
                "ctjs.binary sub %i, %one");
    run({.what = "a remainder enclosure covers both signs across zero",
         .body = crossingRemainder,
         .arrays = "a:[zero,zero,zero,zero]",
         .reads = "a[0]=zero; a[1]=zero; a[2]=zero; a[3]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto signedRemainderReload =
        replace(remainderReload, "%position = ctjs.binary mod %i, %divisor",
                "%negative = ctjs.unary neg %i\n"
                "  %part = ctjs.binary mod %negative, %divisor\n"
                "  %position = ctjs.binary add %part, %one");
    run({.what = "signed remainder writes preserve only disjoint divisor reloads",
         .body = signedRemainderReload,
         .arrays = "a:[zero,zero,two,zero]",
         .reads = "a[2]=two; a[0]=x; a[2]=two; a[1]=zero; a[2]=two; a[2]=two; "
                  "a[2]=two; a[3]=zero",
         .exit = "a -> {a}"},
        "x");
    reject("later writes invalidate signed remainder reloads",
           replace(signedRemainderReload,
                   "  %step =", "  ctjs.set_property %base[%two], %zero\n  %step ="));
    reject("signed remainder extrema cannot omit an interior divisor reload",
           replace(replace(crossingRemainder, "[%x, %x, %x, %zero]", "[%x, %two, %x, %zero]"),
                   "%part = ctjs.binary mod %negative, %two",
                   "%divisor = ctjs.get_property %base[%one]\n"
                   "  %part = ctjs.binary mod %negative, %divisor"));
    reject("a negative final remainder is not an own array index",
           replace(signedRemainder, "ctjs.binary add %part, %one", "ctjs.unary plus %part"));
    const auto remainderBand = replace(remainder, "%position = ctjs.binary mod %i, %two",
                                       "%four = ctjs.binary add %two, %two\n"
                                       "  %offset = ctjs.binary add %i, %four\n"
                                       "  %part = ctjs.binary mod %offset, %three\n"
                                       "  %position = ctjs.binary sub %part, %one");
    run({.what = "a single remainder quotient band preserves translated endpoints",
         .body = remainderBand,
         .arrays = "a:[zero,zero]",
         .reads = "a[0]=zero; a[1]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto negativeRemainderBand =
        replace(replace(remainderBand, "%offset = ctjs.binary add %i, %four",
                        "%negative = ctjs.unary neg %i\n"
                        "  %offset = ctjs.binary sub %negative, %four"),
                "ctjs.binary sub %part, %one", "ctjs.binary add %part, %two");
    for (const auto & body :
         {negativeRemainderBand,
          replace(negativeRemainderBand, "%part = ctjs.binary mod %offset, %three",
                  "%divisor = ctjs.unary neg %three\n"
                  "  %part = ctjs.binary mod %offset, %divisor")}) {
        run({.what = "negative remainder bands retain descending visits for either divisor sign",
             .body = body,
             .arrays = "a:[zero,zero]",
             .reads = "a[0]=one; a[1]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    const auto remainderBandReload =
        replace(replace(replace(replace(replace(remainder, "  %a =",
                                                "  %eight = ctjs.constant "
                                                "#ctjs.number<4620693217682128896> "
                                                "{storage_test_id = \"eight\"}\n  %a ="),
                                        "[%one, %x]", "[%one, %x, %eight, %x, %one]"),
                                "^header(%a, %zero", "^header(%a, %one"),
                        "add %i, %one", "add %i, %two"),
                "%position = ctjs.binary mod %i, %two",
                "%divisor = ctjs.get_property %base[%two]\n"
                "  %offset = ctjs.binary add %i, %eight\n"
                "  %position = ctjs.binary mod %offset, %divisor");
    run({.what = "one remainder band preserves divisor reloads inside stride gaps",
         .body = remainderBandReload,
         .arrays = "a:[one,zero,eight,zero,one]",
         .reads = "a[2]=eight; a[1]=zero; a[2]=eight; a[3]=zero",
         .exit = "a -> {a}"},
        "x");
    reject("a later write still invalidates a divisor inside a remainder stride gap",
           replace(remainderBandReload,
                   "  %step =", "  ctjs.set_property %base[%two], %zero\n  %step ="));
    reject("one remainder band cannot reload a divisor at a visited lattice point",
           replace(replace(remainderBandReload, "[%one, %x, %eight, %x, %one]",
                           "[%one, %eight, %one, %x, %one]"),
                   "%divisor = ctjs.get_property %base[%two]",
                   "%divisor = ctjs.get_property %base[%one]"));
    reject("remainder wraps cannot borrow a nonzero band's narrow endpoint range",
           replace(remainderBand, "add %i, %four", "add %i, %two"));
    const auto remainderWrapReload =
        replace(replace(replace(replace(remainder, "  %a =",
                                        "  %four = ctjs.constant #ctjs.number<4616189618054758400> "
                                        "{storage_test_id = \"four\"}\n  %a ="),
                                "[%one, %x]", "[%x, %four, %x, %zero, %zero]"),
                        "add %i, %one", "add %i, %two"),
                "%position = ctjs.binary mod %i, %two",
                "%divisor = ctjs.get_property %base[%one]\n"
                "  %position = ctjs.binary mod %i, %divisor");
    for (const auto & body :
         {remainderWrapReload,
          replace(remainderWrapReload, "%position = ctjs.binary mod %i, %divisor",
                  "%negative = ctjs.unary neg %i\n"
                  "  %part = ctjs.binary mod %negative, %divisor\n"
                  "  %position = ctjs.unary neg %part")}) {
        run({.what = "remainder wraps preserve disjoint reloads in a congruence gap",
             .body = body,
             .arrays = "a:[zero,four,zero,zero,zero]",
             .reads = "a[1]=four; a[0]=zero; a[1]=four; a[2]=zero; a[1]=four; a[4]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    const auto oddRemainderWrap = replace(
        replace(replace(remainderWrapReload, "[%x, %four, %x, %zero, %zero]",
                        "[%four, %x, %zero, %x, %zero, %zero]"),
                "^header(%a, %zero", "^header(%a, %one"),
        "%divisor = ctjs.get_property %base[%one]", "%divisor = ctjs.get_property %base[%zero]");
    run({.what = "remainder wrap bounds align to a nonzero residue",
         .body = oddRemainderWrap,
         .arrays = "a:[four,zero,zero,zero,zero,zero]",
         .reads = "a[0]=four; a[1]=zero; a[0]=four; a[3]=zero; a[0]=four; a[5]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "remainder congruences preserve both signs through zero",
         .body = replace(replace(remainderWrapReload, "[%x, %four, %x, %zero, %zero]",
                                 "[%x, %four, %x, %zero, %x, %zero, %zero]"),
                         "%position = ctjs.binary mod %i, %divisor",
                         "%signed = ctjs.binary sub %i, %two\n"
                         "  %part = ctjs.binary mod %signed, %divisor\n"
                         "  %position = ctjs.binary add %part, %two"),
         .arrays = "a:[zero,four,zero,zero,zero,zero,zero]",
         .reads = "a[1]=four; a[0]=zero; a[1]=four; a[2]=zero; a[1]=four; a[4]=zero; "
                  "a[1]=four; a[6]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "remainder wrap replay retains an unvisited child in a congruence gap",
         .body = replace(remainderWrapReload, "[%x, %four, %x, %zero, %zero]",
                         "[%x, %four, %x, %x, %zero]"),
         .arrays = "a:[zero,four,zero,x,zero]",
         .reads = "a[1]=four; a[0]=zero; a[1]=four; a[2]=zero; a[1]=four; a[4]=zero",
         .exit = "a -> {a,x}"});
    reject("remainder congruence cannot hide a later divisor overwrite",
           replace(remainderWrapReload,
                   "  %step =", "  ctjs.set_property %base[%one], %zero\n  %step ="));
    reject("remainder congruence cannot reload at a possible visited residue",
           replace(replace(remainderWrapReload, "[%x, %four, %x, %zero, %zero]",
                           "[%x, %zero, %four, %zero, %zero]"),
                   "%divisor = ctjs.get_property %base[%one]",
                   "%divisor = ctjs.get_property %base[%two]"));
    reject("coprime strides cannot preserve the original sparse lattice after wrapping",
           replace(replace(remainderWrapReload, "[%x, %four, %x, %zero, %zero]",
                           "[%x, %four, %x, %zero, %zero, %zero, %zero]"),
                   "add %i, %two", "add %i, %three"));
    run({.what = "a remainder band preserves children saved before its exact overwrites",
         .body = replace(replace(remainderBand, "  cf.br ^header(%a,",
                                 "  %saved = ctjs.get_property %a[%one]\n  cf.br ^header(%a,"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "a:[zero,zero]",
         .reads = "a[1]=x; a[0]=zero; a[1]=zero",
         .exit = "x -> {x}"});
    reject("a remainder divisor cannot hide an unproved large dividend",
           replace(remainder, "%position = ctjs.binary mod %i, %two",
                   "%maximum = ctjs.constant #ctjs.number<4751297606873776128>\n"
                   "  %large = ctjs.binary add %maximum, %i\n"
                   "  %position = ctjs.binary mod %large, %two"));
    reject("a composed remainder still requires every final key inside its allocation",
           replace(remainder, "%position = ctjs.binary mod %i, %two",
                   "%part = ctjs.binary mod %i, %two\n"
                   "  %position = ctjs.binary add %part, %one"));
    for (const auto & value :
         {"#ctjs.number<13835058055282163712>", "#ctjs.number<4609434218613702656>",
          "#ctjs.string<\"2\">", "#ctjs.boolean<true>", "#ctjs.bigint<\"2\">",
          "#ctjs.number<9223372036854775808>"}) {
        const auto body = replace(
            replace(remainder,
                    "  %a =", "  %divisor = ctjs.constant " + std::string(value) + "\n  %a ="),
            "mod %i, %two", "mod %i, %divisor");
        if (std::string(value) == "#ctjs.number<13835058055282163712>") {
            run({.what = "negative literal divisors preserve positive remainder indices",
                 .body = body,
                 .arrays = "a:[zero,zero]",
                 .reads = "a[0]=zero; a[1]=zero",
                 .exit = "a -> {a}"},
                "x");
        } else {
            reject("remainder index divisors require nonzero integer Numbers", body);
        }
    }
    for (const auto & expression :
         {"ctjs.binary_static bitand %i, %one", "ctjs.binary_static bitand %one, %i",
          "ctjs.unary neg %i\n"
          "  %position = ctjs.binary_static bitand %negative, %one"}) {
        auto body = replace(masked, "ctjs.binary_static bitand %i, %one", expression);
        if (body.find("%negative") != std::string::npos) {
            body = replace(body, "%position = ctjs.unary", "%negative = ctjs.unary");
        }
        run({.what = "numeric masks bound both operand orders and signed inputs",
             .body = body,
             .arrays = "a:[zero,zero]",
             .reads = "a[0]=zero; a[1]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "masked writes retain a child read before its overwrite",
         .body = replace(overwritten, "  ctjs.set_property %base[%i], %zero",
                         "  %position = ctjs.binary_static bitand %i, %one\n"
                         "  ctjs.set_property %base[%position], %zero"),
         .arrays = "a:[zero,zero]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "a mask enclosure does not overwrite its unvisited positions",
         .body = replace(replace(masked, "[%one, %x]", "[%x, %x, %x, %x]"), "bitand %i, %one",
                         "bitand %i, %two"),
         .arrays = "a:[zero,x,zero,x]",
         .reads = "a[0]=zero; a[1]=x; a[2]=zero; a[3]=x",
         .exit = "a -> {a,x}"});
    const auto maskedReload = replace(replace(masked, "[%one, %x]", "[%x, %x, %one, %zero]"),
                                      "%position = ctjs.binary_static bitand %i, %one",
                                      "%mask = ctjs.get_property %base[%two]\n"
                                      "  %position = ctjs.binary_static bitand %i, %mask");
    run({.what = "a disjoint mask reload survives every enclosed store",
         .body = maskedReload,
         .arrays = "a:[zero,zero,one,zero]",
         .reads = "a[2]=one; a[0]=zero; a[2]=one; a[1]=zero; a[2]=one; a[2]=one; "
                  "a[2]=one; a[3]=zero",
         .exit = "a -> {a}"},
        "x");
    reject("a mask cannot reload inside its overwrite enclosure",
           replace(maskedReload, "%mask = ctjs.get_property %base[%two]",
                   "%mask = ctjs.get_property %base[%zero]"));
    reject("a later store invalidates an earlier mask reload",
           replace(maskedReload, "  %step =", "  ctjs.set_property %base[%two], %zero\n  %step ="));
    run({.what = "AND bounds preserve fixed zero bits above the input interval",
         .body = replace(masked, "bitand %i, %one", "bitand %i, %two"),
         .arrays = "a:[zero,x]",
         .reads = "a[0]=zero; a[1]=x",
         .exit = "a -> {a,x}"});
    reject("the mask bound must remain inside the guard allocation",
           replace(masked, "%position = ctjs.binary_static bitand %i, %one",
                   "%outside = ctjs.binary add %i, %two\n"
                   "  %position = ctjs.binary_static bitand %outside, %two"));
    const auto fixedAnd =
        replace(replace(masked, "  %a =",
                        "  %eight = ctjs.constant #ctjs.number<4620693217682128896>\n"
                        "  %fixedMask = ctjs.constant #ctjs.number<4624633867356078080> "
                        "{storage_test_id = \"mask\"}\n  %a ="),
                "%position = ctjs.binary_static bitand %i, %one",
                "%biased = ctjs.binary add %i, %eight\n"
                "  %part = ctjs.binary_static bitand %biased, %fixedMask\n"
                "  %position = ctjs.binary sub %part, %eight");
    for (const auto & operands : {"%biased, %fixedMask", "%fixedMask, %biased"}) {
        run({.what = "AND fixed upper bits bound translated indices in either operand order",
             .body =
                 replace(fixedAnd, "bitand %biased, %fixedMask", "bitand " + std::string(operands)),
             .arrays = "a:[zero,zero]",
             .reads = "a[0]=zero; a[1]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "AND fixed upper bits survive negative Number conversion",
         .body = replace(fixedAnd, "add %i, %eight", "sub %i, %eight"),
         .arrays = "a:[zero,zero]",
         .reads = "a[0]=zero; a[1]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto fixedAndReload =
        replace(replace(replace(replace(fixedAnd, "[%one, %x]", "[%fixedMask, %zero, %x, %x]"),
                                "^header(%a, %zero", "^header(%a, %two"),
                        "  %biased =", "  %mask = ctjs.get_property %base[%zero]\n  %biased ="),
                "bitand %biased, %fixedMask", "bitand %biased, %mask");
    run({.what = "AND fixed upper bits exclude a reload below every written index",
         .body = fixedAndReload,
         .arrays = "a:[mask,zero,zero,zero]",
         .reads = "a[0]=mask; a[2]=zero; a[0]=mask; a[3]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "AND fixed-bit bounds retain children outside the actual writes",
         .body = replace(fixedAndReload, "[%fixedMask, %zero, %x, %x]", "[%fixedMask, %x, %x, %x]"),
         .arrays = "a:[mask,x,zero,zero]",
         .reads = "a[0]=mask; a[2]=zero; a[0]=mask; a[3]=zero",
         .exit = "a -> {a,x}"});
    reject("AND fixed-bit bounds reject a reload at a written index",
           replace(replace(fixedAndReload, "[%fixedMask, %zero, %x, %x]",
                           "[%zero, %zero, %fixedMask, %x]"),
                   "%mask = ctjs.get_property %base[%zero]",
                   "%mask = ctjs.get_property %base[%two]"));
    reject(
        "later stores invalidate fixed-bit AND reload evidence",
        replace(fixedAndReload, "  %step =", "  ctjs.set_property %base[%zero], %zero\n  %step ="));
    reject("clearing the fixed upper AND bit cannot preserve the translated own bound",
           replace(fixedAnd, "4624633867356078080", "4619567317775286272"));
    reject("two varying operands cannot supply an invariant mask",
           replace(masked, "bitand %i, %one", "bitand %i, %i"));
    for (const std::string value :
         {"#ctjs.number<13830554455654793216>", "#ctjs.number<4746794007248502784>",
          "#ctjs.number<4609434218613702656>", "#ctjs.string<\"1\">", "#ctjs.boolean<true>",
          "#ctjs.bigint<\"1\">"}) {
        const auto source =
            replace(replace(masked, "  %a =", "  %mask = ctjs.constant " + value + "\n  %a ="),
                    "bitand %i, %one", "bitand %i, %mask");
        if (value == "#ctjs.number<13830554455654793216>" ||
            value == "#ctjs.number<4746794007248502784>") {
            const bool negativeOne = value == "#ctjs.number<13830554455654793216>";
            run({.what = "signed mask literals retain only the children their exact writes miss",
                 .body = source,
                 .arrays = negativeOne ? "a:[zero,zero]" : "a:[zero,x]",
                 .reads = negativeOne ? "a[0]=zero; a[1]=zero" : "a[0]=zero; a[1]=x",
                 .exit = negativeOne ? "a -> {a}" : "a -> {a,x}"},
                negativeOne ? "x" : "");
        } else {
            reject("masks require exact integer Numbers", source);
        }
    }
    for (const auto & bits : {"4613937818241073152", "13830554455654793216"}) {
        auto inputGap =
            replace(replace(maskedReload, "  %a =",
                            "  %latticeMask = ctjs.constant #ctjs.number<" + std::string(bits) +
                                "> {storage_test_id = \"mask\"}\n  %a ="),
                    "[%x, %x, %one, %zero]", "[%zero, %x, %latticeMask, %x]");
        inputGap = replace(replace(inputGap, "^header(%a, %zero", "^header(%a, %one"),
                           "add %i, %one", "add %i, %two");
        for (const auto & operands : {"%i, %mask", "%mask, %i"}) {
            run({.what = "AND input low bits preserve a nonzero residue around a mask reload",
                 .body = replace(inputGap, "bitand %i, %mask", "bitand " + std::string(operands)),
                 .arrays = "a:[zero,zero,mask,zero]",
                 .reads = "a[2]=mask; a[1]=zero; a[2]=mask; a[3]=zero",
                 .exit = "a -> {a}"},
                "x");
        }
        run({.what = "AND input lattices retain an unwritten child outside their residue",
             .body =
                 replace(inputGap, "[%zero, %x, %latticeMask, %x]", "[%x, %x, %latticeMask, %x]"),
             .arrays = "a:[x,zero,mask,zero]",
             .reads = "a[2]=mask; a[1]=zero; a[2]=mask; a[3]=zero",
             .exit = "a -> {a,x}"});
        reject("AND input lattices reject a mask at a visited residue",
               replace(replace(inputGap, "[%zero, %x, %latticeMask, %x]",
                               "[%zero, %latticeMask, %zero, %x]"),
                       "%mask = ctjs.get_property %base[%two]",
                       "%mask = ctjs.get_property %base[%one]"));
        reject("later stores invalidate a mask inside an AND input-lattice gap",
               replace(inputGap, "  %step =", "  ctjs.set_property %base[%two], %zero\n  %step ="));
        reject("unit input strides cannot retain an AND low-bit gap",
               replace(inputGap, "add %i, %two", "add %i, %one"));
    }
    for (const std::string kind : {"bitor", "bitxor"}) {
        const bool isOr = kind == "bitor";
        const auto bitwise = replace(masked, "bitand", kind);
        for (const auto & operands : {"%i, %one", "%one, %i"}) {
            run({.what = "OR/XOR enclosures preserve both operand orders and exact writes",
                 .body = replace(bitwise, "%i, %one", operands),
                 .arrays = isOr ? "a:[one,zero]" : "a:[zero,zero]",
                 .reads = "a[0]=one; a[1]=zero",
                 .exit = "a -> {a}"},
                "x");
        }
        run({.what = "OR/XOR zero input ranges retain the exact zero key",
             .body = replace(replace(bitwise, "[%one, %x]", "[%x]"), "%i, %one", "%i, %zero"),
             .arrays = "a:[zero]",
             .reads = "a[0]=zero",
             .exit = "a -> {a}"},
            "x");
        run({.what = "OR/XOR replay preserves children saved before their overwrite",
             .body = replace(overwritten, "  ctjs.set_property %base[%i], %zero",
                             "  %position = ctjs.binary_static " + kind +
                                 " %i, %zero\n"
                                 "  ctjs.set_property %base[%position], %zero"),
             .arrays = "a:[zero,zero]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "OR/XOR bounds enclose nonmonotone writes without filling gaps",
             .body = replace(bitwise, "[%one, %x]", "[%x, %x, %x, %x]"),
             .arrays = isOr ? "a:[x,zero,x,zero]" : "a:[zero,zero,zero,zero]",
             .reads = "a[0]=x; a[1]=zero; a[2]=x; a[3]=zero",
             .exit = isOr ? "a -> {a,x}" : "a -> {a}"},
            isOr ? "" : "x");
        const auto gapReload =
            replace(replace(replace(bitwise, "[%one, %x]", "[%zero, %x, %one, %x]"), "add %i, %one",
                            "add %i, %two"),
                    "%position = ctjs.binary_static " + kind + " %i, %one",
                    "%mask = ctjs.get_property %base[%two]\n"
                    "  %position = ctjs.binary_static " +
                        kind + " %i, %mask");
        for (const auto & operands : {"%i, %mask", "%mask, %i"}) {
            run({.what = "OR/XOR input low bits preserve reloads inside the output interval",
                 .body = replace(gapReload, kind + " %i, %mask", kind + " " + operands),
                 .arrays = "a:[zero,zero,one,zero]",
                 .reads = "a[2]=one; a[0]=zero; a[2]=one; a[2]=one",
                 .exit = "a -> {a}"},
                "x");
        }
        run({.what = "OR/XOR low-bit lattices retain unvisited children between writes",
             .body = replace(gapReload, "[%zero, %x, %one, %x]",
                             "[%zero, %x, %one, %x, %x, %zero, %zero, %zero]"),
             .arrays = "a:[zero,zero,one,zero,x,zero,zero,zero]",
             .reads = "a[2]=one; a[0]=zero; a[2]=one; a[2]=one; "
                      "a[2]=one; a[4]=x; a[2]=one; a[6]=zero",
             .exit = "a -> {a,x}"});
        reject("OR/XOR low-bit lattices reject reloads at a possible visited residue",
               replace(replace(gapReload, "[%zero, %x, %one, %x]", "[%zero, %x, %zero, %one]"),
                       "%mask = ctjs.get_property %base[%two]",
                       "%mask = ctjs.get_property %base[%three]"));
        reject(
            "later stores invalidate a reload in an OR/XOR low-bit gap",
            replace(gapReload, "  %step =", "  ctjs.set_property %base[%two], %zero\n  %step ="));
        const auto oddStride = replace(gapReload, "add %i, %two", "add %i, %three");
        if (isOr) {
            run({.what = "OR mask low bits preserve the original odd-stride reload",
                 .body = oddStride,
                 .arrays = "a:[zero,zero,one,zero]",
                 .reads = "a[2]=one; a[0]=zero; a[2]=one; a[3]=zero",
                 .exit = "a -> {a}"},
                "x");
            for (const auto & operands : {"%i, %mask", "%mask, %i"}) {
                run({.what = "OR mask low bits preserve a gap with unit input stride",
                     .body = replace(replace(gapReload, "add %i, %two", "add %i, %one"),
                                     "bitor %i, %mask", "bitor " + std::string(operands)),
                     .arrays = "a:[zero,zero,one,zero]",
                     .reads = "a[2]=one; a[0]=zero; a[2]=one; a[1]=zero; a[2]=one; a[2]=one; "
                              "a[2]=one; a[3]=zero",
                     .exit = "a -> {a}"},
                    "x");
            }
            reject(
                "OR input and mask periods combine without multiplying away a write",
                replace(
                    replace(replace(replace(replace(gapReload, "  %a =",
                                                    "  %four = ctjs.binary add %two, %two\n"
                                                    "  %five = ctjs.binary add %four, %one\n"
                                                    "  %a ="),
                                            "[%zero, %x, %one, %x]",
                                            "[%zero, %x, %zero, %zero, %zero, %one, %zero, %zero]"),
                                    "add %i, %two", "add %i, %four"),
                            "%mask = ctjs.get_property %base[%two]",
                            "%mask = ctjs.get_property %base[%five]"),
                    "bitor %i, %mask", "bitor %mask, %i"));
        } else {
            reject("odd input strides cannot retain a low-bit gap through XOR", oddStride);
        }
        reject("OR/XOR bounds cannot miss a store beyond the guard array",
               replace(bitwise, "[%one, %x]", "[%one, %x, %zero]"));
        reject("OR/XOR require one invariant operand", replace(bitwise, "%i, %one", "%i, %i"));
        reject("OR/XOR refuse varying inputs crossing converted zero",
               replace(bitwise, "%position = ctjs.binary_static " + kind + " %i, %one",
                       "%negative = ctjs.unary neg %i\n"
                       "  %position = ctjs.binary_static " +
                           kind + " %negative, %one"));
        reject("OR/XOR refuse varying inputs crossing a signed conversion boundary",
               replace(bitwise, "%position = ctjs.binary_static " + kind + " %i, %one",
                       "%maximum = ctjs.constant #ctjs.number<4746794007244308480>\n"
                       "  %large = ctjs.binary add %maximum, %i\n"
                       "  %position = ctjs.binary_static " +
                           kind + " %large, %one"));
        for (const auto & value :
             {"#ctjs.number<13830554455654793216>", "#ctjs.number<4746794007248502784>",
              "#ctjs.number<4609434218613702656>", "#ctjs.string<\"1\">", "#ctjs.boolean<true>",
              "#ctjs.bigint<\"1\">"}) {
            reject("OR/XOR require integer Number masks and nonnegative final keys",
                   replace(replace(bitwise, "  %a =",
                                   "  %mask = ctjs.constant " + std::string(value) + "\n  %a ="),
                           "%i, %one", "%i, %mask"));
        }
    }
    for (const std::string kind : {"bitand", "bitor"}) {
        const bool isAnd = kind == "bitand";
        const std::string contents =
            isAnd ? "[%x, %zero, %periodMask, %zero, %x, %zero, %zero, %zero]"
                  : "[%zero, %zero, %x, %zero, %periodMask, %zero, %x, %zero]";
        const std::string guard = isAnd ? "%periodTwo" : "%periodFour";
        const char * values = isAnd ? "a:[zero,zero,mask,zero,zero,zero,zero,zero]"
                                    : "a:[zero,zero,zero,zero,mask,zero,zero,zero]";
        const char * reads = isAnd ? "a[2]=mask; a[0]=zero; a[2]=mask; a[2]=mask; a[2]=mask; "
                                     "a[4]=zero; a[2]=mask; a[6]=zero"
                                   : "a[4]=mask; a[0]=zero; a[4]=mask; a[2]=zero; a[4]=mask; "
                                     "a[4]=mask; a[4]=mask; a[6]=zero";
        const auto combinedBits = replace(
            replace(replace(replace(masked, "[%one, %x]", contents), "  %a =",
                            "  %periodTwo = ctjs.constant #ctjs.number<4611686018427387904>\n"
                            "  %periodFour = ctjs.constant #ctjs.number<4616189618054758400>\n"
                            "  %periodSix = ctjs.constant #ctjs.number<4618441417868443648>\n"
                            "  %periodEight = ctjs.constant #ctjs.number<4620693217682128896>\n"
                            "  %periodSign = ctjs.constant #ctjs.number<4746794007248502784>\n"
                            "  %periodMask = ctjs.constant #ctjs.number<" +
                                std::string(isAnd ? "4617315517961601024" : "4611686018427387904") +
                                "> {storage_test_id = \"mask\"}\n  %a ="),
                    "add %i, %one", "add %i, %periodTwo"),
            "%position = ctjs.binary_static bitand %i, %one",
            "%mask = ctjs.get_property %base[" + guard +
                "]\n"
                "  %bits = ctjs.binary_static " +
                kind +
                " %i, %mask\n"
                "  %position = ctjs.unary plus %bits");
        for (const auto & operands : {"%i, %mask", "%mask, %i"}) {
            run({.what = "interleaved fixed input and mask bits widen the output period",
                 .body = replace(combinedBits, kind + " %i, %mask", kind + " " + operands),
                 .arrays = values,
                 .reads = reads,
                 .exit = "a -> {a}"},
                "x");
        }
        run({.what = "a composed mask period retains an odd factor in the input stride",
             .body = replace(combinedBits, "add %i, %periodTwo", "add %i, %periodSix"),
             .arrays = values,
             .reads = isAnd ? "a[2]=mask; a[0]=zero; a[2]=mask; a[6]=zero"
                            : "a[4]=mask; a[0]=zero; a[4]=mask; a[6]=zero",
             .exit = "a -> {a}"},
            "x");
        for (const auto & bias : {"%periodEight", "%periodSign"}) {
            auto body = replace(combinedBits, "%bits = ctjs.binary_static " + kind + " %i, %mask",
                                "%biased = ctjs.binary sub %i, " + std::string(bias) +
                                    "\n"
                                    "  %bits = ctjs.binary_static " +
                                    kind + " %biased, %mask");
            if (!isAnd) {
                body = replace(body, "ctjs.unary plus %bits",
                               "ctjs.binary add %bits, " + std::string(bias));
            }
            run({.what = "combined mask periods survive signed input conversion",
                 .body = body,
                 .arrays = values,
                 .reads = reads,
                 .exit = "a -> {a}"},
                "x");
        }
        run({.what = "combined mask periods preserve unvisited child identities",
             .body = replace(combinedBits, contents,
                             isAnd ? "[%x, %x, %periodMask, %zero, %x, %zero, %zero, %zero]"
                                   : "[%x, %zero, %x, %zero, %periodMask, %zero, %x, %zero]"),
             .arrays = isAnd ? "a:[zero,x,mask,zero,zero,zero,zero,zero]"
                             : "a:[x,zero,zero,zero,mask,zero,zero,zero]",
             .reads = isAnd ? reads
                            : "a[4]=mask; a[0]=x; a[4]=mask; a[2]=zero; a[4]=mask; "
                              "a[4]=mask; a[4]=mask; a[6]=zero",
             .exit = "a -> {a,x}"});
        auto phased =
            replace(replace(combinedBits, contents,
                            isAnd ? "[%zero, %x, %zero, %periodMask, %zero, %x, %zero, %zero]"
                                  : "[%zero, %zero, %zero, %x, %zero, %periodMask, %zero, %x]"),
                    "  %a =",
                    "  %periodThree = ctjs.binary add %periodTwo, %one\n"
                    "  %periodFive = ctjs.binary add %periodFour, %one\n  %a =");
        phased = replace(phased, "%base[" + guard + "]",
                         isAnd ? "%base[%periodThree]" : "%base[%periodFive]");
        phased = replace(phased, "%bits = ctjs.binary_static " + kind + " %i, %mask",
                         "%biased = ctjs.binary add %i, %one\n"
                         "  %bits = ctjs.binary_static " +
                             kind + " %biased, %mask");
        run({.what = "combined mask periods retain the exact nonzero low-bit residue",
             .body = phased,
             .arrays = isAnd ? "a:[zero,zero,zero,mask,zero,zero,zero,zero]"
                             : "a:[zero,zero,zero,zero,zero,mask,zero,zero]",
             .reads = isAnd ? "a[3]=mask; a[0]=zero; a[3]=mask; a[2]=zero; a[3]=mask; a[4]=zero; "
                              "a[3]=mask; a[6]=zero"
                            : "a[5]=mask; a[0]=zero; a[5]=mask; a[2]=zero; a[5]=mask; a[4]=zero; "
                              "a[5]=mask; a[6]=zero",
             .exit = "a -> {a}"},
            "x");
        const auto saved = replace(replace(combinedBits, "  cf.br ^header(%a,",
                                           "  %periodSaved = ctjs.get_property %a[" +
                                               std::string(isAnd ? "%zero" : "%periodTwo") +
                                               "]\n  cf.br ^header(%a,"),
                                   "ctjs.return %a", "ctjs.return %periodSaved");
        run({.what = "combined periods retain snapshots read before masked writes",
             .body = saved,
             .arrays = values,
             .reads = isAnd ? "a[0]=x; a[2]=mask; a[0]=zero; a[2]=mask; a[2]=mask; a[2]=mask; "
                              "a[4]=zero; a[2]=mask; a[6]=zero"
                            : "a[2]=x; a[4]=mask; a[0]=zero; a[4]=mask; a[2]=zero; a[4]=mask; "
                              "a[4]=mask; a[4]=mask; a[6]=zero",
             .exit = "x -> {x}"});
        reject(
            "combined input and mask periods cannot hide an actual mask overwrite",
            replace(replace(combinedBits, contents,
                            isAnd ? "[%x, %zero, %zero, %zero, %periodMask, %zero, %zero, %zero]"
                                  : "[%zero, %zero, %x, %zero, %zero, %zero, %periodMask, %zero]"),
                    "%base[" + guard + "]", isAnd ? "%base[%periodFour]" : "%base[%periodSix]"));
        reject("combined mask periods still census later writes to a reload slot",
               replace(combinedBits,
                       "  %step =", "  ctjs.set_property %base[" + guard + "], %zero\n  %step ="));
        reject("unit input strides cannot borrow a fixed low bit for a mask period",
               replace(combinedBits, "add %i, %periodTwo", "add %i, %one"));
        reject("changing a required mask bit invalidates the combined period",
               replace(combinedBits,
                       "#ctjs.number<" +
                           std::string(isAnd ? "4617315517961601024" : "4611686018427387904") +
                           "> {storage_test_id = \"mask\"}",
                       "#ctjs.number<" + std::string(isAnd ? "4619567317775286272" : "0") +
                           "> {storage_test_id = \"mask\"}"));
    }
    for (const std::string kind : {"bitand", "bitor"}) {
        const bool isAnd = kind == "bitand";
        const std::string contents = isAnd ? "[%x, %zero, %x, %zero, %x, %roundMask]"
                                           : "[%roundMask, %x, %zero, %x, %zero, %x]";
        const std::string guard = isAnd ? "%roundFive" : "%zero";
        const char * values =
            isAnd ? "a:[zero,zero,zero,zero,zero,mask]" : "a:[mask,zero,zero,zero,zero,zero]";
        const char * reads =
            isAnd ? "a[5]=mask; a[0]=zero; a[5]=mask; a[1]=zero; a[5]=mask; a[2]=zero; "
                    "a[5]=mask; a[3]=zero; a[5]=mask; a[4]=zero; a[5]=mask; a[5]=mask"
                  : "a[0]=mask; a[0]=mask; a[0]=mask; a[1]=zero; a[0]=mask; a[2]=zero; "
                    "a[0]=mask; a[3]=zero; a[0]=mask; a[4]=zero; a[0]=mask; a[5]=zero";
        const auto rounded = replace(
            replace(replace(masked, "[%one, %x]", contents), "  %a =",
                    "  %roundTwo = ctjs.constant #ctjs.number<4611686018427387904>\n"
                    "  %roundThree = ctjs.constant #ctjs.number<4613937818241073152>\n"
                    "  %roundFive = ctjs.constant #ctjs.number<4617315517961601024>\n"
                    "  %roundSign = ctjs.constant #ctjs.number<4746794007248502784>\n"
                    "  %roundMaximum = ctjs.constant #ctjs.number<4746794007244308480>\n"
                    "  %roundLow = ctjs.constant #ctjs.number<13974669643726454784>\n"
                    "  %roundMask = ctjs.constant #ctjs.number<" +
                        std::string(isAnd ? "13835058055282163712" : "4607182418800017408") +
                        "> {storage_test_id = \"mask\"}\n  %a ="),
            "%position = ctjs.binary_static bitand %i, %one",
            "%mask = ctjs.get_property %base[" + guard +
                "]\n"
                "  %bits = ctjs.binary_static " +
                kind +
                " %i, %mask\n"
                "  %position = ctjs.unary plus %bits");
        for (const auto & operands : {"%i, %mask", "%mask, %i"}) {
            run({.what = "low-bit rounding bounds stop at the last actual write",
                 .body = replace(rounded, kind + " %i, %mask", kind + " " + operands),
                 .arrays = values,
                 .reads = reads,
                 .exit = "a -> {a}"},
                "x");
        }
        for (const auto & expression : {"%part = ctjs.binary sub %i, %roundTwo\n"
                                        "  %bits = ctjs.binary_static " +
                                            kind +
                                            " %part, %mask\n"
                                            "  %position = ctjs.binary add %bits, %roundTwo",
                                        "%part = ctjs.binary add %roundSign, %i\n"
                                        "  %bits = ctjs.binary_static " +
                                            kind +
                                            " %part, %mask\n"
                                            "  %position = ctjs.binary add %bits, %roundSign",
                                        "%part = ctjs.binary add %roundLow, %i\n"
                                        "  %bits = ctjs.binary_static " +
                                            kind +
                                            " %part, %mask\n"
                                            "  %position = ctjs.binary sub %bits, %roundTwo"}) {
            run({.what = "low-bit rounding stays monotone within each signed conversion band",
                 .body = replace(rounded,
                                 "%bits = ctjs.binary_static " + kind +
                                     " %i, %mask\n"
                                     "  %position = ctjs.unary plus %bits",
                                 expression),
                 .arrays = values,
                 .reads = reads,
                 .exit = "a -> {a}"},
                "x");
        }
        run({.what = "low-bit rounding retains children outside the actual writes",
             .body = replace(rounded, contents,
                             isAnd ? "[%x, %x, %x, %zero, %x, %roundMask]"
                                   : "[%roundMask, %x, %x, %x, %zero, %x]"),
             .arrays = isAnd ? "a:[zero,x,zero,zero,zero,mask]" : "a:[mask,zero,x,zero,zero,zero]",
             .reads = isAnd ? "a[5]=mask; a[0]=zero; a[5]=mask; a[1]=x; a[5]=mask; a[2]=zero; "
                              "a[5]=mask; a[3]=zero; a[5]=mask; a[4]=zero; a[5]=mask; a[5]=mask"
                            : "a[0]=mask; a[0]=mask; a[0]=mask; a[1]=zero; a[0]=mask; a[2]=x; "
                              "a[0]=mask; a[3]=zero; a[0]=mask; a[4]=zero; a[0]=mask; a[5]=zero",
             .exit = "a -> {a,x}"});
        const auto saved =
            replace(replace(rounded, "  cf.br ^header(%a,",
                            "  %roundSaved = ctjs.get_property %a[" +
                                std::string(isAnd ? "%zero" : "%one") + "]\n  cf.br ^header(%a,"),
                    "ctjs.return %a", "ctjs.return %roundSaved");
        run({.what = "low-bit rounding preserves snapshots read before overwrite",
             .body = saved,
             .arrays = values,
             .reads =
                 isAnd
                     ? "a[0]=x; a[5]=mask; a[0]=zero; a[5]=mask; a[1]=zero; a[5]=mask; "
                       "a[2]=zero; a[5]=mask; a[3]=zero; a[5]=mask; a[4]=zero; a[5]=mask; a[5]=mask"
                     : "a[1]=x; a[0]=mask; a[0]=mask; a[0]=mask; a[1]=zero; a[0]=mask; "
                       "a[2]=zero; a[0]=mask; a[3]=zero; a[0]=mask; a[4]=zero; a[0]=mask; "
                       "a[5]=zero",
             .exit = "x -> {x}"});
        reject("low-bit rounding cannot borrow an overwritten mask",
               replace(replace(rounded, contents,
                               isAnd ? "[%x, %zero, %roundMask, %zero, %x, %zero]"
                                     : "[%zero, %x, %zero, %roundMask, %zero, %x]"),
                       "%base[" + guard + "]", isAnd ? "%base[%roundTwo]" : "%base[%roundThree]"));
        reject("low-bit rounding retains the complete later-store census",
               replace(rounded,
                       "  %step =", "  ctjs.set_property %base[" + guard + "], %zero\n  %step ="));
        reject(
            "low-bit rounding cannot preserve an odd input stride",
            replace(
                replace(replace(rounded, contents,
                                isAnd ? "[%x, %zero, %roundMask, %zero, %zero, %zero, %x]"
                                      : "[%zero, %x, %zero, %roundMask, %zero, %zero, %zero, %x]"),
                        "%base[" + guard + "]", isAnd ? "%base[%roundTwo]" : "%base[%roundThree]"),
                "add %i, %one", "add %i, %roundThree"));
        auto interior = replace(
            replace(replace(rounded, contents, "[%roundMask, %x, %zero, %zero, %x]"),
                    "^header(%a, %zero", "^header(%a, %one"),
            "#ctjs.number<" + std::string(isAnd ? "13835058055282163712" : "4607182418800017408") +
                "> {storage_test_id = \"mask\"}",
            "#ctjs.number<" + std::string(isAnd ? "13837309855095848960" : "4611686018427387904") +
                "> {storage_test_id = \"mask\"}");
        if (isAnd) {
            interior = replace(interior, "%base[%roundFive]", "%base[%zero]");
        } else {
            interior =
                replace(interior, "ctjs.unary plus %bits", "ctjs.binary sub %bits, %roundTwo");
        }
        reject("noncontiguous masks cannot hide an interior write below both endpoints", interior);
        for (const auto & input :
             {"ctjs.binary add %roundMaximum, %i", "ctjs.binary sub %i, %roundSign"}) {
            auto crossing = replace(rounded, "%bits = ctjs.binary_static " + kind + " %i, %mask",
                                    "%part = " + std::string(input) +
                                        "\n"
                                        "  %bits = ctjs.binary_static " +
                                        kind + " %part, %mask");
            // The second input starts in the signed band; move it one below
            // INT32_MIN so the original source crosses the lower discontinuity.
            if (std::string(input) == "ctjs.binary sub %i, %roundSign") {
                crossing = replace(crossing, "ctjs.binary sub %i, %roundSign",
                                   "ctjs.binary sub %i, %roundBeyond");
                crossing =
                    replace(crossing, "  %roundLow =",
                            "  %roundBeyond = ctjs.binary add %roundSign, %one\n  %roundLow =");
            }
            reject("low-bit rounding retains signed conversion guards through composition",
                   replace(crossing, "ctjs.unary plus %bits",
                           "ctjs.binary_static bitand %bits, %one"));
        }

        const auto fixedRounding = replace(
            replace(replace(rounded, "  %a =",
                            "  %roundEight = ctjs.constant #ctjs.number<4620693217682128896>\n"
                            "  %a ="),
                    "#ctjs.number<" +
                        std::string(isAnd ? "13835058055282163712" : "4607182418800017408") +
                        "> {storage_test_id = \"mask\"}",
                    "#ctjs.number<" +
                        std::string(isAnd ? "4618441417868443648" : "4621256167635550208") +
                        "> {storage_test_id = \"mask\"}"),
            "%bits = ctjs.binary_static " + kind +
                " %i, %mask\n"
                "  %position = ctjs.unary plus %bits",
            "%part = ctjs.binary add %i, %roundEight\n"
            "  %bits = ctjs.binary_static " +
                kind +
                " %part, %mask\n"
                "  %position = " +
                (isAnd ? "ctjs.unary plus %bits" : "ctjs.binary sub %bits, %roundEight"));
        for (const auto & operands : {"%part, %mask", "%mask, %part"}) {
            run({.what = "fixed upper bits retain exact low-suffix rounding endpoints",
                 .body = replace(fixedRounding, kind + " %part, %mask", kind + " " + operands),
                 .arrays = values,
                 .reads = reads,
                 .exit = "a -> {a}"},
                "x");
        }
        auto negativeFixedRounding = replace(fixedRounding, "ctjs.binary add %i, %roundEight",
                                             "ctjs.binary sub %i, %roundEight");
        if (!isAnd) {
            negativeFixedRounding =
                replace(negativeFixedRounding, "ctjs.binary sub %bits, %roundEight",
                        "ctjs.binary add %bits, %roundEight");
        }
        run({.what = "negative fixed upper bits retain exact rounding endpoints",
             .body = negativeFixedRounding,
             .arrays = values,
             .reads = reads,
             .exit = "a -> {a}"},
            "x");
        run({.what = "fixed-bit rounding leaves unwritten children reachable",
             .body = replace(fixedRounding, contents,
                             isAnd ? "[%x, %x, %x, %zero, %x, %roundMask]"
                                   : "[%roundMask, %x, %x, %x, %zero, %x]"),
             .arrays = isAnd ? "a:[zero,x,zero,zero,zero,mask]" : "a:[mask,zero,x,zero,zero,zero]",
             .reads = isAnd ? "a[5]=mask; a[0]=zero; a[5]=mask; a[1]=x; a[5]=mask; a[2]=zero; "
                              "a[5]=mask; a[3]=zero; a[5]=mask; a[4]=zero; a[5]=mask; a[5]=mask"
                            : "a[0]=mask; a[0]=mask; a[0]=mask; a[1]=zero; a[0]=mask; a[2]=x; "
                              "a[0]=mask; a[3]=zero; a[0]=mask; a[4]=zero; a[0]=mask; a[5]=zero",
             .exit = "a -> {a,x}"});
        const auto lowRounding = replace(
            replace(replace(rounded, contents,
                            isAnd ? "[%x, %zero, %zero, %zero, %x, %roundMask, %zero, %zero, %x, "
                                    "%zero, %zero, %zero]"
                                  : "[%roundMask, %zero, %x, %zero, %zero, %zero, %x, %zero, "
                                    "%zero, %zero, %x, %zero]"),
                    "#ctjs.number<" +
                        std::string(isAnd ? "13835058055282163712" : "4607182418800017408") +
                        "> {storage_test_id = \"mask\"}",
                    "#ctjs.number<" +
                        std::string(isAnd ? "13837309855095848960" : "4611686018427387904") +
                        "> {storage_test_id = \"mask\"}"),
            "add %i, %one", "add %i, %roundTwo");
        run({.what = "fixed low input bits complete a monotone rounding suffix",
             .body = lowRounding,
             .arrays = isAnd ? "a:[zero,zero,zero,zero,zero,mask,zero,zero,zero,zero,zero,zero]"
                             : "a:[mask,zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,zero]",
             .reads = isAnd ? "a[5]=mask; a[0]=zero; a[5]=mask; a[2]=zero; a[5]=mask; a[4]=zero; "
                              "a[5]=mask; a[6]=zero; a[5]=mask; a[8]=zero; a[5]=mask; a[10]=zero"
                            : "a[0]=mask; a[0]=mask; a[0]=mask; a[2]=zero; a[0]=mask; a[4]=zero; "
                              "a[0]=mask; a[6]=zero; a[0]=mask; a[8]=zero; a[0]=mask; a[10]=zero",
             .exit = "a -> {a}"},
            "x");
        reject("fixed-bit rounding still checks overlapping mask reloads",
               replace(replace(fixedRounding, contents,
                               isAnd ? "[%x, %zero, %roundMask, %zero, %x, %zero]"
                                     : "[%zero, %x, %zero, %roundMask, %zero, %x]"),
                       "%base[" + guard + "]", isAnd ? "%base[%roundTwo]" : "%base[%roundThree]"));
        reject("fixed-bit rounding retains later writes in its complete census",
               replace(fixedRounding,
                       "  %step =", "  ctjs.set_property %base[" + guard + "], %zero\n  %step ="));
        reject("a varying low bit cannot complete the rounding suffix",
               replace(lowRounding, "add %i, %roundTwo", "add %i, %one"));
        reject("a varying interior gap cannot borrow fixed-bit rounding endpoints",
               replace(fixedRounding,
                       "#ctjs.number<" +
                           std::string(isAnd ? "4618441417868443648" : "4621256167635550208") +
                           "> {storage_test_id = \"mask\"}",
                       "#ctjs.number<" +
                           std::string(isAnd ? "4617315517961601024" : "4621819117588971520") +
                           "> {storage_test_id = \"mask\"}"));
    }
    for (const std::string kind : {"bitand", "bitor", "bitxor"}) {
        const bool isAnd = kind == "bitand";
        const bool isOr = kind == "bitor";
        const std::string contents = isAnd
                                         ? "[%x, %x, %pairMask, %zero, %zero, %zero]"
                                         : "[%zero, %zero, %x, %zero, %pairMask, %zero, %zero, %x]";
        const std::string guard = isAnd ? "%pairTwo" : "%pairFour";
        const char * values = isAnd ? "a:[zero,zero,mask,zero,zero,zero]"
                                    : "a:[zero,zero,zero,zero,mask,zero,zero,zero]";
        const char * reads = isAnd ? "a[2]=mask; a[0]=zero; a[2]=mask; a[5]=zero"
                                   : "a[4]=mask; a[0]=zero; a[4]=mask; a[5]=zero";
        const auto pair =
            replace(replace(replace(masked, "[%one, %x]", contents), "  %a =",
                            "  %pairTwo = ctjs.constant #ctjs.number<4611686018427387904>\n"
                            "  %pairThree = ctjs.constant #ctjs.number<4613937818241073152>\n"
                            "  %pairFour = ctjs.constant #ctjs.number<4616189618054758400>\n"
                            "  %pairFive = ctjs.constant #ctjs.number<4617315517961601024>\n"
                            "  %pairSeven = ctjs.constant #ctjs.number<4619567317775286272>\n"
                            "  %pairEight = ctjs.constant #ctjs.number<4620693217682128896>\n"
                            "  %pairHigh = ctjs.constant #ctjs.number<4746794007240114176>\n"
                            "  %pairLow = ctjs.constant #ctjs.number<13970166044105375744>\n"
                            "  %pairMask = ctjs.constant #ctjs.number<" +
                                std::string(isAnd ? "4613937818241073152" : "4611686018427387904") +
                                "> {storage_test_id = \"mask\"}\n  %a ="),
                    "%position = ctjs.binary_static bitand %i, %one",
                    "%pairReload = ctjs.get_property %base[" + guard +
                        "]\n"
                        "  %pairBits = ctjs.binary_static " +
                        kind +
                        " %i, %pairReload\n"
                        "  %position = ctjs.unary plus %pairBits");
        const auto twoPoints = replace(pair, "add %i, %one", "add %i, %pairFive");
        for (const auto & operands : {"%i, %pairReload", "%pairReload, %i"}) {
            run({.what = "two-point bitwise images retain exact bounds and gaps",
                 .body = replace(twoPoints, kind + " %i, %pairReload", kind + " " + operands),
                 .arrays = values,
                 .reads = reads,
                 .exit = "a -> {a}"},
                "x");
        }
        run({.what = "negative two-point bitwise images retain exact signed order",
             .body = replace(
                 replace(twoPoints, "%pairBits = ctjs.binary_static " + kind + " %i, %pairReload",
                         "%pairInput = ctjs.binary sub %i, %pairEight\n"
                         "  %pairBits = ctjs.binary_static " +
                             kind + " %pairInput, %pairReload"),
                 "ctjs.unary plus %pairBits",
                 isAnd ? "ctjs.unary plus %pairBits" : "ctjs.binary add %pairBits, %pairEight"),
             .arrays = values,
             .reads = reads,
             .exit = "a -> {a}"},
            "x");
        run({.what = "two-point gaps retain children outside actual writes",
             .body = replace(twoPoints, contents,
                             isAnd ? "[%x, %x, %pairMask, %x, %zero, %zero]"
                                   : "[%zero, %x, %x, %zero, %pairMask, %zero, %zero, %x]"),
             .arrays = isAnd ? "a:[zero,zero,mask,x,zero,zero]"
                             : "a:[zero,x,zero,zero,mask,zero,zero,zero]",
             .reads = reads,
             .exit = "a -> {a,x}"});
        const auto saved = replace(replace(twoPoints, "  cf.br ^header(%a,",
                                           "  %pairSaved = ctjs.get_property %a[" +
                                               std::string(isAnd ? "%zero" : "%pairTwo") +
                                               "]\n"
                                               "  cf.br ^header(%a,"),
                                   "ctjs.return %a", "ctjs.return %pairSaved");
        run({.what = "two-point writes preserve a previously saved child",
             .body = saved,
             .arrays = values,
             .reads = isAnd ? "a[0]=x; a[2]=mask; a[0]=zero; a[2]=mask; a[5]=zero"
                            : "a[2]=x; a[4]=mask; a[0]=zero; a[4]=mask; a[5]=zero",
             .exit = "x -> {x}"});
        reject("two-point bounds retain overlap checks at their exact endpoint",
               replace(replace(twoPoints, contents,
                               isAnd ? "[%pairMask, %x, %zero, %zero, %zero, %zero]"
                                     : "[%zero, %zero, %pairMask, %zero, %zero, %zero, %zero, %x]"),
                       "%base[" + guard + "]", isAnd ? "%base[%zero]" : "%base[%pairTwo]"));
        reject("two-point bounds retain the complete later-store census",
               replace(twoPoints,
                       "  %step =", "  ctjs.set_property %base[" + guard + "], %zero\n  %step ="));
        auto interior =
            replace(pair, "add %i, %one", isAnd ? "add %i, %pairTwo" : "add %i, %pairThree");
        if (!isAnd) {
            interior =
                replace(replace(interior, contents,
                                isOr ? "[%zero, %zero, %x, %pairMask, %zero, %zero, %x, %zero]"
                                     : "[%zero, %pairMask, %x, %zero, %x, %zero, %zero, %zero]"),
                        "%base[%pairFour]", isOr ? "%base[%pairThree]" : "%base[%one]");
        }
        reject("three-point masks cannot omit a distinct interior write", interior);
        for (const bool upper : {true, false}) {
            const std::string crossedContents =
                isAnd  ? (upper ? "[%pairMask, %zero, %x, %x, %zero, %zero]"
                                : "[%x, %zero, %pairMask, %x, %zero, %zero]")
                : isOr ? (upper ? "[%zero, %zero, %zero, %x, %pairMask, %zero, %x, %zero]"
                                : "[%zero, %zero, %zero, %zero, %pairMask, %zero, %x, %x]")
                       : (upper ? "[%zero, %x, %pairMask, %zero, %x, %zero, %zero, %zero]"
                                : "[%zero, %zero, %zero, %zero, %pairMask, %x, %x, %zero]");
            const std::string crossedGuard = isAnd ? (upper ? "%zero" : "%pairTwo")
                                                   : (!isOr && upper ? "%pairTwo" : "%pairFour");
            auto crossing = replace(replace(replace(twoPoints, contents, crossedContents),
                                            "%base[" + guard + "]", "%base[" + crossedGuard + "]"),
                                    "%pairBits = ctjs.binary_static " + kind + " %i, %pairReload",
                                    "%pairInput = ctjs.binary add %i, " +
                                        std::string(upper ? "%pairHigh" : "%pairLow") +
                                        "\n  %pairBits = ctjs.binary_static " + kind +
                                        " %pairInput, %pairReload");
            crossing = replace(crossing, "ctjs.unary plus %pairBits",
                               "ctjs.binary_static bitand %pairBits, %pairSeven");
            run({.what = "two-point masks compose across either signed conversion jump",
                 .body = crossing,
                 .arrays = isAnd ? (upper ? "a:[mask,zero,zero,zero,zero,zero]"
                                          : "a:[zero,zero,mask,zero,zero,zero]")
                                 : (!isOr && upper ? "a:[zero,zero,mask,zero,zero,zero,zero,zero]"
                                                   : "a:[zero,zero,zero,zero,mask,zero,zero,zero]"),
                 .reads = isAnd ? (upper ? "a[0]=mask; a[0]=mask; a[0]=mask; a[5]=zero"
                                         : "a[2]=mask; a[0]=x; a[2]=mask; a[5]=zero")
                                : (!isOr && upper ? "a[2]=mask; a[0]=zero; a[2]=mask; a[5]=zero"
                                                  : "a[4]=mask; a[0]=zero; a[4]=mask; a[5]=zero"),
                 .exit = "a -> {a}"},
                "x");
        }
        if (isAnd) {
            run({.what = "coincident two-point images keep a positive enclosing stride",
                 .body = replace(
                     replace(twoPoints, contents, "[%x, %zero, %pairMask, %zero, %zero, %zero]"),
                     "#ctjs.number<4613937818241073152> {storage_test_id = \"mask\"}",
                     "#ctjs.number<4611686018427387904> {storage_test_id = \"mask\"}"),
                 .arrays = values,
                 .reads = reads,
                 .exit = "a -> {a}"},
                "x");
        }
    }
    const auto signedBits = replace(masked, "  %a =",
                                    "  %maximum = ctjs.constant #ctjs.number<4746794007244308480>\n"
                                    "  %sign = ctjs.constant #ctjs.number<4746794007248502784>\n"
                                    "  %negativeSign = ctjs.unary neg %sign\n"
                                    "  %negativeOne = ctjs.unary neg %one\n"
                                    "  %low = ctjs.constant #ctjs.number<13974669643726454784>\n"
                                    "  %a =");
    for (const std::string kind : {"bitor", "bitxor", "bitand"}) {
        const auto literal = kind == "bitand" ? "13830554455654793216" : "0";
        const auto identity =
            replace(replace(replace(replace(signedBits, "[%one, %x]", "[%x, %identity, %zero, %x]"),
                                    "add %i, %one", "add %i, %three"),
                            "  %a =",
                            "  %identity = ctjs.constant #ctjs.number<" + std::string(literal) +
                                "> {storage_test_id = \"mask\"}\n  %a ="),
                    "%position = ctjs.binary_static bitand %i, %one",
                    "%mask = ctjs.get_property %base[%one]\n"
                    "  %position = ctjs.binary_static " +
                        kind + " %i, %mask");
        for (const auto & operands : {"%i, %mask", "%mask, %i"}) {
            run({.what = "identity bitwise masks preserve odd strides through either operand order",
                 .body = replace(identity, kind + " %i, %mask", kind + " " + operands),
                 .arrays = "a:[zero,mask,zero,zero]",
                 .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[3]=zero",
                 .exit = "a -> {a}"},
                "x");
        }
        run({.what = "identity bitwise replay retains a child in an odd-stride gap",
             .body = replace(identity, "[%x, %identity, %zero, %x]", "[%x, %identity, %x, %x]"),
             .arrays = "a:[zero,mask,x,zero]",
             .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[3]=zero",
             .exit = "a -> {a,x}"});
        for (const auto & expression : {"%part = ctjs.binary sub %i, %one\n"
                                        "  %bits = ctjs.binary_static " +
                                            kind +
                                            " %part, %mask\n"
                                            "  %position = ctjs.binary add %bits, %one",
                                        "%part = ctjs.binary add %sign, %i\n"
                                        "  %bits = ctjs.binary_static " +
                                            kind +
                                            " %part, %mask\n"
                                            "  %position = ctjs.binary add %bits, %sign",
                                        "%part = ctjs.binary add %low, %i\n"
                                        "  %bits = ctjs.binary_static " +
                                            kind +
                                            " %part, %mask\n"
                                            "  %position = ctjs.binary sub %bits, %two"}) {
            run({.what = "identity masks retain odd strides within each signed conversion band",
                 .body = replace(identity, "%position = ctjs.binary_static " + kind + " %i, %mask",
                                 expression),
                 .arrays = "a:[zero,mask,zero,zero]",
                 .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[3]=zero",
                 .exit = "a -> {a}"},
                "x");
        }
        reject(
            "identity masks cannot reload a visited lattice point",
            replace(replace(identity, "[%x, %identity, %zero, %x]", "[%identity, %x, %zero, %x]"),
                    "%mask = ctjs.get_property %base[%one]",
                    "%mask = ctjs.get_property %base[%zero]"));
        reject("identity masks retain every later store in the reload census",
               replace(identity, "  %step =", "  ctjs.set_property %base[%one], %zero\n  %step ="));
        reject("nonidentity masks cannot borrow an odd input lattice",
               replace(identity,
                       "#ctjs.number<" + std::string(literal) + "> {storage_test_id = \"mask\"}",
                       "#ctjs.number<" +
                           std::string(kind == "bitand" ? "13837309855095848960"
                                                        : "4607182418800017408") +
                           "> {storage_test_id = \"mask\"}"));
        for (const auto & input :
             {"ctjs.binary add %maximum, %i", "ctjs.binary sub %negativeSign, %i"}) {
            reject("identity masks still reject signed conversion discontinuities",
                   replace(identity, "%position = ctjs.binary_static " + kind + " %i, %mask",
                           "%part = " + std::string(input) +
                               "\n"
                               "  %bits = ctjs.binary_static " +
                               kind +
                               " %part, %mask\n"
                               "  %position = ctjs.binary_static bitand %bits, %one"));
        }

        const auto fixedBits = replace(
            replace(
                replace(replace(identity, "[%x, %identity, %zero, %x]",
                                "[%x, %identity, %zero, %x, %zero, %zero, %x]"),
                        "#ctjs.number<" + std::string(literal) + "> {storage_test_id = \"mask\"}",
                        "#ctjs.number<" +
                            std::string(kind == "bitand" ? "4619567317775286272"
                                                         : "4620693217682128896") +
                            "> {storage_test_id = \"mask\"}"),
                "  %a =",
                "  %eight = ctjs.constant #ctjs.number<4620693217682128896>\n"
                "  %sixteen = ctjs.constant #ctjs.number<4625196817309499392>\n  %a ="),
            "%position = ctjs.binary_static " + kind + " %i, %mask",
            "%part = ctjs.binary add %i, " + std::string(kind == "bitand" ? "%eight" : "%zero") +
                "\n  %bits = ctjs.binary_static " + kind +
                " %part, %mask\n"
                "  %position = ctjs.binary sub %bits, " +
                std::string(kind == "bitand" ? "%zero" : "%eight"));
        for (const auto & operands : {"%part, %mask", "%mask, %part"}) {
            run({.what = "fixed-bit masks retain full odd strides in either operand order",
                 .body = replace(fixedBits, kind + " %part, %mask", kind + " " + operands),
                 .arrays = "a:[zero,mask,zero,zero,zero,zero,zero]",
                 .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[3]=zero; a[1]=mask; a[6]=zero",
                 .exit = "a -> {a}"},
                "x");
        }
        run({.what = "fixed-bit translations preserve odd strides for negative input Numbers",
             .body = replace(
                 replace(fixedBits, "add %i, " + std::string(kind == "bitand" ? "%eight" : "%zero"),
                         "sub %i, " + std::string(kind == "bitand" ? "%eight" : "%sixteen")),
                 "sub %bits, " + std::string(kind == "bitand" ? "%zero" : "%eight"),
                 "add %bits, " + std::string(kind == "bitand" ? "%zero" : "%eight")),
             .arrays = "a:[zero,mask,zero,zero,zero,zero,zero]",
             .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[3]=zero; a[1]=mask; a[6]=zero",
             .exit = "a -> {a}"},
            "x");
        run({.what = "fixed-bit stride gaps preserve retained children",
             .body = replace(fixedBits, "[%x, %identity, %zero, %x, %zero, %zero, %x]",
                             "[%x, %identity, %x, %x, %zero, %zero, %x]"),
             .arrays = "a:[zero,mask,x,zero,zero,zero,zero]",
             .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[3]=zero; a[1]=mask; a[6]=zero",
             .exit = "a -> {a,x}"});
        reject("fixed-bit translations reject mask reloads at actual writes",
               replace(replace(fixedBits, "[%x, %identity, %zero, %x, %zero, %zero, %x]",
                               "[%x, %zero, %zero, %identity, %zero, %zero, %x]"),
                       "%mask = ctjs.get_property %base[%one]",
                       "%mask = ctjs.get_property %base[%three]"));
        reject(
            "fixed-bit translations retain the complete later-store census",
            replace(fixedBits, "  %step =", "  ctjs.set_property %base[%one], %zero\n  %step ="));
        reject(
            "a mask changing a varying bit cannot borrow the full input stride",
            replace(
                fixedBits,
                "#ctjs.number<" +
                    std::string(kind == "bitand" ? "4619567317775286272" : "4620693217682128896") +
                    "> {storage_test_id = \"mask\"}",
                "#ctjs.number<" +
                    std::string(kind == "bitand" ? "4617315517961601024" : "4621256167635550208") +
                    "> {storage_test_id = \"mask\"}"));
    }
    const auto varyingComplement =
        replace(replace(replace(replace(signedBits, "[%one, %x]",
                                        "[%x, %seven, %zero, %x, %zero, %zero, %x]"),
                                "add %i, %one", "add %i, %three"),
                        "  %a =",
                        "  %six = ctjs.binary add %three, %three\n"
                        "  %seven = ctjs.constant #ctjs.number<4619567317775286272> "
                        "{storage_test_id = \"mask\"}\n"
                        "  %eight = ctjs.constant #ctjs.number<4620693217682128896>\n  %a ="),
                "%position = ctjs.binary_static bitand %i, %one",
                "%mask = ctjs.get_property %base[%one]\n"
                "  %bits = ctjs.binary_static bitxor %i, %mask\n"
                "  %position = ctjs.binary sub %bits, %one");
    for (const auto & expression : {"%bits = ctjs.binary_static bitxor %i, %mask\n"
                                    "  %position = ctjs.binary sub %bits, %one",
                                    "%bits = ctjs.binary_static bitxor %mask, %i\n"
                                    "  %position = ctjs.binary sub %bits, %one",
                                    "%part = ctjs.binary sub %i, %eight\n"
                                    "  %bits = ctjs.binary_static bitxor %part, %mask\n"
                                    "  %position = ctjs.binary add %bits, %seven",
                                    "%part = ctjs.binary add %sign, %i\n"
                                    "  %bits = ctjs.binary_static bitxor %part, %mask\n"
                                    "  %position = ctjs.binary add %bits, %maximum",
                                    "%lowInput = ctjs.binary add %low, %six\n"
                                    "  %part = ctjs.binary add %lowInput, %i\n"
                                    "  %bits = ctjs.binary_static bitxor %part, %mask\n"
                                    "  %offset = ctjs.binary add %eight, %one\n"
                                    "  %position = ctjs.binary sub %bits, %offset"}) {
        run({.what = "flipping every varying input bit reverses the full odd stride",
             .body = replace(varyingComplement,
                             "%bits = ctjs.binary_static bitxor %i, %mask\n"
                             "  %position = ctjs.binary sub %bits, %one",
                             expression),
             .arrays = "a:[zero,mask,zero,zero,zero,zero,zero]",
             .reads = "a[1]=mask; a[0]=x; a[1]=mask; a[3]=zero; a[1]=mask; a[6]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "a fixed sign bit may flip alongside all varying XOR bits",
         .body = replace(replace(varyingComplement, "4619567317775286272", "4746794007263182848"),
                         "sub %bits, %one", "add %bits, %maximum"),
         .arrays = "a:[zero,mask,zero,zero,zero,zero,zero]",
         .reads = "a[1]=mask; a[0]=x; a[1]=mask; a[3]=zero; a[1]=mask; a[6]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto fixedLowComplement = replace(
        replace(
            replace(replace(replace(varyingComplement, "[%x, %seven, %zero, %x, %zero, %zero, %x]",
                                    "[%x, %zero, %seven, %zero, %zero, %zero, %x]"),
                            "4619567317775286272", "4618441417868443648"),
                    "add %i, %three", "add %i, %six"),
            "%mask = ctjs.get_property %base[%one]", "%mask = ctjs.get_property %base[%two]"),
        "sub %bits, %one", "add %bits, %zero");
    const auto crossZeroComplement = replace(
        replace(fixedLowComplement, "%seven = ctjs.constant #ctjs.number<4618441417868443648>",
                "%seven = ctjs.unary neg %two"),
        "%bits = ctjs.binary_static bitxor %i, %mask\n"
        "  %position = ctjs.binary add %bits, %zero",
        "%part = ctjs.binary sub %i, %three\n"
        "  %bits = ctjs.binary_static bitxor %part, %mask\n"
        "  %position = ctjs.binary add %bits, %three");
    for (const auto & body : {fixedLowComplement, crossZeroComplement}) {
        run({.what = "fixed low XOR bits do not erase the odd factor of a reversed stride",
             .body = body,
             .arrays = "a:[zero,zero,mask,zero,zero,zero,zero]",
             .reads = "a[2]=mask; a[0]=x; a[2]=mask; a[6]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "varying-bit complements preserve children between actual writes",
         .body = replace(varyingComplement, "[%x, %seven, %zero, %x, %zero, %zero, %x]",
                         "[%x, %seven, %x, %x, %zero, %zero, %x]"),
         .arrays = "a:[zero,mask,x,zero,zero,zero,zero]",
         .reads = "a[1]=mask; a[0]=x; a[1]=mask; a[3]=zero; a[1]=mask; a[6]=zero",
         .exit = "a -> {a,x}"});
    reject("varying-bit complements cannot reload a written slot",
           replace(replace(varyingComplement, "[%x, %seven, %zero, %x, %zero, %zero, %x]",
                           "[%x, %zero, %zero, %seven, %zero, %zero, %x]"),
                   "%mask = ctjs.get_property %base[%one]",
                   "%mask = ctjs.get_property %base[%three]"));
    reject("varying-bit complements retain the later-store census",
           replace(varyingComplement,
                   "  %step =", "  ctjs.set_property %base[%one], %zero\n  %step ="));
    reject("flipping only some varying bits cannot reverse the input lattice",
           replace(replace(replace(replace(varyingComplement,
                                           "[%x, %seven, %zero, %x, %zero, %zero, %x]",
                                           "[%x, %zero, %zero, %zero, %zero, %seven, %x, %zero]"),
                                   "4619567317775286272", "4618441417868443648"),
                           "%mask = ctjs.get_property %base[%one]",
                           "%five = ctjs.binary add %three, %two\n"
                           "  %mask = ctjs.get_property %base[%five]"),
                   "sub %bits, %one", "add %bits, %zero"));
    for (const auto & input :
         {"ctjs.binary add %maximum, %i", "ctjs.binary sub %negativeSign, %i"}) {
        run({.what = "two-point varying-bit complements preserve conversion-jump visits",
             .body = replace(crossZeroComplement,
                             "%part = ctjs.binary sub %i, %three\n"
                             "  %bits = ctjs.binary_static bitxor %part, %mask\n"
                             "  %position = ctjs.binary add %bits, %three",
                             "%part = " + std::string(input) +
                                 "\n  %bits = ctjs.binary_static bitxor %part, %mask\n"
                                 "  %position = ctjs.binary_static bitand %bits, %one"),
             .arrays = std::string(input) == "ctjs.binary add %maximum, %i"
                           ? "a:[x,zero,mask,zero,zero,zero,x]"
                           : "a:[zero,zero,mask,zero,zero,zero,x]",
             .reads = std::string(input) == "ctjs.binary add %maximum, %i"
                          ? "a[2]=mask; a[0]=x; a[2]=mask; a[6]=x"
                          : "a[2]=mask; a[0]=zero; a[2]=mask; a[6]=x",
             .exit = "a -> {a,x}"});
    }
    const auto complementBits = replace(signedBits, "%negativeOne = ctjs.unary neg %one",
                                        "%negativeOne = ctjs.unary neg %one "
                                        "{storage_test_id = \"negativeOne\"}");
    const auto xorComplement =
        replace(replace(replace(complementBits, "[%one, %x]", "[%x, %negativeOne, %zero, %x]"),
                        "add %i, %one", "add %i, %three"),
                "%position = ctjs.binary_static bitand %i, %one",
                "%mask = ctjs.get_property %base[%one]\n"
                "  %bits = ctjs.binary_static bitxor %i, %mask\n"
                "  %offset = ctjs.binary add %three, %one\n"
                "  %position = ctjs.binary add %bits, %offset");
    for (const auto & operands : {"%i, %mask", "%mask, %i"}) {
        run({.what = "XOR complement reverses odd-stride writes around an invariant mask",
             .body = replace(xorComplement, "bitxor %i, %mask", "bitxor " + std::string(operands)),
             .arrays = "a:[zero,negativeOne,zero,zero]",
             .reads = "a[1]=negativeOne; a[0]=x; a[1]=negativeOne; a[3]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "unsigned all-one XOR retains the same complement lattice",
         .body = replace(xorComplement, "%negativeOne = ctjs.unary neg %one",
                         "%negativeOne = ctjs.constant #ctjs.number<4751297606873776128>"),
         .arrays = "a:[zero,negativeOne,zero,zero]",
         .reads = "a[1]=negativeOne; a[0]=x; a[1]=negativeOne; a[3]=zero",
         .exit = "a -> {a}"},
        "x");
    for (const auto & expression : {"%part = ctjs.binary sub %i, %one\n"
                                    "  %bits = ctjs.binary_static bitxor %part, %mask\n"
                                    "  %position = ctjs.binary add %bits, %three",
                                    "%part = ctjs.binary add %sign, %i\n"
                                    "  %bits = ctjs.binary_static bitxor %part, %mask\n"
                                    "  %offset = ctjs.binary sub %maximum, %three\n"
                                    "  %position = ctjs.binary sub %bits, %offset",
                                    "%part = ctjs.binary add %low, %i\n"
                                    "  %bits = ctjs.binary_static bitxor %part, %mask\n"
                                    "  %offset = ctjs.binary add %three, %three\n"
                                    "  %position = ctjs.binary add %bits, %offset"}) {
        run({.what = "XOR complement retains odd strides within each conversion band",
             .body = replace(xorComplement,
                             "%bits = ctjs.binary_static bitxor %i, %mask\n"
                             "  %offset = ctjs.binary add %three, %one\n"
                             "  %position = ctjs.binary add %bits, %offset",
                             expression),
             .arrays = "a:[zero,negativeOne,zero,zero]",
             .reads = "a[1]=negativeOne; a[0]=x; a[1]=negativeOne; a[3]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "XOR complement replay retains an unvisited child",
         .body =
             replace(xorComplement, "[%x, %negativeOne, %zero, %x]", "[%x, %negativeOne, %x, %x]"),
         .arrays = "a:[zero,negativeOne,x,zero]",
         .reads = "a[1]=negativeOne; a[0]=x; a[1]=negativeOne; a[3]=zero",
         .exit = "a -> {a,x}"});
    for (const auto & body :
         {replace(replace(xorComplement, "[%x, %negativeOne, %zero, %x]",
                          "[%negativeOne, %x, %zero, %x]"),
                  "%mask = ctjs.get_property %base[%one]",
                  "%mask = ctjs.get_property %base[%zero]"),
          replace(xorComplement, "  %step =", "  ctjs.set_property %base[%one], %zero\n  %step ="),
          replace(xorComplement, "%negativeOne = ctjs.unary neg %one",
                  "%negativeOne = ctjs.unary neg %two")}) {
        reject("XOR complement requires the exact mask and complete disjoint reload census", body);
    }
    for (const auto & input :
         {"ctjs.binary add %maximum, %i", "ctjs.binary sub %negativeSign, %i"}) {
        reject("XOR complement refuses signed conversion discontinuities",
               replace(xorComplement,
                       "%bits = ctjs.binary_static bitxor %i, %mask\n"
                       "  %offset = ctjs.binary add %three, %one\n"
                       "  %position = ctjs.binary add %bits, %offset",
                       "%part = " + std::string(input) +
                           "\n  %bits = ctjs.binary_static bitxor %part, %mask\n"
                           "  %position = ctjs.binary_static bitand %bits, %one"));
    }
    for (const auto & expression : {"%position = ctjs.binary_static bitand %i, %negativeOne",
                                    "%position = ctjs.binary_static bitand %negativeOne, %i",
                                    "%full = ctjs.binary add %maximum, %sign\n"
                                    "  %position = ctjs.binary_static bitand %i, %full",
                                    "%negative = ctjs.unary bitnot %i\n"
                                    "  %bits = ctjs.binary_static bitand %negative, %negativeOne\n"
                                    "  %position = ctjs.unary bitnot %bits",
                                    "%high = ctjs.binary add %sign, %i\n"
                                    "  %bits = ctjs.binary_static bitand %high, %negativeOne\n"
                                    "  %position = ctjs.binary add %bits, %sign",
                                    "%high = ctjs.binary add %low, %i\n"
                                    "  %bits = ctjs.binary_static bitand %high, %negativeOne\n"
                                    "  %position = ctjs.binary sub %bits, %two"}) {
        run({.what = "signed AND masks preserve exact indices through conversion and composition",
             .body =
                 replace(signedBits, "%position = ctjs.binary_static bitand %i, %one", expression),
             .arrays = "a:[zero,zero]",
             .reads = "a[0]=zero; a[1]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "a sign-only AND mask clears nonnegative inputs without visiting other slots",
         .body = replace(signedBits, "bitand %i, %one", "bitand %i, %sign"),
         .arrays = "a:[zero,x]",
         .reads = "a[0]=zero; a[1]=x",
         .exit = "a -> {a,x}"});
    run({.what = "signed AND enclosures leave sparse unvisited children intact",
         .body = replace(replace(signedBits, "[%one, %x]", "[%x, %x, %x, %x]"),
                         "%position = ctjs.binary_static bitand %i, %one",
                         "%mask = ctjs.unary neg %two\n"
                         "  %position = ctjs.binary_static bitand %i, %mask"),
         .arrays = "a:[zero,x,zero,x]",
         .reads = "a[0]=zero; a[1]=x; a[2]=zero; a[3]=x",
         .exit = "a -> {a,x}"});
    for (const auto & input : {"ctjs.binary sub %i, %one", "ctjs.binary add %maximum, %i",
                               "ctjs.binary sub %negativeSign, %i"}) {
        const auto body = replace(signedBits, "%position = ctjs.binary_static bitand %i, %one",
                                  "%part = " + std::string(input) +
                                      "\n"
                                      "  %bits = ctjs.binary_static bitand %part, %negativeOne\n"
                                      "  %position = ctjs.binary_static bitand %bits, %one");
        if (std::string(input) == "ctjs.binary sub %i, %one") {
            run({.what = "identity AND preserves the original zero-crossing source",
                 .body = body,
                 .arrays = "a:[zero,zero]",
                 .reads = "a[0]=one; a[1]=zero",
                 .exit = "a -> {a}"},
                "x");
        } else {
            run({.what = "two-point identity AND preserves both conversion-jump images",
                 .body = body,
                 .arrays = "a:[zero,zero]",
                 .reads = std::string(input) == "ctjs.binary add %maximum, %i"
                              ? "a[0]=one; a[1]=zero"
                              : "a[0]=zero; a[1]=zero",
                 .exit = "a -> {a}"},
                "x");
        }
    }
    reject("a signed AND result must still become a nonnegative own key",
           replace(signedBits, "%position = ctjs.binary_static bitand %i, %one",
                   "%negative = ctjs.unary bitnot %i\n"
                   "  %position = ctjs.binary_static bitand %negative, %negativeOne"));
    const auto andReload =
        replace(replace(maskedReload, "  %a =",
                        "  %negativeMask = ctjs.constant #ctjs.number<13970166044099084288> "
                        "{storage_test_id = \"mask\"}\n  %a ="),
                "[%x, %x, %one, %zero]", "[%x, %x, %negativeMask, %zero]");
    run({.what = "signed AND bounds preserve a disjoint invariant mask reload",
         .body = andReload,
         .arrays = "a:[zero,zero,mask,zero]",
         .reads = "a[2]=mask; a[0]=zero; a[2]=mask; a[1]=zero; a[2]=mask; a[2]=mask; "
                  "a[2]=mask; a[3]=zero",
         .exit = "a -> {a}"},
        "x");
    reject("signed AND cannot borrow a mask reloaded from its overwrite range",
           replace(replace(andReload, "[%x, %x, %negativeMask, %zero]",
                           "[%negativeMask, %x, %zero, %zero]"),
                   "%mask = ctjs.get_property %base[%two]",
                   "%mask = ctjs.get_property %base[%zero]"));
    reject("later stores invalidate earlier signed AND mask reloads",
           replace(andReload, "  %step =", "  ctjs.set_property %base[%two], %zero\n  %step ="));
    for (const auto & bits : {"4611686018427387904", "13835058055282163712"}) {
        const auto gapReload = replace(
            replace(replace(andReload, "13970166044099084288", bits),
                    "[%x, %x, %negativeMask, %zero]", "[%x, %negativeMask, %x, %zero]"),
            "%mask = ctjs.get_property %base[%two]", "%mask = ctjs.get_property %base[%one]");
        for (const auto & operands : {"%i, %mask", "%mask, %i"}) {
            run({.what = "AND low zero bits preserve mask reloads inside the enclosing interval",
                 .body = replace(gapReload, "bitand %i, %mask", "bitand " + std::string(operands)),
                 .arrays = "a:[zero,mask,zero,zero]",
                 .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[1]=mask; a[1]=mask; a[2]=zero; "
                          "a[1]=mask; a[3]=zero",
                 .exit = "a -> {a}"},
                "x");
        }
        run({.what = "AND lattice gaps retain children the exact writes never visit",
             .body = replace(gapReload, "[%x, %negativeMask, %x, %zero]",
                             "[%x, %negativeMask, %x, %x]"),
             .arrays = "a:[zero,mask,zero,x]",
             .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[1]=mask; a[1]=mask; a[2]=zero; "
                      "a[1]=mask; a[3]=x",
             .exit = "a -> {a,x}"});
        reject("AND lattices reject a mask reloaded at a visited residue",
               replace(replace(gapReload, "[%x, %negativeMask, %x, %zero]",
                               "[%x, %zero, %negativeMask, %zero]"),
                       "%mask = ctjs.get_property %base[%one]",
                       "%mask = ctjs.get_property %base[%two]"));
        reject(
            "later stores invalidate a mask in an AND lattice gap",
            replace(gapReload, "  %step =", "  ctjs.set_property %base[%one], %zero\n  %step ="));
    }
    for (const auto & expression :
         {"%high = ctjs.binary sub %maximum, %i\n"
          "  %position = ctjs.binary_static bitxor %high, %maximum",
          "%high = ctjs.binary add %sign, %i\n"
          "  %position = ctjs.binary_static bitxor %high, %sign",
          "%negative = ctjs.unary bitnot %i\n"
          "  %position = ctjs.binary_static bitxor %negative, %negativeOne",
          "%negative = ctjs.unary bitnot %i\n"
          "  %position = ctjs.binary_static bitxor %negativeOne, %negative",
          "%high = ctjs.binary add %low, %i\n"
          "  %position = ctjs.binary_static bitxor %high, %two",
          "%negative = ctjs.binary_static bitor %i, %negativeSign\n"
          "  %position = ctjs.binary add %negative, %sign",
          "%negative = ctjs.binary_static bitor %sign, %i\n"
          "  %position = ctjs.binary add %negative, %sign",
          "%high = ctjs.binary add %sign, %i\n"
          "  %negative = ctjs.binary_static bitor %high, %zero\n"
          "  %position = ctjs.binary add %negative, %sign"}) {
        run({.what = "signed OR/XOR bands preserve exact indices through composition",
             .body =
                 replace(signedBits, "%position = ctjs.binary_static bitand %i, %one", expression),
             .arrays = "a:[zero,zero]",
             .reads = "a[0]=zero; a[1]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "identity OR preserves the original zero-crossing source",
         .body = replace(signedBits, "%position = ctjs.binary_static bitand %i, %one",
                         "%part = ctjs.binary sub %i, %one\n"
                         "  %bits = ctjs.binary_static bitor %part, %zero\n"
                         "  %position = ctjs.binary add %bits, %one"),
         .arrays = "a:[zero,zero]",
         .reads = "a[0]=zero; a[1]=zero",
         .exit = "a -> {a}"},
        "x");
    for (const auto & input :
         {"ctjs.binary add %maximum, %i", "ctjs.binary sub %negativeSign, %i"}) {
        run({.what = "two-point XOR composes exact images through a final mask",
             .body = replace(signedBits, "%position = ctjs.binary_static bitand %i, %one",
                             "%part = " + std::string(input) +
                                 "\n"
                                 "  %bits = ctjs.binary_static bitxor %part, %maximum\n"
                                 "  %position = ctjs.binary_static bitand %bits, %one"),
             .arrays = "a:[zero,zero]",
             .reads = std::string(input) == "ctjs.binary sub %negativeSign, %i"
                          ? "a[0]=one; a[1]=zero"
                          : "a[0]=zero; a[1]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    const auto orReload = replace(replace(masked, "[%one, %x]", "[%one, %x, %zero, %zero]"),
                                  "%position = ctjs.binary_static bitand %i, %one",
                                  "%mask = ctjs.get_property %base[%zero]\n"
                                  "  %position = ctjs.binary_static bitor %i, %mask");
    run({.what = "OR's mask lower bound preserves a disjoint reload below all writes",
         .body = orReload,
         .arrays = "a:[one,zero,zero,zero]",
         .reads = "a[0]=one; a[0]=one; a[0]=one; a[1]=zero; a[0]=one; a[2]=zero; "
                  "a[0]=one; a[3]=zero",
         .exit = "a -> {a}"},
        "x");
    reject("XOR cannot borrow OR's nonzero lower bound for a reload",
           replace(orReload, "bitor", "bitxor"));
    reject("a later write invalidates an earlier OR reload",
           replace(orReload, "  %step =", "  ctjs.set_property %base[%zero], %zero\n  %step ="));
    const auto signedReload = replace(
        replace(replace(orReload, "  %a =",
                        "  %sign = ctjs.constant #ctjs.number<4746794007248502784>\n"
                        "  %negativeMask = ctjs.constant #ctjs.number<13970166044099084288> "
                        "{storage_test_id = \"mask\"}\n  %a ="),
                "[%one, %x, %zero, %zero]", "[%negativeMask, %x, %zero, %zero]"),
        "%position = ctjs.binary_static bitor %i, %mask",
        "%negative = ctjs.binary_static bitor %i, %mask\n"
        "  %position = ctjs.binary add %negative, %sign");
    run({.what = "signed OR mask bounds preserve disjoint lower reloads",
         .body = signedReload,
         .arrays = "a:[mask,zero,zero,zero]",
         .reads = "a[0]=mask; a[0]=mask; a[0]=mask; a[1]=zero; a[0]=mask; a[2]=zero; "
                  "a[0]=mask; a[3]=zero",
         .exit = "a -> {a}"},
        "x");
    reject("signed XOR cannot borrow OR's disjoint reload proof",
           replace(signedReload, "bitor", "bitxor"));
    run({.what = "signed OR low bits preserve a reload inside the translated interval",
         .body = replace(replace(signedReload, "[%negativeMask, %x, %zero, %zero]",
                                 "[%zero, %x, %negativeMask, %x]"),
                         "%mask = ctjs.get_property %base[%zero]",
                         "%mask = ctjs.get_property %base[%two]"),
         .arrays = "a:[zero,zero,mask,zero]",
         .reads = "a[2]=mask; a[0]=zero; a[2]=mask; a[1]=zero; a[2]=mask; a[2]=mask; "
                  "a[2]=mask; a[3]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto allOneReload = replace(
        replace(replace(replace(signedReload, "13970166044099084288", "13830554455654793216"),
                        "[%negativeMask, %x, %zero, %zero]", "[%x, %negativeMask]"),
                "%mask = ctjs.get_property %base[%zero]", "%mask = ctjs.get_property %base[%one]"),
        "add %negative, %sign", "add %negative, %one");
    run({.what = "an all-one OR mask remains a singleton without a width-sized stride shift",
         .body = allOneReload,
         .arrays = "a:[zero,mask]",
         .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[1]=mask",
         .exit = "a -> {a}"},
        "x");
    for (const auto & input : {"%part = ctjs.binary sub %i, %one",
                               "%maximum = ctjs.binary sub %sign, %one\n"
                               "  %part = ctjs.binary add %maximum, %i",
                               "%minimum = ctjs.binary sub %zero, %sign\n"
                               "  %part = ctjs.binary sub %minimum, %i"}) {
        const auto crossing =
            replace(allOneReload, "%negative = ctjs.binary_static bitor %i, %mask",
                    std::string(input) + "\n  %negative = ctjs.binary_static bitor %part, %mask");
        run({.what = "an all-one OR mask is constant across zero and signed conversion bands",
             .body = crossing,
             .arrays = "a:[zero,mask]",
             .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[1]=mask",
             .exit = "a -> {a}"},
            "x");
        reject("a near-all-one OR mask cannot borrow a constant output across conversion bands",
               replace(crossing, "13830554455654793216", "13835058055282163712"));
        reject("later stores invalidate an all-one mask even when its output is constant",
               replace(crossing, "  %step =", "  ctjs.set_property %base[%one], %zero\n  %step ="));
        const auto signedCrossing = replace(
            replace(replace(replace(crossing, "13830554455654793216", "13835058055282163712"),
                            "[%x, %negativeMask]", "[%x, %x, %negativeMask, %zero]"),
                    "%mask = ctjs.get_property %base[%one]",
                    "%mask = ctjs.get_property %base[%two]"),
            "add %negative, %one", "add %negative, %two");
        run({.what = "a sign-setting OR mask bounds crossing inputs without a range union",
             .body = signedCrossing,
             .arrays = "a:[zero,zero,mask,zero]",
             .reads = std::string(input).starts_with("%minimum")
                          ? "a[2]=mask; a[0]=zero; a[2]=mask; a[1]=zero; a[2]=mask; a[2]=mask; "
                            "a[2]=mask; a[3]=zero"
                          : "a[2]=mask; a[0]=x; a[2]=mask; a[1]=zero; a[2]=mask; a[2]=mask; "
                            "a[2]=mask; a[3]=zero",
             .exit = "a -> {a}"},
            "x");
        reject("XOR cannot borrow OR's fixed output sign across conversion boundaries",
               replace(signedCrossing, "bitor", "bitxor"));
        reject("a sign-setting mask cannot reload from its crossing output range",
               replace(replace(signedCrossing, "[%x, %x, %negativeMask, %zero]",
                               "[%x, %negativeMask, %x, %zero]"),
                       "%mask = ctjs.get_property %base[%two]",
                       "%mask = ctjs.get_property %base[%one]"));
        reject("later writes invalidate sign-setting masks across conversion boundaries",
               replace(signedCrossing,
                       "  %step =", "  ctjs.set_property %base[%two], %zero\n  %step ="));
    }
    reject(
        "signed masks retain the complete later-store census",
        replace(signedReload, "  %step =", "  ctjs.set_property %base[%zero], %zero\n  %step ="));
    const auto xorReload = replace(maskedReload, "%position = ctjs.binary_static bitand %i, %mask",
                                   "%part = ctjs.binary_static bitand %i, %one\n"
                                   "  %position = ctjs.binary_static bitxor %part, %mask");
    run({.what = "composed XOR writes preserve a reload beyond their enclosing range",
         .body = xorReload,
         .arrays = "a:[zero,zero,one,zero]",
         .reads = "a[2]=one; a[0]=x; a[2]=one; a[1]=zero; a[2]=one; a[2]=one; "
                  "a[2]=one; a[3]=zero",
         .exit = "a -> {a}"},
        "x");
    reject("a later write invalidates an earlier XOR reload",
           replace(xorReload, "  %step =", "  ctjs.set_property %base[%two], %zero\n  %step ="));
    run({.what = "overwriting current own elements preserves a saved child",
         .body = overwritten,
         .arrays = "a:[zero,zero]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "returning an overwritten array releases its former children",
         .body = replace(overwritten, "ctjs.return %result", "ctjs.return %a"),
         .arrays = "a:[zero,zero]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "a -> {a}"},
        "x");
    run({.what = "reading after an own-element overwrite sees the new value",
         .body =
             replace(savedChild, "  %read =", "  ctjs.set_property %base[%i], %zero\n  %read ="),
         .arrays = "a:[zero,zero]",
         .reads = "a[0]=zero; a[1]=zero",
         .exit = "zero -> {}"},
        "x");
    run({.what = "zero-trip overwrite loops preserve existing children",
         .body =
             replace(replace(overwritten, "^header(%a, %zero, %zero", "^header(%a, %two, %zero"),
                     "ctjs.return %result", "ctjs.return %a"),
         .arrays = "a:[one,x]",
         .exit = "a -> {a,x}"});
    const std::string fixedOverwrite =
        replace(savedChild, "  %read =", "  ctjs.set_property %base[%one], %zero\n  %read =");
    run({.what = "invariant own-index overwrites release the replaced child",
         .body = fixedOverwrite,
         .arrays = "a:[one,zero]",
         .reads = "a[0]=one; a[1]=zero",
         .exit = "zero -> {}"},
        "x");
    run({.what = "invariant overwrites preserve a different returned child",
         .body = replace(fixedOverwrite, "%base[%one], %zero", "%base[%zero], %zero"),
         .arrays = "a:[zero,x]",
         .reads = "a[0]=zero; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "saved String own indices remain invariant during overwrites",
         .body = replace(replace(fixedOverwrite,
                                 "  %a =", "  %fixed = ctjs.constant #ctjs.string<\"1\">\n  %a ="),
                         "%base[%one], %zero", "%base[%fixed], %zero"),
         .arrays = "a:[one,zero]",
         .reads = "a[0]=one; a[1]=zero",
         .exit = "zero -> {}"},
        "x");
    run({.what = "invariant overwrite keys cannot extend the guard array",
         .body = replace(fixedOverwrite, "%base[%one], %zero", "%base[%two], %zero"),
         .failure = ArrayContentsFailure::UnsupportedControlFlow});
    run({.what = "a key reloaded from a disjoint guard slot remains invariant",
         .body = replace(fixedOverwrite, "  ctjs.set_property %base[%one], %zero",
                         "  %fixed = ctjs.get_property %base[%zero]\n"
                         "  ctjs.set_property %base[%fixed], %zero"),
         .arrays = "a:[one,zero]",
         .reads = "a[0]=one; a[0]=one; a[0]=one; a[1]=zero",
         .exit = "zero -> {}"},
        "x");
    const auto disjointIndex =
        replace(replace(fixedOverwrite,
                        "  %step =", "  %stride = ctjs.get_property %base[%zero]\n  %step ="),
                "add %i, %one", "add %i, %stride");
    run({.what = "a stride reloaded from a disjoint guard slot survives fixed overwrites",
         .body = disjointIndex,
         .arrays = "a:[one,zero]",
         .reads = "a[0]=one; a[0]=one; a[1]=zero; a[0]=one",
         .exit = "zero -> {}"},
        "x");
    for (const auto & store : {"%base[%zero]", "%base[%i]"}) {
        run({.what = "an overlapping fixed or current-index store cannot prove its stride",
             .body = replace(disjointIndex, "%base[%one]", store),
             .failure = ArrayContentsFailure::UnsupportedControlFlow});
    }
    run({.what = "a later write invalidates an earlier disjoint reload proof",
         .body = replace(disjointIndex,
                         "  %step =", "  ctjs.set_property %base[%zero], %two\n  %step ="),
         .failure = ArrayContentsFailure::UnsupportedControlFlow});
    const auto visitedIndex =
        replace(replace(disjointIndex, "%base[%one], %zero", "%base[%i], %zero"),
                "^header(%a, %zero, %zero", "^header(%a, %one, %zero");
    run({.what = "a reload below the start survives current-index overwrites",
         .body = visitedIndex,
         .arrays = "a:[one,zero]",
         .reads = "a[1]=zero; a[0]=one",
         .exit = "zero -> {}"},
        "x");
    run({.what = "zero-trip current-index overwrites preserve reloaded guard contents",
         .body =
             replace(replace(visitedIndex, "^header(%a, %one, %zero", "^header(%a, %two, %zero"),
                     "ctjs.return %result", "ctjs.return %a"),
         .arrays = "a:[one,x]",
         .exit = "a -> {a,x}"});
    const auto skippedIndex = replace(
        replace(replace(visitedIndex, "^header(%a, %one, %zero", "^header(%a, %zero, %zero"),
                "[%one, %x]", "[%x, %two, %x]"),
        "%stride = ctjs.get_property %base[%zero]", "%stride = ctjs.get_property %base[%one]");
    run({.what = "a reload between stride positions survives current-index overwrites",
         .body = skippedIndex,
         .arrays = "a:[zero,two,zero]",
         .reads = "a[0]=zero; a[1]=two; a[2]=zero; a[1]=two",
         .exit = "zero -> {}"},
        "x");
    run({.what = "visited positions are relative to the nonzero start",
         .body = replace(
             replace(replace(skippedIndex, "^header(%a, %zero, %zero", "^header(%a, %one, %zero"),
                     "[%x, %two, %x]", "[%two, %x, %two, %x]"),
             "%stride = ctjs.get_property %base[%one]", "%stride = ctjs.get_property %base[%two]"),
         .arrays = "a:[two,zero,two,zero]",
         .reads = "a[1]=zero; a[2]=two; a[3]=zero; a[2]=two",
         .exit = "zero -> {}"},
        "x");
    run({.what = "a later visited position cannot supply an invariant reload",
         .body = replace(replace(skippedIndex, "[%x, %two, %x]", "[%x, %x, %two]"),
                         "%stride = ctjs.get_property %base[%one]",
                         "%stride = ctjs.get_property %base[%two]"),
         .failure = ArrayContentsFailure::UnsupportedControlFlow});
    run({.what = "an unvisited slot still cannot overlap a fixed overwrite",
         .body = replace(visitedIndex,
                         "  %step =", "  ctjs.set_property %base[%zero], %one\n  %step ="),
         .failure = ArrayContentsFailure::UnsupportedControlFlow});
    const auto offsetIndex = replace(
        replace(replace(replace(savedChild, "[%one, %x]", "[%one, %x, %one, %x]"), "  %read =",
                        "  %position = ctjs.binary add %i, %one\n"
                        "  ctjs.set_property %base[%position], %zero\n  %read ="),
                "add %i, %one\n  cf.br", "add %i, %two\n  cf.br"),
        "ctjs.return %result", "ctjs.return %a");
    for (const auto & expression : {"ctjs.binary add %i, %one", "ctjs.binary add %one, %i",
                                    "ctjs.binary_static add %i, %one"}) {
        run({.what = "bounded Number offsets overwrite only the shifted visited positions",
             .body = replace(offsetIndex, "ctjs.binary add %i, %one", expression),
             .arrays = "a:[one,zero,one,zero]",
             .reads = "a[0]=one; a[2]=one",
             .exit = "a -> {a}"},
            "x");
    }
    const auto previousIndex =
        replace(replace(replace(offsetIndex, "[%one, %x, %one, %x]", "[%x, %one, %x, %one]"),
                        "^header(%a, %zero, %zero", "^header(%a, %one, %zero"),
                "ctjs.binary add %i, %one", "ctjs.binary sub %i, %one");
    const auto complementIndex =
        replace(replace(offsetIndex, "[%one, %x, %one, %x]", "[%x, %one, %x, %one]"),
                "%position = ctjs.binary add %i, %one",
                "%negative = ctjs.unary bitnot %i\n"
                "  %position = ctjs.unary bitnot %negative");
    run({.what = "double complement preserves exact bounded own positions and stride",
         .body = complementIndex,
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[0]=zero; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto negativeComplement = replace(complementIndex, "%negative = ctjs.unary bitnot %i",
                                            "%part = ctjs.binary sub %zero, %i\n"
                                            "  %negative = ctjs.binary sub %part, %one");
    run({.what = "complement of bounded negative indices preserves zero and positive positions",
         .body = negativeComplement,
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[0]=zero; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto signedBoundary =
        replace(replace(complementIndex, "  %a =",
                        "  %maximum = ctjs.constant #ctjs.number<4746794007244308480>\n"
                        "  %minimumMagnitude = ctjs.binary add %maximum, %one\n  %a ="),
                "%negative = ctjs.unary bitnot %i\n  %position = ctjs.unary bitnot %negative",
                "%part = ctjs.binary sub %maximum, %i\n"
                "  %negative = ctjs.unary bitnot %part\n"
                "  %position = ctjs.binary add %negative, %minimumMagnitude");
    run({.what = "complement includes the signed i32 endpoints without wrapping",
         .body = signedBoundary,
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[0]=zero; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto positiveBand =
        replace(replace(signedBoundary, "sub %maximum, %i", "add %minimumMagnitude, %i"),
                "add %negative, %minimumMagnitude", "sub %maximum, %negative");
    const auto negativeBand =
        replace(replace(positiveBand, "%part = ctjs.binary add %minimumMagnitude, %i",
                        "%lower = ctjs.unary neg %minimumMagnitude\n"
                        "  %below = ctjs.binary sub %lower, %one\n"
                        "  %part = ctjs.binary sub %below, %i"),
                "sub %maximum, %negative", "add %negative, %minimumMagnitude");
    for (const auto & body : {positiveBand, negativeBand}) {
        run({.what = "a single wrapping ToInt32 band preserves complement stride and own positions",
             .body = body,
             .arrays = "a:[zero,one,zero,one]",
             .reads = "a[0]=zero; a[2]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    const auto unsignedBoundary =
        replace(replace(replace(signedBoundary, "4746794007244308480", "4751297606873776128"),
                        "  %minimumMagnitude = ctjs.binary add %maximum, %one\n", ""),
                "%negative = ctjs.unary bitnot %part\n"
                "  %position = ctjs.binary add %negative, %minimumMagnitude",
                "%position = ctjs.unary bitnot %part");
    const auto negativeUnsignedBoundary =
        replace(replace(unsignedBoundary, "sub %maximum, %i", "sub %i, %maximum"),
                "%position = ctjs.unary bitnot %part",
                "%complement = ctjs.unary bitnot %part\n"
                "  %positive = ctjs.unary neg %complement\n"
                "  %position = ctjs.binary sub %positive, %two");
    for (const auto & body : {unsignedBoundary, negativeUnsignedBoundary}) {
        run({.what = "complement bands include the exact positive and negative u32 magnitudes",
             .body = body,
             .arrays = "a:[zero,one,zero,one]",
             .reads = "a[0]=zero; a[2]=zero",
             .exit = "a -> {a}"},
            "x");
        reject("complement bands cannot borrow an intermediate outside the bounded Number range",
               replace(body, "4751297606873776128", "4751297606875873280"));
    }
    for (const auto & body : {replace(signedBoundary, "sub %maximum, %i", "add %maximum, %i"),
                              replace(negativeBand, "sub %below, %i", "add %below, %i"),
                              replace(negativeComplement, "sub %part, %one", "sub %part, %zero")}) {
        reject("complement indices reject ToInt32 discontinuities and negative own positions",
               body);
    }
    const auto crossingComplement =
        replace(replace(signedBoundary, "sub %maximum, %i", "add %maximum, %i"),
                "ctjs.binary add %negative, %minimumMagnitude",
                "ctjs.binary_static bitand %negative, %two");
    const auto negativeCrossingComplement =
        replace(replace(negativeBand, "sub %below, %i", "add %below, %i"),
                "ctjs.binary add %negative, %minimumMagnitude",
                "ctjs.binary_static bitand %negative, %two");
    for (const auto & body : {crossingComplement, negativeCrossingComplement}) {
        run({.what = "two-point complements compose across either signed conversion jump",
             .body = body,
             .arrays = "a:[zero,one,zero,one]",
             .reads = "a[0]=zero; a[2]=zero",
             .exit = "a -> {a}"},
            "x");
        run({.what = "two-point complement gaps preserve a reloaded mask",
             .body = replace(replace(body, "[%x, %one, %x, %one]", "[%x, %two, %x, %one]"),
                             "%position = ctjs.binary_static bitand %negative, %two",
                             "%mask = ctjs.get_property %base[%one]\n"
                             "  %position = ctjs.binary_static bitand %negative, %mask"),
             .arrays = "a:[zero,two,zero,one]",
             .reads = "a[1]=two; a[0]=zero; a[1]=two; a[2]=zero",
             .exit = "a -> {a}"},
            "x");
        reject("complement endpoints cannot omit an interior conversion-jump visit",
               replace(body, "[%x, %one, %x, %one]", "[%x, %one, %x, %one, %x, %one]"));
    }
    run({.what = "a second complement reverses the exact conversion-jump endpoint images",
         .body =
             replace(crossingComplement, "%position = ctjs.binary_static bitand %negative, %two",
                     "%again = ctjs.unary bitnot %negative\n"
                     "  %position = ctjs.binary_static bitand %again, %two"),
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[0]=x; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "a two-value intermediate can have more than two loop visits",
         .body = replace(replace(crossingComplement, "%part = ctjs.binary add %maximum, %i",
                                 "%bucket = ctjs.binary_static bitand %i, %two\n"
                                 "  %part = ctjs.binary add %maximum, %bucket"),
                         "add %i, %two\n  cf.br", "add %i, %one\n  cf.br"),
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[0]=zero; a[1]=one; a[2]=zero; a[3]=one",
         .exit = "a -> {a}"},
        "x");
    run({.what = "two-point complement writes retain a child outside their exact lattice",
         .body = replace(crossingComplement, "[%x, %one, %x, %one]", "[%x, %x, %x, %one]"),
         .arrays = "a:[zero,x,zero,one]",
         .reads = "a[0]=zero; a[2]=zero",
         .exit = "a -> {a,x}"});
    run({.what = "two-point complements preserve an earlier child snapshot",
         .body = replace(replace(crossingComplement, "  cf.br ^header(%a,",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header(%a,"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[0]=x; a[0]=zero; a[2]=zero",
         .exit = "x -> {x}"});
    const auto complementReload =
        replace(replace(crossingComplement, "[%x, %one, %x, %one]", "[%x, %two, %x, %one]"),
                "%position = ctjs.binary_static bitand %negative, %two",
                "%mask = ctjs.get_property %base[%one]\n"
                "  %position = ctjs.binary_static bitand %negative, %mask");
    reject("two-point complements retain the exact endpoint reload overlap check",
           replace(replace(complementReload, "[%x, %two, %x, %one]", "[%two, %one, %x, %one]"),
                   "%mask = ctjs.get_property %base[%one]",
                   "%mask = ctjs.get_property %base[%zero]"));
    reject("two-point complement gaps retain the complete later-store census",
           replace(complementReload,
                   "  %step =", "  ctjs.set_property %base[%one], %zero\n  %step ="));
    run({.what = "subtracted Number offsets stay relative to a nonzero start",
         .body = previousIndex,
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[1]=one; a[3]=one",
         .exit = "a -> {a}"},
        "x");
    run({.what = "signed Number offset snapshots preserve the same visited writes",
         .body =
             replace(replace(previousIndex, "  %a =", "  %negative = ctjs.unary neg %one\n  %a ="),
                     "ctjs.binary sub %i, %one", "ctjs.binary add %i, %negative"),
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[1]=one; a[3]=one",
         .exit = "a -> {a}"},
        "x");
    const auto reloadedOffset = replace(offsetIndex, "  %position = ctjs.binary add %i, %one",
                                        "  %offset = ctjs.get_property %base[%zero]\n"
                                        "  %position = ctjs.binary add %i, %offset");
    run({.what = "an offset reload outside the shifted footprint stays invariant",
         .body = reloadedOffset,
         .arrays = "a:[one,zero,one,zero]",
         .reads = "a[0]=one; a[0]=one; a[0]=one; a[2]=one",
         .exit = "a -> {a}"},
        "x");
    run({.what = "a saved child survives an offset overwrite of its former slot",
         .body = replace(replace(offsetIndex, "  cf.br ^header(%a,",
                                 "  %saved = ctjs.get_property %a[%one]\n  cf.br ^header(%a,"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "a:[one,zero,one,zero]",
         .reads = "a[1]=x; a[0]=one; a[2]=one",
         .exit = "x -> {x}"});
    run({.what = "a zero-trip offset loop preserves every original child",
         .body =
             replace(replace(previousIndex, "^header(%a, %one, %zero", "^header(%a, %three, %zero"),
                     "[%x, %one, %x, %one]", "[%x, %one, %x]"),
         .arrays = "a:[x,one,x]",
         .exit = "a -> {a,x}"});
    for (const auto & body :
         {replace(previousIndex, "^header(%a, %one, %zero", "^header(%a, %zero, %zero"),
          replace(offsetIndex, "[%one, %x, %one, %x]", "[%one, %x, %one]"),
          replace(replace(reloadedOffset, "[%one, %x, %one, %x]", "[%one, %one, %x, %one]"),
                  "%offset = ctjs.get_property %base[%zero]",
                  "%offset = ctjs.get_property %base[%one]"),
          replace(replace(reloadedOffset, "[%one, %x, %one, %x]", "[%one, %x, %one, %one]"),
                  "%offset = ctjs.get_property %base[%zero]",
                  "%offset = ctjs.get_property %base[%three]"),
          replace(reloadedOffset, "  %step =", "  ctjs.set_property %base[%zero], %two\n  %step ="),
          replace(offsetIndex, "ctjs.binary add %i, %one", "ctjs.binary add %i, %s"),
          replace(
              replace(offsetIndex, "  %a =", "  %text = ctjs.constant #ctjs.string<\"1\">\n  %a ="),
              "ctjs.binary add %i, %one", "ctjs.binary add %i, %text"),
          replace(replace(offsetIndex, "  %a =",
                          "  %maximum = ctjs.constant #ctjs.number<4751297606873776128>\n  %a ="),
                  "ctjs.binary add %i, %one", "ctjs.binary add %i, %maximum")}) {
        run({.what =
                 "offset stores refuse bounds, mutable reloads, changing values and non-Numbers",
             .body = body,
             .failure = ArrayContentsFailure::UnsupportedControlFlow});
    }
    const auto quotientIndex =
        replace(replace(offsetIndex, "[%one, %x, %one, %x]", "[%x, %x, %one, %one]"),
                "ctjs.binary add %i, %one", "ctjs.binary div %i, %two");
    const auto leftShiftIndex =
        replace(complementIndex,
                "%negative = ctjs.unary bitnot %i\n  %position = ctjs.unary bitnot %negative",
                "%part = ctjs.binary div %i, %two\n"
                "  %position = ctjs.binary_static shl %part, %one");
    run({.what = "nonwrapping left shifts scale exact quotient positions and stride",
         .body = leftShiftIndex,
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[0]=zero; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    for (const auto & count : {"4629700416936869888", "13852790978814935040"}) {
        auto body = replace(
            leftShiftIndex,
            "  %a =", std::string{"  %count = ctjs.constant #ctjs.number<"} + count + ">\n  %a =");
        body = replace(body, "shl %part, %one",
                       std::string{count} == "4629700416936869888" ? "shl %i, %count"
                                                                   : "shl %part, %count");
        run({.what = "left-shift counts preserve modulo32 and negative Number semantics",
             .body = body,
             .arrays = "a:[zero,one,zero,one]",
             .reads = "a[0]=zero; a[2]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    const auto leftShiftReload =
        replace(leftShiftIndex, "  %position = ctjs.binary_static shl %part, %one",
                "  %count = ctjs.get_property %base[%one]\n"
                "  %position = ctjs.binary_static shl %part, %count");
    run({.what = "left-shift count reloads retain the scaled stride gaps",
         .body = leftShiftReload,
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[1]=one; a[0]=zero; a[1]=one; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto carriedLeftShift = replace(
        replace(replace(leftShiftIndex, "^header(%a, %zero, %zero", "^header(%a, %zero, %one"),
                "^header(%base, %step, %read", "^header(%base, %step, %s"),
        "shl %part, %one", "shl %part, %s");
    run({.what = "left-shift counts retain exact CFG header and backedge transport",
         .body = carriedLeftShift,
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[0]=zero; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto leftShiftBoundary =
        replace(replace(leftShiftIndex, "  %a =",
                        "  %half = ctjs.constant #ctjs.number<4742290407612743680>\n"
                        "  %maximum = ctjs.binary mul %half, %two\n  %a ="),
                "%position = ctjs.binary_static shl %part, %one",
                "%input = ctjs.binary sub %half, %part\n"
                "  %shifted = ctjs.binary_static shl %input, %one\n"
                "  %position = ctjs.binary sub %maximum, %shifted");
    run({.what = "left shifts include the largest input whose doubled output stays positive",
         .body = leftShiftBoundary,
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[0]=zero; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    reject("left shifts refuse the signed-output discontinuity",
           replace(leftShiftBoundary, "4742290407612743680", "4742290407621132288"));
    const auto negativeLeftShift = replace(
        leftShiftIndex,
        "%part = ctjs.binary div %i, %two\n  %position = ctjs.binary_static shl %part, %one",
        "%input = ctjs.binary sub %i, %two\n"
        "  %shifted = ctjs.binary_static shl %input, %one\n"
        "  %part = ctjs.binary div %shifted, %two\n"
        "  %position = ctjs.binary add %part, %two");
    const auto minimumLeftShift =
        replace(replace(replace(leftShiftBoundary, "4742290407612743680", "4742290407621132288"),
                        "sub %half, %part", "sub %part, %half"),
                "sub %maximum, %shifted", "add %shifted, %maximum");
    for (const auto & body : {negativeLeftShift, minimumLeftShift,
                              replace(replace(negativeLeftShift, "sub %i, %two", "sub %i, %one"),
                                      "add %part, %two", "add %part, %one"),
                              replace(replace(negativeLeftShift, "sub %i, %two", "sub %zero, %i"),
                                      "add %part, %two", "sub %zero, %part")}) {
        run({.what = "signed left-shift intermediates retain bounds, zero crossing and reversal",
             .body = body,
             .arrays = "a:[zero,one,zero,one]",
             .reads = "a[0]=zero; a[2]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    const auto negativeLeftReload =
        replace(negativeLeftShift, "%shifted = ctjs.binary_static shl %input, %one",
                "%count = ctjs.get_property %base[%one]\n"
                "  %shifted = ctjs.binary_static shl %input, %count");
    run({.what = "signed left shifts retain exact reload gaps after quotient composition",
         .body = negativeLeftReload,
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[1]=one; a[0]=zero; a[1]=one; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    reject("signed left shifts refuse output underflow below INT32_MIN",
           replace(minimumLeftShift, "4742290407621132288", "4742290407625326592"));
    reject("signed left shifts refuse crossing an input conversion boundary with count zero",
           replace(replace(replace(minimumLeftShift, "4742290407621132288", "4746794007250599936"),
                           "ctjs.binary mul %half, %two", "ctjs.unary plus %half"),
                   "shl %input, %one", "shl %input, %zero"));
    reject("signed left shifts retain the complete later-store reload census",
           replace(negativeLeftReload,
                   "  %step =", "  ctjs.set_property %base[%one], %zero\n  %step ="));
    const auto positiveLeftBand = replace(
        replace(minimumLeftShift, "  %a =", "  %upper = ctjs.binary add %maximum, %half\n  %a ="),
        "sub %part, %half", "add %part, %upper");
    const auto negativeLeftBand = replace(
        replace(leftShiftIndex,
                "  %a =", "  %maximum = ctjs.constant #ctjs.number<4751297606873776128>\n  %a ="),
        "%position = ctjs.binary_static shl %part, %one",
        "%input = ctjs.binary sub %part, %maximum\n"
        "  %shifted = ctjs.binary_static shl %input, %one\n"
        "  %position = ctjs.binary sub %shifted, %two");
    const auto zeroLeftBand = replace(
        replace(replace(positiveLeftBand, "binary add %maximum, %half", "unary plus %maximum"),
                "add %part, %upper", "add %i, %upper"),
        "shl %input, %one", "shl %input, %zero");
    const auto reversedLeftBand =
        replace(positiveLeftBand, "%position = ctjs.binary add %shifted, %maximum",
                "%forward = ctjs.binary add %shifted, %maximum\n"
                "  %position = ctjs.binary sub %two, %forward");
    for (const auto & body : {positiveLeftBand, negativeLeftBand, zeroLeftBand}) {
        run({.what = "left shifts preserve each ToInt32 band before bounded multiplication",
             .body = body,
             .arrays = "a:[zero,one,zero,one]",
             .reads = "a[0]=zero; a[2]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "left-shift conversion bands preserve reversed write order",
         .body = reversedLeftBand,
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[0]=x; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto bandLeftReload =
        replace(negativeLeftBand, "%shifted = ctjs.binary_static shl %input, %one",
                "%count = ctjs.get_property %base[%one]\n"
                "  %shifted = ctjs.binary_static shl %input, %count");
    run({.what = "left-shift conversion bands preserve scaled reload gaps",
         .body = bandLeftReload,
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[1]=one; a[0]=zero; a[1]=one; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    reject(
        "left-shift conversion bands retain the complete reload census",
        replace(bandLeftReload, "  %step =", "  ctjs.set_property %base[%one], %zero\n  %step ="));
    reject("positive left-shift input bands still refuse signed output underflow",
           replace(positiveLeftBand, "add %part, %upper", "sub %upper, %part"));
    reject("negative left-shift input bands still refuse signed output overflow",
           replace(negativeLeftBand, "4751297606873776128", "4749045807064285184"));
    reject("left-shift count zero still requires a single positive ToInt32 band",
           replace(zeroLeftBand, "%upper = ctjs.unary plus %maximum",
                   "%upper = ctjs.binary sub %maximum, %one"));
    for (const auto & body :
         {replace(leftShiftReload, "%count = ctjs.get_property %base[%one]",
                  "%count = ctjs.get_property %base[%two]"),
          replace(leftShiftReload,
                  "  %step =", "  ctjs.set_property %base[%one], %zero\n  %step ="),
          replace(carriedLeftShift, "^header(%base, %step, %s", "^header(%base, %step, %read"),
          replace(leftShiftIndex, "shl %part, %one", "shl %one, %part"),
          replace(leftShiftIndex, "add %i, %two\n  cf.br", "add %i, %one\n  cf.br"),
          replace(leftShiftIndex, "shl %part, %one", "shl %i, %one"),
          replace(leftShiftIndex, "%part = ctjs.binary div %i, %two",
                  "%part = ctjs.unary neg %i")}) {
        reject("left shifts retain count, reload, integral-input and own-bounds guards", body);
    }
    const auto hugeLeftShift = replace(
        replace(replace(leftShiftIndex, "  %a =",
                        "  %count = ctjs.constant #ctjs.number<4629418941960159232>\n  %a ="),
                "[%x, %one, %x, %one]", "[%x]"),
        "shl %part, %one", "shl %i, %count");
    reject("left-shift stride multiplication must remain within the bounded Number range",
           hugeLeftShift);
    run({.what = "left-shift stride admits the exact high-bit factor for a single zero visit",
         .body = replace(hugeLeftShift, "add %i, %two\n  cf.br", "add %i, %one\n  cf.br"),
         .arrays = "a:[zero]",
         .reads = "a[0]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto positiveOutputBand =
        replace(minimumLeftShift, "sub %part, %half", "add %part, %half");
    const auto negativeOutputBand =
        replace(replace(minimumLeftShift, "sub %part, %half", "sub %part, %maximum"),
                "ctjs.binary add %shifted, %maximum", "ctjs.unary plus %shifted");
    for (const auto & body :
         {positiveOutputBand, negativeOutputBand,
          replace(negativeOutputBand, "sub %part, %maximum", "add %part, %maximum")}) {
        run({.what = "left-shift outputs remain affine inside one positive or negative wrap band",
             .body = body,
             .arrays = "a:[zero,one,zero,one]",
             .reads = "a[0]=zero; a[2]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "wrapped left-shift output preserves reversed visit order",
         .body = replace(positiveOutputBand, "%position = ctjs.binary add %shifted, %maximum",
                         "%forward = ctjs.binary add %shifted, %maximum\n"
                         "  %position = ctjs.binary sub %two, %forward"),
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[0]=x; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto outputBandReload =
        replace(positiveOutputBand, "%shifted = ctjs.binary_static shl %input, %one",
                "%count = ctjs.get_property %base[%one]\n"
                "  %shifted = ctjs.binary_static shl %input, %count");
    run({.what = "wrapped left-shift output preserves exact reload gaps",
         .body = outputBandReload,
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[1]=one; a[0]=zero; a[1]=one; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "wrapped left-shift output leaves unvisited children retained",
         .body = replace(positiveOutputBand, "[%x, %one, %x, %one]", "[%x, %x, %x, %one]"),
         .arrays = "a:[zero,x,zero,one]",
         .reads = "a[0]=zero; a[2]=zero",
         .exit = "a -> {a,x}"});
    auto wideOutputBand =
        replace(replace(positiveOutputBand, "  %a =",
                        "  %start = ctjs.constant #ctjs.number<4746794007235919872>\n"
                        "  %count = ctjs.constant #ctjs.number<4629418941960159232>\n  %a ="),
                "add %part, %half", "add %part, %start");
    wideOutputBand = replace(replace(wideOutputBand, "shl %input, %one", "shl %input, %count"),
                             "%position = ctjs.binary add %shifted, %maximum",
                             "%quotient = ctjs.binary div %shifted, %maximum\n"
                             "  %position = ctjs.binary add %quotient, %one");
    wideOutputBand = replace(wideOutputBand, "[%x, %one, %x, %one]", "[%x, %x, %one, %one]");
    for (const auto & body : {wideOutputBand, replace(replace(wideOutputBand, "4746794007235919872",
                                                              "4746794007244308480"),
                                                      "add %part, %start", "sub %part, %start")}) {
        run({.what = "left-shift output bands use exact wide positive and negative products",
             .body = body,
             .arrays = "a:[zero,zero,one,one]",
             .reads = "a[0]=zero; a[2]=one",
             .exit = "a -> {a}"},
            "x");
    }
    reject("wrapped left-shift output still refuses a later overlapping count store",
           replace(outputBandReload,
                   "  %step =", "  ctjs.set_property %base[%one], %zero\n  %step ="));
    reject("wrapped left-shift output still requires bounded own indices",
           replace(positiveOutputBand, "add %shifted, %maximum", "add %shifted, %half"));
    reject("equal left-shift endpoints cannot hide an intervening output wrap",
           replace(replace(wideOutputBand, "ctjs.binary add %part, %start", "ctjs.unary plus %i"),
                   "add %i, %two\n  cf.br", "add %i, %one\n  cf.br"));
    const auto rightShiftIndex =
        replace(quotientIndex, "ctjs.binary div %i, %two", "ctjs.binary_static shr %i, %one");
    for (const auto & kind : {"shr", "ushr"}) {
        const auto body =
            replace(rightShiftIndex, "binary_static shr", std::string{"binary_static "} + kind);
        run({.what = "right shifts preserve exact bounded affine own positions",
             .body = body,
             .arrays = "a:[zero,zero,one,one]",
             .reads = "a[0]=zero; a[2]=one",
             .exit = "a -> {a}"},
            "x");
        run({.what = "right shifts allow an unaligned start with a divisible stride",
             .body = replace(body, "^header(%a, %zero, %zero", "^header(%a, %one, %zero"),
             .arrays = "a:[zero,zero,one,one]",
             .reads = "a[1]=x; a[3]=one",
             .exit = "a -> {a}"},
            "x");
        run({.what = "right shifts replay repeated own positions with nondivisible strides",
             .body = replace(body, "add %i, %two\n  cf.br", "add %i, %one\n  cf.br"),
             .arrays = "a:[zero,zero,one,one]",
             .reads = "a[0]=zero; a[1]=x; a[2]=one; a[3]=one",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "a dense shift footprint does not erase children at unwritten positions",
         .body = replace(rightShiftIndex, "shr %i, %one", "shr %i, %two"),
         .arrays = "a:[zero,x,one,one]",
         .reads = "a[0]=zero; a[2]=one",
         .exit = "a -> {a,x}"});
    run({.what = "a masked zero shift preserves each original own index",
         .body =
             replace(replace(complementIndex, "  %a =",
                             "  %count = ctjs.constant #ctjs.number<4629700416936869888>\n  %a ="),
                     "%negative = ctjs.unary bitnot %i\n  %position = ctjs.unary bitnot %negative",
                     "%position = ctjs.binary_static shr %i, %count"),
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[0]=zero; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "negative Number counts use their exact modulo32 shift",
         .body =
             replace(replace(rightShiftIndex, "  %a =",
                             "  %count = ctjs.constant #ctjs.number<13852790978814935040>\n  %a ="),
                     "shr %i, %one", "shr %i, %count"),
         .arrays = "a:[zero,zero,one,one]",
         .reads = "a[0]=zero; a[2]=one",
         .exit = "a -> {a}"},
        "x");
    const auto shiftReload =
        replace(rightShiftIndex, "  %position = ctjs.binary_static shr %i, %one",
                "  %count = ctjs.get_property %base[%three]\n"
                "  %position = ctjs.binary_static shr %i, %count");
    run({.what = "right-shift counts may reload beyond every transformed write",
         .body = shiftReload,
         .arrays = "a:[zero,zero,one,one]",
         .reads = "a[3]=one; a[0]=zero; a[3]=one; a[2]=one",
         .exit = "a -> {a}"},
        "x");
    run({.what = "dense right-shift footprints preserve disjoint count reloads",
         .body = replace(shiftReload, "add %i, %two\n  cf.br", "add %i, %one\n  cf.br"),
         .arrays = "a:[zero,zero,one,one]",
         .reads = "a[3]=one; a[0]=zero; a[3]=one; a[1]=x; a[3]=one; a[2]=one; a[3]=one; a[3]=one",
         .exit = "a -> {a}"},
        "x");
    const auto signedShiftBoundary =
        replace(replace(rightShiftIndex, "  %a =",
                        "  %maximum = ctjs.constant #ctjs.number<4746794007244308480>\n"
                        "  %half = ctjs.constant #ctjs.number<4742290407612743680>\n  %a ="),
                "%position = ctjs.binary_static shr %i, %one",
                "%part = ctjs.binary sub %maximum, %i\n"
                "  %shifted = ctjs.binary_static shr %part, %one\n"
                "  %position = ctjs.binary sub %half, %shifted");
    const auto unsignedShiftBoundary =
        replace(replace(replace(signedShiftBoundary, "4746794007244308480", "4751297606873776128"),
                        "4742290407612743680", "4746794007244308480"),
                "binary_static shr", "binary_static ushr");
    for (const auto & body : {signedShiftBoundary, unsignedShiftBoundary}) {
        run({.what = "right-shift ranges include exact signed and unsigned upper endpoints",
             .body = body,
             .arrays = "a:[zero,zero,one,one]",
             .reads = "a[0]=zero; a[2]=one",
             .exit = "a -> {a}"},
            "x");
    }
    reject("signed right shifts cannot cross the ToInt32 sign boundary",
           replace(signedShiftBoundary, "4746794007244308480", "4746794007248502784"));
    reject("unsigned right shifts cannot borrow an input above the bounded u32 range",
           replace(unsignedShiftBoundary, "4751297606873776128", "4751297606875873280"));
    const auto negativeShiftBoundary =
        replace(replace(unsignedShiftBoundary, "sub %maximum, %i", "sub %i, %maximum"),
                "sub %half, %shifted", "sub %shifted, %zero");
    run({.what = "unsigned shifts include the lowest bounded negative ToUint32 input",
         .body = negativeShiftBoundary,
         .arrays = "a:[zero,zero,one,one]",
         .reads = "a[0]=zero; a[2]=one",
         .exit = "a -> {a}"},
        "x");
    const auto negativeShift = replace(
        replace(replace(negativeShiftBoundary, "4751297606873776128", "4616189618054758400"),
                "4746794007244308480", "4746794007240114176"),
        "sub %shifted, %zero", "sub %shifted, %half");
    const auto negativeSignBoundary =
        replace(replace(negativeShift, "4616189618054758400", "4746794007250599936"),
                "4746794007240114176", "4742290407612743680");
    for (const auto & body : {negativeShift, negativeSignBoundary}) {
        run({.what = "negative unsigned shifts keep one ToUint32 band across the signed boundary",
             .body = body,
             .arrays = "a:[zero,zero,one,one]",
             .reads = "a[0]=zero; a[2]=one",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "negative unsigned shifts allow an unaligned first input",
         .body = replace(negativeShift, "^header(%a, %zero, %zero", "^header(%a, %one, %zero"),
         .arrays = "a:[zero,zero,one,one]",
         .reads = "a[1]=x; a[3]=one",
         .exit = "a -> {a}"},
        "x");
    run({.what = "negative unsigned shifts retain exact masked negative counts",
         .body = replace(replace(negativeShift, "  %a =",
                                 "  %count = ctjs.constant #ctjs.number<13852790978814935040>\n"
                                 "  %a ="),
                         "ushr %part, %one", "ushr %part, %count"),
         .arrays = "a:[zero,zero,one,one]",
         .reads = "a[0]=zero; a[2]=one",
         .exit = "a -> {a}"},
        "x");
    const auto negativeShiftReload =
        replace(negativeShift, "  %shifted = ctjs.binary_static ushr %part, %one",
                "  %count = ctjs.get_property %base[%three]\n"
                "  %shifted = ctjs.binary_static ushr %part, %count");
    run({.what = "negative unsigned shift counts reload outside the translated footprint",
         .body = negativeShiftReload,
         .arrays = "a:[zero,zero,one,one]",
         .reads = "a[3]=one; a[0]=zero; a[3]=one; a[2]=one",
         .exit = "a -> {a}"},
        "x");
    for (const auto & body :
         {replace(negativeShift, "sub %i, %maximum", "sub %i, %two"),
          replace(negativeShiftBoundary, "4751297606873776128", "4751297606875873280"),
          replace(negativeSignBoundary, "binary_static ushr", "binary_static shr"),
          replace(negativeShiftReload, "%base[%three]", "%base[%one]"),
          replace(negativeShiftReload,
                  "  %step =", "  ctjs.set_property %base[%three], %two\n  %step =")}) {
        reject("negative unsigned shifts retain band, bounds and reload guards", body);
    }
    const auto signedNegative =
        replace(replace(negativeShift, "binary_static ushr", "binary_static shr"),
                "sub %shifted, %half", "add %shifted, %two");
    const auto signedMinimum =
        replace(replace(replace(signedNegative, "4616189618054758400", "4746794007248502784"),
                        "4746794007240114176", "4742290407621132288"),
                "add %shifted, %two", "add %shifted, %half");
    const auto signedPositiveWrapped =
        replace(signedMinimum, "sub %i, %maximum", "add %i, %maximum");
    const auto signedZeroCrossing =
        replace(replace(signedNegative, "sub %i, %maximum", "sub %i, %one"), "add %shifted, %two",
                "add %shifted, %one");
    for (const auto & body :
         {signedNegative, signedMinimum, signedPositiveWrapped, signedZeroCrossing,
          replace(negativeShiftBoundary, "binary_static ushr", "binary_static shr")}) {
        run({.what = "signed right shifts remain affine inside each bounded ToInt32 band",
             .body = body,
             .arrays = "a:[zero,zero,one,one]",
             .reads = "a[0]=zero; a[2]=one",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "negative signed shifts floor unaligned inputs with a divisible stride",
         .body = replace(signedNegative, "^header(%a, %zero, %zero", "^header(%a, %one, %zero"),
         .arrays = "a:[zero,zero,one,one]",
         .reads = "a[1]=x; a[3]=one",
         .exit = "a -> {a}"},
        "x");
    const auto signedNegativeReload =
        replace(replace(negativeShiftReload, "binary_static ushr", "binary_static shr"),
                "sub %shifted, %half", "add %shifted, %two");
    run({.what = "signed shift count reloads retain the translated negative-band footprint",
         .body = signedNegativeReload,
         .arrays = "a:[zero,zero,one,one]",
         .reads = "a[3]=one; a[0]=zero; a[3]=one; a[2]=one",
         .exit = "a -> {a}"},
        "x");
    for (const auto & body : {replace(signedMinimum, "4746794007248502784", "4746794007250599936"),
                              replace(signedNegativeReload, "%base[%three]", "%base[%one]"),
                              replace(signedNegativeReload, "  %step =",
                                      "  ctjs.set_property %base[%three], %two\n  %step =")}) {
        reject("signed shift bands retain discontinuity and full reload guards", body);
    }
    for (const auto & body :
         {negativeShift, signedNegative, signedMinimum, signedPositiveWrapped}) {
        run({.what = "dense right shifts preserve negative and wrapping conversion bands",
             .body = replace(body, "add %i, %two\n  cf.br", "add %i, %one\n  cf.br"),
             .arrays = "a:[zero,zero,one,one]",
             .reads = "a[0]=zero; a[1]=x; a[2]=one; a[3]=one",
             .exit = "a -> {a}"},
            "x");
    }
    const auto unevenShift = replace(
        replace(rightShiftIndex, "[%x, %x, %one, %one]", "[%x, %x, %x, %x, %one, %one, %one]"),
        "add %i, %two\n  cf.br", "add %i, %three\n  cf.br");
    run({.what = "uneven right-shift footprints preserve unwritten gap children",
         .body = unevenShift,
         .arrays = "a:[zero,zero,x,zero,one,one,one]",
         .reads = "a[0]=zero; a[3]=x; a[6]=one",
         .exit = "a -> {a,x}"});
    run({.what = "dense right-shift footprints compose with reversed negative scaling",
         .body = replace(unevenShift, "%position = ctjs.binary_static shr %i, %one",
                         "%part = ctjs.binary_static shr %i, %one\n"
                         "  %position = ctjs.binary sub %three, %part"),
         .arrays = "a:[zero,x,zero,zero,one,one,one]",
         .reads = "a[0]=x; a[3]=zero; a[6]=one",
         .exit = "a -> {a,x}"});
    const auto scaledShift = replace(replace(unevenShift, "[%x, %x, %x, %x, %one, %one, %one]",
                                             "[%x, %one, %x, %one, %x, %one, %x]"),
                                     "%position = ctjs.binary_static shr %i, %one",
                                     "%count = ctjs.get_property %base[%one]\n"
                                     "  %part = ctjs.binary_static shr %i, %count\n"
                                     "  %scaled = ctjs.binary_static shl %part, %two\n"
                                     "  %position = ctjs.binary div %scaled, %two");
    run({.what = "dense shift enclosures retain proved scaling gaps and exact division",
         .body = scaledShift,
         .arrays = "a:[zero,one,zero,one,x,one,zero]",
         .reads = "a[1]=one; a[0]=zero; a[1]=one; a[3]=one; a[1]=one; a[6]=zero",
         .exit = "a -> {a,x}"});
    const auto unevenReload = replace(replace(unevenShift, "[%x, %x, %x, %x, %one, %one, %one]",
                                              "[%x, %x, %one, %x, %one, %one, %one]"),
                                      "%position = ctjs.binary_static shr %i, %one",
                                      "%count = ctjs.get_property %base[%two]\n"
                                      "  %position = ctjs.binary_static shr %i, %count");
    reject("an uneven shift footprint cannot claim its unproved sparse gap for a reload",
           unevenReload);
    reject("dense shift enclosures reject overlapping scaled reloads",
           replace(replace(scaledShift, "[%x, %one, %x, %one, %x, %one, %x]",
                           "[%x, %one, %one, %one, %x, %one, %x]"),
                   "%base[%one]", "%base[%two]"));
    reject("dense shift endpoints cannot imply integral intermediate quotients",
           replace(unevenShift, "%position = ctjs.binary_static shr %i, %one",
                   "%part = ctjs.binary_static shr %i, %one\n"
                   "  %position = ctjs.binary div %part, %two"));
    for (const auto & body :
         {replace(shiftReload, "%base[%three]", "%base[%one]"),
          replace(shiftReload, "  %step =", "  ctjs.set_property %base[%three], %two\n  %step ="),
          replace(rightShiftIndex, "shr %i, %one", "shr %one, %i"),
          replace(rightShiftIndex, "shr %i, %one", "shr %i, %s"),
          replace(rightShiftIndex, "%position = ctjs.binary_static shr %i, %one",
                  "%part = ctjs.unary neg %i\n  %position = ctjs.binary_static ushr %part, %one"),
          replace(rightShiftIndex, "%position = ctjs.binary_static shr %i, %one",
                  "%part = ctjs.binary_static shr %i, %one\n"
                  "  %position = ctjs.binary add %part, %three")}) {
        reject("right-shift ranges retain count, reload, signed-input and own-bounds guards", body);
    }
    run({.what = "exact index quotients overwrite only their bounded prefix",
         .body = quotientIndex,
         .arrays = "a:[zero,zero,one,one]",
         .reads = "a[0]=zero; a[2]=one",
         .exit = "a -> {a}"},
        "x");
    run({.what = "nonzero quotient starts preserve their translated positions",
         .body = replace(
             replace(quotientIndex, "[%x, %x, %one, %one]", "[%one, %x, %x, %one, %one, %one]"),
             "^header(%a, %zero, %zero", "^header(%a, %two, %zero"),
         .arrays = "a:[one,zero,zero,one,one,one]",
         .reads = "a[2]=x; a[4]=one",
         .exit = "a -> {a}"},
        "x");
    const auto reloadedDivisor =
        replace(replace(quotientIndex, "[%x, %x, %one, %one]", "[%x, %x, %one, %two]"),
                "  %position = ctjs.binary div %i, %two",
                "  %divisor = ctjs.get_property %base[%three]\n"
                "  %position = ctjs.binary div %i, %divisor");
    run({.what = "a divisor reload outside the quotient footprint stays invariant",
         .body = reloadedDivisor,
         .arrays = "a:[zero,zero,one,two]",
         .reads = "a[3]=two; a[0]=zero; a[3]=two; a[2]=one",
         .exit = "a -> {a}"},
        "x");
    const auto quotientGap =
        replace(replace(replace(replace(reloadedDivisor, "[%x, %x, %one, %two]",
                                        "[%x, %two, %x, %one, %one]"),
                                "%divisor = ctjs.get_property %base[%three]",
                                "%divisor = ctjs.get_property %base[%one]"),
                        "  %a =", "  %four = ctjs.binary add %two, %two\n  %a ="),
                "add %i, %two\n  cf.br", "add %i, %four\n  cf.br");
    run({.what = "quotient footprints retain their own stride gaps",
         .body = quotientGap,
         .arrays = "a:[zero,two,zero,one,one]",
         .reads = "a[1]=two; a[0]=zero; a[1]=two; a[4]=one",
         .exit = "a -> {a}"},
        "x");
    run({.what = "saved children survive quotient overwrites",
         .body = replace(replace(quotientIndex, "  cf.br ^header(%a,",
                                 "  %saved = ctjs.get_property %a[%one]\n  cf.br ^header(%a,"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "a:[zero,zero,one,one]",
         .reads = "a[1]=x; a[0]=zero; a[2]=one",
         .exit = "x -> {x}"});
    run({.what = "zero-trip quotient overwrites retain their original children",
         .body = replace(replace(quotientIndex, "[%x, %x, %one, %one]", "[%x, %x]"),
                         "^header(%a, %zero, %zero", "^header(%a, %two, %zero"),
         .arrays = "a:[x,x]",
         .exit = "a -> {a,x}"});
    for (const auto & body :
         {replace(quotientIndex, "^header(%a, %zero, %zero", "^header(%a, %one, %zero"),
          replace(replace(quotientIndex, "[%x, %x, %one, %one]", "[%x, %x, %one]"),
                  "add %i, %two\n  cf.br", "add %i, %one\n  cf.br"),
          replace(quotientIndex, "div %i, %two", "div %i, %zero"),
          replace(quotientIndex, "div %i, %two", "div %two, %i"),
          replace(replace(quotientIndex, "  %a =", "  %negative = ctjs.unary neg %two\n  %a ="),
                  "div %i, %two", "div %i, %negative"),
          replace(replace(quotientIndex,
                          "  %a =", "  %text = ctjs.constant #ctjs.string<\"2\">\n  %a ="),
                  "div %i, %two", "div %i, %text"),
          replace(replace(reloadedDivisor, "[%x, %x, %one, %two]", "[%x, %two, %one, %two]"),
                  "%divisor = ctjs.get_property %base[%three]",
                  "%divisor = ctjs.get_property %base[%one]"),
          replace(reloadedDivisor,
                  "  %step =", "  ctjs.set_property %base[%three], %one\n  %step ="),
          replace(quotientIndex, "div %i, %two", "div %i, %s")}) {
        run({.what = "quotient stores require exact division and immutable Number divisors",
             .body = body,
             .failure = ArrayContentsFailure::UnsupportedControlFlow});
    }
    const auto negativeQuotient = replace(
        replace(replace(quotientIndex, "[%x, %x, %one, %one]", "[%one, %x, %x, %one]"), "  %a =",
                "  %negative = ctjs.unary neg %two {storage_test_id = \"negative\"}\n  %a ="),
        "%position = ctjs.binary div %i, %two",
        "%part = ctjs.binary div %i, %negative\n"
        "  %position = ctjs.binary add %part, %two");
    run({.what = "negative divisors reverse exact quotient positions with a positive stride",
         .body = negativeQuotient,
         .arrays = "a:[one,zero,zero,one]",
         .reads = "a[0]=one; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "negative divisors preserve signed intermediate endpoints",
         .body = replace(replace(negativeQuotient, "%part = ctjs.binary div %i, %negative",
                                 "%signed = ctjs.binary sub %i, %two\n"
                                 "  %part = ctjs.binary div %signed, %negative"),
                         "add %part, %two", "add %part, %one"),
         .arrays = "a:[one,zero,zero,one]",
         .reads = "a[0]=one; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "a negative divisor preserves a single signed-zero own key",
         .body = replace(replace(negativeQuotient, "[%one, %x, %x, %one]", "[%x, %one]"),
                         "add %part, %two", "add %part, %zero"),
         .arrays = "a:[zero,one]",
         .reads = "a[0]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto negativeDivisorGap =
        replace(replace(replace(replace(negativeQuotient, "[%one, %x, %x, %one]",
                                        "[%one, %x, %negative, %x, %one]"),
                                "  %a =", "  %four = ctjs.binary add %two, %two\n  %a ="),
                        "add %i, %two\n  cf.br", "add %i, %four\n  cf.br"),
                "%part = ctjs.binary div %i, %negative\n"
                "  %position = ctjs.binary add %part, %two",
                "%divisor = ctjs.get_property %base[%two]\n"
                "  %part = ctjs.binary div %i, %divisor\n"
                "  %position = ctjs.binary add %part, %three");
    run({.what = "negative divisor reloads remain invariant in descending quotient gaps",
         .body = negativeDivisorGap,
         .arrays = "a:[one,zero,negative,zero,one]",
         .reads = "a[2]=negative; a[0]=one; a[2]=negative; a[4]=one",
         .exit = "a -> {a}"},
        "x");
    for (const auto & body :
         {replace(negativeQuotient, "^header(%a, %zero, %zero", "^header(%a, %one, %zero"),
          replace(negativeQuotient, "add %i, %two\n  cf.br", "add %i, %one\n  cf.br"),
          replace(negativeQuotient, "add %part, %two", "add %part, %zero"),
          replace(replace(negativeDivisorGap, "[%one, %x, %negative, %x, %one]",
                          "[%one, %x, %one, %negative, %one]"),
                  "%divisor = ctjs.get_property %base[%two]",
                  "%divisor = ctjs.get_property %base[%three]"),
          replace(negativeDivisorGap,
                  "  %step =", "  ctjs.set_property %base[%two], %zero\n  %step =")}) {
        reject("negative quotient stores retain integrality, bounds and reload exclusions", body);
    }
    const auto scaledIndex =
        replace(replace(savedChild, "  %read =",
                        "  %position = ctjs.binary mul %i, %one\n"
                        "  ctjs.set_property %base[%position], %zero\n  %read ="),
                "ctjs.return %result", "ctjs.return %a");
    for (const auto & expression :
         {"%position = ctjs.binary mul %i, %one", "%position = ctjs.binary mul %one, %i",
          "%position = ctjs.unary plus %i",
          "%part = ctjs.unary neg %i\n"
          "  %position = ctjs.unary neg %part"}) {
        run({.what = "unit scaling and unary signs preserve current-index overwrites",
             .body = replace(scaledIndex, "%position = ctjs.binary mul %i, %one", expression),
             .arrays = "a:[zero,zero]",
             .reads = "a[0]=zero; a[1]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    const auto scaledVisit = replace(
        replace(replace(scaledIndex, "[%one, %x]", "[%x, %one]"), "mul %i, %one", "mul %i, %two"),
        "add %i, %one", "add %i, %two");
    run({.what = "a nonunit factor admits a single bounded visit",
         .body = scaledVisit,
         .arrays = "a:[zero,one]",
         .reads = "a[0]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "unary negation preserves a single signed-zero own key",
         .body = replace(scaledVisit, "ctjs.binary mul %i, %two", "ctjs.unary neg %i"),
         .arrays = "a:[zero,one]",
         .reads = "a[0]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto scaledStart = replace(replace(scaledVisit, "[%x, %one]", "[%one, %one, %x]"),
                                     "^header(%a, %zero, %zero", "^header(%a, %one, %zero");
    run({.what = "scaled nonzero starts overwrite their translated own position",
         .body = scaledStart,
         .arrays = "a:[one,one,zero]",
         .reads = "a[1]=one",
         .exit = "a -> {a}"},
        "x");
    const auto reloadedFactor = replace(replace(scaledVisit, "[%x, %one]", "[%x, %two]"),
                                        "  %position = ctjs.binary mul %i, %two",
                                        "  %factor = ctjs.get_property %base[%one]\n"
                                        "  %position = ctjs.binary mul %i, %factor");
    run({.what = "a factor outside the scaled footprint remains invariant",
         .body = reloadedFactor,
         .arrays = "a:[zero,two]",
         .reads = "a[1]=two; a[0]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "scaled overwrites preserve a saved child",
         .body = replace(replace(scaledVisit, "  cf.br ^header(%a,",
                                 "  %saved = ctjs.get_property %a[%zero]\n  cf.br ^header(%a,"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "a:[zero,one]",
         .reads = "a[0]=x; a[0]=zero",
         .exit = "x -> {x}"});
    run({.what = "scaled overwrites preserve aliases in unvisited elements",
         .body = replace(scaledVisit, "[%x, %one]", "[%x, %x]"),
         .arrays = "a:[zero,x]",
         .reads = "a[0]=zero",
         .exit = "a -> {a,x}"});
    run({.what = "zero-trip scaled overwrites preserve their original elements",
         .body = replace(scaledVisit, "^header(%a, %zero, %zero", "^header(%a, %two, %zero"),
         .arrays = "a:[x,one]",
         .exit = "a -> {a,x}"});
    run({.what = "negative scaling preserves a single visit to the signed-zero own key",
         .body =
             replace(replace(scaledVisit, "  %a =", "  %negative = ctjs.unary neg %two\n  %a ="),
                     "mul %i, %two", "mul %i, %negative"),
         .arrays = "a:[zero,one]",
         .reads = "a[0]=zero",
         .exit = "a -> {a}"},
        "x");
    for (const auto & body :
         {replace(scaledVisit, "add %i, %two", "add %i, %one"),
          replace(scaledStart, "[%one, %one, %x]", "[%one, %x]"),
          replace(scaledVisit, "mul %i, %two", "mul %i, %zero"),
          replace(scaledVisit, "mul %i, %two", "mul %i, %s"),
          replace(
              replace(scaledVisit, "  %a =", "  %text = ctjs.constant #ctjs.string<\"2\">\n  %a ="),
              "mul %i, %two", "mul %i, %text"),
          replace(replace(scaledVisit, "  %a =",
                          "  %huge = ctjs.constant #ctjs.number<4751297606873776128>\n  %a ="),
                  "mul %i, %two", "mul %i, %huge"),
          replace(replace(reloadedFactor, "[%x, %two]", "[%two, %x]"),
                  "%factor = ctjs.get_property %base[%one]",
                  "%factor = ctjs.get_property %base[%zero]"),
          replace(reloadedFactor, "  %step =", "  ctjs.set_property %base[%one], %one\n  %step ="),
          replace(replace(scaledStart, "[%one, %one, %x]", "[%one, %one, %two]"),
                  "  %position = ctjs.binary mul %i, %two",
                  "  %factor = ctjs.get_property %base[%two]\n"
                  "  %position = ctjs.binary mul %i, %factor")}) {
        run({.what = "scaled stores reject growth, invalid factors and overlapping reloads",
             .body = body,
             .failure = ArrayContentsFailure::UnsupportedControlFlow});
    }
    const std::string quotientOffset =
        "ctjs.binary div %i, %two\n  %position = ctjs.binary add %part, %one";
    const auto composedIndex =
        replace(replace(offsetIndex, "[%one, %x, %one, %x]", "[%one, %x, %x, %one]"),
                "%position = ctjs.binary add %i, %one", "%part = " + quotientOffset);
    for (const auto & expression : {quotientOffset,
                                    std::string{"ctjs.binary div %i, %two\n"
                                                "  %position = ctjs.binary add %one, %part"},
                                    std::string{"ctjs.binary add %i, %two\n"
                                                "  %position = ctjs.binary div %part, %two"},
                                    std::string{"ctjs.binary sub %i, %two\n"
                                                "  %half = ctjs.binary div %part, %two\n"
                                                "  %position = ctjs.binary add %half, %two"}}) {
        run({.what = "composed exact quotients and offsets retain every intermediate Number",
             .body = replace(composedIndex, quotientOffset, expression),
             .arrays = "a:[one,zero,zero,one]",
             .reads = "a[0]=one; a[2]=zero",
             .exit = "a -> {a}"},
            "x");
    }
    for (const auto & expression : {"ctjs.binary mul %i, %two", "ctjs.binary mul %two, %i"}) {
        run({.what = "composed scaling retains operation order and the translated stride",
             .body = replace(offsetIndex, "%position = ctjs.binary add %i, %one",
                             std::string{"%part = "} + expression +
                                 "\n  %half = ctjs.binary div %part, %two\n"
                                 "  %position = ctjs.binary_static add %one, %half"),
             .arrays = "a:[one,zero,one,zero]",
             .reads = "a[0]=one; a[2]=one",
             .exit = "a -> {a}"},
            "x");
    }
    const auto composedReload =
        replace(replace(replace(replace(composedIndex, "[%one, %x, %x, %one]",
                                        "[%one, %x, %two, %x, %one]"),
                                "  %a =", "  %four = ctjs.binary add %two, %two\n  %a ="),
                        "add %i, %two\n  cf.br", "add %i, %four\n  cf.br"),
                "  %part = ctjs.binary div %i, %two",
                "  %divisor = ctjs.get_property %base[%two]\n"
                "  %part = ctjs.binary div %i, %divisor");
    run({.what = "composed footprints preserve reloads in their translated stride gaps",
         .body = composedReload,
         .arrays = "a:[one,zero,two,zero,one]",
         .reads = "a[2]=two; a[0]=one; a[2]=two; a[4]=one",
         .exit = "a -> {a}"},
        "x");
    run({.what = "composed overwrites retain children saved before the loop",
         .body = replace(replace(composedIndex, "  cf.br ^header(%a,",
                                 "  %saved = ctjs.get_property %a[%one]\n  cf.br ^header(%a,"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "a:[one,zero,zero,one]",
         .reads = "a[1]=x; a[0]=one; a[2]=zero",
         .exit = "x -> {x}"});
    run({.what = "zero-trip composed overwrites preserve original children",
         .body = replace(replace(composedIndex, "[%one, %x, %x, %one]", "[%one, %x]"),
                         "^header(%a, %zero, %zero", "^header(%a, %two, %zero"),
         .arrays = "a:[one,x]",
         .exit = "a -> {a,x}"});
    for (const auto & body :
         {replace(composedIndex, "add %i, %two\n  cf.br", "add %i, %one\n  cf.br"),
          replace(composedIndex, "div %i, %two", "div %i, %zero"),
          replace(composedIndex, "add %part, %one", "add %part, %i"),
          replace(composedIndex, "add %part, %one", "add %part, %s"),
          replace(composedIndex, "add %part, %one", "add %part, %three"),
          replace(composedIndex, "div %i, %two", "mul %i, %zero"),
          replace(replace(composedIndex,
                          "  %a =", "  %text = ctjs.constant #ctjs.string<\"1\">\n  %a ="),
                  "add %part, %one", "add %part, %text"),
          replace(
              replace(composedReload, "[%one, %x, %two, %x, %one]", "[%one, %x, %two, %two, %one]"),
              "%divisor = ctjs.get_property %base[%two]",
              "%divisor = ctjs.get_property %base[%three]"),
          replace(composedReload,
                  "  %step =", "  ctjs.set_property %base[%two], %one\n  %step =")}) {
        reject("composed stores retain exact division, own bounds and invariant reload guards",
               body);
    }
    for (const auto & number : {"4751297606873776128", "4845873199050653696"}) {
        reject("cancelling a large offset cannot conceal an unproved intermediate Number",
               replace(replace(scaledIndex, "  %a =",
                               std::string{"  %huge = ctjs.constant #ctjs.number<"} + number +
                                   ">\n  %a ="),
                       "%position = ctjs.binary mul %i, %one",
                       "%part = ctjs.binary add %i, %huge\n"
                       "  %position = ctjs.binary sub %part, %huge"));
    }
    std::string deepIndex, deepUnaryIndex, deepLeftShiftIndex;
    for (unsigned i = 0; i < 65; ++i) {
        deepIndex += "  %part" + std::to_string(i) + " = ctjs.binary add %" +
                     (i == 0 ? std::string{"i"} : "part" + std::to_string(i - 1)) + ", %zero\n";
        deepUnaryIndex += "  %part" + std::to_string(i) + " = ctjs.unary plus %" +
                          (i == 0 ? std::string{"i"} : "part" + std::to_string(i - 1)) + "\n";
        deepLeftShiftIndex += "  %part" + std::to_string(i) + " = ctjs.binary_static shl %" +
                              (i == 0 ? std::string{"i"} : "part" + std::to_string(i - 1)) +
                              ", %zero\n";
    }
    reject("composed index depth is bounded even when every operation adds zero",
           replace(scaledIndex, "  %position = ctjs.binary mul %i, %one\n",
                   deepIndex + "  %position = ctjs.binary add %part64, %zero\n"));
    reject("unary index depth is bounded even when every operation preserves its Number",
           replace(scaledIndex, "  %position = ctjs.binary mul %i, %one\n",
                   deepUnaryIndex + "  %position = ctjs.unary plus %part64\n"));
    reject("left-shift index depth is bounded even when every count is zero",
           replace(scaledIndex, "  %position = ctjs.binary mul %i, %one\n",
                   deepLeftShiftIndex + "  %position = ctjs.binary_static shl %part64, %zero\n"));
    const auto reverseIndex =
        replace(offsetIndex, "ctjs.binary add %i, %one", "ctjs.binary sub %three, %i");
    run({.what = "reversed subtraction overwrites only descending visited own positions",
         .body = reverseIndex,
         .arrays = "a:[one,zero,one,zero]",
         .reads = "a[0]=one; a[2]=one",
         .exit = "a -> {a}"},
        "x");
    run({.what = "descending compositions preserve exact signed intermediate Numbers",
         .body = replace(reverseIndex, "%position = ctjs.binary sub %three, %i",
                         "%part = ctjs.binary sub %one, %i\n"
                         "  %position = ctjs.binary add %part, %two"),
         .arrays = "a:[one,zero,one,zero]",
         .reads = "a[0]=one; a[2]=one",
         .exit = "a -> {a}"},
        "x");
    run({.what = "descending integral quotients keep their translated stride",
         .body = replace(composedIndex, "add %part, %one", "sub %two, %part"),
         .arrays = "a:[one,zero,zero,one]",
         .reads = "a[0]=one; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    const auto reverseReload =
        replace(replace(reverseIndex, "[%one, %x, %one, %x]", "[%one, %x, %three, %x]"),
                "  %position = ctjs.binary sub %three, %i",
                "  %offset = ctjs.get_property %base[%two]\n"
                "  %position = ctjs.binary sub %offset, %i");
    run({.what = "reloads in descending stride gaps stay invariant",
         .body = reverseReload,
         .arrays = "a:[one,zero,three,zero]",
         .reads = "a[2]=three; a[0]=one; a[2]=three; a[2]=three",
         .exit = "a -> {a}"},
        "x");
    for (const auto & body :
         {replace(reverseIndex, "sub %three, %i", "sub %zero, %i"),
          replace(reverseIndex, "sub %three, %i", "sub %i, %i"),
          replace(replace(reverseReload, "[%one, %x, %three, %x]", "[%one, %x, %one, %three]"),
                  "%offset = ctjs.get_property %base[%two]",
                  "%offset = ctjs.get_property %base[%three]"),
          replace(reverseReload,
                  "  %step =", "  ctjs.set_property %base[%two], %zero\n  %step =")}) {
        reject("descending footprints retain own bounds and complete reload exclusions", body);
    }
    const auto negativeScale = replace(
        replace(reverseIndex, "  %a =",
                "  %negative = ctjs.unary neg %one {storage_test_id = \"negative\"}\n  %a ="),
        "%position = ctjs.binary sub %three, %i",
        "%part = ctjs.binary mul %i, %negative\n"
        "  %position = ctjs.binary add %part, %three");
    for (const auto & expression :
         {"ctjs.binary mul %i, %negative", "ctjs.binary mul %negative, %i", "ctjs.unary neg %i"}) {
        run({.what = "negative factors and unary negation reverse exact visited positions",
             .body = replace(negativeScale, "ctjs.binary mul %i, %negative", expression),
             .arrays = "a:[one,zero,one,zero]",
             .reads = "a[0]=one; a[2]=one",
             .exit = "a -> {a}"},
            "x");
    }
    run({.what = "negative nonunit factors preserve quotient and stride magnitudes",
         .body = replace(replace(negativeScale, "neg %one", "neg %two"),
                         "%part = ctjs.binary mul %i, %negative",
                         "%half = ctjs.binary div %i, %two\n"
                         "  %part = ctjs.binary mul %half, %negative"),
         .arrays = "a:[one,zero,one,zero]",
         .reads = "a[0]=one; a[2]=one",
         .exit = "a -> {a}"},
        "x");
    const auto unaryReload = replace(reverseReload, "%position = ctjs.binary sub %offset, %i",
                                     "%part = ctjs.unary neg %i\n"
                                     "  %position = ctjs.binary add %offset, %part");
    run({.what = "unary index reversal preserves reloads in stride gaps",
         .body = unaryReload,
         .arrays = "a:[one,zero,three,zero]",
         .reads = "a[2]=three; a[0]=one; a[2]=three; a[2]=three",
         .exit = "a -> {a}"},
        "x");
    for (const auto & body :
         {replace(unaryReload, "add %offset, %part", "add %zero, %part"),
          replace(unaryReload, "add %offset, %part", "sub %offset, %part"),
          replace(replace(unaryReload, "[%one, %x, %three, %x]", "[%one, %x, %one, %three]"),
                  "%offset = ctjs.get_property %base[%two]",
                  "%offset = ctjs.get_property %base[%three]"),
          replace(unaryReload, "  %step =", "  ctjs.set_property %base[%two], %zero\n  %step ="),
          replace(unaryReload, "ctjs.unary neg %i", "ctjs.unary bitnot %i")}) {
        reject("unary indices retain own bounds, exact operations and reload exclusions", body);
    }
    const auto negativeReload =
        replace(replace(negativeScale, "[%one, %x, %one, %x]", "[%one, %x, %negative, %x]"),
                "%part = ctjs.binary mul %i, %negative",
                "%factor = ctjs.get_property %base[%two]\n"
                "  %part = ctjs.binary mul %i, %factor");
    run({.what = "negative factor reloads remain invariant in the scaled stride gaps",
         .body = negativeReload,
         .arrays = "a:[one,zero,negative,zero]",
         .reads = "a[2]=negative; a[0]=one; a[2]=negative; a[2]=negative",
         .exit = "a -> {a}"},
        "x");
    for (const auto & body : {replace(negativeScale, "add %part, %three", "add %part, %zero"),
                              replace(negativeScale, "add %part, %three", "sub %three, %part"),
                              replace(replace(negativeReload, "[%one, %x, %negative, %x]",
                                              "[%one, %x, %one, %negative]"),
                                      "%factor = ctjs.get_property %base[%two]",
                                      "%factor = ctjs.get_property %base[%three]"),
                              replace(negativeReload, "  %step =",
                                      "  ctjs.set_property %base[%two], %zero\n  %step =")}) {
        reject("negative scaling retains own bounds and complete reload exclusions", body);
    }
    reversed = replace(savedChild, "compare lt %index, %length", "compare gt %length, %index");
    run({.what = "reversed strict length guards retain the original returned child",
         .body = reversed,
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "reversed strict guards discharge only unreturned children",
         .body = replace(reversed, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    run({.what = "a reversed strict guard preserves a zero-trip start",
         .body = replace(reversed, "^header(%a, %zero, %zero", "^header(%a, %two, %zero"),
         .arrays = "a:[one,x]",
         .exit = "zero -> {}"},
        "x");
    negated =
        replace(replace(savedChild, "compare lt %index, %length", "compare ge %index, %length"),
                "  %flag = ctjs.truthy %less",
                "  %negated = ctjs.unary not %less\n  %flag = ctjs.truthy %negated");
    negatedReversed = replace(negated, "compare ge %index, %length", "compare le %length, %index");
    for (const std::string & source : {negated, negatedReversed}) {
        run({.what = "negated inclusive comparisons retain the exact returned child",
             .body = source,
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "negated guards discharge only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        run({.what = "negated guards preserve the exact zero-trip start",
             .body = replace(source, "^header(%a, %zero, %zero", "^header(%a, %two, %zero"),
             .arrays = "a:[one,x]",
             .exit = "zero -> {}"},
            "x");
    }
    const std::string makeStart =
        "  %start = ctjs.binary sub %one, %one {storage_test_id = \"start\"}\n";
    computed =
        prefix + makeStart + replace(loop, "^header(%a, %zero, %zero", "^header(%a, %start, %zero");
    run({.what = "an exact computed zero initializes the existing unit-step induction",
         .body = computed,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    computedChild = replace(savedChild, "  cf.br ^header(%a, %zero, %zero",
                            makeStart + "  cf.br ^header(%a, %start, %zero");
    run({.what = "computed-zero induction retains the exact returned child",
         .body = computedChild,
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "computed-zero induction confines children only when no result retains them",
         .body = replace(computedChild, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    const std::string savedLength =
        prefix +
        "  %seed = ctjs.create_array [] {storage_test_id = \"seed\"}\n"
        "  %name = ctjs.constant #ctjs.string<\"length\">\n"
        "  %start = ctjs.get_property %seed[%name]\n"
        "  ctjs.append %one to %seed\n" +
        replace(loop, "^header(%a, %zero, %zero", "^header(%a, %start, %zero");
    run({.what = "a saved empty length starts at zero after its source array grows",
         .body = savedLength,
         .arrays = "a:[one,two,three]; seed:[one]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    alternateStart =
        prefix + makeStart +
        "  %choice = ctjs.truthy %zero\n"
        "  cf.cond_br %choice, ^entry(%start : !ctjs.value), ^entry(%zero : !ctjs.value)\n"
        "^entry(%initial: !ctjs.value):\n" +
        replace(loop, "^header(%a, %zero, %zero", "^header(%a, %initial, %zero");
    run({.what = "separate literal and computed zeros survive predecessor transport",
         .body = alternateStart,
         .arrays = "a:[one,two,three] | a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three; a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}; added -> {}"});
    const std::string nonzeroStart =
        replace(computed, "binary sub %one, %one", "binary sub %two, %one");
    run({.what = "a computed nonzero start skips exactly its preceding own elements",
         .body = nonzeroStart,
         .arrays = "a:[one,two,three]",
         .reads = "a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    run({.what = "a saved nonzero length survives its source array being emptied",
         .body = replace(replace(savedLength, "%seed = ctjs.create_array []",
                                 "%seed = ctjs.create_array [%one]"),
                         "ctjs.append %one to %seed", "ctjs.set_property %seed[%name], %zero"),
         .arrays = "a:[one,two,three]; seed:[]",
         .reads = "a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    run({.what = "distinct predecessor starts preserve their own visited indices",
         .body = replace(alternateStart, "^entry(%start :", "^entry(%one :"),
         .arrays = "a:[one,two,three] | a:[one,two,three]",
         .reads = "a[1]=two; a[2]=three; a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}; added -> {}"});
    nonzeroChild = replace(computedChild, "binary sub %one, %one", "binary sub %two, %one");
    run({.what = "a nonzero start preserves the exact returned child",
         .body = nonzeroChild,
         .arrays = "a:[one,x]",
         .reads = "a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "a child before the start is confined when its array is not returned",
         .body = replace(nonzeroChild, "[%one, %x]", "[%x, %one]"),
         .arrays = "a:[x,one]",
         .reads = "a[1]=one",
         .exit = "one -> {}"},
        "x");
    run({.what = "a skipped child stays reachable when the loop returns its array",
         .body = replace(replace(nonzeroChild, "[%one, %x]", "[%x, %one]"), "ctjs.return %result",
                         "ctjs.return %a"),
         .arrays = "a:[x,one]",
         .reads = "a[1]=one",
         .exit = "a -> {a,x}"});
    run({.what = "a start equal to length preserves the saved child without reading",
         .body = replace(replace(nonzeroChild, "binary sub %two, %one", "binary sub %two, %zero"),
                         "^header(%a, %start, %zero", "^header(%a, %start, %x"),
         .arrays = "a:[one,x]",
         .exit = "x -> {x}"});
    run({.what = "a start beyond an empty array keeps its original Number",
         .body = replace(replace(nonzeroStart, "[%one, %two, %three]", "[]"),
                         "^exit(%sum :", "^exit(%index :"),
         .arrays = "a:[]",
         .exit = "start -> {}"});
    makeUnit = "  %unit = ctjs.binary sub %two, %one {storage_test_id = \"unit\"}\n";
    const std::string unitLoop = replace(loop, "add %i, %one", "add %i, %unit");
    computedUnit = prefix + makeUnit + unitLoop;
    run({.what = "an independently computed Number one supplies an invariant unit step",
         .body = computedUnit,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    computedUnitChild =
        replace(replace(savedChild, "  cf.br ^header", makeUnit + "  cf.br ^header"),
                "add %i, %one", "add %i, %unit");
    run({.what = "held-unit induction retains its exact final child",
         .body = computedUnitChild,
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "held-unit induction discharges only unreturned children",
         .body = replace(computedUnitChild, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    savedUnit = prefix +
                "  %seed = ctjs.create_array [%one] {storage_test_id = \"seed\"}\n"
                "  %name = ctjs.constant #ctjs.string<\"length\">\n"
                "  %unit = ctjs.get_property %seed[%name]\n"
                "  ctjs.append %one to %seed\n" +
                unitLoop;
    run({.what = "a saved one-length step survives later growth of its source array",
         .body = savedUnit,
         .arrays = "a:[one,two,three]; seed:[one,one]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    carriedUnit = replace(
        computedUnit, "^header(%a, %zero, %zero : !ctjs.value, !ctjs.value, !ctjs.value)",
        "^header(%a, %zero, %zero, %unit : !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value)");
    carriedUnit =
        replace(carriedUnit, "%sum: !ctjs.value):", "%sum: !ctjs.value, %delta: !ctjs.value):");
    carriedUnit = replace(
        carriedUnit, "^body(%array, %index, %sum : !ctjs.value, !ctjs.value, !ctjs.value)",
        "^body(%array, %index, %sum, %delta : !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value)");
    carriedUnit = replace(carriedUnit, "%s: !ctjs.value):", "%s: !ctjs.value, %d: !ctjs.value):");
    carriedUnit = replace(carriedUnit, "add %i, %unit", "add %i, %d");
    carriedUnit = replace(
        carriedUnit, "^header(%base, %step, %added : !ctjs.value, !ctjs.value, !ctjs.value)",
        "^header(%base, %step, %added, %d : !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value)");
    run({.what = "held Number-one steps survive exact CFG header/body/backedge transport",
         .body = carriedUnit,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    run({.what = "a carried header step may pass through its backedge directly",
         .body = replace(carriedUnit, "^header(%base, %step, %added, %d",
                         "^header(%base, %step, %added, %delta"),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    alternateUnit = prefix + makeUnit +
                    "  %choice = ctjs.truthy %one\n"
                    "  cf.cond_br %choice, ^entry(%unit : !ctjs.value), "
                    "^entry(%one : !ctjs.value)\n"
                    "^entry(%chosen: !ctjs.value):\n" +
                    replace(unitLoop, "add %i, %unit", "add %i, %chosen");
    run({.what = "separate literal and computed unit steps survive predecessor transport",
         .body = alternateUnit,
         .arrays = "a:[one,two,three] | a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three; a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}; added -> {}"});
    stride = replace(original, "add %i, %one", "add %i, %two");
    run({.what = "positive strides replay only visited own indices and may overshoot length",
         .body = stride,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    run({.what = "a stride equal to length reaches the exact exit bound",
         .body = replace(original, "add %i, %one", "add %i, %three"),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one",
         .exit = "added -> {}"});
    run({.what = "computed positive Number strides retain their exact arithmetic result",
         .body = replace(computedUnit, "binary sub %two, %one", "binary sub %two, %zero"),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    run({.what = "a saved positive stride survives its source array shrinking",
         .body = replace(replace(savedUnit, "%seed = ctjs.create_array [%one]",
                                 "%seed = ctjs.create_array [%one, %one]"),
                         "ctjs.append %one to %seed", "ctjs.set_property %seed[%name], %one"),
         .arrays = "a:[one,two,three]; seed:[one]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    carriedStride = replace(carriedUnit, "binary sub %two, %one", "binary sub %three, %one");
    run({.what = "positive strides remain invariant across CFG header and body transport",
         .body = carriedStride,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    run({.what = "different positive predecessor strides retain separate visited indices",
         .body = replace(alternateUnit, "binary sub %two, %one", "binary sub %three, %one"),
         .arrays = "a:[one,two,three] | a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three; a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}; added -> {}"});
    computedStrideChild =
        replace(replace(computedUnitChild, "binary sub %two, %one", "binary sub %three, %one"),
                "[%one, %x]", "[%one, %two, %x]");
    run({.what = "a nonunit stride retains the identity of its final returned child",
         .body = computedStrideChild,
         .arrays = "a:[one,two,x]",
         .reads = "a[0]=one; a[2]=x",
         .exit = "x -> {x}"});
    run({.what = "skipped children are confined when no result retains their array",
         .body = replace(savedChild, "add %i, %one", "add %i, %two"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one",
         .exit = "one -> {}"},
        "x");
    run({.what = "the overshoot index survives transport and later array growth exactly",
         .body = replace(replace(stride, "^exit(%sum :", "^exit(%index :"), "  ctjs.return %result",
                         "  ctjs.append %zero to %a\n  ctjs.append %zero to %a\n"
                         "  %after = ctjs.get_property %a[%result]\n  ctjs.return %after"),
         .arrays = "a:[one,two,three,zero,zero]",
         .reads = "a[0]=one; a[2]=three; a[4]=zero",
         .exit = "zero -> {}"});
    maxStride = "  %max = ctjs.constant #ctjs.number<4751297606873776128>\n" +
                replace(original, "add %i, %one", "add %i, %max");
    run({.what = "the maximum bounded stride remains an exact Number at loop exit",
         .body =
             replace(replace(maxStride, "^exit(%sum :", "^exit(%index :"), "  ctjs.return %result",
                     "  %slot = ctjs.binary sub %result, %max\n"
                     "  %after = ctjs.get_property %a[%slot]\n  ctjs.return %after"),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[0]=one",
         .exit = "one -> {}"});
    run({.what = "a zero-trip loop never applies even the maximum bounded stride",
         .body = replace(maxStride, "[%one, %two, %three]", "[]"),
         .arrays = "a:[]",
         .exit = "zero -> {}"});
    nonzeroStride = replace(nonzeroStart, "add %i, %one", "add %i, %three");
    run({.what = "reversed strict guards preserve a nonzero start and stride overshoot",
         .body = replace(nonzeroStride, "compare lt %index, %length", "compare gt %length, %index"),
         .arrays = "a:[one,two,three]",
         .reads = "a[1]=two",
         .exit = "added -> {}"});
    run({.what = "a nonzero start computes its exact stride overshoot relative to that start",
         .body = replace(replace(nonzeroStride, "^exit(%sum :", "^exit(%index :"),
                         "  ctjs.return %result",
                         "  ctjs.append %zero to %a\n  ctjs.append %one to %a\n"
                         "  %after = ctjs.get_property %a[%result]\n  ctjs.return %after"),
         .arrays = "a:[one,two,three,zero,one]",
         .reads = "a[1]=two; a[4]=one",
         .exit = "one -> {}"});
    run({.what = "a nonzero start can update exactly to the largest bounded Number",
         .body = replace(
             replace(replace(replace(maxStride, "  cf.br ^header",
                                     "  %delta = ctjs.binary sub %max, %one\n  cf.br ^header"),
                             "^header(%a, %zero, %zero", "^header(%a, %one, %zero"),
                     "add %i, %max", "add %i, %delta"),
             "  ctjs.return %result",
             "  %slot = ctjs.binary sub %index, %max\n"
             "  %after = ctjs.get_property %a[%slot]\n  ctjs.return %after"),
         .arrays = "a:[one,two,three]",
         .reads = "a[1]=two; a[0]=one",
         .exit = "one -> {}"});
    run({.what = "the largest start takes no iteration and survives exit unchanged",
         .body = replace(replace(maxStride, "^header(%a, %zero, %zero", "^header(%a, %max, %zero"),
                         "  ctjs.return %result",
                         "  %slot = ctjs.binary sub %index, %max\n"
                         "  %after = ctjs.get_property %a[%slot]\n  ctjs.return %after"),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one",
         .exit = "one -> {}"});
    run({.what = "unrelated registers swap simultaneously on each backedge",
         .body = prefix + "  cf.br ^header(%a, %zero, %one, %two : !ctjs.value, !ctjs.value, "
                          "!ctjs.value, !ctjs.value)\n"
                          "^header(%array: !ctjs.value, %index: !ctjs.value, %left: !ctjs.value, "
                          "%right: !ctjs.value):\n"
                          "  %key = ctjs.constant #ctjs.string<\"length\">\n"
                          "  %length = ctjs.get_property %array[%key]\n"
                          "  %less = ctjs.compare lt %index, %length\n"
                          "  %flag = ctjs.truthy %less\n"
                          "  cf.cond_br %flag, ^body(%array, %index : !ctjs.value, !ctjs.value), "
                          "^exit(%left : !ctjs.value)\n"
                          "^body(%base: !ctjs.value, %i: !ctjs.value):\n"
                          "  %read = ctjs.get_property %base[%i]\n"
                          "  %step = ctjs.binary_static add %i, %one\n"
                          "  cf.br ^header(%base, %step, %right, %left : !ctjs.value, !ctjs.value, "
                          "!ctjs.value, !ctjs.value)\n"
                          "^exit(%result: !ctjs.value):\n"
                          "  ctjs.return %result\n",
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "two -> {}"});

    const auto directOverwrite = replace(overwritten, "set_property %base[", "set_property %a[");
    run({.what = "the guard allocation may also be the direct overwrite receiver",
         .body = directOverwrite,
         .arrays = "a:[zero,zero]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    const auto savedReceiver =
        replace(replace(overwritten, "  cf.br ^header",
                        "  %box = ctjs.create_array [%a] {storage_test_id = \"box\"}\n"
                        "  %alias = ctjs.get_property %box[%zero]\n  cf.br ^header"),
                "set_property %base[", "set_property %alias[");
    run({.what = "a saved receiver alias releases overwritten children",
         .body = replace(savedReceiver, "ctjs.return %result", "ctjs.return %a"),
         .arrays = "a:[zero,zero]; box:[a]",
         .reads = "box[0]=a; a[0]=one; a[1]=x",
         .exit = "a -> {a}"},
        "x");
    const auto reloadedReceiver =
        replace(replace(savedReceiver, "  %alias = ctjs.get_property %box[%zero]\n", ""),
                "  ctjs.set_property %alias[",
                "  %alias = ctjs.get_property %box[%zero]\n  ctjs.set_property %alias[");
    run({.what = "a disjoint receiver reload retains a previously read child",
         .body = reloadedReceiver,
         .arrays = "a:[zero,zero]; box:[a]",
         .reads = "a[0]=one; box[0]=a; a[1]=x; box[0]=a",
         .exit = "x -> {x}"},
        "a");
    const auto carriedReceiver = replace(
        replace(replace(replace(overwritten, "^header(%a, %zero, %zero", "^header(%a, %zero, %a"),
                        "set_property %base[", "set_property %s["),
                "^header(%base, %step, %read", "^header(%base, %step, %s"),
        "ctjs.return %result", "ctjs.return %a");
    run({.what = "an invariant transported receiver keeps its exact allocation",
         .body = carriedReceiver,
         .arrays = "a:[zero,zero]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "a -> {a}"},
        "x");
    reject("a changed receiver cannot borrow the initial array identity",
           replace(carriedReceiver, "^header(%base, %step, %s", "^header(%base, %step, %zero"));
    reject("a disjoint array cannot borrow the guard allocation's bound",
           replace(savedReceiver, "create_array [%a]", "create_array [%x]"));
    reject("receiver reloads cannot borrow the changing induction index",
           replace(reloadedReceiver, "%box[%zero]", "%box[%i]"));
    reject("receiver alias proof cannot read the overwritten allocation",
           replace(replace(reloadedReceiver, "  cf.br ^header",
                           "  ctjs.set_property %a[%zero], %a\n  cf.br ^header"),
                   "%alias = ctjs.get_property %box[%zero]",
                   "%alias = ctjs.get_property %base[%zero]"));
}

} // namespace ctcompile::test::escape::arrays::induction_detail
