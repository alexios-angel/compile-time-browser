//--- override-constructor.js
function override_constructor() {
    class Base {
        constructor(n) { this.n = n; this.n = this.read() + 1; }
        read() { return this.n; }
    }
    class Derived extends Base {
        constructor(n) { super(n); }
        read() { return this.n * 2; }
    }
    return new Derived(7).n;
}
var a = override_constructor();

//--- override-middle.js
function override_middle() {
    class Base {
        constructor(n) { this.n = n; }
        read() { return 999; }
        forward() { return this.read() + this.n; }
    }
    class Middle extends Base {
        constructor(n) { super(n); }
        read() { return this.n * 2; }
    }
    class Derived extends Middle { constructor(n) { super(n); } }
    return new Derived(7).forward();
}
var a = override_middle();

//--- override-leaf.js
function override_leaf() {
    class Base {
        constructor(n) { this.n = n; }
        read() { return 999; }
        forward() { return this.read() + this.n; }
    }
    class Middle extends Base {
        constructor(n) { super(n); }
        read() { return this.n * 2; }
    }
    class Derived extends Middle {
        constructor(n) { super(n); }
        read() { return this.n * 3; }
    }
    return new Derived(7).forward();
}
var a = override_leaf();

//--- override-distinct.js
function override_distinct() {
    class Base { constructor(n) { this.n = n; } read() { return this.n; } }
    class Derived extends Base {
        constructor(n) { super(n); }
        read() { return this.n * 2; }
    }
    return new Base(7).read() + new Derived(10).read();
}
var a = override_distinct();

//--- override-ambient.js
function override_ambient() {
    class Base { constructor(n) { this.n = n; } read() { return ambient(); } }
    class Derived extends Base {
        constructor(n) { super(n); }
        read() { return this.n; }
    }
    return new Derived(7).read();
}
var a = override_ambient();

//--- override-shadowed-receiver.js
function override_shadowed_receiver() {
    class Base { constructor(n) { this.n = n; } read() { this.leaf = 9; } }
    class Derived extends Base {
        constructor(n) { super(n); }
        read() { return this.n; }
        leaf() { return 1; }
    }
    return new Derived(7).read();
}
var a = override_shadowed_receiver();

//--- override-hidden-ancestor.js
function override_hidden_ancestor() {
    class Base { constructor(n) { this.n = n; } read() { this.leaf = 9; } }
    class Middle extends Base {
        constructor(n) { super(n); }
        read() { return this.n; }
    }
    class Derived extends Middle {
        constructor(n) { super(n); }
        leaf() { return 1; }
    }
    return new Derived(7).read();
}
var a = override_hidden_ancestor();

//--- override-different-leaves.js
function override_different_leaves() {
    class Base {
        constructor(n) { this.n = n; }
        read() { return this.n; }
        forward() { return this.read(); }
    }
    class Left extends Base {
        constructor(n) { super(n); }
        read() { return this.n * 2; }
    }
    class Right extends Base {
        constructor(n) { super(n); }
        read() { return this.n * 3; }
    }
    return new Left(7).forward() + new Right(10).forward();
}
var a = override_different_leaves();

//--- override-unused-constructor.js
function override_unused_constructor() {
    class Base {
        constructor(n) { this.n = n; }
        read() { this.constructor; return this.n; }
    }
    class Derived extends Base {
        constructor(n) { super(n); }
        read() { return this.n; }
    }
    return new Derived(7).read();
}
var a = override_unused_constructor();

//--- inherited-method-leaf.js
function inherited_method_leaf() {
    class Base { constructor(n) { this.n = n; } read() { return this.n; } }
    class Derived extends Base {
        constructor(n) { super(n); }
        twice() { return this.read() * 2; }
    }
    return new Derived(7).twice();
}
var a = inherited_method_leaf();

//--- inherited-method-unused-constructor.js
function inherited_method_unused_constructor() {
    class Base {
        constructor(n) { this.n = n; }
        read() { this.constructor; return this.n; }
    }
    class Derived extends Base { constructor(n) { super(n); } }
    return new Derived(7).read();
}
var a = inherited_method_unused_constructor();

//--- inherited-method-leaf-shadow.js
function inherited_method_leaf_shadow() {
    class Base {
        constructor(n) { this.n = n; }
        read() { return this.n; }
        unused() { this.leaf = 9; }
    }
    class Derived extends Base {
        constructor(n) { super(n); }
        leaf() { return 1; }
    }
    return new Derived(7).read();
}
var a = inherited_method_leaf_shadow();

//--- inherited-method-base-shadow.js
function inherited_method_base_shadow() {
    class Base { constructor(n) { this.n = n; this.leaf = 9; } read() { return this.n; } }
    class Derived extends Base {
        constructor(n) { super(n); }
        leaf() { return 1; }
    }
    return new Derived(7).read();
}
var a = inherited_method_base_shadow();

