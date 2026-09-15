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
    reject("a computed nonzero start cannot borrow the zero induction certificate",
           replace(computed, "binary sub %one, %one", "binary sub %two, %one"));
    reject("an old nonempty length cannot borrow a subsequently emptied array's zero",
           replace(replace(savedLength, "%seed = ctjs.create_array []",
                           "%seed = ctjs.create_array [%one]"),
                   "ctjs.append %one to %seed", "ctjs.set_property %seed[%name], %zero"));
    reject("even an untaken predecessor must independently supply an exact zero",
           replace(alternateStart, "^entry(%start :", "^entry(%one :"));
    for (const std::string constant :
         {"#ctjs.string<\"0\">", "#ctjs.bigint<\"0\">", "#ctjs.number<4602678819172646912>"}) {
        reject("coercible and fractional starts do not become exact Number zero",
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
    }
    reject("a stride computation that exceeds the bounded range supplies no wrapped fact",
           replace(replace(maxStride, "  cf.br ^header",
                           "  %overflow = ctjs.binary_static add %max, %one\n  cf.br ^header"),
                   "add %i, %max", "add %i, %overflow"));
    reject("inclusive guards do not prove an own index",
           replace(original, "compare lt", "compare le"));
    reject("inverted guards do not borrow strict induction",
           replace(original, "compare lt %index, %length", "compare lt %length, %index"));
    reject("an unknown entry index does not borrow literal zero",
           replace(original, "^header(%a, %zero, %zero", "^header(%a, %p, %zero"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("a nonzero start stays outside zero induction",
           replace(original, "^header(%a, %zero, %zero", "^header(%a, %one, %zero"));
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
    reject("a dynamic Add update has no static Number induction certificate",
           replace(original, "binary_static add %i, %one", "binary add %i, %one"));
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
    for (const std::string & source : {computedChild, computedUnitChild, computedStrideChild}) {
        const bool nonunit = source == computedStrideChild;
        contents_row live{.what = "live induction facts override stale solver and forged markers",
                          .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                          .arrays = nonunit ? "a:[one,two,x]" : "a:[one,x]",
                          .reads = nonunit ? "a[0]=one; a[2]=x" : "a[0]=one; a[1]=x",
                          .exit = "zero -> {}"};
        if (auto module = mlir::parseSourceString<mlir::ModuleOp>(
                std::string{kPrologue} + live.body + "}\n", &context)) {
            ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
            ctjs::BinaryOp producer;
            mlir::Value zero;
            module->walk([&](ctjs::BinaryOp op) { producer = op; });
            module->walk([&](ctjs::ConstantOp op) {
                if (contentsLabel(op) == "zero") { zero = op.getResult(); }
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
            const mlir::Value invalid = source == computedChild ? zero : producer.getLhs();
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
