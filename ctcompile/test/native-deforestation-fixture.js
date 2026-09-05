// Snapshot producers and scalar consumers are inferred before elaboration.
// Conflicting consumers, aliases with mutation, calls and control retain the
// intermediate vector; simple projections remove it without moving effects.
function absent() { return; }
function flags(value) {
    var result = 0;
    if (typeof value === "number") { result += 1; }
    if (value === absent()) { result += 2; }
    if (value !== value) { result += 4; }
    if (value === 0) { result += 8; }
    if (value === 10) { result += 16; }
    if (value === 20) { result += 32; }
    if (value === 30) { result += 64; }
    if (value === 40) { result += 128; }
    return result;
}
function keyAt(index) {
    const map = new Map();
    map.set(10, 1).set(20, 2).set(30, 3);
    map.delete(20);
    map.set(20, 4);
    const keys = map.keys();
    return flags(keys[index]);
}
function valueAt(index) {
    const map = new Map();
    map.set("a", 10).set("b", 20).set("c", 30);
    map.set("b", 40);
    const values = map.values();
    return flags(values[index]);
}
function specialValue(index) {
    const map = new Map();
    map.set(1, 0).set(2, 0 / 0);
    const values = map.values();
    return flags(values[index]);
}
function emptyIndex(index) {
    const map = new Map();
    const values = map.values();
    return flags(values[index]);
}
function snapshotLength(count) {
    const map = new Map();
    for (var i = 0; i < count; ++i) { map.set(i, i + 1); }
    const values = map.values();
    return values.length;
}
function nestedKey() {
    const outer = new Map();
    const inner = new Map();
    inner.set("value", 42);
    outer.set(40, inner);
    return outer.keys()[0] + 2;
}
function retainedZeroKey() {
    const map = new Map();
    map.set(-0, 10).set(0, 20);
    const keys = map.keys();
    // The reference preserves the first inserted key's negative sign.
    return 1 / keys[0];
}
function scalarReuse() {
    const map = new Map();
    map.set(1, 21);
    const values = map.values();
    const first = values[0];
    return first + first;
}
function multipleConsumers() {
    const map = new Map();
    map.set(1, 20).set(2, 22);
    const values = map.values();
    return values[0] + values[1];
}
function mutateAlias() {
    const map = new Map();
    const alias = map.set(1, 42);
    const values = map.values();
    alias.set(1, 99);
    return values[0] + 0;
}
function mutate(map) { map.clear(); return 0; }
function mutateCall() {
    const map = new Map();
    map.set(1, 42);
    const values = map.values();
    mutate(map);
    return values[0] + 0;
}
function mutateCaptured() {
    const map = new Map();
    map.set(1, 42);
    function clear() { map.clear(); }
    const values = map.values();
    clear();
    return values[0] + 0;
}
function lengthBeforeMutation() {
    const map = new Map();
    map.set(1, 42);
    const keys = map.keys();
    map.clear();
    return keys.length;
}
function acrossControl(flag) {
    const map = new Map();
    map.set(1, 42);
    const values = map.values();
    if (flag) { return values[0] + 0; }
    return 0;
}
function loopSnapshots(count) {
    const map = new Map();
    map.set(1, 10);
    var result = 0;
    for (var i = 0; i < count; ++i) {
        const values = map.values();
        result += values[0];
        map.set(1, i);
    }
    return result;
}

var key_first = keyAt(0);
var key_second = keyAt(1);
var key_reinserted = keyAt(2);
var key_fractional = keyAt(1.9);
var key_negative_fractional = keyAt(-0.5);
var key_negative = keyAt(-1);
var key_past_end = keyAt(3);
var key_nan = keyAt(0 / 0);
var key_infinity = keyAt(1 / 0);
var key_false = keyAt(false);
var key_true = keyAt(true);
var key_null = keyAt(null);
var key_undefined = keyAt(absent());
var value_first = valueAt(0);
var value_overwritten = valueAt(1);
var value_last = valueAt(2);
var value_missing = valueAt(3);
var value_zero = specialValue(0);
var value_nan = specialValue(1);
var value_absent = specialValue(2);
var empty_absent = emptyIndex(0);
var empty_length = snapshotLength(0);
var populated_length = snapshotLength(4);
var nested_key42 = nestedKey();
var negative_zero = retainedZeroKey();
var reused_scalar42 = scalarReuse();
var multiple42 = multipleConsumers();
var alias42 = mutateAlias();
var call42 = mutateCall();
var capture42 = mutateCaptured();
var retained_length = lengthBeforeMutation();
var control42 = acrossControl(true);
var control_zero = acrossControl(false);
var zz_loop13 = loopSnapshots(4);
