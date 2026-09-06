// RUN: split-file %s %t
// RUN: python3 %S/check-owning-callables.py --fixtures %t --work %t.executables --translate ctjs-translate --opt ctjs-opt

// The returned lambda outlives its factory. Aliases keep the same Map, separate
// factories keep separate Maps, and strings own storage beyond their frame.
// This test executes ordinary and deduced output with both C++ compilers and
// repeats the lifetime observations under address/undefined sanitizers.

//--- owning.js
function makeCounter(seed) {
    const state = new Map();
    state.set("value", seed);
    return function (delta) {
        const next = state.get("value") + delta;
        state.set("value", next);
        return next;
    };
}
function forward(callback) { return callback; }
function invoke(callback, delta) { return callback(delta); }
function afterFactoryReturns() {
    const increment = makeCounter(40);
    for (var index = 0; index < 200; ++index) {
        const temporary = makeCounter(index);
        temporary(1);
    }
    return increment(2);
}
function sharedAlias() {
    const counter = makeCounter(10);
    const alias = forward(counter);
    counter(2);
    return invoke(alias, 3);
}
function independentCounters() {
    const first = makeCounter(10);
    const second = makeCounter(100);
    first(7);
    return second(1);
}
function captureExisting(state) {
    return function () { return state.get("value") + 0; };
}
function externalMutation() {
    const state = new Map();
    state.set("value", 10);
    const read = captureExisting(state);
    state.set("value", 42);
    return read();
}
var independentResult = independentCounters();
var lifetimeResult = afterFactoryReturns();
var sharedResult = sharedAlias();
var mutationResult = externalMutation();

//--- scalar-string.js
// No Map or method table: callable emission itself must request its headers.
function makeText(prefix) {
    const text = prefix + "-a-string-long-enough-to-need-owned-storage";
    return suffix => text + suffix;
}
function strings() {
    const first = makeText("first");
    const second = makeText("second");
    const a = first("!") === "first-a-string-long-enough-to-need-owned-storage!" ? 1 : 0;
    const b = second("?") === "second-a-string-long-enough-to-need-owned-storage?" ? 10 : 0;
    return a + b;
}
function makeAdder(value) { return delta => value + delta; }
function scalars() {
    const first = makeAdder(30);
    const second = makeAdder(10);
    return first(12) + second(3) * 100;
}
function makeKeyword(template) { return concept => template + concept; }
function keywords() { const fn = makeKeyword(40); return fn(2); }
var stringResult = strings();
var scalarResult = scalars();
var keywordResult = keywords();

//--- fallback.js
// A callable-valued parameter is outside the concrete std::function signature
// set. Its already-supported tuple representation must remain available while
// the scalar adder uses the new owning lambda representation in the same unit.
function makeApply(delta) { return callback => callback(delta); }
function makeAdder(seed) { return value => seed + value; }
function applyCallback() {
    const apply = makeApply(2);
    const add = makeAdder(40);
    return apply(add);
}
var fallbackResult = applyCallback();
