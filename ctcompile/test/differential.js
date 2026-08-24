// THE BODIES THE DIFFERENTIAL TEST COMPILES.
//
// Each is compiled by the build through the real pipeline AND run by the
// interpreter, and the two answers must agree. The interpreter is the
// definition of correct here, which the dialect's own policy states outright:
// "when a CTJS operation and the ctbrowser VM disagree, the VM is correct by
// definition."
//
// THIS FILE MUST STAY TEXTUALLY IDENTICAL to the fixture string in
// Differential.cpp - that is what makes the two tiers compile the same
// function_proto, and the driver checks the function count as a cheap guard
// against them drifting.
//
// EACH BODY IS CHOSEN FOR AN ANSWER THAT DIFFERS IF THE LOWERING IS WRONG in a
// specific way, rather than for coverage. A test that returns the same thing
// whether or not the compiler is right is worse than no test.

// `+` IS THE RE-ENTERING FAMILY. If it were lowered to op::add - the static
// one, which `++` uses - an object with a valueOf would answer NaN instead of
// running it.
function plus(a, b) { return a + b; }

// THE FOUR RELATIONAL OPERATORS ARE NOT NEGATIONS OF ONE ANOTHER, because
// ct_aot_compare can answer UNORDERED and that makes all four false. Lowering
// `>=` as `!(<)` makes `NaN >= NaN` true, which this catches.
function ge(a, b) { return a >= b; }

// STRICT AND LOOSE EQUALITY REACH DIFFERENT HELPERS with different effect
// profiles - one cannot throw at all, the other converts.
// Two functions rather than one returning both: an array literal needs
// ctjs.append, whose helper is one of the rows aot_bridge.cpp does not define.
function strict(a, b) { return a === b; }
function loose(a, b) { return a == b; }

// CONTROL FLOW, whose block arguments are the shape the C++ emitter miscompiles
// unless they are lowered to variables first.
function pick(a, b) { if (a < b) { return a; } return b; }

// GLOBALS, which are a read and a write with no status and no edge.
function globals(a) { DIFF_W = a; return DIFF_R; }

// A CALL, the only operation needing a contiguous run of frame slots.
function apply(k, a, b) { return k(a, b); }
