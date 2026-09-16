// Map, Set, WeakMap and WeakSet as 24.1-24.4 write them: the receiver checks,
// the -0 key, WeakMap and WeakSet as their own classes with weakly-holdable
// keys, the ES2025 set algebra over set-likes, Map.groupBy and the upsert
// pair. Each assertion is one that test262's built-ins/Map or /Set rows
// failed on 2026-09-12.

#include "vm_expect.hpp"

namespace {

void test_receivers() {
    expect_result("try { Map(); return 'no'; } catch (e) { return e.name; }", "TypeError");
    expect_result("try { Map.prototype.get.call(new Set(), 1); return 'no'; } catch (e) { return "
                  "e.name; }",
                  "TypeError");
    expect_result(
        "try { Set.prototype.has.call({}, 1); return 'no'; } catch (e) { return e.name; }",
        "TypeError");
    expect_result("try { Object.getOwnPropertyDescriptor(Map.prototype, 'size').get.call([]); "
                  "return 'no'; } catch (e) { return e.name; }",
                  "TypeError");
    expect_result("try { new Set([1]).forEach(3); return 'no'; } catch (e) { return e.name; }",
                  "TypeError");
    // a subclass instance reaches the base through super() and keeps its state
    expect_result("class S extends Set { x = 1 } const s = new S([1, 2]); return s.size + s.x;",
                  "3");
    // the seed goes through the receiver's own adder, and a Map entry must be
    // an object
    expect_result("class M extends Map { set(k, v) { return super.set(k, v * 2); } } "
                  "return new M([['a', 1]]).get('a');",
                  "2");
    expect_result("try { new Map([1]); return 'no'; } catch (e) { return e.name; }", "TypeError");
    expect_result("return Object.keys(new Map([[1, 2]])).length;", "0");
}

void test_keys() {
    expect_result("const m = new Map(); m.set(-0, 'z'); return m.has(0) + '|' + "
                  "Object.is([...m.keys()][0], 0);",
                  "true|true");
    expect_result("const s = new Set([1]); s.add(1); s.add(NaN); s.add(NaN); return s.size;", "2");
    // forEach sees an entry added during the walk
    expect_result(
        "const s = new Set([1]); let n = 0; s.forEach(v => { n++; if (v === 1) s.add(2); });"
        " return n;",
        "2");
}

void test_weak() {
    expect_result("return WeakMap === Map || WeakMap.prototype === Map.prototype;", "false");
    expect_result("const w = new WeakMap(); const k = {}; w.set(k, 1); return w.get(k) + '|' + "
                  "w.has({}) + '|' + ('size' in w) + '|' + (Symbol.iterator in w);",
                  "1|false|false|false");
    expect_result("try { new WeakMap().set(1, 1); return 'no'; } catch (e) { return e.name; }",
                  "TypeError");
    expect_result("try { new WeakSet().add(Symbol.for('x')); return 'no'; } catch (e) { return "
                  "e.name; }",
                  "TypeError");
    expect_result("const w = new WeakSet(); const s = Symbol(); w.add(s); return w.has(s);",
                  "true");
    expect_result("return Object.prototype.toString.call(new WeakSet());", "[object WeakSet]");
}

void test_algebra() {
    expect_result("return [...new Set([1, 2]).union(new Set([2, 3]))].join();", "1,2,3");
    expect_result("return [...new Set([1, 2, 3]).intersection(new Set([2, 3, 4]))].join();", "2,3");
    expect_result("return [...new Set([1, 2, 3]).difference(new Set([2]))].join();", "1,3");
    expect_result("return [...new Set([1, 2]).symmetricDifference(new Set([2, 3]))].join();",
                  "1,3");
    expect_result(
        "return new Set([1]).isSubsetOf(new Set([1, 2])) + '' + "
        "new Set([1, 2]).isSupersetOf(new Set([1])) + new Set([1]).isDisjointFrom(new Set([2]));",
        "truetruetrue");
    // a set-like is anything with size, has and keys; a bad size is refused
    expect_result("const like = {size: 2, has: v => v === 'a', keys: () => ['a', 'b'].values()};"
                  " return [...new Set(['x']).union(like)].join();",
                  "x,a,b");
    expect_result("try { new Set().union({size: NaN, has() {}, keys() {}}); return 'no'; } "
                  "catch (e) { return e.name; }",
                  "TypeError");
    expect_result("try { new Set().union({size: -1, has() {}, keys() {}}); return 'no'; } "
                  "catch (e) { return e.name; }",
                  "RangeError");
    expect_result("try { new Set().union({size: 1, has: 1, keys() {}}); return 'no'; } "
                  "catch (e) { return e.name; }",
                  "TypeError");
    // the result is a plain Set even from a subclass
    expect_result("class S extends Set {} return new S([1]).union(new Set()).constructor === Set;",
                  "true");
}

void test_extras() {
    expect_result("const m = Map.groupBy([1, 2, 3, 4], v => v % 2 ? 'odd' : 'even'); "
                  "return m.get('odd').join() + '|' + m.get('even').join();",
                  "1,3|2,4");
    expect_result(
        "const m = new Map(); m.getOrInsert('a', 1); return m.getOrInsert('a', 2) + '|' + "
        "m.getOrInsertComputed('b', k => k + '!');",
        "1|b!");
    expect_result("return Map[Symbol.species] === Map && Set[Symbol.species] === Set;", "true");
    expect_result("return Object.prototype.toString.call(new Map()) + "
                  "Object.prototype.toString.call(new Set());",
                  "[object Map][object Set]");
}

// 24.1.1.2 AddEntriesFromIterable, one step at a time: an endless iterator
// ends when the adder throws, the iterator is CLOSED (return() called once)
// and the adder's throw is the one that surfaces; a non-object entry closes
// it too; a non-iterable is a TypeError before anything is read.
void test_constructor_steps_its_iterable() {
    expect_result("var count = 0; var it = { [Symbol.iterator]() { return { next() { return {"
                  " value: [], done: false }; }, return() { count++; return {}; } }; } };"
                  " var saved = Map.prototype.set; Map.prototype.set = function () { throw new"
                  " RangeError('adder'); }; var r; try { new Map(it); } catch (e) { r = e.name; }"
                  " Map.prototype.set = saved; return r + ',' + count;",
                  "RangeError,1");
    expect_result("var count = 0; var it = { [Symbol.iterator]() { return { next() { return {"
                  " value: 1, done: false }; }, return() { count++; return {}; } }; } };"
                  " var r; try { new Map(it); } catch (e) { r = e.name; } return r + ',' + count;",
                  "TypeError,1");
    expect_result("try { new Set({}); } catch (e) { return e.name; }", "TypeError");
    expect_result("return new Map([[1, 2], [3, 4]]).size + new Set([1, 1, 2]).size;", "4");
    // %ArrayIteratorPrototype% and its siblings: one prototype per kind,
    // carrying next and the tag, under %Iterator.prototype%.
    expect_result("const a = [1].values(), b = [2].keys(); const P = Object.getPrototypeOf(a);"
                  " return [P === Object.getPrototypeOf(b), Object.getPrototypeOf(P) ==="
                  " Iterator.prototype, a.hasOwnProperty('next'), P[Symbol.toStringTag],"
                  " Object.prototype.toString.call(new Map().entries()), a[Symbol.iterator]() ==="
                  " a, [...a].join()].join('|');",
                  "true|true|false|Array Iterator|[object Map Iterator]|true|1");
    // ...and the prototype is nobody's global: it lives under a private key on
    // Array.prototype, so neither `window` nor Array.prototype enumerates it.
    expect_result("[1].values(); new Map().keys(); return Object.keys(globalThis).concat("
                  "Object.getOwnPropertyNames(Array.prototype)).filter(k => k.includes("
                  "'Iterator')).length;",
                  "0");
}

} // namespace

int main() {
    test_constructor_steps_its_iterable();
    test_receivers();
    test_keys();
    test_weak();
    test_algebra();
    test_extras();
    REPORT("keyed_collections");
}
