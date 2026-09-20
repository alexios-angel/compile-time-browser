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
        reject("right shifts cannot assume an affine range from a nondivisible stride",
               replace(body, "add %i, %two\n  cf.br", "add %i, %one\n  cf.br"));
    }
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
          replace(negativeShift, "add %i, %two\n  cf.br", "add %i, %one\n  cf.br"),
          replace(negativeSignBoundary, "binary_static ushr", "binary_static shr"),
          replace(negativeShiftReload, "%base[%three]", "%base[%one]"),
          replace(negativeShiftReload,
                  "  %step =", "  ctjs.set_property %base[%three], %two\n  %step =")}) {
        reject("negative unsigned shifts retain band, bounds, stride and reload guards", body);
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
    for (const auto & body :
         {replace(signedMinimum, "4746794007248502784", "4746794007250599936"),
          replace(signedNegative, "add %i, %two\n  cf.br", "add %i, %one\n  cf.br"),
          replace(signedNegativeReload, "%base[%three]", "%base[%one]"),
          replace(signedNegativeReload,
                  "  %step =", "  ctjs.set_property %base[%three], %two\n  %step =")}) {
        reject("signed shift bands retain discontinuity, stride and full reload guards", body);
    }
    for (const auto & body :
         {replace(shiftReload, "%base[%three]", "%base[%one]"),
          replace(shiftReload, "  %step =", "  ctjs.set_property %base[%three], %two\n  %step ="),
          replace(rightShiftIndex, "shr %i, %one", "shr %one, %i"),
          replace(rightShiftIndex, "shr %i, %one", "shr %i, %s"),
          replace(rightShiftIndex, "shr %i, %one", "shr %i, %two"),
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
