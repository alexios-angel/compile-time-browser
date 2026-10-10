// When every `new` takes the same branch, the helper call in the branch never taken is
// removed with it. The helper, called nowhere else, never runs, so the class proof
// does not examine its call to `ambient`.
//--- helper-decided-uncalled.js
function helper_decided_uncalled() {
    const pick = t => typeof t == "string" ? ambient(t) : null;
    class Shape {
        constructor(t) { this.n = 7; (t = pick(t)) && (this.n = t); }
        read() { return this.n; }
    }
    return new Shape(null).read();
}
var a = helper_decided_uncalled();

// Bootstrap's element/isElement shape: the nested helper is called only from the
// uncalled one, so neither runs.
//--- helper-decided-nested-uncalled.js
function helper_decided_nested_uncalled() {
    const isElement = t => !(!t || "object" != typeof t) && void 0 !== t.nodeType;
    const element = t => isElement(t) ? t : "string" == typeof t && t.length > 0 ? document.querySelector(t) : null;
    class Shape {
        constructor(t) { this.n = 7; (t = element(t)) && (this.n = t); }
        read() { return this.n; }
    }
    return new Shape(null).read();
}
var a = helper_decided_nested_uncalled();

// A base built only through its subclass: the subclass's copy decides the branch,
// and the call in the base's own constructor never runs.
//--- inherited-helper-decided-leaf-built.js
function inherited_helper_decided_leaf_built() {
    const pick = t => typeof t == "string" ? ambient(t) : null;
    class Base {
        constructor(t) { this.n = 7; (t = pick(t)) && (this.n = t); }
        read() { return this.n; }
    }
    class Leaf extends Base { constructor(t) { super(t); } }
    return new Leaf(null).read();
}
var a = inherited_helper_decided_leaf_built();

// The call's result outlives the branch, so the call stays and the helper is examined.
//--- helper-decided-live-result.js
function helper_decided_live_result() {
    const pick = t => typeof t == "string" ? ambient(t) : null;
    class Shape {
        constructor(t) { this.n = 7; (t = pick(t)) && (this.n = 1); this.m = t; }
        read() { return this.n; }
    }
    return new Shape(null).read();
}
var a = helper_decided_live_result();

// Another live call to the helper means it is still examined.
//--- helper-decided-other-call.js
function helper_decided_other_call() {
    const pick = t => typeof t == "string" ? ambient(t) : null;
    class Shape {
        constructor(t) { this.n = 7; (t = pick(t)) && (this.n = t); }
        read() { return pick(this.n) === null ? this.n : 0; }
    }
    return new Shape(null).read();
}
var a = helper_decided_other_call();

// A Data method whose only call sat in the removed branch, like Bootstrap's Data
// `set`, never runs; the method another method calls is still examined.
//--- helper-decided-holder-slot.js
function helper_decided_holder_slot() {
    const pick = t => typeof t == "string" ? t : null;
    const Data = { set(k, v) { ambient(k, v); }, read(k) { return k; } };
    class Shape {
        constructor(t) { this.n = 7; (t = pick(t)) && Data.set(t, 1); }
        read() { return Data.read(this.n); }
    }
    return new Shape(null).read();
}
var a = helper_decided_holder_slot();
