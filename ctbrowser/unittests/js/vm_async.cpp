// Pending promises, `await`, the microtask queue, and generators - everything
// that suspends and resumes. Every promise case reads its answer AFTER THE TURN
// through `expect_after_turn`, because a handler runs when the queue drains and
// not when it is attached. Carved out of js/vm_basics.cpp on 2026-09-08 -
// vm_operators.cpp names the family, and vm_expect.hpp is the assertion every
// file in it shares.

#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include "vm_expect.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

using namespace ctbrowser::script;

namespace {

// A PROMISE CAN BE PENDING. It was settled-only - created already resolved,
// `then` running immediately, `new Promise(executor)` absent because an
// executor implies pending state. p5.js opens with
// `Promise.all([waitForDocumentReady(), ...]).then(_globalInit)`, so the
// library could not begin without it.
//
// Every handler here runs at the END OF THE TURN, so these read the result out
// of a global afterwards rather than returning it: a `return` is evaluated
// before the microtask queue drains and cannot see anything a `then` did.
void test_pending_promises() {
    expect_after_turn("var result = 0; new Promise(r => r(5)).then(v => { result = v; });", "5");
    // Pending until something resolves it - and `before` proves the handler had
    // not run at the point of resolution either, which is the queue's whole job.
    expect_after_turn(
        "var result = ''; let go; const p = new Promise(r => { go = r; });"
        "let seen = 0; p.then(v => { seen = v; });"
        "const before = seen; go(7);"
        "const atResolve = seen;"
        "Promise.resolve().then(() => { result = before + '|' + atResolve + '|' + seen; });",
        "0|0|7");
    // reject reaches catch, not then
    expect_after_turn("var result = ''; let ok = '', bad = '';"
                      "new Promise((r, j) => j('no')).then(v => { ok = v; }, e => { bad = e; })"
                      "  .finally(() => { result = ok + '|' + bad; });",
                      "|no");
    expect_after_turn("var result = ''; new Promise((r, j) => j('x')).catch(e => { result = e; });",
                      "x");
    // a rejection passes THROUGH a bare then to a later catch
    expect_after_turn("var result = ''; new Promise((r, j) => j('y')).then(v => v)"
                      "  .catch(e => { result = e; });",
                      "y");
    // then CHAINS: the next promise gets what the handler returned
    expect_after_turn("var result = 0; new Promise(r => r(1)).then(v => v + 1)"
                      "  .then(v => { result = v; });",
                      "2");
    // a handler returning a promise is adopted rather than nested
    expect_after_turn("var result = 0; new Promise(r => r(1))"
                      "  .then(v => new Promise(r2 => r2(v + 10))).then(v => { result = v; });",
                      "11");
    // settle once: a second resolve is ignored
    expect_after_turn(
        "var result = 0; new Promise(r => { r(1); r(2); }).then(v => { result = v; });", "1");
    // Promise.all over already-settled promises
    expect_after_turn("var result = ''; Promise.all([Promise.resolve(1), Promise.resolve(2)])"
                      "  .then(v => { result = v.join(','); });",
                      "1,2");
}

// GENERATORS. Landed for Babylon.js, where 622 `function*` bodies are not an
// author writing generators at all - TypeScript compiles every `async` function
// into one driven by an `__awaiter` helper, so `yield` there is what `await`
// became. That helper is the shape these pin.
void test_generators() {
    expect_result("function* g() { yield 1; yield 2; }"
                  "const it = g(); const a = it.next();"
                  "return a.value + ',' + a.done;",
                  "1,false");
    expect_result("function* g() { yield 1; yield 2; }"
                  "const it = g(); it.next(); const b = it.next();"
                  "return b.value + ',' + b.done;",
                  "2,false");
    // Falling off the end is done with no value; a generator that has finished
    // keeps answering rather than running its body again.
    expect_result("function* g() { yield 1; }"
                  "const it = g(); it.next(); const c = it.next(); const d = it.next();"
                  "return c.value + ',' + c.done + ',' + d.done;",
                  "undefined,true,true");
    // `return v` in the body is the value of the DONE record.
    expect_result("function* g() { yield 1; return 9; }"
                  "const it = g(); it.next(); const r = it.next();"
                  "return r.value + ',' + r.done;",
                  "9,true");
    // WHAT `.next(v)` SENDS IN. `var x = yield y` sees it, and this is the half
    // an eager implementation gets wrong by yielding the operand and never
    // resuming.
    expect_result("function* g() { const x = yield 1; yield x * 2; }"
                  "const it = g(); it.next(); return it.next(21).value;",
                  "42");
    // The body runs NOTHING until the first next().
    expect_result("var ran = false;"
                  "function* g() { ran = true; yield 1; }"
                  "const it = g(); const before = ran; it.next();"
                  "return before + ',' + ran;",
                  "false,true");
    // State survives across suspensions - the register window is the point.
    expect_result("function* g() { let n = 0; while (n < 3) { yield n; n++; } }"
                  "const it = g(); return it.next().value + ',' + it.next().value + ','"
                  " + it.next().value + ',' + it.next().done;",
                  "0,1,2,true");
    // Arguments are bound, and parameters survive a suspension.
    expect_result("function* g(a, b) { yield a; yield b; yield a + b; }"
                  "const it = g(3, 4); it.next(); it.next(); return it.next().value;",
                  "7");
    // `.throw()` throws AT THE YIELD, which is how __awaiter turns a rejected
    // promise back into an exception inside the async function's body.
    expect_result("function* g() { try { yield 1; } catch (e) { yield 'caught ' + e; } }"
                  "const it = g(); it.next(); return it.throw('boom').value;",
                  "caught boom");
    // A generator is its own iterable, so for-of and spread work.
    expect_result("function* g() { yield 1; yield 2; yield 3; }"
                  "let sum = 0; for (const v of g()) { sum += v; } return sum;",
                  "6");
    // A generator EXPRESSION, which is the form TypeScript actually emits.
    expect_result("const g = function* () { yield 5; };"
                  "return g().next().value;",
                  "5");
    // A generator method in an object literal, and one in a class.
    expect_result("const o = { *g() { yield 7; } }; return o.g().next().value;", "7");
    expect_result("class C { *g() { yield 8; } } return new C().g().next().value;", "8");
    // Two generators from one function do not share state.
    expect_result("function* g() { yield 1; yield 2; }"
                  "const a = g(), b = g(); a.next();"
                  "return a.next().value + ',' + b.next().value;",
                  "2,1");
    // THE __awaiter SHAPE ITSELF, which is the only reason this exists.
    expect_result("function drive(gen) {"
                  "  const it = gen(); let step = it.next(); let last;"
                  "  while (!step.done) { last = step.value; step = it.next(last * 2); }"
                  "  return step.value;"
                  "}"
                  "return drive(function* () { const a = yield 1; const b = yield a; return b; });",
                  "4");
}

void test_async_and_promises() {
    // `await` on a plain value or an already-settled promise reads it straight
    // out, so these run to completion inside the turn.
    expect_result("return await 3;", "3");
    expect_result("async function f() { return 5; } return await f();", "5");
    // An async function hands back a PROMISE, not the bare value - otherwise
    // `f().then(...)` has nothing to call.
    expect_result("async function f() { return 5; } return typeof f().then;", "function");
    expect_result("async function f() { return 1; } async function g() { return await f() + 1; }"
                  "return await g();",
                  "2");
    // await inside a loop, which is the fetchboard shape.
    expect_result(
        "async function one(n) { return n * 2; }"
        "async function run() { var t = 0; for (const n of [1,2,3]) { t += await one(n); }"
        "  return t; } return await run();",
        "12");
    expect_result("return await Promise.resolve(7);", "7");
    expect_result("var p = await Promise.all([Promise.resolve(1), 2]); return p.join(',');", "1,2");
    // Awaiting a rejected promise THROWS, which is what makes try/catch around
    // an await behave the way pages assume.
    expect_result("var r = 0; try { await Promise.reject(9); } catch (e) { r = e; } return r;",
                  "9");
}

// A HANDLER RUNS AT THE END OF THE TURN, NOT WHEN THE PROMISE SETTLES.
//
// The difference is observable and pages are written against it. It used to run
// on settle, which also let a chain reenter code that was halfway through its
// own work. Each case here is asserted TWICE: `expect_result` says the handler
// has not run yet at the point of return, and `expect_after_turn` says it has
// by the time the queue drains.
void test_promise_handlers_are_microtasks() {
    expect_result("var r = 0; Promise.resolve(2).then(function (v) { r = v * 3; }); return r;",
                  "0");
    expect_after_turn("var result = 0; Promise.resolve(2).then(function (v) { result = v * 3; });",
                      "6");
    // then() chains: each callback's return settles the next promise, and the
    // whole chain completes within one drain rather than one link per turn.
    expect_after_turn("var result = 0; Promise.resolve(2).then(function (v) { return v + 1; })"
                      "  .then(function (v) { result = v; });",
                      "3");
    // A rejected promise skips then and reaches catch.
    expect_after_turn("var result = 'none';"
                      "Promise.reject('bad').then(function () { result = 'ran'; })"
                      "  .catch(function (e) { result = e; });",
                      "bad");
    expect_after_turn("var result = 0; Promise.reject(1).catch(function () { return 9; })"
                      "  .then(function (v) { result = v; });",
                      "9");
    expect_after_turn("var result = 0; Promise.resolve(1).finally(function () { result = 5; });",
                      "5");
    // `finally` RUNS EITHER WAY AND CHANGES NOTHING: no argument, its return
    // value ignored, the outcome passing through. It used to call its callback
    // the moment it was registered and hand back the SAME promise, so it ran
    // before the rejection it was meant to follow.
    expect_after_turn(
        "var result = ''; Promise.resolve('kept').finally(function () { return 'x'; })"
        "  .then(function (v) { result = v; });",
        "kept");
    expect_after_turn("var result = ''; Promise.reject('bad').finally(function () { return 'x'; })"
                      "  .catch(function (e) { result = 'caught:' + e; });",
                      "caught:bad");
    // ...and it follows the handler before it rather than preceding it.
    expect_after_turn("var result = ''; var log = '';"
                      "Promise.reject('no').catch(function (e) { log += 'catch;'; })"
                      "  .finally(function () { log += 'fin;'; result = log; });",
                      "catch;fin;");
    // An async function's own promise is no different.
    expect_after_turn("var result = 0; async function f() { return 5; }"
                      "f().then(function (v) { result = v; });",
                      "5");
    // A promise settled from an executor, synchronously, still defers.
    expect_result("var r = 0; new Promise(function (go) { go(5); })"
                  "  .then(function (v) { r = v; }); return r;",
                  "0");
    expect_after_turn("var result = 0; var go;"
                      "new Promise(function (f) { go = f; }).then(function (v) { result = v; });"
                      "go(7);",
                      "7");
    // THE ORDER ACROSS SEVERAL CHAINS, which is what a queue is for: every
    // first-round handler runs before any second-round one.
    expect_after_turn("var result = '';"
                      "Promise.resolve(1).then(function (v) { result += 'a'; return v; })"
                      "                  .then(function () { result += 'b'; });"
                      "Promise.resolve(9).then(function () { result += 'c'; });"
                      "result += 'sync';",
                      "syncacb");
}

} // namespace

int main() {
    test_pending_promises();
    test_generators();
    test_async_and_promises();
    test_promise_handlers_are_microtasks();
    REPORT("vm_async");
}
