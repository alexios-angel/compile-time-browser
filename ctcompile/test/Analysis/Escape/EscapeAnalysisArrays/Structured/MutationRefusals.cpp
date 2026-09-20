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
    for (const auto & expression : {"mul %i, %one", "mul %one, %i"}) {
        rows.push_back({.what = "structured unit scaling preserves current-element overwrites",
                        .body = replace(scaledIndex, "mul %i, %one", expression),
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
    for (const auto & body :
         {replace(scaledVisit, "add %i, %two", "add %i, %one"),
          replace(scaledStart, "create_array [%one, %one]", "create_array [%one]"),
          replace(scaledVisit, "mul %i, %two", "mul %i, %zero"),
          replace(scaledVisit, "mul %i, %two", "mul %i, %last"),
          replace(reloadedFactor,
                  "    %step =", "    ctjs.set_property %base[%one], %one\n    %step ="),
          replace(replace(scaledStart, "ctjs.append %x to %a", "ctjs.append %two to %a"),
                  "    %position = ctjs.binary mul %i, %two",
                  "    %factor = ctjs.get_property %base[%two]\n"
                  "    %position = ctjs.binary mul %i, %factor")}) {
        reject("structured scaled stores retain bounds, invariance and reload exclusions", body);
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
    const auto reverseIndex = replace(scaledIndex, "mul %i, %one", "sub %one, %i");
    rows.push_back({.what = "structured reverse subtraction releases descending own children",
                    .body = reverseIndex,
                    .arrays = "a:[zero,zero]",
                    .reads = "a[0]=x; a[1]=zero",
                    .exit = "a -> {a}"});
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
    for (const auto & expression : {"mul %i, %negative", "mul %negative, %i"}) {
        rows.push_back({.what = "structured negative scaling reverses exact own writes",
                        .body = replace(negativeScale, "mul %i, %negative", expression),
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
