// getOwnPropertyNames on a get result lists all the object's fields in creation order.
//--- class-map-record-alias-snapshot-order.js
function class_map_record_alias_snapshot_order() {
    const values = new Map();
    class Item { constructor(n) { this.z = n; this.a = 9; } }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot");
    const names = Object.getOwnPropertyNames(saved);
    return names.length * 1000 + (names[0] === "z") * 100 + (names[1] === "a") * 10 + saved.z + item.a;
}
var a = class_map_record_alias_snapshot_order();

// getOwnPropertyNames(this) in a method called on a get result sees the original object.
//--- class-map-record-alias-snapshot-method.js
function class_map_record_alias_snapshot_method() {
    const values = new Map();
    class Item {
        constructor(n) { this.first = n; this.second = 9; }
        read() {
            const names = Object.getOwnPropertyNames(this);
            return names.length * 100 + (names[0] === "first") * 10 + this.first;
        }
    }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot");
    return saved.read() * 100 + item.first;
}
var a = class_map_record_alias_snapshot_method();

// Keep the dispose loop that nulls every field of the original object via a get result.
//--- class-map-record-alias-snapshot-dispose.js
function class_map_record_alias_snapshot_dispose() {
    const values = new Map();
    class Item {
        constructor(n) { this.first = n; this.second = 9; }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return (this.first === null) * 10 + (this.second === null);
        }
    }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot");
    const disposed = saved.dispose();
    return values.size * 1000 + disposed * 100 + (item.first === null) * 10 + (item.second === null);
}
var a = class_map_record_alias_snapshot_dispose();

// After overwrite and delete, each get result keeps its own field names and later writes.
//--- class-map-record-alias-snapshot-overwrite-delete.js
function class_map_record_alias_snapshot_overwrite_delete() {
    const values = new Map();
    class Item { constructor(n) { this.z = n; this.a = n + 1; } }
    const first = new Item(2), second = new Item(7);
    values.set("slot", first);
    const savedFirst = values.get("slot");
    values.set("slot", second);
    const savedSecond = values.get("slot");
    values.delete("slot");
    savedFirst.z = 5;
    savedSecond.a = 9;
    const firstNames = Object.getOwnPropertyNames(savedFirst);
    const secondNames = Object.getOwnPropertyNames(savedSecond);
    return firstNames.length * 10000 + secondNames.length * 1000 + (firstNames[0] === "z") * 100 + (secondNames[1] === "a") * 10 + first.z + second.a + values.size;
}
var a = class_map_record_alias_snapshot_overwrite_delete();

// An inherited method lists the Leaf instance's fields in order through a get result.
//--- class-map-record-alias-snapshot-inherited.js
function class_map_record_alias_snapshot_inherited() {
    const values = new Map();
    class Base {
        constructor(n) { this.z = n; this.a = n + 1; }
        read() {
            const names = Object.getOwnPropertyNames(this);
            return names.length * 100 + (names[0] === "z") * 10 + (names[1] === "a") + this.z;
        }
    }
    class Middle extends Base { constructor(n) { super(n); } }
    class Leaf extends Middle { constructor(n) { super(n); this.a = n + 4; } }
    const item = new Leaf(5);
    values.set("slot", item);
    const saved = values.get("slot");
    return saved.read() * 100 + item.a;
}
var a = class_map_record_alias_snapshot_inherited();

// Refused: a field added through a copy of a get result changes the original's field list.
//--- class-map-record-alias-snapshot-added.js
function class_map_record_alias_snapshot_added() {
    const values = new Map();
    class Item { constructor(n) { this.first = n; } }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot"), alias = saved;
    alias.extra = 9;
    const names = Object.getOwnPropertyNames(item);
    return names.length * 100 + saved.extra;
}
var a = class_map_record_alias_snapshot_added();

// Refused: a method on a get result adds a field, so the class's full field set must grow.
//--- class-map-record-alias-snapshot-method-added.js
function class_map_record_alias_snapshot_method_added() {
    const values = new Map();
    class Item {
        constructor(n) { this.first = n; }
        grow() { this.extra = 9; }
    }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot");
    saved.grow();
    const names = Object.getOwnPropertyNames(item);
    return names.length * 100 + saved.extra;
}
var a = class_map_record_alias_snapshot_method_added();

// Refused: deleting a field through a get result changes the original's field list.
//--- class-map-record-alias-snapshot-deleted.js
function class_map_record_alias_snapshot_deleted() {
    const values = new Map();
    class Item { constructor(n) { this.first = n; this.second = 9; } }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot");
    delete saved.first;
    const names = Object.getOwnPropertyNames(item);
    return names.length * 100 + (names[0] === "second") * 10 + saved.second;
}
var a = class_map_record_alias_snapshot_deleted();

// Refused: the constructor stores `this` in the Map before all its fields are written.
//--- class-map-record-alias-snapshot-constructor-publication.js
function class_map_record_alias_snapshot_constructor_publication() {
    const values = new Map();
    class Item {
        constructor(n) { this.first = n; values.set("slot", this); this.second = 9; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.count() * 100 + item.first;
}
var a = class_map_record_alias_snapshot_constructor_publication();
