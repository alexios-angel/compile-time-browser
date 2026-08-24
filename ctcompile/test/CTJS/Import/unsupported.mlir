// AN OPCODE WITH NO MAPPING ABANDONS THE WHOLE FUNCTION.
//
// "Never emit partially correct AOT code" is the plan's invariant and this is
// the test of it: a function containing one unsupported opcode produces NO
// ctjs.func at all, and says why in a form a machine can read.
//
// The failure this guards against is not a crash. It is a function that is
// translated up to the opcode nobody handled and then quietly ends - which
// verifies, prints, and computes something else.

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | ctjs-opt | FileCheck %s

// THE EXAMPLE USED TO BE A CLOSURE, and closures compile now - which is the
// right reason for a negative test to need rewriting. `+a` is unary plus, and
// it reaches op::to_number, for which the importer still has no operation.
function coerces(a) { return +a; }

// NO FUNCTION AT ALL, not a partial one. The top level is skipped with it,
// because it declares `coerces` and a function that cannot be imported cannot
// be named by a closure either.
// CHECK-NOT: ctjs.func

// AND THE REASON, structured: which function, which opcode, where.
// CHECK: ctjs.skipped
// CHECK-SAME: opcode = "to_number"
// CHECK-SAME: reason = "no CTJS operation for this opcode yet"
