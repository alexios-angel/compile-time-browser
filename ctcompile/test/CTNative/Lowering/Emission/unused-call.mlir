// A CALL WHOSE RESULT NOTHING READS IS A STATEMENT, not a declaration.
//
// The native lowering and --convert-scf-to-emitc both produce calls whose
// result is dead - a function invoked for its effect on a global, a helper
// whose value is dropped on one path. Declaring a variable for one emits
// `double v = f();` with v never read, and part 24 Phase 63 Step 7 compiles
// generated code with -Wall -Wextra -Werror: an unused variable is a BUILD
// FAILURE on a program the pipeline declared native. Nothing downstream saves
// it either - EmitC ops declare no memory effects, so the canonicalizer's
// `isOpTriviallyDead` is false for every one of them, and the prune pass will
// not erase a call, because a call may do anything.
//
// THE DIVERGENCE IS OPT-IN. Upstream's emitter declares the variable, and its
// tests live in test/Target/Cpp/upstream/ verbatim to measure this fork's
// distance from it, so the printer keys off `ctnative.statement` rather than
// off use_empty() itself: --ctnative-prune-dead-stores sets it, the emitter
// reads it, and nothing else changes.
//
// RUN: ctjs-opt --ctnative-prune-dead-stores %s | FileCheck %s
// RUN: ctjs-opt --ctnative-prune-dead-stores %s | ctjs-translate --mlir-to-cpp | FileCheck --check-prefix=CPP %s
// RUN: ctjs-translate --mlir-to-cpp %s | FileCheck --check-prefix=UNMARKED %s
// RUN: ctjs-opt --ctnative-prune-dead-stores --ctnative-print-deduced %s | ctjs-translate --mlir-to-cpp | FileCheck --check-prefix=DEDUCED %s
// RUN: ctjs-opt "--ctnative-prune-dead-stores=report=true" %s 2>&1 >/dev/null | FileCheck --check-prefix=REPORT %s

emitc.func private @effect(%x: f64) -> f64

emitc.func @dropped(%a: f64) -> f64 attributes {ctnative.provenance = "function dropped, fixture.js:1:1"} {
  // read: a declaration, as always
  %kept = emitc.call @effect(%a) : (f64) -> f64
  // NOT read: a statement
  %dead = emitc.call @effect(%kept) : (f64) -> f64
  emitc.return %kept : f64
}

// --- the pass marks exactly the dead one -----------------------------------
//
// CHECK-LABEL: emitc.func @dropped
// CHECK: %[[K:.*]] = call @effect
// CHECK-NOT: ctnative.statement
// CHECK: call @effect(%[[K]])
// CHECK-SAME: ctnative.statement

// --- and the emitter prints it as a statement ------------------------------
//
// CPP: double dropped(double [[A:v[0-9]+]]) {
// CPP-NEXT: double [[K:v[0-9]+]] = effect([[A]]);
// CPP-NEXT: effect([[K]]);
// CPP-NEXT: return [[K]];

// --- WITHOUT the mark, upstream's behaviour is unchanged --------------------
//
// This is the guard on the guard: if the printer ever keys off use_empty()
// again, this line goes red and the eight verbatim upstream tests go red with
// it.
//
// UNMARKED: double dropped(double [[A:v[0-9]+]]) {
// UNMARKED-NEXT: double [[K:v[0-9]+]] = effect([[A]]);
// UNMARKED-NEXT: double {{v[0-9]+}} = effect([[K]]);

// --- the printing policy does not deduce or pin what is not declared -------
//
// DEDUCED: double dropped(double [[A:v[0-9]+]]) {
// DEDUCED-NEXT: auto [[K:v[0-9]+]] = effect([[A]]);
// DEDUCED-NEXT: CTCOMPILE_PIN([[K]],
// DEDUCED-NEXT: effect([[K]]);
// DEDUCED-NOT: CTCOMPILE_PIN

// --- the count is asserted, not read ---------------------------------------
//
// Statistics are inert in the release LLVM this box installs, so the pass
// carries an explicit report and the test pins its number: exactly one call
// marked, which is the dead one and not the live one.
//
// REPORT: marked 1 call(s) as statements
