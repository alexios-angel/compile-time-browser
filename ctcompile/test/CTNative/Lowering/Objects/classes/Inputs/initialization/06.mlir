// Immutable sibling helpers retain their complete bodies and ordinary call order.
//--- captured-helper-method.js
function captured_helper_method() {
    const plus = n => n + 1;
    class Shape { read(n) { return plus(n); } }
    return new Shape().read(7);
}
var a = captured_helper_method();

// Ordinary inherited dispatch keeps the helper selected by the method's closure.
//--- inherited-helper-method.js
function inherited_helper_method() {
    const plus = n => n + 1;
    class Base {
        constructor(n) { this.n = n; }
        read() { return plus(this.n); }
    }
    class Leaf extends Base {
        constructor(n) { super(n); }
        run() { return this.read() * 2; }
    }
    return new Leaf(7).run();
}
var a = inherited_helper_method();

//--- captured-helper-constructor.js
function captured_helper_constructor() {
    const plus = n => n + 1;
    class Shape {
        constructor(n) { this.n = plus(n); }
        read() { return this.n; }
    }
    return new Shape(7).read();
}
var a = captured_helper_constructor();

// Argument effects precede the constructor's original captured helper call.
//--- captured-helper-order.js
function captured_helper_order() {
    const combine = (left, right) => left * 100 + right;
    class Shape { constructor(left, right) { this.n = combine(left, right); } }
    var value = 0;
    var shape = new Shape(value = value + 1, value = value + 1);
    return shape.n * 10 + value;
}
var a = captured_helper_order();

//--- captured-helper-replaced.js
function captured_helper_replaced() {
    let plus = n => n + 1;
    class Shape { read(n) { return plus(n); } }
    plus = n => n + 2;
    return new Shape().read(7);
}
var a = captured_helper_replaced();

// An uncalled writer still prevents freezing the captured helper's identity.
//--- captured-helper-writer.js
function captured_helper_writer() {
    let plus = n => n + 1;
    const replace = () => { plus = n => n + 2; };
    class Shape { read(n) { return plus(n); } }
    var shape = new Shape();
    return 7;
}
var a = captured_helper_writer();

// A shadowed method's captured helper cannot hide an unknown effect.
//--- inherited-helper-ambient.js
function inherited_helper_ambient() {
    const unsafe = n => unknown(n);
    class Base { read(n) { return unsafe(n); } }
    class Leaf extends Base {
        constructor() { super(); }
        read(n) { return n; }
    }
    return new Leaf().read(7);
}
var a = inherited_helper_ambient();

//--- captured-helper-identity.js
function captured_helper_identity() {
    const plus = n => n + 1;
    class Shape { read() { return plus; } }
    return new Shape().read() === plus ? 1 : 0;
}
var a = captured_helper_identity();

//--- captured-helper-receiver.js
function captured_helper_receiver() {
    const plus = function(n) { return this.n + n; };
    class Shape { read(n) { return plus(n); } }
    var shape = new Shape();
    return 7;
}
var a = captured_helper_receiver();

//--- captured-helper-excess.js
function captured_helper_excess() {
    const plus = n => n + 1;
    class Shape { read(n) { return plus(n, 2); } }
    return new Shape().read(7);
}
var a = captured_helper_excess();

// Recursive closure environments need a separate proof from one sibling helper.
//--- captured-helper-nested.js
function captured_helper_nested() {
    const offset = 1;
    const plus = n => n + offset;
    class Shape { read(n) { return plus(n); } }
    return new Shape().read(7);
}
var a = captured_helper_nested();

// Captured base constructors and lexical-super targets retain their own gates.
//--- inherited-helper-constructor.js
function inherited_helper_constructor() {
    const plus = n => n + 1;
    class Base { constructor(n) { this.n = plus(n); } }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(7).n;
}
var a = inherited_helper_constructor();

//--- inherited-super-helper.js
function inherited_super_helper() {
    const plus = n => n + 1;
    class Base { read(n) { return plus(n); } }
    class Leaf extends Base {
        constructor() { super(); }
        read(n) { return super.read(n); }
    }
    return new Leaf().read(7);
}
var a = inherited_super_helper();
