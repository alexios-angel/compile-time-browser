// A class method may call a fixed holder captured from its local scope.
//--- captured-holder-method.js
function captured_holder_method() {
    const H = {read(n) { return n + 1; }};
    class Shape { read(n) { return H.read(n); } }
    return new Shape().read(7);
}
var a = captured_holder_method();

// A holder slot retains its own immutable sibling-function capture.
//--- captured-holder-sibling.js
function captured_holder_sibling() {
    const plus = n => n + 1;
    const H = {read: n => plus(n)};
    class Shape { read(n) { return H.read(n) * 2; } }
    return new Shape().read(7);
}
var a = captured_holder_sibling();

// Base and leaf constructors share the holder while preserving capture identity.
//--- inherited-captured-holder-shared.js
function inherited_captured_holder_shared() {
    const H = {read: n => n + 1, scale: n => n * 10};
    class Base { constructor(n) { this.n = H.read(n); } }
    class Leaf extends Base {
        constructor(n) { super(n); this.n = H.scale(this.n); }
    }
    return new Base(2).n * 100 + new Leaf(7).n;
}
var a = inherited_captured_holder_shared();

// Local capture transport preserves super effects and left-to-right arguments.
//--- inherited-captured-holder-order.js
function inherited_captured_holder_order() {
    const H = {combine: (left, right) => left * 10 + right};
    class Base { constructor(n) { this.n = n; } }
    class Leaf extends Base {
        constructor(n) {
            super(n = n + 1);
            this.n = H.combine(n = n + 1, n = n + 1) * 100 + this.n * 10 + n;
        }
    }
    return new Leaf(0).n * 10000 + new Leaf(3).n;
}
var a = inherited_captured_holder_order();

// Two unrelated class methods select the same original holder target.
//--- captured-holder-shared.js
function captured_holder_shared() {
    const H = {read: n => n + 1};
    class First { read(n) { return H.read(n); } }
    class Second { read(n) { return H.read(n); } }
    return new First().read(2) * 100 + new Second().read(7);
}
var a = captured_holder_shared();

// An unused class method and holder slot still participate in the complete proof.
//--- captured-holder-unused.js
function captured_holder_unused() {
    const H = {read: n => n + 1, scale: n => n * 10};
    class Shape {
        read(n) { return H.read(n); }
        unused(n) { return H.scale(n); }
    }
    return new Shape().read(7);
}
var a = captured_holder_unused();

// A second write to the holder cell invalidates its captured identity.
//--- captured-holder-replaced.js
function captured_holder_replaced() {
    let H = {read: n => n + 1};
    class Shape { read(n) { return H.read(n); } }
    H = {read: n => n + 2};
    return new Shape().read(7);
}
var a = captured_holder_replaced();

// A fixed holder cannot make a slot's mutable captured cell immutable.
//--- captured-holder-mutable-cell.js
function captured_holder_mutable_cell() {
    let offset = 1;
    const H = {read: n => n + offset};
    class Shape { read(n) { return H.read(n); } }
    offset = 2;
    return new Shape().read(7);
}
var a = captured_holder_mutable_cell();

// The whole alias census must see a slot write after the class captured H.
//--- captured-holder-late-alias.js
function captured_holder_late_alias() {
    const H = {read: n => n + 1}, alias = H;
    class Shape { read(n) { return H.read(n); } }
    alias.read = n => n + 2;
    return new Shape().read(7);
}
var a = captured_holder_late_alias();

// Object-method syntax does not prove that its implicit receiver is unused.
//--- captured-holder-receiver.js
function captured_holder_receiver() {
    const H = {read(n) { return this ? n + 1 : 0; }};
    class Shape { read(n) { return H.read(n); } }
    return new Shape().read(7);
}
var a = captured_holder_receiver();

// Uncalled slots cannot hide an ambient effect behind the captured holder.
//--- captured-holder-unused-ambient.js
function captured_holder_unused_ambient() {
    const H = {read: n => n + 1, unused: n => unknown(n)};
    class Shape { read(n) { return H.read(n); } }
    return new Shape().read(7);
}
var a = captured_holder_unused_ambient();

// Knowing the holder target does not authorize passing away the class receiver.
//--- captured-holder-receiver-escape.js
function captured_holder_receiver_escape() {
    const H = {read: receiver => receiver.n + 1};
    class Shape {
        constructor() { this.n = 7; }
        read() { return H.read(this); }
    }
    return new Shape().read();
}
var a = captured_holder_receiver_escape();

// Surplus argument frames remain outside the fixed callable-holder proof.
//--- captured-holder-surplus.js
function captured_holder_surplus() {
    const H = {read: n => n + 1};
    class Shape { read(n) { return H.read(n, 9); } }
    return new Shape().read(7);
}
var a = captured_holder_surplus();

// A shared Map requires its own ownership proof even when the holder is fixed.
//--- captured-holder-map.js
function captured_holder_map() {
    const values = new Map();
    const H = {read(n) { values.set(n, n + 1); return values.get(n); }};
    class Shape { read(n) { return H.read(n); } }
    return new Shape().read(7);
}
var a = captured_holder_map();

// Retiring an unused slot must not invalidate its recorded sibling-capture reads.
//--- captured-holder-unused-capture.js
function captured_holder_unused_capture() {
    const plus = n => n + 1;
    const H = {read: n => plus(n), unused: n => plus(n)};
    class Shape { read(n) { return H.read(n); } }
    return new Shape().read(7);
}
var a = captured_holder_unused_capture();

// Slot zero in each constructor names a different holder after base expansion.
//--- inherited-captured-holder-distinct.js
function inherited_captured_holder_distinct() {
    const base = {read: n => n + 1};
    const leaf = {read: n => n * 10};
    class Base { constructor(n) { this.n = base.read(n); } }
    class Leaf extends Base {
        constructor(n) { super(n); this.n = leaf.read(this.n); }
    }
    return new Base(2).n * 100 + new Leaf(7).n;
}
var a = inherited_captured_holder_distinct();

// Re-expanding a middle constructor retains both shared and distinct holders.
//--- inherited-captured-holder-chain.js
function inherited_captured_holder_chain() {
    const shared = {read: n => n + 1, finish: n => n + 3};
    const middle = {read: n => n * 10};
    class Base { constructor(n) { this.n = shared.read(n); } }
    class Middle extends Base {
        constructor(n) { super(n); this.n = middle.read(this.n); }
    }
    class Leaf extends Middle {
        constructor(n) { super(n); this.n = shared.finish(this.n); }
    }
    return new Middle(2).n * 100 + new Leaf(7).n;
}
var a = inherited_captured_holder_chain();
