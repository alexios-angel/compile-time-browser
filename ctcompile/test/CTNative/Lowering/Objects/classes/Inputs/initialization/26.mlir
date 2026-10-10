// Preparation preserves completed record registrations; native storage is a separate proof.
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

// Replacing an entry must not change the record already returned by get.
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

// Deletion removes the entry without ending the saved record's owner lifetime.
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

// Clearing every Map entry leaves both entry-frame owners and the saved alias alive.
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

// One constructor schema can carry distinct payload values in independent entries.
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

// Writes through a copied get result reach the original record and a later get.
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

// A branch-local construction cannot supply the enclosing Map's borrowed owner.
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

// A missing get requires an optional record carrier beyond a present-record borrow.
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

// Different constructors and field layouts cannot share one proved record schema.
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

// A scalar write prevents the complete producer census from proving record payloads.
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

// Publishing the Map would let its borrowed payload outlive the entry-frame owner.
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

// A saved record published outside the entry needs its own lifetime proof.
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

// Declared standard Map identity does not authorize a replaced prototype operation.
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

// Constructor-time publication still needs an exception and reentry lifetime proof.
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

// Membership must be re-established after deletion, even for an existing key.
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

// A computed key needs identity/presence evidence beyond literal string equality.
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

// Saved receiver constructor/getter identity is deliberately a separate proof.
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

// Saved receiver methods need a complete call-site proof in addition to fields.
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

// A saved alias cannot write an own field over a class method.
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

// Alias writes cannot hide a shadow of a constructor whose getter would be folded.
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

// The own-key snapshot must include a field added through a saved Map alias.
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
