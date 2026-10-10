// Preserve the complete vendor Data conflict diagnostic; only proved dead arms retire.
//--- class-map-record-nested-conflict-direct.js
function probe() {
    const outer = new Map();
    function register(element, key, item) {
        if (!outer.has(element)) outer.set(element, new Map());
        const child = outer.get(element);
        child.has(key) || 0 === child.size ? child.set(key, item) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(child.keys())[0]}.`);
    }
    function get(element, key) {
        return outer.has(element) && outer.get(element).get(key) || null;
    }
    function remove(element, key) {
        if (outer.has(element)) {
            const child = outer.get(element);
            child.delete(key);
            if (child.size === 0) outer.delete(element);
        }
    }
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7), other = new Item(4);
    const absentBefore = get("left", "bs.item");
    register("left", "bs.item", first);
    register("right", "bs.item", other);
    const savedChild = outer.get("left"), savedFirst = get("left", "bs.item");
    const missingInner = get("left", "missing");
    register("left", "bs.item", second);
    const savedSecond = get("left", "bs.item");
    remove("left", "bs.item");
    const absentAfter = get("left", "bs.item");
    register("left", "bs.next", other);
    savedFirst.n = 5;
    savedSecond.n = 9;
    return savedFirst.n * 100000 + savedSecond.n * 10000
        + get("right", "bs.item").n * 1000 + get("left", "bs.next").n * 100
        + (absentBefore === null) * 10 + (missingInner === null)
        + (absentAfter === null) + savedChild.size + outer.size;
}
var a = probe();

//--- class-map-record-nested-conflict-holder.js
function probe() {
    const outer = new Map();
    const Data = {
        set(element, key, item) {
            outer.has(element) || outer.set(element, new Map());
            const child = outer.get(element);
        child.has(key) || 0 === child.size ? child.set(key, item) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(child.keys())[0]}.`);
        },
        get(element, key) {
            return outer.has(element) && outer.get(element).get(key) || null;
        },
        remove(element, key) {
            if (outer.has(element)) {
                const child = outer.get(element);
                child.delete(key);
                if (child.size === 0) outer.delete(element);
            }
        }
    };
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    Data.set("element", "bs.item", first);
    const savedChild = outer.get("element"), savedFirst = Data.get("element", "bs.item");
    const missing = Data.get("element", "missing");
    Data.remove("element", "bs.item");
    const absent = Data.get("element", "bs.item");
    Data.set("element", "bs.item", second);
    savedFirst.n = 5;
    second.n = 9;
    const savedMissing = savedChild.get("late");
    savedChild.set("late", first);
    const absentSnapshot = savedMissing || null;
    const count = savedChild.has("missing") ? 31 : 5 + second.n;
    return savedFirst.n * 1000 + Data.get("element", "bs.item").n * 100
        + (missing === null) * 10 + (absent === null) + savedChild.size + outer.size
        + (absentSnapshot === null) * 10000 + count;
}
var a = probe();

//--- class-map-record-nested-conflict-constructor.js
function probe() {
    const outer = new Map();
    const Data = {
        set(element, key, item) {
            if (!outer.has(element)) outer.set(element, new Map());
            const child = outer.get(element);
        child.has(key) || 0 === child.size ? child.set(key, item) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(child.keys())[0]}.`);
        },
        get(element, key) {
            return outer.has(element) && outer.get(element).get(key) || null;
        },
        remove(element, key) {
            if (outer.has(element)) {
                const child = outer.get(element);
                child.delete(key);
                if (child.size === 0) outer.delete(element);
            }
        }
    };
    class Item {
        constructor(n) {
            this.n = n;
            Data.set("element", "bs.item", this);
        }
    }
    const absentBefore = Data.get("element", "bs.item");
    const first = new Item(2);
    const savedChild = outer.get("element"), savedFirst = Data.get("element", "bs.item");
    const missing = Data.get("element", "missing");
    Data.remove("element", "bs.item");
    const absentAfter = Data.get("element", "bs.item");
    const second = new Item(7);
    savedFirst.n = 5;
    second.n = 9;
    return savedFirst.n * 10000 + Data.get("element", "bs.item").n * 1000
        + (absentBefore === null) * 100 + (missing === null) * 10
        + (absentAfter === null) + savedChild.size + outer.size;
}
var a = probe();

//--- class-map-record-nested-conflict-reached.js
function probe() {
 const outer = new Map();
 function set(e,i,n) {
  outer.has(e) || outer.set(e,new Map());
  const s=outer.get(e);
  s.has(i) || 0 === s.size ? s.set(i,n) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(s.keys())[0]}.`);
 }
 class Item { constructor(n) {this.n=n;} }
 const first=new Item(2), second=new Item(7);
 set("element", "bs.item", first);
 set("element", "bs.other", second);
 return outer.get("element").get("bs.item").n;
}
var a=probe();

