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

// Constructor receiver origins cannot make a later binding available earlier.
//--- plain-constructor-before-store.js
function plain_constructor_before_store() {
    function Shape() { this.n = this.read(); }
    var instance = new Shape();
    instance.read = function () { return 7; };
    return instance.n;
}
var a = plain_constructor_before_store();

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

// An immutable literal prototype exposes its method before the constructor runs.
//--- plain-prototype.js
function plain_prototype() {
    function Shape(n) { this.n = this.read(n); }
    Shape.prototype = {read: function (n) { this.n = n; return this.n + 1; }};
    var first = new Shape(7), second = new Shape(2);
    return first.n * 10 + second.n;
}
var a = plain_prototype();

// An actual call cannot authorize retaining the same method as a function value.
//--- plain-prototype-identity.js
function plain_prototype_identity() {
    function Shape() { this.n = this.read(); }
    Shape.prototype = {read: function () { return 7; }};
    var instance = new Shape();
    var called = instance.read(), saved = instance.read;
    return called === 7 && saved === instance.read ? 1 : 0;
}
var a = plain_prototype_identity();

//--- plain-prototype-replace.js
function plain_prototype_replace() {
    function Shape() { this.n = this.read(); }
    Shape.prototype = {read: function () { return 7; }};
    var instance = new Shape();
    instance.read = function () { return 9; };
    return instance.n * 10 + instance.read();
}
var a = plain_prototype_replace();

//--- plain-prototype-self-replace.js
function plain_prototype_self_replace() {
    function Shape() { this.n = this.read() * 10 + this.read(); }
    Shape.prototype = {read: function () { this.read = function () { return 9; }; return 7; }};
    return new Shape().n;
}
var a = plain_prototype_self_replace();

// One available prototype method does not initialize a different, later binding.
//--- plain-prototype-before-store.js
function plain_prototype_before_store() {
    function Shape() { this.n = this.read() + this.late(); }
    Shape.prototype = {read: function () { return 7; }};
    var instance = new Shape();
    instance.late = function () { return 2; };
    return instance.n;
}
var a = plain_prototype_before_store();

//--- method-constructor-chain.js
// Each construction seeds the receiver used by every method in the chain.
function method_constructor_chain() {
    class Shape {
        constructor(n) { this.init(n); }
        init(n) { this.write(n); this.n = this.n + 1; }
        write(n) { this.n = n; }
        read() { return this.n; }
    }
    var first = new Shape(3), second = new Shape(7);
    return first.read() * 10 + second.read();
}
var a = method_constructor_chain();

//--- method-constructor-constant.js
// A constant leaf drops its unused receiver without dropping the constructor store.
function method_constructor_constant() {
    class Shape {
        constructor() { this.n = this.constant(); }
        constant() { return 7; }
    }
    return new Shape().n;
}
var a = method_constructor_constant();

//--- method-constructor-identity.js
// Calling an immutable method does not authorize observing its closure identity.
function method_constructor_identity() {
    class Shape {
        constructor() { this.n = (this.init === this.init) * 1; }
        init() { return 7; }
    }
    return new Shape().n;
}
var a = method_constructor_identity();

//--- method-constructor-argument-replace.js
// Resolve read before replace runs, then observe the replacement after construction.
function method_constructor_argument_replace() {
    class Shape {
        constructor() { this.n = this.read(this.replace()); }
        read(n) { return 7; }
        replace() { this.read = function () { return 9; }; return 0; }
    }
    var instance = new Shape();
    return instance.n * 10 + instance.read();
}
var a = method_constructor_argument_replace();

//--- method-constructor-return-object.js
// A method initializes the original receiver before construction replaces it.
function method_constructor_return_object() {
    class Shape {
        constructor() { this.init(); return {n: 9}; }
        init() { this.n = 7; }
    }
    return new Shape().n;
}
var a = method_constructor_return_object();

//--- method-constructor-argument-receiver.js
// A constructor receiver used as a method argument needs its own borrow proof.
function method_constructor_argument_receiver() {
    class Shape {
        constructor() { this.n = 7; this.n = this.init(this); }
        init(other) { other.n = 9; return this.n; }
    }
    return new Shape().n;
}
var a = method_constructor_argument_receiver();

//--- method-constructor-return-receiver.js
// Seeding the constructor receiver does not authorize a method to return it.
function method_constructor_return_receiver() {
    class Shape {
        constructor() { this.n = 7; this.n = this.init().n; }
        init() { return this; }
    }
    return new Shape().n;
}
var a = method_constructor_return_receiver();

