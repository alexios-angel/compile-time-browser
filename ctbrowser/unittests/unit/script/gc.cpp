//===- ctbrowser/unittests/unit/script/gc.cpp - Collector rooting tests ---===//
//
// The precise collector, made to run at the moment a C++ local is the only
// reference to a heap value.
//
// In a shipped page nothing collects while script is running: the only
// production trigger is `collect_if_due`, once per tick, from the browser's
// frame loop. So a rooting bug inside a synchronous builtin call is unreachable
// from an ordinary page and invisible to test262 and WPT. It is reachable
// through `$262.gc` and through GC-stress mode, and the ctcompile differential
// tests run this interpreter in those modes and treat its answers as the
// reference the compiler must match, so a wrong answer here becomes a wrong
// expectation there.
//
// `gc()` here is `$262.gc` without test262: c.collect(), the real mark-sweep,
// so a case in this file is a case a corpus run can reproduce. Every assertion
// checks two things: the answer, and that a collection actually happened. An
// answer alone proves nothing, because it is the same when the collector
// silently did not run, which is exactly how a broken forced-GC mode looks.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include "options.hpp"
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

using ctbrowser::script::context;
using ctbrowser::script::program;
using ctbrowser::script::run_result;
using ctbrowser::script::value;

namespace {

/// The result of run_with_gc: the script's answer as a string ("THREW" if it
/// threw) and how many collections ran.
struct gc_run {
    std::string answer;
    std::size_t collections = 0;
};

/// Compiles and runs \p source in a fresh context with a global `gc()` that
/// forces a full collection.
[[nodiscard]] gc_run run_with_gc(const std::string & source) {
    const program prog = ctbrowser::script::compiler::compile(source);
    context cx;
    ctbrowser::script::install_builtins(cx);
    // The same hook ct262 installs, under the same name a test262 case uses.
    cx.define_native("gc", [](context & c, std::span<value>) {
        return value::number(static_cast<double>(c.collect()));
    });
    const run_result r = cx.run(prog);
    gc_run out;
    out.collections = cx.collections();
    out.answer = r.ok ? cx.to_string(r.returned) : std::string{"THREW"};
    return out;
}

/// Fails the test unless \p source answers \p want and at least one
/// collection ran.
void expect_gc(std::string_view what, const std::string & source, std::string_view want) {
    const gc_run got = run_with_gc(source);
    if (got.answer != want) {
        std::printf("FAIL %-46s => %s (want %s)\n", std::string{what}.c_str(), got.answer.c_str(),
                    std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
    // A guard on the guard: a `gc()` that collected nothing gives the same
    // answer as a correct engine, so without this the whole file could pass
    // while measuring an interpreter no collection ever entered.
    if (got.collections == 0) {
        std::printf("FAIL %-46s ran NO collection - the case measured nothing\n",
                    std::string{what}.c_str());
        ++ctbrowser_test_failures;
    }
}

void test_await_constructor_survives_a_collection() {
    expect_gc("await constructor getter collects", R"(
        async function f() {
            const kept = {answer: 42};
            const p = Promise.resolve(kept);
            Object.defineProperty(p, 'constructor', {get() { gc(); return Promise; }});
            return (await p).answer;
        }
        return await f();
    )",
              "42");
    expect_gc("await adoption getter collects", R"(
        async function f() {
            const kept = {answer: 42};
            const p = Promise.resolve(0);
            p.constructor = function Other() {};
            Object.defineProperty(p, 'then', {get() {
                gc(); return resolve => { gc(); resolve(kept); };
            }});
            return (await p).answer;
        }
        return await f();
    )",
              "42");
}

void test_argument_list_survives_a_collection() {
    for (const std::string_view invoke :
         {"target.apply(null, args)", "Reflect.apply(target, null, args)",
          "Reflect.construct(target, args)"}) {
        expect_gc(invoke,
                  R"(
            function target(x) { return x; }
            var args = {length: 2, 0: {answer: 42},
                        get 1() { delete this[0]; gc(); return 0; }};
            return ()" +
                      std::string{invoke} + R"().answer;
        )",
                  "42");
    }
}

