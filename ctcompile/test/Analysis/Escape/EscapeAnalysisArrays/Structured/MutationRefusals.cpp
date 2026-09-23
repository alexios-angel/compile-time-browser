#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::structured_detail {

void StructuredCases::mutationRefusals() {
    reject("an opaque structured backedge cannot reuse a prior exact Number",
           replace(original, "scf.yield %base, %step, %read", "scf.yield %base, %p, %read"));
    reject("a structured array backedge must preserve its certified formal",
           replace(original, "scf.yield %base, %step, %read", "scf.yield %a, %step, %read"));
    reject("a reordered structured condition must still supply the induction formal",
           replace(original, "scf.condition(%continue) %index, %saved, %array",
                   "scf.condition(%continue) %saved, %index, %array"));
    rows.push_back({.what = "structured current-element overwrites precede subsequent reads",
                    .body = replace(original, "    %read =",
                                    "    ctjs.set_property %base[%i], %zero\n    %read ="),
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=zero; a[1]=zero",
                    .exit = "zero -> {}"});
    const std::string overwritten =
        replace(original, "    %step =", "    ctjs.set_property %base[%i], %zero\n    %step =");
    rows.push_back({.what = "structured overwrites retain the value saved before the store",
                    .body = overwritten,
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "structured overwrites release former children of returned arrays",
                    .body = replace(overwritten, "ctjs.return %result", "ctjs.return %a"),
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "a -> {a}"});
    const auto scaledIndex = replace(replace(original, "    %read =",
                                             "    %position = ctjs.binary mul %i, %one\n"
                                             "    ctjs.set_property %base[%position], %zero\n"
                                             "    %read ="),
                                     "ctjs.return %result", "ctjs.return %a");
    const auto unitPower = replace(scaledIndex, "binary mul %i, %one", "binary pow %i, %one");
    rows.push_back({.what = "structured unit powers preserve reordered induction transport",
                    .body = unitPower,
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=zero; a[1]=zero",
                    .exit = "a -> {a}"});
    for (const std::string literal :
         {"#ctjs.boolean<false>", "#ctjs.null", "#ctjs.string<\"0\">"}) {
        rows.push_back(
            {.what = "structured zero powers retain the unwritten child",
             .body = replace(replace(unitPower, "  %a =",
                                     "  %exponent = ctjs.constant " + literal + "\n  %a ="),
                             "pow %i, %one", "pow %i, %exponent"),
             .arrays = "a:[x,zero]",
             .reads = "a[0]=x; a[1]=zero",
             .exit = "a -> {a,x}"});
    }
    const auto powerReload =
        replace(replace(unitPower, "create_array [%x]", "create_array [%zero]"),
                "%position = ctjs.binary pow %i, %one",
                "%exponent = ctjs.get_property %base[%zero]\n"
                "    %position = ctjs.binary pow %i, %exponent");
    rows.push_back({.what = "structured power exponent reloads preserve the singleton gap",
                    .body = powerReload,
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=zero; a[0]=zero; a[0]=zero; a[1]=zero",
                    .exit = "a -> {a}"});
    reject("structured power exponent reloads retain the complete later-store census",
           replace(powerReload,
                   "    %step =", "    ctjs.set_property %base[%zero], %one\n    %step ="));
    reject("structured power exponents cannot overlap their write footprint",
           replace(powerReload, "create_array [%zero]", "create_array [%one]"));
    rows.push_back({.what = "structured unit-base powers retain the unwritten child",
                    .body = replace(unitPower, "pow %i, %one", "pow %one, %i"),
                    .arrays = "a:[x,zero]",
                    .reads = "a[0]=x; a[1]=zero",
                    .exit = "a -> {a,x}"});
    const auto unitBase = replace(unitPower, "pow %i, %one", "pow %one, %i");
    const auto baseReload = replace(replace(unitBase, "create_array [%x]", "create_array [%one]"),
                                    "%position = ctjs.binary pow %one, %i",
                                    "%powerBase = ctjs.get_property %base[%zero]\n"
                                    "    %position = ctjs.binary pow %powerBase, %i");
    rows.push_back({.what = "structured unit bases reload outside their singleton footprint",
                    .body = baseReload,
                    .arrays = "a:[one,zero]",
                    .reads = "a[0]=one; a[0]=one; a[0]=one; a[1]=zero",
                    .exit = "a -> {a}"});
    rows.push_back({.what = "structured unit bases preserve negative exponent bounds",
                    .body = replace(unitBase, "%position = ctjs.binary pow %one, %i",
                                    "%exponent = ctjs.unary neg %i\n"
                                    "    %position = ctjs.binary pow %one, %exponent"),
                    .arrays = "a:[x,zero]",
                    .reads = "a[0]=x; a[1]=zero",
                    .exit = "a -> {a,x}"});
    reject("structured unit-base reloads retain the complete later-store census",
           replace(baseReload,
                   "    %step =", "    ctjs.set_property %base[%zero], %zero\n    %step ="));
    reject("structured unit-base reloads cannot overlap their write footprint",
           replace(replace(replace(baseReload, "create_array [%one]", "create_array [%x]"),
                           "ctjs.append %y to %a", "ctjs.append %one to %a"),
                   "%powerBase = ctjs.get_property %base[%zero]",
                   "%powerBase = ctjs.get_property %base[%one]"));
    rows.push_back({.what = "structured zero bases preserve both descending own writes",
                    .body = replace(unitBase, "pow %one, %i", "pow %zero, %i"),
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=x; a[1]=zero",
                    .exit = "a -> {a}"});
    const auto zeroBase = replace(unitBase, "pow %one, %i", "pow %zero, %i");
    rows.push_back({.what = "structured positive exponents retain the unvisited child",
                    .body = replace(zeroBase, "%position = ctjs.binary pow %zero, %i",
                                    "%exponent = ctjs.binary add %i, %one\n"
                                    "    %position = ctjs.binary pow %zero, %exponent"),
                    .arrays = "a:[zero,y]",
                    .reads = "a[0]=zero; a[1]=y",
                    .exit = "a -> {a,y}"});
    const auto zeroBaseReload =
        replace(replace(zeroBase, "  %a =", "  %two = ctjs.binary add %one, %one\n  %a ="),
                "  ctjs.append %y to %a\n", "  ctjs.append %y to %a\n  ctjs.append %zero to %a\n");
    const auto reloadedZeroBase = replace(zeroBaseReload, "%position = ctjs.binary pow %zero, %i",
                                          "%powerBase = ctjs.get_property %base[%two]\n"
                                          "    %position = ctjs.binary pow %powerBase, %i");
    rows.push_back({.what = "structured zero bases reload outside both endpoint writes",
                    .body = reloadedZeroBase,
                    .arrays = "a:[zero,zero,zero]",
                    .reads = "a[2]=zero; a[0]=x; a[2]=zero; a[1]=zero; a[2]=zero; a[2]=zero",
                    .exit = "a -> {a}"});
    reject("structured zero-base reloads retain the complete later-store census",
           replace(reloadedZeroBase,
                   "    %step =", "    ctjs.set_property %base[%two], %one\n    %step ="));
    reject("structured zero bases cannot reload an overwritten endpoint",
           replace(replace(reloadedZeroBase, "create_array [%x]", "create_array [%zero]"),
                   "%base[%two]", "%base[%zero]"));
    reject("structured zero bases require nonnegative exponents throughout the range",
           replace(zeroBase, "%position = ctjs.binary pow %zero, %i",
                   "%exponent = ctjs.binary sub %i, %one\n"
                   "    %position = ctjs.binary pow %zero, %exponent"));
    reject("structured zero to zero cannot grow the guard array",
           replace(zeroBase, "  ctjs.append %y to %a\n", ""));
    const auto parityPower = replace(
        replace(
            replace(unitPower, "  %a =",
                    "  %two = ctjs.binary add %one, %one\n"
                    "  %negative = ctjs.unary neg %one {storage_test_id = \"negative\"}\n  %a ="),
            "ctjs.append %y to %a", "ctjs.append %zero to %a\n  ctjs.append %y to %a"),
        "%position = ctjs.binary pow %i, %one",
        "%parity = ctjs.binary pow %negative, %i\n"
        "    %position = ctjs.binary add %parity, %one");
    rows.push_back({.what = "structured parity powers include interior signs",
                    .body = parityPower,
                    .arrays = "a:[zero,zero,zero]",
                    .reads = "a[0]=x; a[1]=zero; a[2]=zero",
                    .exit = "a -> {a}"});
    rows.push_back({.what = "structured even exponent strides retain unvisited parity children",
                    .body = replace(parityPower, "add %i, %one", "add %i, %two"),
                    .arrays = "a:[x,zero,zero]",
                    .reads = "a[0]=x; a[2]=zero",
                    .exit = "a -> {a,x}"});
    const auto parityReload =
        replace(replace(parityPower, "ctjs.append %zero to %a", "ctjs.append %negative to %a"),
                "%parity = ctjs.binary pow %negative, %i",
                "%powerBase = ctjs.get_property %base[%one]\n"
                "    %parity = ctjs.binary pow %powerBase, %i");
    rows.push_back({.what = "structured parity bases reload from the unwritten gap",
                    .body = parityReload,
                    .arrays = "a:[zero,negative,zero]",
                    .reads = "a[1]=negative; a[0]=x; a[1]=negative; a[1]=negative; "
                             "a[1]=negative; a[2]=zero",
                    .exit = "a -> {a}"});
    reject("structured parity reloads retain the complete later-store census",
           replace(parityReload,
                   "    %step =", "    ctjs.set_property %base[%one], %zero\n    %step ="));
    reject("structured endpoint parity cannot hide an interior base overwrite",
           replace(replace(parityReload, "create_array [%x]", "create_array [%negative]"),
                   "%powerBase = ctjs.get_property %base[%one]",
                   "%powerBase = ctjs.get_property %base[%zero]"));
    reject("structured negative powers cannot create own negative array elements",
           replace(parityPower, "add %parity, %one", "add %parity, %zero"));
    reject("structured powers refuse nonunit varying results",
           replace(unitPower, "pow %i, %one", "pow %i, %i"));
    const auto twoPointPower =
        replace(replace(unitPower, "  %a =", "  %two = ctjs.binary add %one, %one\n  %a ="),
                "pow %i, %one", "pow %i, %two");
    rows.push_back({.what = "structured two-point base powers retain exact scalar identities",
                    .body = twoPointPower,
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=zero; a[1]=zero",
                    .exit = "a -> {a}"});
    const auto twoPointExponent =
        replace(replace(twoPointPower, "create_array [%x]", "create_array [%zero, %x]"),
                "%position = ctjs.binary pow %i, %two",
                "%exponent = ctjs.binary mod %i, %two\n"
                "    %position = ctjs.binary pow %two, %exponent");
    rows.push_back({.what = "structured two-point exponents preserve source operand order",
                    .body = twoPointExponent,
                    .arrays = "a:[zero,zero,zero]",
                    .reads = "a[0]=zero; a[1]=zero; a[2]=zero",
                    .exit = "a -> {a}"});
    const auto twoPointReload =
        replace(replace(twoPointExponent, "create_array [%zero, %x]", "create_array [%two, %x]"),
                "%position = ctjs.binary pow %two, %exponent",
                "%powerBase = ctjs.get_property %base[%zero]\n"
                "    %position = ctjs.binary pow %powerBase, %exponent");
    rows.push_back({.what = "structured two-point powers preserve invariant base reloads",
                    .body = twoPointReload,
                    .arrays = "a:[ctjs.binary,zero,zero]",
                    .reads = "a[0]=ctjs.binary; a[0]=ctjs.binary; a[0]=ctjs.binary; a[1]=zero; "
                             "a[0]=ctjs.binary; a[2]=zero",
                    .exit = "a -> {a}"});
    reject("structured two-point powers retain the complete later-store census",
           replace(twoPointReload,
                   "    %step =", "    ctjs.set_property %base[%zero], %one\n    %step ="));
    reject("structured two-point powers cannot borrow a third interior identity",
           replace(twoPointPower, "create_array [%x]", "create_array [%x, %x]"));
    const auto signedUnitEven =
        replace(replace(twoPointPower, "create_array [%x]", "create_array [%x, %y]"),
                "%position = ctjs.binary pow %i, %two",
                "%powerBase = ctjs.binary sub %i, %one\n"
                "    %position = ctjs.binary pow %powerBase, %two");
    rows.push_back({.what = "structured even powers include the interior zero minimum",
                    .body = signedUnitEven,
                    .arrays = "a:[zero,zero,y]",
                    .reads = "a[0]=x; a[1]=zero; a[2]=y",
                    .exit = "a -> {a,y}"});
    const auto signedUnitOdd =
        replace(replace(signedUnitEven, "  %a =", "  %three = ctjs.binary add %two, %one\n  %a ="),
                "%position = ctjs.binary pow %powerBase, %two",
                "%power = ctjs.binary pow %powerBase, %three\n"
                "    %position = ctjs.binary add %power, %one");
    rows.push_back({.what = "structured signed-unit odd powers preserve all three writes",
                    .body = signedUnitOdd,
                    .arrays = "a:[zero,zero,zero]",
                    .reads = "a[0]=zero; a[1]=zero; a[2]=zero",
                    .exit = "a -> {a}"});
    const auto signedUnitReload =
        replace(replace(signedUnitEven, "ctjs.append %y to %a", "ctjs.append %two to %a"),
                "%position = ctjs.binary pow %powerBase, %two",
                "%exponent = ctjs.get_property %base[%two]\n"
                "    %position = ctjs.binary pow %powerBase, %exponent");
    rows.push_back({.what = "structured signed-unit powers preserve invariant exponent reloads",
                    .body = signedUnitReload,
                    .arrays = "a:[zero,zero,ctjs.binary]",
                    .reads = "a[2]=ctjs.binary; a[0]=x; a[2]=ctjs.binary; a[1]=zero; "
                             "a[2]=ctjs.binary; a[2]=ctjs.binary",
                    .exit = "a -> {a}"});
    reject("structured signed-unit powers retain the complete later-store census",
           replace(signedUnitReload,
                   "    %step =", "    ctjs.set_property %base[%two], %one\n    %step ="));
    reject("structured signed-unit powers cannot hide an interior exponent overwrite",
           replace(replace(signedUnitReload, "create_array [%x, %y]", "create_array [%two, %y]"),
                   "%exponent = ctjs.get_property %base[%two]",
                   "%exponent = ctjs.get_property %base[%zero]"));
    reject("structured signed-unit powers cannot cross zero with a negative exponent",
           replace(signedUnitEven, "%position = ctjs.binary pow %powerBase, %two",
                   "%negative = ctjs.unary neg %two\n"
                   "    %position = ctjs.binary pow %powerBase, %negative"));
    const auto primitiveAnd = replace(
        replace(scaledIndex, "  %a =",
                "  %text = ctjs.constant #ctjs.string<\"1\"> {storage_test_id = \"text\"}\n  %a ="),
        "binary mul %i, %one", "binary_static bitand %i, %text");
    const auto primitiveAdd =
        replace(primitiveAnd, "binary_static bitand %i, %text", "binary add %text, %i");
    for (const auto & literal : {"#ctjs.boolean<false>", "#ctjs.null"}) {
        rows.push_back({.what = "structured zero primitive Add offsets preserve exact own writes",
                        .body = replace(primitiveAdd, "#ctjs.string<\"1\">", literal),
                        .arrays = "a:[zero,zero]",
                        .reads = "a[0]=zero; a[1]=zero",
                        .exit = "a -> {a}"});
    }
    const auto booleanAdd =
        replace(replace(replace(replace(primitiveAdd, "#ctjs.string<\"1\">", "#ctjs.boolean<true>"),
                                "  %a =", "  %two = ctjs.binary add %one, %one\n  %a ="),
                        "create_array [%x]", "create_array [%text, %x, %one]"),
                "add %i, %one", "add %i, %two");
    const auto booleanAddReload = replace(booleanAdd, "%position = ctjs.binary add %text, %i",
                                          "%offset = ctjs.get_property %base[%zero]\n"
                                          "    %position = ctjs.binary add %offset, %i");
    rows.push_back({.what = "structured Boolean Add reloads retain their primitive gap identity",
                    .body = booleanAddReload,
                    .arrays = "a:[text,zero,one,zero]",
                    .reads = "a[0]=text; a[0]=text; a[0]=text; a[2]=one",
                    .exit = "a -> {a}"});
    reject("structured Boolean Add reloads retain the complete later-store census",
           replace(booleanAddReload,
                   "    %step =", "    ctjs.set_property %base[%zero], %zero\n    %step ="));
    reject("structured Add still refuses String concatenation",
           replace(booleanAdd, "#ctjs.boolean<true>", "#ctjs.string<\"1\">"));
    for (const std::string kind : {"shl", "shr", "ushr"}) {
        for (const std::string literal : {"#ctjs.string<\"0\">", "#ctjs.string<\"32\">",
                                          "#ctjs.boolean<false>", "#ctjs.null"}) {
            rows.push_back({.what = "structured primitive zero counts preserve exact shift writes",
                            .body = replace(replace(primitiveAnd, "#ctjs.string<\"1\">", literal),
                                            "bitand %i, %text", kind + " %i, %text"),
                            .arrays = "a:[zero,zero]",
                            .reads = "a[0]=zero; a[1]=zero",
                            .exit = "a -> {a}"});
        }
    }
    for (const auto & literal :
         {"#ctjs.string<\"1\">", "#ctjs.string<\"-1\">", "#ctjs.boolean<true>"}) {
        rows.push_back({.what = "structured primitive AND masks preserve both exact own positions",
                        .body = replace(primitiveAnd, "#ctjs.string<\"1\">", literal),
                        .arrays = "a:[zero,zero]",
                        .reads = "a[0]=zero; a[1]=zero",
                        .exit = "a -> {a}"});
    }
    for (const auto & literal : {"#ctjs.boolean<false>", "#ctjs.null"}) {
        rows.push_back({.what = "structured zero primitive AND masks retain the unvisited child",
                        .body = replace(replace(primitiveAnd, "#ctjs.string<\"1\">", literal),
                                        "bitand %i, %text", "bitand %text, %i"),
                        .arrays = "a:[zero,y]",
                        .reads = "a[0]=zero; a[1]=y",
                        .exit = "a -> {a,y}"});
    }
    const auto stringAndReload =
        replace(replace(replace(primitiveAnd, "create_array [%x]", "create_array [%x, %y, %text]"),
                        "ctjs.append %y to %a", "ctjs.append %zero to %a"),
                "%position = ctjs.binary_static bitand %i, %text",
                "%two = ctjs.binary add %one, %one\n"
                "    %mask = ctjs.get_property %base[%two]\n"
                "    %position = ctjs.binary_static bitand %mask, %i");
    rows.push_back({.what = "structured commuted AND reloads retain their primitive String origin",
                    .body = stringAndReload,
                    .arrays = "a:[zero,zero,text,zero]",
                    .reads = "a[2]=text; a[0]=zero; a[2]=text; a[1]=zero; a[2]=text; a[2]=text; "
                             "a[2]=text; a[3]=zero",
                    .exit = "a -> {a}"});
    reject("structured primitive AND reloads cannot overlap their own writes",
           replace(replace(stringAndReload, "create_array [%x, %y, %text]",
                           "create_array [%x, %text, %y]"),
                   "%mask = ctjs.get_property %base[%two]",
                   "%mask = ctjs.get_property %base[%one]"));
    reject("structured primitive AND reloads retain the complete later-store census",
           replace(stringAndReload,
                   "    %step =", "    ctjs.set_property %base[%two], %zero\n    %step ="));
    reject("structured primitive AND cannot borrow an object's conversion",
           replace(primitiveAnd, "bitand %i, %text", "bitand %i, %x"));
    for (const std::string kind : {"bitor", "bitxor"}) {
        const bool isOr = kind == "bitor";
        const auto primitiveMask = replace(primitiveAnd, "bitand", kind);
        for (const auto & literal : {"#ctjs.string<\"1\">", "#ctjs.boolean<true>"}) {
            rows.push_back({.what = "structured primitive OR/XOR masks replay only actual writes",
                            .body = replace(primitiveMask, "#ctjs.string<\"1\">", literal),
                            .arrays = isOr ? "a:[x,zero]" : "a:[zero,zero]",
                            .reads = "a[0]=x; a[1]=zero",
                            .exit = isOr ? "a -> {a,x}" : "a -> {a}"});
        }
        for (const auto & literal : {"#ctjs.boolean<false>", "#ctjs.null"}) {
            rows.push_back({.what = "structured zero primitive OR/XOR masks retain exact identity",
                            .body = replace(replace(primitiveMask, "#ctjs.string<\"1\">", literal),
                                            kind + " %i, %text", kind + " %text, %i"),
                            .arrays = "a:[zero,zero]",
                            .reads = "a[0]=zero; a[1]=zero",
                            .exit = "a -> {a}"});
        }
        const auto stringReload = replace(
            replace(replace(primitiveMask, "create_array [%x]", "create_array [%zero, %x, %text]"),
                    "add %i, %one", "add %i, %two"),
            "%position = ctjs.binary_static " + kind + " %i, %text",
            "%two = ctjs.binary add %one, %one\n"
            "    %mask = ctjs.get_property %base[%two]\n"
            "    %position = ctjs.binary_static " +
                kind + " %mask, %i");
        rows.push_back({.what = "structured primitive OR/XOR reloads preserve String gap identity",
                        .body = stringReload,
                        .arrays = "a:[zero,zero,text,zero]",
                        .reads = "a[2]=text; a[0]=zero; a[2]=text; a[2]=text",
                        .exit = "a -> {a}"});
        reject("structured primitive OR/XOR reloads cannot overlap their own writes",
               replace(replace(stringReload, "create_array [%zero, %x, %text]",
                               "create_array [%zero, %text, %x]"),
                       "%mask = ctjs.get_property %base[%two]",
                       "%mask = ctjs.get_property %base[%one]"));
        reject("structured primitive OR/XOR reloads retain the complete later-store census",
               replace(stringReload,
                       "    %step =", "    ctjs.set_property %base[%two], %zero\n    %step ="));
        reject("structured primitive OR/XOR cannot borrow an object's conversion",
               replace(primitiveMask, kind + " %i, %text", kind + " %i, %x"));
    }
    const auto stringRemainder = replace(
        replace(scaledIndex, "  %a =",
                "  %text = ctjs.constant #ctjs.string<\"2\"> {storage_test_id = \"text\"}\n  %a ="),
        "mul %i, %one", "mod %i, %text");
    for (const auto & literal : {"#ctjs.string<\"2\">", "#ctjs.string<\"-2\">"}) {
        rows.push_back({.what = "structured primitive remainders preserve exact own positions",
                        .body = replace(stringRemainder, "#ctjs.string<\"2\">", literal),
                        .arrays = "a:[zero,zero]",
                        .reads = "a[0]=zero; a[1]=zero",
                        .exit = "a -> {a}"});
    }
    rows.push_back({.what = "structured Boolean remainder preserves an unvisited child",
                    .body = replace(stringRemainder, "#ctjs.string<\"2\">", "#ctjs.boolean<true>"),
                    .arrays = "a:[zero,y]",
                    .reads = "a[0]=zero; a[1]=y",
                    .exit = "a -> {a,y}"});
    const auto stringRemainderReload = replace(
        replace(
            replace(replace(replace(stringRemainder, "#ctjs.string<\"2\">", "#ctjs.string<\"4\">"),
                            "  %a =", "  %two = ctjs.binary add %one, %one\n  %a ="),
                    "create_array [%x]", "create_array [%x, %text, %y, %zero]"),
            "ctjs.append %y to %a", "ctjs.append %zero to %a"),
        "add %i, %one", "add %i, %two");
    const auto reloadedRemainder =
        replace(stringRemainderReload, "%position = ctjs.binary mod %i, %text",
                "%divisor = ctjs.get_property %base[%one]\n"
                "    %position = ctjs.binary mod %i, %divisor");
    rows.push_back({.what = "structured String remainder reloads preserve wrap gaps",
                    .body = reloadedRemainder,
                    .arrays = "a:[zero,text,zero,zero,zero]",
                    .reads = "a[1]=text; a[0]=zero; a[1]=text; a[2]=zero; a[1]=text; a[4]=zero",
                    .exit = "a -> {a}"});
    rows.push_back({.what = "structured primitive remainder replay retains unvisited children",
                    .body = replace(reloadedRemainder, "create_array [%x, %text, %y, %zero]",
                                    "create_array [%x, %text, %y, %x]"),
                    .arrays = "a:[zero,text,zero,x,zero]",
                    .reads = "a[1]=text; a[0]=zero; a[1]=text; a[2]=zero; a[1]=text; a[4]=zero",
                    .exit = "a -> {a,x}"});
    rows.push_back(
        {.what = "structured primitive remainder preserves children saved before overwrites",
         .body = replace(replace(reloadedRemainder, "  %finalIndex,",
                                 "  %savedChild = ctjs.get_property %a[%two]\n  %finalIndex,"),
                         "ctjs.return %a", "ctjs.return %savedChild"),
         .arrays = "a:[zero,text,zero,zero,zero]",
         .reads = "a[2]=y; a[1]=text; a[0]=zero; a[1]=text; a[2]=zero; a[1]=text; a[4]=zero",
         .exit = "y -> {y}"});
    reject("structured String remainder reloads cannot conceal an actual write",
           replace(replace(reloadedRemainder, "create_array [%x, %text, %y, %zero]",
                           "create_array [%x, %zero, %text, %zero]"),
                   "%divisor = ctjs.get_property %base[%one]",
                   "%divisor = ctjs.get_property %base[%two]"));
    reject("structured String remainder reloads retain the complete later-store census",
           replace(reloadedRemainder,
                   "    %step =", "    ctjs.set_property %base[%one], %zero\n    %step ="));
    for (const std::string literal :
         {"#ctjs.string<\"0\">", "#ctjs.boolean<false>", "#ctjs.null", "#ctjs.undefined",
          "#ctjs.string<\"02\">", "#ctjs.string<\"4294967296\">", "#ctjs.bigint<\"2\">"}) {
        reject("structured primitive remainders require bounded nonzero conversion",
               replace(stringRemainder, "#ctjs.string<\"2\">", literal));
    }
    reject("structured remainder cannot borrow an object's conversion",
           replace(stringRemainder, "mod %i, %text", "mod %i, %x"));
    for (const auto & expression :
         {"%position = ctjs.binary mul %i, %one", "%position = ctjs.binary mul %one, %i",
          "%position = ctjs.unary plus %i",
          "%part = ctjs.unary neg %i\n"
          "    %position = ctjs.unary neg %part"}) {
        rows.push_back(
            {.what = "structured scaling and signs preserve current-element overwrites",
             .body = replace(scaledIndex, "%position = ctjs.binary mul %i, %one", expression),
             .arrays = "a:[zero,zero]",
             .reads = "a[0]=zero; a[1]=zero",
             .exit = "a -> {a}"});
    }
    const auto scaledVisit =
        replace(replace(replace(replace(scaledIndex, "  %a =",
                                        "  %two = ctjs.binary add %one, %one "
                                        "{storage_test_id = \"two\"}\n  %a ="),
                                "ctjs.append %y to %a", "ctjs.append %one to %a"),
                        "mul %i, %one", "mul %i, %two"),
                "add %i, %one", "add %i, %two");
    rows.push_back({.what = "structured scaling accepts a single bounded visit",
                    .body = scaledVisit,
                    .arrays = "a:[zero,one]",
                    .reads = "a[0]=zero",
                    .exit = "a -> {a}"});
    const auto wideVisit = replace(
        replace(scaledVisit,
                "  %a =", "  %huge = ctjs.constant #ctjs.number<4751297606873776128>\n  %a ="),
        "mul %i, %two", "mul %i, %huge");
    rows.push_back({.what = "structured singleton products do not scale an unvisited stride",
                    .body = wideVisit,
                    .arrays = "a:[zero,one]",
                    .reads = "a[0]=zero",
                    .exit = "a -> {a}"});
    const auto wideTranslated =
        replace(replace(wideVisit, "4751297606873776128", "4746794007248502784"),
                "%position = ctjs.binary mul %i, %huge",
                "%part = ctjs.binary add %i, %one\n"
                "    %product = ctjs.binary mul %part, %huge\n"
                "    %position = ctjs.binary sub %product, %huge");
    rows.push_back({.what = "structured nonzero singleton products keep exact intermediate Numbers",
                    .body = wideTranslated,
                    .arrays = "a:[zero,one]",
                    .reads = "a[0]=zero",
                    .exit = "a -> {a}"});
    rows.push_back({.what = "structured singleton products preserve unvisited children",
                    .body = replace(wideVisit, "ctjs.append %one to %a", "ctjs.append %x to %a"),
                    .arrays = "a:[zero,x]",
                    .reads = "a[0]=zero",
                    .exit = "a -> {a,x}"});
    reject("structured singleton multiplication retains intermediate bounds",
           replace(replace(wideTranslated, "add %i, %one", "add %i, %two"),
                   "%position = ctjs.binary sub %product, %huge",
                   "%half = ctjs.binary sub %product, %huge\n"
                   "    %position = ctjs.binary sub %half, %huge"));
    reject("structured non-singleton multiplication retains endpoint bounds",
           replace(wideVisit, "create_array [%x]", "create_array [%x, %one]"));
    rows.push_back({.what = "structured unary negation preserves the signed-zero own key",
                    .body = replace(scaledVisit, "ctjs.binary mul %i, %two", "ctjs.unary neg %i"),
                    .arrays = "a:[zero,one]",
                    .reads = "a[0]=zero",
                    .exit = "a -> {a}"});
    const auto scaledStart =
        replace(replace(replace(scaledVisit, "create_array [%x]", "create_array [%one, %one]"),
                        "ctjs.append %one to %a", "ctjs.append %x to %a"),
                "%index = %zero", "%index = %one");
    rows.push_back({.what = "structured scaled starts preserve translated own positions",
                    .body = scaledStart,
                    .arrays = "a:[one,one,zero]",
                    .reads = "a[1]=one",
                    .exit = "a -> {a}"});
    const auto reloadedFactor =
        replace(replace(scaledVisit, "ctjs.append %one to %a", "ctjs.append %two to %a"),
                "    %position = ctjs.binary mul %i, %two",
                "    %factor = ctjs.get_property %base[%one]\n"
                "    %position = ctjs.binary mul %i, %factor");
    rows.push_back({.what = "structured factors outside the scaled footprint remain invariant",
                    .body = reloadedFactor,
                    .arrays = "a:[zero,two]",
                    .reads = "a[1]=two; a[0]=zero",
                    .exit = "a -> {a}"});
    rows.push_back({.what = "structured scaled overwrites retain previously saved children",
                    .body = replace(replace(scaledVisit, "  %finalIndex,",
                                            "  %savedChild = ctjs.get_property %a[%zero]\n"
                                            "  %finalIndex,"),
                                    "ctjs.return %a", "ctjs.return %savedChild"),
                    .arrays = "a:[zero,one]",
                    .reads = "a[0]=x; a[0]=zero",
                    .exit = "x -> {x}"});
    rows.push_back({.what = "structured zero-trip scaling preserves original contents",
                    .body = replace(scaledVisit, "%index = %zero", "%index = %two"),
                    .arrays = "a:[x,one]",
                    .exit = "a -> {a,x}"});
    const auto zeroVisit = replace(scaledVisit, "mul %i, %two", "mul %i, %zero");
    rows.push_back({.what = "structured zero scaling preserves the historical single visit",
                    .body = zeroVisit,
                    .arrays = "a:[zero,one]",
                    .reads = "a[0]=zero",
                    .exit = "a -> {a}"});
    const auto zeroIndex = replace(zeroVisit, "add %i, %two", "add %i, %one");
    for (const auto & expression :
         {"%position = ctjs.binary mul %i, %zero", "%position = ctjs.binary mul %zero, %i",
          "%part = ctjs.binary sub %i, %two\n"
          "    %position = ctjs.binary mul %part, %zero"}) {
        rows.push_back(
            {.what = "structured zero products preserve repeated signed own-zero visits",
             .body = replace(zeroIndex, "%position = ctjs.binary mul %i, %zero", expression),
             .arrays = "a:[zero,one]",
             .reads = "a[0]=zero; a[1]=one",
             .exit = "a -> {a}"});
    }
    rows.push_back(
        {.what = "structured zero-product translation preserves a single own position",
         .body = replace(replace(replace(zeroIndex, "create_array [%x]", "create_array [%one]"),
                                 "ctjs.append %one to %a", "ctjs.append %x to %a"),
                         "%position = ctjs.binary mul %i, %zero",
                         "%part = ctjs.binary mul %i, %zero\n"
                         "    %position = ctjs.binary add %part, %one"),
         .arrays = "a:[one,zero]",
         .reads = "a[0]=one; a[1]=zero",
         .exit = "a -> {a}"});
    rows.push_back({.what = "structured zero products retain unvisited children",
                    .body = replace(zeroIndex, "ctjs.append %one to %a", "ctjs.append %x to %a"),
                    .arrays = "a:[zero,x]",
                    .reads = "a[0]=zero; a[1]=x",
                    .exit = "a -> {a,x}"});
    rows.push_back({.what = "structured zero products preserve earlier child snapshots",
                    .body = replace(replace(zeroIndex, "  %finalIndex,",
                                            "  %savedChild = ctjs.get_property %a[%zero]\n"
                                            "  %finalIndex,"),
                                    "ctjs.return %a", "ctjs.return %savedChild"),
                    .arrays = "a:[zero,one]",
                    .reads = "a[0]=x; a[0]=zero; a[1]=one",
                    .exit = "x -> {x}"});
    const auto zeroReload =
        replace(replace(reloadedFactor, "ctjs.append %two to %a", "ctjs.append %zero to %a"),
                "add %i, %two", "add %i, %one");
    rows.push_back({.what = "structured zero factors reload outside their singleton write",
                    .body = zeroReload,
                    .arrays = "a:[zero,zero]",
                    .reads = "a[1]=zero; a[0]=zero; a[1]=zero; a[1]=zero",
                    .exit = "a -> {a}"});
    reject("structured zero factors cannot reload the overwritten element",
           replace(replace(replace(zeroReload, "create_array [%x]", "create_array [%zero]"),
                           "ctjs.append %zero to %a", "ctjs.append %x to %a"),
                   "%factor = ctjs.get_property %base[%one]",
                   "%factor = ctjs.get_property %base[%zero]"));
    reject(
        "structured later stores invalidate earlier zero-factor reloads",
        replace(zeroReload, "    %step =", "    ctjs.set_property %base[%one], %one\n    %step ="));
    for (const auto & body :
         {replace(scaledVisit, "add %i, %two", "add %i, %one"),
          replace(scaledStart, "create_array [%one, %one]", "create_array [%one]"),
          replace(scaledVisit, "mul %i, %two", "mul %i, %last"),
          replace(reloadedFactor,
                  "    %step =", "    ctjs.set_property %base[%one], %one\n    %step ="),
          replace(replace(scaledStart, "ctjs.append %x to %a", "ctjs.append %two to %a"),
                  "    %position = ctjs.binary mul %i, %two",
                  "    %factor = ctjs.get_property %base[%two]\n"
                  "    %position = ctjs.binary mul %i, %factor")}) {
        reject("structured scaled stores retain bounds, invariance and reload exclusions", body);
    }
    for (const std::string literal : {"#ctjs.string<\"1\">", "#ctjs.boolean<true>"}) {
        const auto primitive = replace(
            replace(scaledIndex, "  %a =", "  %factor = ctjs.constant " + literal + "\n  %a ="),
            "mul %i, %one", "mul %i, %factor");
        for (const auto & body :
             {primitive, replace(primitive, "mul %i, %factor", "mul %factor, %i")}) {
            rows.push_back({.what = "structured primitive factors retain exact own-index products",
                            .body = body,
                            .arrays = "a:[zero,zero]",
                            .reads = "a[0]=zero; a[1]=zero",
                            .exit = "a -> {a}"});
        }
        rows.push_back(
            {.what = "structured primitive scaling retains previously saved children",
             .body = replace(replace(primitive, "  %finalIndex,",
                                     "  %savedChild = ctjs.get_property %a[%zero]\n  %finalIndex,"),
                             "ctjs.return %a", "ctjs.return %savedChild"),
             .arrays = "a:[zero,zero]",
             .reads = "a[0]=x; a[0]=zero; a[1]=zero",
             .exit = "x -> {x}"});
    }
    for (const std::string literal :
         {"#ctjs.null", "#ctjs.boolean<false>", "#ctjs.string<\"0\">"}) {
        rows.push_back(
            {.what = "structured zero-valued primitive factors retain unvisited children",
             .body = replace(replace(scaledIndex,
                                     "  %a =", "  %factor = ctjs.constant " + literal + "\n  %a ="),
                             "mul %i, %one", "mul %factor, %i"),
             .arrays = "a:[zero,y]",
             .reads = "a[0]=zero; a[1]=y",
             .exit = "a -> {a,y}"});
    }
    rows.push_back({.what = "structured negative String factors reverse translated own indices",
                    .body = replace(
                        replace(scaledIndex,
                                "  %a =", "  %factor = ctjs.constant #ctjs.string<\"-1\">\n  %a ="),
                        "%position = ctjs.binary mul %i, %one",
                        "%part = ctjs.binary mul %i, %factor\n"
                        "    %position = ctjs.binary add %part, %one"),
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=x; a[1]=zero",
                    .exit = "a -> {a}"});
    reject("a structured object factor cannot borrow primitive Number conversion",
           replace(scaledIndex, "mul %i, %one", "mul %i, %x"));
    const auto stringReload = replace(
        replace(reloadedFactor, "  %a =",
                "  %text = ctjs.constant #ctjs.string<\"2\"> {storage_test_id = \"text\"}\n  %a ="),
        "ctjs.append %two to %a", "ctjs.append %text to %a");
    rows.push_back({.what = "structured String factor reloads preserve original primitive identity",
                    .body = stringReload,
                    .arrays = "a:[zero,text]",
                    .reads = "a[1]=text; a[0]=zero",
                    .exit = "a -> {a}"});
    reject("structured String factor reloads cannot overlap a write",
           replace(replace(replace(stringReload, "create_array [%x]", "create_array [%text]"),
                           "ctjs.append %text to %a", "ctjs.append %x to %a"),
                   "%factor = ctjs.get_property %base[%one]",
                   "%factor = ctjs.get_property %base[%zero]"));
    reject("structured later stores invalidate earlier String factor reloads",
           replace(stringReload,
                   "    %step =", "    ctjs.set_property %base[%one], %one\n    %step ="));
    for (const std::string literal :
         {"#ctjs.string<\"01\">", "#ctjs.undefined", "#ctjs.bigint<\"1\">"}) {
        reject("structured primitive scaling requires bounded side-effect-free conversion",
               replace(replace(scaledIndex,
                               "  %a =", "  %factor = ctjs.constant " + literal + "\n  %a ="),
                       "mul %i, %one", "mul %i, %factor"));
    }
    const auto composedIndex =
        replace(replace(scaledVisit, "create_array [%x]", "create_array [%one, %x, %y]"),
                "%position = ctjs.binary mul %i, %two",
                "%part = ctjs.binary div %i, %two\n"
                "    %position = ctjs.binary add %part, %one");
    rows.push_back({.what = "structured composed quotients release exactly the shifted children",
                    .body = composedIndex,
                    .arrays = "a:[one,zero,zero,one]",
                    .reads = "a[0]=one; a[2]=zero",
                    .exit = "a -> {a}"});
    const auto stringQuotient = replace(
        replace(composedIndex, "  %a =", "  %text = ctjs.constant #ctjs.string<\"2\">\n  %a ="),
        "div %i, %two", "div %i, %text");
    rows.push_back({.what = "structured String division preserves exact translated positions",
                    .body = stringQuotient,
                    .arrays = "a:[one,zero,zero,one]",
                    .reads = "a[0]=one; a[2]=zero",
                    .exit = "a -> {a}"});
    rows.push_back({.what = "structured primitive quotient replay retains unvisited children",
                    .body = replace(stringQuotient, "create_array [%one, %x, %y]",
                                    "create_array [%x, %x, %y]"),
                    .arrays = "a:[x,zero,zero,one]",
                    .reads = "a[0]=x; a[2]=zero",
                    .exit = "a -> {a,x}"});
    rows.push_back(
        {.what = "structured primitive division preserves previously saved children",
         .body = replace(replace(stringQuotient, "  %finalIndex,",
                                 "  %savedChild = ctjs.get_property %a[%one]\n  %finalIndex,"),
                         "ctjs.return %a", "ctjs.return %savedChild"),
         .arrays = "a:[one,zero,zero,one]",
         .reads = "a[1]=x; a[0]=one; a[2]=zero",
         .exit = "x -> {x}"});
    rows.push_back({.what = "structured Boolean true divides each own index exactly",
                    .body = replace(replace(scaledIndex, "  %a =",
                                            "  %true = ctjs.constant #ctjs.boolean<true>\n  %a ="),
                                    "mul %i, %one", "div %i, %true"),
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=zero; a[1]=zero",
                    .exit = "a -> {a}"});
    const auto stringQuotientReload =
        replace(replace(replace(stringQuotient, "ctjs.constant #ctjs.string<\"2\">",
                                "ctjs.constant #ctjs.string<\"2\"> {storage_test_id = \"text\"}"),
                        "ctjs.append %one to %a", "ctjs.append %text to %a"),
                "%part = ctjs.binary div %i, %text",
                "%three = ctjs.binary add %two, %one\n"
                "    %divisor = ctjs.get_property %base[%three]\n"
                "    %part = ctjs.binary div %i, %divisor");
    rows.push_back({.what = "structured String divisor reloads preserve primitive identity",
                    .body = stringQuotientReload,
                    .arrays = "a:[one,zero,zero,text]",
                    .reads = "a[3]=text; a[0]=one; a[3]=text; a[2]=zero",
                    .exit = "a -> {a}"});
    for (const auto & body : {replace(stringQuotient, "add %i, %two", "add %i, %one"),
                              replace(stringQuotient, "%index = %zero", "%index = %one"),
                              replace(stringQuotient, "div %i, %text", "div %text, %i"),
                              replace(stringQuotient, "div %i, %text", "div %i, %x"),
                              replace(replace(stringQuotientReload, "create_array [%one, %x, %y]",
                                              "create_array [%one, %text, %y]"),
                                      "%divisor = ctjs.get_property %base[%three]",
                                      "%divisor = ctjs.get_property %base[%one]"),
                              replace(stringQuotientReload, "    %step =",
                                      "    ctjs.set_property %base[%three], %two\n    %step =")}) {
        reject("structured primitive division retains exact visits and the complete reload census",
               body);
    }
    for (const std::string literal :
         {"#ctjs.string<\"0\">", "#ctjs.boolean<false>", "#ctjs.null", "#ctjs.undefined",
          "#ctjs.string<\"02\">", "#ctjs.bigint<\"2\">"}) {
        reject("structured primitive divisors require bounded nonzero conversion",
               replace(stringQuotient, "#ctjs.string<\"2\">", literal));
    }
    rows.push_back({.what = "structured compositions keep signed intermediate Numbers exact",
                    .body = replace(composedIndex,
                                    "%part = ctjs.binary div %i, %two\n"
                                    "    %position = ctjs.binary add %part, %one",
                                    "%part = ctjs.binary sub %i, %two\n"
                                    "    %half = ctjs.binary div %part, %two\n"
                                    "    %position = ctjs.binary add %half, %two"),
                    .arrays = "a:[one,zero,zero,one]",
                    .reads = "a[0]=one; a[2]=zero",
                    .exit = "a -> {a}"});
    for (const auto & body : {replace(composedIndex, "add %i, %two", "add %i, %one"),
                              replace(composedIndex, "add %part, %one", "add %part, %i"),
                              replace(composedIndex, "add %part, %one", "sub %part, %one"),
                              replace(composedIndex, "%part = ctjs.binary div %i, %two",
                                      "%factor = ctjs.get_property %base[%one]\n"
                                      "    %part = ctjs.binary div %i, %factor")}) {
        reject("structured compositions preserve integrality, bounds and invariance", body);
    }
    const auto negativeQuotient = replace(
        replace(replace(composedIndex, "  %a =", "  %negative = ctjs.unary neg %two\n  %a ="),
                "div %i, %two", "div %i, %negative"),
        "add %part, %one", "add %part, %two");
    rows.push_back({.what = "structured negative quotients retain descending own positions",
                    .body = negativeQuotient,
                    .arrays = "a:[one,zero,zero,one]",
                    .reads = "a[0]=one; a[2]=zero",
                    .exit = "a -> {a}"});
    rows.push_back({.what = "structured negative String divisors preserve descending own positions",
                    .body = replace(negativeQuotient, "ctjs.unary neg %two",
                                    "ctjs.constant #ctjs.string<\"-2\">"),
                    .arrays = "a:[one,zero,zero,one]",
                    .reads = "a[0]=one; a[2]=zero",
                    .exit = "a -> {a}"});
    rows.push_back(
        {.what = "structured negative quotients retain signed intermediate endpoints",
         .body = replace(replace(negativeQuotient, "%part = ctjs.binary div %i, %negative",
                                 "%signed = ctjs.binary sub %i, %two\n"
                                 "    %part = ctjs.binary div %signed, %negative"),
                         "add %part, %two", "add %part, %one"),
         .arrays = "a:[one,zero,zero,one]",
         .reads = "a[0]=one; a[2]=zero",
         .exit = "a -> {a}"});
    rows.push_back(
        {.what = "two negative divisions restore the ascending footprint",
         .body = replace(
             replace(negativeQuotient, "  %a =", "  %negativeOne = ctjs.unary neg %one\n  %a ="),
             "ctjs.binary add %part, %two", "ctjs.binary div %part, %negativeOne"),
         .arrays = "a:[zero,zero,y,one]",
         .reads = "a[0]=zero; a[2]=y",
         .exit = "a -> {a,y}"});
    reject("structured negative divisors cannot conceal fractional intermediate positions",
           replace(negativeQuotient, "add %i, %two", "add %i, %one"));
    reject("structured negative divisors cannot conceal negative own positions",
           replace(negativeQuotient, "add %part, %two", "add %part, %zero"));
    const auto reverseIndex = replace(scaledIndex, "mul %i, %one", "sub %one, %i");
    rows.push_back({.what = "structured reverse subtraction releases descending own children",
                    .body = reverseIndex,
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=x; a[1]=zero",
                    .exit = "a -> {a}"});
    const auto primitiveReverse = replace(
        replace(reverseIndex, "  %a =", "  %text = ctjs.constant #ctjs.string<\"1\">\n  %a ="),
        "sub %one, %i", "sub %text, %i");
    for (const auto & literal : {"#ctjs.string<\"1\">", "#ctjs.boolean<true>"}) {
        rows.push_back({.what = "structured primitive subtraction preserves descending own writes",
                        .body = replace(primitiveReverse, "#ctjs.string<\"1\">", literal),
                        .arrays = "a:[zero,zero]",
                        .reads = "a[0]=x; a[1]=zero",
                        .exit = "a -> {a}"});
    }
    for (const auto & literal : {"#ctjs.null", "#ctjs.boolean<false>"}) {
        rows.push_back({.what = "structured zero primitive subtraction preserves current indices",
                        .body = replace(replace(primitiveReverse, "#ctjs.string<\"1\">", literal),
                                        "sub %text, %i", "sub %i, %text"),
                        .arrays = "a:[zero,zero]",
                        .reads = "a[0]=zero; a[1]=zero",
                        .exit = "a -> {a}"});
    }
    const auto stringReverseReload = replace(
        replace(replace(replace(primitiveReverse, "ctjs.constant #ctjs.string<\"1\">",
                                "ctjs.constant #ctjs.string<\"3\"> {storage_test_id = \"text\"}"),
                        "  %a =", "  %two = ctjs.binary add %one, %one\n  %a ="),
                "create_array [%x]", "create_array [%zero, %x, %text]"),
        "add %i, %one", "add %i, %two");
    const auto reloadedReverse =
        replace(stringReverseReload, "%position = ctjs.binary sub %text, %i",
                "%offset = ctjs.get_property %base[%two]\n"
                "    %position = ctjs.binary sub %offset, %i");
    rows.push_back({.what = "structured primitive subtraction reloads preserve their gap identity",
                    .body = reloadedReverse,
                    .arrays = "a:[zero,zero,text,zero]",
                    .reads = "a[2]=text; a[0]=zero; a[2]=text; a[2]=text",
                    .exit = "a -> {a}"});
    reject("structured later stores invalidate primitive subtraction reloads",
           replace(reloadedReverse,
                   "    %step =", "    ctjs.set_property %base[%two], %zero\n    %step ="));
    reject("structured String Add still concatenates",
           replace(primitiveReverse, "sub %text, %i", "add %text, %i"));
    reject("structured subtraction cannot borrow object conversion",
           replace(primitiveReverse, "sub %text, %i", "sub %x, %i"));
    rows.push_back({.what = "structured nested reversals preserve source subtraction order",
                    .body = replace(reverseIndex, "%position = ctjs.binary sub %one, %i",
                                    "%part = ctjs.binary sub %one, %i\n"
                                    "    %position = ctjs.binary sub %one, %part"),
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=zero; a[1]=zero",
                    .exit = "a -> {a}"});
    rows.push_back({.what = "structured reversed quotients preserve the descending footprint",
                    .body = replace(composedIndex, "add %part, %one", "sub %two, %part"),
                    .arrays = "a:[one,zero,zero,one]",
                    .reads = "a[0]=one; a[2]=zero",
                    .exit = "a -> {a}"});
    reject("structured reverse indices cannot visit negative own positions",
           replace(reverseIndex, "sub %one, %i", "sub %zero, %i"));
    reject("structured reverse indices cannot depend on two varying operands",
           replace(reverseIndex, "sub %one, %i", "sub %i, %i"));
    const auto negativeScale =
        replace(replace(reverseIndex, "  %a =", "  %negative = ctjs.unary neg %one\n  %a ="),
                "%position = ctjs.binary sub %one, %i",
                "%part = ctjs.binary mul %i, %negative\n"
                "    %position = ctjs.binary add %part, %one");
    for (const auto & expression :
         {"ctjs.binary mul %i, %negative", "ctjs.binary mul %negative, %i", "ctjs.unary neg %i"}) {
        rows.push_back({.what = "structured negative factors and signs reverse exact own writes",
                        .body = replace(negativeScale, "ctjs.binary mul %i, %negative", expression),
                        .arrays = "a:[zero,zero]",
                        .reads = "a[0]=x; a[1]=zero",
                        .exit = "a -> {a}"});
    }
    rows.push_back({.what = "structured negative factors reverse signed intermediate ranges",
                    .body = replace(negativeScale,
                                    "%part = ctjs.binary mul %i, %negative\n"
                                    "    %position = ctjs.binary add %part, %one",
                                    "%part = ctjs.binary sub %i, %one\n"
                                    "    %position = ctjs.binary mul %part, %negative"),
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=x; a[1]=zero",
                    .exit = "a -> {a}"});
    reject("structured negative products cannot conceal negative own positions",
           replace(negativeScale, "add %part, %one", "add %part, %zero"));
    reject("structured negative products cannot conceal array growth",
           replace(negativeScale, "add %part, %one", "sub %one, %part"));
    const auto unaryIndex =
        replace(negativeScale, "ctjs.binary mul %i, %negative", "ctjs.unary neg %i");
    reject("structured unary signs cannot conceal negative own positions",
           replace(unaryIndex, "add %part, %one", "add %part, %zero"));
    reject("structured unary signs cannot conceal array growth",
           replace(unaryIndex, "add %part, %one", "sub %one, %part"));
    const std::string fixedOverwrite =
        replace(original, "    %read =", "    ctjs.set_property %base[%one], %zero\n    %read =");
    rows.push_back({.what = "structured invariant own-index overwrites precede later reads",
                    .body = fixedOverwrite,
                    .arrays = "a:[x,zero]",
                    .reads = "a[0]=x; a[1]=zero",
                    .exit = "zero -> {}"});
    rows.push_back({.what = "structured invariant overwrites retain other returned children",
                    .body = replace(fixedOverwrite, "%base[%one], %zero", "%base[%zero], %zero"),
                    .arrays = "a:[zero,y]",
                    .reads = "a[0]=zero; a[1]=y",
                    .exit = "y -> {y}"});
    reject(
        "structured invariant overwrite keys must name an existing own index",
        replace(replace(fixedOverwrite, "  %a =", "  %fixed = ctjs.binary add %one, %one\n  %a ="),
                "%base[%one], %zero", "%base[%fixed], %zero"));
    reject("structured keys cannot reload overwritten guard elements",
           replace(fixedOverwrite, "    ctjs.set_property %base[%one], %zero",
                   "    %fixed = ctjs.get_property %base[%zero]\n"
                   "    ctjs.set_property %base[%fixed], %zero"));
    reject("structured element-dependent strides cannot survive overwrites",
           replace(reloaded, "    %unit =", "    ctjs.set_property %base[%i], %zero\n    %unit ="));
    reject("structured header stores can reach outside the guarded own elements",
           replace(original,
                   "    %less =", "    ctjs.set_property %array[%index], %zero\n    %less ="));
    reject("structured next-index stores can extend the array",
           replace(original, "    scf.yield %base, %step",
                   "    ctjs.set_property %base[%step], %zero\n    scf.yield %base, %step"));
    reject("structured loop allocation cannot collapse repeated instances",
           replace(original, "    %read =", "    %fresh = ctjs.create_array []\n    %read ="));
    reject("structured nested control needs a separate lifetime proof",
           replace(original, "    %read =", "    scf.if %flag {\n    }\n    %read ="));
    reject("a structured zero-trip body cannot conceal publication",
           replace(replace(replace(original, "[%x]", "[]"), "  ctjs.append %y to %a\n", ""),
                   "    %read =", "    ctjs.store_global \"held\", %base\n    %read ="));
    reject("a structured offset read must stay within the guard array on every iteration",
           replace(original, "    %read = ctjs.get_property %base[%i]",
                   "    %offset = ctjs.binary_static add %i, %one\n"
                   "    %read = ctjs.get_property %base[%offset]"),
           ArrayContentsFailure::MissingElement);
    reject("effects after structured induction discard all earlier exact reads",
           replace(original, "  ctjs.return %result",
                   "  ctjs.store_global \"held\", %result\n  ctjs.return %result"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("opaque carried contents cannot borrow a prior structured scalar fact",
           replace(replace(original, "%base[%i]", "%base[%last]"), "scf.yield %base, %step, %read",
                   "scf.yield %base, %step, %p"),
           ArrayContentsFailure::UnknownIndex);
    const std::string directLoop =
        "  %finalIndex, %result = scf.while (%index = %zero, %saved = %zero) : "
        "(!ctjs.value, !ctjs.value) -> (!ctjs.value, !ctjs.value) {\n"
        "    %key = ctjs.constant #ctjs.string<\"length\">\n"
        "    %length = ctjs.get_property %a[%key]\n"
        "    %less = ctjs.compare lt %index, %length\n"
        "    %continue = ctjs.truthy %less\n"
        "    scf.condition(%continue) %index, %saved : !ctjs.value, !ctjs.value\n"
        "  } do {\n  ^body(%i: !ctjs.value, %last: !ctjs.value):\n"
        "    %read = ctjs.get_property %a[%i]\n"
        "    %step = ctjs.binary_static add %i, %one\n"
        "    scf.yield %step, %read : !ctjs.value, !ctjs.value\n  }\n";
    const std::string direct = prefix + directLoop + "  ctjs.return %result\n";
    rows.push_back({.what = "SCF may remove an invariant dominating array parameter",
                    .body = direct,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back(
        {.what = "direct structured arrays permit guarded current-element overwrites",
         .body = replace(direct, "    %read =", "    ctjs.set_property %a[%i], %zero\n    %read ="),
         .arrays = "a:[zero,zero]",
         .reads = "a[0]=zero; a[1]=zero",
         .exit = "zero -> {}"});
    rows.push_back(
        {.what = "reversed strict guards also support direct structured array aliases",
         .body = replace(direct, "compare lt %index, %length", "compare gt %length, %index"),
         .arrays = "a:[x,y]",
         .reads = "a[0]=x; a[1]=y",
         .exit = "y -> {y}"});
    rows.push_back({.what = "direct-array structured induction preserves a nonzero start",
                    .body = replace(direct, "%index = %zero", "%index = %one"),
                    .arrays = "a:[x,y]",
                    .reads = "a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "computed zero also initializes an invariant direct-array loop",
                    .body = prefix + "  %start = ctjs.unary plus %zero\n" +
                            replace(directLoop, "%index = %zero", "%index = %start") +
                            "  ctjs.return %result\n",
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "held unit steps also support invariant direct-array loops",
                    .body = prefix + makeUnit +
                            replace(directLoop, "add %i, %one", "add %i, %unit") +
                            "  ctjs.return %result\n",
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "positive strides also support invariant direct-array loops",
                    .body = prefix + makeStride +
                            replace(directLoop, "add %i, %one", "add %i, %unit") +
                            "  ctjs.return %result\n",
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    rows.push_back({.what = "direct structured arrays retain preceding own overwrites",
                    .body = prefix + "  ctjs.set_property %a[%one], %x\n" + directLoop +
                            "  ctjs.return %result\n",
                    .arrays = "a:[x,x]",
                    .reads = "a[0]=x; a[1]=x",
                    .exit = "x -> {x}"});
    rows.push_back({.what = "a direct empty array proves zero trips",
                    .body = replace(replace(direct, "[%x]", "[]"), "  ctjs.append %y to %a\n", ""),
                    .arrays = "a:[]",
                    .exit = "zero -> {}"});
    rows.push_back({.what = "direct arrays retain their identity inside an enclosing branch",
                    .body = prefix + "  %chosen = scf.if %flag -> (!ctjs.value) {\n" + directLoop +
                            "    scf.yield %result : !ctjs.value\n"
                            "  } else {\n    scf.yield %x : !ctjs.value\n  }\n"
                            "  ctjs.return %chosen\n",
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}; x -> {x}"});
    reject("an outer opaque parameter is not a dominating direct array",
           replace(direct, "%length = ctjs.get_property %a", "%length = ctjs.get_property %p"));
    reject("direct-array nonzero starts preserve own-bound checks on every read",
           replace(replace(replace(direct, "%index = %zero", "%index = %one"),
                           "  ctjs.append %y to %a\n",
                           "  ctjs.append %y to %a\n"
                           "  %b = ctjs.create_array [%x]\n"),
                   "%read = ctjs.get_property %a", "%read = ctjs.get_property %b"),
           ArrayContentsFailure::MissingElement);
    rows.push_back(
        {.what = "direct array induction permits invariant own-element writes",
         .body = replace(direct, "    %read =", "    ctjs.set_property %a[%zero], %y\n    %read ="),
         .arrays = "a:[y,y]",
         .reads = "a[0]=y; a[1]=y",
         .exit = "y -> {y}"});
    reject("direct array length cannot certify another shorter array",
           replace(replace(direct, "  %finalIndex,", "  %b = ctjs.create_array []\n  %finalIndex,"),
                   "%read = ctjs.get_property %a", "%read = ctjs.get_property %b"),
           ArrayContentsFailure::MissingElement);
    reject("direct array offset reads retain own-bound checks",
           replace(direct, "    %read = ctjs.get_property %a[%i]",
                   "    %offset = ctjs.binary_static add %i, %one\n"
                   "    %read = ctjs.get_property %a[%offset]"),
           ArrayContentsFailure::MissingElement);
}

} // namespace ctcompile::test::escape::arrays::structured_detail