//--- inherited-method.js
function inherited_method() {
    class Base { constructor(n) { this.n = n; } read() { return this.n; } }
    class Derived extends Base { constructor(n) { super(n); } }
    return new Derived(7).read();
}
var a = inherited_method();

//--- inherited-method-chain.js
function inherited_method_chain() {
    class Base {
        constructor(n) { this.n = n; }
        read() { return this.n; }
        forward() { return this.read() + this.n; }
    }
    class Middle extends Base { constructor(n) { super(n + 1); } }
    class Derived extends Middle { constructor(n) { super(n); this.n = this.n + 2; } }
    return new Derived(4).forward();
}
var a = inherited_method_chain();

//--- inherited-method-instances.js
function inherited_method_instances() {
    class Base { constructor(n) { this.n = n; } read() { return this.n; } }
    class Derived extends Base { constructor(n) { super(n); } }
    var base = new Base(2), first = new Derived(7), second = new Derived(3);
    return base.read() * 100 + first.read() * 10 + second.read();
}
var a = inherited_method_instances();

//--- inherited-method-constructor.js
function inherited_method_constructor() {
    class Base {
        constructor(n) { this.n = n; this.n = this.read() + 1; }
        read() { return this.n; }
    }
    class Derived extends Base { constructor(n) { super(n); } }
    return new Derived(6).read();
}
var a = inherited_method_constructor();

//--- inherited-method-override.js
function inherited_method_override() {
    class Base {
        constructor(n) { this.n = n; }
        read() { return this.n; }
        forward() { return this.read() + this.n; }
    }
    class Derived extends Base {
        constructor(n) { super(n); }
        read() { return this.n * 2; }
    }
    return new Derived(7).forward();
}
var a = inherited_method_override();

//--- inherited-method-ambient.js
function inherited_method_ambient() {
    class Base {
        constructor(n) { this.n = n; }
        read() { return this.n; }
        unused() { return ambient(); }
    }
    class Derived extends Base { constructor(n) { super(n); } }
    return new Derived(7).read();
}
var a = inherited_method_ambient();

//--- inherited-method-getter.js
function inherited_method_getter() {
    class Base {
        constructor(n) { this.n = n; }
        static get Ready() { return 1; }
        read() { return this.n + this.constructor.Ready; }
    }
    class Derived extends Base { constructor(n) { super(n); } }
    return new Derived(7).read();
}
var a = inherited_method_getter();

//--- inherited-method-shadow.js
function inherited_method_shadow() {
    class Base { constructor(n) { this.n = n; } read() { return this.n; } }
    class Derived extends Base { constructor(n) { super(n); } }
    var instance = new Derived(7);
    instance.read = function replacement() { return 9; };
    return instance.read();
}
var a = inherited_method_shadow();

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

// Explicit super is separate from the default derived rest/apply constructor.
// The base is completed but never directly constructed.
//--- inherited-explicit.js
function inherited_explicit() {
    class Base { constructor(n) { this.n = n; } }
    class Derived extends Base { constructor(n) { super(n); } }
    return new Derived(7).n;
}
var a = inherited_explicit();

// Base effects run on the final receiver before the original derived suffix.
//--- inherited-order.js
function inherited_order() {
    class Base { constructor(n) { this.n = n; } }
    class Derived extends Base {
        constructor(n) { var x = n * 10; super(x + 1); this.n = this.n * 10 + 2; }
    }
    return new Derived(3).n;
}
var a = inherited_order();

// Each completed base body composes onto the same final receiver.
//--- inherited-chain.js
function inherited_chain() {
    class Base { constructor(n) { this.n = n; } }
    class Middle extends Base { constructor(n) { super(n + 1); this.n += 2; } }
    class Derived extends Middle { constructor(n) { super(n * 2); this.n += 4; } }
    return new Derived(3).n;
}
var a = inherited_chain();

// A separately constructed base must retain its original callable body.
//--- inherited-base-instance.js
function inherited_base_instance() {
    class Base { constructor(n) { this.n = n; } }
    class Derived extends Base { constructor(n) { super(n); } }
    var first = new Base(2), second = new Derived(7);
    return first.n * 10 + second.n;
}
var a = inherited_base_instance();

// Observing new.target is outside the current receiver-only normalization.
//--- inherited-new-target.js
function inherited_new_target() {
    class Base { constructor(n) { this.n = new.target === Base ? n : n + 1; } }
    class Derived extends Base { constructor(n) { super(n); } }
    return new Derived(7).n;
}
var a = inherited_new_target();

// Replacement objects, explicit primitive base returns and fields stay refused.
//--- inherited-replacement.js
function inherited_replacement() {
    class Base { constructor(n) { this.n = n; return {n: 9}; } }
    class Derived extends Base { constructor(n) { super(n); } }
    return new Derived(7).n;
}
var a = inherited_replacement();