void test_constructor_prototype_survives_a_collection() {
    expect_gc("escaped super arrows share a rooted receiver", R"(
        let finish, read;
        class A { constructor() { return {answer: 42}; } }
        class B extends A {
            constructor() {
                read = () => this;
                finish = () => super();
                return {};
            }
        }
        new B();
        gc();
        finish();
        finish = null;
        gc();
        return read().answer;
    )",
              "42");
    expect_gc("constructor keeps shared receiver after arrow returns", R"(
        class A { constructor() { return {answer: 42}; } }
        class B extends A {
            constructor() {
                (() => super())();
                gc();
            }
        }
        return new B().answer;
    )",
              "42");
    expect_gc("escaped super arrow retains new.target", R"(
        let finish;
        class A { constructor() { return {target: new.target}; } }
        class B extends A { constructor() { finish = () => super(); return {}; } }
        (function () {
            function Other() {}
            Other.prototype = null;
            Reflect.construct(B, [], Other);
        })();
        gc();
        return finish().target.name;
    )",
              "Other");
    expect_gc("constructor prototype getter collects", R"(
        const handle = Proxy.revocable(function () { this.answer = 42; }, {
            get(target, key) {
                if (key === 'prototype') {
                    handle.revoke();
                    gc();
                    return {marker: 7};
                }
                return target[key];
            }
        });
        const made = new handle.proxy();
        return made.answer + ',' + made.marker;
    )",
              "42,7");
}

void test_private_brand_survives_a_collection() {
    expect_gc("stamped receiver retains private method home", R"(
        const pair = (function () {
            class B { constructor(o) { return o; } }
            class C extends B {
                #method() { return 42; }
                get #value() { return this.#method(); }
                static reader() { return function (o) { return o.#value; }; }
            }
            const o = {};
            new C(o);
            return [o, C.reader()];
        })();
        gc();
        const filler = [];
        for (let i = 0; i < 400; i++) { filler.push({answer: i}); }
        gc();
        return pair[1](pair[0]);
    )",
              "42");
}

void test_private_proxy_survives_a_collection() {
    expect_gc("revoked proxy retains private fields and method home", R"(
        const pair = (function () {
            class B { constructor(o) { return o; } }
            class C extends B {
                #data = {answer: 42};
                #method() { gc(); return this.#data.answer; }
                get #value() { return this.#method(); }
                static reader() { return function (o) { return o.#value; }; }
            }
            const r = Proxy.revocable({}, {});
            new C(r.proxy);
            r.revoke();
            return [r.proxy, C.reader()];
        })();
        gc();
        const filler = [];
        for (let i = 0; i < 400; i++) { filler.push({answer: i}); }
        gc();
        return pair[1](pair[0]);
    )",
              "42");
}

// --- native lambda captures ------------------------------------------------
//
// A `value` captured by a C++ lambda inside a native is invisible to the
// collector unless the native also lists it in native_object::retained.
// These cases free everything else and check the captured values survive.

/// `Function.prototype.bind` captures three values - the target closure, the
/// receiver and the bound arguments - in its native's lambda. The IIFE makes
/// that capture the only reference: after it returns, `target` and `tag` are
/// reachable only through the bound function, so the test fails unless they
/// are rooted.
void test_bind_captures_survive_a_collection() {
    expect_gc("bind's target and bound args survive gc", R"(
        var g = (function () {
            var tag = { name: 'bound' };
            function target(a, b) { return a.name + '/' + b.name; }
            return target.bind(null, tag);
        })();
        gc();
        // Allocate over the freed cells, so a stale pointer reads somebody
        // else's object rather than merely reading memory nobody rewrote.
        var filler = [];
        for (var i = 0; i < 400; i++) { filler.push({ name: 'filler' }); }
        gc();
        return g({ name: 'late' });
    )",
              "bound/late");
}

/// `Symbol.for`'s registry must keep its symbols alive across a collection:
/// a freed symbol would let `Symbol.for` hand back a pointer into a recycled
/// cell.
void test_symbol_registry_survives_a_collection() {
    expect_gc("Symbol.for's registry survives gc", R"(
        var s = Symbol.for('KEY');
        s = null;
        gc();
        var filler = [];
        for (var i = 0; i < 400; i++) { filler.push(Symbol('recycled')); }
        return Symbol.for('KEY').description;
    )",
              "KEY");
    expect_gc("Symbol.keyFor's registry survives gc", R"(
        var s = Symbol.for('OTHER');
        s = null;
        gc();
        var filler = [];
        for (var i = 0; i < 400; i++) { filler.push(Symbol('recycled')); }
        return Symbol.keyFor(Symbol.for('OTHER'));
    )",
              "OTHER");
    // And identity, which is the one guarantee the registry exists to give.
    expect_gc("Symbol.for interns across a gc", R"(
        var a = Symbol.for('SAME');
        a = null;
        gc();
        return String(Symbol.for('SAME') === Symbol.for('SAME'));
    )",
              "true");
}

