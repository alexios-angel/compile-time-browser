// Nullable scalars preserve null, undefined, booleans and numeric payloads.
// Observations stay numeric so the differential exercises the value semantics
// independently of the global-printing convention.
function absent() { return; }
function maybeNumber(mode) {
    if (mode < 0) { return; }
    if (mode === 0) { return null; }
    if (mode === 1) { return 0; }
    if (mode === 2) { return -0; }
    if (mode === 3) { return 0 / 0; }
    return 42;
}
function maybeBoolean(mode) {
    if (mode < 0) { return; }
    if (mode === 0) { return null; }
    if (mode === 1) { return false; }
    return true;
}
function forwardNumber(value) { return value; }
function forwardBoolean(value) { return value; }
function numberFlags(value) {
    var flags = 0;
    if (value === null) { flags = flags + 1; }
    if (value == null) { flags = flags + 2; }
    if (value === absent()) { flags = flags + 4; }
    if (value == 0) { flags = flags + 8; }
    if (value === 0) { flags = flags + 16; }
    if (value !== value) { flags = flags + 32; }
    if (value) { flags = flags + 64; }
    if (typeof value === "number") { flags = flags + 128; }
    if (typeof value === "object") { flags = flags + 256; }
    if (typeof value === "undefined") { flags = flags + 512; }
    return flags;
}
function booleanFlags(value) {
    var flags = 0;
    if (value === null) { flags = flags + 1; }
    if (value == null) { flags = flags + 2; }
    if (value === absent()) { flags = flags + 4; }
    if (value == false) { flags = flags + 8; }
    if (value === false) { flags = flags + 16; }
    if (value == 0) { flags = flags + 32; }
    if (value === 0) { flags = flags + 64; }
    if (value) { flags = flags + 128; }
    if (typeof value === "boolean") { flags = flags + 256; }
    if (typeof value === "object") { flags = flags + 512; }
    if (typeof value === "undefined") { flags = flags + 1024; }
    return flags;
}
function arithmetic(value) {
    // null converts to zero; undefined and a present NaN convert to NaN.
    return (+value) + (value + 2) + (value - 2) + value * 3 + value / 2;
}
function booleanArithmetic(value) { return (+value) + value * 2; }
function ordering(value) {
    var flags = 0;
    if (value < 0) { flags = flags + 1; }
    if (value <= 0) { flags = flags + 2; }
    if (value > 0) { flags = flags + 4; }
    if (value >= 0) { flags = flags + 8; }
    return flags;
}
function fallbackNumber(value) { return value || 17; }
function fallbackBoolean(value) { return value || true; }
function conditionalNumber(flag) { return flag ? null : 42; }
function conditionalBoolean(flag) { return flag ? false : null; }
function stringType(value) { return typeof value === "string" ? 1 : 0; }
function missingParameter(value) { return numberFlags(value); }
function localBeforeWrite(flag) {
    var value;
    if (flag) { value = null; }
    return numberFlags(value);
}
function loopValue(count) {
    var value;
    for (var i = 0; i < count; i = i + 1) {
        if (i === 0) { value = null; }
        else { value = i; }
    }
    return numberFlags(value);
}
function mapObservations() {
    var map = new Map();
    map.set("zero", 0);
    map.set("nan", 0 / 0);
    var zero = numberFlags(map.get("zero"));
    var nan = numberFlags(map.get("nan"));
    var missing = numberFlags(map.get("absent"));
    var cleared = numberFlags(map.clear());
    return zero + nan * 1000 + missing * 1000000 + cleared * 1000000000;
}
function arrayObservations() {
    var values = [0, 0 / 0];
    return numberFlags(values[0]) + numberFlags(values[1]) * 1000 +
        numberFlags(values[2]) * 1000000;
}
function arrayNumberKey(key) {
    var values = [42, 99];
    return numberFlags(values[key]);
}
function arrayBooleanKey(key) {
    var values = [42, 99];
    return numberFlags(values[key]);
}
function fieldBeforeWrite(flag) {
    var object = {};
    if (flag) { object.value = 42; }
    return numberFlags(object.value);
}
function makeData() {
    const values = new Map();
    return {
        set(key, value) { values.set(key, value); return values.size; },
        get(key) {
            if (!values.has(key)) { return null; }
            return values.get(key);
        },
        remove(key) { return values.delete(key); }
    };
}
function retainedData() {
    const retained = makeData();
    retained.set("number", 42);
    retained.set("zero", 0);
    retained.set("nan", 0 / 0);
    for (var i = 0; i < 20; i = i + 1) {
        const temporary = makeData();
        temporary.set("number", i);
        temporary.get("number");
        temporary.remove("number");
    }
    const before = numberFlags(retained.get("number"));
    const zero = numberFlags(retained.get("zero"));
    const nan = numberFlags(retained.get("nan"));
    retained.remove("number");
    const missing = numberFlags(retained.get("number"));
    return before + zero * 1000 + nan * 1000000 + missing * 1000000000;
}

