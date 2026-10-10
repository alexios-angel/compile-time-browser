// Captured literal constants are copied before each inherited constructor is inlined.
//--- inherited-primitive-captures.js
function inherited_primitive_captures() {
    const base = 3, leaf = 4;
    class Base { constructor() { this.n = base; } }
    class Leaf extends Base { constructor() { super(); this.m = leaf; } }
    const item = new Leaf();
    return item.n * 10 + item.m;
}
var a = inherited_primitive_captures();

// A method with a loop keeps the captured literal through method normalization.
//--- primitive-capture-loop.js
function primitive_capture_loop() {
    const increment = 2;
    class Item {
        read(n) { let value = 0; for (let i = 0; i < n; i++) value += increment; return value; }
    }
    return new Item().read(4);
}
var a = primitive_capture_loop();

// A captured primitive must come from a literal written once; `1 + 2` is computed.
//--- primitive-capture-computed.js
function primitive_capture_computed() {
    const increment = 1 + 2;
    const plus = n => n + increment;
    class Item { read(n) { return plus(n); } }
    return new Item().read(4);
}
var a = primitive_capture_computed();
