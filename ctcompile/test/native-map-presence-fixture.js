// Membership facts follow runtime identity and survive only known safe effects.
function leaf(value) { const map = new Map(); map.set("value", value); return map; }
function ensure(outer, key, value) {
    outer.has(key) || outer.set(key, leaf(value));
    return outer.get(key).get("value") + 0;
}
function lazyInitialization() {
    const map = new Map();
    const first = ensure(map, "key", 40);
    const second = ensure(map, "key", 99);
    const other = ensure(map, "other", 2);
    return first * 10000 + second * 100 + other;
}
function guardedRead(outer, key) {
    if (!outer.has(key)) { return 0; }
    return outer.get(key).get("value") + 0;
}
function remove(outer, key) {
    if (!outer.has(key)) { return 0; }
    const child = outer.get(key);
    child.delete("value");
    if (child.size === 0) { outer.delete(key); }
    return 1;
}
function guardedLifecycle() {
    const map = new Map();
    ensure(map, "key", 42);
    const before = guardedRead(map, "key");
    const removed = remove(map, "key");
    const after = guardedRead(map, "key");
    const twice = remove(map, "key");
    return before * 100 + removed + after + twice;
}
function guardedAnd(flag) {
    const outer = new Map();
    outer.set("key", leaf(42));
    if (flag) { outer.delete("key"); }
    if (outer.has("key") && outer.get("key").has("value")) { return 42; }
    return 0;
}
function bothBranches(flag) {
    const outer = new Map();
    const inner = leaf(42);
    if (flag) { outer.set("key", inner); }
    else { outer.set("key", inner); }
    return outer.get("key").get("value") + 0;
}
function reinsert() {
    const outer = new Map();
    outer.set("key", leaf(20));
    const before = outer.has("key");
    outer.clear();
    outer.has("key") || outer.set("key", leaf(42));
    return (before ? 100 : 0) + outer.get("key").get("value");
}
function retainedChild() {
    const outer = new Map();
    outer.set("key", leaf(42));
    const child = outer.get("key");
    outer.clear();
    return child;
}
function lifetime() {
    const child = retainedChild();
    for (var i = 0; i < 100; ++i) { retainedChild(); }
    return child.get("value") + 0;
}
function freshLoopGuard() {
    const outer = new Map();
    outer.set("key", leaf(42));
    var result = 0;
    for (var i = 0; i < 2; ++i) {
        if (outer.has("key")) { result += outer.get("key").get("value"); }
        outer.clear();
    }
    return result;
}
function recheckAfterCall(outer) { outer.clear(); }
function restoredAfterCall() {
    const outer = new Map();
    outer.set("key", leaf(20));
    recheckAfterCall(outer);
    if (!outer.has("key")) { outer.set("key", leaf(42)); }
    return outer.get("key").get("value") + 0;
}
var lazy = lazyInitialization();
var guarded = guardedLifecycle();
var shortCircuit42 = guardedAnd(false) + guardedAnd(true);
var branches84 = bothBranches(false) + bothBranches(true);
var reinsertion142 = reinsert();
var lifetime42 = lifetime();
var loop42 = freshLoopGuard();
var call42 = restoredAfterCall();
