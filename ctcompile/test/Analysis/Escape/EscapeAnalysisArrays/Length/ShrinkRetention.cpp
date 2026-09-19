#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::length_detail {

void LengthCases::shrinkRetention() {
    run({.what = "a same-length write preserves every original child",
         .body = values + one + read + "  ctjs.set_property %a[%key], %one\n  ctjs.return %a\n",
         .arrays = "a:[x]",
         .exit = "a -> {a,x}"},
        "");
    run({.what = "a bounded nonzero shrink retains the unremoved prefix",
         .body = values + one + "  ctjs.append %x to %a\n" +
                 "  ctjs.set_property %a[%key], %one\n  ctjs.return %a\n",
         .arrays = "a:[x]",
         .exit = "a -> {a,x}"},
        "");
    run({.what = "negative zero is an exact empty length",
         .body = values + "  %wanted = ctjs.constant #ctjs.number<9223372036854775808>\n" + read +
                 "  ctjs.set_property %a[%key], %wanted\n" + done,
         .arrays = "a:[]",
         .exit = "length -> {}"});
    run({.what = "unfolded source negative zero clears length without changing saved values",
         .body = values + read +
                 "  %wanted = ctjs.unary neg %zero\n"
                 "  ctjs.set_property %a[%key], %wanted\n" +
                 done,
         .arrays = "a:[]",
         .exit = "length -> {}"});
    for (const std::string literal : {"#ctjs.string<\"0\">", "#ctjs.bigint<\"0\">"}) {
        if (literal == "#ctjs.string<\"0\">") {
            run({.what = "canonical String negated zero supplies an exact empty length",
                 .body = values + "  %input = ctjs.constant " + literal +
                         "\n  %wanted = ctjs.unary neg %input\n" + read +
                         "  ctjs.set_property %a[%key], %wanted\n" + done,
                 .arrays = "a:[]",
                 .exit = "length -> {}"});
            continue;
        }
        run({.what = "negated coercive zero cannot borrow Number length authority",
             .body = values + "  %input = ctjs.constant " + literal +
                     "\n  %wanted = ctjs.unary neg %input\n" + read +
                     "  ctjs.set_property %a[%key], %wanted\n" + done,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    run({.what = "a saved child remains retained after its array is truncated",
         .body = values + read + "  %saved = ctjs.get_property %a[%zero]\n" + shrink +
                 "  ctjs.return %saved\n",
         .arrays = "a:[]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        "");
    run({.what = "an exact array alias truncates only its own contents",
         .body = values + "  %b = ctjs.create_array [%x] {storage_test_id = \"b\"}\n"
                          "  %aliases = ctjs.create_array [%a] {storage_test_id = \"aliases\"}\n"
                          "  %alias = ctjs.get_property %aliases[%zero]\n"
                          "  ctjs.set_property %alias[%key], %zero\n  ctjs.return %b\n",
         .arrays = "a:[]; b:[x]; aliases:[a]",
         .reads = "aliases[0]=a",
         .exit = "b -> {b,x}"},
        "a");
    run({.what = "saved length and index Numbers survive shrink while a new read sees zero",
         .body = values + one + "  ctjs.append %zero to %a\n" + read + subtract + shrink +
                 "  %keys = ctjs.create_array [%zero, %one] {storage_test_id = \"keys\"}\n"
                 "  %saved = ctjs.get_property %keys[%index]\n"
                 "  %later = ctjs.get_property %a[%key] {storage_test_id = \"later\"}\n"
                 "  %fresh = ctjs.get_property %keys[%later]\n"
                 "  %result = ctjs.create_array [%length, %index, %later, %saved, %fresh] "
                 "{storage_test_id = \"result\"}\n"
                 "  ctjs.return %result\n",
         .arrays =
             "a:[]; keys:[zero,ctjs.constant]; result:[length,index,later,ctjs.constant,zero]",
         .reads = "keys[1]=ctjs.constant; keys[0]=zero",
         .exit = "result -> {result}"});
    run({.what = "a saved length is not retargeted when subtraction happens after shrink",
         .body = values + one + "  ctjs.append %zero to %a\n" + read +
                 "  ctjs.set_property %a[%key], %one\n" + subtract + indexed,
         .failure = ArrayContentsFailure::MissingElement});
    run({.what = "a reloaded old length cannot borrow a later post-shrink value",
         .body = values + one + "  ctjs.append %zero to %a\n" + read +
                 "  %snapshots = ctjs.create_array [%length]\n"
                 "  %saved = ctjs.get_property %snapshots[%zero]\n"
                 "  ctjs.set_property %a[%key], %one\n"
                 "  %later = ctjs.get_property %a[%key]\n"
                 "  ctjs.set_property %snapshots[%zero], %later\n"
                 "  cf.br ^next(%saved : !ctjs.value)\n"
                 "^next(%oldLength: !ctjs.value):\n"
                 "  %index = ctjs.binary sub %oldLength, %one\n" +
                 indexed,
         .failure = ArrayContentsFailure::MissingElement});
    run({.what = "a removed slot cannot be read through its saved original index",
         .body = values + one + read + subtract + shrink +
                 "  %saved = ctjs.get_property %a[%index]\n  ctjs.return %saved\n",
         .failure = ArrayContentsFailure::MissingElement});
    run({.what = "literal append after truncation establishes only its new element",
         .body = values + read + shrink +
                 "  ctjs.append %zero to %a\n"
                 "  %saved = ctjs.get_property %a[%zero]\n  ctjs.return %a\n",
         .arrays = "a:[zero]",
         .reads = "a[0]=zero",
         .exit = "a -> {a}"});
    run({.what = "a loaded original Number length is independent of its overwritten slot",
         .body = values + "  %targets = ctjs.create_array [%zero] {storage_test_id = \"targets\"}\n"
                          "  %wanted = ctjs.get_property %targets[%zero]\n"
                          "  ctjs.set_property %targets[%zero], %x\n"
                          "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
         .arrays = "a:[]; targets:[x]",
         .reads = "targets[0]=zero",
         .exit = "a -> {a}"});
    run({.what = "forwarded original array key and Number keep their exact identities",
         .body = values + "  cf.br ^next(%a, %key, %zero : !ctjs.value, !ctjs.value, !ctjs.value)\n"
                          "^next(%alias: !ctjs.value, %name: !ctjs.value, %wanted: !ctjs.value):\n"
                          "  ctjs.set_property %alias[%name], %wanted\n  ctjs.return %alias\n",
         .arrays = "a:[]",
         .exit = "a -> {a}"});
    run({.what = "one truncating arm cannot release a child retained by the other arm",
         .body = values +
                 "  %flag = ctjs.truthy %zero\n"
                 "  cf.cond_br %flag, ^left, ^right\n^left:\n" +
                 shrink + "  cf.br ^join\n^right:\n  cf.br ^join\n^join:\n  ctjs.return %a\n",
         .arrays = "a:[] | a:[x]",
         .exit = "a -> {a}; a -> {a,x}"},
        "");
    for (const std::string literal : {"#ctjs.number<4602678819172646912>",  // 0.5
                                      "#ctjs.number<13830554455654793216>", // -1
                                      "#ctjs.number<4751297606875873280>",  // 2^32
                                      "#ctjs.number<4845873199050653696>",  // 2^53
                                      "#ctjs.number<9218868437227405312>",  // infinity
                                      "#ctjs.number<18442240474082181120>", // -infinity
                                      "#ctjs.number<9221120237041090560>",  // NaN
                                      "#ctjs.string<\"0\">", "#ctjs.bigint<\"0\">",
                                      "#ctjs.boolean<false>", "#ctjs.null", "#ctjs.undefined"}) {
        run({.what = "length truncation requires an independently valid original Number",
             .body = values + "  %wanted = ctjs.constant " + literal + "\n" +
                     "  ctjs.set_property %a[%key], %wanted\n  ctjs.return %a\n",
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    run({.what = "a valid increasing length remains outside dense truncation",
         .body = values + "  %two = ctjs.constant #ctjs.number<4611686018427387904>\n"
                          "  ctjs.set_property %a[%key], %two\n  ctjs.return %a\n",
         .failure = ArrayContentsFailure::MissingElement});
    for (const std::string wanted : {"%p", "%x"}) {
        run({.what = "opaque length targets gain no truncation authority",
             .body = values + one + read + subtract + "  ctjs.set_property %a[%key], " + wanted +
                     "\n  ctjs.return %a\n",
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    const contents_row shrinkCycle{
        .what = "truncation does not erase the historical cycle obligation",
        .body = values + "  ctjs.append %a to %a\n" + shrink + "  ctjs.return %a\n",
        .arrays = "a:[]",
        .exit = "a -> {a}",
        .writes = "ctjs.create_array[0]:a[0]=x; ctjs.append[1]:a[1]=a"};
    if (auto module = parse(shrinkCycle)) {
        checkArrayContents(*module, shrinkCycle);
        budgets += checkArrayRetention(
            *module, {.what = shrinkCycle.what, .body = shrinkCycle.body, .complete = false});
        ++rows;
    } else {
        fail(row{.what = shrinkCycle.what, .body = shrinkCycle.body, .expected = ""},
             "the historical truncation cycle did not parse");
    }
    contents_row wideShrink = originalShrink;
    std::string removed;
    for (unsigned i = 0; i < 32; ++i) { removed += "  ctjs.append %zero to %a\n"; }
    wideShrink.body.insert(wideShrink.body.find("  %length ="), removed);
    auto narrowShrinkModule = parse(originalShrink);
    auto wideShrinkModule = parse(wideShrink);
    if (narrowShrinkModule && wideShrinkModule) {
        const auto narrow =
            computeArrayContents(*narrowShrinkModule->getOps<ctjs::FuncOp>().begin());
        const auto expanded =
            computeArrayContents(*wideShrinkModule->getOps<ctjs::FuncOp>().begin());
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 64) {
            fail(row{.what = "dense shrink charges every removed element",
                     .body = wideShrink.body,
                     .expected = ""},
                 "32 extra elements did not cost one append and one removal each");
        }
        check(*wideShrinkModule, wideShrink, "x");
    } else {
        fail(row{.what = wideShrink.what, .body = wideShrink.body, .expected = ""},
             "the wide dense shrink did not parse");
    }
    contents_row wide = branch;
    std::string extra;
    for (unsigned i = 0; i < 32; ++i) {
        extra += "  %extra_" + std::to_string(i) + " = ctjs.get_property %a[%key]\n";
    }
    wide.body.insert(wide.body.find("  %flag ="), extra);
    auto narrowModule = parse(branch);
    auto wideModule = parse(wide);
    if (narrowModule && wideModule) {
        const auto narrow = computeArrayContents(*narrowModule->getOps<ctjs::FuncOp>().begin());
        const auto expanded = computeArrayContents(*wideModule->getOps<ctjs::FuncOp>().begin());
        if (!narrow.complete || !expanded.complete || expanded.work != narrow.work + 96) {
            fail(row{.what = "length snapshots charge every independent result",
                     .body = wide.body,
                     .expected = ""},
                 "32 extra lengths did not charge each read, exact value and held snapshot");
        }
        check(*wideModule, wide, "x");
    } else {
        fail(row{.what = wide.what, .body = wide.body, .expected = ""},
             "the wide length snapshot did not parse");
    }
    mutation = {.what = "live length origins defeat stale and forged completion",
                .body = values + read +
                        "  %result = ctjs.unary neg %length "
                        "{storage_test_id = \"result\"}\n" +
                        overwrite + "  ctjs.return %result\n",
                .arrays = "a:[zero]",
                .exit = "result -> {}"};
}

} // namespace ctcompile::test::escape::arrays::length_detail