void test_native_accessors_survive_a_collection() {
    expect_gc("native accessor closures retain their cells", R"(
        (function () {
            var stored = {name: 'retained'};
            Object.defineProperty(Array, 'held', {
                get: function() { return stored.name; },
                set: function(value) { stored = value; }
            });
        })();
        gc();
        var filler = [];
        for (var i = 0; i < 400; i++) { filler.push({name: 'recycled'}); }
        var before = Array.held;
        Array.held = {name: 'updated'};
        gc();
        return before + '/' + Array.held;
    )",
              "retained/updated");
    // Resolving the method before arguments must leave both its callable and
    // original receiver rooted even when an argument removes every alias.
    expect_gc("saved callee and receiver survive argument gc", R"(
        var object = {name: 'receiver', get method() {
            var captured = {name: 'callee'};
            return function(value) { return captured.name + '/' + this.name + '/' + value; };
        }};
        function argument() {
            object = null;
            gc();
            return 'argument';
        }
        return object.method(argument());
    )",
              "callee/receiver/argument");
}

// --- array builtins across a callback --------------------------------------
//
// Each of these holds a heap value in a C++ local across a call back into the
// VM, which `context::rooted` must protect. The callback's `gc()` is what a
// page cannot do and `$262.gc` can.

void test_map_result_survives_a_collection() {
    // `map`'s `out` is allocated before the first callback and is held only in
    // a C++ local until it is returned; unrooted, the first `gc()` would free
    // the array the remaining iterations then push into.
    expect_gc("map's result array survives gc in the callback", R"(
        var out = [1, 2, 3].map(function (x) { gc(); return { v: x }; });
        return out.length + ':' + out[0].v + ',' + out[1].v + ',' + out[2].v;
    )",
              "3:1,2,3");
}

void test_filter_result_survives_a_collection() {
    // `filter`'s result array is held only in a C++ local, like `map`'s; its
    // elements are also in the rooted source array, which would hide a missing
    // root on the elements but not on the array itself.
    expect_gc("filter's result array survives gc in the callback", R"(
        var src = [{ v: 1 }, { v: 2 }, { v: 3 }];
        var out = src.filter(function (o) { gc(); return o.v !== 2; });
        return out.length + ':' + out[0].v + ',' + out[1].v;
    )",
              "2:1,3");
}

void test_flat_map_result_survives_a_collection() {
    expect_gc("flatMap's result array survives gc in the callback", R"(
        var out = [1, 2].flatMap(function (x) { gc(); return [{ v: x }, { v: x * 10 }]; });
        return out.length + ':' + out[0].v + ',' + out[1].v + ',' + out[2].v + ',' + out[3].v;
    )",
              "4:1,10,2,20");
}

void test_sort_snapshot_survives_a_collection() {
    // `sort` copies the array so a comparator cannot make it index out of
    // bounds. A comparator that empties the array leaves that snapshot holding
    // the only reference to every element not currently an argument, so an
    // unrooted snapshot would be freed under the merge still reading it.
    expect_gc("sort's snapshot survives gc in the comparator", R"(
        var a = [{ k: 3 }, { k: 1 }, { k: 4 }, { k: 2 }];
        a.sort(function (x, y) { a.length = 0; gc(); return x.k - y.k; });
        var out = [];
        for (var i = 0; i < a.length; i++) { out.push(a[i].k); }
        return out.join(',');
    )",
              "1,2,3,4");
    // The comparator itself is a C++ local too, and the only reference to it
    // once the expression that produced it has been overwritten.
    expect_gc("sort's comparator survives gc in itself", R"(
        var a = [{ k: 2 }, { k: 3 }, { k: 1 }];
        var by = function (x, y) { hold = null; gc(); return x.k - y.k; };
        var hold = by;
        by = null;
        a.sort(hold);
        var out = [];
        for (var i = 0; i < a.length; i++) { out.push(a[i].k); }
        return out.join(',');
    )",
              "1,2,3");
    // The default sort runs user code too. It has no comparator, but it calls
    // `to_string` on every element to build its keys, and `toString` is a page
    // method, so the same window is open on the path nobody passes a function
    // to. That code can also push to the array being sorted.
    expect_gc("the default sort survives a mutating toString", R"(
        var a = [];
        a.push({ toString: function () { a.push({ toString: function () { return 'z'; } });
                                         gc(); return 'b'; } });
        a.push({ toString: function () { gc(); return 'a'; } });
        a.sort();
        return String(a.length >= 2);
    )",
              "true");
}

