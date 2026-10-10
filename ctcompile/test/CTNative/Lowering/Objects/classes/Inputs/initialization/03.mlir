
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

//--- static-throw-unused-chain.js
function static_throw_unused_chain() {
    class Config {
        static get NAME() { throw new Error("NAME required"); }
        static get Alias() { return this.NAME; }
        static get Top() { return this.Alias; }
    }
    var instance = new Config();
    instance.n = 7;
    return instance.n;
}
var a = static_throw_unused_chain();

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

//--- static-error-method.js
function static_error_method() {
    class Config { fail() { throw new Error("NAME required"); } }
    var instance = new Config();
    instance.n = 7;
    return instance.n;
}
var a = static_error_method();

//--- local-helper-arguments.js
function plus(n) { return n + 1; }
function helperArguments() {
    class Shape { read(n) { return plus(n); } }
    var instance = new Shape();
    return instance.read(7);
}
var a = helperArguments();

//--- local-helper-branches.js
function choose(n) { if (n > 0) { return n + 1; } return 2; }
function helperBranches() {
    class Shape { read(n) { return choose(n); } }
    var instance = new Shape();
    return instance.read(7) * 10 + instance.read(0);
}
var a = helperBranches();

//--- local-helper-order.js
function bump(box) { box.n = box.n + 1; return box.n; }
function combine(left, right) { return left * 10 + right; }
function helperOrder() {
    class Shape { read(left, right) { return combine(left, right); } }
    var instance = new Shape(), box = {n: 0};
    return instance.read(bump(box), bump(box)) * 10 + box.n;
}
var a = helperOrder();

//--- local-helper-replaced.js
function plus(n) { return n + 1; }
plus = function(n) { return n + 2; };
function helperReplaced() {
    class Shape { read(n) { return plus(n); } }
    var instance = new Shape();
    return instance.read(7);
}
var a = helperReplaced();

// The helper is never called by this entry; its body must still be checked.
//--- local-helper-ambient.js
function unsafe(n) { unknown(n); return n; }
function helperAmbient() {
    class Shape { read(n) { return unsafe(n); } }
    var instance = new Shape();
    return 7;
}
var a = helperAmbient();

//--- local-helper-receiver.js
const read = n => this.n + n;
function helperReceiver() {
    class Shape { read(n) { return read(n); } }
    var instance = new Shape();
    return 7;
}
var a = helperReceiver();

// Scalar assignment effects keep their left-to-right order through the helper call.
//--- local-helper-values.js
function combine(left, right) { return left * 10 + right; }
function helperValues() {
    class Shape { read(left, right) { return combine(left, right); } }
    var instance = new Shape(), value = 0;
    return instance.read(value = value + 1, value = value + 1) * 10 + value;
}
var a = helperValues();

//--- local-helper-dynamic-key.js
function select(object, key) { return object[key]; }
function helperDynamicKey() {
    class Shape { read(object, key) { return select(object, key); } }
    var instance = new Shape();
    return 7;
}
var a = helperDynamicKey();

// Fresh callable holders share the DOM source proof. Class preparation leaves
// their source operations intact for the existing native closure lifter.
//--- local-holder-method.js
function holderMethod() {
    class Shape {}
    var instance = new Shape();
    const H = {read(n) { return n + 1; }};
    return H.read(7);
}
var a = holderMethod();

//--- local-holder-arrow.js
function holderArrow() {
    class Shape {}
    var instance = new Shape();
    const H = {read: n => n + 1};
    return H.read(7);
}
var a = holderArrow();

//--- local-holder-branches.js
function holderBranches() {
    class Shape {}
    var instance = new Shape();
    const H = {read(n) { if (n > 0) { return n + 1; } return 2; }};
    return H.read(7) * 10 + H.read(0);
}
var a = holderBranches();

//--- local-holder-order.js
function holderOrder() {
    class Shape {}
    var instance = new Shape(), n = 0;
    const H = {read: (left, right) => left * 10 + right};
    return H.read(n = n + 1, n = n + 1);
}
var a = holderOrder();

//--- local-holder-replaced.js
function holderReplaced() {
    class Shape {}
    var instance = new Shape();
    const H = {read: n => n + 1};
    H.read = n => n + 2;
    return H.read(7);
}
var a = holderReplaced();

//--- local-holder-alias.js
function holderAlias() {
    class Shape {}
    var instance = new Shape();
    const H = {read: n => n + 1}, alias = H;
    alias.read = n => n + 2;
    return H.read(7);
}
var a = holderAlias();

//--- local-holder-detached.js
function holderDetached() {
    class Shape {}
    var instance = new Shape();
    const H = {read: n => n + 1}, read = H.read;
    return read(7);
}
var a = holderDetached();

//--- local-holder-receiver.js
function holderReceiver() {
    class Shape {}
    var instance = new Shape();
    const H = {read(n) { return this.other(n); }, other(n) { return n + 1; }};
    return H.read(7);
}
var a = holderReceiver();

// The uncalled slot still has its complete body checked.
//--- local-holder-ambient.js
function holderAmbient() {
    class Shape {}
    var instance = new Shape();
    const H = {read: n => n, unused(n) { return unknown(n); }};
    return H.read(7);
}
var a = holderAmbient();

