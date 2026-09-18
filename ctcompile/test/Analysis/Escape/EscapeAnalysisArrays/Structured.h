#pragma once

#include "Harness.h"

namespace ctcompile::test::escape::arrays {

inline void checkStructuredContents(mlir::MLIRContext & context) {
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
    std::vector<contents_row> rows = {
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
    const std::string prefix = values + "  ctjs.append %y to %a\n";
    const std::string original = prefix + loop + "  ctjs.return %result\n";
    const auto replace = [](std::string source, const std::string & before,
                            const std::string & after) {
        const std::size_t position = source.find(before);
        assert(position != std::string::npos);
        source.replace(position, before.size(), after);
        return source;
    };
    rows.push_back({.what = "structured induction preserves reordered condition and yield aliases",
                    .body = original,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    const std::string reversed =
        replace(original, "compare lt %index, %length", "compare gt %length, %index");
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
    const std::string negated =
        replace(replace(original, "compare lt %index, %length", "compare ge %index, %length"),
                "    %continue = ctjs.truthy %less",
                "    %negated = ctjs.unary not %less\n    %continue = ctjs.truthy %negated");
    const std::string negatedReversed =
        replace(negated, "compare ge %index, %length", "compare le %length, %index");
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
    const std::string computedStart = prefix + "  %start = ctjs.unary plus %zero\n" +
                                      replace(loop, "%index = %zero", "%index = %start") +
                                      "  ctjs.return %result\n";
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
    const std::string alternateStart =
        replace(computedStart, "  %start = ctjs.unary plus %zero\n",
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
    const std::string makeUnit = "  %unit = ctjs.unary plus %one\n";
    const std::string unitLoop = replace(loop, "add %i, %one", "add %i, %unit");
    const std::string computedUnit = prefix + makeUnit + unitLoop + "  ctjs.return %result\n";
    rows.push_back({.what = "structured induction accepts an independently computed unit step",
                    .body = computedUnit,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    const std::string savedUnit =
        prefix +
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
    std::string carriedUnit = replace(computedUnit, "%finalIndex, %result, %finalArray =",
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
    const std::string alternateUnit =
        replace(computedUnit, makeUnit,
                "  %unit = scf.if %flag -> (!ctjs.value) {\n"
                "    %proved = ctjs.unary plus %one\n"
                "    scf.yield %proved : !ctjs.value\n"
                "  } else {\n    scf.yield %one : !ctjs.value\n  }\n");
    rows.push_back({.what = "independently proved structured unit alternatives keep both paths",
                    .body = alternateUnit,
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
                    .exit = "y -> {y}; y -> {y}"});
    const std::string makeStride = "  %unit = ctjs.binary add %one, %one\n";
    const std::string stride = replace(computedUnit, makeUnit, makeStride);
    rows.push_back({.what = "structured induction accepts an exact computed positive stride",
                    .body = stride,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    const std::string carriedStride = replace(carriedUnit, makeUnit, makeStride);
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
    const std::string overshoot = replace(stride, "ctjs.binary add %one, %one",
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
    const std::string maxStride = replace(computedUnit, "ctjs.unary plus %one",
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
    const auto reject = [&](const char * what, std::string body,
                            ArrayContentsFailure failure =
                                ArrayContentsFailure::UnsupportedControlFlow) {
        rows.push_back({.what = what, .body = std::move(body), .failure = failure});
    };
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
    reject("a structured repeated step producer needs a separate invariant proof",
           replace(replace(computedUnit, makeUnit, ""), "    %step =", makeUnit + "    %step ="));
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
    reject("structured Sub cannot borrow a String stride",
           replace(subtract, negativeLiteral, "#ctjs.string<\"-1\">"));
    reject("structured Sub cannot borrow a BigInt stride",
           replace(subtract, negativeLiteral, "#ctjs.bigint<\"-1\">"));
    reject("structured Sub cannot borrow an unknown stride",
           replace(subtract, "sub %i, %minus", "sub %i, %p"));
    reject("a structured computed negative stride needs an independent invariant proof",
           replace(subtractNegated, "unary neg %one", "binary sub %zero, %one"));
    reject("structured negative subtraction still bounds its final update",
           replace(replace(subtract, negativeLiteral, "#ctjs.number<13974669643728551936>"),
                   "%index = %zero", "%index = %one"));
    const std::string makeNegative = "  %magnitude = ctjs.binary add %one, %one\n"
                                     "  %unit = ctjs.unary neg %magnitude\n";
    const auto carriedNegative = replace(replace(carriedUnit, makeUnit, makeNegative),
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
    const auto savedNegative =
        replace(replace(savedUnit, "  %unit = ctjs.get_property %seed[%name]",
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
    reject("a repeated structured Neg producer cannot borrow a prior iteration",
           replace(replace(directNegative, "  %unit = ctjs.unary neg %magnitude\n", ""),
                   "    %step =", "    %unit = ctjs.unary neg %magnitude\n    %step ="));
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
    const auto carriedComplement = replace(carriedNegative, makeNegative,
                                           "  %magnitude = ctjs.unary plus %one\n"
                                           "  %unit = ctjs.unary bitnot %magnitude\n");
    rows.push_back({.what = "BitNot keeps its signed snapshot through reordered structured yields",
                    .body = carriedComplement,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    rows.push_back({.what = "structured BitNot snapshots release only unreturned children",
                    .body = replace(carriedComplement, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x",
                    .exit = "zero -> {}"});
    const auto savedComplement =
        replace(savedNegative, "unary neg %magnitude", "unary bitnot %magnitude");
    rows.push_back({.what = "structured BitNot retains its source length before shrink",
                    .body = savedComplement,
                    .arrays = "a:[x,y]; seed:[]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    rows.push_back({.what = "structured BitNot predecessors preserve their own exact magnitude",
                    .body = replace(carriedComplement, "  %unit = ctjs.unary bitnot %magnitude\n",
                                    "  %unit = scf.if %flag -> (!ctjs.value) {\n"
                                    "    %twoStep = ctjs.unary bitnot %magnitude\n"
                                    "    scf.yield %twoStep : !ctjs.value\n"
                                    "  } else {\n"
                                    "    %oneStep = ctjs.unary bitnot %zero\n"
                                    "    scf.yield %oneStep : !ctjs.value\n  }\n"),
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[0]=x; a[1]=y",
                    .exit = "x -> {x}; y -> {y}"});
    reject("a BitNot snapshot cannot change across structured yields",
           replace(carriedComplement, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    const auto stringComplement =
        replace(carriedComplement, "  %magnitude = ctjs.unary plus %one\n",
                "  %text = ctjs.constant #ctjs.string<\"1\">\n"
                "  %magnitude = scf.if %flag -> (!ctjs.value) {\n"
                "    scf.yield %one : !ctjs.value\n"
                "  } else {\n    scf.yield %text : !ctjs.value\n  }\n");
    rows.push_back({.what = "BitNot preserves original Number and String predecessor snapshots",
                    .body = stringComplement,
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[0]=x",
                    .exit = "x -> {x}; x -> {x}"});
    rows.push_back({.what = "String BitNot discharges only unreturned structured children",
                    .body = replace(stringComplement, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[0]=x",
                    .exit = "zero -> {}; zero -> {}"});
    reject("BitNot cannot borrow a canonical String from another structured predecessor",
           replace(stringComplement, "#ctjs.string<\"1\">", "#ctjs.string<\"01\">"));
    reject("a String BitNot snapshot cannot change across structured yields",
           replace(stringComplement, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    const auto primitiveComplement =
        replace(carriedComplement, "  %magnitude = ctjs.unary plus %one\n",
                "  %magnitude = scf.if %flag -> (!ctjs.value) {\n"
                "    %boolean = ctjs.constant #ctjs.boolean<true>\n"
                "    scf.yield %boolean : !ctjs.value\n"
                "  } else {\n"
                "    %null = ctjs.constant #ctjs.null\n"
                "    scf.yield %null : !ctjs.value\n  }\n");
    rows.push_back({.what = "Boolean/null unary results retain separate structured snapshots",
                    .body = primitiveComplement,
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[0]=x; a[1]=y",
                    .exit = "x -> {x}; y -> {y}"});
    rows.push_back(
        {.what = "primitive unary snapshots release only unreturned structured children",
         .body = replace(primitiveComplement, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[x,y] | a:[x,y]",
         .reads = "a[0]=x; a[0]=x; a[1]=y",
         .exit = "zero -> {}; zero -> {}"});
    reject(
        "a primitive unary result cannot change across structured yields",
        replace(primitiveComplement, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    reject("an Undefined predecessor cannot borrow the Boolean/null Number proof",
           replace(primitiveComplement, "#ctjs.null", "#ctjs.undefined"));
    for (const std::string kind : {"bitand", "bitor", "bitxor", "shl", "shr"}) {
        const std::string right = kind == "bitand" ? "%operand" : "%zero";
        const std::string operation =
            "  %unit = ctjs.binary_static " + kind + " %operand, " + right + "\n";
        const auto carried = replace(carriedNegative, makeNegative,
                                     "  %operand = ctjs.unary neg %one\n" + operation);
        rows.push_back({.what = "signed bitwise snapshots survive reordered structured yields",
                        .body = carried,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        const auto saved = replace(savedNegative, "  %unit = ctjs.unary neg %magnitude\n",
                                   "  %operand = ctjs.unary neg %magnitude\n" + operation);
        rows.push_back({.what = "structured bitwise snapshots survive source length shrink",
                        .body = saved,
                        .arrays = "a:[x,y]; seed:[]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "structured bitwise snapshots release only unreturned children",
                        .body = replace(saved, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]; seed:[]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
        reject("signed bitwise snapshots cannot change across structured yields",
               replace(carried, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
        reject("bitwise snapshots cannot borrow Numbers from another structured predecessor",
               replace(carried, "  %operand = ctjs.unary neg %one\n",
                       "  %negative = ctjs.unary neg %one\n"
                       "  %text = ctjs.constant #ctjs.string<\"-1\">\n"
                       "  %operand = scf.if %flag -> (!ctjs.value) {\n"
                       "    scf.yield %negative : !ctjs.value\n"
                       "  } else {\n    scf.yield %text : !ctjs.value\n  }\n"));
    }
    const std::string unsignedOperation = "  %count = ctjs.unary neg %one\n"
                                          "  %unit = ctjs.binary_static ushr %operand, %count\n";
    const auto unsignedCarried =
        replace(carriedUnit, makeUnit, "  %operand = ctjs.unary neg %one\n" + unsignedOperation);
    const auto unsignedSaved = replace(savedUnit, "  %unit = ctjs.get_property %seed[%name]\n",
                                       "  %magnitude = ctjs.get_property %seed[%name]\n"
                                       "  %operand = ctjs.unary neg %magnitude\n" +
                                           unsignedOperation);
    for (const auto & source : {unsignedCarried, unsignedSaved}) {
        const char * arrays = source == unsignedCarried ? "a:[x,y]" : "a:[x,y]; seed:[]";
        rows.push_back({.what = "unsigned shifts preserve signed snapshots through structured flow",
                        .body = source,
                        .arrays = arrays,
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "structured unsigned shifts release only unreturned children",
                        .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = arrays,
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
    }
    reject("an unsigned shift snapshot cannot change across structured yields",
           replace(unsignedCarried, "%base, %step, %read, %d :", "%base, %step, %read, %zero :"));
    const auto stringCount = replace(unsignedSaved, "  %count = ctjs.unary neg %one",
                                     "  %count = ctjs.constant #ctjs.string<\"31\">");
    rows.push_back(
        {.what = "String counts preserve signed snapshots after structured source shrink",
         .body = stringCount,
         .arrays = "a:[x,y]; seed:[]",
         .reads = "a[0]=x; a[1]=y",
         .exit = "y -> {y}"});
    rows.push_back({.what = "String shift snapshots release only unreturned structured children",
                    .body = replace(stringCount, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]; seed:[]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    reject("structured String shifts cannot borrow noncanonical counts",
           replace(stringCount, "#ctjs.string<\"31\">", "#ctjs.string<\"031\">"));
    const auto subSnapshot =
        replace(savedNegative, "unary neg %magnitude", "binary sub %zero, %magnitude");
    rows.push_back(
        {.what = "structured Sub snapshots retain their child after source length shrink",
         .body = subSnapshot,
         .arrays = "a:[x,y]; seed:[]",
         .reads = "a[0]=x; a[1]=y",
         .exit = "y -> {y}"});
    rows.push_back({.what = "structured Sub snapshots discharge only unreturned children",
                    .body = replace(subSnapshot, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]; seed:[]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    rows.push_back({.what = "zero-trip structured Sub snapshots preserve the original result",
                    .body = replace(replace(subSnapshot, "  ctjs.append %y to %a\n", ""),
                                    "%index = %zero", "%index = %one"),
                    .arrays = "a:[x]; seed:[]",
                    .exit = "zero -> {}"});
    const auto carriedSub =
        replace(carriedNegative, "unary neg %magnitude", "binary sub %one, %magnitude");
    rows.push_back({.what = "structured Sub snapshots preserve exact reordered transport",
                    .body = carriedSub,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    reject("structured Sub snapshots cannot change on the backedge",
           replace(carriedSub, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    reject("structured Add cannot borrow a negative Sub snapshot",
           replace(carriedSub, "binary sub %i, %d", "binary add %i, %d"));
    for (const std::string producer : {"binary sub %zero, %zero", "binary sub %magnitude, %zero"}) {
        reject("structured Sub snapshots preserve the zero and positive stride refusals",
               replace(subSnapshot, "binary sub %zero, %magnitude", producer));
    }
    const auto stringSub =
        replace(carriedSub, "binary add %one, %one", "constant #ctjs.string<\"2\">");
    rows.push_back({.what = "canonical String offsets survive reordered structured transport",
                    .body = stringSub,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    const auto leftStringSub = replace(carriedSub, "  %unit = ctjs.binary sub %one, %magnitude",
                                       "  %left = ctjs.constant #ctjs.string<\"1\">\n"
                                       "  %unit = ctjs.binary sub %left, %magnitude");
    rows.push_back({.what = "left String subtraction survives reordered structured transport",
                    .body = leftStringSub,
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "structured left String subtraction releases only unreturned children",
                    .body = replace(leftStringSub, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    reject("left String subtraction snapshots cannot change across structured yields",
           replace(leftStringSub, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    const auto leftPredecessors =
        replace(leftStringSub, "  %left = ctjs.constant #ctjs.string<\"1\">\n",
                "  %left = scf.if %flag -> (!ctjs.value) {\n"
                "    %good = ctjs.constant #ctjs.string<\"1\">\n"
                "    scf.yield %good : !ctjs.value\n"
                "  } else {\n    scf.yield %one : !ctjs.value\n  }\n");
    rows.push_back({.what = "left subtraction proves String and Number predecessors independently",
                    .body = leftPredecessors,
                    .arrays = "a:[x,y] | a:[x,y]",
                    .reads = "a[0]=x; a[1]=y; a[0]=x; a[1]=y",
                    .exit = "y -> {y}; y -> {y}"});
    reject("left subtraction cannot borrow another predecessor's canonical String",
           replace(leftPredecessors, "scf.yield %one : !ctjs.value",
                   "%bad = ctjs.constant #ctjs.string<\"01\">\n"
                   "    scf.yield %bad : !ctjs.value"));
    const auto savedStringSub = replace(subSnapshot, "  %unit = ctjs.binary sub %zero, %magnitude",
                                        "  %offset = ctjs.constant #ctjs.string<\"2\">\n"
                                        "  %unit = ctjs.binary sub %magnitude, %offset");
    rows.push_back({.what = "structured String-offset snapshots survive source length shrink",
                    .body = savedStringSub,
                    .arrays = "a:[x,y]; seed:[]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "y -> {y}"});
    rows.push_back({.what = "structured String offsets release only unreturned children",
                    .body = replace(savedStringSub, "ctjs.return %result", "ctjs.return %zero"),
                    .arrays = "a:[x,y]; seed:[]",
                    .reads = "a[0]=x; a[1]=y",
                    .exit = "zero -> {}"});
    reject("a String-offset snapshot cannot change across structured yields",
           replace(stringSub, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    reject("a canonical String offset cannot authorize a noncanonical predecessor",
           replace(stringSub, "  %magnitude = ctjs.constant #ctjs.string<\"2\">\n",
                   "  %magnitude = scf.if %flag -> (!ctjs.value) {\n"
                   "    %good = ctjs.constant #ctjs.string<\"2\">\n"
                   "    scf.yield %good : !ctjs.value\n"
                   "  } else {\n"
                   "    %bad = ctjs.constant #ctjs.string<\"02\">\n"
                   "    scf.yield %bad : !ctjs.value\n  }\n"));
    reject("structured negative Sub snapshots cannot be own indices",
           replace(subSnapshot, "%base[%i]", "%base[%unit]"), ArrayContentsFailure::UnknownIndex);
    reject("repeated structured Sub producers need independent invariance",
           replace(replace(subSnapshot, "  %unit = ctjs.binary sub %zero, %magnitude\n", ""),
                   "    %step =", "    %unit = ctjs.binary sub %zero, %magnitude\n    %step ="));
    reject("structured negative Sub snapshots still bound the last exact update",
           replace(replace(replace(carriedSub, "binary sub %one, %magnitude",
                                   "binary sub %zero, %magnitude"),
                           "binary add %one, %one", "constant #ctjs.number<4751297606873776128>"),
                   "%index = %zero", "%index = %one"));
    const std::string signedDifference = "  %negative = ctjs.unary neg %magnitude\n"
                                         "  %other = ctjs.unary neg %one\n"
                                         "  %unit = ctjs.binary sub %negative, %other\n";
    const auto negativeLeft =
        replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n", signedDifference);
    for (const auto & source : {negativeLeft, replace(replace(negativeLeft, "sub %negative, %other",
                                                              "sub %other, %negative"),
                                                      "binary sub %i, %d", "binary add %i, %d")}) {
        rows.push_back({.what = "negative-left Sub survives reordered structured transport",
                        .body = source,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "structured negative-left Sub releases unreturned children",
                        .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
    }
    reject("negative-left Sub snapshots cannot change across structured yields",
           replace(negativeLeft, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
    rows.push_back({.what = "negative-left String offsets retain their structured result",
                    .body = replace(negativeLeft, "unary neg %one", "constant #ctjs.string<\"1\">"),
                    .arrays = "a:[x,y]",
                    .reads = "a[0]=x",
                    .exit = "x -> {x}"});
    reject("negative-left Sub needs exact Numbers on every structured predecessor",
           replace(negativeLeft, "  %negative = ctjs.unary neg %magnitude\n",
                   "  %negative = scf.if %flag -> (!ctjs.value) {\n"
                   "    %n = ctjs.unary neg %magnitude\n"
                   "    scf.yield %n : !ctjs.value\n"
                   "  } else {\n    scf.yield %p : !ctjs.value\n  }\n"),
           ArrayContentsFailure::UnsupportedOperation);
    for (const std::string opcode : {"binary", "binary_static"}) {
        const std::string cancellation = "  %negative = ctjs.binary sub %zero, %one\n"
                                         "  %positive = ctjs.binary add %one, %one\n"
                                         "  %unit = ctjs." +
                                         opcode + " add %positive, %negative\n";
        const auto cancelled = replace(carriedUnit, makeUnit, cancellation);
        rows.push_back({.what = "Add cancellation survives reordered structured transport",
                        .body = cancelled,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "cancelled structured strides release unreturned children",
                        .body = replace(cancelled, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
        const auto zero = replace(cancelled, "add %positive, %negative", "add %one, %negative");
        rows.push_back({.what = "structured cancellation to zero remains a valid start",
                        .body = replace(replace(zero, "%index = %zero", "%index = %unit"),
                                        "add %i, %d", "add %i, %one"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        reject("structured cancellation to zero cannot certify progress", zero);
        reject("a cancelled structured stride must remain unchanged on the backedge",
               replace(cancelled, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
        reject("structured cancellation cannot borrow a coercible String",
               replace(cancelled, "binary sub %zero, %one", "constant #ctjs.string<\"-1\">"));
        reject("structured cancellation cannot borrow another arm's Number",
               replace(cancelled, "  %negative = ctjs.binary sub %zero, %one\n",
                       "  %negative = scf.if %flag -> (!ctjs.value) {\n"
                       "    %n = ctjs.binary sub %zero, %one\n"
                       "    scf.yield %n : !ctjs.value\n"
                       "  } else {\n    scf.yield %p : !ctjs.value\n  }\n"),
               opcode == "binary" ? ArrayContentsFailure::UnsupportedOperation
                                  : ArrayContentsFailure::UnknownValue);
    }
    for (const std::string opcode : {"binary", "binary_static"}) {
        const std::string negativeSum = "  %negative = ctjs.unary neg %magnitude\n"
                                        "  %unit = ctjs." +
                                        opcode + " add %negative, %one\n";
        const auto source =
            replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n", negativeSum);
        rows.push_back({.what = "negative Add survives reordered structured transport",
                        .body = source,
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "y -> {y}"});
        rows.push_back({.what = "structured negative Add releases unreturned children",
                        .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                        .arrays = "a:[x,y]",
                        .reads = "a[0]=x; a[1]=y",
                        .exit = "zero -> {}"});
        reject("negative Add snapshots cannot change across structured yields",
               replace(source, "%base, %step, %read, %d :", "%base, %step, %read, %one :"));
        reject("negative Add needs an exact Number on every structured predecessor",
               replace(source, "  %negative = ctjs.unary neg %magnitude\n",
                       "  %negative = scf.if %flag -> (!ctjs.value) {\n"
                       "    %n = ctjs.unary neg %magnitude\n"
                       "    scf.yield %n : !ctjs.value\n"
                       "  } else {\n    scf.yield %p : !ctjs.value\n  }\n"),
               opcode == "binary" ? ArrayContentsFailure::UnsupportedOperation
                                  : ArrayContentsFailure::UnknownValue);
    }
    for (const std::string operation : {"div", "mod", "pow"}) {
        const std::string makeResult =
            "  %unit = ctjs.binary " + operation +
            (operation != "mod" ? " %negative, %one\n" : " %negative, %two\n");
        const auto source = replace(replace(savedNegative, "  %unit = ctjs.unary neg %magnitude\n",
                                            "  %negative = ctjs.unary neg %magnitude\n"),
                                    "  ctjs.set_property %seed[%name], %zero\n",
                                    "  ctjs.set_property %seed[%name], %zero\n"
                                    "  %two = ctjs.binary add %one, %one\n" +
                                        makeResult);
        rows.push_back(
            {.what = "structured signed division, remainder and power retain pre-shrink Numbers",
             .body = source,
             .arrays = "a:[x,y]; seed:[]",
             .reads = "a[0]=x; a[1]=y",
             .exit = "y -> {y}"});
        rows.push_back(
            {.what = "structured signed division, remainder and power release unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[x,y]; seed:[]",
             .reads = "a[0]=x; a[1]=y",
             .exit = "zero -> {}"});
        reject("repeated structured division, remainder and power need independent invariance",
               replace(replace(source, makeResult, ""), "    %step =", makeResult + "    %step ="));
    }
    for (const std::string operation : {"div", "mod", "pow"}) {
        const std::string literal = "  %text = ctjs.constant #ctjs.string<\"" +
                                    std::string(operation == "mod" ? "2" : "1") + "\">\n";
        const std::string makeResult = "  %negative = ctjs.unary neg %one\n" + literal +
                                       "  %unit = ctjs.binary " + operation + " %negative, %text\n";
        const auto stringRight =
            replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n", makeResult);
        const auto stringLeft = replace(
            replace(replace(stringRight, literal, "  %text = ctjs.constant #ctjs.string<\"1\">\n"),
                    "%negative, %text", operation == "div" ? "%text, %one" : "%text, %magnitude"),
            "binary sub %i, %d", "binary add %i, %d");
        for (const auto & body : {stringRight, stringLeft}) {
            rows.push_back({.what = "String arithmetic survives structured transport",
                            .body = body,
                            .arrays = "a:[x,y]",
                            .reads = "a[0]=x; a[1]=y",
                            .exit = "y -> {y}"});
            rows.push_back({.what = "structured String arithmetic releases unreturned children",
                            .body = replace(body, "ctjs.return %result", "ctjs.return %zero"),
                            .arrays = "a:[x,y]",
                            .reads = "a[0]=x; a[1]=y",
                            .exit = "zero -> {}"});
            reject("a String arithmetic snapshot cannot change on the structured backedge",
                   replace(body, "%base, %step, %read, %d :", "%base, %step, %read, %zero :"));
        }
        reject("String arithmetic cannot borrow a canonical value from another predecessor",
               replace(stringRight, literal,
                       "  %text = scf.if %flag -> (!ctjs.value) {\n" +
                           replace(literal, "%text =", "%good =") +
                           "    scf.yield %good : !ctjs.value\n"
                           "  } else {\n"
                           "    %bad = ctjs.constant #ctjs.string<\"01\">\n"
                           "    scf.yield %bad : !ctjs.value\n  }\n"));
    }
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
    reject("a repeated structured product cannot borrow an earlier iteration",
           replace(replace(savedProduct, "  %unit = ctjs.binary mul %one, %negative\n", ""),
                   "    %step =", "    %unit = ctjs.binary mul %one, %negative\n    %step ="));
    reject("structured products cannot borrow a coercible String",
           replace(carriedProduct, "unary neg %magnitude", "constant #ctjs.string<\"-2\">"));
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
        reject("repeated structured signed unary producers need independent invariance",
               replace(replace(savedSigned, makeSigned, ""),
                       "    %step =", "  " + makeSigned + "    %step ="));
        reject("signed structured snapshots cannot borrow an unknown input",
               replace(savedSigned, makeSigned, "  %unit = ctjs.unary " + unary + " %p\n"),
               ArrayContentsFailure::UnsupportedOperation);
        reject("signed structured strides still bound their final exact update",
               replace(replace(carriedSigned, "ctjs.binary add %one, %one",
                               "ctjs.constant #ctjs.number<4751297606873776128>"),
                       "%index = %zero", "%index = %one"));
    }
    reject("an opaque structured backedge cannot reuse a prior exact Number",
           replace(original, "scf.yield %base, %step, %read", "scf.yield %base, %p, %read"));
    reject("a structured array backedge must preserve its certified formal",
           replace(original, "scf.yield %base, %step, %read", "scf.yield %a, %step, %read"));
    reject("a reordered structured condition must still supply the induction formal",
           replace(original, "scf.condition(%continue) %index, %saved, %array",
                   "scf.condition(%continue) %saved, %index, %array"));
    reject("structured loop mutation refuses before replay",
           replace(original, "    %read =", "    ctjs.set_property %base[%i], %zero\n    %read ="));
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
    reject("direct array induction cannot hide a mutation",
           replace(direct, "    %read =", "    ctjs.set_property %a[%zero], %y\n    %read ="));
    reject("direct array length cannot certify another shorter array",
           replace(replace(direct, "  %finalIndex,", "  %b = ctjs.create_array []\n  %finalIndex,"),
                   "%read = ctjs.get_property %a", "%read = ctjs.get_property %b"),
           ArrayContentsFailure::MissingElement);
    reject("direct array offset reads retain own-bound checks",
           replace(direct, "    %read = ctjs.get_property %a[%i]",
                   "    %offset = ctjs.binary_static add %i, %one\n"
                   "    %read = ctjs.get_property %a[%offset]"),
           ArrayContentsFailure::MissingElement);
    std::size_t budgets = 0;
    for (const auto & expected : rows) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.body + "}\n", &context);
        if (!module) {
            fail(row{.what = expected.what, .body = expected.body, .expected = ""},
                 "the structured contents fixture did not parse");
            continue;
        }
        checkArrayContents(*module, expected);
        budgets += computeArrayContents(*module->getOps<ctjs::FuncOp>().begin()).work;
    }
    std::printf("structured contents: %zu rows, %zu contents budget cutoffs\n", rows.size(),
                budgets);
}

} // namespace ctcompile::test::escape::arrays
