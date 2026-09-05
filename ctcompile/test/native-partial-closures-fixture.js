// Factory construction is static. Closure bodies run only after their factory
// has returned, with dynamic arguments and ordinary owning C++ environments.
function makeRetained() {
    const scratch = { amount: 1 };
    scratch.amount = 20;
    const discarded = new Map();
    discarded.set("temporary", 99);
    const state = new Map();
    state.set("value", 0);
    state.set("value", scratch.amount * 2);
    state.set("calls", 0);
    const before = state.get("value");
    return function (delta) {
        lastDelta = delta;
        state.set("calls", state.get("calls") + 1);
        state.set("value", state.get("value") + delta);
        return before * 1000 + state.get("value") + state.get("calls") * 100;
    };
}
function retainedAliases() {
    const first = makeRetained();
    const alias = first;
    const second = makeRetained();
    const a = first(1);
    const b = alias(1);
    return a + b * 10 + second(2) * 100;
}
function retainedLifetime() {
    const retained = makeRetained();
    for (var i = 0; i < 40; ++i) {
        const temporary = makeRetained();
        temporary(i);
    }
    return retained(2);
}
function makeData() {
    const state = new Map();
    state.set("value", 0);
    state.set("value", 40);
    return {
        get(delta) { return state.get("value") + delta; },
        set(value) { state.set("value", value); return state.get("value") + 0; },
        remove() { return state.delete("value") ? 1 : 0; }
    };
}
function tableSharing() {
    const first = makeData();
    const alias = first;
    const second = makeData();
    const before = first.get(1);
    alias.set(42);
    return before + first.get(0) * 100 + second.get(2) * 10000 +
        first.remove() * 1000000 + alias.remove() * 10000000;
}
function makeKeyed() {
    const first = {};
    const second = {};
    const state = new Map();
    state.set(first, 10);
    state.set(second, 22);
    state.set(first, 20);
    return function (delta) {
        state.set(first, state.get(first) + delta);
        return state.get(first) * 100 + state.get(second);
    };
}
function keyedLifetime() {
    const first = makeKeyed();
    const second = makeKeyed();
    const a = first(1);
    return a + first(2) * 10000 + second(3) * 100000000;
}
function makeSnapshot() {
    const state = new Map();
    state.set("value", 10);
    state.set("value", 42);
    const saved = state.get("value");
    return delta => saved + delta;
}
function scalarSnapshot() {
    const first = makeSnapshot();
    const second = makeSnapshot();
    return first(0) + second(3) * 100;
}
function makeText() {
    const text = "value:" + ("40" + 2) + "-a-string-long-enough-to-need-owned-storage";
    return suffix => text + suffix;
}
function textLifetime() {
    const first = makeText();
    const second = makeText();
    const yes = first("!") === "value:402-a-string-long-enough-to-need-owned-storage!" ? 1 : 0;
    const other = second("?") === "value:402-a-string-long-enough-to-need-owned-storage?" ? 10 : 0;
    return yes + other;
}
var lastDelta = 0;
var aliases = retainedAliases();
var sharedMethods = tableSharing();
var objectKeys = keyedLifetime();
var snapshot = scalarSnapshot();
var text = textLifetime();
var lifetime42 = retainedLifetime();
