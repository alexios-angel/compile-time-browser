// Own static slots preserve call arguments and their evaluation order.
//--- static-call-arguments.js
function static_call_arguments() {
    class Shape { static combine(left, right) { return left * 100 + right; } }
    var instance = new Shape(), value = 0;
    var result = Shape.combine(value = value + 1, value = value + 1);
    return result * 10 + value;
}
var a = static_call_arguments();

// A static method may reuse the same immutable sibling-helper capture proof.
//--- static-call-capture.js
function static_call_capture() {
    const plus = n => n + 1;
    class Shape { static read(n) { return plus(n) * 2; } }
    var instance = new Shape();
    return Shape.read(7);
}
var a = static_call_capture();

// The exact constructor receiver selects its own closed getter chain.
//--- static-call-getter.js
function static_call_getter() {
    class Shape {
        static get NAME() { return "button"; }
        static get DATA_KEY() { return "bs." + this.NAME; }
        static read(n) { return (this.DATA_KEY === "bs.button") * 100 + n; }
    }
    var instance = new Shape();
    return Shape.read(7);
}
var a = static_call_getter();

// An unused static body still participates in the complete source census.
//--- static-call-unused.js
function static_call_unused() {
    class Shape { static read(n) { return n + 1; } }
    var instance = new Shape();
    return 7;
}
var a = static_call_unused();

// An unused body and an untaken arm do not authorize an ambient effect.
//--- static-call-ambient.js
function static_call_ambient() {
    class Shape { static read(n) { if (n) unknown(n); return 7; } }
    var instance = new Shape();
    return 7;
}
var a = static_call_ambient();

//--- static-call-duplicate.js
function static_call_duplicate() {
    class Shape { static read() { return 7; } static read() { return 9; } }
    var instance = new Shape();
    return Shape.read();
}
var a = static_call_duplicate();

// Ordinary methods and accessors cannot share the same proved own slot.
// The current VM retains the method; Node observes the later accessor.
//--- static-call-accessor.js
function static_call_accessor() {
    class Shape { static read() { return 7; } static get read() { return 9; } }
    var instance = new Shape();
    return typeof Shape.read === 'function' ? 8 : Shape.read;
}
var a = static_call_accessor();

//--- static-call-replaced.js
function static_call_replaced() {
    class Shape { static read() { return 7; } }
    var instance = new Shape();
    Shape.read = () => 9;
    return Shape.read();
}
var a = static_call_replaced();

// A detached call does not carry the exact constructor receiver.
//--- static-call-detached.js
function static_call_detached() {
    class Shape { static read() { return 7; } }
    var instance = new Shape(), read = Shape.read;
    return read();
}
var a = static_call_detached();

// The uncalled new-this arm requires its own construction proof.
//--- static-call-new-this.js
function static_call_new_this() {
    class Shape { static make(n) { if (n) return new this(); return 7; } }
    var instance = new Shape();
    return Shape.make(0);
}
var a = static_call_new_this();

// Reserved closure metadata remains refused even without a source read.
//--- static-call-reserved.js
function static_call_reserved() {
    class Shape { static name() { return 9; } }
    var instance = new Shape();
    return 7;
}
var a = static_call_reserved();

// An inherited lookup needs a separate proof for its effective receiver.
//--- inherited-static-call.js
function inherited_static_call() {
    class Base { static read() { return 7; } }
    class Leaf extends Base { constructor() { super(); } }
    var instance = new Leaf();
    return Leaf.read();
}
var a = inherited_static_call();
