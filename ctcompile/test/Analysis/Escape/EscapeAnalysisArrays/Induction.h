#pragma once

#include "Harness.h"

#include <utility>

namespace ctcompile::test::escape::arrays {

inline void checkArrayInduction(mlir::MLIRContext & context) {
    const std::string prefix =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %one = ctjs.constant #ctjs.number<4607182418800017408> {storage_test_id = \"one\"}\n"
        "  %two = ctjs.constant #ctjs.number<4611686018427387904> {storage_test_id = \"two\"}\n"
        "  %three = ctjs.constant #ctjs.number<4613937818241073152> {storage_test_id = \"three\"}\n"
        "  %a = ctjs.create_array [%one, %two, %three] {storage_test_id = \"a\"}\n";
    const std::string loop =
        "  cf.br ^header(%a, %zero, %zero : !ctjs.value, !ctjs.value, !ctjs.value)\n"
        "^header(%array: !ctjs.value, %index: !ctjs.value, %sum: !ctjs.value):\n"
        "  %key = ctjs.constant #ctjs.string<\"length\">\n"
        "  %length = ctjs.get_property %array[%key]\n"
        "  %less = ctjs.compare lt %index, %length\n"
        "  %flag = ctjs.truthy %less\n"
        "  cf.cond_br %flag, ^body(%array, %index, %sum : !ctjs.value, !ctjs.value, !ctjs.value), "
        "^exit(%sum : !ctjs.value)\n"
        "^body(%base: !ctjs.value, %i: !ctjs.value, %s: !ctjs.value):\n"
        "  %read = ctjs.get_property %base[%i]\n"
        "  %added = ctjs.binary add %s, %read {storage_test_id = \"added\"}\n"
        "  %step = ctjs.binary_static add %i, %one\n"
        "  cf.br ^header(%base, %step, %added : !ctjs.value, !ctjs.value, !ctjs.value)\n"
        "^exit(%result: !ctjs.value):\n"
        "  ctjs.return %result\n";
    const std::string original = prefix + loop;
    const auto replace = [](std::string source, const std::string & before,
                            const std::string & after) {
        const std::size_t position = source.find(before);
        assert(position != std::string::npos);
        source.replace(position, before.size(), after);
        return source;
    };
    unsigned rows = 0;
    std::size_t budgets = 0;
    const auto run = [&](const contents_row & expected, const char * discharged = "") {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.body + "}\n", &context);
        if (!module) {
            fail(row{.what = expected.what, .body = expected.body, .expected = ""},
                 "the induction fixture did not parse");
            return;
        }
        checkArrayContents(*module, expected);
        budgets += checkArrayRetention(
            *module, {.what = expected.what,
                      .body = expected.body,
                      .discharged = discharged,
                      .complete = expected.failure == ArrayContentsFailure::None});
        ++rows;
    };
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
    const std::string savedChild =
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
    const std::string reversed =
        replace(savedChild, "compare lt %index, %length", "compare gt %length, %index");
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
    const std::string negated =
        replace(replace(savedChild, "compare lt %index, %length", "compare ge %index, %length"),
                "  %flag = ctjs.truthy %less",
                "  %negated = ctjs.unary not %less\n  %flag = ctjs.truthy %negated");
    const std::string negatedReversed =
        replace(negated, "compare ge %index, %length", "compare le %length, %index");
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
    const std::string computed =
        prefix + makeStart + replace(loop, "^header(%a, %zero, %zero", "^header(%a, %start, %zero");
    run({.what = "an exact computed zero initializes the existing unit-step induction",
         .body = computed,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    const std::string computedChild = replace(savedChild, "  cf.br ^header(%a, %zero, %zero",
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
    const std::string alternateStart =
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
    const std::string nonzeroChild =
        replace(computedChild, "binary sub %one, %one", "binary sub %two, %one");
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
    const std::string makeUnit =
        "  %unit = ctjs.binary sub %two, %one {storage_test_id = \"unit\"}\n";
    const std::string unitLoop = replace(loop, "add %i, %one", "add %i, %unit");
    const std::string computedUnit = prefix + makeUnit + unitLoop;
    run({.what = "an independently computed Number one supplies an invariant unit step",
         .body = computedUnit,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    const std::string computedUnitChild =
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
    const std::string savedUnit =
        prefix +
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
    std::string carriedUnit = replace(
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
    const std::string alternateUnit = prefix + makeUnit +
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
    const std::string stride = replace(original, "add %i, %one", "add %i, %two");
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
    const std::string carriedStride =
        replace(carriedUnit, "binary sub %two, %one", "binary sub %three, %one");
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
    const std::string computedStrideChild =
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
    const std::string maxStride = "  %max = ctjs.constant #ctjs.number<4751297606873776128>\n" +
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
    const std::string nonzeroStride = replace(nonzeroStart, "add %i, %one", "add %i, %three");
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

    const auto reject = [&](const char * what, std::string body,
                            ArrayContentsFailure failure =
                                ArrayContentsFailure::UnsupportedControlFlow) {
        run({.what = what, .body = std::move(body), .failure = failure});
    };
    reject("a negative computed start has no bounded Number certificate",
           replace(computed, "binary sub %one, %one", "binary sub %zero, %one"));
    reject("even an untaken predecessor must independently supply a bounded Number",
           replace(alternateStart, "binary sub %one, %one", "binary sub %zero, %one"));
    for (const std::string constant :
         {"#ctjs.string<\"0\">", "#ctjs.bigint<\"0\">", "#ctjs.number<4602678819172646912>"}) {
        reject("coercible and fractional starts do not become bounded integral Numbers",
               replace(computed, "ctjs.binary sub %one, %one", "ctjs.constant " + constant));
    }
    reject("a computed start leaves every indexed read subject to the original own bound",
           replace(computed, "%base[%i]", "%base[%three]"), ArrayContentsFailure::MissingElement);
    reject("a computed zero stride cannot borrow a previous positive Number fact",
           replace(computedUnit, "binary sub %two, %one", "binary sub %two, %two"));
    for (const std::string constant :
         {"#ctjs.string<\"1\">", "#ctjs.bigint<\"1\">", "#ctjs.number<4602678819172646912>"}) {
        reject("held coercible or fractional values cannot supply a Number-one step",
               replace(computedUnit, "ctjs.binary sub %two, %one", "ctjs.constant " + constant));
    }
    reject("an opaque held unit step cannot borrow any source Number fact",
           replace(computedUnit, "add %i, %unit", "add %i, %p"));
    reject("a changing carried step cannot reuse its initial Number-one fact",
           replace(carriedUnit, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %zero"));
    reject("a swapping carried step cannot borrow another register's initial Number one",
           replace(carriedUnit, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %d, %added"));
    reject("a body-computed unit step needs its own invariant proof",
           replace(replace(computedUnit, makeUnit, ""), "  %step =", makeUnit + "  %step ="));
    reject("a header-computed unit step needs the same invariant proof",
           replace(replace(computedUnit, makeUnit, ""), "  %key =", makeUnit + "  %key ="));
    reject("a held unit step still cannot hide loop mutation",
           replace(computedUnit, "  %read =", "  ctjs.set_property %base[%i], %zero\n  %read ="));
    reject("an untaken predecessor must independently supply a Number-one step",
           replace(alternateUnit, "^entry(%one :", "^entry(%zero :"));
    reject("a positive carried stride cannot change even to another positive Number",
           replace(carriedStride, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    reject("positive strides do not certify their final overshoot as an own element",
           replace(replace(stride, "^exit(%sum :", "^exit(%index :"), "  ctjs.return %result",
                   "  %after = ctjs.get_property %a[%result]\n  ctjs.return %after"),
           ArrayContentsFailure::MissingElement);
    reject("nonunit induction still checks every visited offset against the own array",
           replace(stride, "  %read = ctjs.get_property %base[%i]",
                   "  %offset = ctjs.binary_static add %i, %one\n"
                   "  %read = ctjs.get_property %base[%offset]"),
           ArrayContentsFailure::MissingElement);
    for (const std::string constant :
         {"#ctjs.number<4751297606875873280>", "#ctjs.number<4845873199050653696>",
          "#ctjs.number<9218868437227405312>", "#ctjs.number<9221120237041090560>"}) {
        reject("out-of-range and nonfinite strides cannot supply bounded Number induction",
               replace(maxStride, "#ctjs.number<4751297606873776128>", constant));
        reject("out-of-range and nonfinite starts cannot borrow a bounded Number",
               replace(computed, "ctjs.binary sub %one, %one", "ctjs.constant " + constant));
    }
    reject("a nonzero start cannot overflow on its final stride update",
           replace(maxStride, "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    reject("nonzero starts retain own bounds on every visited offset",
           replace(nonzeroStride, "%base[%i]", "%base[%three]"),
           ArrayContentsFailure::MissingElement);
    reject("a stride computation that exceeds the bounded range supplies no wrapped fact",
           replace(replace(maxStride, "  cf.br ^header",
                           "  %overflow = ctjs.binary_static add %max, %one\n  cf.br ^header"),
                   "add %i, %max", "add %i, %overflow"));
    reject("inclusive guards do not prove an own index",
           replace(original, "compare lt", "compare le"));
    reject("reversed inclusive guards do not prove an own index",
           replace(reversed, "compare gt", "compare ge"));
    reject("negated greater-than still includes the inherited length index",
           replace(negated, "compare ge", "compare gt"));
    reject("negated reversed less-than still includes the inherited length index",
           replace(negatedReversed, "compare le", "compare lt"));
    reject("negated guards cannot use a different bound",
           replace(negated, "compare ge %index, %length", "compare ge %index, %two"));
    reject("negated guards still reject a dynamic bound",
           replace(negated, "compare ge %index, %length", "compare ge %index, %p"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("negated guards cannot conceal an array mutation",
           replace(negated, "  %read =", "  ctjs.set_property %base[%key], %zero\n  %read ="));
    reject("negated guards check every own index before releasing children",
           replace(negated, "%base[%i]", "%base[%two]"), ArrayContentsFailure::MissingElement);
    reject("logical negation cannot borrow a NaN initialization",
           replace(negated, "#ctjs.number<0>", "#ctjs.number<9221120237041090560>"));
    reject("logical negation cannot borrow a String initialization",
           replace(negated, "#ctjs.number<0>", "#ctjs.string<\"0\">"));
    reject("typeof does not complement the comparison",
           replace(negated, "unary not %less", "unary typeof %less"));
    reject("a second logical negation does not certify the strict guard",
           replace(negated, "  %flag = ctjs.truthy %negated",
                   "  %twice = ctjs.unary not %negated\n  %flag = ctjs.truthy %twice"));
    reject("greater-than guards still require length on the left",
           replace(reversed, "compare gt %length, %index", "compare gt %index, %length"));
    reject("reversed strict guards cannot use a different bound",
           replace(reversed, "compare gt %length, %index", "compare gt %two, %index"));
    reject("reversed strict guards cannot conceal a length mutation",
           replace(reversed, "  %read =", "  ctjs.set_property %base[%key], %zero\n  %read ="));
    reject("reversed strict guards still check every indexed read",
           replace(reversed, "%base[%i]", "%base[%two]"), ArrayContentsFailure::MissingElement);
    reject("inverted guards do not borrow strict induction",
           replace(original, "compare lt %index, %length", "compare lt %length, %index"));
    reject("an unknown entry index does not borrow literal zero",
           replace(original, "^header(%a, %zero, %zero", "^header(%a, %p, %zero"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("a zero step is not a finite induction",
           replace(original, "add %i, %one", "add %i, %zero"));
    reject("a fractional step is not integer induction",
           replace(replace(original, "  %step =",
                           "  %half = ctjs.constant "
                           "#ctjs.number<4602678819172646912>\n  %step ="),
                   "add %i, %one", "add %i, %half"));
    reject("a negative step is not increasing induction",
           replace(replace(original, "  %step =",
                           "  %minus = ctjs.constant "
                           "#ctjs.number<13830554455654793216>\n  %step ="),
                   "add %i, %one", "add %i, %minus"));
    reject("a mixed BigInt update does not establish Number induction",
           replace(replace(original,
                           "  %step =", "  %big = ctjs.constant #ctjs.bigint<\"1\">\n  %step ="),
                   "add %i, %one", "add %i, %big"));
    reject("an unknown update cannot reuse the previous iteration's exact index",
           replace(original, "^header(%base, %step, %added", "^header(%base, %p, %added"));
    for (const std::string & source : {savedChild, reversed, negated, negatedReversed}) {
        const auto dynamic = replace(source, "binary_static add %i, %one", "binary add %i, %one");
        run({.what = "dynamic Add retains a returned child under exact Number induction",
             .body = dynamic,
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "dynamic Add releases children only after complete bounded replay",
             .body = replace(dynamic, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    const auto dynamic = replace(original, "binary_static add %i, %one", "binary add %i, %one");
    for (const std::string constant : {"#ctjs.string<\"1\">", "#ctjs.bigint<\"1\">",
                                       "#ctjs.number<0>", "#ctjs.number<9221120237041090560>"}) {
        reject("dynamic Add requires a bounded positive Number stride",
               replace(dynamic, "#ctjs.number<4607182418800017408>", constant));
    }
    reject("dynamic Add cannot concatenate its String initialization",
           replace(dynamic, "#ctjs.number<0>", "#ctjs.string<\"0\">"));
    reject("a dynamic subtract latch cannot borrow the Add proof",
           replace(dynamic, "binary add %i, %one", "binary sub %i, %one"));
    for (const std::string literal : {"#ctjs.boolean<true>", "#ctjs.string<\"1\">"}) {
        for (const std::string unary : {"plus", "neg"}) {
            const auto source =
                replace(savedChild, "  %step = ctjs.binary_static add %i, %one",
                        "  %literal = ctjs.constant " + literal + "\n  %converted = ctjs.unary " +
                            unary + " %literal\n  %step = ctjs.binary " +
                            (unary == "neg" ? "sub" : "add") + " %i, %converted");
            run({.what = "original unary primitive latches preserve the returned child",
                 .body = source,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "original unary primitive latches release only unreturned children",
                 .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
            reject("a repeated unary parameter cannot borrow literal conversion",
                   replace(source, unary + " %literal", unary + " %p"));
            reject("unary primitive latches retain final own-index bounds",
                   replace(source, "%base[%i]", "%base[%two]"),
                   ArrayContentsFailure::MissingElement);
        }
    }
    const std::string makeBoolean =
        "  %unit = ctjs.constant #ctjs.boolean<true> {storage_test_id = \"unit\"}\n";
    for (const std::string opcode : {"binary_static", "binary"}) {
        for (const std::string operands : {"%i, %unit", "%unit, %i"}) {
            const auto boolean = makeBoolean + replace(savedChild, "binary_static add %i, %one",
                                                       opcode + " add " + operands);
            run({.what = "Boolean Add latches retain the original returned child in either order",
                 .body = boolean,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "Boolean Add latches release only unreturned children",
                 .body = replace(boolean, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
            reject("Boolean latch conversion leaves the original property key unchanged",
                   replace(boolean, "%base[%i]", "%base[%unit]"),
                   ArrayContentsFailure::UnknownIndex);
            for (const std::string constant :
                 {"#ctjs.boolean<false>", "#ctjs.null", "#ctjs.undefined", "#ctjs.string<\"1\">"}) {
                reject("zero, unknown and concatenating latches cannot borrow Boolean progress",
                       replace(boolean, "#ctjs.boolean<true>", constant));
            }
        }
    }
    const auto carriedBoolean = replace(replace(carriedUnit, makeUnit, makeBoolean),
                                        "binary_static add %i, %d", "binary add %d, %i");
    run({.what = "Boolean strides survive original CFG header and backedge transport",
         .body = carriedBoolean,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    reject("a Boolean stride cannot change even to the equivalent Number one",
           replace(carriedBoolean, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    const auto alternateBoolean =
        replace(replace(alternateUnit, makeUnit, makeBoolean), "binary_static add %i, %chosen",
                "binary add %i, %chosen");
    run({.what = "each CFG predecessor independently proves its Boolean or Number stride",
         .body = alternateBoolean,
         .arrays = "a:[one,two,three] | a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three; a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}; added -> {}"});
    reject("an unknown predecessor cannot borrow another predecessor's Boolean stride",
           replace(alternateBoolean, "^entry(%one :", "^entry(%p :"));
    const std::string savedBoolean =
        "  %truth = ctjs.constant #ctjs.boolean<true> {storage_test_id = \"truth\"}\n"
        "  %seed = ctjs.create_array [%truth] {storage_test_id = \"seed\"}\n"
        "  %unit = ctjs.get_property %seed[%zero]\n"
        "  ctjs.set_property %seed[%zero], %zero\n";
    run({.what = "a saved Boolean stride survives replacement in its source array",
         .body = replace(carriedBoolean, makeBoolean, savedBoolean),
         .arrays = "a:[one,two,three]; seed:[zero]",
         .reads = "seed[0]=truth; a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    reject("a repeated Boolean producer needs its own invariant proof",
           replace(replace(replace(computedUnit, makeUnit, savedBoolean),
                           "binary_static add %i, %unit", "binary add %i, %repeated"),
                   "  %step =", "  %repeated = ctjs.get_property %seed[%zero]\n  %step ="));
    for (const std::string opcode : {"binary_static", "binary"}) {
        const auto commuted =
            replace(savedChild, "binary_static add %i, %one", opcode + " add %one, %i");
        run({.what = "commuted Number Add retains the exact returned child",
             .body = commuted,
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "commuted Number Add discharges only unreturned children",
             .body = replace(commuted, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        run({.what = "commuted Number Add preserves a zero-trip saved child",
             .body = replace(commuted, "^header(%a, %zero, %zero", "^header(%a, %two, %x"),
             .arrays = "a:[one,x]",
             .exit = "x -> {x}"});
        const auto carried =
            replace(carriedStride, "binary_static add %i, %d", opcode + " add %d, %i");
        run({.what = "commuted held strides preserve exact backedge transport and overshoot",
             .body = carried,
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one; a[2]=three",
             .exit = "added -> {}"});
        reject("a commuted carried stride must remain unchanged",
               replace(carried, "^header(%base, %step, %added, %d",
                       "^header(%base, %step, %added, %one"));
        reject("two induction operands cannot supply an invariant positive stride",
               replace(commuted, "add %one, %i", "add %i, %i"));
        reject("commuted Add still requires the original induction operand",
               replace(commuted, "add %one, %i", "add %one, %s"));
        reject("commuted Add cannot borrow a String stride",
               replace(commuted, "#ctjs.number<4607182418800017408>", "#ctjs.string<\"1\">"));
        reject("commuted Add cannot borrow a BigInt stride",
               replace(commuted, "#ctjs.number<4607182418800017408>", "#ctjs.bigint<\"1\">"));
        reject("commuted Add still bounds its final update",
               replace(replace(maxStride, "binary_static add %i, %max", opcode + " add %max, %i"),
                       "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    }
    const std::string negativeLiteral = "#ctjs.number<13830554455654793216>";
    const std::string subtract =
        "  %minus = ctjs.constant " + negativeLiteral + "\n" +
        replace(savedChild, "binary_static add %i, %one", "binary sub %i, %minus");
    const std::string subtractNegated =
        replace(replace(subtract, "  %minus = ctjs.constant " + negativeLiteral + "\n", ""),
                "  %step =", "  %minus = ctjs.unary neg %one\n  %step =");
    for (const std::string & source : {subtract, subtractNegated}) {
        run({.what = "subtracting an original negative Number retains the exact returned child",
             .body = source,
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "negative Number subtraction discharges only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        run({.what = "negative subtraction preserves a zero-trip saved child",
             .body = replace(source, "^header(%a, %zero, %zero", "^header(%a, %two, %x"),
             .arrays = "a:[one,x]",
             .exit = "x -> {x}"});
    }
    const auto carriedSubtract =
        replace(replace(carriedUnit, makeUnit,
                        "  %unit = ctjs.constant #ctjs.number<13835058055282163712>\n"),
                "binary_static add %i, %d", "binary sub %i, %d");
    run({.what = "held negative strides preserve exact backedge transport and overshoot",
         .body = carriedSubtract,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    run({.what = "negative subtraction preserves its exact final index after array growth",
         .body = replace(replace(carriedSubtract, "^exit(%sum :", "^exit(%index :"),
                         "  ctjs.return %result",
                         "  ctjs.append %zero to %a\n  ctjs.append %zero to %a\n"
                         "  %after = ctjs.get_property %a[%result]\n  ctjs.return %after"),
         .arrays = "a:[one,two,three,zero,zero]",
         .reads = "a[0]=one; a[2]=three; a[4]=zero",
         .exit = "zero -> {}"});
    reject("a negative carried stride must remain unchanged",
           replace(carriedSubtract, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    reject("subtraction cannot commute its induction operand",
           replace(subtract, "sub %i, %minus", "sub %minus, %i"));
    reject("subtraction cannot borrow an opaque negative stride",
           replace(subtract, "sub %i, %minus", "sub %i, %p"));
    reject("a repeated computed negative stride needs an independent invariant proof",
           replace(subtractNegated, "unary neg %one", "binary sub %zero, %one"));
    run({.what = "repeated original String Neg supplies its invariant literal stride",
         .body =
             replace(subtractNegated, "#ctjs.number<4607182418800017408>", "#ctjs.string<\"1\">"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    for (const std::string constant :
         {"#ctjs.number<0>", "#ctjs.number<4607182418800017408>",
          "#ctjs.number<13826050856027422720>", "#ctjs.number<13974669643730649088>",
          "#ctjs.number<18442240474082181120>", "#ctjs.number<9221120237041090560>",
          "#ctjs.string<\"-1\">", "#ctjs.bigint<\"-1\">"}) {
        const auto body = replace(subtract, negativeLiteral, constant);
        if (constant == "#ctjs.string<\"-1\">") {
            run({.what = "original negative String Sub latches preserve returned children",
                 .body = body,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
        } else {
            reject("subtraction requires a bounded negative integral conversion", body);
        }
    }
    const auto stringSubtract = replace(subtract, negativeLiteral, "#ctjs.string<\"-1\">");
    run({.what = "String Sub latches discharge only unreturned children",
         .body = replace(stringSubtract, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    run({.what = "body-local String literals are invariant Sub latches",
         .body =
             replace(replace(stringSubtract, "  %minus = ctjs.constant #ctjs.string<\"-1\">\n", ""),
                     "  %step =", "  %minus = ctjs.constant #ctjs.string<\"-1\">\n  %step ="),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "held String Sub latches preserve exact backedge transport",
         .body =
             replace(carriedSubtract, "#ctjs.number<13835058055282163712>", "#ctjs.string<\"-2\">"),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    for (const std::string text : {"-0", "-01", "-1.0", " -1", "-1 ", "-4294967296"}) {
        reject("String Sub latches require bounded canonical decimal spelling",
               replace(stringSubtract, "#ctjs.string<\"-1\">", "#ctjs.string<\"" + text + "\">"));
    }
    reject("String Sub latches preserve original property keys",
           replace(stringSubtract, "%base[%i]", "%base[%minus]"),
           ArrayContentsFailure::UnknownIndex);
    reject("String latches cannot borrow Sub conversion for Add concatenation",
           replace(stringSubtract, "sub %i, %minus", "add %i, %minus"));
    reject("String Sub still bounds its exact final update",
           replace(replace(stringSubtract, "#ctjs.string<\"-1\">", "#ctjs.string<\"-4294967295\">"),
                   "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    reject("negative subtraction still bounds the final update before replay",
           replace(replace(subtract, negativeLiteral, "#ctjs.number<13974669643728551936>"),
                   "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    reject("a negative stride is not a nonnegative own array index",
           replace(subtract, "%base[%i]", "%base[%minus]"), ArrayContentsFailure::UnknownIndex);
    const std::string makeNegative = "  %magnitude = ctjs.binary add %one, %one\n"
                                     "  %unit = ctjs.unary neg %magnitude\n";
    const auto carriedNegative = replace(replace(carriedUnit, makeUnit, makeNegative),
                                         "binary_static add %i, %d", "binary sub %i, %d");
    run({.what = "Neg of a held bounded Number preserves its exact CFG stride",
         .body = carriedNegative,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    run({.what = "a held Neg stride preserves its final overshoot after array growth",
         .body = replace(replace(carriedNegative, "^exit(%sum :", "^exit(%index :"),
                         "  ctjs.return %result",
                         "  ctjs.append %zero to %a\n  ctjs.append %zero to %a\n"
                         "  %after = ctjs.get_property %a[%result]\n  ctjs.return %after"),
         .arrays = "a:[one,two,three,zero,zero]",
         .reads = "a[0]=one; a[2]=three; a[4]=zero",
         .exit = "zero -> {}"});
    run({.what = "a held Neg stride keeps its source length snapshot after shrink",
         .body = replace(carriedNegative, makeNegative,
                         "  %seed = ctjs.create_array [%one, %one] {storage_test_id = \"seed\"}\n"
                         "  %name = ctjs.constant #ctjs.string<\"length\">\n"
                         "  %magnitude = ctjs.get_property %seed[%name]\n"
                         "  %unit = ctjs.unary neg %magnitude\n"
                         "  ctjs.set_property %seed[%name], %zero\n"),
         .arrays = "a:[one,two,three]; seed:[]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    const std::string makeNegativeUnit = "  %magnitude = ctjs.unary plus %one\n"
                                         "  %minus = ctjs.unary neg %magnitude\n";
    const auto negativeChild =
        replace(replace(savedChild, "  cf.br ^header", makeNegativeUnit + "  cf.br ^header"),
                "binary_static add %i, %one", "binary sub %i, %minus");
    run({.what = "a computed Neg stride retains the original final child",
         .body = negativeChild,
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "a computed Neg stride discharges only the unreturned child",
         .body = replace(negativeChild, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    run({.what = "a zero-trip Neg stride preserves the original saved child",
         .body = replace(negativeChild, "^header(%a, %zero, %zero", "^header(%a, %two, %x"),
         .arrays = "a:[one,x]",
         .exit = "x -> {x}"});
    reject("a held Neg stride must remain identical across the CFG backedge",
           replace(carriedNegative, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    reject("a repeated Neg of a computed Number needs independent invariance",
           replace(replace(negativeChild, "  %minus = ctjs.unary neg %magnitude\n", ""),
                   "  %step =", "  %minus = ctjs.unary neg %magnitude\n  %step ="));
    reject("a negative snapshot is not an own array index",
           replace(negativeChild, "%base[%i]", "%base[%minus]"),
           ArrayContentsFailure::UnknownIndex);
    reject("Add cannot borrow a negative magnitude as a positive stride",
           replace(negativeChild, "binary sub %i, %minus", "binary add %i, %minus"));
    for (const std::string constant :
         {"#ctjs.number<0>", "#ctjs.number<13830554455654793216>",
          "#ctjs.number<4602678819172646912>", "#ctjs.number<4751297606875873280>",
          "#ctjs.number<9218868437227405312>", "#ctjs.number<9221120237041090560>",
          "#ctjs.string<\"1\">", "#ctjs.bigint<\"1\">"}) {
        const auto body = replace(negativeChild, "#ctjs.number<4607182418800017408>", constant);
        if (constant == "#ctjs.string<\"1\">") {
            run({.what = "canonical String Plus then Neg supplies an exact negative stride",
                 .body = body,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
        } else {
            reject("a computed Neg stride requires a bounded strictly positive Number input", body);
        }
    }
    reject("Neg of an unknown producer cannot supply a bounded stride",
           replace(negativeChild, "unary neg %magnitude", "unary neg %p"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("a held Neg stride cannot overflow on its final update",
           replace(replace(negativeChild, "  %magnitude = ctjs.unary plus %one",
                           "  %maximum = ctjs.constant #ctjs.number<4751297606873776128>\n"
                           "  %magnitude = ctjs.unary plus %maximum"),
                   "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    const std::string negativeText = "  %text = ctjs.constant #ctjs.string<\"-1\">\n";
    for (const auto & [operation, expected] :
         {std::pair{"unary plus %text", "13830554455654793216"},
          std::pair{"unary neg %text", "4607182418800017408"},
          std::pair{"unary bitnot %text", "0"},
          std::pair{"binary sub %text, %zero", "13830554455654793216"},
          std::pair{"binary sub %zero, %text", "4607182418800017408"},
          std::pair{"binary sub %text, %text", "0"},
          std::pair{"binary mul %text, %one", "13830554455654793216"},
          std::pair{"binary mul %one, %text", "13830554455654793216"},
          std::pair{"binary div %text, %one", "13830554455654793216"},
          std::pair{"binary div %one, %text", "13830554455654793216"},
          std::pair{"binary mod %text, %one", "0"},
          std::pair{"binary mod %zero, %text", "0"},
          std::pair{"binary pow %text, %three", "13830554455654793216"},
          std::pair{"binary pow %text, %two", "4607182418800017408"},
          std::pair{"binary pow %one, %text", "4607182418800017408"},
          std::pair{"binary_static bitand %text, %one", "4607182418800017408"},
          std::pair{"binary_static bitor %zero, %text", "13830554455654793216"},
          std::pair{"binary_static bitxor %text, %text", "0"},
          std::pair{"binary_static shl %text, %one", "13835058055282163712"},
          std::pair{"binary_static shr %text, %one", "13830554455654793216"},
          std::pair{"binary_static ushr %text, %text", "4607182418800017408"}}) {
        run({.what = "negative canonical String conversion preserves exact operator results",
             .body = prefix + negativeText + "  %actual = ctjs." + operation +
                     " {storage_test_id = \"actual\"}\n"
                     "  %expected = ctjs.constant #ctjs.number<" +
                     expected +
                     ">\n  %index = ctjs.binary sub %actual, %expected\n"
                     "  %read = ctjs.get_property %a[%index]\n  ctjs.return %actual\n",
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one",
             .exit = "actual -> {}"});
    }
    const auto savedNegativeText = replace(
        negativeChild, makeNegativeUnit,
        negativeText + "  %inputs = ctjs.create_array [%text] {storage_test_id = \"inputs\"}\n"
                       "  %saved = ctjs.get_property %inputs[%zero]\n"
                       "  ctjs.set_property %inputs[%zero], %x\n"
                       "  %minus = ctjs.unary plus %saved\n");
    run({.what = "negative String conversion uses the saved input before container mutation",
         .body = savedNegativeText,
         .arrays = "a:[one,x]; inputs:[x]",
         .reads = "inputs[0]=ctjs.constant; a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "negative String conversion discharges only unreturned saved children",
         .body = replace(savedNegativeText, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]; inputs:[x]",
         .reads = "inputs[0]=ctjs.constant; a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    const auto carriedNegativeText =
        replace(carriedNegative, makeNegative, negativeText + "  %unit = ctjs.unary plus %text\n");
    run({.what = "converted negative Strings preserve simultaneous CFG transport",
         .body = carriedNegativeText,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    reject("a converted negative String cannot change on the CFG backedge",
           replace(carriedNegativeText, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    reject("a repeated negative String conversion needs independent invariance",
           replace(replace(savedNegativeText, "  %minus = ctjs.unary plus %saved\n", ""),
                   "  %step =", "  %minus = ctjs.unary plus %saved\n  %step ="));
    reject("the original negative String keeps its property key after conversion",
           replace(savedNegativeText, "%base[%i]", "%base[%saved]"),
           ArrayContentsFailure::UnknownIndex);
    reject("negative String Add retains concatenation rather than a Number magnitude",
           replace(savedNegativeText, "unary plus %saved", "binary add %saved, %zero"));
    reject("negative String conversion cannot borrow an unknown saved input",
           replace(savedNegativeText, "unary plus %saved", "unary plus %p"),
           ArrayContentsFailure::UnsupportedOperation);
    for (const std::string text :
         {"-0", "-01", "--1", "-+1", "-1.0", "-1e0", " -1", "-1 ", "-0x1", "-4294967296"}) {
        reject(
            "negative String conversion requires bounded canonical decimal digits",
            replace(savedNegativeText, "#ctjs.string<\"-1\">", "#ctjs.string<\"" + text + "\">"));
    }
    run({.what = "negative canonical String conversion includes the exact magnitude bound",
         .body = prefix + "  %text = ctjs.constant #ctjs.string<\"-4294967295\">\n"
                          "  %expected = ctjs.constant #ctjs.number<13974669643728551936>\n"
                          "  %actual = ctjs.unary plus %text {storage_test_id = \"actual\"}\n"
                          "  %index = ctjs.binary sub %actual, %expected\n"
                          "  %read = ctjs.get_property %a[%index]\n  ctjs.return %actual\n",
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one",
         .exit = "actual -> {}"});
    for (const std::string literal :
         {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
        for (const std::string kind : {"plus", "neg", "bitnot"}) {
            const bool truth = literal == "#ctjs.boolean<true>";
            const std::string expected =
                kind == "bitnot" ? (truth ? "13835058055282163712" : "13830554455654793216")
                : !truth         ? "0"
                : kind == "plus" ? "4607182418800017408"
                                 : "13830554455654793216";
            run({.what = "original Boolean/null unary results keep their exact Number magnitude",
                 .body = prefix + "  %input = ctjs.constant " + literal +
                         "\n  %snapshot = ctjs.unary " + kind +
                         " %input\n  %expected = ctjs.constant #ctjs.number<" + expected +
                         ">\n  %index = ctjs.binary sub %snapshot, %expected\n"
                         "  %read = ctjs.get_property %a[%index]\n  ctjs.return %read\n",
                 .arrays = "a:[one,two,three]",
                 .reads = "a[0]=one",
                 .exit = "one -> {}"});
        }
    }
    for (const auto & [literal, kind] :
         {std::pair{"#ctjs.boolean<true>", "plus"}, std::pair{"#ctjs.boolean<true>", "neg"},
          std::pair{"#ctjs.boolean<false>", "bitnot"}, std::pair{"#ctjs.null", "bitnot"}}) {
        const std::string operation = "  %minus = ctjs.unary " + std::string{kind} + " %input\n";
        const auto source =
            replace(negativeChild, makeNegativeUnit,
                    "  %input = ctjs.constant " + std::string{literal} + "\n" + operation);
        const auto body = std::string{kind} == "plus"
                              ? replace(source, "binary sub %i, %minus", "binary add %i, %minus")
                              : source;
        run({.what = "original Boolean/null unary strides retain the final CFG child",
             .body = body,
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "Boolean/null unary strides discharge only unreturned children",
             .body = replace(body, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        const auto repeated =
            replace(replace(body, operation, ""), "  %step =", operation + "  %step =");
        if (std::string{kind} == "bitnot") {
            reject("a repeated primitive BitNot still needs independent invariance", repeated);
        } else {
            run({.what = "repeated primitive Plus/Neg uses its original literal stride",
                 .body = repeated,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
        }
        reject("a converted primitive does not turn its original Boolean/null into an index",
               replace(body, "%base[%i]", "%base[%input]"), ArrayContentsFailure::UnknownIndex);
        reject("Undefined unary conversion supplies no bounded Number fact",
               replace(body, literal, "#ctjs.undefined"));
    }
    const std::string makeComplement = "  %operand = ctjs.unary plus %zero\n"
                                       "  %minus = ctjs.unary bitnot %operand\n";
    const auto complementChild = replace(negativeChild, makeNegativeUnit, makeComplement);
    const auto savedComplement =
        replace(complementChild, makeComplement,
                "  %seed = ctjs.create_array [] {storage_test_id = \"seed\"}\n"
                "  %name = ctjs.constant #ctjs.string<\"length\">\n"
                "  %operand = ctjs.get_property %seed[%name]\n"
                "  %minus = ctjs.unary bitnot %operand\n"
                "  ctjs.append %one to %seed\n");
    for (const auto & source : {complementChild, savedComplement}) {
        const char * arrays = source == complementChild ? "a:[one,x]" : "a:[one,x]; seed:[one]";
        run({.what = "BitNot keeps its exact signed Number snapshot after source growth",
             .body = source,
             .arrays = arrays,
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "BitNot snapshots discharge only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = arrays,
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    const auto carriedComplement = replace(carriedNegative, makeNegative,
                                           "  %magnitude = ctjs.unary plus %one\n"
                                           "  %unit = ctjs.unary bitnot %magnitude\n");
    run({.what = "BitNot preserves its exact negative magnitude through CFG transport",
         .body = carriedComplement,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    reject("a BitNot snapshot cannot change across the backedge",
           replace(carriedComplement, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    reject("a repeated BitNot still needs independent invariance",
           replace(replace(complementChild, "  %minus = ctjs.unary bitnot %operand\n", ""),
                   "  %step =", "  %minus = ctjs.unary bitnot %operand\n  %step ="));
    reject("negative BitNot facts are never own indices",
           replace(complementChild, "%base[%i]", "%base[%minus]"),
           ArrayContentsFailure::UnknownIndex);
    reject("BitNot cannot supply a bounded Number from an unknown input",
           replace(complementChild, "bitnot %operand", "bitnot %p"),
           ArrayContentsFailure::UnsupportedOperation);
    for (const std::string constant :
         {"#ctjs.string<\"0\">", "#ctjs.string<\"00\">", "#ctjs.string<\"-1\">",
          "#ctjs.string<\"1.0\">", "#ctjs.string<\"4294967295\">", "#ctjs.bigint<\"0\">",
          "#ctjs.number<4602678819172646912>", "#ctjs.number<4751297606875873280>",
          "#ctjs.number<13974669643730649088>", "#ctjs.number<9218868437227405312>",
          "#ctjs.number<9221120237041090560>"}) {
        const auto body =
            replace(complementChild, "ctjs.unary plus %zero", "ctjs.constant " + constant);
        if (constant == "#ctjs.string<\"0\">") {
            run({.what = "the original canonical String BitNot keeps its negative CFG stride",
                 .body = body,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "canonical String BitNot discharges only unreturned CFG children",
                 .body = replace(body, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
            reject("a repeated String BitNot still needs independent invariance",
                   replace(replace(body, "  %minus = ctjs.unary bitnot %operand\n", ""),
                           "  %step =", "  %minus = ctjs.unary bitnot %operand\n  %step ="));
        } else {
            reject("BitNot requires bounded Numbers or canonical original Strings", body);
        }
    }
    const auto carriedStringComplement =
        replace(carriedComplement, "ctjs.unary plus %one", "ctjs.constant #ctjs.string<\"1\">");
    run({.what = "canonical String BitNot survives reordered CFG transport",
         .body = carriedStringComplement,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    reject("a String BitNot snapshot cannot change across the CFG backedge",
           replace(carriedStringComplement, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    // Subtracting the expected signed result must produce index zero. This
    // checks the full magnitude at both ToInt32 boundaries, not just its sign.
    for (const auto & [input, output] :
         {std::pair{"0", "13830554455654793216"},
          std::pair{"9223372036854775808", "13830554455654793216"},
          std::pair{"13830554455654793216", "0"},
          std::pair{"13835058055282163712", "4607182418800017408"},
          std::pair{"4746794007244308480", "13970166044103278592"},
          std::pair{"4746794007248502784", "4746794007244308480"},
          std::pair{"4751297606873776128", "0"},
          std::pair{"13970166044103278592", "4746794007244308480"},
          std::pair{"13974669643728551936", "13835058055282163712"}}) {
        run({.what = "BitNot preserves exact ToInt32 boundary values and its result identity",
             .body = prefix + "  %operand = ctjs.constant #ctjs.number<" + input +
                     ">\n"
                     "  %expected = ctjs.constant #ctjs.number<" +
                     output +
                     ">\n"
                     "  %actual = ctjs.unary bitnot %operand {storage_test_id = \"actual\"}\n"
                     "  %index = ctjs.binary sub %actual, %expected\n"
                     "  %read = ctjs.get_property %a[%index]\n  ctjs.return %actual\n",
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one",
             .exit = "actual -> {}"});
    }
    for (const std::string kind : {"bitand", "bitor", "bitxor", "shl", "shr"}) {
        const std::string right = kind == "bitand" ? "%operand" : "%zero";
        const std::string operation =
            "  %minus = ctjs.binary_static " + kind + " %operand, " + right + "\n";
        const auto snapshot =
            replace(negativeChild, makeNegativeUnit,
                    "  %seed = ctjs.create_array [%one] {storage_test_id = \"seed\"}\n"
                    "  %name = ctjs.constant #ctjs.string<\"length\">\n"
                    "  %magnitude = ctjs.get_property %seed[%name]\n"
                    "  %operand = ctjs.unary neg %magnitude\n" +
                        operation + "  ctjs.set_property %seed[%name], %zero\n");
        run({.what = "signed bitwise snapshots retain their child after source length shrink",
             .body = snapshot,
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "signed bitwise snapshots discharge only unreturned children",
             .body = replace(snapshot, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        const auto carried = replace(carriedNegative, makeNegative,
                                     "  %operand = ctjs.unary neg %two\n" +
                                         replace(operation, "%minus =", "%unit ="));
        run({.what = "signed bitwise magnitudes survive exact CFG transport",
             .body = carried,
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one; a[2]=three",
             .exit = "added -> {}"});
        reject("signed bitwise snapshots must remain identical on the backedge",
               replace(carried, "^header(%base, %step, %added, %d",
                       "^header(%base, %step, %added, %one"));
        reject("repeated bitwise producers need independent invariance",
               replace(replace(snapshot, operation, ""), "  %step =", operation + "  %step ="));
        reject("negative bitwise facts never become own indices",
               replace(snapshot, "%base[%i]", "%base[%minus]"), ArrayContentsFailure::UnknownIndex);
        reject("signed bitwise snapshots cannot borrow an unknown operand",
               replace(snapshot, kind + " %operand", kind + " %p"),
               ArrayContentsFailure::UnknownValue);
        for (const std::string input :
             {"#ctjs.string<\"-1\">", "#ctjs.bigint<\"-1\">", "#ctjs.number<4602678819172646912>",
              "#ctjs.number<4751297606875873280>", "#ctjs.number<13974669643730649088>",
              "#ctjs.number<9218868437227405312>", "#ctjs.number<9221120237041090560>"}) {
            const auto body =
                replace(snapshot, "ctjs.unary neg %magnitude", "ctjs.constant " + input);
            if (input == "#ctjs.string<\"-1\">") {
                run({.what = "negative canonical Strings share exact signed bitwise conversion",
                     .body = body,
                     .arrays = "a:[one,x]; seed:[]",
                     .reads = "a[0]=one; a[1]=x",
                     .exit = "x -> {x}"});
            } else {
                reject("signed bitwise inputs require exact bounded primitive conversion", body);
            }
        }
    }
    struct bitwise_case {
        const char * kind;
        const char * left;
        const char * right;
        const char * expected;
    };
    for (const auto & [kind, left, right, expected] :
         {bitwise_case{"bitand", "13830554455654793216", "13835058055282163712",
                       "13835058055282163712"}, // -1 & -2 = -2
          bitwise_case{"bitand", "13974669643728551936", "13830554455654793216",
                       "4607182418800017408"}, // -UINT32_MAX & -1 = 1
          bitwise_case{"bitand", "4746794007248502784", "4751297606873776128",
                       "13970166044103278592"}, // 2^31 & UINT32_MAX = -2^31
          bitwise_case{"bitor", "13970166044103278592", "4746794007244308480",
                       "13830554455654793216"}, // -2^31 | INT32_MAX = -1
          bitwise_case{"bitor", "13974669643728551936", "0", "4607182418800017408"},
          bitwise_case{"bitxor", "13830554455654793216", "13835058055282163712",
                       "4607182418800017408"}, // -1 ^ -2 = 1
          bitwise_case{"bitxor", "13970166044103278592", "4751297606873776128",
                       "4746794007244308480"},
          bitwise_case{"bitxor", "9223372036854775808", "0", "0"},
          bitwise_case{"shl", "13830554455654793216", "13853072453791645696",
                       "13830554455654793216"}, // -1 << -32 = -1
          bitwise_case{"shl", "13830554455654793216", "13830554455654793216",
                       "13970166044103278592"}, // -1 << -1 = -2^31
          bitwise_case{"shl", "13970166044103278592", "4607182418800017408", "0"},
          bitwise_case{"shl", "13974669643728551936", "13852790978814935040",
                       "4611686018427387904"}, // -UINT32_MAX << -31 = 2
          bitwise_case{"shl", "4751297606873776128", "4607182418800017408", "13835058055282163712"},
          bitwise_case{"shr", "13830554455654793216", "13830554455654793216",
                       "13830554455654793216"}, // -1 >> -1 = -1
          bitwise_case{"shr", "13837309855095848960", "4607182418800017408",
                       "13835058055282163712"}, // -3 >> 1 = -2
          bitwise_case{"shr", "4746794007248502784", "4629700416936869888",
                       "13970166044103278592"}, // 2^31 >> 32 = -2^31
          bitwise_case{"shr", "4751297606873776128", "0", "13830554455654793216"},
          bitwise_case{"shr", "13974669643728551936", "13853072453791645696",
                       "4607182418800017408"}, // -UINT32_MAX >> -32 = 1
          bitwise_case{"shr", "9223372036854775808", "13830554455654793216", "0"},
          bitwise_case{"ushr", "13830554455654793216", "0", "4751297606873776128"},
          bitwise_case{"ushr", "13830554455654793216", "13830554455654793216",
                       "4607182418800017408"}, // -1 >>> -1 = 1
          bitwise_case{"ushr", "13970166044103278592", "13853072453791645696",
                       "4746794007248502784"}, // -2^31 >>> -32 = 2^31
          bitwise_case{"ushr", "13974669643728551936", "13852790978814935040", "0"},
          bitwise_case{"ushr", "9223372036854775808", "13830554455654793216", "0"}}) {
        run({.what = "signed bitwise snapshots retain exact ToInt32 bits and result identity",
             .body = prefix + "  %left = ctjs.constant #ctjs.number<" + left +
                     ">\n  %right = ctjs.constant #ctjs.number<" + right +
                     ">\n  %expected = ctjs.constant #ctjs.number<" + expected +
                     ">\n  %actual = ctjs.binary_static " + kind +
                     " %left, %right {storage_test_id = \"actual\"}\n"
                     "  %index = ctjs.binary sub %actual, %expected\n"
                     "  %read = ctjs.get_property %a[%index]\n  ctjs.return %actual\n",
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one",
             .exit = "actual -> {}"});
    }
    struct primitive_bitwise_case {
        const char * kind;
        const char * trueLeft;
        const char * zeroLeft;
        const char * trueRight;
        const char * zeroRight;
    };
    for (const auto & [kind, trueLeft, zeroLeft, trueRight, zeroRight] :
         {primitive_bitwise_case{"bitand", "4607182418800017408", "0", "4607182418800017408", "0"},
          primitive_bitwise_case{"bitor", "13830554455654793216", "13830554455654793216",
                                 "13830554455654793216", "13830554455654793216"},
          primitive_bitwise_case{"bitxor", "13835058055282163712", "13830554455654793216",
                                 "13835058055282163712", "13830554455654793216"},
          primitive_bitwise_case{"shl", "13970166044103278592", "0", "13835058055282163712",
                                 "13830554455654793216"},
          primitive_bitwise_case{"shr", "0", "0", "13830554455654793216", "13830554455654793216"},
          primitive_bitwise_case{"ushr", "0", "0", "4746794007244308480", "4751297606873776128"}}) {
        for (const std::string literal :
             {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
            for (const bool commuted : {false, true}) {
                const std::string expected = literal == "#ctjs.boolean<true>"
                                                 ? (commuted ? trueRight : trueLeft)
                                                 : (commuted ? zeroRight : zeroLeft);
                run({.what =
                         "Boolean/null bitwise operands retain exact signed and unsigned results",
                     .body = prefix + "  %input = ctjs.constant " + literal +
                             "\n  %negative = ctjs.unary neg %one\n"
                             "  %actual = ctjs.binary_static " +
                             kind + " " + (commuted ? "%negative, %input" : "%input, %negative") +
                             " {storage_test_id = \"actual\"}\n"
                             "  %expected = ctjs.constant #ctjs.number<" +
                             expected +
                             ">\n  %index = ctjs.binary sub %actual, %expected\n"
                             "  %read = ctjs.get_property %a[%index]\n  ctjs.return %actual\n",
                     .arrays = "a:[one,two,three]",
                     .reads = "a[0]=one",
                     .exit = "actual -> {}"});
            }
        }
    }
    for (const auto & [literal, kind] :
         {std::pair{"#ctjs.boolean<true>", "bitand"}, std::pair{"#ctjs.boolean<false>", "bitor"},
          std::pair{"#ctjs.null", "bitxor"}}) {
        const std::string input = "  %input = ctjs.constant " + std::string(literal) + "\n";
        const std::string operation =
            "  %unit = ctjs.binary_static " + std::string(kind) + " %input, %one\n";
        const auto source = replace(computedUnitChild, makeUnit, input + operation);
        const auto saved =
            replace(source, operation,
                    "  %holder = ctjs.create_array [%input] {storage_test_id = \"holder\"}\n"
                    "  %savedOperand = ctjs.get_property %holder[%zero]\n"
                    "  ctjs.set_property %holder[%zero], %x\n" +
                        replace(operation, "%input,", "%savedOperand,"));
        run({.what = "saved primitive bitwise operands survive replacement and retain CFG children",
             .body = saved,
             .arrays = "a:[one,x]; holder:[x]",
             .reads = "holder[0]=ctjs.constant; a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "primitive bitwise snapshots discharge only unreturned CFG children",
             .body = replace(saved, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; holder:[x]",
             .reads = "holder[0]=ctjs.constant; a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        const auto carried = replace(carriedUnit, makeUnit, input + operation);
        run({.what = "primitive bitwise snapshots survive exact CFG backedge transport",
             .body = carried,
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one; a[1]=two; a[2]=three",
             .exit = "added -> {}"});
        reject("primitive bitwise snapshots cannot change across CFG backedges",
               replace(carried, "^header(%base, %step, %added, %d",
                       "^header(%base, %step, %added, %zero"));
        reject("primitive bitwise conversion cannot turn its original operand into an own key",
               replace(saved, "%base[%i]", "%base[%savedOperand]"),
               ArrayContentsFailure::UnknownIndex);
        reject("repeated primitive bitwise producers still need independent invariance",
               replace(replace(source, operation, ""), "  %step =", operation + "  %step ="));
        reject("primitive bitwise operands cannot borrow an unknown value",
               replace(source, operation, replace(operation, "%input,", "%p,")),
               ArrayContentsFailure::UnknownValue);
        reject("Undefined cannot borrow primitive bitwise Number evidence",
               replace(source, literal, "#ctjs.undefined"));
    }
    const std::string unsignedOperation = "  %count = ctjs.unary neg %one\n"
                                          "  %unit = ctjs.binary_static ushr %operand, %count\n";
    const auto unsignedCarried =
        replace(carriedUnit, makeUnit, "  %operand = ctjs.unary neg %one\n" + unsignedOperation);
    run({.what = "unsigned shifts preserve negative operands and counts through CFG transport",
         .body = unsignedCarried,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    const auto unsignedSaved =
        replace(replace(savedUnit, "  %unit = ctjs.get_property %seed[%name]\n",
                        "  %magnitude = ctjs.get_property %seed[%name]\n"
                        "  %operand = ctjs.unary neg %magnitude\n" +
                            unsignedOperation),
                "ctjs.append %one to %seed", "ctjs.set_property %seed[%name], %zero");
    run({.what = "unsigned shifts retain signed read-time Numbers after source shrink",
         .body = unsignedSaved,
         .arrays = "a:[one,two,three]; seed:[]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    reject("an unsigned shift snapshot cannot change on a CFG backedge",
           replace(unsignedCarried, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %two"));
    reject("an unsigned shift cannot borrow an unknown count",
           replace(unsignedCarried, "ushr %operand, %count", "ushr %operand, %p"),
           ArrayContentsFailure::UnknownValue);
    const auto stringShift =
        replace(unsignedCarried, "ctjs.unary neg %one",
                "ctjs.constant #ctjs.string<\"4294967294\"> {storage_test_id = \"text\"}");
    run({.what = "unsigned conversion preserves the original String through CFG transport",
         .body = replace(stringShift, "ctjs.return %result", "ctjs.return %operand"),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "text -> {}"});
    const auto stringCount =
        replace(replace(unsignedCarried, "  %count = ctjs.unary neg %one",
                        "  %count = ctjs.constant #ctjs.string<\"31\">"),
                "  %operand = ctjs.unary neg %one",
                "  %operand = ctjs.unary neg %one {storage_test_id = \"negative\"}");
    run({.what = "a String shift count preserves the original signed Number identity",
         .body = replace(stringCount, "ctjs.return %result", "ctjs.return %operand"),
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "negative -> {}"});
    reject("String shift counts require canonical decimal evidence",
           replace(stringCount, "#ctjs.string<\"31\">", "#ctjs.string<\"031\">"));
    const std::string makeSubUnit = "  %left = ctjs.unary plus %one\n"
                                    "  %magnitude = ctjs.unary plus %two\n"
                                    "  %minus = ctjs.binary sub %left, %magnitude\n";
    const auto subChild = replace(negativeChild, makeNegativeUnit, makeSubUnit);
    const auto savedSub =
        replace(subChild, makeSubUnit,
                "  %seed = ctjs.create_array [%one, %one] {storage_test_id = \"seed\"}\n"
                "  %name = ctjs.constant #ctjs.string<\"length\">\n"
                "  %left = ctjs.unary plus %one\n"
                "  %magnitude = ctjs.get_property %seed[%name]\n"
                "  %minus = ctjs.binary sub %left, %magnitude\n"
                "  ctjs.set_property %seed[%name], %zero\n");
    for (const auto & source : {subChild, savedSub}) {
        const char * arrays = source == subChild ? "a:[one,x]" : "a:[one,x]; seed:[]";
        run({.what = "bounded Number subtraction retains its exact negative snapshot and child",
             .body = source,
             .arrays = arrays,
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "negative Sub snapshots discharge only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = arrays,
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    run({.what = "a negative Sub snapshot preserves a zero-trip saved child",
         .body = replace(subChild, "^header(%a, %zero, %zero", "^header(%a, %two, %x"),
         .arrays = "a:[one,x]",
         .exit = "x -> {x}"});
    const auto carriedSub =
        replace(carriedNegative, "unary neg %magnitude", "binary sub %zero, %magnitude");
    for (const std::string literal :
         {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
        for (const bool primitiveLeft : {false, true}) {
            const std::string sum =
                "  %minus = ctjs.binary add " +
                std::string(primitiveLeft ? "%saved, %negative" : "%negative, %saved") + "\n";
            const auto source = replace(
                negativeChild, makeNegativeUnit,
                "  %primitive = ctjs.constant " + literal +
                    " {storage_test_id = \"primitive\"}\n"
                    "  %inputs = ctjs.create_array [%primitive] {storage_test_id = \"inputs\"}\n"
                    "  %saved = ctjs.get_property %inputs[%zero]\n"
                    "  ctjs.set_property %inputs[%zero], %x\n"
                    "  %negative = ctjs.unary neg " +
                    (literal == "#ctjs.boolean<true>" ? "%two\n" : "%one\n") + sum);
            run({.what = "primitive addition retains CFG snapshots after operand replacement",
                 .body = source,
                 .arrays = "a:[one,x]; inputs:[x]",
                 .reads = "inputs[0]=primitive; a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "primitive addition releases only unreturned CFG children",
                 .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]; inputs:[x]",
                 .reads = "inputs[0]=primitive; a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
            reject("addition cannot turn its original Boolean/null into an own key",
                   replace(source, "%base[%i]", "%base[%primitive]"),
                   ArrayContentsFailure::UnknownIndex);
            reject("repeated primitive addition needs independent invariance",
                   replace(replace(source, sum, ""), "  %step =", sum + "  %step ="));
            reject("primitive addition cannot borrow an unknown operand",
                   replace(source, "ctjs.constant " + literal, "ctjs.unary plus %p"),
                   ArrayContentsFailure::UnsupportedOperation);
            reject("String concatenation cannot borrow primitive addition evidence",
                   replace(source, literal, "#ctjs.string<\"0\">"));
            reject("Undefined cannot borrow primitive addition evidence",
                   replace(source, literal, "#ctjs.undefined"));
        }
    }
    const auto primitiveCarriedAdd = replace(carriedNegative, makeNegative,
                                             "  %nil = ctjs.constant #ctjs.null\n"
                                             "  %negative = ctjs.unary neg %one\n"
                                             "  %unit = ctjs.binary_static add %nil, %negative\n");
    run({.what = "primitive addition snapshots survive exact CFG backedge transport",
         .body = primitiveCarriedAdd,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    reject("primitive addition snapshots cannot change across CFG backedges",
           replace(primitiveCarriedAdd, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    for (const std::string literal :
         {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
        for (const bool primitiveLeft : {false, true}) {
            const std::string number = literal == "#ctjs.boolean<true>" ? "%two" : "%one";
            const std::string difference =
                "  %minus = ctjs.binary sub " +
                (primitiveLeft ? "%saved, " + number : number + ", %saved") + "\n";
            auto source = replace(
                subChild, makeSubUnit,
                "  %primitive = ctjs.constant " + literal +
                    " {storage_test_id = \"primitive\"}\n"
                    "  %inputs = ctjs.create_array [%primitive] {storage_test_id = \"inputs\"}\n"
                    "  %saved = ctjs.get_property %inputs[%zero]\n"
                    "  ctjs.set_property %inputs[%zero], %x\n" +
                    difference);
            if (!primitiveLeft) {
                source = replace(source, "binary sub %i, %minus", "binary add %i, %minus");
            }
            run({.what = "primitive subtraction retains CFG snapshots after operand replacement",
                 .body = source,
                 .arrays = "a:[one,x]; inputs:[x]",
                 .reads = "inputs[0]=primitive; a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "primitive subtraction releases only unreturned CFG children",
                 .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]; inputs:[x]",
                 .reads = "inputs[0]=primitive; a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
            reject("subtraction cannot turn its original Boolean/null into an own key",
                   replace(source, "%base[%i]", "%base[%primitive]"),
                   ArrayContentsFailure::UnknownIndex);
            reject("repeated primitive subtraction needs independent invariance",
                   replace(replace(source, difference, ""), "  %step =", difference + "  %step ="));
            reject("primitive subtraction cannot borrow an unknown operand",
                   replace(source, "ctjs.constant " + literal, "ctjs.unary plus %p"),
                   ArrayContentsFailure::UnsupportedOperation);
            reject("Undefined cannot borrow primitive subtraction evidence",
                   replace(source, literal, "#ctjs.undefined"));
        }
    }
    const auto primitiveCarriedSub =
        replace(carriedSub, "  %unit = ctjs.binary sub %zero, %magnitude\n",
                "  %nil = ctjs.constant #ctjs.null\n"
                "  %unit = ctjs.binary sub %nil, %magnitude\n");
    run({.what = "primitive subtraction snapshots survive exact CFG backedge transport",
         .body = primitiveCarriedSub,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    reject("primitive subtraction snapshots cannot change across CFG backedges",
           replace(primitiveCarriedSub, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    run({.what = "negative Sub snapshots retain their exact final overshoot after array growth",
         .body =
             replace(replace(carriedSub, "^exit(%sum :", "^exit(%index :"), "  ctjs.return %result",
                     "  ctjs.append %zero to %a\n  ctjs.append %zero to %a\n"
                     "  %after = ctjs.get_property %a[%result]\n  ctjs.return %after"),
         .arrays = "a:[one,two,three,zero,zero]",
         .reads = "a[0]=one; a[2]=three; a[4]=zero",
         .exit = "zero -> {}"});
    for (const std::string operands : {"%left, %left", "%magnitude, %left"}) {
        reject("zero and positive Sub results cannot supply a negative stride",
               replace(subChild, "binary sub %left, %magnitude", "binary sub " + operands));
    }
    const auto leftStringSub = replace(subChild, "unary plus %one", "constant #ctjs.string<\"1\">");
    const auto bothStringSub =
        replace(leftStringSub, "unary plus %two", "constant #ctjs.string<\"2\">");
    const auto savedLeftStringSub =
        replace(savedSub, "unary plus %one", "constant #ctjs.string<\"1\">");
    const auto stringSub = replace(subChild, "unary plus %two", "constant #ctjs.string<\"2\">");
    const auto savedStringSub =
        replace(replace(savedSub, "unary plus %one", "get_property %seed[%name]"),
                "%magnitude = ctjs.get_property %seed[%name]",
                "%magnitude = ctjs.constant #ctjs.string<\"3\">");
    for (const auto & source :
         {stringSub, savedStringSub, leftStringSub, bothStringSub, savedLeftStringSub}) {
        const char * arrays = source == savedStringSub || source == savedLeftStringSub
                                  ? "a:[one,x]; seed:[]"
                                  : "a:[one,x]";
        run({.what = "canonical String operands retain negative snapshots after source shrink",
             .body = source,
             .arrays = arrays,
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "negative String subtraction snapshots discharge only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = arrays,
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    const auto carriedStringSub =
        replace(carriedSub, "ctjs.binary add %one, %one", "ctjs.constant #ctjs.string<\"2\">");
    run({.what = "negative String-offset snapshots preserve simultaneous CFG transport",
         .body = carriedStringSub,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    reject("a negative String-offset snapshot cannot change on the CFG backedge",
           replace(carriedStringSub, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    reject("repeated String-offset producers still need independent invariance",
           replace(replace(stringSub, "  %minus = ctjs.binary sub %left, %magnitude\n", ""),
                   "  %step =", "  %minus = ctjs.binary sub %left, %magnitude\n  %step ="));
    reject("a negative String-offset snapshot cannot become an own array index",
           replace(stringSub, "%base[%i]", "%base[%minus]"), ArrayContentsFailure::UnknownIndex);
    for (const std::string text :
         {"02", "+2", "-2", "2.0", "2e0", " 2", "0x2", "4294967295", "NaN"}) {
        reject("negative String offsets require the existing bounded canonical decimal proof",
               replace(stringSub, "#ctjs.string<\"2\">", "#ctjs.string<\"" + text + "\">"));
        const auto left =
            replace(leftStringSub, "#ctjs.string<\"1\">", "#ctjs.string<\"" + text + "\">");
        if (text == "-2") {
            run({.what = "a canonical negative left String preserves its exact stride",
                 .body = left,
                 .arrays = "a:[one,x]",
                 .reads = "a[0]=one",
                 .exit = "one -> {}"},
                "x");
        } else {
            reject("left String subtraction requires bounded canonical decimal conversion", left);
        }
    }
    reject("repeated left String producers still need independent invariance",
           replace(replace(leftStringSub, "  %minus = ctjs.binary sub %left, %magnitude\n", ""),
                   "  %step =", "  %minus = ctjs.binary sub %left, %magnitude\n  %step ="));
    reject("a negative left String result cannot become an own array index",
           replace(leftStringSub, "%base[%i]", "%base[%minus]"),
           ArrayContentsFailure::UnknownIndex);
    for (const auto & [left, right] :
         {std::pair{"0", "9223372036854775808"}, std::pair{"1", "4607182418800017408"},
          std::pair{"4294967294", "4751297606871678976"}}) {
        run({.what = "canonical left String cancellation keeps the original exact zero result",
             .body = prefix + "  %left = ctjs.constant #ctjs.string<\"" + left +
                     "\">\n  %right = ctjs.constant #ctjs.number<" + right +
                     ">\n  %actual = ctjs.binary sub %left, %right "
                     "{storage_test_id = \"actual\"}\n"
                     "  %read = ctjs.get_property %a[%actual]\n  ctjs.return %actual\n",
             .arrays = "a:[one,two,three]",
             .reads = "a[0]=one",
             .exit = "actual -> {}"});
    }
    reject("negative Sub snapshots cannot become own array indices",
           replace(subChild, "%base[%i]", "%base[%minus]"), ArrayContentsFailure::UnknownIndex);
    reject("Add cannot use a negative Sub snapshot as its positive stride",
           replace(subChild, "binary sub %i, %minus", "binary add %i, %minus"));
    reject("repeated Sub producers need an independent invariant proof",
           replace(replace(subChild, "  %minus = ctjs.binary sub %left, %magnitude\n", ""),
                   "  %step =", "  %minus = ctjs.binary sub %left, %magnitude\n  %step ="));
    reject("a carried Sub snapshot cannot change on the backedge",
           replace(carriedSub, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    reject("a negative Sub snapshot still bounds its final update",
           replace(replace(carriedSub, "ctjs.binary add %one, %one",
                           "ctjs.constant #ctjs.number<4751297606873776128>"),
                   "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    const std::string difference =
        "  %other = ctjs.unary neg %two\n"
        "  %difference = ctjs.binary sub %minus, %one {storage_test_id = \"difference\"}\n";
    const auto negativeLeft = replace(replace(replace(savedSub, "[%one, %x]", "[%one, %one, %x]"),
                                              "  cf.br ^header", difference + "  cf.br ^header"),
                                      "binary sub %i, %minus", "binary sub %i, %difference");
    for (const std::string operands : {"%minus, %one", "%minus, %other", "%other, %minus"}) {
        const auto source = replace(replace(negativeLeft, "sub %minus, %one", "sub " + operands),
                                    "binary sub %i, %difference",
                                    operands == "%minus, %other" ? "binary add %i, %difference"
                                                                 : "binary sub %i, %difference");
        const char * reads =
            operands == "%minus, %one" ? "a[0]=one; a[2]=x" : "a[0]=one; a[1]=one; a[2]=x";
        run({.what = "negative-left Sub retains signed snapshots after source shrink",
             .body = source,
             .arrays = "a:[one,one,x]; seed:[]",
             .reads = reads,
             .exit = "x -> {x}"});
        run({.what = "negative-left Sub releases only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,one,x]; seed:[]",
             .reads = reads,
             .exit = "zero -> {}"},
            "x");
    }
    const auto cancelledSub = replace(negativeLeft, "sub %minus, %one", "sub %minus, %minus");
    run({.what = "negative Sub cancellation supplies zero without losing result identity",
         .body = replace(replace(replace(cancelledSub, "^header(%a, %zero, %zero",
                                         "^header(%a, %difference, %zero"),
                                 "binary sub %i, %difference", "binary_static add %i, %one"),
                         "ctjs.return %result", "ctjs.return %difference"),
         .arrays = "a:[one,one,x]; seed:[]",
         .reads = "a[0]=one; a[1]=one; a[2]=x",
         .exit = "difference -> {}"},
        "x");
    reject("negative Sub cancellation cannot certify progress", cancelledSub);
    reject("negative-left Sub cannot supply an own index",
           replace(negativeLeft, "%base[%i]", "%base[%difference]"),
           ArrayContentsFailure::UnknownIndex);
    reject("repeated negative-left Sub still needs independent invariance",
           replace(replace(negativeLeft, difference, ""), "  %step =", difference + "  %step ="));
    for (const std::string constant :
         {"#ctjs.string<\"01\">", "#ctjs.bigint<\"1\">", "#ctjs.number<4602678819172646912>",
          "#ctjs.number<4751297606875873280>"}) {
        reject("negative-left Sub needs bounded exact Numbers or canonical String offsets",
               replace(negativeLeft, difference,
                       "  %operand = ctjs.constant " + constant + "\n" +
                           replace(difference, "sub %minus, %one", "sub %minus, %operand")));
    }
    const auto negativeStringLeft =
        replace(negativeLeft, difference,
                "  %operand = ctjs.constant #ctjs.string<\"1\">\n" +
                    replace(difference, "sub %minus, %one", "sub %minus, %operand"));
    run({.what = "a canonical String offset extends a held negative Number after source shrink",
         .body = negativeStringLeft,
         .arrays = "a:[one,one,x]; seed:[]",
         .reads = "a[0]=one; a[2]=x",
         .exit = "x -> {x}"});
    run({.what = "negative-left String offsets release only unreturned children",
         .body = replace(negativeStringLeft, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,one,x]; seed:[]",
         .reads = "a[0]=one; a[2]=x",
         .exit = "zero -> {}"},
        "x");
    reject("negative-left Sub cannot borrow an unknown operand",
           replace(negativeLeft, "sub %minus, %one", "sub %minus, %p"),
           ArrayContentsFailure::UnsupportedOperation);
    const auto maximumSub = replace(replace(negativeLeft, "binary sub %left, %magnitude",
                                            "constant #ctjs.number<13974669643728551936>"),
                                    "sub %minus, %one", "sub %minus, %zero");
    run({.what = "negative-left Sub preserves an exact boundary magnitude and origin",
         .body = replace(maximumSub, "ctjs.return %result", "ctjs.return %difference"),
         .arrays = "a:[one,one,x]; seed:[]",
         .reads = "a[0]=one",
         .exit = "difference -> {}"},
        "x");
    reject("negative-left Sub must bound its final induction update",
           replace(maximumSub, "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    reject("negative-left Sub cannot certify an overflowing magnitude",
           replace(maximumSub, "sub %minus, %zero", "sub %minus, %one"));
    reject("a canonical String offset cannot overflow a held negative magnitude",
           replace(replace(maximumSub, "  %other =",
                           "  %offset = ctjs.constant "
                           "#ctjs.string<\"1\">\n  %other ="),
                   "sub %minus, %zero", "sub %minus, %offset"));
    run({.what = "the largest canonical String offset can reach the exact signed Number boundary",
         .body = prefix + "  %negative = ctjs.constant #ctjs.number<13830554455654793216>\n"
                          "  %offset = ctjs.constant #ctjs.string<\"4294967294\">\n"
                          "  %expected = ctjs.constant #ctjs.number<13974669643728551936>\n"
                          "  %actual = ctjs.binary sub %negative, %offset "
                          "{storage_test_id = \"actual\"}\n"
                          "  %index = ctjs.binary sub %actual, %expected\n"
                          "  %read = ctjs.get_property %a[%index]\n  ctjs.return %actual\n",
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one",
         .exit = "actual -> {}"});
    run({.what = "String zero preserves negative-zero result identity and exact index zero",
         .body = prefix + "  %negativeZero = ctjs.constant #ctjs.number<9223372036854775808>\n"
                          "  %offset = ctjs.constant #ctjs.string<\"0\">\n"
                          "  %actual = ctjs.binary sub %negativeZero, %offset "
                          "{storage_test_id = \"actual\"}\n"
                          "  %read = ctjs.get_property %a[%actual]\n  ctjs.return %actual\n",
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one",
         .exit = "actual -> {}"});
    for (const std::string opcode : {"binary", "binary_static"}) {
        for (const std::string operands : {"%minus, %two", "%two, %minus"}) {
            const std::string cancel = "  %cancelled = ctjs." + opcode + " add " + operands +
                                       " {storage_test_id = \"cancelled\"}\n";
            const auto cancelled =
                replace(replace(savedSub, "  cf.br ^header", cancel + "  cf.br ^header"),
                        "binary sub %i, %minus", "binary_static add %i, %cancelled");
            run({.what = "Add cancellation preserves a held negative snapshot after source shrink",
                 .body = cancelled,
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "Add cancellation discharges only unreturned children",
                 .body = replace(cancelled, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
            run({.what = "a cancelled Number snapshot supplies an exact own index",
                 .body = replace(cancelled, "%base[%i]", "%base[%cancelled]"),
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[1]=x; a[1]=x",
                 .exit = "x -> {x}"});
            reject("repeated Add cancellation needs independent invariance",
                   replace(replace(cancelled, cancel, ""), "  %step =", cancel + "  %step ="));
        }
    }
    const std::string cancel = "  %cancelled = ctjs.binary add %minus, %two "
                               "{storage_test_id = \"cancelled\"}\n";
    const auto cancelled = replace(replace(savedSub, "  cf.br ^header", cancel + "  cf.br ^header"),
                                   "binary sub %i, %minus", "binary_static add %i, %cancelled");
    const auto cancelledZero =
        replace(replace(replace(cancelled, "binary add %minus, %two", "binary add %minus, %one"),
                        "add %i, %cancelled", "add %i, %one"),
                "^header(%a, %zero, %zero", "^header(%a, %cancelled, %zero");
    run({.what = "exact cancellation to zero initializes induction",
         .body = cancelledZero,
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "a cancelled zero retains its result origin",
         .body = replace(cancelledZero, "ctjs.return %result", "ctjs.return %cancelled"),
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "cancelled -> {}"},
        "x");
    run({.what = "a cancelled stride preserves the original zero-trip result",
         .body = replace(cancelled, "^header(%a, %zero, %zero", "^header(%a, %magnitude, %x"),
         .arrays = "a:[one,x]; seed:[]",
         .exit = "x -> {x}"});
    for (const std::string value : {"%one", "%zero", "%minus"}) {
        reject("a zero or negative cancelled result cannot supply a positive stride",
               replace(cancelled, "binary add %minus, %two", "binary add %minus, " + value));
    }
    for (const std::string constant :
         {"#ctjs.string<\"2\">", "#ctjs.bigint<\"2\">", "#ctjs.number<4751297606875873280>"}) {
        reject("cancellation requires both original operands inside the bounded Number domain",
               replace(cancelled, "#ctjs.number<4611686018427387904>", constant));
    }
    reject("cancellation cannot borrow an unknown operand",
           replace(cancelled, "binary add %minus, %two", "binary add %minus, %p"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("a cancelled stride still bounds its final update",
           replace(replace(replace(cancelled, "#ctjs.number<4611686018427387904>",
                                   "#ctjs.number<4751297606873776128>"),
                           "[%one, %x]", "[%one, %x, %one]"),
                   "^header(%a, %zero, %zero", "^header(%a, %magnitude, %zero"));
    const auto savedNegativeAdd =
        replace(savedSub, "binary sub %left, %magnitude", "binary sub %zero, %magnitude");
    for (const std::string opcode : {"binary", "binary_static"}) {
        for (const std::string operands : {"%minus, %one", "%one, %minus", "%minus, %minus"}) {
            const std::string sum = "  %negativeSum = ctjs." + opcode + " add " + operands +
                                    " {storage_test_id = \"negativeSum\"}\n";
            const bool bothNegative = operands == "%minus, %minus";
            auto source = bothNegative ? replace(savedSub, "[%one, %x]", "[%one, %one, %x]")
                                       : savedNegativeAdd;
            source = replace(replace(source, "  cf.br ^header", sum + "  cf.br ^header"),
                             "sub %i, %minus", "sub %i, %negativeSum");
            const char * arrays = bothNegative ? "a:[one,one,x]; seed:[]" : "a:[one,x]; seed:[]";
            const char * reads = bothNegative ? "a[0]=one; a[2]=x" : "a[0]=one; a[1]=x";
            run({.what = "negative Add snapshots retain exact children after source shrink",
                 .body = source,
                 .arrays = arrays,
                 .reads = reads,
                 .exit = "x -> {x}"});
            run({.what = "negative Add snapshots release only unreturned children",
                 .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = arrays,
                 .reads = reads,
                 .exit = "zero -> {}"},
                "x");
        }
    }
    const std::string negativeSum =
        "  %negativeSum = ctjs.binary add %minus, %one {storage_test_id = \"negativeSum\"}\n";
    const auto negativeAdd =
        replace(replace(savedNegativeAdd, "  cf.br ^header", negativeSum + "  cf.br ^header"),
                "sub %i, %minus", "sub %i, %negativeSum");
    run({.what = "a negative Add snapshot preserves a zero-trip saved child",
         .body = replace(negativeAdd, "^header(%a, %zero, %zero", "^header(%a, %two, %x"),
         .arrays = "a:[one,x]; seed:[]",
         .exit = "x -> {x}"});
    reject("a negative Add snapshot cannot become an own index",
           replace(negativeAdd, "%base[%i]", "%base[%negativeSum]"),
           ArrayContentsFailure::UnknownIndex);
    reject("a negative Add snapshot cannot initialize an increasing loop",
           replace(negativeAdd, "^header(%a, %zero, %zero", "^header(%a, %negativeSum, %zero"));
    reject("a negative Add snapshot cannot supply a positive Add stride",
           replace(negativeAdd, "binary sub %i, %negativeSum", "binary add %i, %negativeSum"));
    reject("a repeated negative Add producer needs independent invariance",
           replace(replace(negativeAdd, negativeSum, ""), "  %step =", negativeSum + "  %step ="));
    for (const std::string constant :
         {"#ctjs.string<\"1\">", "#ctjs.bigint<\"1\">", "#ctjs.number<4602678819172646912>",
          "#ctjs.number<4751297606875873280>"}) {
        reject("negative Add requires bounded exact Number operands",
               replace(negativeAdd, negativeSum,
                       "  %operand = ctjs.constant " + constant + "\n" +
                           replace(negativeSum, ", %one", ", %operand")));
    }
    reject("negative Add cannot borrow an unknown operand",
           replace(negativeAdd, "add %minus, %one", "add %minus, %p"),
           ArrayContentsFailure::UnsupportedOperation);
    const auto maximumNegativeAdd = replace(replace(negativeAdd, "binary sub %zero, %magnitude",
                                                    "constant #ctjs.number<13974669643728551936>"),
                                            "add %minus, %one", "add %minus, %zero");
    run({.what = "a negative Add at the bound retains its original result identity",
         .body = replace(maximumNegativeAdd, "ctjs.return %result", "ctjs.return %negativeSum"),
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[0]=one",
         .exit = "negativeSum -> {}"},
        "x");
    reject("negative Add still bounds the final exact induction update",
           replace(maximumNegativeAdd, "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    reject("bounded negative Add operands cannot certify an out-of-domain sum",
           replace(maximumNegativeAdd, "add %minus, %zero", "add %minus, %minus"));
    for (const std::string operation : {"div", "mod"}) {
        for (const std::string literal :
             {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
            for (const bool negative : {false, true}) {
                for (const bool commuted : {false, true}) {
                    const std::string expected =
                        literal != "#ctjs.boolean<true>" || operation == "mod" ? "0"
                        : negative ? "13830554455654793216"
                                   : "4607182418800017408";
                    const auto body = prefix + "  %input = ctjs.constant " + literal +
                                      "\n  %factor = ctjs.unary " + (negative ? "neg" : "plus") +
                                      " %one\n  %snapshot = ctjs.binary " + operation + " " +
                                      (commuted ? "%factor, %input" : "%input, %factor") +
                                      " {storage_test_id = \"snapshot\"}\n"
                                      "  %expected = ctjs.constant #ctjs.number<" +
                                      expected +
                                      ">\n  %index = ctjs.binary sub %snapshot, %expected\n"
                                      "  %read = ctjs.get_property %a[%index]\n"
                                      "  ctjs.return %snapshot\n";
                    if (commuted && literal != "#ctjs.boolean<true>") {
                        reject("Boolean/null zero divisors cannot supply finite Number snapshots",
                               body, ArrayContentsFailure::UnknownIndex);
                    } else {
                        run({.what =
                                 "primitive division and remainder retain signed result origins",
                             .body = body,
                             .arrays = "a:[one,two,three]",
                             .reads = "a[0]=one",
                             .exit = "snapshot -> {}"});
                    }
                }
            }
        }
        const auto divisor = operation == "div" ? "%one" : "%two";
        const std::string makeResult = "  %signedResult = ctjs.binary " + operation + " %minus, " +
                                       divisor + " {storage_test_id = \"signedResult\"}\n";
        const auto source =
            replace(replace(savedSub, "  cf.br ^header", makeResult + "  cf.br ^header"),
                    "sub %i, %minus", "sub %i, %signedResult");
        for (const auto & body :
             {source,
              replace(source, makeResult,
                      "  %divisor = ctjs.unary neg " + std::string(divisor) + "\n" +
                          replace(makeResult, ", " + std::string(divisor), ", %divisor"))}) {
            const auto signedBody = operation == "div" && body != source
                                        ? replace(body, "binary sub %i, %signedResult",
                                                  "binary_static add %i, %signedResult")
                                        : body;
            run({.what = "signed division and remainder keep the original snapshot and child",
                 .body = signedBody,
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "signed division and remainder release only unreturned children",
                 .body = replace(signedBody, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
        }
        reject("negative quotients and remainders cannot supply own indices",
               replace(source, "%base[%i]", "%base[%signedResult]"),
               ArrayContentsFailure::UnknownIndex);
        reject("repeated division and remainder need independent invariance",
               replace(replace(source, makeResult, ""), "  %step =", makeResult + "  %step ="));
        reject("signed division and remainder require a nonzero divisor",
               replace(source, ", " + std::string(divisor) + " {storage_test_id",
                       ", %zero {storage_test_id"));
        const auto originalString =
            replace(source, makeResult,
                    "  %divisor = ctjs.constant #ctjs.string<\"1\">\n" +
                        replace(makeResult, ", " + std::string(divisor), ", %divisor"));
        if (operation == "div") {
            run({.what = "the original canonical String divisor retains its returned child",
                 .body = originalString,
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
        } else {
            reject("a canonical String divisor cannot turn a zero remainder into progress",
                   originalString);
        }
        const std::string text = operation == "div" ? "1" : "2";
        const auto stringRight =
            replace(originalString, "#ctjs.string<\"1\">", "#ctjs.string<\"" + text + "\">");
        const auto stringLeft =
            replace(replace(source, makeResult,
                            "  %text = ctjs.constant #ctjs.string<\"1\">\n" +
                                replace(makeResult, "%minus,", "%text,")),
                    "binary sub %i, %signedResult", "binary add %i, %signedResult");
        for (const auto & body : {stringRight, stringLeft}) {
            run({.what = "canonical String division and remainder retain the exact CFG child",
                 .body = body,
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "canonical String division and remainder release unreturned children",
                 .body = replace(body, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
        }
        const auto primitive =
            operation == "div"
                ? replace(originalString, "#ctjs.string<\"1\">", "#ctjs.boolean<true>")
                : replace(stringLeft, "#ctjs.string<\"1\">", "#ctjs.boolean<true>");
        run({.what = "Boolean division and remainder keep CFG snapshots and returned children",
             .body = primitive,
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "Boolean division and remainder release only unreturned CFG children",
             .body = replace(primitive, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        const std::string operand = operation == "div" ? "%divisor" : "%text";
        const auto savedPrimitive =
            replace(primitive, "  %signedResult =",
                    "  %holder = ctjs.create_array [" + operand +
                        "] {storage_test_id = \"holder\"}\n"
                        "  %savedOperand = ctjs.get_property %holder[%zero]\n"
                        "  ctjs.set_property %holder[%zero], %x\n  %signedResult =");
        run({.what = "a saved Boolean dividend or divisor survives source replacement",
             .body = replace(savedPrimitive, operation == "div" ? ", %divisor {" : "%text,",
                             operation == "div" ? ", %savedOperand {" : "%savedOperand,"),
             .arrays = "a:[one,x]; seed:[]; holder:[x]",
             .reads = "holder[0]=ctjs.constant; a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        reject("division cannot turn an original primitive into an own array key",
               replace(primitive, "%base[%i]", "%base[" + operand + "]"),
               ArrayContentsFailure::UnknownIndex);
        const auto repeated = replace(makeResult, operation == "div" ? ", %one" : "%minus,",
                                      operation == "div" ? ", %divisor" : "%text,");
        reject("repeated primitive division needs independent invariance",
               replace(replace(primitive, repeated, ""), "  %step =", repeated + "  %step ="));
        for (const std::string invalid : {"#ctjs.undefined", "#ctjs.number<9218868437227405312>",
                                          "#ctjs.number<9221120237041090560>"}) {
            reject("nonfinite primitive division cannot borrow exact Boolean evidence",
                   replace(primitive, "#ctjs.boolean<true>", invalid));
        }
        for (const std::string invalid :
             {"0", "01", "+1", "-1", "1.0", "1e0", " 1", "0x1", "4294967295", "NaN"}) {
            reject("String divisors need canonical bounded nonzero decimal evidence",
                   replace(stringRight, "#ctjs.string<\"" + text + "\">",
                           "#ctjs.string<\"" + invalid + "\">"));
        }
        reject(
            "a repeated String division or remainder cannot borrow its earlier result",
            replace(replace(stringRight, "  %signedResult =", "  %unused ="), "  %step =",
                    replace(makeResult, ", " + std::string(divisor), ", %divisor") + "  %step ="));
        reject("signed division and remainder cannot borrow an unknown operand",
               replace(source, ", " + std::string(divisor) + " {storage_test_id",
                       ", %p {storage_test_id"),
               ArrayContentsFailure::UnsupportedOperation);
        if (operation == "div") {
            run({.what = "a positive Number divided by a negative snapshot keeps a negative stride",
                 .body = replace(source, "div %minus, %one", "div %one, %minus"),
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            reject("signed division needs an integral quotient",
                   replace(source, "div %minus, %one", "div %minus, %two"));
            reject("canonical String division still needs an integral quotient",
                   replace(stringRight, "#ctjs.string<\"1\">", "#ctjs.string<\"2\">"));
        }
    }
    const std::string power = "  %power = ctjs.binary pow %minus, %one\n";
    const auto powerChild = replace(replace(savedSub, "  cf.br ^header", power + "  cf.br ^header"),
                                    "sub %i, %minus", "sub %i, %power");
    run({.what = "power one keeps the original negative length snapshot through CFG transport",
         .body = powerChild,
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "power zero releases only unreturned children",
         .body = replace(replace(replace(powerChild, "pow %minus, %one", "pow %minus, %zero"),
                                 "binary sub %i, %power", "binary_static add %i, %power"),
                         "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    reject("an even negative-one power cannot supply a negative stride",
           replace(powerChild, "pow %minus, %one", "pow %minus, %two"));
    for (const std::string exponent : {"%minus", "%three", "%magnitude"}) {
        auto source = replace(powerChild, "pow %minus, %one", "pow %minus, " + exponent);
        if (exponent == "%magnitude") {
            source = replace(source, "binary sub %i, %power", "binary_static add %i, %power");
        }
        run({.what = "negative-one powers preserve exact parity and the saved length snapshot",
             .body = source,
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "negative-one powers release only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    const auto signedUnitPower = replace(powerChild, "pow %minus, %one", "pow %minus, %magnitude");
    for (const std::string literal :
         {"#ctjs.number<4751297606873776128>", "#ctjs.number<13974669643728551936>"}) {
        run({.what = "negative-one powers preserve parity at both signed domain endpoints",
             .body = replace(signedUnitPower, "  %power = ctjs.binary pow %minus, %magnitude",
                             "  %endpoint = ctjs.constant " + literal +
                                 "\n  %power = ctjs.binary pow %minus, %endpoint"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
    }
    for (const std::string literal :
         {"#ctjs.string<\"2\">", "#ctjs.bigint<\"2\">", "#ctjs.number<4602678819172646912>",
          "#ctjs.number<4751297606875873280>", "#ctjs.number<13974669643730649088>",
          "#ctjs.number<9218868437227405312>", "#ctjs.number<18442240474082181120>",
          "#ctjs.number<9221120237041090560>"}) {
        reject("negative-one powers must supply a bounded negative Number stride",
               replace(signedUnitPower, "  %power = ctjs.binary pow %minus, %magnitude",
                       "  %exponent = ctjs.constant " + literal +
                           "\n  %power = ctjs.binary pow %minus, %exponent"));
    }
    reject("general integer powers still need an independent exact Number proof",
           replace(replace(powerChild, "pow %minus, %one", "pow %minus, %three"),
                   "binary sub %left, %magnitude", "unary neg %magnitude"));
    reject("power snapshots cannot borrow an unknown exponent",
           replace(powerChild, "pow %minus, %one", "pow %minus, %p"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("negative powers cannot supply an own array index",
           replace(powerChild, "%base[%i]", "%base[%power]"), ArrayContentsFailure::UnknownIndex);
    reject("a power snapshot cannot change on the CFG backedge",
           replace(replace(powerChild, power, ""), "  %step =", power + "  %step ="));
    const auto unitPower = replace(replace(powerChild, "pow %minus, %one", "pow %one, %minus"),
                                   "binary sub %i, %power", "binary_static add %i, %power");
    for (const std::string literal :
         {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
        for (const bool primitiveBase : {false, true}) {
            const bool negative = !primitiveBase && literal == "#ctjs.boolean<true>";
            const std::string makePower =
                "  %power = ctjs.binary pow " +
                std::string(primitiveBase ? "%primitive, %minus" : "%minus, %primitive") + "\n";
            const auto source =
                replace(negative ? powerChild : unitPower,
                        negative ? power : "  %power = ctjs.binary pow %one, %minus\n",
                        "  %primitive = ctjs.constant " + literal + "\n" + makePower);
            if (primitiveBase && literal != "#ctjs.boolean<true>") {
                reject("false/null to a negative exponent cannot prove finite CFG progress",
                       source);
                continue;
            }
            run({.what = "primitive powers retain signed CFG snapshots after source shrink",
                 .body = source,
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            run({.what = "primitive powers release only unreturned CFG children",
                 .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "zero -> {}"},
                "x");
            reject("power conversion cannot turn its original primitive into an own key",
                   replace(source, "%base[%i]", "%base[%primitive]"),
                   ArrayContentsFailure::UnknownIndex);
            reject("repeated primitive powers need independent invariance",
                   replace(replace(source, makePower, ""), "  %step =", makePower + "  %step ="));
            reject("primitive power identities still require both original operands",
                   replace(source, "ctjs.constant " + literal, "ctjs.unary plus %p"),
                   ArrayContentsFailure::UnsupportedOperation);
            reject("Undefined cannot borrow a primitive power snapshot",
                   replace(source, literal, "#ctjs.undefined"));
        }
    }
    const auto primitiveCarriedPower =
        replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n",
                "  %primitive = ctjs.constant #ctjs.boolean<true>\n"
                "  %negative = ctjs.unary neg %magnitude\n"
                "  %unit = ctjs.binary pow %negative, %primitive\n");
    run({.what = "primitive power snapshots survive exact CFG backedge transport",
         .body = primitiveCarriedPower,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[2]=three",
         .exit = "added -> {}"});
    reject("primitive power snapshots cannot change across CFG backedges",
           replace(primitiveCarriedPower, "^header(%base, %step, %added, %d",
                   "^header(%base, %step, %added, %one"));
    for (const bool stringBase : {false, true}) {
        const std::string makePower = "  %power = ctjs.binary pow " +
                                      std::string(stringBase ? "%text, %minus" : "%minus, %text") +
                                      "\n";
        const auto source =
            replace(stringBase ? unitPower : powerChild,
                    stringBase ? "  %power = ctjs.binary pow %one, %minus\n" : power,
                    "  %text = ctjs.constant #ctjs.string<\"1\">\n" + makePower);
        run({.what = "canonical String powers retain original CFG children after source shrink",
             .body = source,
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "canonical String powers release only unreturned CFG children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        reject("a String power producer cannot borrow its previous iteration's result",
               replace(replace(source, makePower, ""), "  %step =", makePower + "  %step ="));
        for (const std::string invalid :
             {"01", "+1", "-1", "1.0", "1e0", " 1", "0x1", "4294967295", "NaN"}) {
            const auto body =
                replace(source, "#ctjs.string<\"1\">", "#ctjs.string<\"" + invalid + "\">");
            if (invalid == "-1" && !stringBase) {
                run({.what = "a negative String exponent preserves the exact negative unit stride",
                     .body = body,
                     .arrays = "a:[one,x]; seed:[]",
                     .reads = "a[0]=one; a[1]=x",
                     .exit = "x -> {x}"});
            } else {
                reject("String powers require bounded conversion and a progressing stride", body);
            }
        }
    }
    for (const std::string exponent : {"%minus", "%magnitude", "%three"}) {
        const auto source = replace(unitPower, "pow %one, %minus", "pow %one, " + exponent);
        run({.what = "positive-one powers retain bounded signed exponent snapshots after shrink",
             .body = source,
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "positive-one powers release only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    for (const std::string literal :
         {"#ctjs.number<4751297606873776128>", "#ctjs.number<13974669643728551936>"}) {
        run({.what = "positive-one powers accept both signed Number domain endpoints",
             .body = replace(unitPower, "binary sub %left, %magnitude", "constant " + literal),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
    }
    for (const std::string literal : {"#ctjs.number<0>", "#ctjs.number<9223372036854775808>"}) {
        for (const std::string exponent : {"%magnitude", "%three"}) {
            const auto source = replace(
                replace(replace(powerChild, "binary sub %left, %magnitude", "constant " + literal),
                        "pow %minus, %one", "pow %minus, " + exponent),
                "binary sub %i, %power", "binary_static add %i, %one");
            run({.what = "even and odd zero powers initialize finite loops without losing children",
                 .body = replace(source, "^header(%a, %zero, %zero", "^header(%a, %power, %zero"),
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
        }
    }
    for (const std::string literal :
         {"#ctjs.string<\"2\">", "#ctjs.bigint<\"2\">", "#ctjs.number<4602678819172646912>",
          "#ctjs.number<4751297606875873280>", "#ctjs.number<13974669643730649088>",
          "#ctjs.number<9218868437227405312>", "#ctjs.number<18442240474082181120>",
          "#ctjs.number<9221120237041090560>"}) {
        const auto source =
            replace(unitPower, "binary sub %left, %magnitude", "constant " + literal);
        if (literal == "#ctjs.string<\"2\">") {
            run({.what = "the original canonical String exponent retains its returned child",
                 .body = source,
                 .arrays = "a:[one,x]; seed:[]",
                 .reads = "a[0]=one; a[1]=x",
                 .exit = "x -> {x}"});
            continue;
        }
        reject("positive-one powers require exact bounded finite exponent evidence", source);
    }
    reject("a positive-one base cannot bypass an unknown exponent",
           replace(unitPower, "pow %one, %minus", "pow %one, %p"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("zero to a negative exponent cannot certify a bounded start",
           replace(replace(unitPower, "pow %one, %minus", "pow %zero, %minus"),
                   "^header(%a, %zero, %zero", "^header(%a, %power, %zero"));
    reject("a newly admitted power cannot be recomputed on the CFG backedge",
           replace(replace(unitPower, "  %power = ctjs.binary pow %one, %minus\n", ""),
                   "  %step =", "  %power = ctjs.binary pow %one, %minus\n  %step ="));
    const std::string factor = "  %factor = ctjs.unary plus %one\n";
    const std::string product = "  %product = ctjs.binary mul %minus, %factor "
                                "{storage_test_id = \"product\"}\n";
    const auto productChild =
        replace(replace(savedSub, "  cf.br ^header", factor + product + "  cf.br ^header"),
                "sub %i, %minus", "sub %i, %product");
    const auto stringProduct =
        replace(productChild, factor, "  %factor = ctjs.constant #ctjs.string<\"1\">\n");
    for (const std::string literal :
         {"#ctjs.boolean<true>", "#ctjs.boolean<false>", "#ctjs.null"}) {
        for (const bool negative : {false, true}) {
            for (const bool commuted : {false, true}) {
                const std::string expected = literal != "#ctjs.boolean<true>" ? "0"
                                             : negative ? "13830554455654793216"
                                                        : "4607182418800017408";
                run({.what = "Boolean/null products keep exact signed Number magnitudes",
                     .body = prefix + "  %input = ctjs.constant " + literal +
                             "\n  %factor = ctjs.unary " + (negative ? "neg" : "plus") +
                             " %one\n  %snapshot = ctjs.binary mul " +
                             (commuted ? "%factor, %input" : "%input, %factor") +
                             "\n  %expected = ctjs.constant #ctjs.number<" + expected +
                             ">\n  %index = ctjs.binary sub %snapshot, %expected\n"
                             "  %read = ctjs.get_property %a[%index]\n  ctjs.return %read\n",
                     .arrays = "a:[one,two,three]",
                     .reads = "a[0]=one",
                     .exit = "one -> {}"});
            }
        }
    }
    const auto booleanProduct =
        replace(productChild, factor, "  %factor = ctjs.constant #ctjs.boolean<true>\n");
    for (const auto & source :
         {booleanProduct, replace(booleanProduct, "mul %minus, %factor", "mul %factor, %minus")}) {
        run({.what = "Boolean products preserve signed snapshots through CFG transport",
             .body = source,
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "Boolean products discharge only unreturned CFG children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    reject("a product cannot turn its original Boolean factor into an own index",
           replace(booleanProduct, "%base[%i]", "%base[%factor]"),
           ArrayContentsFailure::UnknownIndex);
    reject("a repeated Boolean product still needs independent invariance",
           replace(replace(booleanProduct, product, ""), "  %step =", product + "  %step ="));
    reject("an Undefined factor cannot borrow Boolean product evidence",
           replace(booleanProduct, "#ctjs.boolean<true>", "#ctjs.undefined"));
    for (const auto & source :
         {productChild, replace(productChild, "mul %minus, %factor", "mul %factor, %minus"),
          stringProduct, replace(stringProduct, "mul %minus, %factor", "mul %factor, %minus"),
          replace(productChild, "binary sub %left, %magnitude",
                  "constant #ctjs.number<13830554455654793216>")}) {
        run({.what = "signed products retain the negative snapshot after source shrink",
             .body = source,
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "signed products discharge only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    const auto savedStringProduct =
        replace(stringProduct, product,
                "  %holder = ctjs.create_array [%factor] {storage_test_id = \"holder\"}\n"
                "  %savedFactor = ctjs.get_property %holder[%zero]\n"
                "  ctjs.set_property %holder[%zero], %x\n"
                "  %product = ctjs.binary mul %minus, %savedFactor\n");
    run({.what = "a saved String factor keeps its exact value after source replacement",
         .body = savedStringProduct,
         .arrays = "a:[one,x]; seed:[]; holder:[x]",
         .reads = "holder[0]=ctjs.constant; a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    run({.what = "a saved String product releases only unreturned children",
         .body = replace(savedStringProduct, "ctjs.return %result", "ctjs.return %zero"),
         .arrays = "a:[one,x]; seed:[]; holder:[x]",
         .reads = "holder[0]=ctjs.constant; a[0]=one; a[1]=x",
         .exit = "zero -> {}"},
        "x");
    run({.what = "a saved Boolean factor keeps its read-time value after replacement",
         .body = replace(savedStringProduct, "#ctjs.string<\"1\">", "#ctjs.boolean<true>"),
         .arrays = "a:[one,x]; seed:[]; holder:[x]",
         .reads = "holder[0]=ctjs.constant; a[0]=one; a[1]=x",
         .exit = "x -> {x}"});
    reject("a repeated String product needs independent invariance",
           replace(replace(stringProduct, product, ""), "  %step =", product + "  %step ="));
    const auto positiveProduct =
        replace(replace(productChild, factor, "  %factor = ctjs.unary neg %one\n"),
                "binary sub %i, %product", "binary_static add %i, %product");
    run({.what = "two negative factors supply a positive stride and own index",
         .body = replace(positiveProduct, "%base[%i]", "%base[%product]"),
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[1]=x; a[1]=x",
         .exit = "x -> {x}"});
    for (const std::string zero : {"#ctjs.number<0>", "#ctjs.number<9223372036854775808>",
                                   "#ctjs.string<\"0\">", "#ctjs.boolean<false>", "#ctjs.null"}) {
        const auto zeroProduct = replace(
            replace(replace(productChild, factor, "  %factor = ctjs.constant " + zero + "\n"),
                    "binary sub %i, %product", "binary_static add %i, %one"),
            "^header(%a, %zero, %zero", "^header(%a, %product, %zero");
        run({.what = "signed zero products supply index zero while retaining their origin",
             .body = replace(zeroProduct, "ctjs.return %result", "ctjs.return %product"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "product -> {}"},
            "x");
    }
    run({.what = "a signed product preserves a zero-trip saved child",
         .body = replace(productChild, "^header(%a, %zero, %zero", "^header(%a, %two, %x"),
         .arrays = "a:[one,x]; seed:[]",
         .exit = "x -> {x}"});
    reject("a negative product cannot supply an own array index",
           replace(productChild, "%base[%i]", "%base[%product]"),
           ArrayContentsFailure::UnknownIndex);
    reject("a repeated product needs independent invariance",
           replace(replace(productChild, product, ""), "  %step =", product + "  %step ="));
    for (const std::string constant :
         {"#ctjs.number<0>", "#ctjs.number<4602678819172646912>",
          "#ctjs.number<4751297606875873280>", "#ctjs.string<\"01\">", "#ctjs.string<\"-1\">",
          "#ctjs.string<\"1.0\">", "#ctjs.string<\"4294967295\">", "#ctjs.bigint<\"1\">"}) {
        reject("signed multiplication needs exact bounded operands and nonzero progress",
               replace(productChild, factor, "  %factor = ctjs.constant " + constant + "\n"));
    }
    reject("a product cannot borrow an unknown operand",
           replace(productChild, "mul %minus, %factor", "mul %minus, %p"),
           ArrayContentsFailure::UnsupportedOperation);
    const auto maximumProduct = replace(
        productChild, factor, "  %factor = ctjs.constant #ctjs.number<4751297606873776128>\n");
    reject("a signed product must bound its final induction update",
           replace(maximumProduct, "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    reject("two bounded signed factors cannot certify an overflowing product",
           replace(maximumProduct, "binary sub %left, %magnitude", "binary sub %zero, %magnitude"));
    for (const std::string unary : {"plus", "neg"}) {
        const std::string operation = unary == "plus" ? "sub" : "add";
        const std::string stringOperation = unary == "plus" ? "add" : "sub";
        const std::string makeString = "  %text = ctjs.constant #ctjs.string<\"1\">\n"
                                       "  %minus = ctjs.unary " +
                                       unary + " %text\n";
        const auto stringChild =
            replace(replace(negativeChild, makeNegativeUnit, makeString), "binary sub %i, %minus",
                    "binary " + stringOperation + " %i, %minus");
        const auto savedStringChild =
            replace(stringChild, "  %minus = ctjs.unary " + unary + " %text\n",
                    "  %seed = ctjs.create_array [%text] {storage_test_id = \"seed\"}\n"
                    "  %saved = ctjs.get_property %seed[%zero]\n"
                    "  ctjs.set_property %seed[%zero], %x\n"
                    "  %minus = ctjs.unary " +
                        unary + " %saved\n");
        for (const auto & source : {stringChild, savedStringChild}) {
            const bool saved = source == savedStringChild;
            const char * arrays = saved ? "a:[one,x]; seed:[x]" : "a:[one,x]";
            const char * reads =
                saved ? "seed[0]=ctjs.constant; a[0]=one; a[1]=x" : "a[0]=one; a[1]=x";
            run({.what = "canonical String unary snapshots retain original CFG children",
                 .body = source,
                 .arrays = arrays,
                 .reads = reads,
                 .exit = "x -> {x}"});
            run({.what = "canonical String unary snapshots discharge only unreturned children",
                 .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                 .arrays = arrays,
                 .reads = reads,
                 .exit = "zero -> {}"},
                "x");
        }
        for (const std::string text :
             {"01", "+1", "-1", "1.0", "1e0", " 1", "0x1", "4294967295", "NaN"}) {
            reject("unary String snapshots require the existing bounded canonical decimal proof",
                   replace(stringChild, "#ctjs.string<\"1\">", "#ctjs.string<\"" + text + "\">"));
        }
        run({.what = "repeated String Plus/Neg uses its original literal stride",
             .body =
                 replace(replace(stringChild, "  %minus = ctjs.unary " + unary + " %text\n", ""),
                         "  %step =", "  %minus = ctjs.unary " + unary + " %text\n  %step ="),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        if (unary == "neg") {
            reject("a negated String snapshot is never an own array index",
                   replace(stringChild, "%base[%i]", "%base[%minus]"),
                   ArrayContentsFailure::UnknownIndex);
        }
        const std::string makeSigned = "  %signed = ctjs.unary " + unary + " %minus\n";
        const auto signedChild =
            replace(replace(negativeChild, "  cf.br ^header", makeSigned + "  cf.br ^header"),
                    "binary sub %i, %minus", "binary " + operation + " %i, %signed");
        const auto literalChild =
            replace(signedChild, "ctjs.unary neg %magnitude", "ctjs.constant " + negativeLiteral);
        run({.what = "signed unary literals retain the returned child after CFG induction",
             .body = literalChild,
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "signed unary literals discharge only unreturned CFG children",
             .body = replace(literalChild, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        for (const auto & [input, negated] :
             {std::pair{"9223372036854775808", "0"},
              std::pair{"13830554455654793216", "4607182418800017408"},
              std::pair{"13835058055282163712", "4611686018427387904"},
              std::pair{"13974669643728551936", "4751297606873776128"}}) {
            run({.what =
                     "signed unary literals retain exact magnitude and original result identity",
                 .body = prefix + "  %input = ctjs.constant #ctjs.number<" + input +
                         ">\n"
                         "  %expected = ctjs.constant #ctjs.number<" +
                         (unary == "plus" ? input : negated) +
                         ">\n"
                         "  %actual = ctjs.unary " +
                         unary +
                         " %input {storage_test_id = \"actual\"}\n"
                         "  %index = ctjs.binary sub %actual, %expected\n"
                         "  %read = ctjs.get_property %a[%index]\n  ctjs.return %actual\n",
                 .arrays = "a:[one,two,three]",
                 .reads = "a[0]=one",
                 .exit = "actual -> {}"});
        }
        for (const std::string constant :
             {"#ctjs.number<0>", "#ctjs.number<9223372036854775808>",
              "#ctjs.number<13826050856027422720>", "#ctjs.number<13974669643730649088>",
              "#ctjs.number<18442240474082181120>", "#ctjs.number<9221120237041090560>",
              "#ctjs.string<\"-1\">", "#ctjs.bigint<\"-1\">"}) {
            const auto body = replace(literalChild, negativeLiteral, constant);
            if (constant == "#ctjs.string<\"-1\">") {
                run({.what = "negative canonical String unary strides retain original children",
                     .body = body,
                     .arrays = "a:[one,x]",
                     .reads = "a[0]=one; a[1]=x",
                     .exit = "x -> {x}"});
            } else {
                reject("signed unary strides require exact nonzero bounded conversion", body);
            }
        }
        run({.what = "signed unary literals retain their fixed stride on the CFG backedge",
             .body = replace(replace(literalChild, makeSigned, ""),
                             "  %step =", makeSigned + "  %step ="),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        if (unary == "plus") {
            reject("a signed unary literal's negative magnitude is never an own array index",
                   replace(literalChild, "%base[%i]", "%base[%signed]"),
                   ArrayContentsFailure::UnknownIndex);
        }
        run({.what = "signed unary snapshots retain the exact returned child",
             .body = signedChild,
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "signed unary snapshots discharge only unreturned children",
             .body = replace(signedChild, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
        run({.what = "a zero-trip signed unary stride preserves its saved child",
             .body = replace(signedChild, "^header(%a, %zero, %zero", "^header(%a, %two, %x"),
             .arrays = "a:[one,x]",
             .exit = "x -> {x}"});
        run({.what = "Plus and Neg preserve a negated zero as a nonnegative start",
             .body = replace(signedChild, "  cf.br ^header(%a, %zero, %zero",
                             "  %negativeZero = ctjs.unary neg %zero\n"
                             "  %start = ctjs.unary " +
                                 unary + " %negativeZero\n  cf.br ^header(%a, %start, %zero"),
             .arrays = "a:[one,x]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        const auto carriedSigned =
            replace(replace(carriedNegative, "  %unit = ctjs.unary neg %magnitude\n",
                            "  %negative = ctjs.unary neg %magnitude\n"
                            "  %unit = ctjs.unary " +
                                unary + " %negative\n"),
                    "binary sub %i, %d", "binary " + operation + " %i, %d");
        run({.what = "signed unary strides preserve exact carried values and final overshoot",
             .body = replace(replace(carriedSigned, "^exit(%sum :", "^exit(%index :"),
                             "  ctjs.return %result",
                             "  ctjs.append %zero to %a\n  ctjs.append %zero to %a\n"
                             "  %after = ctjs.get_property %a[%result]\n  ctjs.return %after"),
             .arrays = "a:[one,two,three,zero,zero]",
             .reads = "a[0]=one; a[2]=three; a[4]=zero",
             .exit = "zero -> {}"});
        reject("signed unary strides cannot change on the backedge",
               replace(carriedSigned, "^header(%base, %step, %added, %d",
                       "^header(%base, %step, %added, %one"));
        reject("signed unary strides cannot borrow the opposite sign",
               replace(signedChild, "binary " + operation + " %i, %signed",
                       "binary " + std::string{unary == "plus" ? "add" : "sub"} + " %i, %signed"));
        reject("a held negative snapshot cannot initialize an increasing loop",
               replace(signedChild, "^header(%a, %zero, %zero", "^header(%a, %minus, %zero"));
        reject(
            "repeated signed unary producers need independent invariance",
            replace(replace(signedChild, makeSigned, ""), "  %step =", makeSigned + "  %step ="));
        reject("signed unary snapshots cannot borrow an unknown input",
               replace(signedChild, makeSigned, "  %signed = ctjs.unary " + unary + " %p\n"),
               ArrayContentsFailure::UnsupportedOperation);
        if (unary == "plus") {
            reject("Plus preserves a negative snapshot without making it an own index",
                   replace(signedChild, "%base[%i]", "%base[%signed]"),
                   ArrayContentsFailure::UnknownIndex);
        } else {
            run({.what = "Neg restores an exact positive own index from its held magnitude",
                 .body = replace(signedChild, "%base[%i]", "%base[%signed]"),
                 .arrays = "a:[one,x]",
                 .reads = "a[1]=x; a[1]=x",
                 .exit = "x -> {x}"});
        }
        for (const std::string constant :
             {"#ctjs.number<0>", "#ctjs.number<9223372036854775808>",
              "#ctjs.number<4602678819172646912>", "#ctjs.number<4751297606875873280>",
              "#ctjs.number<9218868437227405312>", "#ctjs.number<9221120237041090560>",
              "#ctjs.string<\"1\">", "#ctjs.bigint<\"1\">"}) {
            const auto body = replace(signedChild, "#ctjs.number<4607182418800017408>", constant);
            if (constant == "#ctjs.string<\"1\">") {
                run({.what = "canonical String unary chains preserve their held signed Number",
                     .body = body,
                     .arrays = "a:[one,x]",
                     .reads = "a[0]=one; a[1]=x",
                     .exit = "x -> {x}"});
            } else {
                reject("signed unary strides require an exact nonzero bounded Number producer",
                       body);
            }
        }
        reject("signed unary strides still bound their final exact update",
               replace(replace(signedChild, "  %magnitude = ctjs.unary plus %one",
                               "  %maximum = ctjs.constant #ctjs.number<4751297606873776128>\n"
                               "  %magnitude = ctjs.unary plus %maximum"),
                       "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
    }
    reject("a different guard bound is not the array's own length",
           replace(original, "compare lt %index, %length", "compare lt %index, %three"));
    reject("a replaced array alias invalidates the guard certificate",
           replace(original, "^header(%base, %step, %added", "^header(%a, %step, %added"));
    std::string changedKey = replace(
        original, "  cf.br ^header(%a, %zero, %zero : !ctjs.value, !ctjs.value, !ctjs.value)",
        "  %name = ctjs.constant #ctjs.string<\"length\">\n"
        "  cf.br ^header(%a, %zero, %zero, %name : !ctjs.value, !ctjs.value, "
        "!ctjs.value, !ctjs.value)");
    changedKey =
        replace(changedKey, "%sum: !ctjs.value):", "%sum: !ctjs.value, %property: !ctjs.value):");
    changedKey = replace(changedKey, "%array[%key]", "%array[%property]");
    changedKey =
        replace(changedKey, "^header(%base, %step, %added : !ctjs.value, !ctjs.value, !ctjs.value)",
                "^header(%base, %step, %added, %zero : !ctjs.value, !ctjs.value, "
                "!ctjs.value, !ctjs.value)");
    reject("a changing guard property cannot borrow its first length snapshot", changedKey);
    reject("array mutation inside the loop refuses before replay",
           replace(original, "  %read =", "  ctjs.set_property %base[%i], %zero\n  %read ="));
    reject("length mutation inside the loop invalidates the stable bound",
           replace(original, "  %read =", "  ctjs.set_property %base[%key], %zero\n  %read ="));
    reject("allocation in a repeated block cannot collapse dynamic instances",
           replace(original, "  %read =", "  %fresh = ctjs.create_array []\n  %read ="));
    reject("publication inside the loop is rejected structurally",
           replace(original, "  %read =", "  ctjs.store_global \"held\", %base\n  %read ="));
    reject("a zero-trip loop still refuses an unsupported operation",
           replace(replace(original, "[%one, %two, %three]", "[]"),
                   "  %read =", "  ctjs.store_global \"held\", %base\n  %read ="));
    reject("effects after a finite loop invalidate complete contents",
           replace(original, "  ctjs.return %result",
                   "  ctjs.store_global \"held\", %a\n"
                   "  ctjs.return %result"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("an offset read must independently remain in bounds on the last iteration",
           replace(original, "  %read = ctjs.get_property %base[%i]",
                   "  %shifted = ctjs.binary_static add %i, %one\n"
                   "  %read = ctjs.get_property %base[%shifted]"),
           ArrayContentsFailure::MissingElement);
    reject("a different shorter array cannot borrow the guard array's bound",
           replace(replace(original, "  %a =",
                           "  %other = ctjs.create_array [%one] "
                           "{storage_test_id = \"other\"}\n  %a ="),
                   "%base[%i]", "%other[%i]"),
           ArrayContentsFailure::MissingElement);
    reject("every later element must independently exclude object coercion",
           replace(replace(original, "  %a =",
                           "  %child = ctjs.create_array [] "
                           "{storage_test_id = \"child\"}\n  %a ="),
                   "[%one, %two, %three]", "[%one, %child, %three]"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("opaque loop-carried keys cannot retain a prior exact Number fact",
           replace(replace(original, "%base[%i]", "%base[%s]"), "^header(%base, %step, %added",
                   "^header(%base, %step, %p"),
           ArrayContentsFailure::UnknownIndex);
    unsigned liveStates = 0;
    for (const std::string & source :
         {computedChild, nonzeroChild, computedUnitChild, computedStrideChild}) {
        const bool starts = source == computedChild || source == nonzeroChild;
        const bool nonunit = source == computedStrideChild;
        contents_row live{.what = "live induction facts override stale solver and forged markers",
                          .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                          .arrays = nonunit ? "a:[one,two,x]" : "a:[one,x]",
                          .reads = source == nonzeroChild ? "a[1]=x"
                                   : nonunit              ? "a[0]=one; a[2]=x"
                                                          : "a[0]=one; a[1]=x",
                          .exit = "zero -> {}"};
        if (auto module = mlir::parseSourceString<mlir::ModuleOp>(
                std::string{kPrologue} + live.body + "}\n", &context)) {
            ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
            ctjs::BinaryOp producer;
            mlir::Value three;
            module->walk([&](ctjs::BinaryOp op) { producer = op; });
            module->walk([&](ctjs::ConstantOp op) {
                if (contentsLabel(op) == "three") { three = op.getResult(); }
            });
            const mlir::Value one = producer.getRhs();
            mlir::OpBuilder builder(function);
            function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
            function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
            producer->setAttr("ctnative.array_index",
                              builder.getI64IntegerAttr(source == computedChild ? 0
                                                        : nonunit               ? 2
                                                                                : 1));
            mlir::DataFlowSolver stale;
            stale.load<mlir::dataflow::DeadCodeAnalysis>();
            stale.load<mlir::dataflow::SparseConstantPropagation>();
            stale.load<EscapeAnalysis>();
            if (failed(stale.initializeAndRun(*module))) {
                fail(row{.what = live.what, .body = live.body, .expected = ""},
                     "the induction stale solver did not converge");
            }
            const mlir::Value invalid = starts ? three : producer.getLhs();
            for (mlir::Value rhs : {one, invalid, one}) {
                producer->setOperand(1, rhs);
                const bool complete = rhs == one;
                live.failure = complete ? ArrayContentsFailure::None
                                        : ArrayContentsFailure::UnsupportedControlFlow;
                checkArrayContents(*module, live);
                budgets += checkArrayRetention(*module, {.what = live.what,
                                                         .body = live.body,
                                                         .discharged = complete ? "x" : "",
                                                         .complete = complete});
                const auto verdicts = computeVerdicts(stale, function);
                if (verdicts.arrayRetentionComplete != complete ||
                    verdicts.confinedStoredSites != (complete ? 1U : 0U)) {
                    fail(row{.what = live.what, .body = live.body, .expected = ""},
                         "stale solver or forged marker supplied induction authority");
                }
                ++liveStates;
            }
        } else {
            fail(row{.what = live.what, .body = live.body, .expected = ""},
                 "the live induction fixture did not parse");
        }
    }
    std::string many = "%one";
    for (unsigned i = 1; i < 32; ++i) { many += ", %one"; }
    const std::string longLoop = replace(original, "%one, %two, %three", many);
    std::size_t unitWork = 0;
    for (const std::string & source :
         {longLoop, replace(longLoop, "add %i, %one", "add %i, %two")}) {
        if (auto module = mlir::parseSourceString<mlir::ModuleOp>(
                std::string{kPrologue} + source + "}\n", &context)) {
            ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
            const auto complete = computeArrayContents(function);
            const auto partial = computeArrayContents(function, 128);
            const bool unit = source == longLoop;
            if (unit) { unitWork = complete.work; }
            if (!complete.complete || complete.reads.size() != (unit ? 32U : 16U) ||
                (!unit && complete.work >= unitWork) || partial.complete ||
                partial.failure != ArrayContentsFailure::WorkLimit || partial.work != 128 ||
                !partial.arrays.empty() || !partial.reads.empty() || !partial.writes.empty() ||
                !partial.exits.empty()) {
                fail(row{.what = "finite strides charge visited iterations and obey work limits",
                         .body = source,
                         .expected = ""},
                     "a long loop lost exact reads, bounded replay or prefix isolation");
            }
        } else {
            fail(row{.what = "finite induction work budget", .body = source, .expected = ""},
                 "the long induction fixture did not parse");
        }
    }
    std::printf("array induction: %u rows, %u live states, %zu retention budget cutoffs\n", rows,
                liveStates, budgets);
}

} // namespace ctcompile::test::escape::arrays
