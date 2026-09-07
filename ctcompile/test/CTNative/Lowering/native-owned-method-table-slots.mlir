// RUN: split-file %s %t
// RUN: python3 %S/check-owned-method-table-slots.py --translate ctjs-translate --opt ctjs-opt --specimen %S/../../native-owned-method-table-slot-fixture.js --fixtures %t --work %t.executables

// The independent checker runs the exact six-function specimen and this
// lifetime program through admission, explicit/deduced C++, both compilers,
// the interpreter and ASan/UBSan. Its refusal controls keep default
// optimizations off and require a named refusal, not a compiler crash.

//--- lifetime.js
function makeTable(seed, label) {
    const state = new Map();
    state.set("value", seed);
    const text = label + "-a-string-long-enough-to-need-owned-storage";
    return {
        get() { return state.get("value") + 0; },
        set(value) { state.set("value", value); return 0; },
        name(suffix) { return text + suffix; }
    };
}
function transport(table) { return table; }
function publish(seed, label) {
    // No special meaning is attached to the specimen's property `exports`.
    const ns = {payload: makeTable(seed, label)};
    return transport(ns.payload);
}
function nestedPublish(seed, label) {
    const ns = {item: publish(seed, label)};
    return transport(ns.item);
}
function churn() {
    for (var index = 0; index < 200; ++index) {
        const temporary = publish(index, "temporary");
        temporary.set(index + 1);
        temporary.get();
        temporary.name("!");
    }
    return 0;
}
function savedLifetime() {
    const retained = publish(40, "saved");
    churn();
    return retained.get() + 2;
}
function sharedAlias() {
    const first = publish(10, "shared");
    const alias = transport(first);
    alias.set(15);
    return first.get();
}
function independentInstances() {
    const first = publish(10, "first");
    const second = publish(100, "second");
    first.set(42);
    return second.get() + 1;
}
function nestedTransport() {
    const table = nestedPublish(40, "nested");
    table.set(42);
    return table.get();
}
function stringLifetime() {
    const first = publish(1, "first");
    const second = publish(2, "second");
    churn();
    const a = first.name("!") === "first-a-string-long-enough-to-need-owned-storage!" ? 1 : 0;
    const b = second.name("?") === "second-a-string-long-enough-to-need-owned-storage?" ? 10 : 0;
    return a + b;
}
var independent101 = independentInstances();
var lifetime42 = savedLifetime();
var nested42 = nestedTransport();
var shared15 = sharedAlias();
var strings11 = stringLifetime();

//--- families.js
// The owner field has one schema at each site. Sharing the field name makes
// one C++ class template; it must not merge these two distinct table schemas.
function makeMutable(seed) {
    const state = new Map();
    state.set("value", seed);
    return {
        get() { return state.get("value") + 0; },
        set(value) { state.set("value", value); return 0; }
    };
}
function makeScaled(seed) {
    return {read(factor) { return seed * factor; }};
}
function publishMutable(seed) {
    const local = {exports: makeMutable(seed)};
    return local.exports;
}
function publishScaled(seed) {
    const local = {exports: makeScaled(seed)};
    return local.exports;
}
function scalar(seed) {
    const local = {exports: seed};
    return local.exports;
}
function run() {
    const first = publishMutable(40);
    const second = publishScaled(7);
    first.set(42);
    return first.get() * 100 + second.read(3) + scalar(2);
}
var families4223 = run();
