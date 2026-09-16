// Promise as 27.2 specifies it, after the 2026-09-12 rewrite of builtins/async.cpp:
// the resolving functions, the SEPARATE thenable job, species and
// subclassing, the combinators over the iterator protocol, and the ES2026
// globals installed beside it - Iterator helpers, DisposableStack -
// each with one assertion that fails if its algorithm is wrong.
//
// EVERY PROMISE CASE WAITS FOR THE TURN (`expect_after_turn`, vm_expect.hpp):
// a reaction runs as a microtask, never at the moment it is attached.

#include "js_expect.hpp"
#include "vm_expect.hpp"

int main() {
    // ================================================================
    // RESOLUTION, 27.2.1.3.2: a thenable is ADOPTED, on its own tick
    // ================================================================
    // `resolve(thenable)` queues NewPromiseResolveThenableJob: the thenable's
    // `then` runs on the FIRST tick (it was queued before anything else), and
    // the value it resolves with is delivered a tick after 'a' - which is
    // why 'c' lands between 'a' and 'b'.
    expect_after_turn("var result = ''; var log = [];"
                      "var thenable = { then(r) { log.push('then'); r('c'); } };"
                      "new Promise(r => r(thenable)).then(v => log.push(v));"
                      "Promise.resolve().then(() => log.push('a')).then(() => log.push('b'));"
                      "Promise.resolve().then(() => 0).then(() => 0)"
                      ".then(() => { result = log.join(''); });",
                      "thenacb");
    // A handler returning a promise costs the same two extra ticks: the
    // handler on tick 1, the thenable job on 2, its reaction on 3, and the
    // value on 4 - after 1, 2 and 3.
    expect_after_turn("var result = ''; var log = [];"
                      "Promise.resolve().then(() => Promise.resolve('x')).then(v => log.push(v));"
                      "Promise.resolve().then(() => log.push(1)).then(() => log.push(2))"
                      ".then(() => log.push(3)).then(() => { result = log.join(''); });",
                      "123x");
    // 27.7.5.3 Await: PromiseResolve(%Promise%, v) - a thenable that is not
    // a promise is adopted through its `then`, on its own tick, so a thenable
    // that rejects throws at the await, and one that resolves hands its
    // value over; a plain object awaits to itself.
    expect_after_turn("var result = ''; (async () => { try { await { then(_, reject) {"
                      " reject(new RangeError('no')); } }; result = 'no throw'; }"
                      " catch (e) { result = e.name; } })();",
                      "RangeError");
    expect_after_turn(
        "var result = ''; (async () => { result = await { then(r) { r('v'); } }; })();", "v");
    expect_after_turn("var result = ''; var o = { x: 1 }; (async () => { result = (await o) === o;"
                      " })();",
                      "true");
    expect_after_turn(
        "var result = ''; var log = [];"
        "(async () => { await { then(r) { log.push('then'); r(); } }; log.push('after');"
        " })(); Promise.resolve().then(() => log.push('a')).then(() => log.push('b'))"
        ".then(() => 0).then(() => { result = log.join(); });",
        "then,a,after,b");
    // Resolving a promise with itself is a TypeError rejection.
    expect_after_turn("var result = ''; var p = Promise.resolve().then(() => p);"
                      "p.catch(e => { result = e.constructor.name; });",
                      "TypeError");
    // The executor's resolve wins over its later throw.
    expect_after_turn("var result = ''; new Promise((r) => { r(1); throw 2; })"
                      ".then(v => { result = 'ok' + v; }, e => { result = 'no' + e; });",
                      "ok1");
    // A `then` getter that throws rejects with what it threw.
    expect_after_turn("var result = ''; Promise.resolve({ get then() { throw 'boom'; } })"
                      ".catch(e => { result = e; });",
                      "boom");

    // ================================================================
    // THE COMBINATORS TAKE AN ITERABLE, and read `resolve` once
    // ================================================================
    expect_after_turn("var result = ''; Promise.all(new Set([1, Promise.resolve(2)]))"
                      ".then(v => { result = v.join(','); });",
                      "1,2");
    expect_after_turn(
        "var result = ''; Promise.all(5).catch(e => { result = e.constructor.name; });",
        "TypeError");
    expect_after_turn(
        "var result = ''; var closed = false;"
        "var it = { [Symbol.iterator]() { return { next() { return {value: 1, done: false}; },"
        " return() { closed = true; return {}; } }; } };"
        "Promise.resolve = undefined;"
        "Promise.all(it).catch(e => { result = e.constructor.name + closed; });",
        "TypeErrorfalse");
    expect_after_turn("var result = ''; var n = 0;"
                      "var P = Object.assign(function(e) { return new Promise(e); },"
                      " { get resolve() { n++; return Promise.resolve; } });"
                      "Promise.all.call(P, [1, 2, 3]).then(() => { result = n; });",
                      "1");
    expect_after_turn("var result = ''; Promise.any([Promise.reject(1), Promise.reject(2)])"
                      ".catch(e => { result = e.name + ':' + e.errors.join(','); });",
                      "AggregateError:1,2");
    expect_after_turn("var result = ''; Promise.race([new Promise(() => {}), Promise.resolve('r')])"
                      ".then(v => { result = v; });",
                      "r");

    // ================================================================
    // SPECIES AND SUBCLASSING
    // ================================================================
    // `Object.setPrototypeOf(P, Promise)` is what `extends` is specified to
    // do for the CONSTRUCTOR (15.7.14 step 8.d) and what compile/classes.cpp
    // does not yet: it links only the prototypes, so a subclass inherits no
    // statics - no `P.resolve`, no `P[Symbol.species]` - without it.
    expect_after_turn("var result = ''; class P extends Promise {}"
                      "Object.setPrototypeOf(P, Promise);"
                      "var p = new P(r => r(1));"
                      "result = (p instanceof P) + ',' + (p.then(() => 0) instanceof P) + ','"
                      " + (P.resolve(2) instanceof P) + ',' + (p.finally(() => 0) instanceof P);",
                      "true,true,true,true");
    expect_after_turn("var result = ''; var p = Promise.resolve(1);"
                      "p.constructor = { [Symbol.species]: function(e) { result = 'species'; "
                      "return new Promise(e); } }; p.then(() => 0);",
                      "species");
    expect_after_turn("var result = ''; Promise.try(() => { throw 'x'; })"
                      ".catch(e => { result = e; });",
                      "x");
    expect_after_turn("var result = ''; Promise.reject('r').finally(() => 'ignored')"
                      ".catch(e => { result = e; });",
                      "r");
    js_expect("Object.getOwnPropertyDescriptor(Promise, Symbol.species).get.call(1)", "1");
    js_expect("Promise.length + Promise.prototype.then.length + Promise.all.length", "4");

    // ================================================================
    // async functions still adopt their return
    // ================================================================
    expect_after_turn("var result = ''; (async () => { return await Promise.resolve('a'); })()"
                      ".then(v => { result = v; });",
                      "a");
    expect_after_turn(
        "var result = ''; (async () => { throw 'e'; })().catch(e => { result = e; });", "e");

    // ================================================================
    // ITERATOR HELPERS, 27.1.3 / 27.1.4
    // ================================================================
    // IIFEs: js_expect compiles `return (<expression>);`, and a generator
    // declaration is a statement.
    js_expect("(function(){ function* g() { yield 1; yield 2; yield 3; }"
              " return g().map(x => x * 2).toArray().join(); })()",
              "2,4,6");
    js_expect("(function(){ function* g() { yield 1; yield 2; yield 3; yield 4; }"
              " return g().filter(x => x % 2).take(1).toArray().join(); })()",
              "1");
    js_expect("(function(){ function* g() { yield [1, 2]; yield [3]; }"
              " return g().flatMap(x => x).toArray().join(); })()",
              "1,2,3");
    js_expect("(function(){ function* g() { yield 1; yield 2; yield 3; }"
              " return g().drop(1).reduce((a, b) => a + b); })()",
              "5");
    js_expect("Iterator.from({ next() { return { done: true }; } }).toArray().length", "0");
    js_expect("Iterator.from('ab').toArray().join('')", "ab");
    js_expect("(function(){ try { new Iterator(); return 'no'; } catch (e) { return "
              "e.constructor.name; } })()",
              "TypeError");
    js_expect("(function(){ class C extends Iterator {} return new C() instanceof Iterator; })()",
              "true");
    js_expect("Iterator.prototype[Symbol.toStringTag]", "Iterator");
    js_expect("(function(){ var closed = 0; function* g() { try { yield 1; yield 2; } finally { "
              "closed++; } }"
              "g().some(x => x === 1); return closed; })()",
              "1");
    js_expect("Iterator.concat([1], new Set([2, 3])).toArray().join()", "1,2,3");
    js_expect("(function(){ function* g() { yield 1; yield 2; yield 3; }"
              " return g().chunks(2).map(c => c.join('-')).toArray().join(); })()",
              "1-2,3");

    // ================================================================
    // SYMBOL, REFLECT, DISPOSABLESTACK
    // ================================================================
    js_expect("(function(){ try { new Symbol(); return 'no'; } catch (e) { return "
              "e.constructor.name; } })()",
              "TypeError");
    js_expect("Object.getOwnPropertyDescriptor(Symbol.prototype, 'description').get.call(Symbol()) "
              "=== undefined",
              "true");
    js_expect("Symbol.iterator.description", "Symbol.iterator");
    js_expect("Symbol.prototype[Symbol.toPrimitive].call(Symbol.dispose) === Symbol.dispose",
              "true");
    js_expect("(function(){ var o = { set x(v) { this.y = v; } }; var r = {};"
              "return Reflect.set(o, 'x', 1, r) + ',' + r.y; })()",
              "true,1");
    js_expect("Reflect.set(Object.freeze({ a: 1 }), 'a', 2)", "false");
    js_expect("(function(){ try { Reflect.get(1, 'x'); return 'no'; } catch (e) { return "
              "e.constructor.name; } })()",
              "TypeError");
    // WeakRef and FinalizationRegistry (collections/weak.cpp): installed
    // since ctcompile retired its absence pin; a target is held strongly.
    js_expect("typeof WeakRef + ',' + typeof FinalizationRegistry", "function,function");
    js_expect("(function(){ var o = {}; return new WeakRef(o).deref() === o; })()", "true");
    js_expect(
        "(function(){ var log = []; var s = new DisposableStack();"
        "s.use({ [Symbol.dispose]() { log.push('a'); } }); s.defer(() => log.push('b'));"
        "s.adopt(3, v => log.push('c' + v)); s.dispose(); return log.join('') + s.disposed; })()",
        "c3batrue");
    js_expect("(function(){ var s = new DisposableStack(); s.defer(() => { throw 1; });"
              "s.defer(() => { throw 2; }); try { s.dispose(); } catch (e) {"
              "return e.constructor.name + e.error + e.suppressed; } })()",
              "SuppressedError12");
    expect_after_turn("var result = ''; var log = []; var s = new AsyncDisposableStack();"
                      "s.use({ async [Symbol.asyncDispose]() { log.push('a'); } });"
                      "s.defer(() => log.push('b'));"
                      "s.disposeAsync().then(() => { result = log.join('') + s.disposed; });",
                      "batrue");

    REPORT("promise_spec");
}
