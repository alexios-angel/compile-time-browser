// The standard library's internal slots are PRIVATE-keyed (TYPE-001).
//
// A Map's entry list, a promise's state and reactions, a generator's
// coroutine, an array iterator's list and cursor and a Date's time value were
// ordinary string properties - `__entries`, `__settled`, `__handlers`, `__co`,
// `__items`, `__ms` - which a page could read, overwrite, or forge on a plain
// object, and natives cast what they found: `m.__entries = [1.5e-300];
// m.get(0)` dereferenced a pointer the page chose. Each class below checks the
// three things that fix promises: the slot is not observable, writing the old
// name does not reach it, and a forged object is not mistaken for the real one.

#include "js_expect.hpp"
#include "vm/expect.hpp"

int main() {
    // ---- Map / Set -----------------------------------------------------
    js_expect("(function(){var m=new Map([[1,2]]);var n=0;for(var k in m)n++;"
              "return Object.getOwnPropertyNames(m).length+','+Reflect.ownKeys(m).length+','+n+','+"
              "JSON.stringify(m)+','+('__entries' in m)})()",
              "0,0,0,{},false");
    // The old name is now an ordinary property the natives never read.
    js_expect("(function(){var m=new Map([[1,2]]);m.__entries=[1.5e-300];"
              "return m.get(1)+','+m.size+','+structuredClone(m).get(1)})()",
              "2,1,2");
    js_expect("(function(){var s=new Set([1]);s.__entries=[{},1.5e-300];"
              "return s.has(1)+','+[...s].join()+','+structuredClone(s).size})()",
              "true,1,1");
    // A plain object carrying the old name is not a Map to for-of or spread.
    js_expect("(function(){try{[...{__entries:[1.5e-300]}];return 'iterated'}"
              "catch(e){return e.name}})()",
              "TypeError");
    js_expect("(function(){try{Map.prototype.get.call({__entries:[1.5e-300]},0);return 'read'}"
              "catch(e){return e.name}})()",
              "TypeError");

    // ---- array iterators -----------------------------------------------
    js_expect("(function(){var it=[1,2].values();var k=Reflect.ownKeys(it).length;"
              "it.__items=[1.5e-300];it.__at=7;it.__kind='x';"
              "var a=it.next().value;return k+','+a+','+it.next().value})()",
              "0,1,2");
    js_expect("(function(){try{[...{__items:[1,2]}];return 'iterated'}"
              "catch(e){return e.name}})()",
              "TypeError");

    // ---- generators ----------------------------------------------------
    js_expect("(function(){function* g(){yield 1}var it=g();"
              "return Reflect.ownKeys(it).length+','+('__co' in it)})()",
              "0,false");
    js_expect("(function(){function* g(){yield 1}var next=Object.getPrototypeOf(g()).next;"
              "try{next.call({__co:1.5e-300});return 'resumed'}catch(e){return e.name}})()",
              "TypeError");
    js_expect("(function(){function* g(){yield 1;yield 2}var it=g();it.__co=null;"
              "return [...it].join()})()",
              "1,2");

    // ---- Date ----------------------------------------------------------
    js_expect("(function(){var d=new Date(0);var k=Reflect.ownKeys(d).length;d.__ms=5;"
              "return k+','+d.getTime()})()",
              "0,0");
    js_expect("(function(){try{Date.prototype.getTime.call({__ms:{}});return 'read'}"
              "catch(e){return e.name}})()",
              "TypeError");

    // ---- promises ------------------------------------------------------
    js_expect("(function(){var p=Promise.resolve(3);return Reflect.ownKeys(p).length+','+"
              "JSON.stringify(p)+','+('__handlers' in p)+','+('__settled' in p)})()",
              "0,{},false,false");
    // Overwriting the old names changes nothing a reaction sees.
    expect_after_turn("var result; var p = Promise.resolve(1);"
                      "p.__value = 2; p.__rejected = true;"
                      "p.then(function (v) { result = 'ok ' + v; },"
                      "       function (e) { result = 'rejected ' + e; });",
                      "ok 1");
    // A forged pending promise is an ordinary object to `await`: nothing is
    // pushed into its `__handlers`, and the body continues.
    expect_after_turn("var result; var h = [];"
                      "(async function () { var got = await {__settled: false, __handlers: h};"
                      "  result = h.length + ',' + typeof got; })();",
                      "0,object");
    // The double-resume attack: an async function's continuation runs ONCE.
    expect_after_turn("var result; var n = 0; var go;"
                      "var p = new Promise(function (r) { go = r; });"
                      "async function f() { await p; n++; result = n; }"
                      "f();"
                      "if (p.__handlers) { p.__handlers.push(p.__handlers[0]); }"
                      "go();",
                      "1");

    REPORT("internal_slots");
}