//--- method-constructor-self-replace.js
// A constructor observes the live replacement on its second method call.
function method_constructor_self_replace() {
    class Shape {
        constructor() { this.n = this.read() * 10 + this.read(); }
        read() { this.read = function () { return 9; }; return 7; }
    }
    return new Shape().n;
}
var a = method_constructor_self_replace();

// Static reads retain their exact constructor receiver without exposing it.
//--- static-constant.js
function static_constant() {
    class Shape { static get n() { return 7; } }
    var instance = new Shape();
    return Shape.n;
}
var a = static_constant();

// Bootstrap's empty defaults allocate a fresh object at every source read.
//--- static-defaults.js
function static_defaults() {
    class Config {
        static get Default() {
            return {}
        }
        static get DefaultType() {
            return {}
        }
    }
    var instance = new Config();
    var first = Config.Default;
    first.n = 7;
    var second = Config.Default, types = Config.DefaultType;
    second.n = 2;
    types.n = 3;
    first.n = 9;
    return first.n * 100 + second.n * 10 + types.n;
}
var a = static_defaults();

// A dependency must clone its allocation for each read, in source order.
//--- static-defaults-chain.js
function static_defaults_chain() {
    class Config {
        static get DefaultType() { return this.Default; }
        static get Default() { return {}; }
    }
    var instance = new Config();
    var first = Config.DefaultType;
    first.n = 1;
    var second = Config.DefaultType;
    second.n = 2;
    var before = first.n * 10 + second.n;
    var third = Config.Default;
    third.n = 3;
    first.n = 4;
    return before * 1000 + first.n * 100 + second.n * 10 + third.n;
}
var a = static_defaults_chain();

// Fresh allocation does not prove the getter's property initialization effects.
//--- static-default-fields.js
function static_default_fields() {
    class Config { static get Default() { return {n: 7}; } }
    var instance = new Config();
    return Config.Default.n;
}
var a = static_default_fields();

// Bootstrap's NAME/DATA_KEY dependency starts with an ordinary base receiver.
//--- static-chain.js
function static_chain() {
    class Shape {
        static get NAME() { return "button"; }
        static get DATA_KEY() { return "bs." + this.NAME; }
    }
    var instance = new Shape();
    return (Shape.DATA_KEY === "bs.button") * 1;
}
var a = static_chain();

// Constructor accessors coexist with distinct mutable instance fields/methods.
//--- static-methods.js
function static_methods() {
    class Shape {
        constructor(n) { this.n = n; }
        read() { return this.n; }
        static get NAME() { return "button"; }
        static get DATA_KEY() { return "bs." + this.NAME; }
    }
    var first = new Shape(7), second = new Shape(2);
    first.n = 9;
    return (Shape.DATA_KEY === "bs.button") * 100 + first.read() * 10 + second.read();
}
var a = static_methods();

// A setter is executable behavior, even when it leaves the getter unchanged.
//--- static-setter.js
function static_setter() {
    class Shape {
        static get n() { return 7; }
        static set n(value) {}
    }
    var instance = new Shape();
    Shape.n = 9;
    return Shape.n;
}
var a = static_setter();

//--- static-duplicate.js
function static_duplicate() {
    class Shape { static get n() { return 7; } static get n() { return 9; } }
    var instance = new Shape();
    return Shape.n;
}
var a = static_duplicate();

// Mutable constructor fields need a separate initialization/use proof.
//--- static-write.js
function static_write() {
    class Shape { static get n() { return this.value; } }
    var instance = new Shape();
    Shape.value = 9;
    return Shape.n;
}
var a = static_write();

//--- static-alias-write.js
function static_alias_write() {
    class Shape { static get n() { return this.value; } }
    var instance = new Shape(), alias = Shape;
    alias.value = 11;
    return Shape.n;
}
var a = static_alias_write();

// Repeated getter evaluation must not be folded to a saved first result.
//--- static-effect.js
function static_effect() {
    class Shape {
        static get n() { var before = this.value; this.value = before + 2; return before; }
    }
    var instance = new Shape();
    Shape.value = 7;
    return Shape.n * 10 + Shape.n;
}
var a = static_effect();