// --- the mark phase --------------------------------------------------------

/// Marking must not recurse once per edge, or the C++ stack has to be as deep
/// as the object graph's longest chain; `mark_object` uses a worklist. A linked
/// list is the ordinary shape that produces a long chain: `head = { next: head }`
/// in a loop is something a page writes on purpose.
///
/// 200,000 is chosen because a recursive marker exhausts the default 8 MiB
/// stack at about 130,000, and crashes (exit 139) rather than giving a wrong
/// answer.
void test_a_deep_chain_does_not_exhaust_the_stack() {
    expect_gc("a 200k-deep chain can be marked", R"(
        var head = null;
        for (var i = 0; i < 200000; i++) { head = { next: head }; }
        gc();
        var n = 0;
        var at = head;
        while (at !== null && n < 200000) { n++; at = at.next; }
        return String(n);
    )",
              "200000");
    // ...and the same for the other two shapes that make a long chain: an
    // array's elements and a closure's captured cell.
    expect_gc("a 200k-deep array chain can be marked", R"(
        var head = null;
        for (var i = 0; i < 200000; i++) { head = [head]; }
        gc();
        var n = 0;
        while (head !== null && n < 200000) { n++; head = head[0]; }
        return String(n);
    )",
              "200000");
}

} // namespace

/// A loop that only concatenates reaches no call and so no safepoint, so the
/// loop's back edge must collect once the string budget is spent. Without it,
/// a 100,000-character tag name built with `+=` (WPT document-write/032.html)
/// left 300 MB of dead strings and hit the 4 GB address-space cap.
void test_a_concatenating_loop_collects_its_strings() {
    const program prog =
        ctbrowser::script::compiler::compile("var big = 'x'.repeat(1 << 20); var t;"
                                             "for (var i = 0; i < 300; ++i) { t = big + i; }"
                                             "return t.length;");
    context cx;
    ctbrowser::script::install_builtins(cx);
    const run_result r = cx.run(prog);
    CHECK(r.ok);
    if (r.ok) { CHECK_EQ(cx.to_string(r.returned), std::string{"1048579"}); }
    CHECK(cx.collections() > 0);
}

// Runs one case at a time from the command line: `ctbrowser-test-script_gc
// bind` runs the cases whose name contains "bind". ASan does not recover: the
// first heap-use-after-free ends the process, so under the asan preset each
// case has to be run on its own to be seen at all. ctest runs every case with
// no argument.
int main(int argc, char ** argv) {
    namespace po = boost::program_options;
    std::string only;
    po::options_description options{"usage: ctbrowser-test-script_gc [NAME]"};
    options.add_options()("only", po::value(&only), "run only the cases whose name contains NAME");
    po::positional_options_description positional;
    positional.add("only", 1);
    if (!ctbrowser_cli::parse_options(argc, argv, options, positional)) { return 2; }
    const auto run = [&](std::string_view name, void (*body)()) {
        if (only.empty() || name.find(only) != std::string_view::npos) { body(); }
    };
    run("bind", &test_bind_captures_survive_a_collection);
    run("argument-list", &test_argument_list_survives_a_collection);
    run("await-constructor", &test_await_constructor_survives_a_collection);
    run("constructor-prototype", &test_constructor_prototype_survives_a_collection);
    run("private-brand", &test_private_brand_survives_a_collection);
    run("private-proxy", &test_private_proxy_survives_a_collection);
    run("symbol", &test_symbol_registry_survives_a_collection);
    run("native-accessors", &test_native_accessors_survive_a_collection);
    run("map", &test_map_result_survives_a_collection);
    run("filter", &test_filter_result_survives_a_collection);
    run("flatmap", &test_flat_map_result_survives_a_collection);
    run("sort", &test_sort_snapshot_survives_a_collection);
    run("chain", &test_a_deep_chain_does_not_exhaust_the_stack);
    run("concat", &test_a_concatenating_loop_collects_its_strings);
    REPORT("script_gc");
}
