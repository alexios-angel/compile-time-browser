//===- ctbrowser/unittests/js/promise/combinators.cpp - Combinators -------===//
//
// Promise combinators: `withResolvers`, `allSettled`, `any`, `race` and `all`.
//
// Reactions run as microtasks, so every case reads its answer only after
// `run()` drains the queue (`expect_after_turn`). Each combinator is also
// tested with an input that is still pending when it is called, because all
// of them must go through the same `then` path whether an input is settled,
// pending or not a promise.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include "js_expect.hpp"

#include <string>
#include <string_view>

namespace {

/// Runs \p source, drains the microtask queue, and fails the test unless the
/// global `result` then reads \p want. This is the shape `vm_async` uses for
/// its promise cases (../vm/expect.hpp).
void expect_after_turn(std::string_view source, std::string_view want) {
    using namespace ctbrowser::script;
    const program prog = compiler::compile(std::string{source});
    context cx;
    install_builtins(cx);
    const run_result r = cx.run(prog);
    const std::string got = r.ok ? cx.to_string(cx.global("result")) : "<error: " + r.error + ">";
    if (got != want) {
        std::printf("FAIL     %.90s\n  -> %s (want %s)\n", std::string{source}.c_str(), got.c_str(),
                    std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}

} // namespace

int main() {
    // ================================================================
    // They exist, and they are functions.
    // ================================================================
    js_expect("typeof Promise.withResolvers", "function");
    js_expect("typeof Promise.allSettled", "function");
    js_expect("typeof Promise.any", "function");
    js_expect("typeof Promise.race", "function");

    // ================================================================
    // Promise.withResolvers: ECMA-262 27.2.4.8.
    // ================================================================
    // `new Promise(executor)` turned inside out: the same promise and the same
    // two functions, handed back as an object rather than to a callback.
    // `js_expect` compiles `return (<source>);`, so statements go inside an IIFE.
    js_expect("(function(){var d = Promise.withResolvers();"
              "return typeof d.promise + ',' + typeof d.resolve + ',' + typeof d.reject;})()",
              "object,function,function");
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "d.promise.then(v => { result = v; }); d.resolve('ok');",
                      "ok");
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "d.promise.catch(e => { result = 'caught ' + e; }); d.reject('no');",
                      "caught no");
    // Settled before anything is attached, which is the other order a page
    // writes and the one a settled-only promise gets wrong.
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "d.resolve('early'); d.promise.then(v => { result = v; });",
                      "early");
    // A promise settles once.
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "d.promise.then(v => { result = v; }); d.resolve('first'); d.resolve('two');",
                      "first");
    // The destructuring the name was designed for.
    expect_after_turn("var result = ''; const { promise, resolve } = Promise.withResolvers();"
                      "promise.then(v => { result = v; }); resolve('destructured');",
                      "destructured");

    // ================================================================
    // Promise.allSettled: ECMA-262 27.2.4.2.
    // ================================================================
    // It never rejects: every outcome is reported, in input order.
    expect_after_turn(
        "var result = ''; Promise.allSettled([Promise.resolve(1), Promise.reject('e')])"
        ".then(r => { result = r.map(x => x.status).join(','); });",
        "fulfilled,rejected");
    expect_after_turn(
        "var result = ''; Promise.allSettled([Promise.resolve(1), Promise.reject('e')])"
        ".then(r => { result = r[0].value + '|' + r[1].reason; });",
        "1|e");
    // A non-promise input is fulfilled with itself.
    expect_after_turn("var result = ''; Promise.allSettled([1, 2])"
                      ".then(r => { result = r.length + ':' + r[0].value + r[1].value; });",
                      "2:12");
    // An empty list resolves at once, with an empty array.
    expect_after_turn("var result = 'x'; Promise.allSettled([])"
                      ".then(r => { result = 'len=' + r.length; });",
                      "len=0");
    // It waits: the input is pending when allSettled is called.
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "Promise.allSettled([d.promise, 2]).then(r => { result = r[0].value; });"
                      "d.resolve('late');",
                      "late");
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "Promise.allSettled([d.promise]).then(r => { result = r[0].status; });"
                      "d.reject('nope');",
                      "rejected");

    // ================================================================
    // Promise.any: ECMA-262 27.2.4.3.
    // ================================================================
    // The first fulfilment wins, and a rejection before it is not an answer.
    expect_after_turn("var result = ''; Promise.any([Promise.reject(1), Promise.resolve(2)])"
                      ".then(v => { result = 'got ' + v; });",
                      "got 2");
    // Every input rejecting is the only way `any` rejects, and it rejects with an
    // AggregateError whose `errors` are in input order.
    expect_after_turn("var result = ''; Promise.any([Promise.reject('a'), Promise.reject('b')])"
                      ".catch(e => { result = e.name + ':' + e.errors.join(','); });",
                      "AggregateError:a,b");
    // An empty list is already "all rejected".
    expect_after_turn("var result = ''; Promise.any([]).catch(e => { result = e.name; });",
                      "AggregateError");
    // It waits for a pending input.
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "Promise.any([Promise.reject('r'), d.promise]).then(v => { result = v; });"
                      "d.resolve('won');",
                      "won");

    // ================================================================
    // Promise.race: ECMA-262 27.2.4.5.
    // ================================================================
    // Whichever settles first settles the result, however it settled.
    expect_after_turn("var result = ''; Promise.race([Promise.resolve('a'), Promise.reject('b')])"
                      ".then(v => { result = v; }, e => { result = 'rejected ' + e; });",
                      "a");
    expect_after_turn("var result = ''; Promise.race([Promise.reject('x'), Promise.resolve('y')])"
                      ".then(v => { result = v; }, e => { result = 'rejected ' + e; });",
                      "rejected x");
    // A pending entry does not hold up a settled one.
    expect_after_turn(
        "var result = ''; var d = Promise.withResolvers();"
        "Promise.race([d.promise, Promise.resolve('now')]).then(v => { result = v; });",
        "now");
    // One that settles later still wins when it is the only entry.
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "Promise.race([d.promise]).then(v => { result = v; }); d.resolve('alone');",
                      "alone");
    // An empty race stays pending forever, which is the specified answer and
    // not an oversight, so `result` is never written.
    expect_after_turn("var result = 'never'; Promise.race([]).then(v => { result = 'settled'; });",
                      "never");

    // ================================================================
    // Promise.all: ECMA-262 27.2.4.1.
    // ================================================================
    // The values in input order, whatever order they arrived in.
    expect_after_turn("var result = ''; Promise.all([Promise.resolve(1), Promise.resolve(2)])"
                      ".then(v => { result = v.join(','); });",
                      "1,2");
    // A non-promise input is its own value.
    expect_after_turn("var result = ''; Promise.all([1, Promise.resolve(2), 3])"
                      ".then(v => { result = v.join(','); });",
                      "1,2,3");
    // One rejection settles the whole thing, and the values beside it are lost.
    expect_after_turn(
        "var result = ''; Promise.all([Promise.resolve(1), Promise.reject('e')])"
        ".then(v => { result = 'resolved ' + v; }, e => { result = 'rejected ' + e; });",
        "rejected e");
    // An empty list resolves at once, with an empty array.
    expect_after_turn(
        "var result = 'x'; Promise.all([]).then(r => { result = 'len=' + r.length; });", "len=0");
    // The input is still pending when `all` is called; the result is the value
    // that arrives later.
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "Promise.all([d.promise, 2]).then(v => { result = v.join(','); });"
                      "d.resolve('late');",
                      "late,2");
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "Promise.all([d.promise]).then(v => { result = 'resolved'; },"
                      " e => { result = 'rejected ' + e; }); d.reject('no');",
                      "rejected no");
    // Input order, not arrival order: the second entry settles first.
    expect_after_turn("var result = ''; var a = Promise.withResolvers();"
                      "var b = Promise.withResolvers();"
                      "Promise.all([a.promise, b.promise]).then(v => { result = v.join(','); });"
                      "b.resolve('B'); a.resolve('A');",
                      "A,B");
    // An input that never settles keeps `all` pending forever, as the
    // specification requires.
    expect_after_turn("var result = 'never'; var d = Promise.withResolvers();"
                      "Promise.all([d.promise]).then(v => { result = 'settled'; });",
                      "never");

    // ================================================================
    // Promise.resolve and Promise.reject.
    // ================================================================
    expect_after_turn("var result = ''; Promise.resolve('r').then(v => { result = v; });", "r");
    expect_after_turn("var result = ''; Promise.reject('j').catch(e => { result = e; });", "j");

    REPORT("promise_combinators");
}