//--- inherited-number-return.js
function inherited_number_return() {
    class Base { constructor(n) { this.n = n; return 3; } }
    class Derived extends Base { constructor(n) { super(n); } }
    return new Derived(7).n;
}
var a = inherited_number_return();

//--- inherited-fields.js
function inherited_fields() {
    class Base { constructor(n) { this.n = n; } }
    class Derived extends Base { n = 9; constructor(n) { super(n); } }
    return new Derived(7).n;
}
var a = inherited_fields();

//--- inherited-missing-super.js
function inherited_missing_super() {
    class Base { constructor(n) { this.n = n; } }
    class Derived extends Base { constructor(n) { this.n = n; } }
    return new Derived(7).n;
}
var a = inherited_missing_super();

//--- inherited-double-super.js
function inherited_double_super() {
    class Base { constructor(n) { this.n = n; } }
    class Derived extends Base { constructor(n) { super(n); super(n + 1); } }
    return new Derived(7).n;
}
var a = inherited_double_super();

// B's constructor dispatches through the leaf receiver, while super dispatches
// through the declaring method's lexical home. Keep the shadowed W method too.
//--- inherited-dispatch.js
function inherited_dispatch() {
    class W {
        _mergeConfigObj(t) { return t + 1; }
        _getConfig(t) { return 999; }
    }
    class B extends W {
        constructor(t) { super(); this.value = this._getConfig(t); }
        _getConfig(t) { return this._mergeConfigObj(t) * 2; }
    }
    class Qi extends B {
        constructor(t) { super(t); this.value += 10; }
        _getConfig(t) { return super._getConfig(t) + 100; }
    }
    return new Qi(3).value;
}
var a = inherited_dispatch();

// Preserve the formerly differing Button static-inheritance observation:
// both Node and the VM now use Derived as the inherited getter's receiver.
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

// Capturing the local class keeps its original repeated constructor stores.
// Calls on separate instances must resolve the same fully proved getter.
//--- method-captured-class-name.js
function method_captured_class_name() {
    class Button {
        static get NAME() { return "button"; }
        read() { return Button.NAME; }
    }
    var first = new Button(), second = new Button();
    return (first.read() === "button") * 10 + (second.read() === first.read());
}
var a = method_captured_class_name();

// Captured getter selection still expands the original acyclic getter body.
//--- method-captured-class-key.js
function method_captured_class_key() {
    class Button {
        static get NAME() { return "button"; }
        static get DATA_KEY() { return "bs." + this.NAME; }
        read() { return Button.DATA_KEY; }
    }
    var instance = new Button();
    return (instance.read() === "bs.button") * 10 + (instance.read() === Button.DATA_KEY);
}
var a = method_captured_class_key();

// A later value in the captured cell is observable at invocation time.
//--- method-captured-class-replaced.js
function method_captured_class_replaced() {
    let Button = class {
        static get NAME() { return 7; }
        read() { return Button.NAME; }
    };
    var instance = new Button();
    Button = {NAME: 9};
    return instance.read();
}
var a = method_captured_class_replaced();

// Uncalled writers must still enter the complete captured-cell census.
//--- method-captured-class-writer.js
function method_captured_class_writer() {
    class Button {
        static get NAME() { return 7; }
        read() { return Button.NAME; }
    }
    function replace() { Button = {NAME: 9}; }
    return new Button().read();
}
var a = method_captured_class_writer();

// Captured class identity may select getters, but cannot escape as a value.
//--- method-captured-class-identity.js
function method_captured_class_identity() {
    class Button { read() { return Button; } }
    return (new Button().read() === Button) * 1;
}
var a = method_captured_class_identity();

// A captured ordinary object does not acquire class/getter authority.
//--- method-captured-object.js
function method_captured_object() {
    const value = {NAME: 7};
    class Button { read() { return value.NAME; } }
    return new Button().read();
}
var a = method_captured_object();

// Both unused methods and their cyclic getter dependencies remain original.
//--- method-captured-class-cycle.js
function method_captured_class_cycle() {
    class Button {
        static get FIRST() { return this.SECOND; }
        static get SECOND() { return this.FIRST; }
        unused() { return Button.FIRST; }
    }
    var instance = new Button();
    return 7;
}
var a = method_captured_class_cycle();

// An unused captured method cannot hide an unknown effect behind its getter.
//--- method-captured-class-ambient.js
function method_captured_class_ambient() {
    class Button {
        static get NAME() { return 7; }
        read() { return Button.NAME; }
        unused() { ambient(Button.NAME); }
    }
    return new Button().read();
}
var a = method_captured_class_ambient();

// A constructor's replacement object never inherits the class's methods.
