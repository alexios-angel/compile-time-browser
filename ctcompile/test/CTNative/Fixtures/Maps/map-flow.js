// Owning Map handles across closed calls, returns and lifted captures.
// Every observation is compared with the ctjs interpreter, including after
// a factory frame has returned and after unrelated stores are allocated.
function makeStore(seed) {
    var store = new Map();
    function initialize() { store.set("seed", seed); return store.size; }
    initialize();
    return store;
}
function put(store, key, value) { store.set(key, value); return store.size; }
function read(store, key) { return store.get(key) + 0; }
function identity(store) { return store; }
function forward(store) { return identity(store); }
function setAndReturn(store, key, value) { return store.set(key, value); }

function afterReturn() {
    var store = makeStore(10);
    put(store, "next", 32);
    return read(store, "seed") + read(store, "next");
}
var lifetime42 = afterReturn();

function independent() {
    var first = makeStore(10);
    var second = makeStore(20);
    put(second, "seed", 21);
    return read(first, "seed") * 100 + read(second, "seed");
}
var independent1021 = independent();

function returnedAlias() {
    var store = makeStore(10);
    var alias = forward(store);
    setAndReturn(alias, "seed", 42).set("extra", 5);
    return read(store, "seed") + read(alias, "extra");
}
var alias47 = returnedAlias();

function localMethods(seed) {
    var store = makeStore(seed);
    var data = {
        set(key, value) { store.set(key, value); return store.size; },
        get(key) { return store.get(key) + 0; },
        remove(key) { return store.delete(key) ? 1 : 0; }
    };
    data.set("component", 32);
    var before = data.get("seed") + data.get("component");
    return before + data.remove("component") + data.remove("component");
}
var methods43 = localMethods(10);

function nestedCapture() {
    var store = makeStore(40);
    function outer(amount) {
        function inner() { store.set("seed", read(store, "seed") + amount); return store.size; }
        return inner();
    }
    outer(2);
    return read(store, "seed");
}
var nested42 = nestedCapture();

function differentSlots(numbers, strings) {
    numbers.set(1, 20);
    strings.set("1", 22);
    return numbers.get(1) + strings.get("1");
}
function separateSchemas() {
    var numbers = new Map();
    var strings = new Map();
    return differentSlots(numbers, strings);
}
var slots42 = separateSchemas();

function fill(store, n) {
    if (n <= 0) { return store.size; }
    store.set(n, n + 1);
    return fill(store, n - 1);
}
function recursiveStore(n) {
    var store = new Map();
    var size = fill(store, n);
    return size + store.get(1);
}
var recursion7 = recursiveStore(5);

function booleanStore() { return new Map().set(true, 40).set(false, 2); }
function returnedBoolean() {
    var store = booleanStore();
    return store.get(true) + store.get(false);
}
var boolean42 = returnedBoolean();

function unusedStore(store) { return 5; }
function discardedArgument() { return unusedStore(makeStore(42)); }
var unused5 = discardedArgument();

function emptyStore() { return new Map(); }
function emptyReturn() {
    var store = emptyStore();
    return store.size + (store.has(1) ? 10 : 0);
}
var empty0 = emptyReturn();

function retainedAcrossAllocations(n) {
    var retained = makeStore(42);
    var total = 0;
    for (var i = 0; i < n; i = i + 1) {
        var temporary = makeStore(i);
        total = total + read(temporary, "seed");
    }
    return read(retained, "seed") + total;
}
var retained4992 = retainedAcrossAllocations(100);
