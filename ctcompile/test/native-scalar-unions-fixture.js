// Closed boolean/number unions retain their JavaScript tags through native
// calls, fields, shared cells, closures, loops and the Bootstrap Data getter.
// Numeric observations exercise both alternatives without broadening globals.
function choose(flag) { return flag ? false : 42; }
function chooseReturn(flag) {
    if (flag) { return true; }
    return 1;
}
function optional(mode) {
    if (mode < 0) { return; }
    if (mode === 0) { return null; }
    if (mode === 1) { return false; }
    if (mode === 2) { return true; }
    if (mode === 3) { return 0; }
    if (mode === 4) { return -0; }
    if (mode === 5) { return 0 / 0; }
    return 42;
}
function forward(value) { return value; }
function absent() { return; }
function flags(value) {
    var result = 0;
    if (value === null) { result += 1; }
    if (value === absent()) { result += 2; }
    if (value === false) { result += 4; }
    if (value === true) { result += 8; }
    if (value === 0) { result += 16; }
    if (value === 1) { result += 32; }
    if (value == false) { result += 64; }
    if (value == true) { result += 128; }
    if (value) { result += 256; }
    if (typeof value === "boolean") { result += 512; }
    if (typeof value === "number") { result += 1024; }
    if (typeof value === "object") { result += 2048; }
    if (typeof value === "undefined") { result += 4096; }
    if (value !== value) { result += 8192; }
    return result;
}
function arithmetic(value) {
    return value + 2 + (+value) + value * 3 - value / 2;
}
function ordering(value) {
    return (value < 1 ? 1 : 0) + (value <= 1 ? 2 : 0) +
        (value > 1 ? 4 : 0) + (value >= 1 ? 8 : 0);
}
function localField(flag) {
    var object = { value: false };
    if (flag) { object.value = 42; }
    return flags(object.value);
}
function unreadField() {
    var object = { value: false };
    object.value = 42;
    return 42;
}
function sharedCell(flag) {
    var value = false;
    function assign() { value = 42; }
    if (flag) { assign(); }
    return flags(value);
}
function capture(value) { return function() { return value; }; }
function retainedCapture(value) {
    const read = capture(value);
    for (var i = 0; i < 20; ++i) { capture(i); }
    return flags(read());
}
function loop(count) {
    var value = false;
    for (var i = 0; i < count; ++i) {
        value = i;
        if (i === 1) { value = true; }
    }
    return flags(value);
}
function indexed(value) {
    const values = [42, 99];
    return flags(values[value]);
}
function logical(value) { return value && 7 || null; }
function makeData() {
    const t = new Map();
    return {
        set(e, i, n) {
            t.has(e) || t.set(e, new Map());
            t.get(e).set(i, n);
            return 1;
        },
        // Verbatim getter expression from Bootstrap 5.3's vendor Data table.
        get: (e, i) => t.has(e) && t.get(e).get(i) || null,
        remove(e, i) {
            if (!t.has(e)) { return; }
            const n = t.get(e);
            n.delete(i);
            0 === n.size && t.delete(e);
        }
    };
}
function retainedData() {
    const data = makeData();
    const first = {};
    const other = {};
    const absent = {};
    const missing = data.get(absent, "number") === null ? 1 : 0;
    data.set(first, "number", 42);
    data.set(other, "number", 21);
    for (var i = 0; i < 20; ++i) {
        const temporary = makeData();
        temporary.set(first, "number", i);
        temporary.remove(first, "number");
    }
    const retained = data.get(first, "number") === 42 ? 2 : 0;
    const distinct = data.get(other, "number") === 21 ? 4 : 0;
    const wrongKey = data.get(first, "missing") === null ? 8 : 0;
    data.set(first, "number", 0);
    const zero = data.get(first, "number") === null ? 16 : 0;
    data.set(first, "number", 0 / 0);
    const nan = data.get(first, "number") === null ? 32 : 0;
    data.remove(first, "number");
    const removed = data.get(first, "number") === null ? 64 : 0;
    data.set(first, "number", 43);
    const reinserted = data.get(first, "number") === 43 ? 128 : 0;
    return missing + retained + distinct + wrongKey + zero + nan + removed + reinserted;
}

var choose_false = flags(choose(true));
var choose_number = flags(choose(false));
var return_true = flags(chooseReturn(true));
var return_one = flags(chooseReturn(false));
var optional_undefined = flags(forward(optional(-1)));
var optional_null = flags(forward(optional(0)));
var optional_false = flags(forward(optional(1)));
var optional_true = flags(forward(optional(2)));
var optional_zero = flags(forward(optional(3)));
var optional_negative_zero = 1 / forward(optional(4));
var optional_nan = flags(forward(optional(5)));
var optional_number = flags(forward(optional(6)));
var arithmetic_false = arithmetic(choose(true));
var arithmetic_number = arithmetic(choose(false));
var arithmetic_null = arithmetic(optional(0));
var arithmetic_undefined = arithmetic(optional(-1));
var arithmetic_true = arithmetic(optional(2));
var ordering_false = ordering(choose(true));
var ordering_number = ordering(choose(false));
var field_false = localField(false);
var field_number = localField(true);
var field_unread = unreadField();
var cell_false = sharedCell(false);
var cell_number = sharedCell(true);
var closure_false = retainedCapture(false);
var closure_number = retainedCapture(42);
var closure_null = retainedCapture(null);
var loop_false = loop(0);
var loop_number = loop(1);
var loop_true = loop(2);
var array_false_key = indexed(optional(1));
var array_true_key = indexed(optional(2));
var array_zero_key = indexed(optional(3));
var array_null_key = indexed(optional(0));
var logical_false = flags(logical(optional(1)));
var logical_true = flags(logical(optional(2)));
var logical_number = flags(logical(optional(6)));
var logical_nan = flags(logical(optional(5)));
var zz_lifetime = retainedData();
