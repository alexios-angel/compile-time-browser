// A LOCAL BINDING THAT HOLDS A FUNCTION - part 24 Phase 59, slice 2 step 4,
// through the compilation-unit gate.
//
// `var f = function () { ... };` inside a function body is a hoisted local, and
// `compiler_impl::declare_local` BOXES it exactly when a nested function
// mentions the name (`is_captured`, capture.cpp). So the shape is a
// `ctjs.create_cell` of `undefined`, a `ctjs.create_closure`, a
// `ctjs.cell_set` putting the second in the first, and a `ctjs.cell_get` here
// or a `ctjs.load_upvalue` one frame in wherever the name is used. Slice 1
// refused that closure by the MECHANISM - "it reaches `ctjs.cell_set`, which
// slice 1 does not lower" - which was 87 of bootstrap's closure refusals and
// the terminal of 9 of the 19 chains a `ctjs.call_direct` can reach.
//
// A BINDING WRITTEN ONCE WITH A CLOSURE, AND ONLY EVER CALLED THROUGH, IS THAT
// FUNCTION. It is the DECLARATION case one scope in: a create_closure whose one
// use is a `ctjs.store_global` already lowers to nothing, because the closed
// world names the callee and every call of it is direct. A box adds nothing a
// call needs, so the box, its store and its reads lower to nothing too and each
// call through the name becomes a `ctjs.call_direct` of the target.
//
// EVERY BINDING HERE IS READ FROM ANOTHER FRAME, AND THAT IS FORCED RATHER THAN
// CHOSEN. A local is a BOX only when a nested function mentions it, and this
// rule admits a mention only when it is a CALL - so a binding it can take is
// always one some nested function calls. That is why every capture list below
// is either empty or made of other bindings: a call out there has nothing to
// prepend a captured VALUE at (the METHODCAP argument, one binding along), and
// a binding this step erases is not a value at all. `chain27` is the
// composition and `recur15` is the degenerate case of it - the one capture is
// the binding being defined.
//
// THE ANSWERS, worked out by hand and NOT asserted anywhere - the interpreter is
// the reference, by definition. Eight globals, eight distinct values, so a
// wrong one cannot hide behind a right one:
//   both405 = 405       chain27 = 27       declared32 = 32    deep115 = 115
//   many381 = 381       once21 = 21        pair30 = 30        recur15 = 15
//
// THE REFUSED SHAPES ARE NOT HERE, and cannot be: native-pipeline.cmake refuses
// to write a module while any function carries `ctnative.not_native`. A name
// assigned twice, a name called before it is assigned, a name that is passed
// rather than called, a name whose function closes over a DATA binding, and
// mutual recursion - which condition 3 refuses because one of the two stores
// has to come second - are one program apiece in
// CTNative/Lowering/closure-refusals.mlir, under split-file.

// THE SMALLEST ONE. `twice` is a box only because `bump` names it; `bump` is
// named nowhere nested, so it stays an ordinary register and slice 1 already
// took its call. What is new is the `ctjs.load_upvalue` inside `bump`, whose
// call becomes `call_direct @twice` and whose capture slot goes with it - so
// `bump` lowers to a function of one argument that closes over nothing.
function one_level(n) {
    var twice = function (k) { return k * 2; };
    var bump = function (k) { return twice(k) + 1; };
    return bump(n);
}

// THE SAME RULE TWICE, ONE FRAME APART. `add_one` is a binding of
// `outer_helper`'s frame and `outer_helper` is a binding of this one, so the
// rule runs on a box it created the conditions for: after `outer_helper` is a
// direct call, its own body still holds a box holding a function.
function two_levels(n) {
    var outer_helper = function (k) {
        var add_one = function (j) { return j + 1; };
        var tripled = function (j) { return add_one(j) * 3; };
        return tripled(k);
    };
    var lift_it = function (k) { return outer_helper(k) + 100; };
    return lift_it(n);
}

// RECURSION, WHICH IS THE SHAPE THE RULE IS BUILT AROUND AND NOT AN EDGE CASE.
// `down` names itself, so its closure captures the very box it is written into
// and the box is read from inside the function it holds. Condition 5 sees a
// capture that is a binding this step erases - itself - and takes it; the
// `ctjs.load_upvalue` in the body becomes a `ctjs.call_direct` of the function
// it sits in, and the capture list empties. Condition 3 needs one sentence for
// it, in `writeReachesEveryRead`: the closure's only use is the store, and a
// store is not a call, so asking it to dominate itself asks the wrong question.
function recursive(n) {
    var down = function (k) {
        if (k <= 0) { return 0; }
        return k + down(k - 1);
    };
    return down(n);
}

// ONE NAME, THREE CALLS, ALL OF THEM IN THE OTHER FRAME. Each becomes its own
// `ctjs.call_direct`, so a rewrite that took only the first would print 3
// where the interpreter prints 381.
function many_calls() {
    var add = function (a, b) { return a + b; };
    var total = function () { return add(1, 2) * 100 + add(3, 4) * 10 + add(5, 6); };
    return total();
}

// TWO NAMES IN ONE FRAME, HOLDING DIFFERENT FUNCTIONS AND CAPTURED BY ONE
// CLOSURE. `mix` takes both boxes, at slots 0 and 1, and both slots are
// removed - which is the case that renumbers, since dropping slot 0 alone would
// leave `negate` being read at an index that no longer means what it did.
function two_names(n) {
    var square = function (k) { return k * k; };
    var negate = function (k) { return 0 - k; };
    var mix = function (k) { return square(k) + negate(k); };
    return mix(n);
}

// A BINDING WHOSE FUNCTION CLOSES OVER ANOTHER BINDING, which is what condition
// 5 admits and the only capture shape this step can. `scaled`'s closure holds
// `base`'s box, `run`'s holds `scaled`'s, and the two are erased in one
// fixpoint - so `scaled` lowers to a free function of one argument even though
// its capture list was not empty when the rule looked at it.
function captured_chain(n) {
    var base = function (k) { return k + 7; };
    var scaled = function (k) { return base(k) * 2; };
    var run = function (k) { return scaled(k) + 3; };
    return run(n);
}

// READ IN BOTH FRAMES. `half` is called once through a `ctjs.cell_get` here and
// once through a `ctjs.load_upvalue` inside `via`, and the two go by different
// routes: the read in this frame is replaced by the closure VALUE, so slice 1's
// own rule makes that call direct, while the read one frame in has no closure
// value to name and this step writes the `ctjs.call_direct` itself.
function both_frames(n) {
    var half = function (k) { return k / 2; };
    var via = function (k) { return half(k) + 1; };
    return half(n) * 100 + via(n);
}

// AND A FUNCTION DECLARATION, WHICH COMPILES TO THE SAME THREE OPERATIONS.
// `predeclare_locals` hoists `base_step` and boxes it because `scale_step`
// names it, then the declaration is a `ctjs.cell_set` into the box that already
// exists - exactly what `var base_step = function ...` emits. Both forms are
// here so that a rule keyed on one of them would fail on the other.
function declared(n) {
    function base_step(k) { return k - 1; }
    function scale_step(k) { return base_step(k) * 5 + 2; }
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
