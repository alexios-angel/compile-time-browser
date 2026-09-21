// Exact outer object identities; equal shapes do not imply equal Map keys.
//--- class-map-record-nested-object-holder.js
function probe() {
    const t = new Map,
        e = {
            set(e, i, n) {
                t.has(e) || t.set(e, new Map);
                const s = t.get(e);
                s.has(i) || 0 === s.size ? s.set(i, n) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(s.keys())[0]}.`)
            },
            get: (e, i) => t.has(e) && t.get(e).get(i) || null,
            remove(e, i) {
                if (!t.has(e)) return;
                const n = t.get(e);
                n.delete(i), 0 === n.size && t.delete(e)
            }
        };
    const element = {}, alias = element;
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    e.set(element, "bs.item", first);
    const savedChild = t.get(alias), savedFirst = e.get(element, "bs.item");
    const missing = e.get(element, "missing");
    e.remove(element, "bs.item");
    const absent = e.get(element, "bs.item");
    e.set(element, "bs.item", second);
    savedFirst.n = 5;
    second.n = 9;
    const savedMissing = savedChild.get("late");
    savedChild.set("late", first);
    const absentSnapshot = savedMissing || null;
    const count = savedChild.has("missing") ? 31 : 5 + second.n;
    return savedFirst.n * 1000 + e.get(element, "bs.item").n * 100
        + (missing === null) * 10 + (absent === null) + savedChild.size + t.size
        + (absentSnapshot === null) * 10000 + count;
}
var a = probe();


//--- class-map-record-nested-object-distinct.js
function probe() {
    const t = new Map,
        e = {
            set(e, i, n) {
                t.has(e) || t.set(e, new Map);
                const s = t.get(e);
                s.has(i) || 0 === s.size ? s.set(i, n) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(s.keys())[0]}.`)
            },
            get: (e, i) => t.has(e) && t.get(e).get(i) || null,
            remove(e, i) {
                if (!t.has(e)) return;
                const n = t.get(e);
                n.delete(i), 0 === n.size && t.delete(e)
            }
        };
    const element = {}, other = {}, alias = element;
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    e.set(element, "bs.item", first);
    e.set(other, "bs.item", second);
    e.set("[object Object]", "bs.item", first);
    const saved = e.get(alias, "bs.item");
    e.remove(element, "bs.item");
    const absent = e.get(alias, "bs.item");
    saved.n = 5;
    return e.get(other, "bs.item").n * 1000 + saved.n * 100
        + e.get("[object Object]", "bs.item").n * 10 + t.size + (absent === null);
}
var a = probe();


//--- class-map-record-nested-object-returned-key.js
function probe() {
    const t = new Map,
        e = {
            set(e, i, n) {
                t.has(e) || t.set(e, new Map);
                const s = t.get(e);
                s.has(i) || 0 === s.size ? s.set(i, n) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(s.keys())[0]}.`)
            },
            get: (e, i) => t.has(e) && t.get(e).get(i) || null,
            remove(e, i) {
                if (!t.has(e)) return;
                const n = t.get(e);
                n.delete(i), 0 === n.size && t.delete(e)
            }
        };
    function key() { return {}; }
    const element = key(), alias = element;
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    e.set(element, "bs.item", first);
    const savedChild = t.get(alias), savedFirst = e.get(element, "bs.item");
    const missing = e.get(element, "missing");
    e.remove(element, "bs.item");
    const absent = e.get(element, "bs.item");
    e.set(element, "bs.item", second);
    savedFirst.n = 5;
    second.n = 9;
    const savedMissing = savedChild.get("late");
    savedChild.set("late", first);
    const absentSnapshot = savedMissing || null;
    const count = savedChild.has("missing") ? 31 : 5 + second.n;
    return savedFirst.n * 1000 + e.get(element, "bs.item").n * 100
        + (missing === null) * 10 + (absent === null) + savedChild.size + t.size
        + (absentSnapshot === null) * 10000 + count;
}
var a = probe();


//--- class-map-record-nested-object-number-key.js
function probe() {
    const t = new Map,
        e = {
            set(e, i, n) {
                t.has(e) || t.set(e, new Map);
                const s = t.get(e);
                s.has(i) || 0 === s.size ? s.set(i, n) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(s.keys())[0]}.`)
            },
            get: (e, i) => t.has(e) && t.get(e).get(i) || null,
            remove(e, i) {
                if (!t.has(e)) return;
                const n = t.get(e);
                n.delete(i), 0 === n.size && t.delete(e)
            }
        };
    const element = 1, alias = element;
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    e.set(element, "bs.item", first);
    const savedChild = t.get(alias), savedFirst = e.get(element, "bs.item");
    const missing = e.get(element, "missing");
    e.remove(element, "bs.item");
    const absent = e.get(element, "bs.item");
    e.set(element, "bs.item", second);
    savedFirst.n = 5;
    second.n = 9;
    const savedMissing = savedChild.get("late");
    savedChild.set("late", first);
    const absentSnapshot = savedMissing || null;
    const count = savedChild.has("missing") ? 31 : 5 + second.n;
    return savedFirst.n * 1000 + e.get(element, "bs.item").n * 100
        + (missing === null) * 10 + (absent === null) + savedChild.size + t.size
        + (absentSnapshot === null) * 10000 + count;
}
var a = probe();


