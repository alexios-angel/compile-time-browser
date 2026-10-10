// Closure-lifting fixture: a capture filled from the enclosing closure, checked
// end to end by the compilation-unit test (Checks/compilation-unit.py).
//
// Every innermost function here names a binding that belongs to a frame two or
// three levels out. The bytecode compiler does not box that binding in the
// nearest frame (it is not there to box), so the nested closure's descriptor
// says "not from_parent_local: fill this slot from the enclosing closure's
// upvalue k", and context::make_closure copies the enclosing closure's cell
// into the slot. This is the shape a UMD bundle is made of: the module body is
// the factory's frame, so every closure inside a nested function reaches a
// module binding this way. In the Bootstrap workload it was the most common
// reason a directly called function was refused.
//
// Why these lift. The importer leaves an `undefined` placeholder in such a
// capture operand (no binding of this frame belongs there) and records which
// upvalue of the enclosing closure fills the slot in the closure's
// `enclosing_indices` attribute, indexed in parallel with the capture list and
// -1 where the operand is the real cell. `mid$2` in the module this file
// imports reads `captures %7 {enclosing_indices = array<i32: 0>}`. When the
// enclosing function is lifted, its upvalue k becomes its capture parameter
// (entry-block argument 3 + k, holding the initial value of a cell the outer
// frame proved constant), and the nested closure's slot is then that argument,
// selected by the index rather than followed through an operand. The lift is a
// fixpoint: the outer closure lifts in one round, the inner one is judged again
// in the next, when its enclosing function carries `ctnative.captures`, and a
// three-level chain settles in three rounds. Each call passes the argument on
// unchanged, because it already holds the value: the index names a cell in the
// enclosing closure, and ctjs.load_upvalue reads through a cell (run_loop.cpp,
// get_upvalue: `reg = cell->slot`), so a lifted capture parameter is never the
// box and the innermost read is a read of a double.
//
// Every capture is read in the innermost body, not merely passed along. That
// is what lets the differential test fail: a lift that passed the wrong or a
// stale value prints a different number here, and only the interpreter can say
// so. Every captured binding is a parameter, as in Closures/closure.js, for the
// same reason: a hoisted `var` is a cell_set into an already-built cell and
// reads as reassigned.
//
// The refused case cannot be tested here, because Checks/pipeline.cmake will
// not write a module while any function carries `ctnative.not_native`, and a
// refusal spreads to callers and callees: a function that calls a refused one
// is refused, and so is a function that only a refused one calls. The chain
// whose outer frame reassigns its binding (refused, with the enclosing
// closure's own reason in the message) is in
// CTNative/Lowering/Closures/refusals.mlir, under split-file, one program per
// file.
//
// The answers, worked out by hand and not asserted anywhere (the interpreter is
// the reference). Eight globals with eight distinct values, so a wrong one
// cannot hide behind a right one:
//   two22 = 22        both47 = 47       three32 = 32      twice727 = 727
//   chain10080 = 10080  bounded110 = 110  arrow223 = 223  shared21 = 21

// Two levels. `k` belongs to `two_levels`; `deep` reaches it through `mid`'s
// closure. `mid` lifts to `mid(k)` in the first round, and `deep`, whose
// capture is now `mid`'s parameter, lifts to `deep(k)` in the second.
function two_levels(k) {
    function mid() {
        function deep() {
            return k + 1;
        }
        return deep() * 2;
    }
    return mid();
}

// Both kinds in one signature. `deep` captures `k` through `mid`'s closure and
// `j`, which is `mid`'s own parameter and a cell of `mid`'s frame. One capture
// is a lifted parameter passed on, the other is a constant cell's initial
// value, and they arrive in descriptor order, which the interpreter decides.
function both_kinds(k) {
    function mid(j) {
        function deep() {
            return k * 10 + j;
        }
        return deep();
    }
    return mid(3) + mid(4);
}

// Three levels, so the fixpoint needs three rounds: `mid` lifts, then
// `inner`, then `deep`.
function three_levels(k) {
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

// The enclosing function called twice with different arguments. `k` is the
// same value at both calls of `mid`; `j` differs, and `deep` reads both.
function twice_frames(k) {
    function mid(j) {
        function deep(x) {
            return k + j * x;
        }
        return deep(2) + deep(3);
    }
    return mid(1) * 100 + mid(5);
}

// A parameter at every level, and the deepest body reads all of them: `a` and
// `b` come through two enclosing closures, `c` through one, `d` is a cell of
// `inner`'s own frame, and `e` is `deep`'s own parameter. `deep` lifts to a
// function of five doubles.
function chain_params(a, b) {
    function mid(c) {
        function inner(d) {
            function deep(e) {
                return a * 1000 + b * 100 + c * 10 + d + e;
            }
            return deep(1) + deep(2);
        }
        return inner(3) + inner(4);
    }
    return mid(5) + mid(6);
}

// A boolean through two levels, used as a condition. `lo` and `hi` reach
// `inside` through `mid`'s closure; `x` is `mid`'s parameter.
function bounded_two(lo, hi) {
    function mid(x) {
        function inside() {
            return x >= lo && x < hi;
        }
        return inside();
    }
    var hits = 0;
    if (mid(1)) {
        hits = hits + 1;
    }
    if (mid(9)) {
        hits = hits + 10;
    }
    if (mid(4)) {
        hits = hits + 100;
    }
    return hits;
}

// An arrow function nested in a lifted function, reading a capture that came
// through the enclosing closure. It never reads `this`, so it lifts normally.
function arrow_two(k) {
    function mid(j) {
        var f = (x) => k + j + x;
        return f(1) + f(2);
    }
    return mid(10);
}



// The middle function reads the binding too. `mid` has exactly one
// ctjs.load_upvalue of `k` (its own read), and `deep`'s capture beside it is
// the placeholder operand with `enclosing_indices = array<i32: 0>`. The lift
// rewrites the read to `mid`'s capture parameter and hands `deep` that same
// parameter by index, so both arrive at one argument by two routes.
function shared_read(k) {
    function mid() {
        function deep() {
            return k * 2;
        }
        return deep() + k;
    }
    return mid();
}

var two22 = two_levels(10);
var both47 = both_kinds(2);
var three32 = three_levels(5);
var twice727 = twice_frames(1);
var chain10080 = chain_params(1, 2);
var bounded110 = bounded_two(3, 10);
var arrow223 = arrow_two(100);
var shared21 = shared_read(7);
