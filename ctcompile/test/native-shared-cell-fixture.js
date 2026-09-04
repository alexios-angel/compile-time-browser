// A SHARED MUTABLE BINDING, CARRIED BY POINTER - part 24 Phase 59, slice 2
// step 2, through the compilation-unit gate.
//
// STEP 1 ADMITTED A BINDING WITH ONE DOMINATING WRITE by copying the stored
// value into a parameter. These are the bindings it cannot: written twice,
// written on only one path, written inside a loop whose call is outside it,
// and - the shape that dominates the reachable set of a real bundle - written
// by the CLOSURE. `var n = 0; function tick() { n = n + 1; }` has one
// assignment in the frame and one in the closure, and there is no single value
// to copy: copying gives every call its own counter and prints 1 twice where
// the interpreter prints 1 then 2. That refusal ("capture 0 is a binding that
// is reassigned - a shared cell is Phase 59 slice 2") was 11 of the 16 chains
// over bootstrap's 19 reachable callees.
//
// SO THE BOX BECOMES AN ORDINARY LOCAL AND THE CAPTURE BECOMES A POINTER TO
// IT. `ctjs.create_cell` lowers to `double n;` in the frame that owns the
// binding, assigned the box's INITIAL - the `undefined`
// `compiler_impl::predeclare_locals` hoists, carried as NaN. A read is a load
// of that variable, a write an assignment to it, and a lifted target takes
// `double *` for the slot instead of `double`, reading and writing through it.
// Each call site passes `&n`. That is the receiver lift's convention exactly
// (`ctnative.receiver` emits `ctn_x * self`), one operand along.
//
// WHY THE POINTER CANNOT DANGLE, which is the only question worth asking here,
// and each half is a condition that was already being enforced:
//
//   1. THE CLOSURE CANNOT LEAVE THE FRAME. Condition 4 of `whyNotLiftable`
//      admits a closure only when EVERY use of its value is a call this tier
//      lowers; stored, returned or passed, it is refused by name and those
//      three refusals are still pinned in closure-refusals.mlir. A closure
//      reachable from nothing but a call in this frame cannot be called after
//      the frame is gone.
//   2. AND EVERY CALL IS IN THE FRAME THAT OWNS THE VARIABLE.
//      `whyCapturesDoNotReach` compares the `ctjs.func` the captured value
//      lives in against the one the call sits in - METHODCAP in
//      closure-refusals.mlir is that refusal - and then asks that the cell
//      properly dominate the call, which is what puts the variable in scope in
//      the emitted C++ where its address is taken.
//   3. AND THE POINTER GOES NOWHERE ELSE. After the lift the parameter's only
//      uses are the load and the store that replaced the upvalue read and
//      write, plus its re-appearance at a nested lifted call - which is the
//      same three conditions one level in.
//
// NO DOMINANCE PROOF IS NEEDED FOR THE VALUE, and that is what makes this
// step wider than step 1 rather than merely different. Step 1 replaced every
// read with the stored value, so it had to prove no read could see the
// hoisted `undefined`; this one does not replace the box, it EMITS it. A read
// on a path that reached no assignment loads the NaN the variable was
// initialised with, which is exactly what the interpreter answers - so
// `early_read`, `conditional` and `loop_after` below, all three of them
// refusals pinned in closure-refusals.mlir until this step, are now programs
// that compile and agree.
//
// THE REFUSED SHAPES ARE NOT HERE, and cannot be: native-pipeline.cmake
// refuses to write a module while any function carries `ctnative.not_native`.
// A box that escapes into an object, a closure that escapes, and a target that
// writes a slot this tier does not carry are one program apiece in
// CTNative/Lowering/closure-refusals.mlir, under split-file.
//
// The answers, worked out by hand and NOT asserted anywhere - the interpreter
// is the reference, by definition. Ten globals, ten distinct values, so a
// wrong one cannot hide behind a right one:
//   counter122 = 122      shared17 = 17        loop20 = 20
//   readwrite30 = 30      mixed25065 = 25065   nested204103 = 204103
//   twice2 = 2            earlyread5 = 5       cond12 = 12
//   loopafter38 = 38

// THE COUNTER: A CLOSURE INCREMENTS, THE FRAME READS. `tick` compiles to a
// `ctjs.store_upvalue`, which is the refusal this step lifts; `n` is read
// again in `counter`'s own frame after both calls, so a lowering that gave
// `tick` a copy would answer 121 here and only the interpreter could say so.
function counter() {
    var n = 0;
    function tick() { n = n + 1; return n; }
    var a = tick();
    var b = tick();
    return a * 100 + b * 10 + n;
}

// TWO CLOSURES, ONE BOX, BOTH WRITING IT. `add` and `scale` are two lifted
// functions taking the SAME `double *`, and the order of the three calls is
// the whole answer: 5 -> 8 -> 16 -> 17. Each call's result is discarded, which
// is the statement form --ctnative-prune-dead-stores turns into one.
function shared_two(start) {
    var acc = start;
    function add(k) { acc = acc + k; return acc; }
    function scale(k) { acc = acc * k; return acc; }
    add(3);
    scale(2);
    add(1);
    return acc;
}

