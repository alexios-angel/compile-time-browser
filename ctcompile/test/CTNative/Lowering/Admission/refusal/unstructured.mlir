// Two refusals that read alike and come from different places.
//
//   "unstructured control flow"                                   <- the pass
//                                                                    driver's
//                                                                    early-out
//   "unstructured control flow - run --ctjs-lift-to-scf first"    <- the `cf`
//                                                                    arm of
//                                                                    admission
//
// They are not two spellings of one rule, and telling them apart matters.
//
// The first is `runOnOperation`'s early-out. --ctjs-lift-to-scf tried to
// structure the function and could not (here because a try/catch installs a
// handler, and MLIR's transformCFGToSCF refuses a region whose terminator has
// side effects), so it recorded `ctjs.not_structured` and left the CFG alone.
// The lowering then declines the function before the admission check runs at
// all; nothing about the function's operations or types has been looked at.
//
// The second is the `cf` arm inside `admission::op`, and it means the lift
// never ran. The normal pipeline always runs the lift first, so real code
// never reaches this arm (refusals in real code worded like it come from the
// early-out above). Only a test that omits the lift can reach it and pin its
// wording; without this file the arm could be deleted and no test would
// fail.
//
// RUN: split-file %s %t
// The lift runs, cannot structure `guarded`, and records why:
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/handler.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=EARLYOUT
// The lift is omitted on purpose, which is the only way to reach the arm:
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/branchy.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CFARM
// With the lift back in, the same program is native, so the arm reflects the
// pipeline and not the program:
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/branchy.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=LIFTED --implicit-check-not=ctnative.not_native

// --- the early-out: the lift tried and recorded why it could not ------------
//
// The function keeps both attributes (the lift's reason and the lowering's),
// and the lowering's carries no advice, because there is nothing to re-run.
//
// EARLYOUT: ctjs.func {{.*}}@guarded$1
// EARLYOUT-SAME: ctjs.not_structured
// EARLYOUT-SAME: ctnative.not_native = "unstructured control flow"
// EARLYOUT-NOT: run --ctjs-lift-to-scf first

// --- the arm: cf operations reached the lowering, so the lift never ran -----
//
// CFARM: ctjs.func {{.*}}@branchy$1
// CFARM-SAME: ctnative.not_native = "unstructured control flow - run --ctjs-lift-to-scf first"

// --- and the same program, lifted, is claimed -------------------------------
//
// LIFTED: emitc.func @main
// LIFTED: emitc.func @branchy_1

//--- handler.js
function guarded(n) {
  try { return n * 2; } catch (e) { return 0; }
}
var r = guarded(3);

//--- branchy.js
function branchy(n) {
  if (n > 0) { return 1; }
  return 2;
}
var r = branchy(3);
