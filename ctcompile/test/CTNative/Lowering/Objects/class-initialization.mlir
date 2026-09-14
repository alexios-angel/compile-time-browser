// RUN: split-file %s %t
// RUN: python3 %S/class_initialization.py --translate ctjs-translate --opt ctjs-opt --node %node --reference %native_reference --fixtures %t --work %t.controls

// The importer emits an ordinary mutable global call to finish every class.
// The host must establish that call's identity before a complete use census
// can discard unobservable descriptor setup. Class syntax alone is no proof.

//--- empty.js
function empty() {
    class Shape {}
    var instance = new Shape();
    instance.n = 7;
    return instance.n;
}
var a = empty();

//--- number.js
function number() {
    class Shape { constructor(n) { this.n = n; } }
    var first = new Shape(7), second = new Shape(2);
    first.n = 9;
    return first.n * 10 + second.n;
}
var a = number();

//--- method.js
function method() {
    class Shape {
        constructor(n) { this.n = n; }
        read() { return this.n; }
    }
    var first = new Shape(7), second = new Shape(2);
    first.n = 9;
    return first.read() * 10 + second.read();
}
var a = method();

// A single explicit write is enough to replace the host binding, even if the
// closed source resolver can identify that replacement function exactly.
// Node never invokes the implementation hook: a=0 there, a=1 in the VM.
//--- helper-override.js
function helper_override() {
    var calls = 0;
    __ctbrowser_class_defined = function replacement(value) { calls = calls + 1; };
    class Shape {}
    return calls;
}
var a = helper_override();

// The second class must observe the replacement after the first used the
// initial host function. An initial binding assertion is not immutability.
//--- helper-late.js
function helper_late() {
    var calls = 0;
    class First {}
    __ctbrowser_class_defined = function replacement(value) { calls = calls + 1; };
    class Second {}
    return calls;
}
var a = helper_late();

// A property write through a saved realm alias mutates the same global.
//--- helper-global-alias.js
function helper_global_alias() {
    var calls = 0;
    var realm = globalThis;
    realm.__ctbrowser_class_defined = function replacement(value) { calls = calls + 1; };
    class Shape {}
    return calls;
}
var a = helper_global_alias();

//--- prototype-late.js
function prototype_late() {
    class Shape {
        constructor() { this.n = 7; }
        read() { return this.n; }
    }
    var instance = new Shape();
    Shape.prototype.read = function replacement() { return 9; };
    return instance.read();
}
var a = prototype_late();

//--- prototype-alias.js
function prototype_alias() {
    class Shape {
        constructor() { this.n = 7; }
        read() { return this.n; }
    }
    var alias = Shape.prototype;
    var instance = new Shape();
    alias.read = function replacement() { return 11; };
    return instance.read();
}
var a = prototype_alias();

// __fields is executable initialization, not disposable class metadata.
//--- field-initializer.js
function field_initializer() {
    class Shape { n = 7; }
    return new Shape().n;
}
var a = field_initializer();

// __home remains observable to a derived constructor's super call.
//--- inherited.js
function inherited() {
    class Base {
        constructor(n) { this.n = n; }
        read() { return this.n; }
    }
    class Derived extends Base {}
    return new Derived(7).read();
}
var a = inherited();

// Preserve the independently measured Button static-inheritance discrepancy:
// Node observes the inherited getter using Derived as this; the VM does not.
// Explicit linkage isolates accessor lookup from constructor linkage.
//--- static-getter.js
class Base { static get k() { return this.n; } }
class Derived extends Base { static get n() { return 7; } }
Object.setPrototypeOf(Derived, Base);
var a = Derived.k === 7 ? 1 : 0;

// Dropping class setup must not silently turn reflection into own fields.
//--- constructor-identity.js
function constructor_identity() {
    class Shape { constructor() { this.n = 7; } }
    var instance = new Shape();
    return instance.constructor === Shape ? 1 : 0;
}
var a = constructor_identity();

//--- descriptor.js
function descriptor() {
    class Shape { read() { return 7; } }
    return Object.keys(Shape.prototype).length;
}
var a = descriptor();

