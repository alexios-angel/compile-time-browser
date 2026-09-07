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
