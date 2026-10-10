// An opcode with no mapping abandons the whole function.
//
// AOT code is never emitted partially correct, and this is the test of that
// rule: a function containing one unsupported opcode produces no ctjs.func at
// all, and says why in a form a machine can read.
//
// The failure this guards against is not a crash. It is a function that is
// translated up to the opcode nobody handled and then quietly ends, which
// verifies, prints, and computes something else.

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | ctjs-opt | FileCheck %s

// The example is a generator because op::yield_value is the opcode furthest
// from being implemented: it is may_suspend, and the AOT ABI has no suspend
// helper at all. The only row covering it is ct_aot_suspend_unsupported
// (ctbrowser/include/ctbrowser/aot/helpers/calls-and-modules.def), which
// makes "this opcode has no AOT lowering" a declared outcome with a
// diagnosable message rather than a silent gap. Any opcode the importer does
// handle would make the function import and this test fail.
//
// A suspending opcode is also skipped by the importer coverage test
// (Import/Coverage.cpp ignores may_suspend rows), so no other test can make
// this file stale by accident: the example only stops being an example when
// someone deliberately implements suspension.
//
// Once a suspend ABI exists, this file needs a new example. If nothing is
// genuinely unsupported by then, delete the file rather than picking an
// opcode that merely looks obscure: a negative test must name something that
// really is unsupported, or it asserts nothing. Whatever replaces the example
// must first be confirmed unhandled by the importer, or the test passes for
// the wrong reason.

function* counted(n) { yield n; }

// No function at all, not a partial one.
//
// READ WHAT THIS ACTUALLY CHECKS, because it is narrower than it looks. A
// CHECK-NOT before the first CHECK constrains only the text BEFORE that CHECK
// matches, and `ctjs.skipped` is a module attribute, so it matches on line one
// and this covers the module header alone. The top level itself is imported and
// prints below: it declares `counted` with a create_closure over a function
// index, and the importer does not refuse a closure whose target was skipped.
// What this line rules out is a ctjs.func emitted before the skip record (a
// partial `counted`), which is the shape the rule is about.
// CHECK-NOT: ctjs.func

// And the reason, structured: which function, which opcode, where.
//
// Every non-suspending opcode in bytecode_opcodes.def is imported
// (ctcompile_importer_coverage checks exactly that), so the only opcodes left
// are the two that suspend. Those are refused by a predicate expanded from the
// table's own may_suspend column, with a message that says what the problem
// is, so this asserts that specific reason.
//
// The generic "no CTJS operation for this opcode yet" reason survives as the
// default arm for an opcode added to the table later; nothing JavaScript can
// express reaches it today. A new opcode without importer support fails the
// coverage test first, before anything here could notice.

// CHECK: ctjs.skipped
// CHECK-SAME: opcode = "yield_value"
// CHECK-SAME: reason = "a suspension point