//--- method-arguments.js
function method_arguments() {
    class Shape {
        constructor(n) { this.n = n; }
        adjust(delta, scale) { this.n = this.n * scale + delta; return this.n; }
    }
    var first = new Shape(1), second = new Shape(2);
    var left = first.adjust(1, 2), right = second.adjust(1, 3);
    return left * 1000 + right * 100 + first.n * 10 + second.n;
}
var a = method_arguments();

//--- method-empty.js
function method_empty() {
    class Shape { write(n) { this.n = n; return this.n; } }
    return new Shape().write(7);
}
var a = method_empty();

// Calls through an immutable method keep the exact receiver at every depth.
//--- method-chain.js
function method_chain() {
    class Shape {
        constructor(n) { this.n = n; }
        read() { return this.n; }
        outer() { return this.read(); }
    }
    var first = new Shape(7), second = new Shape(2);
    first.n = 9;
    return first.outer() * 10 + second.outer();
}
var a = method_chain();

// The leaf needs no receiver after lifting, nor does its caller after rewrite.
//--- method-chain-empty.js
function method_chain_empty() {
    class Shape {
        read() { return 7; }
        outer() { return this.read(); }
    }
    return new Shape().outer();
}
var a = method_chain_empty();

// An unused inner result still mutates its receiver, independently at each site.
//--- method-chain-effects.js
function method_chain_effects() {
    class Shape {
        constructor(n) { this.n = n; }
        set(n) { this.n = n; return n + 1; }
        middle(n) { this.set(n); return this.n; }
        outer(n) { return this.middle(n) + this.n; }
    }
    var first = new Shape(1), second = new Shape(2);
    var left = first.outer(3), right = second.outer(7);
    return left * 1000 + right * 100 + first.n * 10 + second.n;
}
var a = method_chain_effects();

// Evaluate inner arguments before the outer body and keep its following read.
//--- method-chain-order.js
function method_chain_order() {
    class Shape {
        constructor() { this.n = 1; }
        write(n) { this.n = this.n * 10 + n; return this.n; }
        outer() { return this.write(this.write(2)) + this.n; }
    }
    return new Shape().outer();
}
var a = method_chain_order();

//--- method-chain-replace.js
function method_chain_replace() {
    class Shape {
        read() { return 7; }
        outer() { this.read = function () { return 9; }; return this.read(); }
    }
    return new Shape().outer();
}
var a = method_chain_replace();

// The callee is read before its argument replaces the property: first 7, then 9.
//--- method-chain-argument-replace.js
function method_chain_argument_replace() {
    class Shape {
        read(n) { return 7; }
        replace() { this.read = function () { return 9; }; return 0; }
        outer() { return this.read(this.replace()); }
    }
    var instance = new Shape();
    return instance.outer() * 10 + instance.read();
}
var a = method_chain_argument_replace();

//--- method-chain-identity.js
function method_chain_identity() {
    class Shape {
        read() { return 7; }
        outer() { return (this.read === this.read) * 1; }
    }
    return new Shape().outer();
}
var a = method_chain_identity();

// The same SSA receiver also used as an argument needs a separate borrow proof.
//--- method-chain-argument-receiver.js
function method_chain_argument_receiver() {
    class Shape {
        constructor() { this.n = 7; }
        read(other) { other.n = 9; return this.n; }
        outer() { return this.read(this); }
    }
    return new Shape().outer();
}
var a = method_chain_argument_receiver();

//--- method-chain-return-receiver.js
function method_chain_return_receiver() {
    class Shape {
        constructor() { this.n = 7; }
        next() { return this; }
        outer() { return this.next().n; }
    }
    return new Shape().outer();
}
var a = method_chain_return_receiver();

// Terminating recursion still requires structured control-flow proof.
//--- method-chain-cycle.js
function method_chain_cycle() {
    class Shape {
        constructor() { this.n = 7; }
        first(n) { if (n === 0) { return this.n; } return this.second(n - 1); }
        second(n) { return this.first(n); }
    }
    return new Shape().first(1);
}
var a = method_chain_cycle();

