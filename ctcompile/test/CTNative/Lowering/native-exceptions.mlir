// RUN: split-file %s %t
// RUN: python3 %S/native-exceptions.py --translate ctjs-translate --opt ctjs-opt --fixtures %t --work %t.executables

// Source programs retain runtime branches and both normal and throwing calls.
// The checker compares independent Node and interpreter results with explicit
// and deduced standalone C++ under GCC and Clang. The main specimen has exactly
// two source functions, including its script entry; neither may be omitted.

//--- guarded.js
function guarded(flag) {
    var mark = 0;
    try {
        mark = 10;
        if (flag) { throw 32; }
        mark = 20;
    } catch (value) {
        return mark + value;
    }
    return mark;
}
var caught42 = guarded(true);
var normal20 = guarded(false);

//--- two_sites.js
function twoSites(mode) {
    var mark = 1;
    try {
        mark = 10;
        if (mode === 1) { throw 32; }
        mark = 100;
        if (mode === 2) { throw 7; }
        mark = 20;
    } catch (value) {
        return mark + value;
    }
    return mark;
}
var first42 = twoSites(1);
var normal20 = twoSites(0);
var second107 = twoSites(2);

//--- continuation.js
function continued(flag) {
    var mark = 1;
    try {
        mark = 10;
        if (flag) { throw 3; }
        mark = 20;
    } catch (value) {
        mark = mark + value;
    }
    return mark + 1;
}
var caught14 = continued(true);
var normal21 = continued(false);

//--- normal_return.js
function earlyReturn(flag) {
    try {
        if (flag) { return 7; }
        throw 9;
    } catch (value) {
        return value + 1;
    }
}
var caught10 = earlyReturn(false);
var normal7 = earlyReturn(true);

//--- unconditional.js
function unconditional() {
    try {
        throw 32;
    } catch (value) {
        value = value + 1;
        return value;
    }
}
var caught33 = unconditional();

//--- boolean_state.js
function booleanState(flag) {
    var mark = false;
    try {
        mark = flag;
        if (flag) { throw 42; }
        return 0;
    } catch (value) {
        return mark ? value : -1;
    }
}
var caught42 = booleanState(true);
var normal0 = booleanState(false);

//--- finally_override.js
function overridden(flag) {
    // The bytecode's unconditional override is equivalent to the supported
    // catch/normal completion. This does not require general finally support.
    try {
        if (flag) { throw 32; }
        return 42;
    } finally {
        return 7;
    }
}
var caught7 = overridden(true);
var normal7 = overridden(false);

//--- object_payload.js
function objectPayload() {
    try {
        throw {value: 32};
    } catch (value) {
        return value.value;
    }
}
var object32 = objectPayload();

//--- boolean_payload.js
function booleanPayload() {
    try { throw true; } catch (ignored) { return 17; }
}
var boolean17 = booleanPayload();

//--- string_payload.js
function stringPayload() {
    try { throw "32"; } catch (ignored) { return 18; }
}
var string18 = stringPayload();

//--- boolean_value.js
function booleanValue(flag) {
    try {
        if (flag) { throw true; }
        throw false;
    } catch (value) {
        if (typeof value !== "boolean" || value === 1 || value === 0) { return -1; }
        return value ? 17 : 19;
    }
}
var true17 = booleanValue(true);
var false19 = booleanValue(false);

//--- string_value.js
function stringValue(flag) {
    var mark = "entry: ";
    try {
        mark = "throw: ";
        if (flag) {
            // A separate assignment preserves the importer's complete
            // register vector before the throw-only block.
            var payload = "owning payload that exceeds small string storage and survives the C++ handler " + "tail\u0000end";
            throw payload;
        }
        return "normal";
    } catch (value) {
        var saved = value;
        value = "replaced catch binding";
        return mark + saved;
    }
}
function inspectString(flag) {
    var saved = stringValue(flag);
    var churn = stringValue(!flag);
    var expected = "throw: owning payload that exceeds small string storage and survives the C++ handler tail\u0000end";
    // Score both exact comparisons without introducing an unrelated
    // multi-return CFG in the observer. A broken saved/churn value yields 0.
    var caught = (saved === expected) * (churn === "normal");
    var normal = (saved === "normal") * (churn === expected);
    return flag * caught * 42 + (!flag) * normal * 20;
}
var caught42 = inspectString(true);
var normal20 = inspectString(false);

