#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::structured_detail {

void StructuredCases::reloads() {
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
