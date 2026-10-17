// The operand-related refusal messages of the native lowering, and cases that
// used to be refused here and now compile natively:
//
//   1. `admission::numeric`: one message used from several call sites, each
//      passing a different `where`; that word in front of "operand is" is the
//      only thing that names the refusing operation, so a rewrite that lost
//      the `where` argument would make every one of them say the same thing.
//      The examples below that once hit it now lower; a wider
//      Boolean/Number/String union is refused earlier by the carrier check.
//   2. The carrier check in `admission::function` ("a value of type T from
//      `OP`"), which runs after every operation and catches values that no
//      per-operation rule refused.
//   3. The final fallback of whyOpen and whyNotDense ("... escapes - it
//      reaches `OP`"). Both helpers name the first use that disqualified the
//      literal, and both end in a fallback that names the operation instead of
//      an escape route. The routes are tested in Objects/struct.mlir and
//      Objects/array.mlir; this file tests the fallbacks.
//
// It also pins the one refusal that cites an obligation by number (O-4, the
// array literal created inside a loop).
//
// One program per split-file input: a refusal spreads to callers and callees,
// so two cases in one program could pass for the wrong reason (see the header
// of Objects/shape-field-names.mlir).
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/binary.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false \
// RUN:   | FileCheck %s --check-prefix=BINARY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/generic-binary.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false \
// RUN:   | FileCheck %s --check-prefix=GENERIC --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/union-binary.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false \
// RUN:   | FileCheck %s --check-prefix=UNION --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/optional-binary.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false \
// RUN:   | FileCheck %s --check-prefix=OPTIONAL --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/store.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=STORE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/carrier.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CARRIER
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/objectreaches.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=OBJREACH
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/arrayreaches.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ARRREACH
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dynamickey.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=DYNKEY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dyntable.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/dyntable.mlir
// RUN: FileCheck %s --check-prefix=DYNTABLE --implicit-check-not=ctnative.not_native < %t/dyntable.mlir
// RUN: ctjs-opt %t/dyntable.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/dyntable.cpp
// RUN: %gxx -O2 %t/dyntable.cpp -o %t/dyntable-gcc
// RUN: %t/dyntable-gcc | FileCheck %s --check-prefix=DYNTABLERUN
// RUN: %clangxx -O2 %t/dyntable.cpp -o %t/dyntable-clang
// RUN: %t/dyntable-clang | FileCheck %s --check-prefix=DYNTABLERUN
// RUN: %native_reference %t/dyntable.js | FileCheck %s --check-prefix=DYNTABLERUN
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/dyntable.js | FileCheck %s --check-prefix=DYNTABLERUN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/looparray.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false \
// RUN:   | FileCheck %s --check-prefix=LOOPARRAY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/tilde.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false \
// RUN:   | FileCheck %s --check-prefix=TILDE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/bits.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false \
// RUN:   | FileCheck %s --check-prefix=BITS --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/frameslot.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false \
// RUN:   | FileCheck %s --check-prefix=FRAMESLOT

// --- numeric() cases that now lower, and a wider union that does not ---------
//
// Subtraction on a String or Boolean/String union and generic addition are
// native. A Boolean/Number/String union fails the carrier check at its join.
// Each program below was once refused.
// BINARY: ctjs.func {{.*}}@badd$1
// BINARY-SAME: ctnative.not_native = "a value of type !ctnative.variant<!ctnative.bool, !ctnative.num<i32>, !ctnative.str<utf8>> from `scf.if`"

// GENERIC: call_opaque "ctnative::add"
// UNION: call_opaque "ctnative::to_number"{{.*}}!emitc.opaque<"ctnative::boolean_string">{{.*}} -> !emitc.opaque<"ctnative::js_num">
// OPTIONAL: member_call_opaque {{.*}} "to_number"() : !emitc.opaque<"ctnative::nullable_string">, () -> !emitc.opaque<"ctnative::js_num">

// --- a Boolean global ---------------------------------------------------------
//
// A Boolean global is printed natively. The equality result keeps its Boolean
// tag through storage and output.
//
// STORE-NOT: ctnative.not_native
// STORE: emitc.func @main()
// STORE: call_opaque "ctnative::global_boolean"
// STORE: emitc.func @eqbool_1
// STORE-NOT: ctnative.not_native

// --- the carrier check: a value no arm objected to, with no carrier --------
//
// `boxed` is what a call into the open world returns. No arm in `admission::op`
// refuses ctjs.call_direct (its arguments were all numbers), so it is the
// per-value check afterwards that catches the result, and the message names
// the type and the operation that produced it rather than any rule.
//
// CARRIER: ctjs.func {{.*}}@_script_$0
// CARRIER-SAME: ctnative.not_native = "a value of type !ctnative.boxed from `ctjs.call_direct`"

