// A BINDING WITH ONE DOMINATING WRITE - part 24 Phase 59, slice 2 step 1,
// through the compilation-unit gate.
//
// EVERY CAPTURE HERE IS A HOISTED `var`, which is the shape slice 1 refused
// whole and the shape a real program is made of.
// `compiler_impl::predeclare_locals` hoists every `var`/`let`/`const` of a body
// to the top and BOXES it there holding `undefined`, so `var n = 5` compiles to
// a `ctjs.create_cell` of a constant undefined in the prologue and a
// `ctjs.cell_set` into that already-built box where the declaration is written.
// The box is therefore written after it is built, slice 1's immutability proof
// said "reassigned", and every function on this page was refused. Measured on
// phaser before this step: 715 closures refused for exactly that, 81 on p5, 16
// on bootstrap.
//
// A BINDING WRITTEN ONCE IS CONSTANT AFTER THAT WRITE. The value a lifted call
// prepends is then the STORE'S operand rather than the cell's initial, and what
// makes that exact is dominance:
//
//   1. exactly ONE ctjs.cell_set names the box;
//   2. it DOMINATES every ctjs.cell_get of the cell - so no read in this frame
//      can see the hoisted `undefined`;
//   3. it DOMINATES every CALL of every closure that captured the cell - so no
//      call can either.
//
// CONDITION 3 IS ABOUT THE CALL AND NOT ABOUT THE `function` KEYWORD, and that
// is the whole of this step. A FUNCTION DECLARATION IS HOISTED: `get` in
// `one_level` below is created by an `op::closure` in the PROLOGUE, before
// `var base = 7` runs, because JavaScript makes it callable on the body's first
// line. A rule that asked the store to dominate the create_closure refused
// every program here - measured, all seven. What the lift actually does is
// prepend the captured VALUE at each CALL, and a closure reads its box when it
// RUNS (run_loop.cpp, VM_CASE(get_upvalue): `reg = cell->slot`), so the call is
// the point that has to follow the write.
//
// AND THE INITIAL IS NOT CHECKED, deliberately. Conditions 2 and 3 make it
// unobservable - nothing can read the box on a path that skips the store - so
// requiring a constant `undefined` there would narrow the rule for a claim it
// does not need. It is `undefined` in every function below anyway, because that
// is what a hoist boxes.
//
// EVERY CAPTURE IS READ IN THE INNERMOST BODY, not merely passed along: a lift
// that prepended the cell's initial rather than the stored value compiles clean
// and prints `undefined` - NaN in the native carrier - and only the interpreter
// can say so. That is what the gate below is.
//
// THE REFUSED SHAPES ARE NOT HERE, and cannot be: native-pipeline.cmake refuses
// to write a module while any function carries `ctnative.not_native`. A binding
// written twice, a read the write does not dominate, and a write inside a loop
// whose call is after it are one program apiece in
// CTNative/Lowering/closure-refusals.mlir, under split-file.
//
// The answers, worked out by hand and NOT asserted anywhere - the interpreter is
// the reference, by definition. Seven globals, seven distinct values, so a wrong
// one cannot hide behind a right one:
//   one21 = 21      two22 = 22       three32 = 32     after20 = 20
//   reads24 = 24    loop64 = 64      computed49 = 49

// ONE LEVEL, AND THE CLOSURE IS CREATED BEFORE THE WRITE. `get` is a hoisted
// declaration, so its `op::closure` runs in the prologue and the store to
// `base` runs after it; the CALL runs after both, which is condition 3.
function one_level() {
    var base = 7;
    function get() { return base * 3; }
    return get();
}

// TWO LEVELS. `k` is a hoisted `var` of `two_levels`, boxed once in its
// prologue; `deep` names it from two frames out and reaches it through `mid`'s
// closure, which is slice 1b's mechanism over slice 2's binding.
function two_levels() {
    var k = 10;
    function mid() {
        function deep() { return k + 1; }
        return deep() * 2;
    }
    return mid();
}

// THREE LEVELS, so the lift's fixpoint needs three rounds and the value the
// innermost call is handed has been passed through two capture parameters.
function three_levels() {
    var k = 5;
    function mid() {
        function inner() {
            function deep() { return k * 3; }
            return deep() + 1;
        }
        return inner() * 2;
    }
    return mid();
}

// A CLOSURE CREATED AFTER THE STORE AND CALLED THROUGH A LOCAL, which is the
// other order and the one a reader expects. `apply` is a function EXPRESSION,
// so its create_closure sits where it is written - after the store - and it is
// called twice through the local that holds it.
function after_store(x) {
    var scale = 4;
    var apply = function (v) { return v * scale; };
    return apply(x) + apply(x + 1);
}

// WRITTEN ONCE, READ MANY TIMES, AND IN TWO PLACES. `step` is read directly in
// this frame AND through the capture in `at`, three times; the one store
// dominates all four reads, which is conditions 2 and 3 together.
function many_reads(n) {
    var step = 3;
    function at(i) { return step * i; }
    return step + at(1) + at(2) + at(n);
}

// THE LOOP SHAPE THAT IS ADMISSIBLE, and it is not the one a reader would
// guess. `var` is FUNCTION-scoped and predeclare_locals boxes at the top, so
// `v` is ONE box created in the prologue and not a fresh box per iteration -
// there is no per-iteration binding in this compiler to admit. What makes this
// exact is that the write and the call are both inside the loop body, in that
// order: the call is dominated by the write, so it reads the value this
// iteration stored, and that is what the lift prepends. Move the call after the
// loop and dominance fails, which closure-refusals.mlir pins.
function per_iteration(n) {
    var total = 0;
    var i = 0;
    while (i < n) {
        var v = i * 10;
        var take = function () { return v + 1; };
        total = total + take();
        i = i + 1;
    }
    return total;
}

// THE WRITE IS A COMPUTATION, NOT A LITERAL. What the lift prepends at each
// call site is the operand of the store - here a `ctjs.binary` result - so this
// is the case that fails if anything ever goes back to reading the cell's
// initial, and it fails as a number rather than as a refusal.
function computed(a, b) {
    var span = b - a;
    function scaled(f) { return span * f; }
    return scaled(2) + scaled(5);
}

var one21 = one_level();
var two22 = two_levels();
var three32 = three_levels();
var after20 = after_store(2);
var reads24 = many_reads(4);
var loop64 = per_iteration(4);
var computed49 = computed(3, 10);
