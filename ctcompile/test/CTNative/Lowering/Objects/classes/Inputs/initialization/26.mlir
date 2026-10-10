// Maps whose entries are class instances: get returns the very object that was set.
//--- class-map-record-direct.js
function class_map_record_direct() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot");
    return saved.n * 100 + item.n;
}
var a = class_map_record_direct();

// Overwriting an entry does not change the object an earlier get returned.
//--- class-map-record-overwrite.js
function class_map_record_overwrite() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    values.set("slot", first);
    const saved = values.get("slot");
    values.set("slot", second);
    saved.n = 9;
    const current = values.get("slot");
    return saved.n * 100 + first.n * 10 + current.n;
}
var a = class_map_record_overwrite();

// Deleting the entry leaves the object returned by an earlier get alive and writable.
//--- class-map-record-delete.js
function class_map_record_delete() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(3);
    values.set("slot", item);
    const saved = values.get("slot");
    const removed = values.delete("slot");
    saved.n = 5;
    return saved.n * 100 + item.n * 10 + removed * 2 + values.has("slot");
}
var a = class_map_record_delete();

// clear() empties the Map but both instances and the saved get result stay alive.
//--- class-map-record-clear.js
function class_map_record_clear() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(4), second = new Item(6);
    values.set("left", first);
    values.set("right", second);
    const saved = values.get("left");
    values.clear();
    return saved.n * 100 + second.n * 10 + values.size;
}
var a = class_map_record_clear();

// Two entries hold distinct instances of the same class with different field values.
//--- class-map-record-payloads.js
function class_map_record_payloads() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    values.set("left", first);
    values.set("right", second);
    const left = values.get("left"), right = values.get("right");
    return left.n * 100 + right.n * 10 + values.size;
}
var a = class_map_record_payloads();

// Writes through a copy of a get result reach the original object and a later get.
//--- class-map-record-alias-write.js
function class_map_record_alias_write() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(3);
    values.set("slot", item);
    const saved = values.get("slot"), alias = saved;
    alias.n = 8;
    const current = values.get("slot");
    return item.n * 100 + saved.n * 10 + current.n;
}
var a = class_map_record_alias_write();

// Refused: the instance is created inside an `if` block, so it may not outlive the Map read.
//--- class-map-record-region-owner.js
function class_map_record_region_owner(flag) {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    if (flag) {
        const item = new Item(7);
        values.set("slot", item);
    }
    const saved = values.get("slot");
    return saved.n;
}
var a = class_map_record_region_owner(true);

// Refused: get of a missing key would need an "instance or undefined" result type.
//--- class-map-record-missing.js
function class_map_record_missing() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    const absent = values.get("missing");
    return (absent === void 0) * 100 + item.n;
}
var a = class_map_record_missing();

// Refused: one Map holding instances of two classes with different fields.
//--- class-map-record-mixed-constructors.js
function class_map_record_mixed_constructors() {
    const values = new Map();
    class Left { constructor(n) { this.n = n; } }
    class Right { constructor(n) { this.m = n; } }
    const left = new Left(2), right = new Right(7);
    values.set("left", left);
    values.set("right", right);
    const first = values.get("left"), second = values.get("right");
    return first.n * 100 + second.m;
}
var a = class_map_record_mixed_constructors();

// Refused: one Map holding both an instance and a Number.
//--- class-map-record-mixed-scalar.js
function class_map_record_mixed_scalar() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("record", item);
    values.set("number", 3);
    const saved = values.get("record");
    return saved.n * 100 + values.get("number");
}
var a = class_map_record_mixed_scalar();

// Refused: the Map escapes into a global and would outlive the instance it holds.
//--- class-map-record-map-escaped.js
var retained;
function class_map_record_map_escaped() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    retained = values;
    return item.n;
}
var a = class_map_record_map_escaped();

// Refused: the get result escapes into a global and would outlive the instance.
//--- class-map-record-alias-escaped.js
var retained;
function class_map_record_alias_escaped() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot");
    retained = saved;
    return item.n;
}
var a = class_map_record_alias_escaped();

// Refused: Map.prototype.get is replaced, even though `values` is a real Map.
//--- class-map-record-prototype-replaced.js
function class_map_record_prototype_replaced() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    Map.prototype.get = function replacement(key) { return {n: 9}; };
    const saved = values.get("slot");
    return saved.n;
}
var a = class_map_record_prototype_replaced();

// The constructor stores `this` in the Map; exceptions and reentry need separate checks.
//--- class-map-record-constructor-publication.js
function class_map_record_constructor_publication() {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + item.n;
}
var a = class_map_record_constructor_publication();

// Refused: get after delete returns undefined even though the key existed before.
//--- class-map-record-read-after-delete.js
function class_map_record_read_after_delete() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    values.delete("slot");
    return (values.get("slot") === void 0) * 100 + item.n;
}
var a = class_map_record_read_after_delete();

// Refused: a key passed in as a parameter is not a known string literal.
//--- class-map-record-computed-key.js
function class_map_record_computed_key(key) {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set(key, item);
    const saved = values.get(key);
    return saved.n;
}
var a = class_map_record_computed_key("slot");

// Refused: reading a static getter through the get result's `constructor` is not supported.
//--- class-map-record-alias-constructor.js
function class_map_record_alias_constructor() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } static get VALUE() { return 7; } }
    const item = new Item(3);
    values.set("slot", item);
    const saved = values.get("slot");
    return saved.constructor.VALUE;
}
var a = class_map_record_alias_constructor();

// Calling a method on a get result needs every call site checked, not just the fields.
//--- class-map-record-alias-method.js
function class_map_record_alias_method() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } read() { return this.n; } }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot");
    return saved.read();
}
var a = class_map_record_alias_method();

// Refused: writing an own `read` field through a get result hides the class method.
//--- class-map-record-alias-shadow.js
function class_map_record_alias_shadow() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } read() { return this.n; } }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot");
    saved.read = 9;
    return item.n;
}
var a = class_map_record_alias_shadow();

// Refused: overwriting `constructor` through a get result hides the folded static getter.
//--- class-map-record-alias-constructor-write.js
function class_map_record_alias_constructor_write() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } static get VALUE() { return 7; } }
    const item = new Item(3);
    values.set("slot", item);
    const saved = values.get("slot");
    saved.constructor = {VALUE: 9};
    return item.constructor.VALUE;
}
var a = class_map_record_alias_constructor_write();

// Refused: a field added through a get result must count in getOwnPropertyNames.
//--- class-map-record-alias-snapshot-write.js
function class_map_record_alias_snapshot_write() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot");
    saved.extra = 9;
    return Object.getOwnPropertyNames(item).length;
}
var a = class_map_record_alias_snapshot_write();
