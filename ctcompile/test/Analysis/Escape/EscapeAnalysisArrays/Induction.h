#pragma once

#include "Harness.h"

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
    reject("Neg of a coercible String does not establish an original Number stride",
           replace(subtractNegated, "#ctjs.number<4607182418800017408>", "#ctjs.string<\"1\">"));
    for (const std::string constant :
         {"#ctjs.number<0>", "#ctjs.number<4607182418800017408>",
          "#ctjs.number<13826050856027422720>", "#ctjs.number<13974669643730649088>",
          "#ctjs.number<18442240474082181120>", "#ctjs.number<9221120237041090560>",
          "#ctjs.string<\"-1\">", "#ctjs.bigint<\"-1\">"}) {
        reject("subtraction requires an original bounded negative integral Number stride",
               replace(subtract, negativeLiteral, constant));
    }
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
        reject("a computed Neg stride requires a bounded strictly positive Number input",
               replace(negativeChild, "#ctjs.number<4607182418800017408>", constant));
    }
    reject("Neg of an unknown producer cannot supply a bounded stride",
           replace(negativeChild, "unary neg %magnitude", "unary neg %p"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("a held Neg stride cannot overflow on its final update",
           replace(replace(negativeChild, "  %magnitude = ctjs.unary plus %one",
                           "  %maximum = ctjs.constant #ctjs.number<4751297606873776128>\n"
                           "  %magnitude = ctjs.unary plus %maximum"),
                   "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
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
    reject("a coercible left String supplies no negative Sub snapshot",
           replace(subChild, "unary plus %one", "constant #ctjs.string<\"1\">"));
    reject("a coercible right String supplies no negative Sub snapshot",
           replace(subChild, "unary plus %two", "constant #ctjs.string<\"2\">"));
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
        reject("signed division and remainder cannot borrow coercible Strings",
               replace(source, makeResult,
                       "  %divisor = ctjs.constant #ctjs.string<\"1\">\n" +
                           replace(makeResult, ", " + std::string(divisor), ", %divisor")));
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
        }
    }
    const std::string factor = "  %factor = ctjs.unary plus %one\n";
    const std::string product = "  %product = ctjs.binary mul %minus, %factor "
                                "{storage_test_id = \"product\"}\n";
    const auto productChild =
        replace(replace(savedSub, "  cf.br ^header", factor + product + "  cf.br ^header"),
                "sub %i, %minus", "sub %i, %product");
    for (const auto & source :
         {productChild, replace(productChild, "mul %minus, %factor", "mul %factor, %minus"),
          replace(productChild, "binary sub %left, %magnitude",
                  "constant #ctjs.number<13830554455654793216>")}) {
        run({.what = "signed Number products retain the negative snapshot after source shrink",
             .body = source,
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "x -> {x}"});
        run({.what = "signed Number products discharge only unreturned children",
             .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
             .arrays = "a:[one,x]; seed:[]",
             .reads = "a[0]=one; a[1]=x",
             .exit = "zero -> {}"},
            "x");
    }
    const auto positiveProduct =
        replace(replace(productChild, factor, "  %factor = ctjs.unary neg %one\n"),
                "binary sub %i, %product", "binary_static add %i, %product");
    run({.what = "two negative factors supply a positive stride and own index",
         .body = replace(positiveProduct, "%base[%i]", "%base[%product]"),
         .arrays = "a:[one,x]; seed:[]",
         .reads = "a[1]=x; a[1]=x",
         .exit = "x -> {x}"});
    for (const std::string zero : {"#ctjs.number<0>", "#ctjs.number<9223372036854775808>"}) {
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
          "#ctjs.number<4751297606875873280>", "#ctjs.string<\"1\">", "#ctjs.bigint<\"1\">"}) {
        reject("signed multiplication needs bounded Number operands and nonzero progress",
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
        const std::string makeSigned = "  %signed = ctjs.unary " + unary + " %minus\n";
        const auto signedChild =
            replace(replace(negativeChild, "  cf.br ^header", makeSigned + "  cf.br ^header"),
                    "binary sub %i, %minus", "binary " + operation + " %i, %signed");
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
            reject("signed unary strides require an exact nonzero bounded Number producer",
                   replace(signedChild, "#ctjs.number<4607182418800017408>", constant));
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
