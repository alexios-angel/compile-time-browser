// Closure-lifting fixture: a shared mutable binding captured by pointer,
// checked end to end by the compilation-unit test (Checks/compilation-unit.py).
//
// Copy-by-value capture (Closures/hoisted-capture.js) handles a binding with
// exactly one write that dominates every call, by copying the stored value into
// a parameter. These are the bindings it cannot handle: written twice, written
// on only one path, written inside a loop whose call is outside it, and, the
// most common shape in a real bundle, written by the closure itself.
// `var n = 0; function tick() { n = n + 1; }` has one assignment in the frame
// and one in the closure, so there is no single value to copy: copying gives
// every call its own counter and prints 1 twice where the interpreter prints 1
// then 2. In the Bootstrap workload this was the most common closure refusal.
//
// So the box becomes an ordinary local and the capture becomes a pointer to
// it. `ctjs.create_cell` lowers to `double n;` in the frame that owns the
// binding, assigned the box's initial value: the `undefined`
// `compiler_impl::predeclare_locals` hoists, carried as NaN. A read is a load
// of that variable, a write an assignment to it, and a lifted target takes
// `double *` for the slot instead of `double`, reading and writing through it.
// Each call site passes `&n`. That is the receiver lift's convention
// (`ctnative.receiver` emits `ctn_x * self`), applied to a capture operand.
//
// Why the pointer cannot dangle. Each part is a check the lifter already makes
// (see the pointer-capture comment in ClosureLifter.h):
//
//   1. The closure cannot leave the frame. Condition 4 of `whyNotLiftable`
//      admits a closure only when every use of its value is a call the native
//      lowering handles; a stored, returned or passed closure is refused, and
//      CTNative/Lowering/Closures/refusals.mlir tests those three refusals. A
//      closure reachable from nothing but a call in this frame cannot be called
//      after the frame is gone.
//   2. Every call is in the frame that owns the variable.
//      `whyCapturesDoNotReach` checks that the call and the captured value are
//      in the same `ctjs.func` (the METHODCAP case in
//      CTNative/Lowering/Closures/refusals.mlir) and that the cell properly
//      dominates the call, so the variable is in scope where its address is
//      taken in the emitted C++.
//   3. The pointer goes nowhere else. After the lift the parameter's only uses
//      are the load and the store that replaced the upvalue read and write,
//      plus its reappearance at a nested lifted call, where the same three
//      conditions apply one level in.
//
// No dominance proof is needed for the value. Copy-by-value capture replaces
// every read with the stored value, so it must prove no read can see the
// hoisted `undefined`. Pointer capture does not replace the box, it emits it: a
// read on a path that reached no assignment loads the NaN the variable was
// initialised with. That is why `early_read`, `conditional` and `loop_after`
// below compile, although copy-by-value capture refuses all three.
//
// NaN stands in for undefined correctly in arithmetic, ordering and
// truthiness; `defined()` refuses equality, where the two differ. It is wrong
// when a value is printed: a global prints a Number with `%.17g`, so
// undefined-as-NaN would print `nan` where the interpreter prints `undefined`.
//
// So `ctjs.store_global` refuses a value whose type may be undefined (an `opt`
// type, checked by `admission::printable`). Without that,
// `function pick(k) { var v; if (k > 0) { v = 5; } function get() { return v;
// } return get(); } var out = pick(-1);` would print `out=nan`. The same leak
// exists with no closure at all (`var u; var z = u;`).
// CTNative/Lowering/Admission/global-undefined.mlir tests both programs and
// the OUTERSTORE shape; its DOMINATES program shows the type narrowing that
// keeps this refusal from firing on the programs in this file (see the note
// above the globals at the foot of this file).
//
// The refused shapes cannot be tested here, because Checks/pipeline.cmake will
// not write a module while any function carries `ctnative.not_native`. A box
// that escapes into an object, a closure that escapes, and a target that writes
// a slot the native lowering does not carry each have a program in
// CTNative/Lowering/Closures/refusals.mlir, under split-file.
//
// The answers, worked out by hand and not asserted anywhere (the interpreter is
// the reference). Ten globals with ten distinct values, so a wrong one cannot
// hide behind a right one:
//   counter122 = 122      shared17 = 17        loop20 = 20
//   readwrite30 = 30      mixed25065 = 25065   nested204103 = 204103
//   twice2 = 2            earlyread5 = 5       cond12 = 12
//   loopafter38 = 38











// The counter: a closure increments, the frame reads. `tick` compiles to a
// `ctjs.store_upvalue`, the write that copy-by-value capture cannot handle; `n`
// is read again in `counter`'s own frame after both calls, so a lowering that
// gave `tick` a copy would answer 121 here, and only the interpreter could say so.
function counter() {
    var n = 0;

    function tick() {
        n = n + 1;
        return n;
    }
    var a = tick();
    var b = tick();
    return a * 100 + b * 10 + n;
}

// Two closures, one box, both writing it. `add` and `scale` are two lifted
// functions taking the same `double *`, and the order of the three calls
// decides the answer: 5 -> 8 -> 16 -> 17. Each call's result is discarded, the
// statement form that --ctnative-prune-dead-stores produces.
function shared_two(start) {
    var acc = start;

    function add(k) {
        acc = acc + k;
        return acc;
    }

    function scale(k) {
        acc = acc * k;
        return acc;
    }
    add(3);
    scale(2);
    add(1);
    return acc;
}

// A mutation inside a loop. The box is built once in the prologue and written
// on every iteration through the pointer, so the variable lives at function
// scope in the emitted C++ and its address is taken inside the loop body.
// `take` returns nothing, which is `undefined`: a NaN the lowering never prints.
function loop_sum(n) {
    var total = 0;

    function take(v) {
        total = total + v;
    }
    var i = 0;
    while (i < n) {
        take(i * 2);
        i = i + 1;
    }
    return total;
}

