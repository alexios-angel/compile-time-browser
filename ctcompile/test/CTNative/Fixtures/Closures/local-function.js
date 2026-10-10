
// Closure-lifting fixture: a local binding that holds a function, checked end
// to end by the compilation-unit test (Checks/compilation-unit.py).
//
// `var f = function () { ... };` inside a function body is a hoisted local, and
// `compiler_impl::declare_local` boxes it exactly when a nested function
// mentions the name (`is_captured`, capture.cpp). The IR is then a
// `ctjs.create_cell` of `undefined`, a `ctjs.create_closure`, a
// `ctjs.cell_set` storing the closure in the cell, and a `ctjs.cell_get` (same
// frame) or a `ctjs.load_upvalue` (nested frame) wherever the name is used.
// Without the rule tested here, such a closure is refused because it reaches
// `ctjs.cell_set`; in the Bootstrap workload that was the most common closure
// refusal.
//
// A binding written once with a closure, and only ever called through, is that
// function. It is the function-declaration case one scope in: a create_closure
// whose one use is a `ctjs.store_global` already lowers to nothing, because the
// closed world names the callee and every call of it is direct. The box adds
// nothing a call needs, so the box, its store and its reads lower to nothing
// too, and each call through the name becomes a `ctjs.call_direct` of the target.
//
// Every binding here is read from another frame, and that is forced. A local is
// boxed only when a nested function mentions it, and this rule admits a mention
// only when it is a call, so a binding it can take is always one that some
// nested function calls. That is why every capture list below is either empty
// or made of other such bindings: a call there has no captured value to pass as
// a leading argument, and a binding this rule erases is not a value at all.
// `chain27` combines several such bindings, and `recur15` is the degenerate
// case, where the one capture is the binding being defined.
//
// The answers, worked out by hand and not asserted anywhere (the interpreter is
// the reference). Eight globals with eight distinct values, so a wrong one
// cannot hide behind a right one:
//   both405 = 405       chain27 = 27       declared32 = 32    deep115 = 115
//   many381 = 381       once21 = 21        pair30 = 30        recur15 = 15
//
// The refused shapes cannot be tested here, because Checks/pipeline.cmake will
// not write a module while any function carries `ctnative.not_native`. A name
// assigned twice, a name called before it is assigned, a name passed rather
// than called, a name whose function closes over a data binding, and mutual
// recursion (refused because one of the two stores must come second, so the
// other's reads are not all after its write) each have a program in
// CTNative/Lowering/Closures/refusals.mlir, under split-file.

// The smallest case. `twice` is boxed only because `bump` names it; `bump` is
// named by no nested function, so it stays an ordinary register and its call
// is already direct. The rule rewrites the `ctjs.load_upvalue` inside `bump`:
// its call becomes `call_direct @twice` and its capture slot is dropped, so
// `bump` lowers to a function of one argument that closes over nothing.
function one_level(n) {
    var twice = function (k) {
        return k * 2;
    };
    var bump = function (k) {
        return twice(k) + 1;
    };
    return bump(n);
}

// The same rule twice, one frame apart. `add_one` is a binding of
// `outer_helper`'s frame and `outer_helper` is a binding of this one, so the
// rule runs again on a box its first application exposed: once `outer_helper`
// is a direct call, its own body still holds a box holding a function.
function two_levels(n) {
    var outer_helper = function (k) {
        var add_one = function (j) {
            return j + 1;
        };
        var tripled = function (j) {
            return add_one(j) * 3;
        };
        return tripled(k);
    };
    var lift_it = function (k) {
        return outer_helper(k) + 100;
    };
    return lift_it(n);
}

// Recursion, which is the shape the rule is built around, not an edge case.
// `down` names itself, so its closure captures the very box it is written into
// and the box is read from inside the function it holds. The lifter accepts a
// capture that is itself an erased binding (here the binding being defined);
// the `ctjs.load_upvalue` in the body becomes a `ctjs.call_direct` of the
// function it sits in, and the capture list empties. `writeReachesEveryRead`
// special-cases it: the closure's only use is the store, and a store is not a
// call, so it is not asked to dominate itself.
function recursive(n) {
    var down = function (k) {
        if (k <= 0) {
            return 0;
        }
        return k + down(k - 1);
    };
    return down(n);
}

// One name, three calls, all of them in the other frame. Each becomes its own
// `ctjs.call_direct`, so a rewrite that took only the first would print 3
// where the interpreter prints 381.
function many_calls() {
    var add = function (a, b) {
        return a + b;
    };
    var total = function () {
        return add(1, 2) * 100 + add(3, 4) * 10 + add(5, 6);
    };
    return total();
}

// Two names in one frame, holding different functions and captured by one
// closure. `mix` captures both boxes, at slots 0 and 1, and both slots are
// removed. This case renumbers slots: dropping slot 0 alone would leave
// `negate` read at an index that no longer means what it did.
function two_names(n) {
    var square = function (k) {
        return k * k;
    };
    var negate = function (k) {
        return 0 - k;
    };
    var mix = function (k) {
        return square(k) + negate(k);
    };
    return mix(n);
}

// A binding whose function closes over another binding: the only capture shape
// the rule accepts. `scaled`'s closure holds `base`'s box, `run`'s holds
// `scaled`'s, and both are erased in one fixpoint, so `scaled` lowers to a free
// function of one argument even though its capture list was not empty when the
// rule first looked at it.
function captured_chain(n) {
    var base = function (k) {
        return k + 7;
    };
    var scaled = function (k) {
        return base(k) * 2;
    };
    var run = function (k) {
        return scaled(k) + 3;
    };
    return run(n);
}

// Read in both frames. `half` is called once through a `ctjs.cell_get` here and
// once through a `ctjs.load_upvalue` inside `via`, and the two take different
// routes: the read in this frame is replaced by the closure value, so the
// ordinary closure lift makes that call direct, while the read one frame in has
// no closure value to name, so this rule writes the `ctjs.call_direct` itself.
function both_frames(n) {
    var half = function (k) {
        return k / 2;
    };
    var via = function (k) {
        return half(k) + 1;
    };
    return half(n) * 100 + via(n);
}

// A function declaration compiles to the same three operations.
// `predeclare_locals` hoists `base_step` and boxes it because `scale_step`
// names it, then the declaration is a `ctjs.cell_set` into the existing box,
// exactly what `var base_step = function ...` emits. Both forms are here so a
// rule keyed on only one of them would fail on the other.
function declared(n) {
    function base_step(k) {
        return k - 1;
    }

    function scale_step(k) {
        return base_step(k) * 5 + 2;
    }
    return scale_step(n);
}

var both405 = both_frames(8);
var chain27 = captured_chain(5);
var declared32 = declared(7);
var deep115 = two_levels(4);
var many381 = many_calls();
var once21 = one_level(10);
var pair30 = two_names(6);
var recur15 = recursive(5);
