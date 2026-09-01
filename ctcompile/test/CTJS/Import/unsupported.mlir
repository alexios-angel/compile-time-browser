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

// THE EXAMPLE HAS NOW BEEN REWRITTEN THREE TIMES, and every time for the right
// reason: it named something that started working. It was a closure, then it
// was `+a` - unary plus, op::to_number - then it was `import(u)`, which was
// chosen "to outlast the phase" and lasted until the ES-modules commit that
// removed op::dyn_import from ImporterCoverage's pending list.
//
// SO THIS ONE IS CHOSEN FOR THE OPCODE FURTHEST FROM BEING IMPLEMENTED, which
// is not the same as the one furthest down a plan. op::yield_value is
// may_suspend, and the ABI has NO SUSPEND HELPER AT ALL: the only row covering
// it is ct_aot_suspend_unsupported, which exists to make "this opcode has no
// tier-1 lowering" a declared outcome with a diagnosable message. Its own
// comment says Phase 14 owns the suspend ABI and that this table "commits to
// NO suspend ABI" until then. A suspending opcode is also invisible to
// ImporterCoverage's ratchet - that test skips may_suspend rows - so nothing
// else can make this file stale by accident.
//
// WHEN PHASE 14 LANDS A SUSPEND ABI, THIS FILE NEEDS A NEW EXAMPLE AGAIN, and
// if by then nothing is genuinely unsupported the honest move is to delete it
// rather than to pick something that merely looks obscure. A negative test has
// to keep naming something GENUINELY unsupported or it asserts nothing.
function* counted(n) { yield n; }

// NO FUNCTION AT ALL, not a partial one.
//
// READ WHAT THIS ACTUALLY CHECKS, because it is narrower than it looks. A
// CHECK-NOT before the first CHECK constrains only the text BEFORE that CHECK
// matches - and `ctjs.skipped` is a module attribute, so it matches on line one
// and this covers the module header alone. The top level itself IS imported and
// prints below: it declares `counted` with a create_closure over a function
// index, and the importer does not refuse a closure whose target was skipped.
// What this line rules out is a ctjs.func emitted BEFORE the skip record - a
// partial `counted` - which is the shape the invariant is about.
// CHECK-NOT: ctjs.func

// AND THE REASON, structured: which function, which opcode, where.
//
// THE GENERIC REASON IS NOW UNREACHABLE FROM JAVASCRIPT, which is why this
// asserts the precise one instead. Every NON-suspending opcode in
// bytecode_opcodes.def is imported - ctcompile_importer_coverage asserts
// exactly that - so the only opcodes left are the two that suspend, and those
// are refused by a predicate expanded from the table's own may_suspend column
// with a message that says what the problem actually is.
//
// So "no CTJS operation for this opcode yet" survives as the default arm for an
// opcode ADDED to the table tomorrow, and nothing this file can write reaches
// it today. That is the fourth time this test has been repointed and the first
// time the reason changed rather than the opcode.
// CHECK: ctjs.skipped
// CHECK-SAME: opcode = "yield_value"
// CHECK-SAME: reason = "a suspension point