//--- static-identity.js
function static_identity() {
    class Shape { static get n() { return 7; } }
    var instance = new Shape();
    var getter = Object.getOwnPropertyDescriptor(Shape, "n").get;
    return (getter === Object.getOwnPropertyDescriptor(Shape, "n").get) * 1;
}
var a = static_identity();

//--- static-descriptor.js
function static_descriptor() {
    class Shape { static get n() { return 7; } }
    var instance = new Shape();
    return Object.keys(Shape).length;
}
var a = static_descriptor();

// Returning the constructor cannot be justified by a scalar accessor read.
//--- static-receiver.js
function static_receiver() {
    class Shape { static get SELF() { return this; } }
    var instance = new Shape();
    return (Shape.SELF === Shape) * 1;
}
var a = static_receiver();

// An extracted getter receives the call site's object, not its lexical class.
//--- static-foreign-receiver.js
function static_foreign_receiver() {
    class Shape { static get n() { return this.value; } }
    var instance = new Shape();
    var getter = Object.getOwnPropertyDescriptor(Shape, "n").get;
    return getter.call({value: 11});
}
var a = static_foreign_receiver();

//--- static-captured.js
function static_captured() {
    var value = 7;
    class Shape { static get n() { return value; } }
    var instance = new Shape();
    return Shape.n;
}
var a = static_captured();

// The preparation pass must establish the property from its current IR.
//--- static-dynamic.js
function static_dynamic() {
    class Shape {
        static get NAME() { return 7; }
        static get n() { return this["NA" + "ME"]; }
    }
    var instance = new Shape();
    return Shape.n;
}
var a = static_dynamic();

// Uncalled cyclic getters still cannot supply an acyclic receiver proof.
//--- static-cycle.js
function static_cycle() {
    class Shape {
        static get first() { return this.second; }
        static get second() { return this.first; }
    }
    var instance = new Shape();
    return 7;
}
var a = static_cycle();

// Repeated reads must preserve the whole acyclic dependency expression.
//--- static-repeated.js
function static_repeated() {
    class Shape {
        static get NAME() { return "button"; }
        static get DATA_KEY() { return "bs." + this.NAME; }
    }
    var instance = new Shape();
    var first = Shape.DATA_KEY, second = Shape.DATA_KEY;
    return (first === "bs.button") * 10 + (second === first);
}
var a = static_repeated();

// Even unobserved getter bodies cannot acquire a pure-expression proof.
//--- static-global-effect.js
var count = 0;
function static_global_effect() {
    class Shape { static get n() { count = 3; return 7; } }
    var instance = new Shape();
    return Shape.n * 10 + count;
}
var a = static_global_effect();

// Dependency order comes from reads, including diamonds, not declaration order.
//--- static-forward-chain.js
function static_forward_chain() {
    class Shape {
        static get COMPLETE() { return this.DATA_KEY + "." + this.NAME; }
        static get DATA_KEY() { return "bs." + this.NAME; }
        static get NAME() { return "button"; }
    }
    var instance = new Shape();
    return (Shape.COMPLETE === "bs.button.button") * 1;
}
var a = static_forward_chain();

// Scalar getter expansion must preserve surrounding instance call order.
//--- static-order.js
function static_order() {
    class Shape {
        constructor() { this.n = 1; }
        write(n) { this.n = this.n * 10 + n; return this.n; }
        static get n() { return 2; }
    }
    var instance = new Shape();
    var first = instance.write(Shape.n);
    return first * 100 + instance.write(Shape.n + 1);
}
var a = static_order();

// A setter is outside the scalar getter proof even without an assignment.
//--- static-unused-setter.js
function static_unused_setter() {
    class Shape {
        static get n() { return 7; }
        static set n(value) {}
    }
    var instance = new Shape();
    return Shape.n;
}
var a = static_unused_setter();

// The VM reads function metadata before own accessors; preserve the discrepancy.
//--- static-name.js
function static_name() {
    class Shape { static get name() { return "replacement"; } }
    var instance = new Shape();
    return (Shape.name === "replacement") * 1;
}
var a = static_name();

//--- static-length.js
function static_length() {
    class Shape { static get length() { return 7; } }
    var instance = new Shape();
    return Shape.length;
}
var a = static_length();

// Internal home storage precedes the accessor in the current interpreter.
//--- static-home.js
function static_home() {
    class Shape { static get __home() { return 7; } }
    var instance = new Shape();
    return (Shape.__home === 7) * 1;
}
var a = static_home();

