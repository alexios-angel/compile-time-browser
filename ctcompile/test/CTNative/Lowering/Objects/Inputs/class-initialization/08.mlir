// Nested captures admit only immutable sibling calls, not arbitrary environments.
//--- nested-helper-constructor.js
function nested_helper_constructor() {
    const plus = n => n + 1;
    const wrap = n => plus(n);
    class Shape { constructor(n) { this.n = wrap(n); } }
    return new Shape(7).n;
}
var a = nested_helper_constructor();

// Each inherited wrapper's slot zero still names its own nested helper.
//--- inherited-nested-helper-distinct.js
function inherited_nested_helper_distinct() {
    const plus = n => n + 1;
    const scale = n => n * 10;
    const base = n => plus(n);
    const leaf = n => scale(n);
    class Base { constructor(n) { this.n = base(n); } }
    class Leaf extends Base {
        constructor(n) { super(n); this.n = leaf(this.n); }
    }
    return new Leaf(7).n;
}
var a = inherited_nested_helper_distinct();

//--- nested-helper-method.js
function nested_helper_method() {
    const plus = n => n + 1;
    const wrap = n => plus(n);
    class Shape { read(n) { return wrap(n) * 2; } }
    return new Shape().read(7);
}
var a = nested_helper_method();

// All three calls and their value dependencies survive the complete census.
//--- nested-helper-chain.js
function nested_helper_chain() {
    const plus = n => n + 1;
    const middle = n => plus(n) * 10;
    const outer = n => middle(n) + 3;
    class Shape { constructor(n) { this.n = outer(n); } }
    return new Shape(7).n;
}
var a = nested_helper_chain();

// Shared nested targets are proved once and keep both callers' identities.
//--- nested-helper-shared.js
function nested_helper_shared() {
    const plus = n => n + 1;
    const left = n => plus(n) * 10;
    const right = n => plus(n) + 3;
    class Shape {
        constructor(n) { this.n = left(n); }
        read(n) { return this.n + right(n); }
    }
    return new Shape(2).read(4);
}
var a = nested_helper_shared();

// The outer helper's two capture slots share the same leaf through both paths.
//--- nested-helper-diamond.js
function nested_helper_diamond() {
    const plus = n => n + 1;
    const left = n => plus(n) * 10;
    const right = n => plus(n) + 3;
    const combine = n => left(n) + right(n);
    class Shape { constructor(n) { this.n = combine(n); } }
    return new Shape(2).n;
}
var a = nested_helper_diamond();

// Nested call conversion preserves left-to-right argument effects exactly once.
//--- nested-helper-order.js
function nested_helper_order() {
    const combine = (left, right) => left * 100 + right;
    const wrap = (left, right) => combine(left, right);
    class Shape { constructor(left, right) { this.n = wrap(left, right); } }
    var value = 0;
    var shape = new Shape(value = value + 1, value = value + 1);
    return shape.n * 10 + value;
}
var a = nested_helper_order();

//--- nested-helper-changing.js
function nested_helper_changing() {
    let plus = n => n + 1;
    const wrap = n => plus(n);
    class Shape { constructor(n) { this.n = wrap(n); } }
    plus = n => n + 2;
    return new Shape(7).n;
}
var a = nested_helper_changing();

// An uncalled sibling writer still prevents freezing the inner binding.
//--- nested-helper-writer.js
function nested_helper_writer() {
    let plus = n => n + 1;
    const wrap = n => plus(n);
    const replace = () => { plus = n => n + 2; };
    class Shape { constructor(n) { this.n = wrap(n); } }
    return new Shape(7).n;
}
var a = nested_helper_writer();

//--- nested-helper-identity.js
function nested_helper_identity() {
    const plus = n => n + 1;
    const wrap = n => plus === plus ? n + 1 : 0;
    class Shape { constructor(n) { this.n = wrap(n); } }
    return new Shape(7).n;
}
var a = nested_helper_identity();

//--- nested-helper-excess.js
function nested_helper_excess() {
    const plus = n => n + 1;
    const wrap = n => plus(n, 2);
    class Shape { constructor(n) { this.n = wrap(n); } }
    return new Shape(7).n;
}
var a = nested_helper_excess();

// Nested calls do not grant authority for an arbitrary closure environment.
//--- nested-helper-primitive.js
function nested_helper_primitive() {
    const offset = 1;
    const plus = n => n + offset;
    const wrap = n => plus(n);
    class Shape { constructor(n) { this.n = wrap(n); } }
    return new Shape(7).n;
}
var a = nested_helper_primitive();

// Untaken receiver and ambient-effect arms need proof in nested bodies too.
//--- nested-helper-receiver.js
function nested_helper_receiver() {
    const read = function(n) { if (n) return this.n; return 7; };
    const wrap = n => read(n);
    class Shape { constructor(n) { this.n = wrap(n); } }
    return new Shape(0).n;
}
var a = nested_helper_receiver();

//--- nested-helper-effect.js
function nested_helper_effect() {
    const unsafe = n => { if (n) unknown(n); return 7; };
    const wrap = n => unsafe(n);
    class Shape { constructor(n) { this.n = wrap(n); } }
    return new Shape(0).n;
}
var a = nested_helper_effect();

//--- nested-helper-newtarget.js
function nested_helper_newtarget() {
    const plus = function(n) { return new.target === undefined ? n + 1 : 0; };
    const wrap = n => plus(n);
    class Shape { constructor(n) { this.n = wrap(n); } }
    return new Shape(7).n;
}
var a = nested_helper_newtarget();

// A terminating observation does not establish a complete recursive proof.
//--- nested-helper-recursive.js
function nested_helper_recursive() {
    const first = n => n ? second(n - 1) : 7;
    const second = n => first(n);
    class Shape { constructor(n) { this.n = first(n); } }
    return new Shape(0).n;
}
var a = nested_helper_recursive();