// --- whyOpen's fallback -----------------------------------------------------
//
// `!o` on an object literal is not a get, a set, a loop edge or a return, so
// the helper falls through to naming the operation that took it.
//
// OBJREACH: ctjs.func {{.*}}@nt$1
// OBJREACH-SAME: ctnative.not_native = "an object literal that escapes - it reaches `ctjs.unary`"

// --- whyNotDense's fallback -------------------------------------------------
//
// `a.push(n)` reads `push` as a property and calls it: the array is the
// receiver of a ctjs.call, which is neither an append nor a read.
//
// ARRREACH: ctjs.func {{.*}}@ap$1
// ARRREACH-SAME: ctnative.not_native = "an array literal that escapes - it reaches `ctjs.call`"

// --- whyOpen's dynamic-key route --------------------------------------------
//
// A literal written under a dynamic key that is also kept in an array is not a
// table (Inference/Dictionaries.cpp declines it), so the record rules see the
// dynamic key, its newest use, first.
// DYNKEY: ctjs.func {{.*}}@dyn$1
// DYNKEY-SAME: ctnative.not_native = "an object literal reached through a dynamic key"

// The same literal used only as a table lowers to a dictionary: a write under
// "x" replaces the field, one under "y" adds another.
// DYNTABLE: emitc.func @dyn
// DYNTABLERUN: r=21

// --- whyNotDense's loop-carried route ---------------------------------------
//
// The object version of this message is tested in Objects/struct.mlir; the
// array version comes from a separate helper and is tested here.
//
// LOOPARRAY: ctjs.func {{.*}}@ab$1
// LOOPARRAY-SAME: ctnative.not_native = "an array literal that is loop-carried - more than one value reaches the variable that holds it (assigned again inside a loop, or on only one path before it)"

// --- Number bitwise operators -----------------------------------------------
//
// Both programs lower: unary ~ and binary | operate on js_num, whose overloads
// implement JavaScript's 32-bit conversion.
//
// `x | y`, `x >> y` and `x >>> y` arrive as ctjs.binary (the VM converts their
// operands with ToNumeric), and the BinaryOp arm admits every bitwise kind with
// numeric operands, so its own default ("a bitwise or string operator is not
// native yet") is reachable from no JavaScript operator. It is left unpinned
// rather than pinned against a construct that does not produce it.
//
// TILDE: bitwise_not {{.*}} : (!emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">

// BITS: bitwise_or {{.*}} : (!emitc.opaque<"ctnative::js_num">, !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num">

// --- array literal inside a loop: one literal, one frame slot ---------------
//
// A vector made inside a loop body would declare its std::vector inside that
// block, so its storage would end at the closing brace. The message cites
// obligation O-4 (see the comment above the check in
// lib/CTNative/Lowering/Admission/Operations.cpp); this test keeps that
// citation in the message, so a reader can get from the refusal back to the
// rule.
//
// FRAMESLOT: ctjs.func {{.*}}@la$1
// FRAMESLOT-SAME: ctnative.not_native = "an array literal created inside a branch or a loop - its storage has to be one frame slot (obligation O-4)"

//--- binary.js
function badd(n) { var u = n; if (n > 0) { u = "text"; } else if (n < 0) { u = false; } return u + n; }
var r = badd(1);

//--- generic-binary.js
function badd(n) { var u; if (n > 0) { u = "text"; } else { u = false; } return u + n; }
var r = badd(1);

//--- union-binary.js
function badd(n) { var u; if (n > 0) { u = "text"; } else { u = false; } return u - n; }
var r = badd(1);

//--- optional-binary.js
function badd(n) { var u; if (n > 0) { u = "text"; } return u - n; }
var r = badd(1);

//--- store.js
function eqbool(n) { var u; if (n > 0) { u = true; } return u === true; }
var r = eqbool(3);

//--- carrier.js
function reader() { return unstored + 1; }
var r = reader();

//--- objectreaches.js
function nt(n) { var o = { x: 1 }; return !o; }
var r = nt(1);

//--- arrayreaches.js
function ap(n) { var a = [1]; a.push(n); return a[0] + a.length; }
var r = ap(2);

//--- dynamickey.js
function dyn(n) { var o = { x: 1 }; var keep = [o]; o[n] = 2; return o.x + keep.length; }
var r = dyn(1);

//--- dyntable.js
function dyn(n) { var o = { x: 1 }; o[n] = 2; return o.x; }
var r = dyn("x") * 10 + dyn("y");

//--- looparray.js
function ab(n) { var a; if (n > 0) { a = [1, 2]; } else { a = [3]; } return a[0]; }
var r = ab(1);

//--- tilde.js
function bits(x) { return ~x; }
var r = bits(3);

//--- bits.js
function shift(x, y) { return x | y; }
var r = shift(3, 5);

//--- frameslot.js
function la(n) { var t = 0; while (n > 0) { var a = [1, 2]; t = t + a[0]; n = n - 1; } return t; }
var r = la(3);
