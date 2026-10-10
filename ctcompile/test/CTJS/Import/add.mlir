// The smallest real function there is, imported.
//
// `ctjs-translate` reads JavaScript here rather than a program image, and that
// is deliberate: a lit test then reads as one file (the source in, the expected
// IR beside it), where the image form would need a binary fixture built by
// another tool and regenerated whenever the image format moved. Both spellings
// go through the same importer.
//
// `ctjs-opt` is part of the test: it runs the verifier on what the importer
// produced. A verifier failure here is an importer bug, never a reason to
// relax the verifier.

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | ctjs-opt | FileCheck %s

function add(a, b) { return a + b; }

// The top level compiles too. It contains `closure` (the declaration of
// `add` itself), and a top level that declares anything must be imported
// like any other function rather than recorded as a ctjs.skipped refusal,
// so no skipped row may appear.
// CHECK-NOT: ctjs.skipped

// CHECK: ctjs.func @_script_$0(
// It builds the closure from its own callee, which is where the enclosing
// closure comes from: the program is selected from `enclosing->owner`, because
// a context can be running functions from more than one program at a time.
// CHECK: ctjs.create_closure %arg2[1]

// Five parameters for a two-parameter function. The first three are the frame
// properties the bytecode reads with their own opcodes (`this`, `new.target`
// and the callee), so load_this and its two siblings cost nothing.
// The index is part of the symbol. ctjs.func is a Symbol, and a real program
// has many functions sharing a name (p5.js has dozens called `constructor`).
// Without the suffix a large module fails to verify on a duplicate symbol,
// and ctjs-translate then produces no output at all even though every
// individual function verifies.
// CHECK-LABEL: ctjs.func @add$1(
// CHECK-SAME: %arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value,
// CHECK-SAME: %arg3: !ctjs.value, %arg4: !ctjs.value) -> !ctjs.value

// The register window size is on the operation. `add(a, b)` needs slots for its
// two parameters, and ct_aot_enter has nowhere else to learn the number.
// CHECK: %[[CTX:.*]] = ctjs.frame_enter {{[0-9]+}}
// Source `+` is add_generic, which becomes the re-entering ctjs.binary, not
// ctjs.binary_static. `op::add` comes only from `++` and three internal
// counters, so `x++` on {valueOf: () => 3} is NaN and never runs user code.
// CHECK: %[[SUM:.*]] = ctjs.binary add %arg3, %arg4
// CHECK: ctjs.frame_exit %[[CTX]]
// CHECK: ctjs.return %[[SUM]]