//--- static-caller.js
function static_caller() {
    class Shape { static get caller() { return 7; } }
    var instance = new Shape();
    return (Shape.caller === 7) * 1;
}
var a = static_caller();

//--- static-arguments.js
function static_arguments() {
    class Shape { static get arguments() { return 7; } }
    var instance = new Shape();
    return (Shape.arguments === 7) * 1;
}
var a = static_arguments();

// Every structured arm keeps the same proved receiver and immutable methods.
//--- method-branches.js
function method_branches() {
    class Shape {
        constructor(n) { this.n = n; }
        adjust(n) {
            if (n > 0) { this.n = this.n + n; }
            else { this.n = this.n - 1; }
            return this.n;
        }
    }
    var first = new Shape(1), second = new Shape(5);
    var left = first.adjust(2), right = second.adjust(-1);
    return left * 1000 + right * 100 + first.n * 10 + second.n;
}
var a = method_branches();

//--- method-loop.js
function method_loop() {
    class Shape {
        constructor(n) { this.n = n; }
        add(n) { this.n = this.n + n; return this.n; }
        adjust(count) {
            for (var i = 0; i < count; i = i + 1) {
                if (i === 1) { this.add(2); } else { this.add(1); }
            }
            return this.n;
        }
    }
    var first = new Shape(1), second = new Shape(5);
    var left = first.adjust(3), right = second.adjust(0);
    return left * 1000 + right * 100 + first.n * 10 + second.n;
}
var a = method_loop();

// Even uncalled nested effects remain in the complete source census.
//--- method-branch-ambient.js
function method_branch_ambient() {
    class Shape {
        constructor() { this.n = 7; }
        read(n) { if (n) { Math.abs(n); } return this.n; }
    }
    return new Shape().read(0);
}
var a = method_branch_ambient();

//--- method-branch-shadow.js
function method_branch_shadow() {
    class Shape {
        constructor() { this.n = 7; }
        read(n) { if (n) { this.read = n; } return this.n; }
    }
    return new Shape().read(0);
}
var a = method_branch_shadow();

// Constructors and static getters keep their existing linear-body contract.
//--- constructor-branch.js
function constructor_branch() {
    class Shape { constructor(n) { if (n) { this.n = 7; } else { this.n = 8; } } }
    return new Shape(1).n;
}
var a = constructor_branch();

//--- static-branch.js
function static_branch() {
    class Shape {
        static get Ready() { if (this.Other) { return 7; } return 8; }
        static get Other() { return 1; }
    }
    var instance = new Shape();
    return Shape.Ready;
}
var a = static_branch();

// The lift dispatches continue, break, early return and normal completion.
//--- method-dispatch.js
function method_dispatch() {
    class Shape {
        constructor() { this.n = 1; }
        read(limit) {
            for (var i = 0; i < limit; i = i + 1) {
                if (i === 1) { continue; }
                if (i === 3) { break; }
                if (limit === 5) { return this.n + 20; }
                this.n = this.n + i;
            }
            this.n = this.n + 10;
            return this.n;
        }
    }
    var zero = new Shape(), continued = new Shape(), broken = new Shape(), early = new Shape();
    return zero.read(0) * 1000000 + continued.read(3) * 10000 + broken.read(6) * 100 + early.read(5);
}
var a = method_dispatch();

// The recursive census includes every switch arm, even uncalled methods.
//--- method-dispatch-ambient.js
function method_dispatch_ambient() {
    class Shape {
        constructor() { this.n = 7; }
        read(limit) {
            for (var i = 0; i < limit; i = i + 1) {
                if (i === 1) { continue; }
                if (i === 3) { break; }
                if (limit === 5) { return Math.abs(this.n); }
            }
            return this.n;
        }
    }
    return new Shape().n;
}
var a = method_dispatch_ambient();

//--- method-dispatch-shadow.js
function method_dispatch_shadow() {
    class Shape {
        constructor() { this.n = 7; }
        read(limit) {
            for (var i = 0; i < limit; i = i + 1) {
                if (i === 1) { continue; }
                if (i === 3) { break; }
                if (limit === 5) { this.read = limit; return this.n; }
            }
            return this.n;
        }
    }
    return new Shape().n;
}
var a = method_dispatch_shadow();