// A MUTATION INSIDE A LOOP. The box is built once in the prologue and written
// on every iteration through the pointer, so the variable lives at function
// scope in the emitted C++ and its address is taken inside the loop body.
// `take` returns nothing, which is `undefined` - a NaN this tier never prints.
function loop_sum(n) {
    var total = 0;
    function take(v) { total = total + v; }
    var i = 0;
    while (i < n) {
        take(i * 2);
        i = i + 1;
    }
    return total;
}

// READ-ONLY IN ONE CLOSURE, WRITTEN IN ANOTHER. `peek` holds no
// `ctjs.store_upvalue` at all and still takes a pointer, because carrying is a
// property of the SLOT and not of the target: one `ctjs.func` is one C++
// signature, and `v` is shared whoever is looking at it.
function read_and_write(base) {
    var v = base;
    function bump() { v = v + 7; }
    function peek() { return v * 2; }
    var first = peek();
    bump();
    var second = peek();
    return first + second;
}

// A SHARED CELL, A SINGLE-WRITE CELL AND A PLAIN PARAMETER IN ONE SIGNATURE.
// `step` reads three captures and none of them is the same kind: `running` is
// written by `bump`, so it arrives as `double *`; `fixed` is written once
// before every call and nothing writes it again, so slice 2 step 1 copies it
// and it arrives as a `double`; `p` is a captured parameter, boxed holding its
// value and never written, so slice 1 copies it. `k` is `step`'s own.
//
// THE WRITE IS IN A DIFFERENT CLOSURE ON PURPOSE. `mutatesUpvalue` is a
// property of a FUNCTION and not of a slot, so a target that writes any
// upvalue makes every cell it captures shared; keeping the write in `bump`
// leaves `step` unmarked and is what lets one signature hold all three.
function mixed(p) {
    var fixed = 10;
    var running = 0;
    function bump() { running = running + 1; }
    function step(k) { return running * fixed * k + p; }
    bump();
    var a = step(2);
    bump();
    var b = step(3);
    return a * 1000 + b;
}

// TWO LEVELS, SO SLICE 1b CARRIES THE POINTER OUTWARD. `n` belongs to
// `nested_two`; `deep` names it from two frames out, so its slot is filled
// from `mid`'s closure and the importer leaves a placeholder in the operand.
// `mid` neither reads nor writes `n` - its only use of the capture is to hand
// it to `deep` - so `mid` lifts to `mid(double * n)` and passes the pointer
// straight on, which is the enclosing-index path carrying an address instead
// of a value.
function nested_two() {
    var n = 100;
    function mid() {
        function deep(k) { n = n + k; return n; }
        return deep(1) + deep(2);
    }
    var t = mid();
    return t * 1000 + n;
}

// WRITTEN TWICE, WHICH STEP 1 REFUSED BY NAME ("it is assigned more than
// once"): which value a copy would see depends on the path taken to the call.
// There is nothing to choose here - the variable holds what the last
// assignment put in it, which is what the interpreter reads.
function twice_written() {
    var n;
    n = 1;
    n = 2;
    function get() { return n; }
    return get();
}

// A READ THE WRITE DOES NOT DOMINATE, which step 1 refused because unboxing on
// the stored value would have turned the honest `undefined` into 5. Here the
// variable is initialised with that `undefined` - NaN - so `before` is NaN,
// falsy, and `hit` stays 0. Equality on it would be refused (`defined()` says
// NaN does not compare the way undefined does); truthiness is exact.
function early_read() {
    var before = n;
    var n = 5;
    function get() { return n; }
    var hit = 0;
    if (before) { hit = 1; }
    return hit * 100 + get();
}

// A STORE ON ONE PATH ONLY - the OUTERSTORE program, which is the one that
// caught a real wrong answer in step 1: `t` is computed before the branch and
// dominates every call, while the `ctjs.cell_set` inside the `scf.if`
// dominates none. Compiled by value it answered -2 for `conditional(-1)`;
// carried, the variable is NaN on that path and falsy, which is what the
// interpreter says.
function conditional(k) {
    var v;
    var t = k * 2;
    if (k > 0) { v = t; }
    function get() { return v; }
    var seen = get();
    var hit = 0;
    if (seen) { hit = 1; }
    return hit * 10 + k;
}

// A WRITE INSIDE A LOOP AND THE CALL AFTER IT - the LOOPWRITE program. There
// is a path to `get()` on which the body never ran, so no value dominates the
// call and step 1 refused it. The variable is NaN on that path, `seen` is
// falsy, and the answer is 7; entered, it holds the last iteration's 30.
function loop_after(n) {
    var v;
    var i = 0;
    while (i < n) {
        v = i * 10;
        i = i + 1;
    }
    function get() { return v; }
    var seen = get();
    if (seen) { return seen + 1; }
    return 7;
}

var counter122 = counter();
var shared17 = shared_two(5);
var loop20 = loop_sum(5);
var readwrite30 = read_and_write(4);
var mixed25065 = mixed(5);
var nested204103 = nested_two();
var twice2 = twice_written();
var earlyread5 = early_read();
var cond12 = conditional(3) + conditional(-1);
var loopafter38 = loop_after(4) + loop_after(0);
