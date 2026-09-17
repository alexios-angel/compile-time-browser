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
// `yield*` DELEGATES (14.4.14): every inner value is yielded by the outer
// generator, the expression's value is what the inner one returned, `next(v)`
// is forwarded, a sync `.next()` answers the inner RESULT OBJECT itself, and
// `.throw()`/`.return()` while delegating reach the inner iterator's own
// `throw`/`return` before anything else happens. The parser used to eat the
// star and yield the operand.
void test_yield_delegation() {
    expect_result("function* inner() { yield 1; yield 2; return 'r'; }"
                  "function* outer() { const got = yield* inner(); yield got; }"
                  "return [...outer()].join(',');",
                  "1,2,r");
    expect_result("function* inner() { const a = yield 'first'; yield a + 1; }"
                  "function* outer() { yield* inner(); }"
                  "const it = outer(); it.next(); return it.next(41).value;",
                  "42");
    // The inner result object comes out as it is.
    expect_result("const marker = { value: 5, done: false, extra: true };"
                  "const inner = { [Symbol.iterator]() { return { next() { return marker; } }; } };"
                  "function* outer() { yield* inner; }"
                  "return outer().next() === marker;",
                  "true");
    // Anything iterable, including a string and an array.
    expect_result("function* g() { yield* 'ab'; yield* [3]; } return [...g()].join('');", "ab3");
    // `.throw(e)` goes to inner.throw; without one the inner is closed and a
    // TypeError lands at the yield.
    expect_result("var seen = ''; var closed = 0;"
                  "const inner = { [Symbol.iterator]() { return {"
                  "  next() { return { value: 1, done: false }; },"
                  "  throw(e) { seen = e; return { value: 'handled', done: false }; } }; } };"
                  "function* outer() { yield* inner; }"
                  "const it = outer(); it.next(); const r = it.throw('boom');"
                  "return seen + ',' + r.value + ',' + r.done;",
                  "boom,handled,false");
    expect_result(
        "var closed = 0;"
        "const inner = { [Symbol.iterator]() { return {"
        "  next() { return { value: 1, done: false }; }, return() { closed++; return {}; } }; } };"
        "function* outer() { yield* inner; }"
        "const it = outer(); it.next();"
        "try { it.throw('boom'); return 'no'; } catch (e) { return e.constructor.name + closed; }",
        "TypeError1");
    // `.return(v)` goes to inner.return; its done result finishes the outer.
    expect_result("const inner = { [Symbol.iterator]() { return {"
                  "  next() { return { value: 1, done: false }; },"
                  "  return(v) { return { value: v + '!', done: true }; } }; } };"
                  "function* outer() { yield* inner; yield 'after'; }"
                  "const it = outer(); it.next(); const r = it.return('bye');"
                  "return r.value + ',' + r.done + ',' + it.next().done;",
                  "bye!,true,true");
    // A delegate that finishes through `.throw()` lets the body carry on.
    expect_result("const inner = { [Symbol.iterator]() { return {"
                  "  next() { return { value: 1, done: false }; },"
                  "  throw(e) { return { value: 'end', done: true }; } }; } };"
                  "function* outer() { const v = yield* inner; yield 'got ' + v; }"
                  "const it = outer(); it.next(); return it.throw('x').value;",
                  "got end");
    // Not iterable: TypeError from the yield* itself.
    expect_result("function* g() { yield* 5; }"
                  "try { g().next(); return 'no'; } catch (e) { return e.constructor.name; }",
                  "TypeError");
}

