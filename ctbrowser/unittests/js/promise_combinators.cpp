// The four `Promise` statics this engine did not have: `withResolvers`,
// `allSettled`, `any` and `race`.
//
// `withResolvers` is why the file exists - WPT's
// `css/css-values/urls/referrer-policy/no-referrer/url-import-referrer-policy.html`
// fails on exactly that name, and it is the shape every modern test harness
// uses to hand a promise to one place and its settlement to another. The other
// three were missing beside it and are the same machinery.
//
// EVERY CASE WAITS FOR THE TURN. A combinator's reaction is queued as a
// microtask, exactly as `then`'s is, so what it produced is only readable after
// `run()` has drained the queue - which is what `expect_after_turn` is. A test
// that read the answer immediately would be asserting the bug these were
// written to avoid: a handler that fires the instant it is attached.
//
// AND EVERY ONE OF THEM IS ALSO EXERCISED OVER A PROMISE THAT IS STILL PENDING
// WHEN THE COMBINATOR IS CALLED. `Promise.all` beside them reads `__value` off
// each entry as it walks the array, which answers immediately - and wrongly -
// for an input that has not settled; these go through the same path `then`
// takes, and the `d.resolve(...)` cases below are what proves it.

#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include "js_expect.hpp"

#include <string>
#include <string_view>

namespace {

// Run a program, drain the turn, and report the global `result` - the same
// shape `vm_basics` uses for every promise case it has.
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
    // THEY EXIST, AND THEY ARE FUNCTIONS
    // ================================================================
    js_expect("typeof Promise.withResolvers", "function");
    js_expect("typeof Promise.allSettled", "function");
    js_expect("typeof Promise.any", "function");
    js_expect("typeof Promise.race", "function");

    // ================================================================
    // Promise.withResolvers - 27.2.4.8
    // ================================================================
    // `new Promise(executor)` turned inside out: the same promise and the same
    // two functions, handed back as an object rather than to a callback.
    js_expect("var d = Promise.withResolvers(); typeof d.promise + ',' + typeof d.resolve + ',' + "
              "typeof d.reject",
              "object,function,function");
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "d.promise.then(v => { result = v; }); d.resolve('ok');",
                      "ok");
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "d.promise.catch(e => { result = 'caught ' + e; }); d.reject('no');",
                      "caught no");
    // SETTLED BEFORE ANYTHING IS ATTACHED, which is the other order a page
    // writes and the one a settled-only promise gets wrong.
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "d.resolve('early'); d.promise.then(v => { result = v; });",
                      "early");
    // A promise settles ONCE.
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "d.promise.then(v => { result = v; }); d.resolve('first'); d.resolve('two');",
                      "first");
    // The destructuring the name was designed for.
    expect_after_turn("var result = ''; const { promise, resolve } = Promise.withResolvers();"
                      "promise.then(v => { result = v; }); resolve('destructured');",
                      "destructured");

    // ================================================================
    // Promise.allSettled - 27.2.4.2
    // ================================================================
    // It NEVER rejects: every outcome is reported, in input order.
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
    // ...AND IT WAITS. The input is pending when allSettled is called.
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "Promise.allSettled([d.promise, 2]).then(r => { result = r[0].value; });"
                      "d.resolve('late');",
                      "late");
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "Promise.allSettled([d.promise]).then(r => { result = r[0].status; });"
                      "d.reject('nope');",
                      "rejected");

    // ================================================================
    // Promise.any - 27.2.4.3
    // ================================================================
    // The first FULFILMENT wins, and a rejection before it is not an answer.
    expect_after_turn("var result = ''; Promise.any([Promise.reject(1), Promise.resolve(2)])"
                      ".then(v => { result = 'got ' + v; });",
                      "got 2");
    // Every input rejecting is the only way `any` rejects, and what it rejects
    // with is an AggregateError carrying `errors` in input order. This engine
    // has no AggregateError CONSTRUCTOR (docs/test262.md names it as absent),
    // so the error is an Error with that name - which is what a page reads.
    expect_after_turn("var result = ''; Promise.any([Promise.reject('a'), Promise.reject('b')])"
                      ".catch(e => { result = e.name + ':' + e.errors.join(','); });",
                      "AggregateError:a,b");
    // An empty list is already "all rejected".
    expect_after_turn("var result = ''; Promise.any([]).catch(e => { result = e.name; });",
                      "AggregateError");
    // ...AND IT WAITS.
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "Promise.any([Promise.reject('r'), d.promise]).then(v => { result = v; });"
                      "d.resolve('won');",
                      "won");

    // ================================================================
    // Promise.race - 27.2.4.5
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
    // ...and one that settles later still wins when it is the only entry.
    expect_after_turn("var result = ''; var d = Promise.withResolvers();"
                      "Promise.race([d.promise]).then(v => { result = v; }); d.resolve('alone');",
                      "alone");
    // AN EMPTY RACE STAYS PENDING FOREVER, which is the specified answer and
    // not an oversight - so `result` is never written.
    expect_after_turn("var result = 'never'; Promise.race([]).then(v => { result = 'settled'; });",
                      "never");

    // ================================================================
    // AND THE ONES THAT WERE ALREADY THERE STILL ANSWER
    // ================================================================
    expect_after_turn("var result = ''; Promise.all([Promise.resolve(1), Promise.resolve(2)])"
                      ".then(v => { result = v.join(','); });",
                      "1,2");
    expect_after_turn("var result = ''; Promise.resolve('r').then(v => { result = v; });", "r");
    expect_after_turn("var result = ''; Promise.reject('j').catch(e => { result = e; });", "j");

    REPORT("promise_combinators");
}