//--- method-shadow.js
function method_shadow() {
    class Shape { read() { return 7; } }
    var instance = new Shape();
    instance.read = function () { return 9; };
    return instance.read();
}
var a = method_shadow();

//--- method-extracted.js
function method_extracted() {
    class Shape { read() { return 7; } }
    var instance = new Shape();
    var saved = instance.read;
    return saved === instance.read ? 1 : 0;
}
var a = method_extracted();

//--- method-constructor-read.js
function method_constructor_read() {
    class Shape {
        constructor() { this.n = typeof this.read === "function" ? 1 : 0; }
        read() { return this.n; }
    }
    return new Shape().read();
}
var a = method_constructor_read();

//--- method-constructor-write.js
function method_constructor_write() {
    class Shape {
        constructor() { this.read = function () { return 9; }; }
        read() { return 7; }
    }
    return new Shape().read();
}
var a = method_constructor_write();

// Construction invokes prototype methods before any post-construction binding.
//--- method-constructor-call.js
function method_constructor_call() {
    class Shape {
        constructor() { this.n = this.init(7); }
        init(n) { this.n = n; return n + 1; }
        read() { return this.n; }
    }
    return new Shape().read();
}
var a = method_constructor_call();

//--- method-constructor-order.js
function method_constructor_order() {
    class Shape {
        constructor() { this.n = 1; this.init(this.init(2)); }
        init(n) { this.n = this.n * 10 + n; return this.n; }
        read() { return this.n; }
    }
    return new Shape().read();
}
var a = method_constructor_order();

//--- method-self-replace.js
function method_self_replace() {
    class Shape {
        read() { this.read = function () { return 9; }; return 7; }
    }
    var instance = new Shape();
    return instance.read() * 10 + instance.read();
}
var a = method_self_replace();

//--- method-duplicate.js
function method_duplicate() {
    class Shape { read() { return 7; } read() { return 9; } }
    return new Shape().read();
}
var a = method_duplicate();

//--- method-captured.js
function method_captured() {
    var n = 7;
    class Shape { read() { return n; } }
    return new Shape().read();
}
var a = method_captured();

//--- method-dynamic.js
function method_dynamic() {
    class Shape { constructor() { this.n = 7; } read(k) { return this[k]; } }
    return new Shape().read("n");
}
var a = method_dynamic();

// A constructor's replacement object never inherits the class's methods.
//--- method-return-object.js
function method_return_object() {
    class Shape {
        constructor() { return {read: function () { return 9; }}; }
        read() { return 7; }
    }
    return new Shape().read();
}
var a = method_return_object();

// The ordinary lowering must independently prove method initialization and
// reject mutations even when no class preparation pass supplied the source.
//--- plain-method.js
function plain_method() {
    function Shape() { this.n = 7; }
    var instance = new Shape();
    instance.read = function () { return this.n; };
    return instance.read();
}
var a = plain_method();

//--- plain-before-store.js
function plain_before_store() {
    function Shape() {}
    var instance = new Shape();
    var result = instance.read();
    instance.read = function () { return 7; };
    return result;
}
var a = plain_before_store();

//--- plain-borrowed-write.js
function plain_borrowed_write() {
    function Shape() {}
    var instance = new Shape();
    instance.read = function () { return 7; };
    var replace = function (value) { value.read = function () { return 9; }; };
    replace(instance);
    return instance.read();
}
var a = plain_borrowed_write();

//--- plain-self-replace.js
function plain_self_replace() {
    function Shape() {}
    var instance = new Shape();
    instance.read = function () { this.read = function () { return 9; }; return 7; };
    return instance.read() * 10 + instance.read();
}
var a = plain_self_replace();

//--- plain-constructor-write.js
function plain_constructor_write() {
    function Shape() { this.read = 0; }
    var instance = new Shape();
    instance.read = function () { return 7; };
    return instance.read();
}
var a = plain_constructor_write();

//--- plain-detached.js
function plain_detached() {
    function Shape() {}
    var instance = new Shape();
    instance.read = function () { return 7; };
    var called = instance.read();
    var saved = instance.read;
    return called === 7 && saved === instance.read ? 1 : 0;
}
var a = plain_detached();