// `.return(v)` AT A YIELD RUNS THE FINALLY BLOCKS ON THE WAY OUT (27.5.3.4):
// the return completion travels through the body, a catch clause does not
// see it, a `yield` inside a finally suspends again, and a finally that
// returns overrides the value. It used to finish the generator on the spot.
void test_generator_return_runs_finally() {
    expect_result("var log = []; function* g() { try { yield 1; } finally { log.push('f'); } }"
                  "const it = g(); it.next(); const r = it.return(9);"
                  "return log.join('') + ',' + r.value + ',' + r.done + ',' + it.next().done;",
                  "f,9,true,true");
    expect_result("var caught = 0; function* g() { try { yield 1; } catch (e) { caught++; } }"
                  "const it = g(); it.next(); const r = it.return(2);"
                  "return caught + ',' + r.value + ',' + r.done;",
                  "0,2,true");
    expect_result("function* g() { try { yield 1; } finally { yield 'cleanup'; } }"
                  "const it = g(); it.next(); const a = it.return(3); const b = it.next();"
                  "return a.value + ',' + a.done + ',' + b.value + ',' + b.done;",
                  "cleanup,false,3,true");
    expect_result(
        "function* g() { try { yield 1; } finally { return 'override'; } }"
        "const it = g(); it.next(); const r = it.return(3); return r.value + ',' + r.done;",
        "override,true");
    // Nested try/finally: both run, innermost first.
    expect_result(
        "var log = []; function* g() { try { try { yield 1; } finally { log.push('in'); } }"
        " finally { log.push('out'); } }"
        "const it = g(); it.next(); it.return(); return log.join(',');",
        "in,out");
    // A throw from a finally during return is the throw, not the return.
    expect_result("function* g() { try { yield 1; } finally { throw new Error('fin'); } }"
                  "const it = g(); it.next(); try { it.return(3); return 'no'; }"
                  " catch (e) { return e.message + ',' + it.next().done; }",
                  "fin,true");
    // Before the first next() nothing runs; after the end nothing runs either.
    expect_result(
        "var ran = 0; function* g() { try { ran++; yield 1; } finally { ran += 10; } }"
        "const it = g(); const r = it.return(5); return ran + ',' + r.value + ',' + r.done;",
        "0,5,true");
}

