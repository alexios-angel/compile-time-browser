#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::structured_detail {

void StructuredCases::reloads() {
    const auto masked =
        replace(replace(original, "ctjs.return %result", "ctjs.return %a"), "    %read =",
                "    %position = ctjs.binary_static bitand %i, %one\n"
                "    ctjs.set_property %base[%position], %zero\n    %read =");
    const auto remainder =
        replace(replace(masked, "  %a =",
                        "  %two = ctjs.constant #ctjs.number<4611686018427387904> "
                        "{storage_test_id = \"two\"}\n  %a ="),
                "ctjs.binary_static bitand %i, %one", "ctjs.binary mod %i, %two");
    rows.push_back({.what = "structured remainder writes preserve reordered loop transport",
                    .body = remainder,
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=zero; a[1]=zero",
                    .exit = "a -> {a}"});
    rows.push_back({.what = "structured remainder wraps retain unvisited children",
                    .body = replace(replace(remainder, "[%x]", "[%x, %y, %x, %y]"),
                                    "  ctjs.append %y to %a\n", ""),
                    .arrays = "a:[zero,zero,x,y]",
                    .reads = "a[0]=zero; a[1]=zero; a[2]=x; a[3]=y",
                    .exit = "a -> {a,x,y}"});
    const auto remainderReload =
        replace(replace(replace(remainder, "[%x]", "[%x, %y, %two, %zero]"),
                        "  ctjs.append %y to %a\n", ""),
                "%position = ctjs.binary mod %i, %two",
                "%divisor = ctjs.get_property %base[%two]\n"
                "    %position = ctjs.binary mod %i, %divisor");
    rows.push_back({.what = "structured remainder bounds preserve disjoint divisor reloads",
                    .body = remainderReload,
                    .arrays = "a:[zero,zero,two,zero]",
                    .reads = "a[2]=two; a[0]=zero; a[2]=two; a[1]=zero; a[2]=two; a[2]=two; "
                             "a[2]=two; a[3]=zero",
                    .exit = "a -> {a}"});
    reject("structured remainder rejects overlapping divisor reloads",
           replace(replace(remainderReload, "[%x, %y, %two, %zero]", "[%two, %y, %zero, %zero]"),
                   "%divisor = ctjs.get_property %base[%two]",
                   "%divisor = ctjs.get_property %base[%zero]"));
    reject("later structured stores invalidate earlier remainder divisors",
           replace(remainderReload,
                   "    %step =", "    ctjs.set_property %base[%two], %zero\n    %step ="));
    for (const auto & operands : {"%i, %zero", "%i, %i", "%two, %i"}) {
        reject("structured remainder requires a nonzero invariant divisor without commutation",
               replace(remainder, "mod %i, %two", "mod " + std::string(operands)));
    }
    const auto signedRemainder = replace(remainder, "%position = ctjs.binary mod %i, %two",
                                         "%negative = ctjs.unary neg %i\n"
                                         "    %part = ctjs.binary mod %negative, %two\n"
                                         "    %position = ctjs.binary add %part, %one");
    rows.push_back({.what = "structured signed remainders keep exact descending visits",
                    .body = signedRemainder,
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=x; a[1]=zero",
                    .exit = "a -> {a}"});
    rows.push_back({.what = "structured negative divisors preserve the dividend sign",
                    .body = replace(signedRemainder, "%part = ctjs.binary mod %negative, %two",
                                    "%divisor = ctjs.unary neg %two\n"
                                    "    %part = ctjs.binary mod %negative, %divisor"),
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=x; a[1]=zero",
                    .exit = "a -> {a}"});
    rows.push_back({.what = "structured signed zero remainders preserve unvisited children",
                    .body = replace(signedRemainder, "mod %negative, %two", "mod %negative, %one"),
                    .arrays = "a:[x,zero]",
                    .reads = "a[0]=x; a[1]=zero",
                    .exit = "a -> {a,x}"});
    const auto crossingRemainder =
        replace(replace(replace(signedRemainder, "[%x]", "[%x, %y, %x, %zero]"),
                        "  ctjs.append %y to %a\n", ""),
                "ctjs.unary neg %i", "ctjs.binary sub %i, %one");
    rows.push_back({.what = "structured remainder bounds cross zero without losing extrema",
                    .body = crossingRemainder,
                    .arrays = "a:[zero,zero,zero,zero]",
                    .reads = "a[0]=zero; a[1]=zero; a[2]=zero; a[3]=zero",
                    .exit = "a -> {a}"});
    const auto signedRemainderReload =
        replace(remainderReload, "%position = ctjs.binary mod %i, %divisor",
                "%negative = ctjs.unary neg %i\n"
                "    %part = ctjs.binary mod %negative, %divisor\n"
                "    %position = ctjs.binary add %part, %one");
    rows.push_back({.what = "structured signed remainders preserve disjoint reloads",
                    .body = signedRemainderReload,
                    .arrays = "a:[zero,zero,two,zero]",
                    .reads = "a[2]=two; a[0]=x; a[2]=two; a[1]=zero; a[2]=two; a[2]=two; "
                             "a[2]=two; a[3]=zero",
                    .exit = "a -> {a}"});
    reject("later structured writes invalidate signed remainder reloads",
           replace(signedRemainderReload,
                   "    %step =", "    ctjs.set_property %base[%two], %zero\n    %step ="));
    reject("structured signed remainders reject interior divisor reloads",
           replace(replace(crossingRemainder, "[%x, %y, %x, %zero]", "[%x, %two, %y, %zero]"),
                   "%part = ctjs.binary mod %negative, %two",
                   "%divisor = ctjs.get_property %base[%one]\n"
                   "    %part = ctjs.binary mod %negative, %divisor"));
    reject("structured signed remainders still require nonnegative final keys",
           replace(signedRemainder, "ctjs.binary add %part, %one", "ctjs.unary plus %part"));
    const auto remainderBand = replace(remainder, "%position = ctjs.binary mod %i, %two",
                                       "%three = ctjs.binary add %two, %one\n"
                                       "    %four = ctjs.binary add %two, %two\n"
                                       "    %offset = ctjs.binary add %i, %four\n"
                                       "    %part = ctjs.binary mod %offset, %three\n"
                                       "    %position = ctjs.binary sub %part, %one");
    rows.push_back({.what = "structured remainder bands preserve translated endpoints",
                    .body = remainderBand,
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=zero; a[1]=zero",
                    .exit = "a -> {a}"});
    rows.push_back({.what = "structured negative remainder bands preserve descending visits",
                    .body = replace(replace(remainderBand, "%offset = ctjs.binary add %i, %four",
                                            "%negative = ctjs.unary neg %i\n"
                                            "    %offset = ctjs.binary sub %negative, %four"),
                                    "ctjs.binary sub %part, %one", "ctjs.binary add %part, %two"),
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=x; a[1]=zero",
                    .exit = "a -> {a}"});
    const auto remainderBandReload =
        replace(replace(replace(replace(replace(replace(remainder, "  %a =",
                                                        "  %eight = ctjs.constant "
                                                        "#ctjs.number<4620693217682128896> "
                                                        "{storage_test_id = \"eight\"}\n  %a ="),
                                                "[%x]", "[%one, %x, %eight, %y, %one]"),
                                        "  ctjs.append %y to %a\n", ""),
                                "%index = %zero", "%index = %one"),
                        "add %i, %one", "add %i, %two"),
                "%position = ctjs.binary mod %i, %two",
                "%divisor = ctjs.get_property %base[%two]\n"
                "    %offset = ctjs.binary add %i, %eight\n"
                "    %position = ctjs.binary mod %offset, %divisor");
    rows.push_back({.what = "structured remainder bands retain divisor reloads in stride gaps",
                    .body = remainderBandReload,
                    .arrays = "a:[one,zero,eight,zero,one]",
                    .reads = "a[2]=eight; a[1]=zero; a[2]=eight; a[3]=zero",
                    .exit = "a -> {a}"});
    reject("later structured writes invalidate divisors in remainder stride gaps",
           replace(remainderBandReload,
                   "    %step =", "    ctjs.set_property %base[%two], %zero\n    %step ="));
    reject("structured remainder bands exclude divisor reloads at visited lattice points",
           replace(replace(remainderBandReload, "[%one, %x, %eight, %y, %one]",
                           "[%one, %eight, %one, %y, %one]"),
                   "%divisor = ctjs.get_property %base[%two]",
                   "%divisor = ctjs.get_property %base[%one]"));
    reject("structured remainder wraps cannot use narrow quotient-band bounds",
           replace(remainderBand, "add %i, %four", "add %i, %two"));
    const auto remainderWrapReload =
        replace(replace(replace(replace(replace(remainder, "  %a =",
                                                "  %four = ctjs.constant "
                                                "#ctjs.number<4616189618054758400> "
                                                "{storage_test_id = \"four\"}\n  %a ="),
                                        "[%x]", "[%x, %four, %y, %zero, %zero]"),
                                "  ctjs.append %y to %a\n", ""),
                        "add %i, %one", "add %i, %two"),
                "%position = ctjs.binary mod %i, %two",
                "%divisor = ctjs.get_property %base[%one]\n"
                "    %position = ctjs.binary mod %i, %divisor");
    for (const auto & body :
         {remainderWrapReload,
          replace(remainderWrapReload, "%position = ctjs.binary mod %i, %divisor",
                  "%negative = ctjs.unary neg %i\n"
                  "    %part = ctjs.binary mod %negative, %divisor\n"
                  "    %position = ctjs.unary neg %part")}) {
        rows.push_back({.what = "structured remainder wraps retain invariant gap reloads",
                        .body = body,
                        .arrays = "a:[zero,four,zero,zero,zero]",
                        .reads = "a[1]=four; a[0]=zero; a[1]=four; a[2]=zero; "
                                 "a[1]=four; a[4]=zero",
                        .exit = "a -> {a}"});
    }
    rows.push_back({.what = "structured wrap congruences preserve positive and negative remainders",
                    .body = replace(replace(remainderWrapReload, "[%x, %four, %y, %zero, %zero]",
                                            "[%x, %four, %y, %zero, %x, %zero, %zero]"),
                                    "%position = ctjs.binary mod %i, %divisor",
                                    "%signed = ctjs.binary sub %i, %two\n"
                                    "    %part = ctjs.binary mod %signed, %divisor\n"
                                    "    %position = ctjs.binary add %part, %two"),
                    .arrays = "a:[zero,four,zero,zero,zero,zero,zero]",
                    .reads = "a[1]=four; a[0]=zero; a[1]=four; a[2]=zero; a[1]=four; a[4]=zero; "
                             "a[1]=four; a[6]=zero",
                    .exit = "a -> {a}"});
    rows.push_back({.what = "structured remainder congruences retain unvisited gap identities",
                    .body = replace(remainderWrapReload, "[%x, %four, %y, %zero, %zero]",
                                    "[%x, %four, %y, %y, %zero]"),
                    .arrays = "a:[zero,four,zero,y,zero]",
                    .reads = "a[1]=four; a[0]=zero; a[1]=four; a[2]=zero; "
                             "a[1]=four; a[4]=zero",
                    .exit = "a -> {a,y}"});
    reject("later structured stores invalidate a divisor in a remainder congruence gap",
           replace(remainderWrapReload,
                   "    %step =", "    ctjs.set_property %base[%one], %zero\n    %step ="));
    reject("structured remainder congruences reject a visited divisor residue",
           replace(replace(remainderWrapReload, "[%x, %four, %y, %zero, %zero]",
                           "[%x, %zero, %four, %zero, %zero]"),
                   "%divisor = ctjs.get_property %base[%one]",
                   "%divisor = ctjs.get_property %base[%two]"));
    for (const auto & expression :
         {"ctjs.binary_static bitand %i, %one", "ctjs.binary_static bitand %one, %i"}) {
        rows.push_back({.what = "structured masked writes release exactly visited children",
                        .body = replace(masked, "ctjs.binary_static bitand %i, %one", expression),
                        .arrays = "a:[zero,zero]",
                        .reads = "a[0]=zero; a[1]=zero",
                        .exit = "a -> {a}"});
    }
    const auto gaps = replace(
        replace(replace(masked, "[%x]", "[%x, %y, %x, %y]"), "  ctjs.append %y to %a\n", ""),
        "%position = ctjs.binary_static bitand %i, %one",
        "%mask = ctjs.binary add %one, %one\n"
        "    %position = ctjs.binary_static bitand %i, %mask");
    rows.push_back({.what = "structured mask enclosures retain children in unvisited gaps",
                    .body = gaps,
                    .arrays = "a:[zero,y,zero,y]",
                    .reads = "a[0]=zero; a[1]=y; a[2]=zero; a[3]=y",
                    .exit = "a -> {a,y}"});
    rows.push_back({.what = "structured zero masks preserve the unvisited final child",
                    .body = replace(masked, "bitand %i, %one", "bitand %i, %zero"),
                    .arrays = "a:[zero,y]",
                    .reads = "a[0]=zero; a[1]=y",
                    .exit = "a -> {a,y}"});
    reject("structured masks cannot borrow a varying operand",
           replace(masked, "bitand %i, %one", "bitand %i, %i"));
    const auto fixedZeroAnd = replace(masked, "%position = ctjs.binary_static bitand %i, %one",
                                      "%mask = ctjs.binary add %one, %one\n"
                                      "    %position = ctjs.binary_static bitand %i, %mask");
    rows.push_back({.what = "structured AND bounds preserve fixed zero bits above the input",
                    .body = fixedZeroAnd,
                    .arrays = "a:[zero,y]",
                    .reads = "a[0]=zero; a[1]=y",
                    .exit = "a -> {a,y}"});
    reject("structured masks must bound every possible own position",
           replace(fixedZeroAnd, "%position = ctjs.binary_static bitand %i, %mask",
                   "%outside = ctjs.binary add %i, %mask\n"
                   "    %position = ctjs.binary_static bitand %outside, %mask"));
    const auto fixedAnd =
        replace(replace(masked, "  %a =",
                        "  %two = ctjs.binary add %one, %one\n"
                        "  %eight = ctjs.constant #ctjs.number<4620693217682128896>\n"
                        "  %fixedMask = ctjs.constant #ctjs.number<4624633867356078080> "
                        "{storage_test_id = \"mask\"}\n  %a ="),
                "%position = ctjs.binary_static bitand %i, %one",
                "%biased = ctjs.binary sub %i, %eight\n"
                "    %part = ctjs.binary_static bitand %biased, %fixedMask\n"
                "    %position = ctjs.binary sub %part, %eight");
    for (const auto & operands : {"%biased, %fixedMask", "%fixedMask, %biased"}) {
        rows.push_back(
            {.what = "structured AND fixed upper bits survive negative Number conversion",
             .body =
                 replace(fixedAnd, "bitand %biased, %fixedMask", "bitand " + std::string(operands)),
             .arrays = "a:[zero,zero]",
             .reads = "a[0]=zero; a[1]=zero",
             .exit = "a -> {a}"});
    }
    rows.push_back({.what = "structured AND fixed upper bits bound positive translated indices",
                    .body = replace(fixedAnd, "sub %i, %eight", "add %i, %eight"),
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=zero; a[1]=zero",
                    .exit = "a -> {a}"});
    const auto fixedAndReload = replace(
        replace(replace(replace(replace(fixedAnd, "[%x]", "[%fixedMask, %zero, %x, %y]"),
                                "  ctjs.append %y to %a\n", ""),
                        "%index = %zero", "%index = %two"),
                "    %biased =", "    %mask = ctjs.get_property %base[%zero]\n    %biased ="),
        "bitand %biased, %fixedMask", "bitand %biased, %mask");
    rows.push_back({.what = "structured AND fixed upper bits exclude earlier reload slots",
                    .body = fixedAndReload,
                    .arrays = "a:[mask,zero,zero,zero]",
                    .reads = "a[0]=mask; a[2]=zero; a[0]=mask; a[3]=zero",
                    .exit = "a -> {a}"});
    rows.push_back(
        {.what = "structured AND fixed-bit bounds retain unwritten children",
         .body = replace(fixedAndReload, "[%fixedMask, %zero, %x, %y]", "[%fixedMask, %x, %x, %y]"),
         .arrays = "a:[mask,x,zero,zero]",
         .reads = "a[0]=mask; a[2]=zero; a[0]=mask; a[3]=zero",
         .exit = "a -> {a,x}"});
    reject("structured AND fixed-bit bounds reject a reload at a written index",
           replace(replace(fixedAndReload, "[%fixedMask, %zero, %x, %y]",
                           "[%zero, %zero, %fixedMask, %y]"),
                   "%mask = ctjs.get_property %base[%zero]",
                   "%mask = ctjs.get_property %base[%two]"));
    reject("later structured stores invalidate fixed-bit AND reload evidence",
           replace(fixedAndReload,
                   "    %step =", "    ctjs.set_property %base[%zero], %zero\n    %step ="));
    reject("structured AND still requires the translated own bound after clearing a fixed bit",
           replace(fixedAnd, "4624633867356078080", "4619567317775286272"));
    for (const std::string kind : {"bitor", "bitxor"}) {
        const bool isOr = kind == "bitor";
        const auto bitwise = replace(masked, "bitand", kind);
        for (const auto & operands : {"%i, %one", "%one, %i"}) {
            rows.push_back({.what = "structured OR/XOR operands preserve exact overwritten aliases",
                            .body = replace(bitwise, "%i, %one", operands),
                            .arrays = isOr ? "a:[x,zero]" : "a:[zero,zero]",
                            .reads = "a[0]=x; a[1]=zero",
                            .exit = isOr ? "a -> {a,x}" : "a -> {a}"});
        }
        rows.push_back({.what = "structured OR/XOR enclosures preserve unvisited gap children",
                        .body = replace(replace(bitwise, "[%x]", "[%x, %y, %x, %y]"),
                                        "  ctjs.append %y to %a\n", ""),
                        .arrays = isOr ? "a:[x,zero,x,zero]" : "a:[zero,zero,zero,zero]",
                        .reads = "a[0]=x; a[1]=zero; a[2]=x; a[3]=zero",
                        .exit = isOr ? "a -> {a,x}" : "a -> {a}"});
        const auto gapReload =
            replace(replace(replace(replace(replace(bitwise, "[%x]", "[%zero, %x, %one, %y]"),
                                            "  ctjs.append %y to %a\n", ""),
                                    "  %a =", "  %two = ctjs.binary add %one, %one\n  %a ="),
                            "add %i, %one", "add %i, %two"),
                    "%position = ctjs.binary_static " + kind + " %i, %one",
                    "%mask = ctjs.get_property %base[%two]\n"
                    "    %position = ctjs.binary_static " +
                        kind + " %i, %mask");
        for (const auto & operands : {"%i, %mask", "%mask, %i"}) {
            rows.push_back(
                {.what = "structured OR/XOR low bits preserve reloads inside the output interval",
                 .body = replace(gapReload, kind + " %i, %mask", kind + " " + operands),
                 .arrays = "a:[zero,zero,one,zero]",
                 .reads = "a[2]=one; a[0]=zero; a[2]=one; a[2]=one",
                 .exit = "a -> {a}"});
        }
        rows.push_back({.what = "structured OR/XOR low-bit lattices retain unvisited gap children",
                        .body = replace(gapReload, "[%zero, %x, %one, %y]",
                                        "[%zero, %x, %one, %x, %y, %zero, %zero, %zero]"),
                        .arrays = "a:[zero,zero,one,zero,y,zero,zero,zero]",
                        .reads = "a[2]=one; a[0]=zero; a[2]=one; a[2]=one; "
                                 "a[2]=one; a[4]=y; a[2]=one; a[6]=zero",
                        .exit = "a -> {a,y}"});
        const auto three =
            replace(gapReload, "  %a =", "  %three = ctjs.binary add %two, %one\n  %a =");
        reject("structured OR/XOR low-bit lattices reject a visited reload residue",
               replace(replace(three, "[%zero, %x, %one, %y]", "[%zero, %x, %zero, %one]"),
                       "%mask = ctjs.get_property %base[%two]",
                       "%mask = ctjs.get_property %base[%three]"));
        reject("later structured stores invalidate a reload in an OR/XOR low-bit gap",
               replace(gapReload,
                       "    %step =", "    ctjs.set_property %base[%two], %zero\n    %step ="));
        const auto oddStride = replace(three, "add %i, %two", "add %i, %three");
        if (isOr) {
            rows.push_back({.what = "structured OR mask bits preserve the original odd stride",
                            .body = oddStride,
                            .arrays = "a:[zero,zero,one,zero]",
                            .reads = "a[2]=one; a[0]=zero; a[2]=one; a[3]=zero",
                            .exit = "a -> {a}"});
            for (const auto & operands : {"%i, %mask", "%mask, %i"}) {
                rows.push_back(
                    {.what = "structured OR mask bits preserve unit-stride reload gaps",
                     .body = replace(replace(gapReload, "add %i, %two", "add %i, %one"),
                                     "bitor %i, %mask", "bitor " + std::string(operands)),
                     .arrays = "a:[zero,zero,one,zero]",
                     .reads = "a[2]=one; a[0]=zero; a[2]=one; a[1]=zero; a[2]=one; a[2]=one; "
                              "a[2]=one; a[3]=zero",
                     .exit = "a -> {a}"});
            }
        } else {
            reject("structured odd input strides cannot retain an XOR low-bit gap", oddStride);
        }
        reject("structured OR/XOR reject an enclosure beyond the guard allocation",
               replace(bitwise, "[%x]", "[%x, %y]"));
        reject("structured OR/XOR require an invariant mask",
               replace(bitwise, "%i, %one", "%i, %i"));
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
            replace(
                replace(replace(replace(masked, "  ctjs.append %y to %a\n", ""), "[%x]", contents),
                        "  %a =",
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
                "    %bits = ctjs.binary_static " +
                kind +
                " %i, %mask\n"
                "    %position = ctjs.unary plus %bits");
        for (const auto & operands : {"%i, %mask", "%mask, %i"}) {
            rows.push_back(
                {.what = "interleaved fixed input and mask bits widen the output period",
                 .body = replace(combinedBits, kind + " %i, %mask", kind + " " + operands),
                 .arrays = values,
                 .reads = reads,
                 .exit = "a -> {a}"});
        }
        rows.push_back({.what = "a composed mask period retains an odd factor in the input stride",
                        .body = replace(combinedBits, "add %i, %periodTwo", "add %i, %periodSix"),
                        .arrays = values,
                        .reads = isAnd ? "a[2]=mask; a[0]=zero; a[2]=mask; a[6]=zero"
                                       : "a[4]=mask; a[0]=zero; a[4]=mask; a[6]=zero",
                        .exit = "a -> {a}"});
        for (const auto & bias : {"%periodEight", "%periodSign"}) {
            auto body = replace(combinedBits, "%bits = ctjs.binary_static " + kind + " %i, %mask",
                                "%biased = ctjs.binary sub %i, " + std::string(bias) +
                                    "\n"
                                    "    %bits = ctjs.binary_static " +
                                    kind + " %biased, %mask");
            if (!isAnd) {
                body = replace(body, "ctjs.unary plus %bits",
                               "ctjs.binary add %bits, " + std::string(bias));
            }
            rows.push_back({.what = "combined mask periods survive signed input conversion",
                            .body = body,
                            .arrays = values,
                            .reads = reads,
                            .exit = "a -> {a}"});
        }
        rows.push_back(
            {.what = "combined mask periods preserve unvisited child identities",
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
                         "    %bits = ctjs.binary_static " +
                             kind + " %biased, %mask");
        rows.push_back({.what = "combined mask periods retain the exact nonzero low-bit residue",
                        .body = phased,
                        .arrays = isAnd ? "a:[zero,zero,zero,mask,zero,zero,zero,zero]"
                                        : "a:[zero,zero,zero,zero,zero,mask,zero,zero]",
                        .reads = isAnd ? "a[3]=mask; a[0]=zero; a[3]=mask; a[2]=zero; a[3]=mask; "
                                         "a[4]=zero; a[3]=mask; a[6]=zero"
                                       : "a[5]=mask; a[0]=zero; a[5]=mask; a[2]=zero; a[5]=mask; "
                                         "a[4]=zero; a[5]=mask; a[6]=zero",
                        .exit = "a -> {a}"});
        const auto saved =
            replace(replace(combinedBits, "  %finalIndex,",
                            "  %periodSaved = ctjs.get_property %a[" +
                                std::string(isAnd ? "%zero" : "%periodTwo") + "]\n  %finalIndex,"),
                    "ctjs.return %a", "ctjs.return %periodSaved");
        rows.push_back(
            {.what = "combined periods retain snapshots read before masked writes",
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
               replace(combinedBits, "    %step =",
                       "    ctjs.set_property %base[" + guard + "], %zero\n    %step ="));
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
            replace(replace(replace(masked, "  ctjs.append %y to %a\n", ""), "[%x]", contents),
                    "  %a =",
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
                "    %bits = ctjs.binary_static " +
                kind +
                " %i, %mask\n"
                "    %position = ctjs.unary plus %bits");
        for (const auto & operands : {"%i, %mask", "%mask, %i"}) {
            rows.push_back({.what = "low-bit rounding bounds stop at the last actual write",
                            .body = replace(rounded, kind + " %i, %mask", kind + " " + operands),
                            .arrays = values,
                            .reads = reads,
                            .exit = "a -> {a}"});
        }
        for (const auto & expression : {"%part = ctjs.binary sub %i, %roundTwo\n"
                                        "    %bits = ctjs.binary_static " +
                                            kind +
                                            " %part, %mask\n"
                                            "    %position = ctjs.binary add %bits, %roundTwo",
                                        "%part = ctjs.binary add %roundSign, %i\n"
                                        "    %bits = ctjs.binary_static " +
                                            kind +
                                            " %part, %mask\n"
                                            "    %position = ctjs.binary add %bits, %roundSign",
                                        "%part = ctjs.binary add %roundLow, %i\n"
                                        "    %bits = ctjs.binary_static " +
                                            kind +
                                            " %part, %mask\n"
                                            "    %position = ctjs.binary sub %bits, %roundTwo"}) {
            rows.push_back(
                {.what = "low-bit rounding stays monotone within each signed conversion band",
                 .body = replace(rounded,
                                 "%bits = ctjs.binary_static " + kind +
                                     " %i, %mask\n"
                                     "    %position = ctjs.unary plus %bits",
                                 expression),
                 .arrays = values,
                 .reads = reads,
                 .exit = "a -> {a}"});
        }
        rows.push_back(
            {.what = "low-bit rounding retains children outside the actual writes",
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
            replace(replace(rounded, "  %finalIndex,",
                            "  %roundSaved = ctjs.get_property %a[" +
                                std::string(isAnd ? "%zero" : "%one") + "]\n  %finalIndex,"),
                    "ctjs.return %a", "ctjs.return %roundSaved");
        rows.push_back(
            {.what = "low-bit rounding preserves snapshots read before overwrite",
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
               replace(rounded, "    %step =",
                       "    ctjs.set_property %base[" + guard + "], %zero\n    %step ="));
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
                    "%index = %zero", "%index = %one"),
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
                                        "    %bits = ctjs.binary_static " +
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
                "    %position = ctjs.unary plus %bits",
            "%part = ctjs.binary add %i, %roundEight\n"
            "    %bits = ctjs.binary_static " +
                kind +
                " %part, %mask\n"
                "    %position = " +
                (isAnd ? "ctjs.unary plus %bits" : "ctjs.binary sub %bits, %roundEight"));
        for (const auto & operands : {"%part, %mask", "%mask, %part"}) {
            rows.push_back(
                {.what = "fixed upper bits retain exact low-suffix rounding endpoints",
                 .body = replace(fixedRounding, kind + " %part, %mask", kind + " " + operands),
                 .arrays = values,
                 .reads = reads,
                 .exit = "a -> {a}"});
        }
        auto negativeFixedRounding = replace(fixedRounding, "ctjs.binary add %i, %roundEight",
                                             "ctjs.binary sub %i, %roundEight");
        if (!isAnd) {
            negativeFixedRounding =
                replace(negativeFixedRounding, "ctjs.binary sub %bits, %roundEight",
                        "ctjs.binary add %bits, %roundEight");
        }
        rows.push_back({.what = "negative fixed upper bits retain exact rounding endpoints",
                        .body = negativeFixedRounding,
                        .arrays = values,
                        .reads = reads,
                        .exit = "a -> {a}"});
        rows.push_back(
            {.what = "fixed-bit rounding leaves unwritten children reachable",
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
        rows.push_back(
            {.what = "fixed low input bits complete a monotone rounding suffix",
             .body = lowRounding,
             .arrays = isAnd ? "a:[zero,zero,zero,zero,zero,mask,zero,zero,zero,zero,zero,zero]"
                             : "a:[mask,zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,zero]",
             .reads = isAnd ? "a[5]=mask; a[0]=zero; a[5]=mask; a[2]=zero; a[5]=mask; a[4]=zero; "
                              "a[5]=mask; a[6]=zero; a[5]=mask; a[8]=zero; a[5]=mask; a[10]=zero"
                            : "a[0]=mask; a[0]=mask; a[0]=mask; a[2]=zero; a[0]=mask; a[4]=zero; "
                              "a[0]=mask; a[6]=zero; a[0]=mask; a[8]=zero; a[0]=mask; a[10]=zero",
             .exit = "a -> {a}"});
        reject("fixed-bit rounding still checks overlapping mask reloads",
               replace(replace(fixedRounding, contents,
                               isAnd ? "[%x, %zero, %roundMask, %zero, %x, %zero]"
                                     : "[%zero, %x, %zero, %roundMask, %zero, %x]"),
                       "%base[" + guard + "]", isAnd ? "%base[%roundTwo]" : "%base[%roundThree]"));
        reject("fixed-bit rounding retains later writes in its complete census",
               replace(fixedRounding, "    %step =",
                       "    ctjs.set_property %base[" + guard + "], %zero\n    %step ="));
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
        const auto pair = replace(
            replace(replace(replace(masked, "  ctjs.append %y to %a\n", ""), "[%x]", contents),
                    "  %a =",
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
                "    %pairBits = ctjs.binary_static " +
                kind +
                " %i, %pairReload\n"
                "    %position = ctjs.unary plus %pairBits");
        const auto twoPoints = replace(pair, "add %i, %one", "add %i, %pairFive");
        for (const auto & operands : {"%i, %pairReload", "%pairReload, %i"}) {
            rows.push_back(
                {.what = "two-point bitwise images retain exact bounds and gaps",
                 .body = replace(twoPoints, kind + " %i, %pairReload", kind + " " + operands),
                 .arrays = values,
                 .reads = reads,
                 .exit = "a -> {a}"});
        }
        rows.push_back(
            {.what = "negative two-point bitwise images retain exact signed order",
             .body = replace(
                 replace(twoPoints, "%pairBits = ctjs.binary_static " + kind + " %i, %pairReload",
                         "%pairInput = ctjs.binary sub %i, %pairEight\n"
                         "    %pairBits = ctjs.binary_static " +
                             kind + " %pairInput, %pairReload"),
                 "ctjs.unary plus %pairBits",
                 isAnd ? "ctjs.unary plus %pairBits" : "ctjs.binary add %pairBits, %pairEight"),
             .arrays = values,
             .reads = reads,
             .exit = "a -> {a}"});
        rows.push_back(
            {.what = "two-point gaps retain children outside actual writes",
             .body = replace(twoPoints, contents,
                             isAnd ? "[%x, %x, %pairMask, %x, %zero, %zero]"
                                   : "[%zero, %x, %x, %zero, %pairMask, %zero, %zero, %x]"),
             .arrays = isAnd ? "a:[zero,zero,mask,x,zero,zero]"
                             : "a:[zero,x,zero,zero,mask,zero,zero,zero]",
             .reads = reads,
             .exit = "a -> {a,x}"});
        const auto saved = replace(replace(twoPoints, "  %finalIndex,",
                                           "  %pairSaved = ctjs.get_property %a[" +
                                               std::string(isAnd ? "%zero" : "%pairTwo") +
                                               "]\n"
                                               "  %finalIndex,"),
                                   "ctjs.return %a", "ctjs.return %pairSaved");
        rows.push_back({.what = "two-point writes preserve a previously saved child",
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
               replace(twoPoints, "    %step =",
                       "    ctjs.set_property %base[" + guard + "], %zero\n    %step ="));
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
                                        "\n    %pairBits = ctjs.binary_static " + kind +
                                        " %pairInput, %pairReload");
            crossing = replace(crossing, "ctjs.unary plus %pairBits",
                               "ctjs.binary_static bitand %pairBits, %pairSeven");
            rows.push_back(
                {.what = "two-point masks compose across either signed conversion jump",
                 .body = crossing,
                 .arrays = isAnd ? (upper ? "a:[mask,zero,zero,zero,zero,zero]"
                                          : "a:[zero,zero,mask,zero,zero,zero]")
                                 : (!isOr && upper ? "a:[zero,zero,mask,zero,zero,zero,zero,zero]"
                                                   : "a:[zero,zero,zero,zero,mask,zero,zero,zero]"),
                 .reads = isAnd ? (upper ? "a[0]=mask; a[0]=mask; a[0]=mask; a[5]=zero"
                                         : "a[2]=mask; a[0]=x; a[2]=mask; a[5]=zero")
                                : (!isOr && upper ? "a[2]=mask; a[0]=zero; a[2]=mask; a[5]=zero"
                                                  : "a[4]=mask; a[0]=zero; a[4]=mask; a[5]=zero"),
                 .exit = "a -> {a}"});
        }
        if (isAnd) {
            rows.push_back(
                {.what = "coincident two-point images keep a positive enclosing stride",
                 .body = replace(
                     replace(twoPoints, contents, "[%x, %zero, %pairMask, %zero, %zero, %zero]"),
                     "#ctjs.number<4613937818241073152> {storage_test_id = \"mask\"}",
                     "#ctjs.number<4611686018427387904> {storage_test_id = \"mask\"}"),
                 .arrays = values,
                 .reads = reads,
                 .exit = "a -> {a}"});
        }
    }
    const auto signedBits = replace(masked, "  %a =",
                                    "  %maximum = ctjs.constant #ctjs.number<4746794007244308480>\n"
                                    "  %sign = ctjs.constant #ctjs.number<4746794007248502784>\n"
                                    "  %negativeSign = ctjs.unary neg %sign\n"
                                    "  %negativeOne = ctjs.unary neg %one\n"
                                    "  %low = ctjs.constant #ctjs.number<13974669643726454784>\n"
                                    "  %two = ctjs.binary add %one, %one\n"
                                    "  %a =");
    for (const std::string kind : {"bitor", "bitxor", "bitand"}) {
        const auto literal = kind == "bitand" ? "13830554455654793216" : "0";
        const auto identity = replace(
            replace(replace(replace(replace(signedBits, "[%x]", "[%x, %identity, %zero, %y]"),
                                    "  ctjs.append %y to %a\n", ""),
                            "add %i, %one", "add %i, %three"),
                    "  %a =",
                    "  %identity = ctjs.constant #ctjs.number<" + std::string(literal) +
                        "> {storage_test_id = \"mask\"}\n"
                        "  %three = ctjs.binary add %two, %one\n  %a ="),
            "%position = ctjs.binary_static bitand %i, %one",
            "%mask = ctjs.get_property %base[%one]\n"
            "    %position = ctjs.binary_static " +
                kind + " %i, %mask");
        for (const auto & operands : {"%i, %mask", "%mask, %i"}) {
            rows.push_back(
                {.what = "structured identity masks preserve odd strides in either operand order",
                 .body = replace(identity, kind + " %i, %mask", kind + " " + operands),
                 .arrays = "a:[zero,mask,zero,zero]",
                 .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[3]=zero",
                 .exit = "a -> {a}"});
        }
        rows.push_back(
            {.what = "structured identity replay retains an unvisited gap child",
             .body = replace(identity, "[%x, %identity, %zero, %y]", "[%x, %identity, %x, %y]"),
             .arrays = "a:[zero,mask,x,zero]",
             .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[3]=zero",
             .exit = "a -> {a,x}"});
        for (const auto & expression : {"%part = ctjs.binary sub %i, %one\n"
                                        "    %bits = ctjs.binary_static " +
                                            kind +
                                            " %part, %mask\n"
                                            "    %position = ctjs.binary add %bits, %one",
                                        "%part = ctjs.binary add %sign, %i\n"
                                        "    %bits = ctjs.binary_static " +
                                            kind +
                                            " %part, %mask\n"
                                            "    %position = ctjs.binary add %bits, %sign",
                                        "%part = ctjs.binary add %low, %i\n"
                                        "    %bits = ctjs.binary_static " +
                                            kind +
                                            " %part, %mask\n"
                                            "    %position = ctjs.binary sub %bits, %two"}) {
            rows.push_back(
                {.what = "structured identity masks preserve each affine conversion band",
                 .body = replace(identity, "%position = ctjs.binary_static " + kind + " %i, %mask",
                                 expression),
                 .arrays = "a:[zero,mask,zero,zero]",
                 .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[3]=zero",
                 .exit = "a -> {a}"});
        }
        reject(
            "structured identity masks cannot reload at a visited lattice point",
            replace(replace(identity, "[%x, %identity, %zero, %y]", "[%identity, %x, %zero, %y]"),
                    "%mask = ctjs.get_property %base[%one]",
                    "%mask = ctjs.get_property %base[%zero]"));
        reject("structured identity masks retain the complete later-store census",
               replace(identity,
                       "    %step =", "    ctjs.set_property %base[%one], %zero\n    %step ="));
        reject("structured nonidentity masks cannot borrow an odd input lattice",
               replace(identity,
                       "#ctjs.number<" + std::string(literal) + "> {storage_test_id = \"mask\"}",
                       "#ctjs.number<" +
                           std::string(kind == "bitand" ? "13837309855095848960"
                                                        : "4607182418800017408") +
                           "> {storage_test_id = \"mask\"}"));
        for (const auto & input :
             {"ctjs.binary add %maximum, %i", "ctjs.binary sub %negativeSign, %i"}) {
            reject("structured identity masks reject signed conversion discontinuities",
                   replace(identity, "%position = ctjs.binary_static " + kind + " %i, %mask",
                           "%part = " + std::string(input) +
                               "\n"
                               "    %bits = ctjs.binary_static " +
                               kind +
                               " %part, %mask\n"
                               "    %position = ctjs.binary_static bitand %bits, %one"));
        }

        const auto fixedBits = replace(
            replace(
                replace(replace(identity, "[%x, %identity, %zero, %y]",
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
                "\n    %bits = ctjs.binary_static " + kind +
                " %part, %mask\n"
                "    %position = ctjs.binary sub %bits, " +
                std::string(kind == "bitand" ? "%zero" : "%eight"));
        for (const auto & operands : {"%part, %mask", "%mask, %part"}) {
            rows.push_back(
                {.what = "fixed-bit masks retain full odd strides in either operand order",
                 .body = replace(fixedBits, kind + " %part, %mask", kind + " " + operands),
                 .arrays = "a:[zero,mask,zero,zero,zero,zero,zero]",
                 .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[3]=zero; a[1]=mask; a[6]=zero",
                 .exit = "a -> {a}"});
        }
        rows.push_back(
            {.what = "fixed-bit translations preserve odd strides for negative input Numbers",
             .body = replace(
                 replace(fixedBits, "add %i, " + std::string(kind == "bitand" ? "%eight" : "%zero"),
                         "sub %i, " + std::string(kind == "bitand" ? "%eight" : "%sixteen")),
                 "sub %bits, " + std::string(kind == "bitand" ? "%zero" : "%eight"),
                 "add %bits, " + std::string(kind == "bitand" ? "%zero" : "%eight")),
             .arrays = "a:[zero,mask,zero,zero,zero,zero,zero]",
             .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[3]=zero; a[1]=mask; a[6]=zero",
             .exit = "a -> {a}"});
        rows.push_back({.what = "fixed-bit stride gaps preserve retained children",
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
        reject("fixed-bit translations retain the complete later-store census",
               replace(fixedBits,
                       "    %step =", "    ctjs.set_property %base[%one], %zero\n    %step ="));
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
        replace(replace(replace(replace(replace(signedBits, "[%x]",
                                                "[%x, %seven, %zero, %x, %zero, %zero, %x]"),
                                        "  ctjs.append %y to %a\n", ""),
                                "add %i, %one", "add %i, %three"),
                        "  %a =",
                        "  %three = ctjs.binary add %two, %one\n"
                        "  %six = ctjs.binary add %three, %three\n"
                        "  %seven = ctjs.constant #ctjs.number<4619567317775286272> "
                        "{storage_test_id = \"mask\"}\n"
                        "  %eight = ctjs.constant #ctjs.number<4620693217682128896>\n  %a ="),
                "%position = ctjs.binary_static bitand %i, %one",
                "%mask = ctjs.get_property %base[%one]\n"
                "    %bits = ctjs.binary_static bitxor %i, %mask\n"
                "    %position = ctjs.binary sub %bits, %one");
    for (const auto & expression : {"%bits = ctjs.binary_static bitxor %i, %mask\n"
                                    "    %position = ctjs.binary sub %bits, %one",
                                    "%bits = ctjs.binary_static bitxor %mask, %i\n"
                                    "    %position = ctjs.binary sub %bits, %one",
                                    "%part = ctjs.binary sub %i, %eight\n"
                                    "    %bits = ctjs.binary_static bitxor %part, %mask\n"
                                    "    %position = ctjs.binary add %bits, %seven",
                                    "%part = ctjs.binary add %sign, %i\n"
                                    "    %bits = ctjs.binary_static bitxor %part, %mask\n"
                                    "    %position = ctjs.binary add %bits, %maximum",
                                    "%lowInput = ctjs.binary add %low, %six\n"
                                    "    %part = ctjs.binary add %lowInput, %i\n"
                                    "    %bits = ctjs.binary_static bitxor %part, %mask\n"
                                    "    %offset = ctjs.binary add %eight, %one\n"
                                    "    %position = ctjs.binary sub %bits, %offset"}) {
        rows.push_back({.what = "flipping every varying input bit reverses the full odd stride",
                        .body = replace(varyingComplement,
                                        "%bits = ctjs.binary_static bitxor %i, %mask\n"
                                        "    %position = ctjs.binary sub %bits, %one",
                                        expression),
                        .arrays = "a:[zero,mask,zero,zero,zero,zero,zero]",
                        .reads = "a[1]=mask; a[0]=x; a[1]=mask; a[3]=zero; a[1]=mask; a[6]=zero",
                        .exit = "a -> {a}"});
    }
    rows.push_back(
        {.what = "a fixed sign bit may flip alongside all varying XOR bits",
         .body = replace(replace(varyingComplement, "4619567317775286272", "4746794007263182848"),
                         "sub %bits, %one", "add %bits, %maximum"),
         .arrays = "a:[zero,mask,zero,zero,zero,zero,zero]",
         .reads = "a[1]=mask; a[0]=x; a[1]=mask; a[3]=zero; a[1]=mask; a[6]=zero",
         .exit = "a -> {a}"});
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
        "    %position = ctjs.binary add %bits, %zero",
        "%part = ctjs.binary sub %i, %three\n"
        "    %bits = ctjs.binary_static bitxor %part, %mask\n"
        "    %position = ctjs.binary add %bits, %three");
    for (const auto & body : {fixedLowComplement, crossZeroComplement}) {
        rows.push_back(
            {.what = "fixed low XOR bits do not erase the odd factor of a reversed stride",
             .body = body,
             .arrays = "a:[zero,zero,mask,zero,zero,zero,zero]",
             .reads = "a[2]=mask; a[0]=x; a[2]=mask; a[6]=zero",
             .exit = "a -> {a}"});
    }
    rows.push_back({.what = "varying-bit complements preserve children between actual writes",
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
                   "    %step =", "    ctjs.set_property %base[%one], %zero\n    %step ="));
    reject("flipping only some varying bits cannot reverse the input lattice",
           replace(replace(replace(replace(varyingComplement,
                                           "[%x, %seven, %zero, %x, %zero, %zero, %x]",
                                           "[%x, %zero, %zero, %zero, %zero, %seven, %x, %zero]"),
                                   "4619567317775286272", "4618441417868443648"),
                           "%mask = ctjs.get_property %base[%one]",
                           "%five = ctjs.binary add %three, %two\n"
                           "    %mask = ctjs.get_property %base[%five]"),
                   "sub %bits, %one", "add %bits, %zero"));
    for (const auto & input :
         {"ctjs.binary add %maximum, %i", "ctjs.binary sub %negativeSign, %i"}) {
        rows.push_back(
            {.what = "two-point varying-bit complements preserve conversion-jump visits",
             .body = replace(crossZeroComplement,
                             "%part = ctjs.binary sub %i, %three\n"
                             "    %bits = ctjs.binary_static bitxor %part, %mask\n"
                             "    %position = ctjs.binary add %bits, %three",
                             "%part = " + std::string(input) +
                                 "\n    %bits = ctjs.binary_static bitxor %part, %mask\n"
                                 "    %position = ctjs.binary_static bitand %bits, %one"),
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
    const auto xorComplement = replace(
        replace(replace(replace(replace(complementBits, "[%x]", "[%x, %negativeOne, %zero, %y]"),
                                "  ctjs.append %y to %a\n", ""),
                        "  %a =", "  %three = ctjs.binary add %two, %one\n  %a ="),
                "add %i, %one", "add %i, %three"),
        "%position = ctjs.binary_static bitand %i, %one",
        "%mask = ctjs.get_property %base[%one]\n"
        "    %bits = ctjs.binary_static bitxor %i, %mask\n"
        "    %offset = ctjs.binary add %three, %one\n"
        "    %position = ctjs.binary add %bits, %offset");
    for (const auto & operands : {"%i, %mask", "%mask, %i"}) {
        rows.push_back(
            {.what = "XOR complement reverses odd-stride writes around an invariant mask",
             .body = replace(xorComplement, "bitxor %i, %mask", "bitxor " + std::string(operands)),
             .arrays = "a:[zero,negativeOne,zero,zero]",
             .reads = "a[1]=negativeOne; a[0]=x; a[1]=negativeOne; a[3]=zero",
             .exit = "a -> {a}"});
    }
    rows.push_back(
        {.what = "unsigned all-one XOR retains the same complement lattice",
         .body = replace(xorComplement, "%negativeOne = ctjs.unary neg %one",
                         "%negativeOne = ctjs.constant #ctjs.number<4751297606873776128>"),
         .arrays = "a:[zero,negativeOne,zero,zero]",
         .reads = "a[1]=negativeOne; a[0]=x; a[1]=negativeOne; a[3]=zero",
         .exit = "a -> {a}"});
    for (const auto & expression : {"%part = ctjs.binary sub %i, %one\n"
                                    "    %bits = ctjs.binary_static bitxor %part, %mask\n"
                                    "    %position = ctjs.binary add %bits, %three",
                                    "%part = ctjs.binary add %sign, %i\n"
                                    "    %bits = ctjs.binary_static bitxor %part, %mask\n"
                                    "    %offset = ctjs.binary sub %maximum, %three\n"
                                    "    %position = ctjs.binary sub %bits, %offset",
                                    "%part = ctjs.binary add %low, %i\n"
                                    "    %bits = ctjs.binary_static bitxor %part, %mask\n"
                                    "    %offset = ctjs.binary add %three, %three\n"
                                    "    %position = ctjs.binary add %bits, %offset"}) {
        rows.push_back({.what = "XOR complement retains odd strides within each conversion band",
                        .body = replace(xorComplement,
                                        "%bits = ctjs.binary_static bitxor %i, %mask\n"
                                        "    %offset = ctjs.binary add %three, %one\n"
                                        "    %position = ctjs.binary add %bits, %offset",
                                        expression),
                        .arrays = "a:[zero,negativeOne,zero,zero]",
                        .reads = "a[1]=negativeOne; a[0]=x; a[1]=negativeOne; a[3]=zero",
                        .exit = "a -> {a}"});
    }
    rows.push_back({.what = "XOR complement replay retains an unvisited child",
                    .body = replace(xorComplement, "[%x, %negativeOne, %zero, %y]",
                                    "[%x, %negativeOne, %x, %y]"),
                    .arrays = "a:[zero,negativeOne,x,zero]",
                    .reads = "a[1]=negativeOne; a[0]=x; a[1]=negativeOne; a[3]=zero",
                    .exit = "a -> {a,x}"});
    for (const auto & body :
         {replace(replace(xorComplement, "[%x, %negativeOne, %zero, %y]",
                          "[%negativeOne, %x, %zero, %y]"),
                  "%mask = ctjs.get_property %base[%one]",
                  "%mask = ctjs.get_property %base[%zero]"),
          replace(xorComplement,
                  "    %step =", "    ctjs.set_property %base[%one], %zero\n    %step ="),
          replace(xorComplement, "%negativeOne = ctjs.unary neg %one",
                  "%negativeOne = ctjs.constant #ctjs.number<13835058055282163712>")}) {
        reject("XOR complement requires the exact mask and complete disjoint reload census", body);
    }
    for (const auto & input :
         {"ctjs.binary add %maximum, %i", "ctjs.binary sub %negativeSign, %i"}) {
        reject("XOR complement refuses signed conversion discontinuities",
               replace(xorComplement,
                       "%bits = ctjs.binary_static bitxor %i, %mask\n"
                       "    %offset = ctjs.binary add %three, %one\n"
                       "    %position = ctjs.binary add %bits, %offset",
                       "%part = " + std::string(input) +
                           "\n    %bits = ctjs.binary_static bitxor %part, %mask\n"
                           "    %position = ctjs.binary_static bitand %bits, %one"));
    }
    for (const auto & expression :
         {"%position = ctjs.binary_static bitand %i, %negativeOne",
          "%position = ctjs.binary_static bitand %negativeOne, %i",
          "%negative = ctjs.unary bitnot %i\n"
          "    %bits = ctjs.binary_static bitand %negative, %negativeOne\n"
          "    %position = ctjs.unary bitnot %bits",
          "%high = ctjs.binary add %sign, %i\n"
          "    %bits = ctjs.binary_static bitand %high, %negativeOne\n"
          "    %position = ctjs.binary add %bits, %sign",
          "%high = ctjs.binary add %low, %i\n"
          "    %bits = ctjs.binary_static bitand %high, %negativeOne\n"
          "    %position = ctjs.binary sub %bits, %two"}) {
        rows.push_back(
            {.what = "structured signed AND bands preserve exact overwritten aliases",
             .body =
                 replace(signedBits, "%position = ctjs.binary_static bitand %i, %one", expression),
             .arrays = "a:[zero,zero]",
             .reads = "a[0]=zero; a[1]=zero",
             .exit = "a -> {a}"});
    }
    for (const auto & input : {"ctjs.binary sub %i, %one", "ctjs.binary add %maximum, %i",
                               "ctjs.binary sub %negativeSign, %i"}) {
        const auto body = replace(signedBits, "%position = ctjs.binary_static bitand %i, %one",
                                  "%part = " + std::string(input) +
                                      "\n"
                                      "    %bits = ctjs.binary_static bitand %part, %negativeOne\n"
                                      "    %position = ctjs.binary_static bitand %bits, %one");
        if (std::string(input) == "ctjs.binary sub %i, %one") {
            rows.push_back({.what = "structured identity AND preserves the original zero crossing",
                            .body = body,
                            .arrays = "a:[zero,zero]",
                            .reads = "a[0]=x; a[1]=zero",
                            .exit = "a -> {a}"});
        } else {
            rows.push_back({.what = "two-point identity AND preserves both conversion-jump images",
                            .body = body,
                            .arrays = "a:[zero,zero]",
                            .reads = std::string(input) == "ctjs.binary add %maximum, %i"
                                         ? "a[0]=x; a[1]=zero"
                                         : "a[0]=zero; a[1]=zero",
                            .exit = "a -> {a}"});
        }
    }
    const auto andReload =
        replace(replace(replace(masked, "[%x]", "[%x, %y, %negativeMask, %zero]"),
                        "  ctjs.append %y to %a\n", ""),
                "  %a =",
                "  %two = ctjs.binary add %one, %one\n"
                "  %negativeMask = ctjs.constant #ctjs.number<13970166044099084288> "
                "{storage_test_id = \"mask\"}\n  %a =");
    const auto readAndMask = replace(andReload, "%position = ctjs.binary_static bitand %i, %one",
                                     "%mask = ctjs.get_property %base[%two]\n"
                                     "    %position = ctjs.binary_static bitand %i, %mask");
    rows.push_back({.what = "structured signed AND masks preserve disjoint reloads",
                    .body = readAndMask,
                    .arrays = "a:[zero,zero,mask,zero]",
                    .reads = "a[2]=mask; a[0]=zero; a[2]=mask; a[1]=zero; a[2]=mask; a[2]=mask; "
                             "a[2]=mask; a[3]=zero",
                    .exit = "a -> {a}"});
    reject("structured signed AND rejects overlapping mask reloads",
           replace(replace(readAndMask, "[%x, %y, %negativeMask, %zero]",
                           "[%negativeMask, %y, %zero, %zero]"),
                   "%mask = ctjs.get_property %base[%two]",
                   "%mask = ctjs.get_property %base[%zero]"));
    reject("later structured stores invalidate earlier signed AND reloads",
           replace(readAndMask,
                   "    %step =", "    ctjs.set_property %base[%two], %zero\n    %step ="));
    for (const auto & bits : {"4611686018427387904", "13835058055282163712"}) {
        const auto gapReload = replace(
            replace(replace(readAndMask, "13970166044099084288", bits),
                    "[%x, %y, %negativeMask, %zero]", "[%x, %negativeMask, %y, %zero]"),
            "%mask = ctjs.get_property %base[%two]", "%mask = ctjs.get_property %base[%one]");
        for (const auto & operands : {"%i, %mask", "%mask, %i"}) {
            rows.push_back(
                {.what = "structured AND low zero bits preserve reloads in an interval gap",
                 .body = replace(gapReload, "bitand %i, %mask", "bitand " + std::string(operands)),
                 .arrays = "a:[zero,mask,zero,zero]",
                 .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[1]=mask; a[1]=mask; a[2]=zero; "
                          "a[1]=mask; a[3]=zero",
                 .exit = "a -> {a}"});
        }
        rows.push_back({.what = "structured AND lattices preserve unvisited gap children",
                        .body = replace(gapReload, "[%x, %negativeMask, %y, %zero]",
                                        "[%x, %negativeMask, %y, %y]"),
                        .arrays = "a:[zero,mask,zero,y]",
                        .reads =
                            "a[1]=mask; a[0]=zero; a[1]=mask; a[1]=mask; a[1]=mask; a[2]=zero; "
                            "a[1]=mask; a[3]=y",
                        .exit = "a -> {a,y}"});
        reject("structured AND lattices reject a mask at a visited residue",
               replace(replace(gapReload, "[%x, %negativeMask, %y, %zero]",
                               "[%x, %zero, %negativeMask, %zero]"),
                       "%mask = ctjs.get_property %base[%one]",
                       "%mask = ctjs.get_property %base[%two]"));
        reject("later structured stores invalidate a mask in an AND lattice gap",
               replace(gapReload,
                       "    %step =", "    ctjs.set_property %base[%one], %zero\n    %step ="));
    }
    for (const auto & bits : {"4613937818241073152", "13830554455654793216"}) {
        auto inputGap = replace(replace(readAndMask, "13970166044099084288", bits),
                                "[%x, %y, %negativeMask, %zero]", "[%zero, %x, %negativeMask, %y]");
        inputGap = replace(replace(inputGap, "%index = %zero", "%index = %one"), "add %i, %one",
                           "add %i, %two");
        for (const auto & operands : {"%i, %mask", "%mask, %i"}) {
            rows.push_back(
                {.what = "structured AND input low bits preserve a nonzero mask-reload gap",
                 .body = replace(inputGap, "bitand %i, %mask", "bitand " + std::string(operands)),
                 .arrays = "a:[zero,zero,mask,zero]",
                 .reads = "a[2]=mask; a[1]=zero; a[2]=mask; a[3]=zero",
                 .exit = "a -> {a}"});
        }
        rows.push_back({.what = "structured AND input lattices retain unwritten gap children",
                        .body = replace(inputGap, "[%zero, %x, %negativeMask, %y]",
                                        "[%x, %x, %negativeMask, %y]"),
                        .arrays = "a:[x,zero,mask,zero]",
                        .reads = "a[2]=mask; a[1]=zero; a[2]=mask; a[3]=zero",
                        .exit = "a -> {a,x}"});
        reject("structured AND input lattices reject a mask at a visited residue",
               replace(replace(inputGap, "[%zero, %x, %negativeMask, %y]",
                               "[%zero, %negativeMask, %zero, %y]"),
                       "%mask = ctjs.get_property %base[%two]",
                       "%mask = ctjs.get_property %base[%one]"));
        reject("later structured stores invalidate a mask inside an AND input-lattice gap",
               replace(inputGap,
                       "    %step =", "    ctjs.set_property %base[%two], %zero\n    %step ="));
        reject("structured unit input strides cannot retain an AND low-bit gap",
               replace(inputGap, "add %i, %two", "add %i, %one"));
    }
    for (const auto & expression :
         {"%high = ctjs.binary sub %maximum, %i\n"
          "    %position = ctjs.binary_static bitxor %high, %maximum",
          "%high = ctjs.binary add %sign, %i\n"
          "    %position = ctjs.binary_static bitxor %high, %sign",
          "%negative = ctjs.unary bitnot %i\n"
          "    %position = ctjs.binary_static bitxor %negative, %negativeOne",
          "%high = ctjs.binary add %low, %i\n"
          "    %position = ctjs.binary_static bitxor %high, %two",
          "%negative = ctjs.binary_static bitor %i, %negativeSign\n"
          "    %position = ctjs.binary add %negative, %sign",
          "%negative = ctjs.binary_static bitor %sign, %i\n"
          "    %position = ctjs.binary add %negative, %sign",
          "%high = ctjs.binary add %sign, %i\n"
          "    %negative = ctjs.binary_static bitor %high, %zero\n"
          "    %position = ctjs.binary add %negative, %sign"}) {
        rows.push_back(
            {.what = "structured signed OR/XOR enclosures preserve exact writes",
             .body =
                 replace(signedBits, "%position = ctjs.binary_static bitand %i, %one", expression),
             .arrays = "a:[zero,zero]",
             .reads = "a[0]=zero; a[1]=zero",
             .exit = "a -> {a}"});
    }
    for (const auto & input : {"ctjs.binary sub %i, %one", "ctjs.binary add %maximum, %i",
                               "ctjs.binary sub %negativeSign, %i"}) {
        rows.push_back(
            {.what = "two-point XOR composes exact images through a final mask",
             .body = replace(signedBits, "%position = ctjs.binary_static bitand %i, %one",
                             "%part = " + std::string(input) +
                                 "\n"
                                 "    %bits = ctjs.binary_static bitxor %part, %maximum\n"
                                 "    %position = ctjs.binary_static bitand %bits, %one"),
             .arrays = "a:[zero,zero]",
             .reads = std::string(input) == "ctjs.binary sub %negativeSign, %i"
                          ? "a[0]=x; a[1]=zero"
                          : "a[0]=zero; a[1]=zero",
             .exit = "a -> {a}"});
    }
    const auto orReload = replace(replace(replace(masked, "[%x]", "[%one, %y, %zero, %zero]"),
                                          "  ctjs.append %y to %a\n", ""),
                                  "%position = ctjs.binary_static bitand %i, %one",
                                  "%mask = ctjs.get_property %base[%zero]\n"
                                  "    %position = ctjs.binary_static bitor %i, %mask");
    rows.push_back({.what = "structured OR bounds exclude lower invariant reloads",
                    .body = orReload,
                    .arrays = "a:[one,zero,zero,zero]",
                    .reads = "a[0]=one; a[0]=one; a[0]=one; a[1]=zero; a[0]=one; a[2]=zero; "
                             "a[0]=one; a[3]=zero",
                    .exit = "a -> {a}"});
    reject("structured XOR may overwrite slots below its mask",
           replace(orReload, "bitor", "bitxor"));
    reject(
        "later structured stores invalidate earlier OR mask reloads",
        replace(orReload, "    %step =", "    ctjs.set_property %base[%zero], %zero\n    %step ="));
    const auto signedReload = replace(
        replace(replace(orReload, "  %a =",
                        "  %sign = ctjs.constant #ctjs.number<4746794007248502784>\n"
                        "  %negativeMask = ctjs.constant #ctjs.number<13970166044099084288> "
                        "{storage_test_id = \"mask\"}\n  %a ="),
                "[%one, %y, %zero, %zero]", "[%negativeMask, %y, %zero, %zero]"),
        "%position = ctjs.binary_static bitor %i, %mask",
        "%negative = ctjs.binary_static bitor %i, %mask\n"
        "    %position = ctjs.binary add %negative, %sign");
    rows.push_back({.what = "structured signed masks preserve lower disjoint reloads",
                    .body = signedReload,
                    .arrays = "a:[mask,zero,zero,zero]",
                    .reads = "a[0]=mask; a[0]=mask; a[0]=mask; a[1]=zero; a[0]=mask; a[2]=zero; "
                             "a[0]=mask; a[3]=zero",
                    .exit = "a -> {a}"});
    reject("structured signed XOR cannot borrow OR's disjoint reload proof",
           replace(signedReload, "bitor", "bitxor"));
    rows.push_back(
        {.what = "structured signed OR mask bits preserve translated interior reload gaps",
         .body = replace(
             replace(replace(signedReload, "  %a =", "  %two = ctjs.binary add %one, %one\n  %a ="),
                     "[%negativeMask, %y, %zero, %zero]", "[%zero, %x, %negativeMask, %y]"),
             "%mask = ctjs.get_property %base[%zero]", "%mask = ctjs.get_property %base[%two]"),
         .arrays = "a:[zero,zero,mask,zero]",
         .reads = "a[2]=mask; a[0]=zero; a[2]=mask; a[1]=zero; a[2]=mask; a[2]=mask; "
                  "a[2]=mask; a[3]=zero",
         .exit = "a -> {a}"});
    const auto allOneReload = replace(
        replace(replace(replace(signedReload, "13970166044099084288", "4751297606873776128"),
                        "[%negativeMask, %y, %zero, %zero]", "[%y, %negativeMask]"),
                "%mask = ctjs.get_property %base[%zero]", "%mask = ctjs.get_property %base[%one]"),
        "add %negative, %sign", "add %negative, %one");
    for (const auto & input : {"%part = ctjs.binary sub %i, %one",
                               "%maximum = ctjs.binary sub %sign, %one\n"
                               "    %part = ctjs.binary add %maximum, %i",
                               "%minimum = ctjs.binary sub %zero, %sign\n"
                               "    %part = ctjs.binary sub %minimum, %i"}) {
        const auto crossing =
            replace(allOneReload, "%negative = ctjs.binary_static bitor %i, %mask",
                    std::string(input) + "\n    %negative = ctjs.binary_static bitor %mask, %part");
        rows.push_back(
            {.what = "structured unsigned all-one masks are constant across conversion bands",
             .body = crossing,
             .arrays = "a:[zero,mask]",
             .reads = "a[1]=mask; a[0]=zero; a[1]=mask; a[1]=mask",
             .exit = "a -> {a}"});
        reject("structured near-all-one OR masks retain their conversion guards",
               replace(crossing, "4751297606873776128", "13835058055282163712"));
        reject("structured all-one masks retain the complete later-store census",
               replace(crossing,
                       "    %step =", "    ctjs.set_property %base[%one], %zero\n    %step ="));
        const auto signedCrossing = replace(
            replace(replace(replace(replace(crossing, "4751297606873776128", "4751297606871678976"),
                                    "[%y, %negativeMask]", "[%y, %y, %negativeMask, %zero]"),
                            "  %a =", "  %two = ctjs.binary add %one, %one\n  %a ="),
                    "%mask = ctjs.get_property %base[%one]",
                    "%mask = ctjs.get_property %base[%two]"),
            "add %negative, %one", "add %negative, %two");
        rows.push_back(
            {.what = "structured sign-setting OR masks bound crossing inputs with one interval",
             .body = signedCrossing,
             .arrays = "a:[zero,zero,mask,zero]",
             .reads = std::string(input).starts_with("%minimum")
                          ? "a[2]=mask; a[0]=zero; a[2]=mask; a[1]=zero; a[2]=mask; a[2]=mask; "
                            "a[2]=mask; a[3]=zero"
                          : "a[2]=mask; a[0]=y; a[2]=mask; a[1]=zero; a[2]=mask; a[2]=mask; "
                            "a[2]=mask; a[3]=zero",
             .exit = "a -> {a}"});
        reject("structured XOR cannot borrow a fixed OR output sign across conversion bands",
               replace(signedCrossing, "bitor", "bitxor"));
        reject("structured sign-setting masks cannot reload from a crossing output range",
               replace(replace(signedCrossing, "[%y, %y, %negativeMask, %zero]",
                               "[%y, %negativeMask, %y, %zero]"),
                       "%mask = ctjs.get_property %base[%two]",
                       "%mask = ctjs.get_property %base[%one]"));
        reject("later structured writes invalidate sign-setting masks across conversion bands",
               replace(signedCrossing,
                       "    %step =", "    ctjs.set_property %base[%two], %zero\n    %step ="));
    }
    reject("structured signed masks retain the complete later-store census",
           replace(signedReload,
                   "    %step =", "    ctjs.set_property %base[%zero], %zero\n    %step ="));
    reloaded =
        replace(replace(replace(original, "[%x]", "[%one]"),
                        "    %step =", "    %unit = ctjs.get_property %base[%zero]\n    %step ="),
                "add %i, %one", "add %i, %unit");
    rows.push_back({.what = "structured dense own-element reload retains original aliases",
                    .body = reloaded,
                    .arrays = "a:[one,y]",
                    .reads = "a[0]=one; a[0]=one; a[1]=y; a[0]=one",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "structured reloaded stride leaves discarded children confined",
                    .body = replace(reloaded, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[one,y]",
                    .reads = "a[0]=one; a[0]=one; a[1]=y; a[0]=one",
                    .exit = "zero -> {}"});
    const auto disjointIndex =
        replace(reloaded, "    %read =", "    ctjs.set_property %base[%one], %zero\n    %read =");
    rows.push_back({.what = "structured fixed overwrites preserve a disjoint stride reload",
                    .body = disjointIndex,
                    .arrays = "a:[one,zero]",
                    .reads = "a[0]=one; a[0]=one; a[1]=zero; a[0]=one",
                    .exit = "zero -> {}"});
    for (const auto & store : {"%base[%zero]", "%base[%i]"}) {
        reject("structured overlapping stores cannot prove a reloaded stride",
               replace(disjointIndex, "%base[%one]", store));
    }
    reject("structured later stores invalidate earlier reloads",
           replace(disjointIndex,
                   "    %step =", "    ctjs.set_property %base[%zero], %one\n    %step ="));
    const auto visitedIndex =
        replace(replace(disjointIndex, "%base[%one], %zero", "%base[%i], %zero"), "%index = %zero",
                "%index = %one");
    rows.push_back({.what = "structured reloads below the start survive current-index writes",
                    .body = visitedIndex,
                    .arrays = "a:[one,zero]",
                    .reads = "a[1]=zero; a[0]=one",
                    .exit = "zero -> {}"});
    rows.push_back({.what = "structured zero-trip overwrites preserve guard contents",
                    .body = replace(replace(replace(visitedIndex, "  %finalIndex,",
                                                    "  %two = ctjs.constant "
                                                    "#ctjs.number<4611686018427387904>\n"
                                                    "  %finalIndex,"),
                                            "%index = %one", "%index = %two"),
                                    "ctjs.return %result", "ctjs.return %a"),
                    .arrays = "a:[one,y]",
                    .exit = "a -> {a,y}"});
    const auto skippedIndex = replace(
        replace(replace(replace(replace(visitedIndex, "%index = %one", "%index = %zero"), "  %a =",
                                "  %two = ctjs.constant "
                                "#ctjs.number<4611686018427387904> "
                                "{storage_test_id = \"two\"}\n  %a ="),
                        "[%one]", "[%y, %two, %y]"),
                "  ctjs.append %y to %a\n", ""),
        "%unit = ctjs.get_property %base[%zero]", "%unit = ctjs.get_property %base[%one]");
    rows.push_back({.what = "structured reloads between stride positions survive overwrites",
                    .body = skippedIndex,
                    .arrays = "a:[zero,two,zero]",
                    .reads = "a[0]=zero; a[1]=two; a[2]=zero; a[1]=two",
                    .exit = "zero -> {}"});
    rows.push_back(
        {.what = "structured visited positions depend on the nonzero start",
         .body = replace(replace(replace(skippedIndex, "%index = %zero", "%index = %one"),
                                 "[%y, %two, %y]", "[%two, %y, %two, %y]"),
                         "%unit = ctjs.get_property %base[%one]",
                         "%unit = ctjs.get_property %base[%two]"),
         .arrays = "a:[two,zero,two,zero]",
         .reads = "a[1]=zero; a[2]=two; a[3]=zero; a[2]=two",
         .exit = "zero -> {}"});
    reject("a later structured visit invalidates an invariant reload",
           replace(replace(skippedIndex, "[%y, %two, %y]", "[%y, %y, %two]"),
                   "%unit = ctjs.get_property %base[%one]",
                   "%unit = ctjs.get_property %base[%two]"));
    reject("an unvisited structured slot still cannot overlap a fixed write",
           replace(visitedIndex,
                   "    %step =", "    ctjs.set_property %base[%zero], %one\n    %step ="));
    const auto offsetIndex =
        replace(replace(replace(replace(replace(original, "[%x]", "[%one, %y, %one, %y]"),
                                        "  ctjs.append %y to %a\n", ""),
                                "  %a =", "  %two = ctjs.binary add %one, %one\n  %a ="),
                        "    %read =",
                        "    %position = ctjs.binary add %i, %one\n"
                        "    ctjs.set_property %base[%position], %zero\n    %read ="),
                "    %step = ctjs.binary_static add %i, %one",
                "    %step = ctjs.binary_static add %i, %two");
    for (const auto & expression : {"ctjs.binary add %i, %one", "ctjs.binary add %one, %i",
                                    "ctjs.binary_static add %i, %one"}) {
        rows.push_back({.what = "structured Number offsets write only bounded shifted positions",
                        .body = replace(offsetIndex, "ctjs.binary add %i, %one", expression),
                        .arrays = "a:[one,zero,one,zero]",
                        .reads = "a[0]=one; a[2]=one",
                        .exit = "one -> {}"});
    }
    const auto previousIndex =
        replace(replace(replace(offsetIndex, "[%one, %y, %one, %y]", "[%y, %one, %y, %one]"),
                        "%index = %zero", "%index = %one"),
                "ctjs.binary add %i, %one", "ctjs.binary sub %i, %one");
    const auto complementIndex =
        replace(replace(offsetIndex, "[%one, %y, %one, %y]", "[%y, %one, %y, %one]"),
                "%position = ctjs.binary add %i, %one",
                "%negative = ctjs.unary bitnot %i\n"
                "    %position = ctjs.unary bitnot %negative");
    rows.push_back({.what = "structured double complements preserve bounded positions and stride",
                    .body = complementIndex,
                    .arrays = "a:[zero,one,zero,one]",
                    .reads = "a[0]=zero; a[2]=zero",
                    .exit = "zero -> {}"});
    const auto negativeComplement = replace(complementIndex, "%negative = ctjs.unary bitnot %i",
                                            "%part = ctjs.binary sub %zero, %i\n"
                                            "    %negative = ctjs.binary sub %part, %one");
    rows.push_back({.what = "structured negative complements preserve exact nonnegative indices",
                    .body = negativeComplement,
                    .arrays = "a:[zero,one,zero,one]",
                    .reads = "a[0]=zero; a[2]=zero",
                    .exit = "zero -> {}"});
    const auto signedBoundary =
        replace(replace(complementIndex, "  %a =",
                        "  %maximum = ctjs.constant #ctjs.number<4746794007244308480>\n"
                        "  %minimumMagnitude = ctjs.binary add %maximum, %one\n  %a ="),
                "%negative = ctjs.unary bitnot %i\n    %position = ctjs.unary bitnot %negative",
                "%part = ctjs.binary sub %maximum, %i\n"
                "    %negative = ctjs.unary bitnot %part\n"
                "    %position = ctjs.binary add %negative, %minimumMagnitude");
    rows.push_back({.what = "structured complements include signed i32 boundary values",
                    .body = signedBoundary,
                    .arrays = "a:[zero,one,zero,one]",
                    .reads = "a[0]=zero; a[2]=zero",
                    .exit = "zero -> {}"});
    const auto positiveBand =
        replace(replace(signedBoundary, "sub %maximum, %i", "add %minimumMagnitude, %i"),
                "add %negative, %minimumMagnitude", "sub %maximum, %negative");
    const auto negativeBand =
        replace(replace(positiveBand, "%part = ctjs.binary add %minimumMagnitude, %i",
                        "%lower = ctjs.unary neg %minimumMagnitude\n"
                        "    %below = ctjs.binary sub %lower, %one\n"
                        "    %part = ctjs.binary sub %below, %i"),
                "sub %maximum, %negative", "add %negative, %minimumMagnitude");
    for (const auto & body : {positiveBand, negativeBand}) {
        rows.push_back({.what = "structured complements preserve a single wrapping ToInt32 band",
                        .body = body,
                        .arrays = "a:[zero,one,zero,one]",
                        .reads = "a[0]=zero; a[2]=zero",
                        .exit = "zero -> {}"});
    }
    for (const auto & body : {replace(signedBoundary, "sub %maximum, %i", "add %maximum, %i"),
                              replace(negativeBand, "sub %below, %i", "add %below, %i"),
                              replace(negativeComplement, "sub %part, %one", "sub %part, %zero")}) {
        reject("structured complements reject ToInt32 discontinuities and negative own positions",
               body);
    }
    const auto crossingComplement =
        replace(replace(replace(signedBoundary, "sub %maximum, %i", "add %maximum, %i"),
                        "%two = ctjs.binary add %one, %one",
                        "%two = ctjs.binary add %one, %one {storage_test_id = \"two\"}"),
                "ctjs.binary add %negative, %minimumMagnitude",
                "ctjs.binary_static bitand %negative, %two");
    const auto negativeCrossingComplement =
        replace(replace(replace(negativeBand, "sub %below, %i", "add %below, %i"),
                        "%two = ctjs.binary add %one, %one",
                        "%two = ctjs.binary add %one, %one {storage_test_id = \"two\"}"),
                "ctjs.binary add %negative, %minimumMagnitude",
                "ctjs.binary_static bitand %negative, %two");
    for (const auto & body : {crossingComplement, negativeCrossingComplement}) {
        rows.push_back(
            {.what = "two-point complements compose across either signed conversion jump",
             .body = body,
             .arrays = "a:[zero,one,zero,one]",
             .reads = "a[0]=zero; a[2]=zero",
             .exit = "zero -> {}"});
        rows.push_back(
            {.what = "two-point complement gaps preserve a reloaded mask",
             .body = replace(replace(body, "[%y, %one, %y, %one]", "[%y, %two, %y, %one]"),
                             "%position = ctjs.binary_static bitand %negative, %two",
                             "%mask = ctjs.get_property %base[%one]\n"
                             "    %position = ctjs.binary_static bitand %negative, %mask"),
             .arrays = "a:[zero,two,zero,one]",
             .reads = "a[1]=two; a[0]=zero; a[1]=two; a[2]=zero",
             .exit = "zero -> {}"});
        reject("complement endpoints cannot omit an interior conversion-jump visit",
               replace(body, "[%y, %one, %y, %one]", "[%y, %one, %y, %one, %y, %one]"));
    }
    rows.push_back(
        {.what = "a second complement reverses the exact conversion-jump endpoint images",
         .body =
             replace(crossingComplement, "%position = ctjs.binary_static bitand %negative, %two",
                     "%again = ctjs.unary bitnot %negative\n"
                     "    %position = ctjs.binary_static bitand %again, %two"),
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[0]=y; a[2]=zero",
         .exit = "zero -> {}"});
    rows.push_back(
        {.what = "a two-value intermediate can have more than two loop visits",
         .body = replace(replace(crossingComplement, "%part = ctjs.binary add %maximum, %i",
                                 "%bucket = ctjs.binary_static bitand %i, %two\n"
                                 "    %part = ctjs.binary add %maximum, %bucket"),
                         "%step = ctjs.binary_static add %i, %two",
                         "%step = ctjs.binary_static add %i, %one"),
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[0]=zero; a[1]=one; a[2]=zero; a[3]=one",
         .exit = "one -> {}"});
    rows.push_back(
        {.what = "two-point complement writes retain a child outside their exact lattice",
         .body = replace(replace(crossingComplement, "[%y, %one, %y, %one]", "[%y, %y, %y, %one]"),
                         "ctjs.return %result", "ctjs.return %a"),
         .arrays = "a:[zero,y,zero,one]",
         .reads = "a[0]=zero; a[2]=zero",
         .exit = "a -> {a,y}"});
    rows.push_back(
        {.what = "two-point complements preserve an earlier child snapshot",
         .body = replace(replace(crossingComplement, "  %finalIndex,",
                                 "  %before = ctjs.get_property %a[%zero]\n  %finalIndex,"),
                         "ctjs.return %result", "ctjs.return %before"),
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[0]=y; a[0]=zero; a[2]=zero",
         .exit = "y -> {y}"});
    const auto complementReload =
        replace(replace(crossingComplement, "[%y, %one, %y, %one]", "[%y, %two, %y, %one]"),
                "%position = ctjs.binary_static bitand %negative, %two",
                "%mask = ctjs.get_property %base[%one]\n"
                "    %position = ctjs.binary_static bitand %negative, %mask");
    reject("two-point complements retain the exact endpoint reload overlap check",
           replace(replace(complementReload, "[%y, %two, %y, %one]", "[%two, %one, %y, %one]"),
                   "%mask = ctjs.get_property %base[%one]",
                   "%mask = ctjs.get_property %base[%zero]"));
    reject("two-point complement gaps retain the complete later-store census",
           replace(complementReload,
                   "    %step =", "    ctjs.set_property %base[%one], %zero\n    %step ="));
    rows.push_back({.what = "structured subtracted offsets retain a nonzero start",
                    .body = previousIndex,
                    .arrays = "a:[zero,one,zero,one]",
                    .reads = "a[1]=one; a[3]=one",
                    .exit = "one -> {}"});
    rows.push_back({.what = "structured signed offsets preserve their Number snapshot",
                    .body = replace(replace(previousIndex,
                                            "  %a =", "  %negative = ctjs.unary neg %one\n  %a ="),
                                    "ctjs.binary sub %i, %one", "ctjs.binary add %i, %negative"),
                    .arrays = "a:[zero,one,zero,one]",
                    .reads = "a[1]=one; a[3]=one",
                    .exit = "one -> {}"});
    const auto reloadedOffset = replace(offsetIndex, "    %position = ctjs.binary add %i, %one",
                                        "    %offset = ctjs.get_property %base[%zero]\n"
                                        "    %position = ctjs.binary add %i, %offset");
    rows.push_back({.what = "structured offsets may reload outside every shifted write",
                    .body = reloadedOffset,
                    .arrays = "a:[one,zero,one,zero]",
                    .reads = "a[0]=one; a[0]=one; a[0]=one; a[2]=one",
                    .exit = "one -> {}"});
    rows.push_back({.what = "structured offset overwrites keep a previously saved child",
                    .body = replace(replace(offsetIndex, "  %finalIndex,",
                                            "  %held = ctjs.get_property %a[%one]\n  %finalIndex,"),
                                    "ctjs.return %result", "ctjs.return %held"),
                    .arrays = "a:[one,zero,one,zero]",
                    .reads = "a[1]=y; a[0]=one; a[2]=one",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "structured zero-trip offset stores preserve all children",
                    .body = replace(replace(replace(offsetIndex, "%index = %zero", "%index = %two"),
                                            "[%one, %y, %one, %y]", "[%one, %y]"),
                                    "ctjs.return %result", "ctjs.return %a"),
                    .arrays = "a:[one,y]",
                    .exit = "a -> {a,y}"});
    for (const auto & body :
         {replace(previousIndex, "%index = %one", "%index = %zero"),
          replace(offsetIndex, "[%one, %y, %one, %y]", "[%one, %y, %one]"),
          replace(replace(reloadedOffset, "[%one, %y, %one, %y]", "[%one, %one, %y, %one]"),
                  "%offset = ctjs.get_property %base[%zero]",
                  "%offset = ctjs.get_property %base[%one]"),
          replace(
              replace(replace(reloadedOffset, "[%one, %y, %one, %y]", "[%one, %y, %one, %one]"),
                      "    %offset =", "    %three = ctjs.binary add %two, %one\n    %offset ="),
              "%offset = ctjs.get_property %base[%zero]",
              "%offset = ctjs.get_property %base[%three]"),
          replace(reloadedOffset,
                  "    %step =", "    ctjs.set_property %base[%zero], %one\n    %step ="),
          replace(offsetIndex, "ctjs.binary add %i, %one", "ctjs.binary add %i, %last"),
          replace(
              replace(offsetIndex, "  %a =", "  %text = ctjs.constant #ctjs.string<\"1\">\n  %a ="),
              "ctjs.binary add %i, %one", "ctjs.binary add %i, %text")}) {
        reject("structured offset stores reject invalid bounds, mutable values and non-Numbers",
               body);
    }
    const auto quotientIndex = replace(
        replace(replace(replace(offsetIndex, "%two = ctjs.binary add %one, %one",
                                "%two = ctjs.binary add %one, %one {storage_test_id = \"two\"}"),
                        "[%one, %y, %one, %y]", "[%y, %y, %one, %one]"),
                "ctjs.binary add %i, %one", "ctjs.binary div %i, %two"),
        "ctjs.return %result", "ctjs.return %a");
    const auto leftShiftIndex =
        replace(replace(complementIndex, "ctjs.return %result", "ctjs.return %a"),
                "%negative = ctjs.unary bitnot %i\n    %position = ctjs.unary bitnot %negative",
                "%part = ctjs.binary div %i, %two\n"
                "    %position = ctjs.binary_static shl %part, %one");
    rows.push_back({.what = "structured left shifts preserve nonwrapping positions and stride",
                    .body = leftShiftIndex,
                    .arrays = "a:[zero,one,zero,one]",
                    .reads = "a[0]=zero; a[2]=zero",
                    .exit = "a -> {a}"});
    const auto leftShiftReload =
        replace(leftShiftIndex, "    %position = ctjs.binary_static shl %part, %one",
                "    %count = ctjs.get_property %base[%one]\n"
                "    %position = ctjs.binary_static shl %part, %count");
    rows.push_back({.what = "structured left-shift count reloads survive in the scaled stride gaps",
                    .body = leftShiftReload,
                    .arrays = "a:[zero,one,zero,one]",
                    .reads = "a[1]=one; a[0]=zero; a[1]=one; a[2]=zero",
                    .exit = "a -> {a}"});
    const auto leftShiftBoundary =
        replace(replace(leftShiftIndex, "  %a =",
                        "  %half = ctjs.constant #ctjs.number<4742290407612743680>\n"
                        "  %maximum = ctjs.binary mul %half, %two\n  %a ="),
                "%position = ctjs.binary_static shl %part, %one",
                "%input = ctjs.binary sub %half, %part\n"
                "    %shifted = ctjs.binary_static shl %input, %one\n"
                "    %position = ctjs.binary sub %maximum, %shifted");
    rows.push_back({.what = "structured left shifts include the exact positive output boundary",
                    .body = leftShiftBoundary,
                    .arrays = "a:[zero,one,zero,one]",
                    .reads = "a[0]=zero; a[2]=zero",
                    .exit = "a -> {a}"});
    reject("structured left shifts refuse the signed-output discontinuity",
           replace(leftShiftBoundary, "4742290407612743680", "4742290407621132288"));
    const auto negativeLeftShift = replace(
        leftShiftIndex,
        "%part = ctjs.binary div %i, %two\n    %position = ctjs.binary_static shl %part, %one",
        "%input = ctjs.binary sub %i, %one\n"
        "    %shifted = ctjs.binary_static shl %input, %one\n"
        "    %part = ctjs.binary div %shifted, %two\n"
        "    %position = ctjs.binary add %part, %one");
    const auto minimumLeftShift =
        replace(replace(replace(leftShiftBoundary, "4742290407612743680", "4742290407621132288"),
                        "sub %half, %part", "sub %part, %half"),
                "sub %maximum, %shifted", "add %shifted, %maximum");
    for (const auto & body : {negativeLeftShift, minimumLeftShift}) {
        rows.push_back({.what = "structured signed left shifts include zero crossing and INT32_MIN",
                        .body = body,
                        .arrays = "a:[zero,one,zero,one]",
                        .reads = "a[0]=zero; a[2]=zero",
                        .exit = "a -> {a}"});
    }
    const auto negativeLeftReload =
        replace(negativeLeftShift, "%shifted = ctjs.binary_static shl %input, %one",
                "%count = ctjs.get_property %base[%one]\n"
                "    %shifted = ctjs.binary_static shl %input, %count");
    rows.push_back({.what = "structured signed left shifts retain disjoint count reloads",
                    .body = negativeLeftReload,
                    .arrays = "a:[zero,one,zero,one]",
                    .reads = "a[1]=one; a[0]=zero; a[1]=one; a[2]=zero",
                    .exit = "a -> {a}"});
    reject("structured signed left shifts refuse output underflow below INT32_MIN",
           replace(minimumLeftShift, "4742290407621132288", "4742290407625326592"));
    reject("structured signed left shifts retain the complete reload census",
           replace(negativeLeftReload,
                   "    %step =", "    ctjs.set_property %base[%one], %zero\n    %step ="));
    const auto positiveLeftBand = replace(
        replace(minimumLeftShift, "  %a =", "  %upper = ctjs.binary add %maximum, %half\n  %a ="),
        "sub %part, %half", "add %part, %upper");
    const auto negativeLeftBand = replace(
        replace(leftShiftIndex,
                "  %a =", "  %maximum = ctjs.constant #ctjs.number<4751297606873776128>\n  %a ="),
        "%position = ctjs.binary_static shl %part, %one",
        "%input = ctjs.binary sub %part, %maximum\n"
        "    %shifted = ctjs.binary_static shl %input, %one\n"
        "    %position = ctjs.binary sub %shifted, %two");
    for (const auto & body : {positiveLeftBand, negativeLeftBand}) {
        rows.push_back({.what = "structured left shifts preserve bounded ToInt32 conversion bands",
                        .body = body,
                        .arrays = "a:[zero,one,zero,one]",
                        .reads = "a[0]=zero; a[2]=zero",
                        .exit = "a -> {a}"});
    }
    const auto bandLeftReload =
        replace(negativeLeftBand, "%shifted = ctjs.binary_static shl %input, %one",
                "%count = ctjs.get_property %base[%one]\n"
                "    %shifted = ctjs.binary_static shl %input, %count");
    rows.push_back({.what = "structured left-shift conversion bands retain scaled reload gaps",
                    .body = bandLeftReload,
                    .arrays = "a:[zero,one,zero,one]",
                    .reads = "a[1]=one; a[0]=zero; a[1]=one; a[2]=zero",
                    .exit = "a -> {a}"});
    reject("structured left-shift conversion bands retain the complete reload census",
           replace(bandLeftReload,
                   "    %step =", "    ctjs.set_property %base[%one], %zero\n    %step ="));
    reject("structured positive left-shift bands still refuse output underflow",
           replace(positiveLeftBand, "add %part, %upper", "sub %upper, %part"));
    reject("structured negative left-shift bands still refuse output overflow",
           replace(negativeLeftBand, "4751297606873776128", "4749045807064285184"));
    reject("structured left-shift counts cannot overlap a later store",
           replace(leftShiftReload,
                   "    %step =", "    ctjs.set_property %base[%one], %zero\n    %step ="));
    reject("structured left shifts preserve integrality of every source intermediate",
           replace(leftShiftIndex, "add %i, %two\n    scf.yield", "add %i, %one\n    scf.yield"));
    reject("structured left-shift counts cannot borrow a changing value",
           replace(leftShiftIndex, "shl %part, %one", "shl %part, %last"));
    const auto positiveOutputBand =
        replace(minimumLeftShift, "sub %part, %half", "add %part, %half");
    const auto negativeOutputBand =
        replace(replace(minimumLeftShift, "sub %part, %half", "sub %part, %maximum"),
                "ctjs.binary add %shifted, %maximum", "ctjs.unary plus %shifted");
    for (const auto & body :
         {positiveOutputBand, negativeOutputBand,
          replace(negativeOutputBand, "sub %part, %maximum", "add %part, %maximum")}) {
        rows.push_back({.what = "structured left shifts preserve one signed output wrap band",
                        .body = body,
                        .arrays = "a:[zero,one,zero,one]",
                        .reads = "a[0]=zero; a[2]=zero",
                        .exit = "a -> {a}"});
    }
    const auto outputBandReload =
        replace(positiveOutputBand, "%shifted = ctjs.binary_static shl %input, %one",
                "%count = ctjs.get_property %base[%one]\n"
                "    %shifted = ctjs.binary_static shl %input, %count");
    rows.push_back({.what = "structured wrapped left-shift outputs preserve reload gaps",
                    .body = outputBandReload,
                    .arrays = "a:[zero,one,zero,one]",
                    .reads = "a[1]=one; a[0]=zero; a[1]=one; a[2]=zero",
                    .exit = "a -> {a}"});
    rows.push_back(
        {.what = "structured wrapped left-shift outputs retain unwritten children",
         .body = replace(positiveOutputBand, "[%y, %one, %y, %one]", "[%y, %y, %y, %one]"),
         .arrays = "a:[zero,y,zero,one]",
         .reads = "a[0]=zero; a[2]=zero",
         .exit = "a -> {a,y}"});
    auto wideOutputBand =
        replace(replace(positiveOutputBand, "  %a =",
                        "  %start = ctjs.constant #ctjs.number<4746794007235919872>\n"
                        "  %count = ctjs.constant #ctjs.number<4629418941960159232>\n  %a ="),
                "add %part, %half", "add %part, %start");
    wideOutputBand = replace(replace(wideOutputBand, "shl %input, %one", "shl %input, %count"),
                             "%position = ctjs.binary add %shifted, %maximum",
                             "%quotient = ctjs.binary div %shifted, %maximum\n"
                             "    %position = ctjs.binary add %quotient, %one");
    wideOutputBand = replace(wideOutputBand, "[%y, %one, %y, %one]", "[%y, %y, %one, %one]");
    for (const auto & body : {wideOutputBand, replace(replace(wideOutputBand, "4746794007235919872",
                                                              "4746794007244308480"),
                                                      "add %part, %start", "sub %part, %start")}) {
        rows.push_back({.what = "structured left-shift output bands keep exact wide products",
                        .body = body,
                        .arrays = "a:[zero,zero,one,one]",
                        .reads = "a[0]=zero; a[2]=one",
                        .exit = "a -> {a}"});
    }
    reject("structured wrapped left-shift counts cannot overlap a later store",
           replace(outputBandReload,
                   "    %step =", "    ctjs.set_property %base[%one], %zero\n    %step ="));
    reject("structured wrapped left-shift output must remain an own index",
           replace(positiveOutputBand, "add %shifted, %maximum", "add %shifted, %half"));
    rows.push_back(
        {.what = "full left-shift lattice includes an interior extremum with equal endpoints",
         .body =
             replace(replace(wideOutputBand, "ctjs.binary add %part, %start", "ctjs.unary plus %i"),
                     "add %i, %two\n    scf.yield", "add %i, %one\n    scf.yield"),
         .arrays = "a:[zero,zero,one,one]",
         .reads = "a[0]=y; a[1]=zero; a[2]=one; a[3]=one",
         .exit = "a -> {a}"});
    const auto rightShiftIndex =
        replace(quotientIndex, "ctjs.binary div %i, %two", "ctjs.binary_static shr %i, %one");
    for (const auto & kind : {"shr", "ushr"}) {
        const auto body =
            replace(rightShiftIndex, "binary_static shr", std::string{"binary_static "} + kind);
        rows.push_back({.what = "structured right shifts preserve exact own positions",
                        .body = body,
                        .arrays = "a:[zero,zero,one,one]",
                        .reads = "a[0]=zero; a[2]=one",
                        .exit = "a -> {a}"});
        rows.push_back({.what = "structured right shifts permit an unaligned first endpoint",
                        .body = replace(body, "%index = %zero", "%index = %one"),
                        .arrays = "a:[zero,zero,one,one]",
                        .reads = "a[1]=y; a[3]=one",
                        .exit = "a -> {a}"});
        rows.push_back(
            {.what = "structured right shifts replay repeated own positions",
             .body = replace(body, "add %i, %two\n    scf.yield", "add %i, %one\n    scf.yield"),
             .arrays = "a:[zero,zero,one,one]",
             .reads = "a[0]=zero; a[1]=y; a[2]=one; a[3]=one",
             .exit = "a -> {a}"});
    }
    const auto roundedShift = replace(
        replace(
            replace(rightShiftIndex, "[%y, %y, %one, %one]", "[%y, %three, %y, %one, %y, %one]"),
            "  %a =",
            "  %nine = ctjs.constant #ctjs.number<4621256167635550208>\n  %three = ctjs.constant "
            "#ctjs.number<4613937818241073152> {storage_test_id = \"three\"}\n  %a ="),
        "%position = ctjs.binary_static shr %i, %one",
        "%scaled = ctjs.binary mul %i, %nine\n"
        "    %count = ctjs.get_property %base[%one]\n"
        "    %position = ctjs.binary_static shr %scaled, %count");
    for (const auto & kind : {"shr", "ushr"}) {
        const auto body =
            replace(roundedShift, "binary_static shr", std::string{"binary_static "} + kind);
        rows.push_back(
            {.what = "equal rounded shift increments preserve sparse count reloads",
             .body = body,
             .arrays = "a:[zero,three,zero,one,zero,one]",
             .reads = "a[1]=three; a[0]=zero; a[1]=three; a[2]=zero; a[1]=three; a[4]=zero",
             .exit = "a -> {a}"});
        reject("rounded sparse shifts retain overlapping count guards",
               replace(body, "%base[%one]", "%base[%two]"));
        reject(
            "rounded sparse shifts retain the complete later-store census",
            replace(body, "    %step =", "    ctjs.set_property %base[%one], %two\n    %step ="));
        reject("mixed rounded increments cannot claim one sparse output stride",
               replace(body, "4621256167635550208", "4621819117588971520"));
    }
    const auto crossingShift = replace(replace(crossingComplement, "ctjs.unary bitnot %part",
                                               "ctjs.binary_static shr %part, %zero"),
                                       "ctjs.return %result", "ctjs.return %a");
    const auto lowerCrossingShift =
        replace(replace(negativeCrossingComplement, "ctjs.unary bitnot %part",
                        "ctjs.binary_static shr %part, %zero"),
                "ctjs.return %result", "ctjs.return %a");
    const auto zeroCrossingShift =
        replace(replace(crossingShift, "add %maximum, %i", "sub %i, %one"), "binary_static shr",
                "binary_static ushr");
    const auto outputCrossingShift =
        replace(replace(crossingShift, "%part = ctjs.binary add %maximum, %i",
                        "%half = ctjs.constant #ctjs.number<4742290407612743680>\n"
                        "    %quotient = ctjs.binary div %i, %two\n"
                        "    %part = ctjs.binary add %half, %quotient"),
                "shr %part, %zero", "shl %part, %one");
    for (const auto & body : {crossingShift, lowerCrossingShift, zeroCrossingShift,
                              replace(crossingShift, "binary_static shr", "binary_static shl"),
                              replace(lowerCrossingShift, "binary_static shr", "binary_static shl"),
                              outputCrossingShift}) {
        rows.push_back(
            {.what = "two-point shifts compose across input conversions and output wraps",
             .body = body,
             .arrays = "a:[zero,one,zero,one]",
             .reads = "a[0]=y; a[2]=zero",
             .exit = "a -> {a}"});
        rows.push_back(
            {.what = "two-point shift image gaps preserve reloaded masks",
             .body = replace(replace(body, "[%y, %one, %y, %one]", "[%y, %two, %y, %one]"),
                             "%position = ctjs.binary_static bitand %negative, %two",
                             "%mask = ctjs.get_property %base[%one]\n"
                             "    %position = ctjs.binary_static bitand %negative, %mask"),
             .arrays = "a:[zero,two,zero,one]",
             .reads = "a[1]=two; a[0]=y; a[1]=two; a[2]=zero",
             .exit = "a -> {a}"});
        const auto larger = replace(body, "[%y, %one, %y, %one]", "[%y, %one, %y, %one, %y, %one]");
        rows.push_back({.what = "larger shift enclosures preserve actual unwritten children",
                        .body = larger,
                        .arrays = "a:[zero,one,zero,one,y,one]",
                        .reads = "a[0]=y; a[2]=zero; a[4]=y",
                        .exit = "a -> {a,y}"});
    }
    const auto jumpShift =
        replace(replace(replace(crossingShift, "[%y, %one, %y, %one]",
                                "[%y, %y, %wideCount, %one, %one, %one]"),
                        "  %a =",
                        "  %wideCount = ctjs.constant #ctjs.number<4629418941960159232> "
                        "{storage_test_id = \"count\"}\n  %a ="),
                "%negative = ctjs.binary_static shr %part, %zero\n    %position = "
                "ctjs.binary_static bitand %negative, %two",
                "%count = ctjs.get_property %base[%two]\n"
                "    %negative = ctjs.binary_static shr %part, %count\n"
                "    %position = ctjs.binary add %negative, %one");
    const auto lowerJumpShift =
        replace(replace(jumpShift, "4746794007244308480", "4746794007250599936"),
                "add %maximum, %i", "sub %i, %maximum");
    const auto unsignedJumpShift =
        replace(replace(replace(jumpShift, "add %maximum, %i", "sub %i, %one"), "binary_static shr",
                        "binary_static ushr"),
                "ctjs.binary add %negative, %one", "ctjs.unary plus %negative");
    for (const auto & body : {jumpShift, lowerJumpShift, unsignedJumpShift,
                              replace(jumpShift, "4629418941960159232", "4634063279075885056"),
                              replace(jumpShift, "4629418941960159232", "13830554455654793216")}) {
        rows.push_back(
            {.what = "larger conversion jumps use count-specific full right-shift bounds",
             .body = body,
             .arrays = "a:[zero,zero,count,one,one,one]",
             .reads = "a[2]=count; a[0]=y; a[2]=count; a[2]=count; a[2]=count; a[4]=one",
             .exit = "a -> {a}"});
        reject(
            "conversion-jump right shifts retain the full later-store census",
            replace(body, "    %step =", "    ctjs.set_property %base[%two], %one\n    %step ="));
        reject("conversion-jump right shifts cannot reload an actual written position",
               replace(replace(body, "[%y, %y, %wideCount, %one, %one, %one]",
                               "[%wideCount, %y, %wideCount, %one, %one, %one]"),
                       "%base[%two]", "%base[%zero]"));
    }
    const auto interiorSignedShift = replace(
        replace(lowerJumpShift,
                "  %a =", "  %scale = ctjs.constant #ctjs.number<4742290407612743680>\n  %a ="),
        "%part = ctjs.binary sub %i, %maximum",
        "%scaled = ctjs.binary mul %i, %scale\n    %part = ctjs.binary sub %scaled, %maximum");
    const auto interiorUnsignedShift = replace(
        replace(unsignedJumpShift,
                "  %a =", "  %scale = ctjs.constant #ctjs.number<4742290407612743680>\n  %a ="),
        "%part = ctjs.binary sub %i, %one",
        "%scaled = ctjs.binary mul %i, %scale\n    %part = ctjs.binary sub %scaled, %one");
    for (const auto & body : {interiorSignedShift, interiorUnsignedShift}) {
        rows.push_back(
            {.what = "equal converted endpoints do not hide an interior right-shift extremum",
             .body = body,
             .arrays = "a:[zero,zero,count,one,one,one]",
             .reads = "a[2]=count; a[0]=y; a[2]=count; a[2]=count; a[2]=count; a[4]=one",
             .exit = "a -> {a}"});
    }
    rows.push_back({.what = "full shift enclosures never erase unvisited retained children",
                    .body = replace(jumpShift, "[%y, %y, %wideCount, %one, %one, %one]",
                                    "[%y, %y, %wideCount, %one, %y, %one]"),
                    .arrays = "a:[zero,zero,count,one,y,one]",
                    .reads = "a[2]=count; a[0]=y; a[2]=count; a[2]=count; a[2]=count; a[4]=y",
                    .exit = "a -> {a,y}"});
    rows.push_back(
        {.what = "conversion-jump replay preserves an earlier child snapshot",
         .body = replace(replace(jumpShift, "  %finalIndex,",
                                 "  %before = ctjs.get_property %a[%zero]\n  %finalIndex,"),
                         "ctjs.return %a", "ctjs.return %before"),
         .arrays = "a:[zero,zero,count,one,one,one]",
         .reads = "a[0]=y; a[2]=count; a[0]=y; a[2]=count; a[2]=count; a[2]=count; a[4]=one",
         .exit = "y -> {y}"});
    reject("full signed right-shift bounds still require nonnegative own positions",
           replace(jumpShift, "ctjs.binary add %negative, %one", "ctjs.unary plus %negative"));
    const auto leftJumpShift = replace(
        replace(replace(jumpShift, "  %a =",
                        "  %divisor = ctjs.constant #ctjs.number<4746794007248502784>\n  %a ="),
                "%part = ctjs.binary add %maximum, %i",
                "%halfIndex = ctjs.binary div %i, %two\n    %part = ctjs.binary add %maximum, "
                "%halfIndex"),
        "%negative = ctjs.binary_static shr %part, %count\n    %position = ctjs.binary add "
        "%negative, %one",
        "%negative = ctjs.binary_static shl %part, %count\n"
        "    %quotient = ctjs.binary div %negative, %divisor\n"
        "    %position = ctjs.binary add %quotient, %one");
    const auto lowerLeftJumpShift =
        replace(replace(leftJumpShift, "4746794007244308480", "4746794007250599936"),
                "add %maximum, %halfIndex", "sub %halfIndex, %maximum");
    for (const auto & body :
         {leftJumpShift, lowerLeftJumpShift,
          replace(leftJumpShift, "4629418941960159232", "4634063279075885056"),
          replace(leftJumpShift, "4629418941960159232", "13830554455654793216")}) {
        rows.push_back(
            {.what =
                 "left-shift conversion jumps retain scaled residues and disjoint count reloads",
             .body = body,
             .arrays = "a:[zero,zero,count,one,one,one]",
             .reads = "a[2]=count; a[0]=zero; a[2]=count; a[2]=count; a[2]=count; a[4]=one",
             .exit = "a -> {a}"});
        reject(
            "left-shift jump lattices retain every later count store",
            replace(body, "    %step =", "    ctjs.set_property %base[%two], %one\n    %step ="));
        reject("left-shift jump lattices cannot reload an actual written position",
               replace(replace(body, "[%y, %y, %wideCount, %one, %one, %one]",
                               "[%wideCount, %y, %wideCount, %one, %one, %one]"),
                       "%base[%two]", "%base[%zero]"));
    }
    const auto outputJumpShift = replace(leftJumpShift, "ctjs.binary add %maximum, %halfIndex",
                                         "ctjs.unary plus %halfIndex");
    rows.push_back(
        {.what = "larger left-shift output wraps retain interior visits with equal endpoint images",
         .body = outputJumpShift,
         .arrays = "a:[zero,zero,count,one,one,one]",
         .reads = "a[2]=count; a[0]=y; a[2]=count; a[2]=count; a[2]=count; a[4]=one",
         .exit = "a -> {a}"});
    rows.push_back({.what = "left-shift output periods retain fixed low input bits",
                    .body = replace(replace(outputJumpShift, "ctjs.unary plus %halfIndex",
                                            "ctjs.unary plus %i"),
                                    "4629418941960159232", "4629137466983448576"),
                    .arrays = "a:[zero,zero,count,one,one,one]",
                    .reads = "a[2]=count; a[0]=y; a[2]=count; a[2]=count; a[2]=count; a[4]=one",
                    .exit = "a -> {a}"});
    rows.push_back(
        {.what = "an even input residue collapses every wide left-shift output to zero",
         .body = replace(
             replace(outputJumpShift, "ctjs.unary plus %halfIndex", "ctjs.unary plus %i"),
             "[%y, %y, %wideCount, %one, %one, %one]", "[%zero, %y, %wideCount, %one, %one, %one]"),
         .arrays = "a:[zero,zero,count,one,one,one]",
         .reads = "a[2]=count; a[0]=zero; a[2]=count; a[2]=count; a[2]=count; a[4]=one",
         .exit = "a -> {a}"});
    rows.push_back(
        {.what = "an odd input residue collapses every wide left-shift output to INT32_MIN",
         .body = replace(
             replace(outputJumpShift, "ctjs.unary plus %halfIndex", "ctjs.binary add %i, %one"),
             "[%y, %y, %wideCount, %one, %one, %one]", "[%y, %one, %wideCount, %one, %one, %one]"),
         .arrays = "a:[zero,one,count,one,one,one]",
         .reads = "a[2]=count; a[0]=zero; a[2]=count; a[2]=count; a[2]=count; a[4]=one",
         .exit = "a -> {a}"});
    rows.push_back({.what = "left-shift enclosures retain a child outside every actual write",
                    .body = replace(leftJumpShift, "[%y, %y, %wideCount, %one, %one, %one]",
                                    "[%y, %y, %wideCount, %one, %y, %one]"),
                    .arrays = "a:[zero,zero,count,one,y,one]",
                    .reads = "a[2]=count; a[0]=zero; a[2]=count; a[2]=count; a[2]=count; a[4]=y",
                    .exit = "a -> {a,y}"});
    rows.push_back(
        {.what = "left-shift jump replay preserves an earlier child snapshot",
         .body = replace(replace(leftJumpShift, "  %finalIndex,",
                                 "  %before = ctjs.get_property %a[%zero]\n  %finalIndex,"),
                         "ctjs.return %a", "ctjs.return %before"),
         .arrays = "a:[zero,zero,count,one,one,one]",
         .reads = "a[0]=y; a[2]=count; a[0]=zero; a[2]=count; a[2]=count; a[2]=count; a[4]=one",
         .exit = "y -> {y}"});
    reject("full left-shift bounds still require nonnegative own indices",
           replace(leftJumpShift, "ctjs.binary add %quotient, %one", "ctjs.unary plus %quotient"));
    const auto crossingShiftReload =
        replace(replace(crossingShift, "[%y, %one, %y, %one]", "[%y, %zero, %y, %one]"),
                "%negative = ctjs.binary_static shr %part, %zero",
                "%count = ctjs.get_property %base[%one]\n"
                "    %negative = ctjs.binary_static shr %part, %count");
    rows.push_back({.what = "two-point shifts preserve exact count reloads across conversion jumps",
                    .body = crossingShiftReload,
                    .arrays = "a:[zero,zero,zero,one]",
                    .reads = "a[1]=zero; a[0]=y; a[1]=zero; a[2]=zero",
                    .exit = "a -> {a}"});
    reject("two-point shifts retain the full later-store count census",
           replace(crossingShiftReload,
                   "    %step =", "    ctjs.set_property %base[%one], %one\n    %step ="));
    reject("two-point shifts cannot reload a count at an actual endpoint",
           replace(replace(crossingShiftReload, "[%y, %zero, %y, %one]", "[%zero, %one, %y, %one]"),
                   "%base[%one]", "%base[%zero]"));
    rows.push_back({.what = "two-point shifts retain children outside their exact write lattice",
                    .body = replace(crossingShift, "[%y, %one, %y, %one]", "[%y, %y, %y, %one]"),
                    .arrays = "a:[zero,y,zero,one]",
                    .reads = "a[0]=y; a[2]=zero",
                    .exit = "a -> {a,y}"});
    rows.push_back(
        {.what = "two-point shifts preserve an earlier child snapshot",
         .body = replace(replace(crossingShift, "  %finalIndex,",
                                 "  %before = ctjs.get_property %a[%zero]\n  %finalIndex,"),
                         "ctjs.return %a", "ctjs.return %before"),
         .arrays = "a:[zero,one,zero,one]",
         .reads = "a[0]=y; a[0]=y; a[2]=zero",
         .exit = "y -> {y}"});
    rows.push_back({.what = "two-value shift inputs may arise from four loop visits",
                    .body = replace(replace(crossingShift, "%part = ctjs.binary add %maximum, %i",
                                            "%bucket = ctjs.binary_static bitand %i, %two\n"
                                            "    %part = ctjs.binary add %maximum, %bucket"),
                                    "add %i, %two\n    scf.yield", "add %i, %one\n    scf.yield"),
                    .arrays = "a:[zero,one,zero,one]",
                    .reads = "a[0]=y; a[1]=one; a[2]=zero; a[3]=one",
                    .exit = "a -> {a}"});
    const auto twoPointUnevenShift = replace(
        replace(
            replace(rightShiftIndex, "[%y, %y, %one, %one]", "[%y, %one, %y, %one, %one, %one]"),
            "  %a =", "  %five = ctjs.constant #ctjs.number<4617315517961601024>\n  %a ="),
        "add %i, %two\n    scf.yield", "add %i, %five\n    scf.yield");
    const auto twoPointUnevenReload =
        replace(twoPointUnevenShift, "%position = ctjs.binary_static shr %i, %one",
                "%count = ctjs.get_property %base[%one]\n"
                "    %position = ctjs.binary_static shr %i, %count");
    for (const auto & kind : {"shr", "ushr"}) {
        rows.push_back({.what = "two-point right shifts retain gaps for uneven input strides",
                        .body = replace(twoPointUnevenReload, "binary_static shr",
                                        std::string{"binary_static "} + kind),
                        .arrays = "a:[zero,one,zero,one,one,one]",
                        .reads = "a[1]=one; a[0]=zero; a[1]=one; a[5]=one",
                        .exit = "a -> {a}"});
    }
    reject("two-point uneven shift gaps still require disjoint endpoint reads",
           replace(replace(twoPointUnevenReload, "[%y, %one, %y, %one, %one, %one]",
                           "[%one, %one, %y, %one, %one, %one]"),
                   "%base[%one]", "%base[%zero]"));
    reject("two-point uneven shifts retain the complete later-store census",
           replace(twoPointUnevenReload,
                   "    %step =", "    ctjs.set_property %base[%one], %two\n    %step ="));
    const auto shiftReload =
        replace(replace(rightShiftIndex, "  %a =", "  %three = ctjs.binary add %two, %one\n  %a ="),
                "    %position = ctjs.binary_static shr %i, %one",
                "    %count = ctjs.get_property %base[%three]\n"
                "    %position = ctjs.binary_static shr %i, %count");
    rows.push_back({.what = "structured right-shift counts may reload beyond transformed writes",
                    .body = shiftReload,
                    .arrays = "a:[zero,zero,one,one]",
                    .reads = "a[3]=one; a[0]=zero; a[3]=one; a[2]=one",
                    .exit = "a -> {a}"});
    const auto signedShiftBoundary =
        replace(replace(rightShiftIndex, "  %a =",
                        "  %maximum = ctjs.constant #ctjs.number<4746794007244308480>\n"
                        "  %half = ctjs.constant #ctjs.number<4742290407612743680>\n  %a ="),
                "%position = ctjs.binary_static shr %i, %one",
                "%part = ctjs.binary sub %maximum, %i\n"
                "    %shifted = ctjs.binary_static shr %part, %one\n"
                "    %position = ctjs.binary sub %half, %shifted");
    rows.push_back({.what = "structured signed shifts include the exact upper i32 endpoint",
                    .body = signedShiftBoundary,
                    .arrays = "a:[zero,zero,one,one]",
                    .reads = "a[0]=zero; a[2]=one",
                    .exit = "a -> {a}"});
    reject("structured signed shifts refuse a ToInt32 discontinuity",
           replace(signedShiftBoundary, "4746794007244308480", "4746794007248502784"));
    reject("structured right shifts retain the complete reload overlap check",
           replace(shiftReload,
                   "    %step =", "    ctjs.set_property %base[%three], %two\n    %step ="));
    reject("structured right shifts cannot borrow a changing count",
           replace(rightShiftIndex, "shr %i, %one", "shr %i, %last"));
    const auto negativeShift = replace(
        replace(replace(replace(signedShiftBoundary, "4746794007244308480", "4616189618054758400"),
                        "4742290407612743680", "4746794007240114176"),
                "sub %maximum, %i", "sub %i, %maximum"),
        "binary_static shr", "binary_static ushr");
    const auto negativeShiftForward =
        replace(negativeShift, "sub %half, %shifted", "sub %shifted, %half");
    const auto negativeShiftBoundary =
        replace(replace(negativeShiftForward, "4616189618054758400", "4751297606873776128"),
                "sub %shifted, %half", "sub %shifted, %zero");
    for (const auto & body : {negativeShiftForward, negativeShiftBoundary}) {
        rows.push_back({.what = "structured unsigned shifts preserve one bounded negative band",
                        .body = body,
                        .arrays = "a:[zero,zero,one,one]",
                        .reads = "a[0]=zero; a[2]=one",
                        .exit = "a -> {a}"});
    }
    const auto negativeShiftReload = replace(
        replace(negativeShiftForward, "  %a =", "  %three = ctjs.binary add %two, %one\n  %a ="),
        "    %shifted = ctjs.binary_static ushr %part, %one",
        "    %count = ctjs.get_property %base[%three]\n"
        "    %shifted = ctjs.binary_static ushr %part, %count");
    rows.push_back({.what = "structured negative unsigned shift counts retain disjoint reloads",
                    .body = negativeShiftReload,
                    .arrays = "a:[zero,zero,one,one]",
                    .reads = "a[3]=one; a[0]=zero; a[3]=one; a[2]=one",
                    .exit = "a -> {a}"});
    for (const auto & body :
         {replace(negativeShiftForward, "sub %i, %maximum", "sub %i, %two"),
          replace(negativeShiftBoundary, "4751297606873776128", "4751297606875873280"),
          replace(negativeShiftReload, "%base[%three]", "%base[%one]"),
          replace(negativeShiftReload,
                  "    %step =", "    ctjs.set_property %base[%three], %two\n    %step =")}) {
        reject("structured negative unsigned shifts retain band and reload guards", body);
    }
    const auto signedNegative =
        replace(replace(negativeShiftForward, "binary_static ushr", "binary_static shr"),
                "sub %shifted, %half", "add %shifted, %two");
    const auto signedMinimum =
        replace(replace(replace(signedNegative, "4616189618054758400", "4746794007248502784"),
                        "4746794007240114176", "4742290407621132288"),
                "add %shifted, %two", "add %shifted, %half");
    const auto signedZeroCrossing =
        replace(replace(signedNegative, "sub %i, %maximum", "sub %i, %one"), "add %shifted, %two",
                "add %shifted, %one");
    for (const auto & body :
         {signedNegative, signedMinimum, signedZeroCrossing,
          replace(signedMinimum, "sub %i, %maximum", "add %i, %maximum"),
          replace(negativeShiftBoundary, "binary_static ushr", "binary_static shr")}) {
        rows.push_back({.what = "structured signed shifts retain one bounded ToInt32 band",
                        .body = body,
                        .arrays = "a:[zero,zero,one,one]",
                        .reads = "a[0]=zero; a[2]=one",
                        .exit = "a -> {a}"});
    }
    const auto signedNegativeReload =
        replace(replace(negativeShiftReload, "binary_static ushr", "binary_static shr"),
                "sub %shifted, %half", "add %shifted, %two");
    rows.push_back({.what = "structured signed shift bands preserve disjoint count reloads",
                    .body = signedNegativeReload,
                    .arrays = "a:[zero,zero,one,one]",
                    .reads = "a[3]=one; a[0]=zero; a[3]=one; a[2]=one",
                    .exit = "a -> {a}"});
    for (const auto & body : {replace(signedMinimum, "4746794007248502784", "4746794007250599936"),
                              replace(signedNegativeReload, "%base[%three]", "%base[%one]"),
                              replace(signedNegativeReload, "    %step =",
                                      "    ctjs.set_property %base[%three], %two\n    %step =")}) {
        reject("structured signed shift bands retain discontinuity and reload guards", body);
    }
    for (const auto & body : {negativeShiftForward, signedNegative}) {
        rows.push_back(
            {.what = "structured dense shifts retain negative conversion bands",
             .body = replace(body, "add %i, %two\n    scf.yield", "add %i, %one\n    scf.yield"),
             .arrays = "a:[zero,zero,one,one]",
             .reads = "a[0]=zero; a[1]=y; a[2]=one; a[3]=one",
             .exit = "a -> {a}"});
    }
    const auto unevenShift =
        replace(replace(replace(rightShiftIndex, "[%y, %y, %one, %one]",
                                "[%y, %y, %y, %y, %one, %one, %one]"),
                        "  %a =", "  %three = ctjs.binary add %two, %one\n  %a ="),
                "add %i, %two\n    scf.yield", "add %i, %three\n    scf.yield");
    rows.push_back({.what = "structured dense footprints preserve unwritten gap children",
                    .body = unevenShift,
                    .arrays = "a:[zero,zero,y,zero,one,one,one]",
                    .reads = "a[0]=zero; a[3]=y; a[6]=one",
                    .exit = "a -> {a,y}"});
    rows.push_back({.what = "structured dense right shifts retain reversed footprints",
                    .body = replace(unevenShift, "%position = ctjs.binary_static shr %i, %one",
                                    "%part = ctjs.binary_static shr %i, %one\n"
                                    "    %position = ctjs.binary sub %three, %part"),
                    .arrays = "a:[zero,y,zero,zero,one,one,one]",
                    .reads = "a[0]=y; a[3]=zero; a[6]=one",
                    .exit = "a -> {a,y}"});
    reject("structured dense shift footprints refuse reloads in unproved sparse gaps",
           replace(replace(unevenShift, "[%y, %y, %y, %y, %one, %one, %one]",
                           "[%y, %y, %one, %y, %one, %one, %one]"),
                   "%position = ctjs.binary_static shr %i, %one",
                   "%count = ctjs.get_property %base[%two]\n"
                   "    %position = ctjs.binary_static shr %i, %count"));
    reject("structured dense shift endpoints cannot prove integral quotients",
           replace(unevenShift, "%position = ctjs.binary_static shr %i, %one",
                   "%part = ctjs.binary_static shr %i, %one\n"
                   "    %position = ctjs.binary div %part, %two"));
    rows.push_back({.what = "structured exact quotients overwrite their bounded prefix",
                    .body = quotientIndex,
                    .arrays = "a:[zero,zero,one,one]",
                    .reads = "a[0]=zero; a[2]=one",
                    .exit = "a -> {a}"});
    rows.push_back({.what = "structured nonzero quotient starts retain their actual positions",
                    .body = replace(replace(quotientIndex, "[%y, %y, %one, %one]",
                                            "[%one, %y, %y, %one, %one, %one]"),
                                    "%index = %zero", "%index = %two"),
                    .arrays = "a:[one,zero,zero,one,one,one]",
                    .reads = "a[2]=y; a[4]=one",
                    .exit = "a -> {a}"});
    const auto quotientGap = replace(
        replace(replace(quotientIndex, "[%y, %y, %one, %one]", "[%y, %two, %y, %one, %one]"),
                "  %a =", "  %four = ctjs.binary add %two, %two\n  %a ="),
        "    %position = ctjs.binary div %i, %two",
        "    %divisor = ctjs.get_property %base[%one]\n"
        "    %position = ctjs.binary div %i, %divisor");
    rows.push_back({.what = "structured quotient footprints preserve reloads between writes",
                    .body = replace(quotientGap, "    %step = ctjs.binary_static add %i, %two",
                                    "    %step = ctjs.binary_static add %i, %four"),
                    .arrays = "a:[zero,two,zero,one,one]",
                    .reads = "a[1]=two; a[0]=zero; a[1]=two; a[4]=one",
                    .exit = "a -> {a}"});
    rows.push_back({.what = "structured quotient overwrites preserve saved child identities",
                    .body = replace(replace(quotientIndex, "  %finalIndex,",
                                            "  %held = ctjs.get_property %a[%one]\n  %finalIndex,"),
                                    "ctjs.return %a", "ctjs.return %held"),
                    .arrays = "a:[zero,zero,one,one]",
                    .reads = "a[1]=y; a[0]=zero; a[2]=one",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "structured zero-trip quotient overwrites retain original children",
                    .body = replace(replace(quotientIndex, "[%y, %y, %one, %one]", "[%y, %y]"),
                                    "%index = %zero", "%index = %two"),
                    .arrays = "a:[y,y]",
                    .exit = "a -> {a,y}"});
    for (const auto & body :
         {replace(quotientIndex, "%index = %zero", "%index = %one"),
          replace(replace(quotientIndex, "[%y, %y, %one, %one]", "[%y, %y, %one]"),
                  "    %step = ctjs.binary_static add %i, %two",
                  "    %step = ctjs.binary_static add %i, %one"),
          replace(quotientIndex, "div %i, %two", "div %i, %zero"),
          replace(quotientIndex, "div %i, %two", "div %two, %i"), quotientGap,
          replace(quotientGap,
                  "    %step =", "    ctjs.set_property %base[%one], %one\n    %step ="),
          replace(quotientIndex, "div %i, %two", "div %i, %last")}) {
        reject("structured quotient stores require exact division and an invariant divisor", body);
    }
    const auto disjointReload =
        replace(replace(replace(reloaded, "  %finalIndex,",
                                "  %seed = ctjs.create_array [%one] {storage_test_id = \"seed\"}\n"
                                "  %finalIndex,"),
                        "    %unit =", "    ctjs.set_property %base[%i], %zero\n    %unit ="),
                "%unit = ctjs.get_property %base[%zero]", "%unit = ctjs.get_property %seed[%zero]");
    rows.push_back({.what = "structured disjoint stride reads preserve a saved child",
                    .body = disjointReload,
                    .arrays = "a:[zero,zero]; seed:[one]",
                    .reads = "a[0]=one; seed[0]=one; a[1]=y; seed[0]=one",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "structured disjoint stride reads release overwritten children",
                    .body = replace(disjointReload, "ctjs.return %result", "ctjs.return %a"),
                    .arrays = "a:[zero,zero]; seed:[one]",
                    .reads = "a[0]=one; seed[0]=one; a[1]=y; seed[0]=one",
                    .exit = "a -> {a}"});
    const auto savedReceiver =
        replace(replace(disjointReload, "  %finalIndex,",
                        "  %box = ctjs.create_array [%a] {storage_test_id = \"box\"}\n"
                        "  %alias = ctjs.get_property %box[%zero]\n  %finalIndex,"),
                "set_property %base[", "set_property %alias[");
    rows.push_back({.what = "structured saved receiver aliases release overwritten children",
                    .body = replace(savedReceiver, "ctjs.return %result", "ctjs.return %a"),
                    .arrays = "a:[zero,zero]; seed:[one]; box:[a]",
                    .reads = "box[0]=a; a[0]=one; seed[0]=one; a[1]=y; seed[0]=one",
                    .exit = "a -> {a}"});
    const auto reloadedReceiver =
        replace(replace(savedReceiver, "  %alias = ctjs.get_property %box[%zero]\n", ""),
                "    ctjs.set_property %alias[",
                "    %alias = ctjs.get_property %box[%zero]\n    ctjs.set_property %alias[");
    rows.push_back({.what = "structured disjoint receiver reloads preserve a saved child",
                    .body = reloadedReceiver,
                    .arrays = "a:[zero,zero]; seed:[one]; box:[a]",
                    .reads = "a[0]=one; box[0]=a; seed[0]=one; a[1]=y; box[0]=a; seed[0]=one",
                    .exit = "y -> {y}"});
    reject("structured unrelated receiver aliases cannot borrow the loop bound",
           replace(savedReceiver, "create_array [%a]", "create_array [%seed]"));
    reject("structured receiver reloads cannot use the changing induction index",
           replace(reloadedReceiver, "%box[%zero]", "%box[%i]"));
    reject("structured receiver proof cannot reload overwritten storage",
           replace(reloadedReceiver, "%alias = ctjs.get_property %box[%zero]",
                   "%alias = ctjs.get_property %base[%zero]"));
    const auto nestedReload = replace(
        replace(replace(disjointReload, "  %finalIndex,",
                        "  %box = ctjs.create_array [%seed, %a] {storage_test_id = \"box\"}\n"
                        "  %finalIndex,"),
                "    %unit =", "    %input = ctjs.get_property %box[%zero]\n    %unit ="),
        "%seed[%zero]", "%input[%zero]");
    rows.push_back({.what = "structured nested stride reads keep exact selected origins",
                    .body = replace(nestedReload, "ctjs.return %result", "ctjs.return %a"),
                    .arrays = "a:[zero,zero]; seed:[one]; box:[seed,a]",
                    .reads = "a[0]=one; box[0]=seed; seed[0]=one; a[1]=y; box[0]=seed; seed[0]=one",
                    .exit = "a -> {a}"});
    reject("structured nested reads cannot conceal the overwritten guard allocation",
           replace(nestedReload, "[%seed, %a]", "[%a, %seed]"));
    reject("structured saved array aliases retain their original allocation",
           replace(replace(disjointReload, "  %finalIndex,",
                           "  %box = ctjs.create_array [%a]\n"
                           "  %alias = ctjs.get_property %box[%zero]\n  %finalIndex,"),
                   "%seed[%zero]", "%alias[%zero]"));
    for (const std::string key : {"#ctjs.string<\"00\">", "#ctjs.string<\"-0\">",
                                  "#ctjs.boolean<false>", "#ctjs.number<4613937818241073152>"}) {
        reject("structured reload cannot coerce or inherit an absent own key",
               replace(replace(reloaded,
                               "    %unit =", "    %bad = ctjs.constant " + key + "\n    %unit ="),
                       "%base[%zero]", "%base[%bad]"));
    }
    reject("structured reload cannot borrow a changing index",
           replace(reloaded, "%base[%zero]", "%base[%i]"));
    reject(
        "structured reload cannot overlook loop mutation",
        replace(reloaded, "    %unit =", "    ctjs.set_property %base[%zero], %zero\n    %unit ="));
    const auto lengthReload =
        replace(replace(original, "    %step =",
                        "    %name = ctjs.constant #ctjs.string<\"length\">\n"
                        "    %size = ctjs.get_property %base[%name]\n"
                        "    %unit = ctjs.binary sub %size, %one\n    %step ="),
                "add %i, %one", "add %i, %unit");
    rows.push_back({.what = "a structured own-length reload preserves the returned child",
                    .body = lengthReload,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "a structured own-length reload releases discarded children",
                    .body = replace(lengthReload, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    rows.push_back({.what = "structured length strides retain the visited child's identity",
                    .body = replace(lengthReload, "binary sub %size, %one", "unary plus %size"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    for (const std::string mutation :
         {"ctjs.set_property %base[%name], %one", "ctjs.set_property %base[%zero], %one",
          "ctjs.append %one to %base"}) {
        const auto body = replace(lengthReload, "    %size =", "    " + mutation + "\n    %size =");
        if (mutation == "ctjs.set_property %base[%zero], %one") {
            rows.push_back({.what = "structured own-length strides survive own-element writes",
                            .body = body,
                            .arrays = "a:[one,y]",
                            .reads = "a[0]=x; a[1]=y",
                            .exit = "y -> {y}"});
        } else {
            reject("structured own lengths cannot bypass the complete mutation census", body);
        }
    }
    reject("a structured lookalike key cannot become an own length",
           replace(lengthReload, "%name = ctjs.constant #ctjs.string<\"length\">",
                   "%name = ctjs.constant #ctjs.string<\"length \">"));
    reject("a structured own-length result cannot certify a zero stride",
           replace(lengthReload, "binary sub %size, %one", "binary sub %size, %size"));
    reject("a structured own-length reload must preserve the final-index bound",
           replace(lengthReload, "    %unit = ctjs.binary sub %size, %one",
                   "    %max = ctjs.constant #ctjs.number<4751297606873776128>\n"
                   "    %unit = ctjs.binary add %size, %max"));
    const auto stringLength =
        replace(replace(original, "    %step =",
                        "    %text = ctjs.constant #ctjs.string<\"a\">\n"
                        "    %name = ctjs.constant #ctjs.string<\"length\">\n"
                        "    %unit = ctjs.get_property %text[%name]\n    %step ="),
                "add %i, %one", "add %i, %unit");
    rows.push_back({.what = "original ASCII String length is an invariant Number stride",
                    .body = stringLength,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "an ASCII length stride preserves discarded child confinement",
                    .body = replace(stringLength, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    for (const std::string & text : {std::string{}, std::string{"é"}, std::string(257, 'a')}) {
        reject("String length refuses zero strides, Unicode and its scan ceiling",
               replace(stringLength, "#ctjs.string<\"a\">", "#ctjs.string<\"" + text + "\">"));
    }
    const auto stringIndex =
        replace(replace(stringLength, "#ctjs.string<\"a\">", "#ctjs.string<\"1\">"),
                "    %unit = ctjs.get_property %text[%name]",
                "    %character = ctjs.get_property %text[%zero]\n"
                "    %unit = ctjs.unary plus %character");
    rows.push_back({.what = "original ASCII Number-key reads supply invariant digit strides",
                    .body = stringIndex,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "String index strides discharge only discarded children",
                    .body = replace(stringIndex, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    rows.push_back({.what = "indexed String snapshots retain their own length",
                    .body = replace(stringIndex, "ctjs.unary plus %character",
                                    "ctjs.get_property %character[%name]"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    const auto stringArrayReload =
        replace(replace(replace(stringIndex, "[%x]", "[%one]"), "#ctjs.string<\"1\">",
                        "#ctjs.string<\"0\">"),
                "ctjs.unary plus %character", "ctjs.get_property %base[%character]");
    rows.push_back({.what = "structured digit snapshots select exact invariant own-array keys",
                    .body = stringArrayReload,
                    .arrays = "a:[one,y]",
                    .reads = "a[0]=one; a[0]=one; a[1]=y; a[0]=one",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "structured digit-key reloads discharge only discarded children",
                    .body = replace(stringArrayReload, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[one,y]",
                    .reads = "a[0]=one; a[0]=one; a[1]=y; a[0]=one",
                    .exit = "zero -> {}"});
    for (const std::string text : {" ", "x", "9"}) {
        reject("structured digit keys require an existing canonical own-array element",
               replace(stringArrayReload, "#ctjs.string<\"0\">", "#ctjs.string<\"" + text + "\">"));
    }
    reject("structured digit keys cannot borrow a changing String index",
           replace(stringArrayReload, "%text[%zero]", "%text[%i]"));
    reject("structured digit-key reloads cannot bypass the read-only loop census",
           replace(stringArrayReload,
                   "    %unit =", "    ctjs.set_property %base[%character], %one\n    %unit ="));
    reject(
        "structured digit snapshots remain String keys when indexing another String",
        replace(stringIndex, "ctjs.unary plus %character", "ctjs.get_property %text[%character]"));
    for (const std::string key : {"#ctjs.string<\"0\">", "#ctjs.bigint<\"0\">",
                                  "#ctjs.boolean<false>", "#ctjs.number<4607182418800017408>"}) {
        reject("String reads require an in-range original Number key",
               replace(replace(stringIndex, "    %character =",
                               "    %bad = ctjs.constant " + key + "\n    %character ="),
                       "%text[%zero]", "%text[%bad]"));
    }
    for (const std::string & text : {std::string{}, std::string{"é"}, std::string(257, '1')}) {
        reject("String indexed snapshots retain ASCII and size ceilings",
               replace(stringIndex, "#ctjs.string<\"1\">", "#ctjs.string<\"" + text + "\">"));
    }
    reject("String indices cannot borrow a changing induction value",
           replace(stringIndex, "%text[%zero]", "%text[%i]"));
    reject("String character conversion cannot turn String Add into Number Add",
           replace(stringIndex, "add %i, %unit", "add %i, %character"));
    rows.push_back({.what = "structured String indices survive invariant own-element writes",
                    .body = replace(stringIndex, "    %character =",
                                    "    ctjs.set_property %base[%zero], %one\n    %character ="),
                    .arrays = "a:[one,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    reject("String length requires its original exact key",
           replace(stringLength, "%text[%name]", "%text[%zero]"));
    reject("computed Strings cannot borrow literal length provenance",
           replace(stringLength, "ctjs.constant #ctjs.string<\"a\">", "ctjs.unary typeof %one"));
    rows.push_back({.what = "structured String lengths survive invariant own-element writes",
                    .body = replace(stringLength, "    %unit =",
                                    "    ctjs.set_property %base[%zero], %one\n    %unit ="),
                    .arrays = "a:[one,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    reject("structured induction refuses a computed negative start",
           replace(computedStart, "unary plus %zero", "unary neg %one"));
    reject("structured induction cannot convert a String start into a Number proof",
           replace(computedStart, "ctjs.unary plus %zero", "ctjs.constant #ctjs.string<\"0\">"));
    reject("a structured initializer cannot borrow another path's nonnegative Number",
           replace(alternateStart, "unary plus %zero", "unary neg %one"));
    reject("a structured start beyond the bounded Number range remains unproved",
           replace(computedStart, "ctjs.unary plus %zero",
                   "ctjs.constant #ctjs.number<4751297606875873280>"));
    reject("a structured nonzero start cannot overflow on its last stride update",
           replace(maxStride, "%index = %zero", "%index = %one"));
    reject("structured induction refuses an exact computed zero step",
           replace(computedUnit, "unary plus %one", "unary plus %zero"));
    reject("a structured String step cannot become a proved Number one",
           replace(computedUnit, "ctjs.unary plus %one", "ctjs.constant #ctjs.string<\"1\">"));
    reject("a structured BigInt step cannot borrow its Number spelling",
           replace(computedUnit, "ctjs.unary plus %one", "ctjs.constant #ctjs.bigint<\"1\">"));
    reject("a structured unit step cannot borrow another arm's Number fact",
           replace(alternateUnit, "scf.yield %one :", "scf.yield %zero :"));
    reject("a saved zero length cannot borrow a later unit source length",
           replace(replace(savedUnit, "%seed = ctjs.create_array [%one]",
                           "%seed = ctjs.create_array []"),
                   "ctjs.set_property %seed[%name], %zero", "ctjs.append %one to %seed"));
    reject("a structured carried step must remain unchanged on its backedge",
           replace(carriedUnit, "%base, %step, %read, %d :", "%base, %step, %read, %zero :"));
    reject("a carried positive stride cannot change to another positive Number",
           replace(carriedStride, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    reject("a structured stride outside the exact bounded range is unproved",
           replace(maxStride, "4751297606873776128", "4751297606875873280"));
    reject("structured stride arithmetic cannot borrow an overflowing bounded result",
           replace(replace(maxStride, "  %finalIndex,",
                           "  %overflow = ctjs.binary_static add %unit, %one\n  %finalIndex,"),
                   "add %i, %unit", "add %i, %overflow"));
    reject("a structured overshoot does not become an own element after exit",
           replace(overshoot, "  ctjs.return %result",
                   "  %after = ctjs.get_property %finalArray[%finalIndex]\n"
                   "  ctjs.return %after"),
           ArrayContentsFailure::MissingElement);
    reject("a structured swapped step cannot reuse its first iteration's Number one",
           replace(carriedUnit, "%base, %step, %read, %d :", "%base, %step, %d, %read :"));
    rows.push_back({.what = "a structured repeated literal conversion proves its fixed stride",
                    .body = replace(replace(computedUnit, makeUnit, ""),
                                    "    %step =", makeUnit + "    %step ="),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    reject("a structured inclusive guard does not prove an own index",
           replace(original, "compare lt", "compare le"));
    reject("a structured reversed inclusive guard does not prove an own index",
           replace(reversed, "compare gt", "compare ge"));
    reject("a negated structured greater-than guard remains inclusive",
           replace(negated, "compare ge", "compare gt"));
    reject("a negated structured reversed less-than guard remains inclusive",
           replace(negatedReversed, "compare le", "compare lt"));
    reject("a structured negated guard cannot use a dynamic bound",
           replace(negated, "compare ge %index, %length", "compare ge %index, %p"));
    reject("a structured negated guard cannot invert a NaN comparison",
           replace(negated, "#ctjs.number<0>", "#ctjs.number<9221120237041090560>"));
    reject("a structured negated guard cannot hide a bound change",
           replace(negated, "    %read =",
                   "    %name = ctjs.constant #ctjs.string<\"length\">\n"
                   "    ctjs.set_property %base[%name], %zero\n    %read ="));
    reject("a structured typeof guard is not logical negation",
           replace(negated, "unary not %less", "unary typeof %less"));
    reject("a structured greater-than guard still requires length on the left",
           replace(reversed, "compare gt %length, %index", "compare gt %index, %length"));
    reject("a structured reversed strict guard cannot hide a bound change",
           replace(reversed, "    %read =",
                   "    %name = ctjs.constant #ctjs.string<\"length\">\n"
                   "    ctjs.set_property %base[%name], %zero\n    %read ="));
    reject("a structured guard must read the current array length",
           replace(original, "compare lt %index, %length", "compare lt %index, %one"));
    reject("a structured loop cannot start with an unknown Number",
           replace(original, "%index = %zero", "%index = %p"));
    reject("a structured zero step does not prove termination",
           replace(original, "binary_static add %i, %one", "binary_static add %i, %zero"));
}

} // namespace ctcompile::test::escape::arrays::structured_detail
