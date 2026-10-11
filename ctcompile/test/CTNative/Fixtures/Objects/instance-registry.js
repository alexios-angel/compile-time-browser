// Instances of a class that a registry Map owns, run end to end natively.
//
// The constructor stores `this` into a Map, `getInstance` reads it back with
// `|| null`, and `dispose` removes the entry and then goes on using `this`.
// The registry proof (ctcompile Analysis/Instances.h) shows that the Map and
// the locals that read from it can share ownership of each instance as
// `std::shared_ptr<Widget>`, with no path from an instance back to the Map,
// so destruction is plain RAII. The answer, worked out by hand:
//   a grows from 3 to 4, and sizeOf(a) is 4: 8
//   b.size 10, then dispose() answers 0 + 1 entry left: 19
//   getInstance("b") is null after dispose: 119
//   a read straight from the Map, under `!= null`: + 4 * 1000 = 4119
//   one entry left: 4120, for each of the two runs (`first` and `total`),
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
            this.size = 0;
            return this.size + registry.size;
        }
    }
    function getInstance(key) {
        return registry.get(key) || null;
    }
    function sizeOf(widget) {
        return widget.size;
    }
    new Widget("a", 3);
    new Widget("b", 10);
    let total = 0;
    const a = getInstance("a");
    if (a !== null) { total = total + a.grow() + sizeOf(a); }
    const b = getInstance("b");
    if (b) { total = total + b.size + b.dispose(); }
    if (getInstance("b") === null) { total = total + 100; }
    const c = registry.get("a");
    if (c != null) { total = total + c.size * 1000; }
    return total + registry.size;
}
var first = run();
var total = run();
