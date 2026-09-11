// TWO REFUSALS THAT READ ALIKE AND COME FROM DIFFERENT PLACES.
//
//   "unstructured control flow"                                   <- the pass
//                                                                    driver's
//                                                                    EARLY-OUT
//   "unstructured control flow - run --ctjs-lift-to-scf first"    <- the `cf`
//                                                                    arm of
//                                                                    admission
//
// They are not two spellings of one rule and telling them apart matters:
//
// THE FIRST is `runOnOperation`'s early-out. --ctjs-lift-to-scf TRIED to
// structure the function and could not - here because a try/catch installs a
// handler, and MLIR's transformCFGToSCF refuses a region whose terminator has
// side effects - so it recorded `ctjs.not_structured` and left the CFG alone.
// The lowering then declines the function BEFORE the admission check runs at
// all. Nothing about the function's operations or types has been looked at.
//
// THE SECOND is the `cf` arm inside `admission::op`, and it means the lift
// never ran. On all three real corpora it fires ZERO times, because the
// pipeline always runs the lift first - the 46 refusals worded like it in the
// census come from the early-out above. So a lit test is the ONLY thing that
// can reach it, and without this file its wording is unverifiable: a rewrite
// could delete the arm outright and every gate in the tree would stay green.
//
// RUN: split-file %s %t
// The lift runs, cannot structure `guarded`, and records why:
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/handler.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=EARLYOUT
// The lift is DELIBERATELY OMITTED, which is the only way to reach the arm:
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/branchy.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CFARM
// And with the lift back in, the SAME program is native - so the arm is
// measuring the pipeline and not the program:
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/branchy.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=LIFTED --implicit-check-not=ctnative.not_native

// --- the early-out: the lift tried and recorded why it could not ------------
//
// The function keeps BOTH attributes - the lift's reason and the lowering's -
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
