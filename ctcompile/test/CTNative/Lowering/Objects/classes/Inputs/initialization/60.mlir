// A decided arm's helper call does nothing for any construction's actual, so
// it goes with the arm; the helper, called nowhere else, never runs, and its
// ambient branch is not part of the census.
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

// Bootstrap's element/isElement shape: the nested helper's only call is in the
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

// A base only its leaf builds: the leaf's copy decides the arm, and the base
// original's call never runs.
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

// The call's result outlives the arm, so the call and the helper's census stay.
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

// Another live call keeps the helper's census.
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

// A holder slot whose only call sat in the dropped arm, like Bootstrap's Data
// `set`, never runs; the slot another method calls keeps its census.
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