var number_undefined = numberFlags(forwardNumber(maybeNumber(-1)));
var number_null = numberFlags(forwardNumber(maybeNumber(0)));
var number_zero = numberFlags(forwardNumber(maybeNumber(1)));
var number_negative_zero = 1 / forwardNumber(maybeNumber(2));
var number_nan = numberFlags(forwardNumber(maybeNumber(3)));
var number_present = numberFlags(forwardNumber(maybeNumber(4)));
var boolean_undefined = booleanFlags(forwardBoolean(maybeBoolean(-1)));
var boolean_null = booleanFlags(forwardBoolean(maybeBoolean(0)));
var boolean_false = booleanFlags(forwardBoolean(maybeBoolean(1)));
var boolean_true = booleanFlags(forwardBoolean(maybeBoolean(2)));
var boolean_literal_false = booleanFlags(false);
var boolean_literal_true = booleanFlags(true);
var arithmetic_undefined = arithmetic(maybeNumber(-1));
var arithmetic_null = arithmetic(maybeNumber(0));
var arithmetic_nan = arithmetic(maybeNumber(3));
var arithmetic_number = arithmetic(maybeNumber(4));
var arithmetic_false = booleanArithmetic(maybeBoolean(1));
var arithmetic_true = booleanArithmetic(maybeBoolean(2));
var arithmetic_boolean_null = booleanArithmetic(maybeBoolean(0));
var arithmetic_boolean_undefined = booleanArithmetic(maybeBoolean(-1));
var ordering_null = ordering(maybeNumber(0));
var ordering_undefined = ordering(maybeNumber(-1));
var ordering_nan = ordering(maybeNumber(3));
var fallback_null = numberFlags(fallbackNumber(maybeNumber(0)));
var fallback_undefined = numberFlags(fallbackNumber(maybeNumber(-1)));
var fallback_zero = numberFlags(fallbackNumber(maybeNumber(1)));
var fallback_nan = numberFlags(fallbackNumber(maybeNumber(3)));
var fallback_false = booleanFlags(fallbackBoolean(maybeBoolean(1)));
var conditional_null = numberFlags(conditionalNumber(true));
var conditional_number = numberFlags(conditionalNumber(false));
var conditional_false = booleanFlags(conditionalBoolean(true));
var conditional_boolean_null = booleanFlags(conditionalBoolean(false));
var string_type = stringType("retained string");
var missing_argument = missingParameter();
var present_argument = missingParameter(42);
var local_undefined = localBeforeWrite(false);
var local_null = localBeforeWrite(true);
var loop_undefined = loopValue(0);
var loop_null = loopValue(1);
var loop_number = loopValue(3);
var map_tags = mapObservations();
var array_tags = arrayObservations();
var array_zero_key = arrayNumberKey(0);
var array_null_key = arrayNumberKey(null);
var array_undefined_key = arrayNumberKey(absent());
var array_false_key = arrayBooleanKey(false);
var array_true_key = arrayBooleanKey(true);
var array_boolean_null_key = arrayBooleanKey(null);
var array_boolean_undefined_key = arrayBooleanKey(absent());
var field_undefined = fieldBeforeWrite(false);
var field_number = fieldBeforeWrite(true);
var retained_data = retainedData();