// Publish every fixed slot before reaching the holder through closed calls.
//--- local-holder-global.js
const H = {read: n => n + 1};
function holderGlobal() {
    class Shape {}
    var instance = new Shape();
    return H.read(7);
}
var a = holderGlobal();

//--- global-holder-methods.js
const H = {read(n) { return n + 1; }, combine: (left, right) => left * 10 + right};
function holderGlobalMethods() {
    class Shape {}
    var instance = new Shape();
    return H.combine(H.read(7), H.read(0));
}
var a = holderGlobalMethods();

// A function's declaration may precede publication; all calls must follow it.
//--- global-holder-chain.js
function readHolder(n) { return H.read(n); }
function holderGlobalChain() {
    class Shape {}
    var instance = new Shape();
    return readHolder(7);
}
const H = {read: n => n + 1};
var a = holderGlobalChain();

//--- global-holder-order.js
const H = {read: (left, right) => left * 10 + right};
function holderGlobalOrder() {
    class Shape {}
    var instance = new Shape(), n = 0;
    return H.read(n = n + 1, n = n + 1) * 10 + n;
}
var a = holderGlobalOrder();

// Holder reads inside exit-dispatch clones retain targets, including short calls.
// Unused slots still undergo the complete source-effect census.
//--- global-holder-dispatch.js
const H = {
    read(n) { return n + 1; },
    empty(n) { return 3; },
    unused(n) { return n * 2; }
};
function holderGlobalDispatch() {
    class Shape {
        read(limit) {
            for (var i = 0; i < limit; i = i + 1) {
                if (i === 1) { continue; }
                if (i === 3) { break; }
                if (limit === 5) { return H.empty(); }
            }
            return H.read(7);
        }
    }
    var instance = new Shape();
    return instance.read(5) * 10 + instance.read(0);
}
var a = holderGlobalDispatch();

// Surplus argument frame semantics remain outside this bounded normalization.
//--- global-holder-surplus.js
const H = {read: n => n + 1};
function holderGlobalSurplus() {
    class Shape {}
    var instance = new Shape();
    return H.read(7, 9);
}
var a = holderGlobalSurplus();

//--- global-holder-early.js
function holderGlobalEarly() {
    class Shape {}
    var instance = new Shape();
    return H.read(7);
}
var a = holderGlobalEarly();
var H = {read: n => n + 1};

// A direct caller census cannot ignore invocation through an escaped callback.
//--- global-holder-indirect-early.js
function holderGlobalIndirectEarly() {
    class Shape {}
    var instance = new Shape();
    return H.read(7);
}
function invoke(callback) { return callback(); }
var a = invoke(holderGlobalIndirectEarly);
var H = {read: n => n + 1};

// Even an unrelated pure call ends the initial non-reentrant publication prefix.
//--- global-holder-prefix-call.js
function warmup() { return 1; }
warmup();
const H = {read: n => n + 1};
function holderGlobalPrefixCall() {
    class Shape {}
    var instance = new Shape();
    return H.read(7);
}
var a = holderGlobalPrefixCall();

// A slot added after publication stays outside the fixed-object proof even
// when this particular call happens after the additional store.
//--- global-holder-late-slot.js
const H = {};
H.read = n => n + 1;
function holderGlobalLateSlot() {
    class Shape {}
    var instance = new Shape();
    return H.read(7);
}
var a = holderGlobalLateSlot();

//--- global-holder-replaced.js
var H = {read: n => n + 1};
H = {read: n => n + 2};
function holderGlobalReplaced() {
    class Shape {}
    var instance = new Shape();
    return H.read(7);
}
var a = holderGlobalReplaced();

//--- global-holder-slot-replaced.js
const H = {read: n => n + 1};
H.read = n => n + 2;
function holderGlobalSlotReplaced() {
    class Shape {}
    var instance = new Shape();
    return H.read(7);
}
var a = holderGlobalSlotReplaced();

//--- global-holder-alias.js
const H = {read: n => n + 1};
function holderGlobalAlias() {
    class Shape {}
    var instance = new Shape(), alias = H;
    alias.read = n => n + 2;
    return H.read(7);
}
var a = holderGlobalAlias();

//--- global-holder-detached.js
const H = {read: n => n + 1};
function holderGlobalDetached() {
    class Shape {}
    var instance = new Shape(), read = H.read;
    return read(7);
}
var a = holderGlobalDetached();

//--- global-holder-identity.js
const H = {read: n => n + 1};
function holderGlobalIdentity() {
    class Shape {}
    var instance = new Shape();
    return (H === H) * 1;
}
var a = holderGlobalIdentity();

//--- global-holder-receiver.js
const H = {read(n) { return this ? n + 1 : 0; }};
function holderGlobalReceiver() {
    class Shape {}
    var instance = new Shape();
    return H.read(7);
}
var a = holderGlobalReceiver();

// The uncalled global slot still requires a complete effect census.
//--- global-holder-ambient.js
const H = {read: n => n, unused(n) { return unknown(n); }};
function holderGlobalAmbient() {
    class Shape {}
    var instance = new Shape();
    return H.read(7);
}
var a = holderGlobalAmbient();
