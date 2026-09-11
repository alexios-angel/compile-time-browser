// THE ARM THAT CATCHES EVERYTHING ELSE, AND ITS WORDING.
//
//     return refuse(("`" + o->getName().getStringRef() + "` is not native yet").str());
//
// It is the last line of `admission::op` and the roadmap the plan asks for:
// every operation nobody has taught this tier about comes out of it NAMED, so
// the refusals are a work list rather than a shrug. `rg 'is not native yet'
// over the test tree found nothing under CTNative before this file - the
// arm that produces the largest share of DISTINCT reasons was the one nothing
// pinned.
//
// FOUR OPERATIONS, ONE TEMPLATE. One case would pin a string; four pin the
// SHAPE - operation name in backticks, then the fixed tail - which is what a
// rewrite would break. They are chosen from four different JavaScript
// constructs so that a change to any one importer path leaves the others.
//
// THE ARM IS NOT LIMITED TO ctjs OPERATIONS, and that is deliberate: anything
// left in the function that no earlier arm claimed is named here, whatever
// dialect it came from. (Measured: a `while` with an early return reaches the
// lowering as `scf.index_switch`, refused by this arm as
// "`scf.index_switch` is not native yet". It is not pinned below because it
// depends on how upstream's transformCFGToSCF chooses to structure that loop,
// which is not this project's to fix in place.)
//
// ONE PROGRAM PER FILE - a refusal is contagious both ways; see the header of
// shape-field-names.mlir.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/deleted.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=DELETE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/arguments.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ARGUMENTS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/instanceof.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=INSTANCEOF
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/inop.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=IN

// --- `delete o.x` -----------------------------------------------------------
//
// DELETE: ctnative.not_native = "`ctjs.delete_named` is not native yet"

// --- `arguments` ------------------------------------------------------------
//
// ARGUMENTS: ctnative.not_native = "`ctjs.make_arguments` is not native yet"

// --- `a instanceof b` -------------------------------------------------------
//
// INSTANCEOF: ctnative.not_native = "`ctjs.instanceof` is not native yet"

// --- `k in o` ---------------------------------------------------------------
//
// IN: ctnative.not_native = "`ctjs.has_property` is not native yet"

//--- deleted.js
function deleted(n) { delete n.x; return 1; }
var r = deleted(1);

//--- arguments.js
function counted(n) { return arguments; }
var r = counted(1);

//--- instanceof.js
function isa(a, b) { return a instanceof b; }
var r = isa(1, 2);

//--- inop.js
function has(k, o) { return k in o; }
var r = has(1, 2);
