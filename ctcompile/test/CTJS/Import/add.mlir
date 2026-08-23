// The smallest real function there is, imported.
//
// `ctjs-translate` reads JAVASCRIPT here rather than a program image, and that
// is deliberate: a lit test then reads as one file - the source in, the expected
// IR beside it - where the image form would need a binary fixture built by
// another tool and regenerated whenever the image format moved. Both spellings
// go through the same importer.
//
// THE SECOND TOOL IS PART OF THE TEST. `ctjs-opt` verifies what the importer
// produced, which is the plan's instruction: "Verify the module. A verifier
// failure here is an importer bug, never a reason to relax the verifier."

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | ctjs-opt | FileCheck %s

function add(a, b) { return a + b; }

// THE TOP-LEVEL FUNCTION IS NOT COMPILED, because it contains `closure` - the
// declaration of `add` itself. Checked FIRST because it is a module attribute
// and prints on the module line, above everything else; FileCheck matches in
// order, so asserting it after the function body never matches.
// CHECK: ctjs.skipped
// CHECK-SAME: opcode = "closure"

// FIVE PARAMETERS FOR A TWO-PARAMETER FUNCTION. The first three are the frame
// properties the bytecode reads with their own opcodes - `this`, `new.target`
// and the callee - so load_this and its two siblings cost nothing.
// THE INDEX IS PART OF THE SYMBOL. ctjs.func is a Symbol and a real program has
// many functions sharing a name - p5.js has dozens called `constructor`. Without
// the suffix a 4,000-function module fails to verify on a duplicate symbol,
// which showed up as ctjs-translate producing no output at all while every
// individual function verified.
// CHECK-LABEL: ctjs.func @add$1(
// CHECK-SAME: %arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value,
// CHECK-SAME: %arg3: !ctjs.value, %arg4: !ctjs.value) -> !ctjs.value

// THE REGISTER WINDOW IS ON THE OPERATION. `add(a, b)` needs slots for its two
// parameters, and ct_aot_enter has nowhere else to learn the number.
// CHECK: %[[CTX:.*]] = ctjs.frame_enter {{[0-9]+}}
// SOURCE `+` IS add_generic, which becomes the RE-ENTERING ctjs.binary - not
// ctjs.binary_static. `op::add` comes only from `++` and three internal
// counters, so `x++` on {valueOf: () => 3} is NaN and never runs user code.
// CHECK: %[[SUM:.*]] = ctjs.binary add %arg3, %arg4
// CHECK: ctjs.frame_exit %[[CTX]]
// CHECK: ctjs.return %[[SUM]]
