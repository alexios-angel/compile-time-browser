// A non-mutating method called on a get result sees the original object's fields.
//--- class-map-record-alias-method-read.js
function class_map_record_alias_method_read() {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; }
        read() { return this.n + 1; }
    }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot");
    return saved.read() * 100 + item.n;
}
var a = class_map_record_alias_method_read();

// A method called on a get result mutates the original object, seen by a later get.
//--- class-map-record-alias-method-write.js
function class_map_record_alias_method_write() {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; }
        add(amount) { this.n = this.n + amount; return this.n; }
    }
    const item = new Item(3);
    values.set("slot", item);
    const saved = values.get("slot");
    const result = saved.add(5);
    const current = values.get("slot");
    return result * 100 + item.n * 10 + current.n;
}
var a = class_map_record_alias_method_write();

// After overwrite and delete, methods on each saved get result use that result's object.
//--- class-map-record-alias-method-overwrite-delete.js
function class_map_record_alias_method_overwrite_delete() {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; }
        add(amount) { this.n = this.n + amount; return this.n; }
    }
    const first = new Item(2), second = new Item(7);
    values.set("slot", first);
    const savedFirst = values.get("slot");
    values.set("slot", second);
    const savedSecond = values.get("slot");
    values.delete("slot");
    const left = savedFirst.add(3), right = savedSecond.add(4);
    return left * 10000 + right * 100 + first.n * 10 + second.n + values.size;
}
var a = class_map_record_alias_method_overwrite_delete();

// An inherited method called on a get result runs with the Leaf instance as `this`.
//--- class-map-record-alias-method-inherited.js
function class_map_record_alias_method_inherited() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; }
        read() { return this.n + 2; }
    }
    class Middle extends Base { constructor(n) { super(n); } }
    class Leaf extends Middle { constructor(n) { super(n); } }
    const item = new Leaf(5);
    values.set("slot", item);
    const saved = values.get("slot");
    return saved.read() * 100 + item.n * 10 + values.size;
}
var a = class_map_record_alias_method_inherited();

// Two instances of one class share method code but keep separate `this` objects.
//--- class-map-record-alias-method-distinct.js
function class_map_record_alias_method_distinct() {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; }
        add(amount) { this.n = this.n + amount; }
        read() { return this.n; }
    }
    const first = new Item(2), second = new Item(5);
    values.set("left", first);
    values.set("right", second);
    const left = values.get("left"), right = values.get("right");
    left.add(4);
    right.add(7);
    return left.read() * 10000 + right.read() * 100 + first.n * 10 + second.n;
}
var a = class_map_record_alias_method_distinct();

// Refused: assigning `read` on a get result replaces the method for the original too.
//--- class-map-record-alias-method-overwritten.js
function class_map_record_alias_method_overwritten() {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; }
        read() { return this.n; }
    }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot");
    saved.read = function replacement() { return 9; };
    return saved.read() * 100 + item.read();
}
var a = class_map_record_alias_method_overwritten();

// Refused: `saved.read` is taken as a value and escapes, not called on its object.
//--- class-map-record-alias-method-extracted.js
var escaped;
function class_map_record_alias_method_extracted() {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; }
        read() { return this.n; }
    }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot");
    escaped = saved.read;
    return item.n;
}
var a = class_map_record_alias_method_extracted();

// Refused: a method that stores `this` in a global lets the object outlive its owner.
//--- class-map-record-alias-method-receiver-escape.js
var retained;
function class_map_record_alias_method_receiver_escape() {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; }
        publish() { retained = this; return this; }
    }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot");
    const result = saved.publish();
    return result.n * 100 + retained.n;
}
var a = class_map_record_alias_method_receiver_escape();

// The constructor stores `this` in the Map before construction ends; a method reads it.
//--- class-map-record-alias-method-constructor-publication.js
function class_map_record_alias_method_constructor_publication() {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; values.set("slot", this); }
        read() { return this.n; }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.read() * 100 + item.n;
}
var a = class_map_record_alias_method_constructor_publication();