// Literal throws preserve their outer exits through class setup preparation.
// Native completion lowering remains a separate proof.
//--- method-dispatch-throw.js
function method_dispatch_throw() {
    class Shape {
        constructor() { this.n = 7; }
        read(limit) {
            for (var i = 0; i < limit; i = i + 1) {
                if (i === 1) { continue; }
                if (i === 3) { throw 9; }
            }
            return this.n;
        }
    }
    return new Shape().read(0);
}
var a = method_dispatch_throw();

//--- method-throw-ambient.js
// An uncalled alternate exit still cannot invoke an ambient function.
function method_throw_ambient() {
    class Shape {
        read(flag) { if (flag) { external(); throw 9; } return 7; }
    }
    return new Shape().read(false);
}
var a = method_throw_ambient();

//--- method-throw-object.js
// Uncaught object formatting may reenter through toString.
function method_throw_object() {
    class Shape {
        read(flag) { if (flag) { throw {}; } return 7; }
    }
    return new Shape().read(false);
}
var a = method_throw_object();

//--- method-throw-parameter.js
function method_throw_parameter() {
    class Shape {
        read(flag, error) { if (flag) { throw error; } return 7; }
    }
    return new Shape().read(false, 9);
}
var a = method_throw_parameter();

//--- method-throw-default.js
// Getter reads in a copied method with multiple outer exits retain provenance.
function method_throw_default() {
    class Shape {
        static get Default() { return 7; }
        read(limit) {
            for (var i = 0; i < limit; i++) {
                if (i === 1) { continue; }
                if (i === 3) { throw 9; }
            }
            return this.constructor.Default;
        }
    }
    return new Shape().read(0);
}
var a = method_throw_default();

//--- method-increment-dispatch.js
// Increment and decrement retain the importer's non-reentering static Add.
function method_increment_dispatch() {
    class Shape {
        constructor() { this.n = 1; }
        read(limit) {
            for (var i = 0; i < limit; i++) {
                if (i === 1) { continue; }
                if (i === 3) { break; }
                if (limit === 5) { return this.n + 20; }
                this.n = this.n + i;
            }
            this.n = this.n + 10;
            return this.n;
        }
    }
    var zero = new Shape(), continued = new Shape(), broken = new Shape(), early = new Shape();
    return zero.read(0) * 1000000 + continued.read(3) * 10000 + broken.read(6) * 100 + early.read(5);
}
var a = method_increment_dispatch();

//--- method-decrement-dispatch.js
function method_decrement_dispatch() {
    class Shape {
        constructor() { this.n = 1; }
        read(limit) {
            for (var i = 0; i > limit; i--) {
                if (i === -1) { continue; }
                if (i === -3) { break; }
                if (limit === -5) { return this.n + 20; }
                this.n = this.n + i;
            }
            this.n = this.n + 10;
            return this.n;
        }
    }
    var zero = new Shape(), continued = new Shape(), broken = new Shape(), early = new Shape();
    return zero.read(0) * 1000000 + continued.read(-3) * 10000 + broken.read(-6) * 100 + early.read(-5);
}
var a = method_decrement_dispatch();

//--- method-counter-ambient.js
// Static counter admission cannot hide an ambient call in an uncalled method.
function method_counter_ambient() {
    class Shape {
        constructor() { this.n = 7; }
        read(limit) {
            for (var i = 0; i < limit; i++) {
                if (i === 1) { continue; }
                if (i === 3) { break; }
                if (limit === 5) { return Math.abs(this.n); }
            }
            return this.n;
        }
    }
    return new Shape().n;
}
var a = method_counter_ambient();

//--- receiver-defaults.js
function receiver_defaults() {
    class Config {
        static get Default() { return {}; }
        static get DefaultType() { return this.Default; }
        read() {
            var first = this.constructor.Default;
            first.n = 7;
            var second = this.constructor.Default, types = this.constructor.DefaultType;
            second.n = 2;
            types.n = 3;
            first.n = 9;
            return first.n * 100 + second.n * 10 + types.n;
        }
    }
    return new Config().read();
}
var a = receiver_defaults();

//--- instance-defaults.js
function instance_defaults() {
    class Config { static get Default() { return {}; } }
    var instance = new Config();
    var first = instance.constructor.Default, second = instance.constructor.Default;
    first.n = 7;
    second.n = 2;
    return first.n * 10 + second.n;
}
var a = instance_defaults();

