// A CAPTURE FILLED FROM THE ENCLOSING CLOSURE - part 24 Phase 59, slice 1b,
// through the compilation-unit gate.
//
// Every innermost function here names a binding that belongs to a frame TWO OR
// THREE levels out. The bytecode compiler does not box that binding in the
// nearest frame - it is not there to box - so the nested closure's descriptor
// says "not from_parent_local: fill this slot from the enclosing closure's
// upvalue k", and context::make_closure copies the enclosing closure's cell into
// the slot. Slice 1 refused every one of these ("capture N is not a cell of
// this frame"), and it is the shape a UMD bundle is made of: the module body is
// the factory's frame, so every closure inside a nested function reaches a
// module binding exactly this way. Measured on bootstrap before this slice, 15
// of the 19 callees a direct call can reach were refused for it.
//
// WHAT MAKES THEM ADMISSIBLE. The importer leaves an `undefined` PLACEHOLDER in
// such a capture operand - no binding of this frame belongs there - and writes
// WHICH upvalue of the enclosing closure fills the slot on the closure's
// `enclosing_indices` attribute, indexed in parallel with the capture list and
// -1 where the operand is the real cell. `mid$2` in the module this file
// imports reads `captures %7 {enclosing_indices = array<i32: 0>}`. When the
// enclosing function is lifted (slice 1), its upvalue k becomes its capture
// PARAMETER - the entry-block argument 3 + k, holding the initial of a cell the
// outer frame proved constant - and the nested closure's slot is then that
// argument, selected by the index rather than followed through an operand. The
// lift is a fixpoint: the outer closure lifts in one round, the inner one is
// judged again in the next, when its enclosing function carries
// `ctnative.captures`, and a three-level chain settles in three rounds. Each
// call passes the argument on as it is, because it already holds the VALUE: the
// index names a CELL in the enclosing closure, and ctjs.load_upvalue reads
// THROUGH a cell (run_loop.cpp, get_upvalue: `reg = cell->slot`), so a lifted
// capture parameter is never the box and the innermost read is a read of a
// double.
//
// EVERY CAPTURE IS READ IN THE INNERMOST BODY, not merely passed along. That is
// what makes the gate below able to fail: a lift that passed the wrong value,
// or a stale one, prints a different number here and only the interpreter can
// say so. And every captured binding is a PARAMETER, as in
// native-closure-fixture.js and for the same reason - a hoisted `var` is a
// cell_set into an already-built cell and reads as reassigned.
//
// THE REFUSED CASE IS NOT HERE, and cannot be: native-pipeline.cmake refuses to
// write a module while any function carries `ctnative.not_native`, and a
// refusal is contagious in both directions. The chain whose outer frame
// reassigns its binding - refused, with the enclosing closure's own reason
// spelled into the sentence - is pinned in CTNative/Lowering/
// closure-refusals.mlir under split-file, one program per file.
//
// The answers, worked out by hand and NOT asserted anywhere - the interpreter
// is the reference, by definition. Eight globals, eight distinct values, so a
// wrong one cannot hide behind a right one:
//   two22 = 22        both47 = 47       three32 = 32      twice727 = 727
//   chain10080 = 10080  bounded110 = 110  arrow223 = 223  shared21 = 21

// TWO LEVELS. `k` belongs to `two_levels`; `deep` reaches it through `mid`'s
// closure. `mid` lifts to `mid(k)` in the first round, and `deep` - whose
// capture is now `mid`'s parameter - lifts to `deep(k)` in the second.
function two_levels(k) {
    function mid() {
        function deep() { return k + 1; }
        return deep() * 2;
    }
    return mid();
}

// BOTH KINDS IN ONE SIGNATURE. `deep` captures `k` through `mid`'s closure AND
// `j`, which is `mid`'s own parameter and a cell of `mid`'s frame. One capture
// is a lifted parameter passed on, the other is a constant cell's initial, and
// they arrive in descriptor order whichever that is - the interpreter decides.
function both_kinds(k) {
    function mid(j) {
        function deep() { return k * 10 + j; }
        return deep();
    }
    return mid(3) + mid(4);
}

// THREE LEVELS, so the fixpoint needs three rounds: `mid` lifts, then
// `inner`, then `deep`.
function three_levels(k) {
    function mid() {
        function inner() {
            function deep() { return k * 3; }
            return deep() + 1;
        }
        return inner() * 2;
    }
    return mid();
}

// THE ENCLOSING FUNCTION CALLED TWICE WITH DIFFERENT FRAME ARGUMENTS. `k` is
// the same value at both calls of `mid`; `j` differs, and `deep` reads both.
function twice_frames(k) {
    function mid(j) {
        function deep(x) { return k + j * x; }
        return deep(2) + deep(3);
    }
    return mid(1) * 100 + mid(5);
}

// A PARAMETER AT EVERY LEVEL, and the deepest body reads all of them: `a` and
// `b` come through two enclosing closures, `c` through one, `d` is a cell of
// `inner`'s own frame, and `e` is `deep`'s own parameter. `deep` lifts to a
// function of five doubles.
function chain_params(a, b) {
    function mid(c) {
        function inner(d) {
            function deep(e) { return a * 1000 + b * 100 + c * 10 + d + e; }
            return deep(1) + deep(2);
        }
        return inner(3) + inner(4);
    }
    return mid(5) + mid(6);
}

// A BOOLEAN THROUGH TWO LEVELS, used as a condition. `lo` and `hi` reach
// `inside` through `mid`'s closure; `x` is `mid`'s parameter.
function bounded_two(lo, hi) {
    function mid(x) {
        function inside() { return x >= lo && x < hi; }
        return inside();
    }
    var hits = 0;
    if (mid(1)) { hits = hits + 1; }
    if (mid(9)) { hits = hits + 10; }
    if (mid(4)) { hits = hits + 100; }
    return hits;
}

// AN ARROW NESTED IN A LIFTED FUNCTION, reading a capture that came through
// the enclosing closure. It never reads `this`, so it lifts like any other.
function arrow_two(k) {
    function mid(j) {
        var f = (x) => k + j + x;
        return f(1) + f(2);
    }
    return mid(10);
}

// THE MIDDLE FUNCTION READS THE BINDING TOO. `mid` has exactly ONE
// ctjs.load_upvalue of `k` - its own read - and `deep`'s capture beside it is
// the placeholder operand with `enclosing_indices = array<i32: 0>`. The lift
// rewrites the read to `mid`'s capture parameter and hands `deep` that same
// parameter by index, so both arrive at one argument by two routes. (It was two
// loads while the index lived in an operand, and the second was a runtime
// upvalue read the boxed tier paid for and nothing consumed.)
function shared_read(k) {
    function mid() {
        function deep() { return k * 2; }
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
