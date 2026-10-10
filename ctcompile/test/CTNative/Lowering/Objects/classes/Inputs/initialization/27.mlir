// Two Maps hold the same instance; clearing one leaves both get results usable.
//--- class-map-record-shared-owner.js
function class_map_record_shared_owner() {
    const left = new Map(), right = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(3);
    left.set("left", item);
    right.set("right", item);
    const savedLeft = left.get("left"), savedRight = right.get("right");
    left.clear();
    savedLeft.n = 8;
    const current = right.get("right");
    return item.n * 1000 + savedRight.n * 100 + current.n * 10 + right.size;
}
var a = class_map_record_shared_owner();

// After the entry is replaced, a method on the original and a get result write one object.
//--- class-map-record-owner-method-write.js
function class_map_record_owner_method_write() {
    const values = new Map();
    class Item {
        constructor(n) { this.n = n; }
        add(amount) { this.n = this.n + amount; }
    }
    const first = new Item(2), second = new Item(7);
    values.set("slot", first);
    const saved = values.get("slot");
    values.set("slot", second);
    first.add(3);
    saved.n = saved.n + 4;
    second.add(1);
    const current = values.get("slot");
    return saved.n * 100 + first.n * 10 + current.n;
}
var a = class_map_record_owner_method_write();

// Delete and reinsert of one key: each earlier get result keeps its own object.
//--- class-map-record-reinsert-aliases.js
function class_map_record_reinsert_aliases() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(5);
    values.set("slot", first);
    const savedFirst = values.get("slot");
    values.delete("slot");
    values.set("slot", second);
    const savedSecond = values.get("slot");
    values.delete("slot");
    values.set("slot", first);
    savedFirst.n = 4;
    savedSecond.n = 7;
    const current = values.get("slot");
    return first.n * 1000 + second.n * 100 + current.n * 10 + values.size;
}
var a = class_map_record_reinsert_aliases();

// Refused: get after clear returns undefined even though the instance still exists.
//--- class-map-record-read-after-clear.js
function class_map_record_read_after_clear() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    values.clear();
    return (values.get("slot") === void 0) * 100 + item.n;
}
var a = class_map_record_read_after_clear();

// Refused: returning a get result lets it outlive the function that created the instance.
//--- class-map-record-returned-alias.js
function class_map_record_returned_alias() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot");
    values.clear();
    return saved;
}
var returned = class_map_record_returned_alias();
returned.n = 9;
var a = returned.n;

// Refused: a returned closure capturing a get result outlives the function owning it.
//--- class-map-record-captured-alias.js
function class_map_record_captured_alias() {
    const values = new Map();
    class Item { constructor(n) { this.n = n; } }
    const item = new Item(7);
    values.set("slot", item);
    const saved = values.get("slot");
    values.clear();
    return function readSaved() { return saved.n; };
}
var read = class_map_record_captured_alias();
var a = read();