//--- class-map-record-nested-conflict-saved-child-conflict.js
function probe() {
 const outer = new Map();
 function set(e,i,n) {
  outer.has(e) || outer.set(e,new Map());
  const s=outer.get(e);
  s.has(i) || 0 === s.size ? s.set(i,n) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(s.keys())[0]}.`);
 }
 class Item { constructor(n) {this.n=n;} }
 const first=new Item(2), second=new Item(7);
 set("element", "bs.item", first);
 const saved = outer.get("element"); saved.set("bs.other", first); saved.delete("bs.item");
 set("element", "bs.item", second);
 return outer.get("element").get("bs.other").n;
}
var a=probe();

//--- class-map-record-nested-conflict-hidden-observer.js
function probe() {
 const outer = new Map();
 function set(e,i,n) {
  outer.has(e) || outer.set(e,new Map());
  const s=outer.get(e);
  s.has(i) || 0 === s.size ? s.set(i,n) : (unknownObserver(outer), console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(s.keys())[0]}.`));
 }
 class Item { constructor(n) {this.n=n;} }
 const first=new Item(2), second=new Item(7);
 set("element", "bs.item", first);
 set("element", "bs.item", second);
 return outer.get("element").get("bs.item").n;
}
var a=probe();

//--- class-map-record-nested-conflict-hidden-coercion.js
function probe() {
 const outer = new Map();
 function set(e,i,n) {
  outer.has(e) || outer.set(e,new Map());
  const s=outer.get(e);
  s.has(i) || 0 === s.size ? s.set(i,n) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${unknownValue}.`);
 }
 class Item { constructor(n) {this.n=n;} }
 const first=new Item(2), second=new Item(7);
 set("element", "bs.item", first);
 set("element", "bs.item", second);
 return outer.get("element").get("bs.item").n;
}
var a=probe();

//--- class-map-record-nested-conflict-unused-holder-slot.js
function probe() {
    const outer = new Map();
    const Data = {
        inspect() { return unknownObserver(outer); },
        set(element, key, item) {
            outer.has(element) || outer.set(element, new Map());
            const child = outer.get(element);
        child.has(key) || 0 === child.size ? child.set(key, item) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(child.keys())[0]}.`);
        },
        get(element, key) {
            return outer.has(element) && outer.get(element).get(key) || null;
        },
        remove(element, key) {
            if (outer.has(element)) {
                const child = outer.get(element);
                child.delete(key);
                if (child.size === 0) outer.delete(element);
            }
        }
    };
    class Item { constructor(n) { this.n = n; } }
    const first = new Item(2), second = new Item(7);
    Data.set("element", "bs.item", first);
    const savedChild = outer.get("element"), savedFirst = Data.get("element", "bs.item");
    const missing = Data.get("element", "missing");
    Data.remove("element", "bs.item");
    const absent = Data.get("element", "bs.item");
    Data.set("element", "bs.item", second);
    savedFirst.n = 5;
    second.n = 9;
    const savedMissing = savedChild.get("late");
    savedChild.set("late", first);
    const absentSnapshot = savedMissing || null;
    const count = savedChild.has("missing") ? 31 : 5 + second.n;
    return savedFirst.n * 1000 + Data.get("element", "bs.item").n * 100
        + (missing === null) * 10 + (absent === null) + savedChild.size + outer.size
        + (absentSnapshot === null) * 10000 + count;
}
var a = probe();

//--- class-map-record-nested-conflict-dynamic-key.js
function probe(key) {
 const outer = new Map();
 function set(e,i,n) {
  outer.has(e) || outer.set(e,new Map());
  const s=outer.get(e);
  s.has(i) || 0 === s.size ? s.set(i,n) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(s.keys())[0]}.`);
 }
 class Item { constructor(n) {this.n=n;} }
 const first=new Item(2), second=new Item(7);
 set("element", "bs.item", first);
 set("element", key, second);
 return outer.get("element").get("bs.item").n;
}
var a=probe("bs.item");

//--- class-map-record-nested-conflict-returned-owner.js
function probe() {
 const outer = new Map();
 function set(e,i,n) {
  outer.has(e) || outer.set(e,new Map());
  const s=outer.get(e);
  s.has(i) || 0 === s.size ? s.set(i,n) : console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(s.keys())[0]}.`);
 }
 class Item { constructor(n) {this.n=n;} }
 const first=new Item(2), second=new Item(7);
 set("element", "bs.item", first);
 set("element", "bs.item", second);
 return outer;
}
var a=probe().get("element").get("bs.item").n;

//--- class-map-record-nested-conflict-hidden-store.js
function probe() {
 const outer = new Map();
 function set(e,i,n) {
  outer.has(e) || outer.set(e,new Map());
  const s=outer.get(e);
  s.has(i) || 0 === s.size ? s.set(i,n) : (globalThis.unexpected = 1, console.error(`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(s.keys())[0]}.`));
 }
 class Item { constructor(n) {this.n=n;} }
 const first=new Item(2), second=new Item(7);
 set("element", "bs.item", first);
 set("element", "bs.item", second);
 return outer.get("element").get("bs.item").n;
}
var a=probe();

