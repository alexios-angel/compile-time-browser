// Number, Null and Undefined keys keep distinct tags through own-field reads.
//--- class-map-optional-key-tags.js
function class_map_optional_key_tags() {
    const values = new Map();
    class Shape {
        constructor(key, n) { this.key = key; values.set(key, n); }
        read() { return values.get(this.key); }
    }
    const zero = new Shape(0, 2), absent = new Shape(void 0, 3), empty = new Shape(null, 5);
    return zero.read() * 1000 + absent.read() * 100 + empty.read() * 10 + values.size;
}
var a = class_map_optional_key_tags();

// An Undefined key is an entry, while a missing get still returns Undefined.
//--- class-map-optional-key-missing.js
function class_map_optional_key_missing() {
    const values = new Map();
    class Shape {
        constructor(key) { this.key = key; values.set(key, 7); }
        read() { return values.get(this.key); }
        remove() { return values.delete(this.key); }
        has() { return values.has(this.key); }
    }
    const absent = new Shape(), zero = new Shape(0);
    const saved = absent.read(), had = absent.has(), removed = absent.remove();
    return saved * 100000 + had * 10000 + removed * 1000
        + (absent.read() === void 0) * 100 + zero.read() * 10 + values.size;
}
var a = class_map_optional_key_missing();

// Boolean keys cannot alias numerically equal Number keys.
//--- class-map-optional-key-booleans.js
function class_map_optional_key_booleans() {
    const values = new Map();
    class Shape {
        constructor(key, n) { this.key = key; values.set(key, n); }
        read() { return values.get(this.key); }
    }
    const zero = new Shape(0, 2), no = new Shape(false, 3);
    const one = new Shape(1, 5), yes = new Shape(true, 7);
    return zero.read() * 10000 + no.read() * 1000 + one.read() * 100
        + yes.read() * 10 + values.size;
}
var a = class_map_optional_key_booleans();

// Distinct NaN producers address one entry; saved reads survive overwrite/delete.
//--- class-map-optional-key-nan.js
function class_map_optional_key_nan() {
    const values = new Map();
    class Shape {
        constructor(key, n) { this.key = key; values.set(key, n); }
        read() { return values.get(this.key); }
        update(n) { values.set(this.key, n); }
        remove() { return values.delete(this.key); }
    }
    const first = new Shape(0 / 0, 2), second = new Shape(0 / 0, 7), finite = new Shape(5, 3);
    const saved = first.read();
    first.update(9);
    const changed = second.read(), removed = second.remove();
    return saved * 100000 + changed * 10000 + removed * 1000 + values.size * 100
        + (first.read() === void 0) * 10 + finite.read();
}
var a = class_map_optional_key_nan();

// Positive and negative zero remain one key through writes in both directions.
//--- class-map-optional-key-signed-zero.js
function class_map_optional_key_signed_zero() {
    const values = new Map();
    class Shape {
        constructor(key, n) { this.key = key; values.set(key, n); }
        read() { return values.get(this.key); }
        update(n) { values.set(this.key, n); }
        remove() { return values.delete(this.key); }
        has() { return values.has(this.key); }
    }
    const first = new Shape(-0, 2), second = new Shape(0, 7);
    const saved = first.read();
    first.update(9);
    const changed = second.read(), removed = second.remove();
    return saved * 10000 + changed * 1000 + removed * 100 + first.has() * 10 + values.size;
}
var a = class_map_optional_key_signed_zero();

// Inherited fields and Data helpers preserve the same optional numeric key.
//--- class-map-inherited-optional-key-helper.js
function class_map_inherited_optional_key_helper() {
    const values = new Map();
    const Data = {
        set: (key, n) => { values.set(key, n); },
        get: key => values.get(key)
    };
    class Base {
        constructor(key, n) { this.key = key; Data.set(key, n); }
        read() { return Data.get(this.key); }
    }
    class Leaf extends Base {
        constructor(key, n) { super(key, n); }
        update(n) { Data.set(this.key, n); return Data.get(this.key); }
    }
    const first = new Leaf(7, 2), second = new Base(3, 5);
    const saved = first.read(), changed = first.update(9);
    return saved * 10000 + changed * 1000 + first.read() * 100
        + second.read() * 10 + values.size;
}
var a = class_map_inherited_optional_key_helper();

// Finite scalar tags do not authorize a mixed String/Number key carrier.
//--- class-map-optional-key-mixed-string.js
function class_map_optional_key_mixed_string() {
    const values = new Map();
    class Shape {
        constructor(key, n) { this.key = key; values.set(key, n); }
        read() { return values.get(this.key); }
    }
    const text = new Shape("7", 3), number = new Shape(7, 5);
    return text.read() * 100 + number.read() * 10 + values.size;
}
var a = class_map_optional_key_mixed_string();

// An optional object key needs identity ownership beyond the scalar carrier.
//--- class-map-optional-key-object.js
function class_map_optional_key_object() {
    const values = new Map();
    class Shape {
        constructor(key, n) { this.key = key; values.set(key, n); }
        read() { return values.get(this.key); }
    }
    const object = new Shape({}, 3), absent = new Shape(void 0, 5);
    return object.read() * 100 + absent.read() * 10 + values.size;
}
var a = class_map_optional_key_object();

// Optional keys cannot be exposed through the existing homogeneous snapshots.
//--- class-map-optional-key-snapshot.js
function class_map_optional_key_snapshot() {
    const values = new Map();
    class Shape {
        constructor(key, n) { this.key = key; values.set(key, n); }
        read() { return values.get(this.key); }
    }
    const absent = new Shape(void 0, 3), number = new Shape(7, 5);
    const keys = Array.from(values.keys());
    return absent.read() * 1000 + number.read() * 100
        + (keys[0] === void 0) * 10 + keys[1];
}
var a = class_map_optional_key_snapshot();
