// Closures that capture a hoisted `var` written exactly once, compiled
// natively and checked end to end by the compilation-unit test.
//
// Every capture here is a hoisted `var`, the shape real programs are made of.
// `compiler_impl::predeclare_locals` hoists every `var`/`let`/`const` of a
// body to the top and boxes it there holding `undefined`, so `var n = 5`
// compiles to a `ctjs.create_cell` of undefined in the prologue plus a
// `ctjs.cell_set` into that box where the declaration is written. A rule that
// required the box never to be written after creation would call it
// "reassigned" and refuse every function in this file, and most closures in
// the Phaser, p5 and Bootstrap corpora.
//
// Each function below orders the closure's creation, the store and the call
// differently; all must print the stored value.
//
// A binding written once is constant after that write. The value a lifted
// call prepends is then the store's operand rather than the cell's initial
// value, and dominance is what makes that exact:
//
//   1. exactly one ctjs.cell_set names the box;
//   2. it dominates every ctjs.cell_get of the cell, so no read in this frame
//      can see the hoisted `undefined`;
//   3. it dominates every call of every closure that captured the cell, so no
//      call can either.
//
// Condition 3 is about the call, not about where the closure is created. A
// function declaration is hoisted: `get` in `one_level` below is created by
// an `op::closure` in the prologue, before `var base = 7` runs, because
// JavaScript makes it callable from the body's first line. Requiring the store
// to dominate the create_closure would refuse every program here. The lift
// prepends the captured value at each call, and a closure reads its box when
// it runs (run_loop.cpp, VM_CASE(get_upvalue): `reg = cell->slot`), so the
// call is the point that has to follow the write.
//
// AND THE INITIAL IS NOT CHECKED, deliberately. Conditions 2 and 3 make it
// unobservable (nothing can read the box on a path that skips the store), so
// requiring a constant `undefined` there would narrow the rule for nothing.
// It is `undefined` in every function below anyway, because that is what a
// hoist boxes.
//
// Every capture is read in the innermost body, not just passed along. A lift
// that prepended the cell's initial value instead of the stored one would
// still compile and would print `undefined` (NaN in the native carrier), and
// only the comparison against the interpreter catches that.
//
// Refused shapes cannot live here: test/CTNative/Checks/pipeline.cmake fails
// if any function carries `ctnative.not_native`. A binding written twice, a
// read the write does not dominate, and a write inside a loop whose call is
// after the loop each have their own program in
// test/CTNative/Lowering/Closures/refusals.mlir, under split-file.
//
// Expected values, worked out by hand and not asserted here: the interpreter
// is the reference. Seven globals, seven distinct values, so a wrong one
// cannot hide behind a right one:
//   one21 = 21      two22 = 22       three32 = 32     after20 = 20
//   reads24 = 24    loop64 = 64      computed49 = 49

// One level, with the closure created before the write. `get` is a hoisted
// declaration, so its `op::closure` runs in the prologue and the store to
// `base` runs after it; the call runs after both, which is condition 3.
function one_level() {
    var base = 7;

    function get() {
        return base * 3;
    }
    return get();
}

// Two levels. `k` is a hoisted `var` of `two_levels`, boxed once in its
// prologue; `deep` names it from two frames out and reaches it through `mid`'s
// closure: the capture is passed down through each enclosing closure in turn.
function two_levels() {
    var k = 10;

    function mid() {
        function deep() {
            return k + 1;
        }
        return deep() * 2;
    }
    return mid();
}

// Three levels, so the lift's fixpoint needs three rounds and the value the
// innermost call is handed has been passed through two capture parameters.
function three_levels() {
    var k = 5;

    function mid() {
        function inner() {
            function deep() {
                return k * 3;
            }
            return deep() + 1;
        }
        return inner() * 2;
    }
    return mid();
}

// A closure created after the store and called through a local: the other
// order, and the one a reader expects. `apply` is a function expression, so
// its create_closure sits where it is written, after the store, and it is
// called twice through the local that holds it.
function after_store(x) {
    var scale = 4;
    var apply = function (v) {
        return v * scale;
    };
    return apply(x) + apply(x + 1);
}

// Written once, read many times, in two places. `step` is read directly in
// this frame and through the capture in `at`, three times; the one store
// dominates all four reads, which is conditions 2 and 3 together.
function many_reads(n) {
    var step = 3;

    function at(i) {
        return step * i;
    }
    return step + at(1) + at(2) + at(n);
}

// The loop shape that compiles, which is not the one a reader would guess.
// `var` is function-scoped and predeclare_locals boxes at the top, so `v` is
// one box created in the prologue, not a fresh box per iteration; this
// compiler has no per-iteration binding. This is exact because the write and
// the call are both inside the loop body, in that order: the write dominates
// the call, so the call reads the value this iteration stored, and that is
// what the lift prepends. Moving the call after the loop breaks dominance;
// test/CTNative/Lowering/Closures/refusals.mlir pins that refusal.
function per_iteration(n) {
    var total = 0;
    var i = 0;
    while (i < n) {
        var v = i * 10;
        var take = function () {
            return v + 1;
        };
        total = total + take();
        i = i + 1;
    }
    return total;
}

// The write is a computation, not a literal. What the lift prepends at each
// call site is the store's operand, here a `ctjs.binary` result, so this case
// fails if anything ever reads the cell's initial value again, and it fails
// as a wrong number rather than as a refusal.
function computed(a, b) {
    var span = b - a;

    function scaled(f) {
        return span * f;
    }
    return scaled(2) + scaled(5);
}

var one21 = one_level();
var two22 = two_levels();
var three32 = three_levels();
var after20 = after_store(2);
var reads24 = many_reads(4);
var loop64 = per_iteration(4);
var computed49 = computed(3, 10);