//--- class-map-record-nested-object-changing-key.js
function probe() {
    const t = new Map,
        e = {
            set(e, i, n) {
                t.has(e) || t.set(e, new Map);
                const s = t.get(e);
                s.has(i) || 0 === s.size ? s.set(i, n) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(s.keys())[0]}.`)
            },
            get: (e, i) => t.has(e) && t.get(e).get(i) || null,
            remove(e, i) {
                if (!t.has(e)) return;
                const n = t.get(e);
                n.delete(i), 0 === n.size && t.delete(e)
            }
        };
    let element = {}; const alias = element;
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    e.set(element, "bs.item", first);
    const savedChild = t.get(alias), savedFirst = e.get(element, "bs.item");
    const missing = e.get(element, "missing");
    element = {};
    e.remove(element, "bs.item");
    const absent = e.get(element, "bs.item");
    e.set(element, "bs.item", second);
    savedFirst.n = 5;
    second.n = 9;
    const savedMissing = savedChild.get("late");
    savedChild.set("late", first);
    const absentSnapshot = savedMissing || null;
    const count = savedChild.has("missing") ? 31 : 5 + second.n;
    return savedFirst.n * 1000 + e.get(element, "bs.item").n * 100
        + (missing === null) * 10 + (absent === null) + savedChild.size + t.size
        + (absentSnapshot === null) * 10000 + count;
}
var a = probe();


//--- class-map-record-nested-object-unused-observer.js
function probe() {
    const t = new Map,
        e = {
            set(e, i, n) {
                t.has(e) || t.set(e, new Map);
                const s = t.get(e);
                s.has(i) || 0 === s.size ? s.set(i, n) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(s.keys())[0]}.`)
            },
            get: (e, i) => t.has(e) && t.get(e).get(i) || null,
            remove(e, i) {
                if (!t.has(e)) return;
                const n = t.get(e);
                n.delete(i), 0 === n.size && t.delete(e)
            }
        };
    const element = {}, alias = element;
    e.inspect = () => unknownObserver(t);
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    e.set(element, "bs.item", first);
    const savedChild = t.get(alias), savedFirst = e.get(element, "bs.item");
    const missing = e.get(element, "missing");
    e.remove(element, "bs.item");
    const absent = e.get(element, "bs.item");
    e.set(element, "bs.item", second);
    savedFirst.n = 5;
    second.n = 9;
    const savedMissing = savedChild.get("late");
    savedChild.set("late", first);
    const absentSnapshot = savedMissing || null;
    const count = savedChild.has("missing") ? 31 : 5 + second.n;
    return savedFirst.n * 1000 + e.get(element, "bs.item").n * 100
        + (missing === null) * 10 + (absent === null) + savedChild.size + t.size
        + (absentSnapshot === null) * 10000 + count;
}
var a = probe();


