#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::structured_detail {

void StructuredCases::setup() {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %one = ctjs.constant #ctjs.number<4607182418800017408> {storage_test_id = \"one\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %y = ctjs.create_object {storage_test_id = \"y\"}\n"
        "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n"
        "  %flag = ctjs.truthy %p\n";
    const std::string select = "  %selected = scf.if %flag -> (!ctjs.value) {\n"
                               "    scf.yield %a : !ctjs.value\n"
                               "  } else {\n"
                               "    scf.yield %b : !ctjs.value\n"
                               "  }\n";
    const std::string done = "  ctjs.return %zero\n";
    rows = {
        {.what = "structured aliases retain separate strong-update targets",
         .body = values +
                 "  %b = ctjs.create_array [%y] {storage_test_id = \"b\"}\n"
                 "  %c = ctjs.create_array [%a, %b] {storage_test_id = \"c\"}\n" +
                 select + "  ctjs.set_property %selected[%zero], %zero\n  ctjs.return %c\n",
         .arrays = "a:[zero]; b:[y]; c:[a,b] | a:[x]; b:[zero]; c:[a,b]",
         .exit = "c -> {a,b,c,y}; c -> {a,b,c,x}"},
        {.what = "structured yields carry exact original Number facts into CFG operands",
         .body = values + "  %index = scf.if %flag -> (!ctjs.value) {\n"
                          "    %sum = ctjs.binary add %zero, %zero\n"
                          "    scf.yield %sum : !ctjs.value\n"
                          "  } else {\n"
                          "    scf.yield %zero : !ctjs.value\n"
                          "  }\n"
                          "  cf.br ^next(%a, %index : !ctjs.value, !ctjs.value)\n"
                          "^next(%base: !ctjs.value, %key: !ctjs.value):\n"
                          "  %read = ctjs.get_property %base[%key]\n  ctjs.return %read\n",
         .arrays = "a:[x] | a:[x]",
         .reads = "a[0]=x; a[0]=x",
         .exit = "x -> {x}; x -> {x}"},
        {.what = "structured scalar snapshots survive mutation of their source array",
         .body = values + "  %key = ctjs.constant #ctjs.string<\"length\">\n"
                          "  %saved = scf.if %flag -> (!ctjs.value) {\n"
                          "    %length = ctjs.get_property %a[%key]\n"
                          "    ctjs.append %y to %a\n"
                          "    scf.yield %length : !ctjs.value\n"
                          "  } else {\n"
                          "    ctjs.append %y to %a\n"
                          "    scf.yield %one : !ctjs.value\n"
                          "  }\n"
                          "  %read = ctjs.get_property %a[%saved]\n  ctjs.return %read\n",
         .arrays = "a:[x,y] | a:[x,y]",
         .reads = "a[1]=y; a[1]=y",
         .exit = "y -> {y}; y -> {y}"},
        {.what = "all structured results are transported together",
         .body = values + "  %left, %right = scf.if %flag -> (!ctjs.value, !ctjs.value) {\n"
                          "    scf.yield %x, %y : !ctjs.value, !ctjs.value\n"
                          "  } else {\n"
                          "    scf.yield %y, %x : !ctjs.value, !ctjs.value\n"
                          "  }\n"
                          "  ctjs.set_property %a[%zero], %left\n"
                          "  ctjs.append %right to %a\n  ctjs.return %a\n",
         .arrays = "a:[x,y] | a:[y,x]",
         .exit = "a -> {a,x,y}; a -> {a,x,y}"},
        {.what = "an implicit empty else retains the unmodified array",
         .body = values + "  scf.if %flag {\n    ctjs.set_property %a[%zero], %y\n  }\n"
                          "  ctjs.return %a\n",
         .arrays = "a:[y] | a:[x]",
         .exit = "a -> {a,y}; a -> {a,x}"},
        {.what = "nested structured aliases resume the correct enclosing yield",
         .body = values + "  %outer = scf.if %flag -> (!ctjs.value) {\n"
                          "    %inner = scf.if %flag -> (!ctjs.value) {\n"
                          "      scf.yield %x : !ctjs.value\n"
                          "    } else {\n"
                          "      scf.yield %y : !ctjs.value\n"
                          "    }\n    scf.yield %inner : !ctjs.value\n"
                          "  } else {\n    scf.yield %zero : !ctjs.value\n  }\n"
                          "  ctjs.return %outer\n",
         .arrays = "a:[x] | a:[x] | a:[x]",
         .exit = "x -> {x}; y -> {y}; zero -> {}"},
        {.what = "unused opaque structured results do not become retention facts",
         .body = values +
                 "  %unused = scf.if %flag -> (!ctjs.value) {\n"
                 "    scf.yield %a : !ctjs.value\n"
                 "  } else {\n    scf.yield %p : !ctjs.value\n  }\n" +
                 done,
         .arrays = "a:[x] | a:[x]",
         .exit = "zero -> {}; zero -> {}"},
        {.what = "an opaque structured result cannot borrow the other arm's array",
         .body = values + "  %base = scf.if %flag -> (!ctjs.value) {\n"
                          "    scf.yield %a : !ctjs.value\n"
                          "  } else {\n    scf.yield %p : !ctjs.value\n  }\n"
                          "  %read = ctjs.get_property %base[%zero]\n  ctjs.return %read\n",
         .failure = ArrayContentsFailure::UnknownArray},
        {.what = "an unmodified structured arm cannot borrow the other arm's appended slot",
         .body = values + "  scf.if %flag {\n    ctjs.append %y to %a\n  }\n"
                          "  %read = ctjs.get_property %a[%one]\n  ctjs.return %read\n",
         .failure = ArrayContentsFailure::MissingElement},
        {.what = "a constant structured condition does not conceal publication in its other arm",
         .body = values +
                 "  %known = ctjs.truthy %one\n"
                 "  scf.if %known {\n  } else {\n    ctjs.store_global \"held\", %a\n  }\n" +
                 done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "a failure after structured yields discards every earlier path",
         .body = values +
                 "  scf.if %flag {\n    ctjs.set_property %a[%zero], %y\n  }\n"
                 "  ctjs.store_global \"held\", %a\n" +
                 done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "an arbitrary structured loop has no finite own-length certificate",
         .body = values +
                 "  %loop = scf.while (%before = %a) : (!ctjs.value) -> !ctjs.value {\n"
                 "    scf.condition(%flag) %before : !ctjs.value\n"
                 "  } do {\n  ^body(%after: !ctjs.value):\n"
                 "    scf.yield %after : !ctjs.value\n  }\n" +
                 done,
         .failure = ArrayContentsFailure::UnsupportedControlFlow},
    };
    const std::string loop =
        "  %finalIndex, %result, %finalArray = scf.while "
        "(%array = %a, %index = %zero, %saved = %zero) : "
        "(!ctjs.value, !ctjs.value, !ctjs.value) -> "
        "(!ctjs.value, !ctjs.value, !ctjs.value) {\n"
        "    %key = ctjs.constant #ctjs.string<\"length\">\n"
        "    %length = ctjs.get_property %array[%key]\n"
        "    %less = ctjs.compare lt %index, %length\n"
        "    %continue = ctjs.truthy %less\n"
        "    scf.condition(%continue) %index, %saved, %array : "
        "!ctjs.value, !ctjs.value, !ctjs.value\n"
        "  } do {\n"
        "  ^body(%i: !ctjs.value, %last: !ctjs.value, %base: !ctjs.value):\n"
        "    %read = ctjs.get_property %base[%i]\n"
        "    %step = ctjs.binary_static add %i, %one\n"
        "    scf.yield %base, %step, %read : !ctjs.value, !ctjs.value, !ctjs.value\n"
        "  }\n";
    prefix = values + "  ctjs.append %y to %a\n";
    original = prefix + loop + "  ctjs.return %result\n";

    rows.push_back({.what = "structured induction preserves reordered condition and yield aliases",
                    .body = original,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    reversed = replace(original, "compare lt %index, %length", "compare gt %length, %index");
    rows.push_back({.what = "structured reversed strict guards preserve reordered aliases",
                    .body = reversed,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "structured reversed strict guards preserve zero-trip starts",
                    .body = replace(replace(reversed, "  ctjs.append %y to %a\n", ""),
                                    "%index = %zero", "%index = %one"),
                    .arrays = "a:[x]",
                    .exit = "zero -> {}"});
    negated = replace(replace(original, "compare lt %index, %length", "compare ge %index, %length"),
                      "    %continue = ctjs.truthy %less",
                      "    %negated = ctjs.unary not %less\n    %continue = ctjs.truthy %negated");
    negatedReversed = replace(negated, "compare ge %index, %length", "compare le %length, %index");
    for (const std::string & source : {negated, negatedReversed}) {
        rows.push_back({.what = "structured negated guards preserve reordered aliases",
                        .body = source,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "structured negated guards preserve zero-trip starts",
                        .body = replace(replace(source, "  ctjs.append %y to %a\n", ""),
                                        "%index = %zero", "%index = %one"),
                        .arrays = "a:[x]",
                        .exit = "zero -> {}"});
    }
    computedStart = prefix + "  %start = ctjs.unary plus %zero\n" +
                    replace(loop, "%index = %zero", "%index = %start") + "  ctjs.return %result\n";
    rows.push_back({.what = "structured induction accepts a transported computed Number zero",
                    .body = computedStart,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    const std::string savedLength = prefix +
                                    "  %seed = ctjs.create_array [] {storage_test_id = \"seed\"}\n"
                                    "  %name = ctjs.constant #ctjs.string<\"length\">\n"
                                    "  %start = ctjs.get_property %seed[%name]\n"
                                    "  ctjs.append %one to %seed\n" +
                                    replace(loop, "%index = %zero", "%index = %start") +
                                    "  ctjs.return %result\n";
    rows.push_back({.what = "a saved zero length survives mutation before structured induction",
                    .body = savedLength,
                    .arrays = "a:[x,y]; seed:[one]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    const std::string nonzeroStart = replace(computedStart, "unary plus %zero", "unary plus %one");
    rows.push_back({.what = "structured induction starts from a computed nonzero Number",
                    .body = nonzeroStart,
                    .arrays = "a:[x,y]",
                    .reads = "a[1]=y",
                    .exit = "y -> {y}"});
    alternateStart = replace(computedStart, "  %start = ctjs.unary plus %zero\n",
                             "  %start = scf.if %flag -> (!ctjs.value) {\n"
                             "    %zeroResult = ctjs.unary plus %zero\n"
                             "    scf.yield %zeroResult : !ctjs.value\n"
                             "  } else {\n    scf.yield %one : !ctjs.value\n  }\n");
    rows.push_back({.what = "structured predecessor starts keep their exact separate paths",
                    .body = alternateStart,
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[1]=y",
                    .exit = "y -> {y}; y -> {y}"});
    rows.push_back(
        {.what = "a structured start keeps its saved length after its array is emptied",
         .body = replace(replace(savedLength, "%seed = ctjs.create_array []",
                                 "%seed = ctjs.create_array [%one]"),
                         "ctjs.append %one to %seed", "ctjs.set_property %seed[%name], %zero"),
         .arrays = "a:[x,y]; seed:[]",
         .reads = "a[1]=y",
         .exit = "y -> {y}"});
    rows.push_back({.what = "a structured start at length keeps the initial saved child",
                    .body = replace(replace(nonzeroStart, "  ctjs.append %y to %a\n", ""),
                                    "%saved = %zero", "%saved = %x"),
                    .arrays = "a:[x]",
                    .exit = "x -> {x}"});
    makeUnit = "  %unit = ctjs.unary plus %one\n";
    const std::string unitLoop = replace(loop, "add %i, %one", "add %i, %unit");
    computedUnit = prefix + makeUnit + unitLoop + "  ctjs.return %result\n";
    rows.push_back({.what = "structured induction accepts an independently computed unit step",
                    .body = computedUnit,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    savedUnit = prefix +
                "  %seed = ctjs.create_array [%one] {storage_test_id = \"seed\"}\n"
                "  %name = ctjs.constant #ctjs.string<\"length\">\n"
                "  %unit = ctjs.get_property %seed[%name]\n"
                "  ctjs.set_property %seed[%name], %zero\n" +
                unitLoop + "  ctjs.return %result\n";
    rows.push_back({.what = "structured unit steps preserve saved length after the source shrinks",
                    .body = savedUnit,
                    .arrays = "a:[x,y]; seed:[]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    carriedUnit = replace(computedUnit, "%finalIndex, %result, %finalArray =",
                          "%finalIndex, %result, %finalArray, %finalUnit =");
    carriedUnit = replace(carriedUnit, "%saved = %zero)", "%saved = %zero, %delta = %unit)");
    carriedUnit = replace(carriedUnit, "(!ctjs.value, !ctjs.value, !ctjs.value) ->",
                          "(!ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value) ->");
    carriedUnit = replace(carriedUnit, "-> (!ctjs.value, !ctjs.value, !ctjs.value) {",
                          "-> (!ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value) {");
    carriedUnit = replace(
        carriedUnit, "%index, %saved, %array :", "%index, %saved, %array, %delta : !ctjs.value,");
    carriedUnit =
        replace(carriedUnit, "%base: !ctjs.value):", "%base: !ctjs.value, %d: !ctjs.value):");
    carriedUnit = replace(carriedUnit, "add %i, %unit", "add %i, %d");
    carriedUnit =
        replace(carriedUnit, "%base, %step, %read :", "%base, %step, %read, %d : !ctjs.value,");
    rows.push_back({.what = "unit facts survive reordered structured condition and yield transport",
                    .body = carriedUnit,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    alternateUnit = replace(computedUnit, makeUnit,
                            "  %unit = scf.if %flag -> (!ctjs.value) {\n"
                            "    %proved = ctjs.unary plus %one\n"
                            "    scf.yield %proved : !ctjs.value\n"
                            "  } else {\n    scf.yield %one : !ctjs.value\n  }\n");
    rows.push_back({.what = "independently proved structured unit alternatives keep both paths",
                    .body = alternateUnit,
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
                    .exit = "y -> {y}; y -> {y}"});
    makeStride = "  %unit = ctjs.binary add %one, %one\n";
    const std::string stride = replace(computedUnit, makeUnit, makeStride);
    rows.push_back({.what = "structured induction accepts an exact computed positive stride",
                    .body = stride,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    carriedStride = replace(carriedUnit, makeUnit, makeStride);
    rows.push_back({.what = "positive strides survive reordered structured backedge transport",
                    .body = replace(carriedStride, "  ctjs.append %y to %a\n",
                                    "  ctjs.append %y to %a\n  ctjs.append %y to %a\n"),
                    .arrays = "a:[x,y,y]",
                    .reads = "a[0]=x; a[2]=y",
                    .exit = "y -> {y}"});
    rows.push_back(
        {.what = "each structured predecessor keeps its own positive stride",
         .body = replace(alternateUnit, "ctjs.unary plus %one", "ctjs.binary add %one, %one"),
         .arrays = "a:[x,y] | a:[x,y]",
         .reads = "a[0]=x; a[0]=x; a[1]=y",
         .exit = "x -> {x}; y -> {y}"});
    rows.push_back({.what = "structured positive strides retain read-time length after shrink",
                    .body = replace(savedUnit, "%seed = ctjs.create_array [%one]",
                                    "%seed = ctjs.create_array [%one, %one]"),
                    .arrays = "a:[x,y]; seed:[]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    overshoot = replace(stride, "ctjs.binary add %one, %one",
                        "ctjs.constant #ctjs.number<4613937818241073152>");
    rows.push_back({.what = "a structured overshoot preserves its exact final Number after growth",
                    .body = replace(overshoot, "  ctjs.return %result",
                                    "  ctjs.append %zero to %finalArray\n"
                                    "  ctjs.append %zero to %finalArray\n"
                                    "  %after = ctjs.get_property %finalArray[%finalIndex]\n"
                                    "  ctjs.return %after"),
                    .arrays = "a:[x,y,zero,zero]",
                    .reads = "a[0]=x; a[3]=zero",
                    .exit = "zero -> {}"});
    maxStride = replace(computedUnit, "ctjs.unary plus %one",
                        "ctjs.constant #ctjs.number<4751297606873776128>");
    rows.push_back({.what = "structured induction preserves the maximum exact stride at exit",
                    .body = replace(maxStride, "  ctjs.return %result",
                                    "  %slot = ctjs.binary sub %finalIndex, %unit\n"
                                    "  %after = ctjs.get_property %finalArray[%slot]\n"
                                    "  ctjs.return %after"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[0]=x",
                    .exit = "x -> {x}"});
    rows.push_back(
        {.what = "structured zero-trip induction does not apply its maximum stride",
         .body = replace(replace(maxStride, "[%x]", "[]"), "  ctjs.append %y to %a\n", ""),
         .arrays = "a:[]",
         .exit = "zero -> {}"});
    rows.push_back({.what = "structured overshoot is relative to its nonzero start",
                    .body = replace(replace(stride, "%index = %zero", "%index = %one"),
                                    "  ctjs.return %result",
                                    "  ctjs.append %zero to %finalArray\n"
                                    "  ctjs.append %x to %finalArray\n"
                                    "  %after = ctjs.get_property %finalArray[%finalIndex]\n"
                                    "  ctjs.return %after"),
                    .arrays = "a:[x,y,zero,x]",
                    .reads = "a[1]=y; a[3]=x",
                    .exit = "x -> {x}"});
    rows.push_back({.what = "the largest structured start preserves its zero-trip Number",
                    .body = replace(replace(maxStride, "%index = %zero", "%index = %unit"),
                                    "  ctjs.return %result",
                                    "  %slot = ctjs.binary sub %finalIndex, %unit\n"
                                    "  %after = ctjs.get_property %finalArray[%slot]\n"
                                    "  ctjs.return %after"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    rows.push_back(
        {.what = "a nonzero structured start skips an empty array's body",
         .body = replace(replace(nonzeroStart, "[%x]", "[]"), "  ctjs.append %y to %a\n", ""),
         .arrays = "a:[]",
         .exit = "zero -> {}"});
    rows.push_back(
        {.what = "a zero-trip structured loop returns its initial scalar",
         .body = replace(replace(original, "[%x]", "[]"), "  ctjs.append %y to %a\n", ""),
         .arrays = "a:[]",
         .exit = "zero -> {}"});
    rows.push_back({.what = "a one-trip structured loop reads exactly one own element",
                    .body = replace(original, "  ctjs.append %y to %a\n", ""),
                    .arrays = "a:[x]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    rows.push_back({.what = "a structured loop result retains its read-time Number after mutation",
                    .body = prefix + loop +
                            "  ctjs.append %zero to %finalArray\n"
                            "  %after = ctjs.get_property %finalArray[%finalIndex]\n"
                            "  ctjs.return %after\n",
                    .arrays = "a:[x,y,zero]",
                    .reads = "a[0]=x; a[1]=y; a[2]=zero",
                    .exit = "zero -> {}"});
    rows.push_back({.what = "a structured loop result transports its saved child into CFG",
                    .body = prefix + loop +
                            "  cf.br ^exit(%result : !ctjs.value)\n"
                            "^exit(%child: !ctjs.value):\n  ctjs.return %child\n",
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "structured induction resumes an enclosing if yield on each path",
                    .body = prefix + "  %selected = scf.if %flag -> (!ctjs.value) {\n" + loop +
                            "    scf.yield %result : !ctjs.value\n"
                            "  } else {\n    scf.yield %x : !ctjs.value\n  }\n"
                            "  ctjs.return %selected\n",
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}; x -> {x}"});
    rows.push_back({.what = "a later structured loop has a fresh guard and exact contents",
                    .body = prefix + loop +
                            replace(replace(replace(loop, "%finalIndex, %result, %finalArray",
                                                    "%nextIndex, %nextResult, %nextArray"),
                                            "%array = %a", "%array = %finalArray"),
                                    "%saved = %zero", "%saved = %result") +
                            "  ctjs.return %nextResult\n",
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
}

} // namespace ctcompile::test::escape::arrays::structured_detail