// Read-only in one closure, written in another. `peek` holds no
// `ctjs.store_upvalue` at all and still takes a pointer, because carrying by
// pointer is a property of the slot, not of the target: one `ctjs.func` is one
// C++ signature, and `v` is shared whoever is looking at it.
function read_and_write(base) {
    var v = base;

    function bump() {
        v = v + 7;
    }

    function peek() {
        return v * 2;
    }
    var first = peek();
    bump();
    var second = peek();
    return first + second;
}

// A shared cell, a single-write cell and a plain parameter in one signature.
// `step` reads three captures, each of a different kind: `running` is written
// by `bump`, so it arrives as `double *`; `fixed` is written once before every
// call and never again, so copy-by-value capture copies it and it arrives as a
// `double`; `p` is a captured parameter, boxed holding its value and never
// written, so the ordinary closure lift copies it. `k` is `step`'s own.
//
// The write is in a different closure on purpose. `mutatesUpvalue` is a
// property of a function, not of a slot, so a target that writes any upvalue
// makes every cell it captures shared; keeping the write in `bump` leaves
// `step` unmarked, which lets one signature hold all three kinds.
function mixed(p) {
    var fixed = 10;
    var running = 0;

    function bump() {
        running = running + 1;
    }

    function step(k) {
        return running * fixed * k + p;
    }
    bump();
    var a = step(2);
    bump();
    var b = step(3);
    return a * 1000 + b;
}

// Two levels, so enclosing-capture forwarding carries the pointer outward. `n`
// belongs to `nested_two`; `deep` names it from two frames out, so its slot is
// filled from `mid`'s closure and the importer leaves a placeholder in the
// operand. `mid` neither reads nor writes `n` (its only use of the capture is
// to hand it to `deep`), so `mid` lifts to `mid(double * n)` and passes the
// pointer straight on: the enclosing-index path (Closures/nested-closure.js)
// carrying an address instead of a value.
function nested_two() {
    var n = 100;

    function mid() {
        function deep(k) {
            n = n + k;
            return n;
        }
        return deep(1) + deep(2);
    }
    var t = mid();
    return t * 1000 + n;
}

// Written twice, which copy-by-value capture refuses ("it is assigned more than
// once"): which value a copy would see depends on the path taken to the call.
// Here there is nothing to choose: the variable holds what the last assignment
// put in it, which is what the interpreter reads.
function twice_written() {
    var n;
    n = 1;
    n = 2;

    function get() {
        return n;
    }
    return get();
}

// A read the write does not dominate, which copy-by-value capture refuses,
// because replacing the read with the stored value would turn the real
// `undefined` into 5. Here the variable is initialised with that `undefined`
// (NaN), so `before` is NaN, falsy, and `hit` stays 0. Equality on it would be
// refused (`defined()`); truthiness is exact.
function early_read() {
    var before = n;
    var n = 5;

    function get() {
        return n;
    }
    var hit = 0;
    if (before) {
        hit = 1;
    }
    return hit * 100 + get();
}

// A store on one path only: the OUTERSTORE program, which caught a real wrong
// answer in copy-by-value capture. `t` is computed before the branch and
// dominates every call, while the `ctjs.cell_set` inside the `scf.if`
// dominates none. Copied by value it answered -2 for `conditional(-1)`; by
// pointer, the variable is NaN on that path and falsy, which is what the
// interpreter says.
function conditional(k) {
    var v;
    var t = k * 2;
    if (k > 0) {
        v = t;
    }

    function get() {
        return v;
    }
    var seen = get();
    var hit = 0;
    if (seen) {
        hit = 1;
    }
    return hit * 10 + k;
}

// A write inside a loop and the call after it: the LOOPWRITE program. There is
// a path to `get()` on which the body never ran, so no value dominates the call
// and copy-by-value capture refuses it. The variable is NaN on that path, `seen`
// is falsy, and the answer is 7; when the loop runs it holds the last value, 30.
function loop_after(n) {
    var v;
    var i = 0;
    while (i < n) {
        v = i * 10;
        i = i + 1;
    }

    function get() {
        return v;
    }
    var seen = get();
    if (seen) {
        return seen + 1;
    }
    return 7;
}



// No global below uses a `+ 0` coercion, and that is what this file asserts.
// `shared17`, `loop20` and `twice2` are a pointer-captured binding returned
// straight out of its frame. The variable is emitted holding the box's hoisted
// `undefined`, so a flow-insensitive type for the binding would be `opt<num>`
// ("may be undefined") even at a read that a write dominates. A global prints
// `%.17g` of the double, which cannot spell `undefined`, so
// `admission::printable` refuses an `opt` there and these three would be
// refused; a `+ 0` would force a `num`.
//
// The type narrowing (`TypeInference::cellTypeOf`) makes the coercion
// unnecessary. `var acc = start`, `var total = 0` and the pair of assignments
// in `twice_written` each dominate every read of their binding (the frame's own
// reads and every call of every closure over it), so the box's initial value is
// unobservable and the type is the join of the writes. Without the coercions
// this file tests the narrowing: with the narrowing stubbed out, these three
// globals are refused by name and the pipeline fails before any binary is built.
//
// Three more need no coercion for another reason. `earlyread5`, `cond12` and
// `loopafter38` are the shapes where no write dominates the read (a read
// before the store, a store on one path of an `if`, a store inside a loop), so
// their bindings stay `opt<num>`, which is correct: the variable really is NaN
// on those paths. Each reaches its global through arithmetic or a comparison,
// where NaN represents undefined exactly, so the refusal never fires and
// `undefined` is never printed.
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
