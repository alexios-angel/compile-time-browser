// The complete vendor Data holder, including its arrow getter and early return.
//--- class-map-record-nested-vendor-holder.js
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
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    e.set("element", "bs.item", first);
    const savedChild = t.get("element"), savedFirst = e.get("element", "bs.item");
    const missing = e.get("element", "missing");
    e.remove("element", "bs.item");
    const absent = e.get("element", "bs.item");
    e.set("element", "bs.item", second);
    savedFirst.n = 5;
    second.n = 9;
    const savedMissing = savedChild.get("late");
    savedChild.set("late", first);
    const absentSnapshot = savedMissing || null;
    const count = savedChild.has("missing") ? 31 : 5 + second.n;
    return savedFirst.n * 1000 + e.get("element", "bs.item").n * 100
        + (missing === null) * 10 + (absent === null) + savedChild.size + t.size
        + (absentSnapshot === null) * 10000 + count;
}
var a = probe();


//--- class-map-record-nested-vendor-constructor.js
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
    class Item {
        constructor(n) {
            this.n = n;
            e.set("element", "bs.item", this);
        }
    }
    const absentBefore = e.get("element", "bs.item");
    const first = new Item(2);
    const savedChild = t.get("element"), savedFirst = e.get("element", "bs.item");
    const missing = e.get("element", "missing");
    e.remove("element", "bs.item");
    const absentAfter = e.get("element", "bs.item");
    const second = new Item(7);
    savedFirst.n = 5;
    second.n = 9;
    return savedFirst.n * 10000 + e.get("element", "bs.item").n * 1000
        + (absentBefore === null) * 100 + (missing === null) * 10
        + (absentAfter === null) + savedChild.size + t.size;
}
var a = probe();


//--- class-map-record-nested-vendor-live-this.js
function probe() {
    "use strict";
    const t = new Map,
        e = {
            set(e, i, n) {
                t.has(e) || t.set(e, new Map);
                const s = t.get(e);
                s.has(i) || 0 === s.size ? s.set(i, n) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(s.keys())[0]}.`)
            },
            get: (e, i) => this === undefined ? t.has(e) && t.get(e).get(i) || null : null,
            remove(e, i) {
                if (!t.has(e)) return;
                const n = t.get(e);
                n.delete(i), 0 === n.size && t.delete(e)
            }
        };
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    e.set("element", "bs.item", first);
    const savedChild = t.get("element"), savedFirst = e.get("element", "bs.item");
    const missing = e.get("element", "missing");
    e.remove("element", "bs.item");
    const absent = e.get("element", "bs.item");
    e.set("element", "bs.item", second);
    savedFirst.n = 5;
    second.n = 9;
    const savedMissing = savedChild.get("late");
    savedChild.set("late", first);
    const absentSnapshot = savedMissing || null;
    const count = savedChild.has("missing") ? 31 : 5 + second.n;
    return savedFirst.n * 1000 + e.get("element", "bs.item").n * 100
        + (missing === null) * 10 + (absent === null) + savedChild.size + t.size
        + (absentSnapshot === null) * 10000 + count;
}
var a = probe();


//--- class-map-record-nested-vendor-unused-observer.js
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
    e.inspect = () => unknownObserver(t);
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    e.set("element", "bs.item", first);
    const savedChild = t.get("element"), savedFirst = e.get("element", "bs.item");
    const missing = e.get("element", "missing");
    e.remove("element", "bs.item");
    const absent = e.get("element", "bs.item");
    e.set("element", "bs.item", second);
    savedFirst.n = 5;
    second.n = 9;
    const savedMissing = savedChild.get("late");
    savedChild.set("late", first);
    const absentSnapshot = savedMissing || null;
    const count = savedChild.has("missing") ? 31 : 5 + second.n;
    return savedFirst.n * 1000 + e.get("element", "bs.item").n * 100
        + (missing === null) * 10 + (absent === null) + savedChild.size + t.size
        + (absentSnapshot === null) * 10000 + count;
}
var a = probe();


