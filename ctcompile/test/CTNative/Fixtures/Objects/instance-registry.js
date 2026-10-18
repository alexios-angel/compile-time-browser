// Instances of a class that a registry Map owns, run end to end natively.
//
// The constructor stores `this` into a Map, `getInstance` reads it back with
// `|| null`, and `dispose` removes the entry and then goes on using `this`.
// The registry proof (ctcompile Analysis/Instances.h) shows that the Map and
// the locals that read from it can share ownership of each instance as
// `std::shared_ptr<Widget>`, with no path from an instance back to the Map,
// so destruction is plain RAII. dispose() then nulls every own property
// through Object.getOwnPropertyNames, which the compiler unrolls over the
// class's two fields. The keys are numbers because a struct field that holds
// a String and later null has no carrier yet. The answer, worked out by hand:
//   a grows from 3 to 4, and sizeOf(a) is 4: 8
//   b.size 10, then dispose() answers 10 + 1 entry left: 29
//   b's fields are both null after dispose: 1000029
//   getInstance(2) is null after dispose: 1000129
//   a read straight from the Map, under `!= null`: + 4 * 1000 = 1004129
//   findInstance(1) through `has(k) && get(k) || null`: + 4 * 10000 = 1044129
//   findInstance(2) is null: 1144129
//   one entry left: 1144130, for each of the two runs (`first` and `total`),
//   each with a registry of its own
function run() {
    const registry = new Map();
    class Widget {
        constructor(key, size) {
            this.key = key;
            this.size = size;
            registry.set(key, this);
        }
        grow() {
            this.size = this.size + 1;
            return this.size;
        }
        dispose() {
            registry.delete(this.key);
            const left = this.size + registry.size;
            // As Bootstrap's BaseComponent.dispose: every own property null.
            for (const name of Object.getOwnPropertyNames(this)) {
                this[name] = null;
            }
            return left;
        }
    }
    function getInstance(key) {
        return registry.get(key) || null;
    }
    // Bootstrap's Data.get: `has` answers false for a missing key, which
    // only `|| null` reads, so the join is an instance or null.
    function findInstance(key) {
        return (registry.has(key) && registry.get(key)) || null;
    }
    function sizeOf(widget) {
        return widget.size;
    }
    new Widget(1, 3);
    new Widget(2, 10);
    let total = 0;
    const a = getInstance(1);
    if (a !== null) { total = total + a.grow() + sizeOf(a); }
    const b = getInstance(2);
    if (b) {
        total = total + b.size + b.dispose();
        if (b.size === null && b.key === null) { total = total + 1000000; }
    }
    if (getInstance(2) === null) { total = total + 100; }
    const c = registry.get(1);
    if (c != null) { total = total + c.size * 1000; }
    const d = findInstance(1);
    if (d !== null) { total = total + d.size * 10000; }
    if (findInstance(2) === null) { total = total + 100000; }
    return total + registry.size;
}
var first = run();
var total = run();
