// Owning immutable captures, invoked after the factory frame has returned.
// Map contents remain shared; the binding to the Map is never reassigned.
function makeStore(seed) {
    const state = new Map();
    state.set("value", seed);
    return function (delta) {
        state.set("value", state.get("value") + delta);
        return state.get("value") + 0;
    };
}
function forwardCallable(fn) { return fn; }
function invokeCallable(fn, delta) { return fn(delta); }
function storeLifetime() {
    const first = makeStore(40);
    const alias = forwardCallable(first);
    const second = makeStore(10);
    const a = first(1);
    const b = invokeCallable(alias, 1);
    return a + b * 100 + second(2) * 10000;
}
function retainedStore() {
    const retained = makeStore(40);
    for (var i = 0; i < 100; ++i) {
        const temporary = makeStore(i);
        temporary(1);
    }
    return retained(2);
}
function makeAdder(value) { return delta => value + delta; }
function scalarLifetime() {
    const first = makeAdder(30);
    const second = makeAdder(10);
    return first(12) + second(3) * 100;
}
function makeText(prefix) {
    const text = prefix + "-a-string-long-enough-to-need-owned-storage";
    return suffix => text + suffix;
}
function stringLifetime() {
    const first = makeText("first");
    const second = makeText("second");
    const a = first("!") === "first-a-string-long-enough-to-need-owned-storage!" ? 1 : 0;
    const b = second("?") === "second-a-string-long-enough-to-need-owned-storage?" ? 10 : 0;
    return a + b;
}
function makeFlag(flag, yes, no) { return () => flag ? yes : no; }
function orderedCaptures() {
    const first = makeFlag(true, 42, 9);
    const second = makeFlag(false, 100, 7);
    return first() + second() * 100;
}
function makeEmpty() { return value => value * 2; }
function emptyEnvironment() {
    const fn = makeEmpty();
    return fn(21);
}
function captureStore(store) { return () => store.get("value") + 0; }
function sharedStore() {
    const store = new Map();
    store.set("value", 10);
    const read = captureStore(store);
    store.set("value", 42);
    return read();
}
function makePair() {
    const first = new Map();
    const second = new Map();
    first.set(1, 20);
    second.set("value", 22);
    return () => first.get(1) + second.get("value");
}
function distinctCaptureSchemas() {
    const fn = makePair();
    return fn();
}
function missingArgument() {
    const fn = makeAdder(42);
    return fn();
}
function invokeInsideLiftedFrame() {
    return (function () {
        const fn = makeAdder(10);
        return fn(32);
    })();
}
function invokeInsideLiftedFrameWithParameter() {
    return (function (fallback) {
        const fn = makeAdder(10);
        return fn(32) + fallback * 0;
    })(20);
}
function nestedFactory(k) {
    function mid() { return () => k; }
    return mid()();
}
function invokeThroughLiftedParameter() {
    const fn = makeAdder(10);
    return (function (callback) { return callback(32); })(fn);
}
var stores = storeLifetime();
var lifetime42 = retainedStore();
var scalars = scalarLifetime();
var strings = stringLifetime();
var captures = orderedCaptures();
var empty = emptyEnvironment();
var shared = sharedStore();
var distinct = distinctCaptureSchemas();
var missing = missingArgument();
var liftedFrame = invokeInsideLiftedFrame();
var liftedArgument = invokeInsideLiftedFrameWithParameter();
var nested = nestedFactory(42);
var liftedCallback = invokeThroughLiftedParameter();