//--- receiver-default-dispatch.js
// Getter reads inside copied dispatch bodies must follow their private clones.
function receiver_default_dispatch() {
    class Config {
        static get START() { return 1; }
        static get EARLY() { return 20; }
        static get TAIL() { return 10; }
        constructor() { this.n = this.constructor.START; }
        read(limit) {
            for (var i = 0; i < limit; i++) {
                if (i === 1) { continue; }
                if (i === 3) { break; }
                if (limit === 5) { return this.n + this.constructor.EARLY; }
                this.n = this.n + i;
            }
            return this.n + this.constructor.TAIL;
        }
    }
    var zero = new Config(), continued = new Config(), broken = new Config(), early = new Config();
    return zero.read(0) * 1000000 + continued.read(3) * 10000 + broken.read(6) * 100 + early.read(5);
}
var a = receiver_default_dispatch();

//--- receiver-default-shadow.js
// Even an uncalled method cannot change the constructor backedge.
function receiver_default_shadow() {
    class Config {
        static get Default() { return 7; }
        read() { return this.constructor.Default; }
        replace() { this.constructor = {}; }
    }
    return new Config().read();
}
var a = receiver_default_shadow();

//--- receiver-default-write.js
function receiver_default_write() {
    class Config {
        static get Default() { return 7; }
        read() { return this.constructor.Default; }
        replace() { this.constructor.Default = 9; }
    }
    return new Config().read();
}
var a = receiver_default_write();

//--- receiver-default-identity.js
function receiver_default_identity() {
    class Config {
        static get Default() { return 7; }
        read() { return this.constructor.Default; }
        identity() { return this.constructor; }
    }
    return new Config().read();
}
var a = receiver_default_identity();

//--- receiver-default-inherited.js
function receiver_default_inherited() {
    class Base { static get Default() { return 7; } }
    class Config extends Base {
        read() { return this.constructor.Default; }
    }
    return new Config().read();
}
var a = receiver_default_inherited();

//--- instance-default-replacement.js
// A constructor returning an object changes which constructor new exposes.
function instance_default_replacement() {
    class Config {
        constructor() { return {}; }
        static get Default() { return 7; }
    }
    return (typeof new Config().constructor.Default === "undefined") * 1;
}
var a = instance_default_replacement();

// Throwing getters retain calls and abrupt exits; unused getters may be removed.
//--- static-throw-unused.js
function static_throw_unused() {
    class Config { static get NAME() { throw new Error("NAME required"); } }
    var instance = new Config();
    instance.n = 7;
    return instance.n;
}
var a = static_throw_unused();

//--- static-throw-literal.js
function static_throw_literal() {
    class Config {
        static get NAME() { throw 9; }
        read() { return this.constructor.NAME; }
    }
    var instance = new Config();
    instance.n = 7;
    return instance.n;
}
var a = static_throw_literal();

//--- static-throw-error.js
function static_throw_error() {
    class Config {
        static get NAME() { throw new Error("NAME required"); }
        read() { return this.constructor.NAME; }
    }
    var instance = new Config();
    instance.n = 7;
    return instance.n;
}
var a = static_throw_error();

//--- static-throw-chain.js
function static_throw_chain() {
    class Config {
        static get NAME() { throw new Error("NAME required"); }
        static get Alias() { return this.NAME; }
        static get Top() { return this.Alias; }
        read() { return this.constructor.Top; }
    }
    var instance = new Config();
    instance.n = 7;
    return instance.n;
}
var a = static_throw_chain();

//--- static-throw-ambient.js
function static_throw_ambient() {
    class Config {
        static get NAME() { Math.abs(0); throw new Error("NAME required"); }
    }
    var instance = new Config();
    instance.n = 7;
    return instance.n;
}
var a = static_throw_ambient();

//--- static-throw-object.js
function static_throw_object() {
    class Config { static get NAME() { throw {}; } }
    var instance = new Config();
    instance.n = 7;
    return instance.n;
}
var a = static_throw_object();

//--- static-error-return.js
function static_error_return() {
    class Config { static get NAME() { return new Error("NAME required"); } }
    var instance = new Config();
    instance.n = 7;
    return instance.n;
}
var a = static_error_return();

//--- static-error-replaced.js
function static_error_replaced() {
    class Config { static get NAME() { throw new Error("NAME required"); } }
    var instance = new Config();
    instance.n = 7;
    Error = 9;
    return instance.n;
}
var a = static_error_replaced();

//--- static-error-coercion.js
function static_error_coercion() {
    class Config { static get NAME() { throw new Error({}); } }
    var instance = new Config();
    instance.n = 7;
    return instance.n;
}
var a = static_error_coercion();
