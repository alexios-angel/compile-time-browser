// THE OPERAND RULES, AND THE TWO `why...()` CATCH-ALLS.
//
// Three families that between them account for most of the refusals a program
// slightly outside the tier will ever see, and none of which had a pin - plus
// the one refusal that cites an obligation by number:
//
//   1. `admission::numeric` - ONE message with NINE call sites, each passing a
//      different `where`. The word in front of "operand is" is the only thing
//      that tells a reader which operation refused, so a rewrite that lost the
//      `where` argument would leave every one of them saying the same thing
//      and no test would notice. Two are pinned here.
//   2. the carrier check in `admission::function`, "a value of type T from
//      `OP`" - the sweep after every operation, which catches what the arms
//      let through. It is the most common refusal in this file's own probes
//      and it was pinned nowhere.
//   3. the LAST alternative of whyOpen and whyNotDense, "... escapes - it
//      reaches `OP`". Both helpers name the first use that disqualified the
//      literal, and both end in a catch-all that names the operation instead
//      of a route. The routes are pinned (native-struct.mlir,
//      native-array.mlir); the catch-alls were not, and a reader who meets one
//      is exactly the reader who most needs it to say something true.
//
// ONE PROGRAM PER FILE; see the header of shape-field-names.mlir for why.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/binary.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BINARY
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
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/looparray.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=LOOPARRAY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/tilde.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=TILDE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/bits.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BITS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/frameslot.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=FRAMESLOT

// --- numeric(), where = "binary" --------------------------------------------
//
// BINARY: ctjs.func {{.*}}@badd$1
// BINARY-SAME: ctnative.not_native = "binary operand is !ctnative.bool, not a number"

// --- numeric(), where = "store to global `X`" -------------------------------
//
// The only `where` that is COMPUTED rather than a literal, so it is the one a
// rewrite is most likely to drop. A global is a `static double`; storing a
// boolean into one has no representation.
//
// STORE: ctjs.func {{.*}}@_script_$0
// STORE-SAME: ctnative.not_native = "store to global `r` operand is !ctnative.bool, not a number"

// --- the carrier sweep: a value no arm objected to, with no carrier ---------
//
// `boxed` is what a call into the open world returns. No arm in `admission::op`
// refuses ctjs.call_direct - its arguments were all numbers - so it is the
// per-value sweep afterwards that catches the RESULT, and the message names
// the type and the operation that produced it rather than any rule.
//
// CARRIER: ctjs.func {{.*}}@_script_$0
// CARRIER-SAME: ctnative.not_native = "a value of type !ctnative.boxed from `ctjs.call_direct`"

// --- whyOpen's catch-all ----------------------------------------------------
//
// `!o` on an object literal: not a get, not a set, not a loop edge, not a
// return - so the helper falls through to naming the operation that took it.
//
// OBJREACH: ctjs.func {{.*}}@nt$1
// OBJREACH-SAME: ctnative.not_native = "an object literal that escapes - it reaches `ctjs.unary`"

// --- whyNotDense's catch-all ------------------------------------------------
//
// `a.push(n)` where `push` is reached as a PROPERTY and called: the array is
// the receiver of a ctjs.call, which is neither an append nor a read.
//
// ARRREACH: ctjs.func {{.*}}@ap$1
// ARRREACH-SAME: ctnative.not_native = "an array literal that escapes - it reaches `ctjs.call`"

// --- whyOpen's dynamic-key route --------------------------------------------
//
// DYNKEY: ctjs.func {{.*}}@dyn$1
// DYNKEY-SAME: ctnative.not_native = "an object literal reached through a dynamic key"

// --- whyNotDense's loop-carried route ---------------------------------------
//
// The OBJECT half of this pair is pinned by native-struct.mlir; the ARRAY half
// is a separate string in a separate helper and was pinned by nothing.
//
// LOOPARRAY: ctjs.func {{.*}}@ab$1
// LOOPARRAY-SAME: ctnative.not_native = "an array literal that is loop-carried - more than one value reaches the variable that holds it (assigned again inside a loop, or on only one path before it)"

// --- the two "not native yet" operator arms ---------------------------------
//
// They are worded almost alike and come from different operations - ctjs.unary
// and ctjs.binary_static - so a reader cannot tell them apart by eye and a
// rewrite that merged them would look like a tidy-up.
//
// MEASURED, AND IT SURPRISES: `x | y`, `x >> y` and `x >>> y` all arrive as
// ctjs.binary_static, so the BinaryOp arm's own default ("a bitwise or string
// operator is not native yet") is not reachable from any JavaScript operator
// tried here. It is left unpinned rather than pinned against a construct that
// does not produce it.
//
// TILDE: ctjs.func {{.*}}@bits$1
// TILDE-SAME: ctnative.not_native = "typeof, void and ~ are not native yet"

// BITS: ctjs.func {{.*}}@shift$1
// BITS-SAME: ctnative.not_native = "a static bitwise operator is not native yet"

// --- obligation O-4: one literal, one frame slot ----------------------------
//
// A vector site made INSIDE a loop body would declare its std::vector inside
// that block and the storage would end at the closing brace. The refusal is
// the only one in the file that cites an obligation by number, which is what
// makes it worth pinning: the number is how a reader gets from the message
// back to part 24, and a reworded message that dropped it would break that
// link with nothing to notice.
//
// FRAMESLOT: ctjs.func {{.*}}@la$1
// FRAMESLOT-SAME: ctnative.not_native = "an array literal created inside a branch or a loop - its storage has to be one frame slot (obligation O-4)"

//--- binary.js
function badd(n) { var u = true; return u + n; }
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
function dyn(n) { var o = { x: 1 }; o[n] = 2; return o.x; }
var r = dyn(1);

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
