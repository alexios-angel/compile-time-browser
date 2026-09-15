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
    reject("structured induction still refuses a computed nonzero start",
           replace(computedStart, "unary plus %zero", "unary plus %one"));
    reject("structured induction cannot recover zero by converting a String start",
           replace(computedStart, "ctjs.unary plus %zero", "ctjs.constant #ctjs.string<\"0\">"));
    reject("a structured initializer cannot borrow zero from another exact path",
           replace(computedStart, "  %start = ctjs.unary plus %zero\n",
                   "  %start = scf.if %flag -> (!ctjs.value) {\n"
                   "    %zeroResult = ctjs.unary plus %zero\n"
                   "    scf.yield %zeroResult : !ctjs.value\n"
                   "  } else {\n    scf.yield %one : !ctjs.value\n  }\n"));
    reject("a structured saved nonzero length stays nonzero after its array is emptied",
           replace(replace(savedLength, "%seed = ctjs.create_array []",
                           "%seed = ctjs.create_array [%one]"),
                   "ctjs.append %one to %seed", "ctjs.set_property %seed[%name], %zero"));
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
    reject("a structured guard must read the current array length",
           replace(original, "compare lt %index, %length", "compare lt %index, %one"));
    reject("a structured loop cannot start with an unknown Number",
           replace(original, "%index = %zero", "%index = %p"));
    reject("a structured zero step does not prove termination",
           replace(original, "binary_static add %i, %one", "binary_static add %i, %zero"));
    reject("a structured dynamic step does not borrow static Number induction",
           replace(original, "binary_static add %i, %one", "binary add %i, %one"));
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
    reject("direct arrays still require zero initial induction",
           replace(direct, "%index = %zero", "%index = %one"));
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
