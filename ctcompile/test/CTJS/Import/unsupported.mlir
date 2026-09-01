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

// THE EXAMPLE HAS NOW BEEN REWRITTEN TWICE, and both times for the right
// reason: it named something that started working. It was a closure, then it
// was `+a` - unary plus, op::to_number - and the importer handles both.
//
// SO THIS ONE IS CHOSEN TO OUTLAST THE PHASE. `import(u)` is op::dyn_import,
// which belongs to the ES-modules phases and is not on Phase 13's list at all;
// picking any of Phase 13's own remaining opcodes would mean rewriting this
// file again within the week. A negative test has to keep naming something
// GENUINELY unsupported or it asserts nothing.
function loads(u) { return import(u); }

// NO FUNCTION AT ALL, not a partial one. The top level is skipped with it,
// because it declares `loads` and a function that cannot be imported cannot be
// named by a closure either.
// CHECK-NOT: ctjs.func

// AND THE REASON, structured: which function, which opcode, where.
// CHECK: ctjs.skipped
// CHECK-SAME: opcode = "dyn_import"
// CHECK-SAME: reason = "no CTJS operation for this opcode yet"
