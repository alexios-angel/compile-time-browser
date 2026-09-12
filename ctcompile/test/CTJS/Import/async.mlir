// WHERE ASYNC SPLITS IN TWO, asserted from both sides in one file.
//
// An async function wraps normal returns and catches throws in a rejection
// fence even when it contains no `await`. Neither operation is a
// suspension - the frame is never lifted and nothing is saved - so it lowers
// like any other runtime call, and an async function with no `await` is fully
// AOT-eligible.
//
// An `await` on a pending promise is a different animal: the interpreter lifts
// the whole frame out of the register stack into a coroutine_object and puts it
// back later, and a compiled body is a C++ stack frame with no register window
// to copy. Phase 14 owns that decision; until it is made the importer refuses
// the function with a reason that SAYS so, rather than falling through to
// "no CTJS operation for this opcode yet" - which reads as a work item and is
// not one.
//
// BOTH HALVES IN ONE FILE ON PURPOSE. Asserting only the refusal would pass
// just as well if wrap_promise were refused too; asserting only the lowering
// would pass if the refusal message had rotted. The pair is what pins the line
// between them.
//
// TWO RUN LINES RATHER THAN ONE, because the two facts are in opposite ends of
// the output: `ctjs.skipped` is a MODULE attribute and prints on the module's
// own line, above every function, so a single ordered sequence of CHECKs
// covering both would be asserting the printer's layout instead of the
// importer's behaviour.

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | ctjs-opt | FileCheck %s --check-prefix=LOWERED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | ctjs-opt | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | ctjs-opt --ctjs-lower-to-emitc | FileCheck %s --check-prefix=EMITTED

// THE COMPILABLE HALF.
async function settles(a) { return a + 1; }

// AND THE HALF THAT IS A DESIGN DECISION. It produces NO ctjs.func at all -
// "never emit partially correct AOT code".
async function waits(p) { return await p; }

// Every operator table must reach the shared per-instruction caught edge.
// Otherwise the promise wrapper inherits an unmapped earlier result as its
// handler snapshot, and the boxed emitter correctly refuses the function.
async function operators(a) { return -(a + 1) < 3; }

// LOWERED: ctjs.func @settles$
// LOWERED: ctjs.wrap_promise
// `waits` follows `settles` in the program's function list, so a body emitted
// for it would appear after this point.
// LOWERED-NOT: ctjs.func @waits$

// REFUSED: ctjs.skipped
// REFUSED-SAME: opcode = "await_value"
// REFUSED-SAME: reason = "a suspension point

// EMITTED: emitc.func @settles_
// EMITTED: emitc.func @operators_