void test_generators() {
    // 27.3 / 27.4 / 27.7: the three function-kind intrinsics. A `function*`'s
    // [[Prototype]] is %GeneratorFunction.prototype%, whose `prototype` is
    // %GeneratorPrototype%; the function's own `prototype` inherits that and
    // has no constructor; its instances inherit the function's prototype;
    // an async function has no `prototype` at all; each constructor makes a
    // function of its kind from source.
    expect_result("function* g() {} const GF = Object.getPrototypeOf(g);"
                  " return [GF === g.constructor.prototype, GF.prototype === "
                  "Object.getPrototypeOf(g.prototype), g.prototype.hasOwnProperty('constructor'),"
                  " Object.getPrototypeOf(g()) === g.prototype, GF[Symbol.toStringTag],"
                  " Object.getPrototypeOf(GF) === Function.prototype,"
                  " Object.getPrototypeOf(g.constructor) === Function].join();",
                  "true,true,false,true,GeneratorFunction,true,true");
    expect_result("const GF = (function* () {}).constructor; const g = GF('a', 'yield a * 2;');"
                  " return g(21).next().value + ',' + g.length + ',' + GF.name + ',' + GF.length;",
                  "42,1,GeneratorFunction,1");
    expect_result(
        "const AF = (async function () {}).constructor; const f = async () => 1;"
        " return [AF.name, Object.getPrototypeOf(f) === AF.prototype,"
        " 'prototype' in (async function () {}), f.hasOwnProperty('prototype'),"
        " AF.prototype[Symbol.toStringTag], typeof AF('a', 'await 1; return a;')].join();",
        "AsyncFunction,true,false,false,AsyncFunction,function");
    expect_result("const AGF = (async function* () {}).constructor; async function* ag() {}"
                  " return [AGF.name, Object.getPrototypeOf(ag) === AGF.prototype,"
                  " Object.getPrototypeOf(ag()) === ag.prototype,"
                  " Object.getPrototypeOf(ag.prototype) === AGF.prototype.prototype,"
                  " AGF.prototype.prototype[Symbol.toStringTag]].join();",
                  "AsyncGeneratorFunction,true,true,true,AsyncGenerator");
    expect_result("function f() {} return [Object.getPrototypeOf(f) === Function.prototype,"
                  " f.prototype.constructor === f].join();",
                  "true,true");
    // GeneratorValidate: a receiver that is not a generator is a TypeError;
    // the instance's prototype is read AFTER the parameters ran (27.5.3.x
    // EvaluateBody: FunctionDeclarationInstantiation first).
    expect_result("const next = Object.getPrototypeOf(function* () {}).prototype.next;"
                  " try { next.call({}); return 'no'; } catch (e) { return e.name; }",
                  "TypeError");
    expect_result("function* g(a = (g.prototype = null)) {} const old = g.prototype;"
                  " return Object.getPrototypeOf(g()) !== old;",
                  "true");
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
    // `Promise.all` HANDS BACK A PENDING PROMISE even when every input is
    // already settled - 27.2.4.1.2 step 4.i attaches with `then`, and a
    // reaction is a job - so this await suspends and its answer is only there
    // once the queue has drained. It read `1,2` from a `return` while `all`
    // settled synchronously, which is the thing that was wrong.
    expect_after_turn("var result = ''; async function f() {"
                      "  const p = await Promise.all([Promise.resolve(1), 2]);"
                      "  result = p.join(','); } f();",
                      "1,2");
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

// AN ASYNC FUNCTION NEVER THROWS AT ITS CALLER (27.7.5.2): a throw its body
// does not catch rejects the promise it returned - before a suspension, after
// one, and from an arrow. Before 2026-09-12 the first threw synchronously out
// of `f()` and the second was an uncaught engine fault.
void test_async_rejection() {
    expect_after_turn(
        "var result = ''; async function f() { throw new Error('boom'); }"
        "let p; try { p = f(); result = typeof p.then; } catch (e) { result = 'threw'; }"
        "p.catch(e => { result += ' ' + e.message; });",
        "function boom");
    expect_after_turn(
        "var result = '';"
        "async function f() { await new Promise(r => Promise.resolve().then(r)); "
        "  throw new TypeError('later'); }"
        "f().then(() => { result = 'fulfilled'; }, e => { result = e.name + ' ' + e.message; });",
        "TypeError later");
    expect_after_turn("var result = ''; const f = async () => { throw new TypeError('t'); };"
                      "f().catch(e => { result = e instanceof TypeError; });",
                      "true");
    // A throw INSIDE a try the body catches is not the fence's business.
    expect_after_turn(
        "var result = ''; async function f() { try { throw 1; } catch (e) { return e + 1; } }"
        "f().then(v => { result = v; });",
        "2");
    // And `return` inside a try still goes through its finally.
    expect_after_turn(
        "var result = ''; async function f() { try { return 'r'; } finally { result += 'f'; } }"
        "f().then(v => { result += v; });",
        "fr");
}

// TWO ASYNC FUNCTIONS SUSPENDED AT ONCE. An interpreted callee's frame starts
// inside its caller's window (op::call: base + a + 1), and the suspension cut
// the register stack at the callee's base - every caller register above the
// callee slot went with it, and the NEXT call's resize refilled them with
// undefined. `f(1); g(2);` had g read `v` as undefined (2026-09-17).
void test_concurrent_awaits() {
    expect_after_turn("var result = '';"
                      "async function f(v) { const r = await v; return r + '/' + v; }"
                      "async function g(v) { const r = await v; return r + '/' + v; }"
                      "f(1).then(x => { result += x; }); g(2).then(x => { result += ' ' + x; });",
                      "1/1 2/2");
    expect_after_turn("var result = '';"
                      "class C { static async #a(v) { return await v; }"
                      "  static async b(v) { return await this.#a(v); } }"
                      "class D { static async b(v) { return await v; } }"
                      "C.b(1).then(x => { result += x; }); D.b(2).then(x => { result += x; });",
                      "21"); // D settles first: C.b awaits twice
}

// %AsyncIteratorPrototype%[Symbol.asyncDispose] (27.1.3.2): `return` called
// and awaited, undefined answered; no `return` is undefined at once; a throw
// from the getter or the call is the rejection.
void test_async_dispose() {
    expect_after_turn("var result = ''; async function* g() { try { yield 1; } finally {"
                      " result += 'closed'; } } const it = g();"
                      "it.next().then(() => it[Symbol.asyncDispose]()).then(v => {"
                      " result += ':' + v + ':' + it[Symbol.asyncDispose].name; });",
                      "closed:undefined:[Symbol.asyncDispose]");
    expect_after_turn("var result = ''; const it = { __proto__: Object.getPrototypeOf("
                      "Object.getPrototypeOf(Object.getPrototypeOf((async function* () {})()))),"
                      " return() { throw new RangeError('r'); } };"
                      "it[Symbol.asyncDispose]().catch(e => { result = e.name; });",
                      "RangeError");
}

// `async function*`: every request is a promise of the record, queued behind
// the body; `yield` awaits its operand; a throw rejects the request; the body
// may park on an `await` between two requests.
void test_async_generators() {
    expect_after_turn("var result = ''; async function* g() { yield 1; yield 2; }"
                      "const it = g(); result = typeof it.next().then;",
                      "function");
    expect_after_turn("var result = ''; async function* g() { yield 1; yield 2; }"
                      "const it = g();"
                      "it.next().then(r => { result += r.value + ':' + r.done + ' '; });"
                      "it.next().then(r => { result += r.value + ':' + r.done + ' '; });"
                      "it.next().then(r => { result += r.value + ':' + r.done; });",
                      "1:false 2:false undefined:true");
    // `yield` awaits: a promise's value comes out, and the body sees `.next(v)`'s v.
    expect_after_turn(
        "var result = ''; async function* g() { const got = yield Promise.resolve(5); "
        "  yield got * 2; }"
        "const it = g(); it.next().then(r => { result += r.value; return it.next(7); })"
        "  .then(r => { result += ',' + r.value; });",
        "5,14");
    // An await between requests: the second `.next()` waits for the first.
    expect_after_turn(
        "var result = ''; let go;"
        "async function* g() { await new Promise(r => { go = r; }); yield 'a'; yield 'b'; }"
        "const it = g();"
        "it.next().then(r => { result += r.value; });"
        "it.next().then(r => { result += r.value; });"
        "Promise.resolve().then(() => { result += '|'; go(); });",
        "|ab");
    // A throw rejects the request, and the generator is done afterwards.
    expect_after_turn(
        "var result = ''; async function* g() { yield 1; throw new Error('x'); }"
        "const it = g(); it.next().then(() => it.next()).then(() => { result = 'no'; },"
        "  e => { result = e.message; return it.next(); }).then(r => { result += r.done; });",
        "xtrue");
    // `.throw()` at a yield lands in the body's catch; `.return()` finishes.
    expect_after_turn(
        "var result = ''; async function* g() { try { yield 1; } catch (e) { yield 'c' + e; } }"
        "const it = g(); it.next().then(() => it.throw('!')).then(r => { result = r.value; });",
        "c!");
    expect_after_turn("var result = ''; async function* g() { yield 1; yield 2; }"
                      "const it = g(); it.next().then(() => it.return('r')).then(r => { result = "
                      "r.value + r.done; });",
                      "rtrue");
    expect_after_turn("var result = ''; async function* g() {}"
                      "result = typeof g()[Symbol.asyncIterator];",
                      "function");
    // `.next` on something that is not an async generator rejects.
    expect_after_turn(
        "var result = ''; async function* g() {}"
        "g().next.call({}).then(() => { result = 'no'; }, e => { result = e.name; });",
        "TypeError");
}

// `for await (x of y)`: an async generator pulled lazily, a sync iterable
// whose values are promises, `break`, and a rejection reaching the body's
// caller.
void test_for_await() {
    expect_after_turn(
        "var result = ''; async function* g() { yield 1; await null; yield 2; yield 3; }"
        "(async () => { for await (const v of g()) { result += v; } result += '.'; })();",
        "123.");
    expect_after_turn(
        "var result = '';"
        "(async () => { for await (const v of [Promise.resolve('a'), 'b']) { result += v; } })();",
        "ab");
    expect_after_turn(
        "var result = ''; async function* g() { yield 1; yield 2; yield 3; }"
        "(async () => { for await (const v of g()) { if (v === 2) { break; } result += v; } "
        "  result += '|'; })();",
        "1|");
    expect_after_turn("var result = ''; async function* g() { yield 1; throw new Error('bad'); }"
                      "(async () => { try { for await (const v of g()) { result += v; } } "
                      "  catch (e) { result += e.message; } })();",
                      "1bad");
    // AsyncIteratorClose: `break` and a throw out of the body call the
    // iterator's return(); a normal end and a throw from next() do not.
    expect_after_turn("var result = ''; var it = { i: 0, next() { return Promise.resolve({value: "
                      "this.i++, done: this.i > 5}); },"
                      "  return() { result += 'R'; return Promise.resolve({done: true}); }, "
                      "[Symbol.asyncIterator]() { return this; } };"
                      "(async () => { for await (const v of it) { result += v; if (v === 1) { "
                      "break; } } result += '.'; })();",
                      "01R.");
    expect_after_turn("var result = ''; var it = { i: 0, next() { return Promise.resolve({value: "
                      "this.i++, done: this.i > 2}); },"
                      "  return() { result += 'R'; return Promise.resolve({done: true}); }, "
                      "[Symbol.asyncIterator]() { return this; } };"
                      "(async () => { try { for await (const v of it) { result += v; throw new "
                      "Error('t'); } } catch (e) { result += e.message; } })();",
                      "0Rt");
    expect_after_turn(
        "var result = ''; var it = { i: 0, next() { return Promise.resolve({value: this.i++, done: "
        "this.i > 2}); },"
        "  return() { result += 'R'; return Promise.resolve({done: true}); }, "
        "[Symbol.asyncIterator]() { return this; } };"
        "(async () => { for await (const v of it) { result += v; } result += '.'; })();",
        "01.");
    expect_after_turn("var result = ''; var x;"
                      "(async () => { for await (x of [1, 2]) { result += x; } })();",
                      "12");
    // GetIterator(async), 7.4.3: a [Symbol.asyncIterator] that is present but
    // not callable is the TypeError; [Symbol.iterator] is asked only when it
    // is absent - so the sync getter here is never read.
    expect_after_turn("var result = ''; var it = { get [Symbol.iterator]() { result += 'sync'; }, "
                      "[Symbol.asyncIterator]: false };"
                      "(async () => { try { for await (const v of it) {} } catch (e) { result += "
                      "e.constructor.name; } })();",
                      "TypeError");
    // Outside an async function it is refused at compile time.
    CHECK(!compiler::compile("function f() { for await (const v of []) {} }").ok);

    // Array.fromAsync, built on it: a sync iterable of promises, an async
    // generator, an array-like, and a mapper whose result is awaited.
    expect_after_turn("var result = ''; Array.fromAsync([Promise.resolve(1), 2]).then(a => { "
                      "result = a.join(); });",
                      "1,2");
    expect_after_turn("var result = ''; async function* g() { yield 'a'; yield 'b'; }"
                      "Array.fromAsync(g()).then(a => { result = a.join(); });",
                      "a,b");
    expect_after_turn(
        "var result = ''; Array.fromAsync({length: 2, 0: 'x', 1: Promise.resolve('y')})"
        "  .then(a => { result = a.join(); });",
        "x,y");
    expect_after_turn("var result = ''; Array.fromAsync([1, 2], async (v, i) => v * 10 + i)"
                      "  .then(a => { result = a.join(); });",
                      "10,21");
    expect_after_turn("var result = ''; Array.fromAsync(null).then(() => { result = 'no'; }, e => "
                      "{ result = e.name; });",
                      "TypeError");
    // GetMethod: a present, non-callable @@iterator is a TypeError; a BigInt
    // length is ToLength's TypeError; a thenable element that rejects rejects
    // the whole thing (the await adopts it).
    expect_after_turn("var result = ''; Array.fromAsync({[Symbol.iterator]: true}).then(() => {"
                      " result = 'no'; }, e => { result = e.name; });",
                      "TypeError");
    expect_after_turn("var result = ''; Array.fromAsync({length: 1n, 0: 0}).then(() => {"
                      " result = 'no'; }, e => { result = e.name; });",
                      "TypeError");
    expect_after_turn("var result = ''; Array.fromAsync({length: 1, 0: { then(_, rej) {"
                      " rej(new RangeError('r')); } }}).then(() => { result = 'no'; }, e => {"
                      " result = e.name; });",
                      "RangeError");
    // A constructor as `this` takes the elements through defineProperty: a
    // non-configurable slot is a TypeError, not an endless loop (test262
    // this-constructor-with-unsettable-element ran the box out of memory).
    expect_after_turn(
        "var result = ''; function M() { Object.defineProperty(this, 0, {value: 0,"
        "  writable: true, configurable: false}); }"
        "var it = { next() { return Promise.resolve({value: 1, done: false}); },"
        "  [Symbol.asyncIterator]() { return this; } };"
        "Array.fromAsync.call(M, it).then(() => { result = 'no'; }, e => { result = e.name; });",
        "TypeError");
    expect_after_turn(
        "var result = ''; Array.fromAsync({length: 2 ** 40}).then(() => { result = 'no'; },"
        "  e => { result = e.name; });",
        "RangeError");
}

} // namespace

int main() {
    test_pending_promises();
    test_generators();
    test_yield_delegation();
    test_generator_return_runs_finally();
    test_async_and_promises();
    test_promise_handlers_are_microtasks();
    test_async_rejection();
    test_concurrent_awaits();
    test_async_dispose();
    test_async_generators();
    test_for_await();
    REPORT("vm_async");
}
