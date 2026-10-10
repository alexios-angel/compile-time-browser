// A static method no read reaches never runs: it is removed before the class
// proof, so its body's `new this` is never examined.
//--- static-unread-new-this.js
function static_unread_new_this() {
    class Shape {
        constructor(n) { this.n = n; }
        static make(n) { return new this(n); }
        read() { return this.n; }
    }
    return new Shape(7).read();
}
var a = static_unread_new_this();

// A static read only by another unread static is unread too.
//--- static-unread-chain.js
function static_unread_chain() {
    class Shape {
        constructor(n) { this.n = n; }
        static find(n) { return n ? null : null; }
        static make(n) { return this.find(n) || new this(n); }
        read() { return this.n; }
    }
    return new Shape(7).read();
}
var a = static_unread_chain();

// Functions only an unread static body creates go with it.
//--- static-unread-nested.js
function static_unread_nested() {
    class Shape {
        constructor(n) { this.n = n; }
        static each(list) { return list.map(function(x) { return unknownHelper(x); }); }
        read() { return this.n; }
    }
    return new Shape(7).read();
}
var a = static_unread_nested();

// A read on a subclass keeps the inherited static, even unused.
//--- inherited-static-unread-leaf-read.js
function inherited_static_unread_leaf_read() {
    class Shape {
        constructor(n) { this.n = n; }
        static make(n) { return new this(n); }
        read() { return this.n; }
    }
    class Leaf extends Shape { constructor(n) { super(n); } }
    var probe = Leaf.make;
    return new Leaf(7).read();
}
var a = inherited_static_unread_leaf_read();

// A computed key on the class may name any static.
//--- static-unread-dynamic.js
function static_unread_dynamic(k = "ma" + "ke") {
    class Shape {
        constructor(n) { this.n = n; }
        static make(n) { return new this(n); }
        read() { return this.n; }
    }
    var probe = Shape[k];
    return new Shape(7).read();
}
var a = static_unread_dynamic();

// An instance reaches its class's statics through this.constructor.
//--- static-unread-constructor-read.js
function static_unread_constructor_read() {
    class Shape {
        constructor(n) { this.n = n; }
        static make(n) { return new this(n); }
        read() { return this.constructor.make ? this.n : 0; }
    }
    return new Shape(7).read();
}
var a = static_unread_constructor_read();
