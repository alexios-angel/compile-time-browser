// The exact Bootstrap getter with owning component identities and scalar payloads.
// Fieldful components and the vendor's UMD/console paths remain separate gates.
function makeInstance() { return {}; }
function absent() { return; }
function forward(value) { return value; }
function choose(flag, instance) { return flag ? instance : null; }
function chooseReturn(flag, instance) {
    if (flag) { return instance; }
    return false;
}
function retain(value) { return function () { return value; }; }
function makeData() {
    const t = new Map();
    return {
        set(e, i, n) {
            t.has(e) || t.set(e, new Map());
            t.get(e).set(i, n);
            return 1;
        },
        get: (e, i) => t.has(e) && t.get(e).get(i) || null,
        remove(e, i) {
            if (!t.has(e)) { return; }
            const n = t.get(e);
            n.delete(i);
            0 === n.size && t.delete(e);
        }
    };
}
function dataLifetime() {
    const first = makeInstance();
    const second = makeInstance();
    const element = {};
    const other = {};
    const data = makeData();
    const independent = makeData();
    traceAbsent = +(data.get(element, "bs.alert") === null);
    traceDistinct = +(first !== second);
    traceDefiniteType = +(typeof first === "object");
    traceDefiniteTruthy = first ? 1 : 0;
    data.set(element, "bs.alert", first);
    data.set(other, "bs.alert", second);
    independent.set(element, "bs.alert", second);
    const saved = forward(data.get(element, "bs.alert"));
    const readSaved = retain(saved);
    traceAlias = +(saved === first);
    traceOther = +(data.get(other, "bs.alert") === second);
    traceIndependent = +(independent.get(element, "bs.alert") === second);
    data.set(element, "bs.alert", second);
    traceReplacement = +(data.get(element, "bs.alert") === second);
    traceSavedReplacement = +(saved === first);
    data.remove(element, "bs.alert");
    traceRemoved = +(data.get(element, "bs.alert") === null);
    traceSavedRemoved = +(readSaved() === first);
    data.set(element, "bs.collapse", 42);
    traceScalar = +(data.get(element, "bs.collapse") === 42);
    traceOldKey = +(data.get(element, "bs.alert") === null);
    data.set(element, "bs.collapse", choose(true, first));
    traceReinserted = +(data.get(element, "bs.collapse") === first);
    data.set(element, "bs.collapse", choose(false, second));
    traceNullFallback = +(data.get(element, "bs.collapse") === null);
    data.set(element, "bs.collapse", chooseReturn(false, second));
    traceFalseFallback = +(data.get(element, "bs.collapse") === null);
    data.set(element, "bs.collapse", chooseReturn(true, second));
    traceTrueBranch = +(data.get(element, "bs.collapse") === second);
    data.set(element, "bs.collapse", 0);
    traceZeroFallback = +(data.get(element, "bs.collapse") === null);
    data.set(element, "bs.collapse", -0);
    traceNegativeZeroFallback = +(data.get(element, "bs.collapse") === null);
    data.set(element, "bs.collapse", 0 / 0);
    traceNaNFallback = +(data.get(element, "bs.collapse") === null);
    data.set(element, "bs.collapse", true);
    traceTrueValue = +(data.get(element, "bs.collapse") === true);
    for (var i = 0; i < 50; ++i) {
        const temporary = makeData();
        temporary.set(element, "bs.alert", makeInstance());
        temporary.remove(element, "bs.alert");
    }
    return saved === first && readSaved() === first && saved !== second ? 42 : 0;
}
function rawValues() {
    const map = new Map();
    const instance = makeInstance();
    map.set("instance", instance);
    map.set("number", 42);
    map.set("zero", 0);
    map.set("negativeZero", -0);
    map.set("nan", 0 / 0);
    map.set("infinity", 1 / 0);
    map.set("true", true);
    map.set("false", false);
    map.set("null", null);
    map.set("undefined", absent());
    const saved = map.get("instance");
    traceRawIdentity = +(saved === instance);
    traceRawTruthy = map.get("instance") ? 1 : 0;
    traceRawType = +(typeof map.get("instance") === "object");
    traceNumberType = +(typeof map.get("number") === "number");
    traceBooleanType = +(typeof map.get("true") === "boolean");
    traceMissingType = +(typeof map.get("missing") === "undefined");
    traceNullType = +(typeof map.get("null") === "object");
    traceTagDifference = +(map.get("true") !== 1);
    traceNullUndefined = +(map.get("null") !== map.get("undefined"));
    traceMissingUndefined = +(map.get("missing") === absent());
    traceLooseAbsence = +(map.get("null") == null && map.get("undefined") == null);
    traceObjectNotNull = +(map.get("instance") != null);
    traceNaN = +(map.get("nan") !== map.get("nan"));
    traceSignedZero = +(map.get("negativeZero") === map.get("zero"));
    traceFalsy = +(!map.get("zero") && !map.get("negativeZero") && !map.get("nan") &&
                  !map.get("false") && !map.get("null") && !map.get("undefined"));
    traceTruthy = +(!!map.get("infinity") && !!map.get("true") && !!map.get("number"));
    map.clear();
    traceSavedClear = +(saved === instance && map.get("instance") === absent());
    map.set("instance", makeInstance());
    traceFreshAfterClear = +(map.get("instance") !== saved);
    return saved === instance ? 42 : 0;
}
function loopValue(limit) {
    const map = new Map();
    const instance = makeInstance();
    map.set("instance", instance);
    var value = instance;
    for (var i = 0; i < limit; ++i) {
        value = i % 2 === 0 ? null : map.get("instance");
    }
    return value === instance ? 1 : (value === null ? 2 : 0);
}
// A definite identity also needs a typed inert slot on the loop's exit arm.
function identityLoop(limit) {
    const map = new Map();
    const original = {};
    map.set("instance", original);
    var value = original;
    for (var i = 0; i < limit; ++i) {
        value = {};
        map.set("instance", value);
    }
    return value === original ? 1 : 0;
}
lifetime42 = dataLifetime();
raw42 = rawValues();
loopZero = loopValue(0);
loopOne = loopValue(1);
loopTwo = loopValue(2);
loopThree = loopValue(3);
identityZero = identityLoop(0);
identityMany = identityLoop(2);