//--- string_sites.js
function stringSites(mode) {
    var mark = "entry";
    try {
        mark = "first";
        if (mode === 1) { throw ""; }
        mark = "second";
        if (mode === 2) { throw "payload\u0000\uD83D\uDE00\uD800"; }
        return 20;
    } catch (value) {
        if (typeof value !== "string") { return -1; }
        if (!value) { return mark === "first" ? 42 : -1; }
        return value === "payload\u0000\uD83D\uDE00\uD800" && mark === "second" ? 107 : -1;
    }
}
var first42 = stringSites(1);
var second107 = stringSites(2);
var normal20 = stringSites(0);

//--- numeric_bits.js
function numericBits(flag) {
    try {
        var value = -0;
        if (flag) { throw value; }
        value = 0 / 0;
        throw value;
    } catch (value) {
        if (value !== value) { return 43; }
        return 1 / value === -1 / 0 ? 42 : -1;
    }
}
var negativeZero42 = numericBits(true);
var nan43 = numericBits(false);

//--- null_payload.js
function nullPayload() {
    try { throw null; } catch (ignored) { return 19; }
}
var null19 = nullPayload();

//--- undefined_payload.js
function undefinedPayload() {
    // Use the primitive directly, without a lookup of a writable global name.
    try { throw void 0; } catch (ignored) { return 20; }
}
var undefined20 = undefinedPayload();

//--- implicit_property.js
function implicitProperty(flag) {
    try {
        if (flag) { throw 32; }
        var bad = null;
        return bad.x;
    } catch (value) {
        return 42;
    }
}
var explicit42 = implicitProperty(true);
var implicit42 = implicitProperty(false);

//--- mixed_payload.js
function mixedPayload(flag) {
    try {
        if (flag) { throw 32; }
        throw true;
    } catch (ignored) {
        return 42;
    }
}
var first42 = mixedPayload(true);
var second42 = mixedPayload(false);

//--- mixed_string_payload.js
function mixedStringPayload(flag) {
    try {
        if (flag) { throw "32"; }
        throw 32;
    } catch (ignored) {
        return 42;
    }
}
var first42 = mixedStringPayload(true);
var second42 = mixedStringPayload(false);

//--- mixed_boolean_string_payload.js
function mixedBooleanStringPayload(flag) {
    try {
        if (flag) { throw "true"; }
        throw true;
    } catch (ignored) {
        return 42;
    }
}
var first42 = mixedBooleanStringPayload(true);
var second42 = mixedBooleanStringPayload(false);

//--- computed_throw.js
function computedThrow() {
    // The importer leaves the addition inside the throw block, without a
    // following complete register vector. Keep this structural refusal.
    try { throw "a" + "b"; } catch (value) { return value === "ab" ? 42 : -1; }
}
var computed42 = computedThrow();

//--- mixed_concatenation.js
function mixedConcatenation(flag) {
    try {
        // A completed computation can update the scratch register before
        // its assignment check. Recovery still needs operation admission;
        // primitive support does not authorize string/number coercion.
        var payload = "value:" + 42;
        if (flag) { throw payload; }
        return 20;
    } catch (value) {
        return value === "value:42" ? 42 : -1;
    }
}
var caught42 = mixedConcatenation(true);
var normal20 = mixedConcatenation(false);

//--- bare_finally.js
function bareFinally(flag) {
    var mark = 1;
    try {
        if (flag) { return 10; }
        mark = 20;
    } finally {
        mark = mark + 100;
    }
    return mark;
}
var normal120 = bareFinally(false);
var returned10 = bareFinally(true);

//--- catch_finally.js
function catchFinally() {
    var mark = 0;
    try {
        throw 1;
    } catch (value) {
        mark = value + 20;
    } finally {
        mark = mark + 100;
    }
    return mark;
}
var finally121 = catchFinally();

//--- nested_catch.js
function nestedCatch() {
    try {
        try {
            throw 4;
        } catch (value) {
            throw value + 1;
        }
    } catch (value) {
        return value + 2;
    }
}
var nested7 = nestedCatch();

//--- throwing_callee.js
function fail() { throw 32; }
function callThrower() {
    var mark = 10;
    try {
        // A failed right-hand side must not assign a normal return value.
        mark = fail();
    } catch (value) {
        return mark + value;
    }
    return mark;
}
var called42 = callThrower();
