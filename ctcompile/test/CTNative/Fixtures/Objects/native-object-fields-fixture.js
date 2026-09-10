// Scalar component fields survive the exact Bootstrap getter and Map removal.
// UMD publication and console effects are measured by separate vendor probes.
function absent() { return; }
function makeInstance(value) { return {value: value, active: true}; }
function retain(instance) { return function () { return +instance.value; }; }
function write(instance, value) { instance.value = value; return +instance.value; }
function readGuarded(saved, expected) {
    if (saved === expected) {
        saved.value = 23;
        return +saved.value;
    }
    return 0;
}
function readNegated(saved, expected) {
    if (saved !== expected) { return 0; }
    else { return +saved.value; }
}
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
function lifetime() {
    const data = makeData();
    const element = {};
    const first = makeInstance(7);
    const second = makeInstance(11);
    const readSaved = retain(first);
    data.set(element, "bs.alert", first);
    const saved = data.get(element, "bs.alert");
    traceGuarded = readGuarded(saved, first);
    traceAlias = +first.value;
    traceNegatedGuard = readNegated(saved, first);
    traceDistinct = +second.value;
    traceMissing = +(first.missing === absent());
    traceBoolean = +(first.active === true);
    first.active = false;
    traceFalse = +(first.active === false);
    first.value = null;
    traceNull = +(first.value === null);
    first.value = absent();
    traceUndefined = +(first.value === absent());
    first.value = true;
    traceTag = +(first.value !== 1 && first.value === true);
    first.value = -0;
    traceNegativeZero = +(1 / first.value === -1 / 0);
    first.value = 0 / 0;
    traceNaN = +(first.value !== first.value);
    first["x-y"] = 9;
    first[""] = 10;
    first["class"] = 12;
    traceEncoded = +first["x-y"] + +first[""] + +first["class"];
    traceWrite = write(first, 31);
    data.set(element, "bs.alert", second);
    traceRetained = readSaved();
    traceOldGuard = readGuarded(saved, first);
    traceReplaced = +(data.get(element, "bs.alert") === second);
    data.remove(element, "bs.alert");
    traceRemoved = +(data.get(element, "bs.alert") === null);
    traceRetainedRemoved = readSaved();
    data.set(element, "bs.alert", 5);
    traceScalarGuard = readGuarded(data.get(element, "bs.alert"), first);
    traceNegatedScalar = readNegated(data.get(element, "bs.alert"), first);
    traceAbsentGuard = readGuarded(data.get(element, "missing"), first);
    first.value = 42;
    return readSaved();
}
function optionalLookup() {
    const map = new Map();
    const instance = makeInstance(5);
    map.set("instance", instance);
    const saved = map.get("instance");
    map.clear();
    if (saved) { saved.value = 19; traceOptional = +saved.value; }
    const missing = map.get("missing");
    traceWrongArm = 0;
    if (missing) { traceWrongArm = +missing.value; }
    return +instance.value;
}
function separateInstances() {
    const map = new Map();
    const first = {};
    const second = {value: 13};
    map.set("first", first);
    map.set("second", second);
    traceAbsentOther = +(first.value === absent());
    first.value = 17;
    traceSeparateSecond = +second.value;
    return +first.value;
}
lifetime42 = lifetime();
optional19 = optionalLookup();
separate17 = separateInstances();
