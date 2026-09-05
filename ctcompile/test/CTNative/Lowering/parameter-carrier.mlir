// A proved owning string has a carrier. An unresolved parameter still needs
// a closed-world caller proof; adding strings must not weaken that boundary.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/proved.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PROVED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unproved.js 2>/dev/null | ctjs-opt --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=UNPROVED

// PROVED: emitc.func @tag_1({{.*}}!emitc.opaque<"std::string">) -> !emitc.opaque<"std::string">
// PROVED-NOT: ctnative.not_native

// --- AND THE ONE NOTHING PROVED, WHICH IS STILL 62½-A -----------------------
//
// UNPROVED: ctjs.func @scale$1
// UNPROVED-SAME: ctnative.not_native = "parameter 0 is !ctnative.boxed - no caller proves it (a closed-world call is Phase 62\C2\BD-A)"

//--- proved.js
// Keep the call straight-line: a top-level ternary can forward the last
// declaration's dead closure register and prevent the resolver closing it.
function tag(s) { return s; }
tag("hello");

//--- unproved.js
function scale(x) { return x * 2; }
var b = 1;
