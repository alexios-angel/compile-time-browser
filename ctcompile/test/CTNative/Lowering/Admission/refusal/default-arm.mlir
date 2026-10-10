// The arm that catches every other operation, and its wording.
//
//     return refuse(("`" + o->getName().getStringRef() + "` is not native yet").str());
//
// It is the last line of `admission::op` in Admission/Operations.cpp: every
// operation no earlier arm handles is refused with its name, so the refusal
// list doubles as a to-do list for native lowering. It produces the largest
// share of distinct refusal reasons.
//
// Four programs. Two reach the default arm (`arguments`, `instanceof`) and pin
// its wording: the operation name in backticks, then the fixed tail. Two
// (`delete`, `in`) stop earlier: the importer turns them into calls to the
// opaque host helpers `__ctbrowser_delete` and `__ctbrowser_in_object`, and a
// host call is refused before the default arm is reached. They use four
// different JavaScript constructs, so a change to one importer path leaves the
// others covering the arm.
//
// The arm is not limited to ctjs operations, deliberately: anything left in
// the function that no earlier arm claimed is named here, whatever dialect it
// came from (an upstream scf or cf operation, for example). No such case is
// pinned below, because which upstream operations reach the lowering depends
// on how upstream's transformCFGToSCF structures the control flow.
//
// One program per split-file input, because a refusal spreads to callers and
// callees and could hide another; see the header of
// Objects/shape-field-names.mlir.
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
// The importer turns `delete` into a call of an opaque host helper, refused.
// DELETE-LABEL: ctjs.func private @deleted$1
// DELETE-SAME: ctnative.not_native =
// DELETE: ctjs.load_global "__ctbrowser_delete"

// --- `arguments` ------------------------------------------------------------
//
// ARGUMENTS: ctnative.not_native = "`ctjs.make_arguments` is not native yet"

// --- `a instanceof b` -------------------------------------------------------
//
// INSTANCEOF: ctnative.not_native = "`ctjs.instanceof` is not native yet"

// --- `k in o` ---------------------------------------------------------------
//
// The frontend checks both operands through opaque helpers before
// ctjs.has_property (13.10.1: the object first, then ToPropertyKey), so, like
// delete, the function stops at the first host call rather than the default arm.
// IN-LABEL: ctjs.func private @has$1
// IN-SAME: ctnative.not_native =
// IN: ctjs.load_global "__ctbrowser_in_object"

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
