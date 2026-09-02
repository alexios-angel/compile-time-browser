// EVERY INSTRUCTION CARRIES ITS OWN LINE, not its function's span.
//
// It used to carry `proto.source_begin, proto.source_end` - the FUNCTION's
// extent, identical on every instruction in it. That is a location which cannot
// tell two statements apart, so nothing downstream could either: no `#line` in
// the emitted C++, no way to step in the JavaScript, and a diagnostic that
// named a function rather than a statement.
//
// WHAT MADE IT POSSIBLE was not this file. `function_proto::code_offsets` is a
// per-instruction source offset the compiler always knew and always discarded,
// and `script::line_table` turns one into a line and a column. Both arrived
// with the debug tables; this is only the wiring.
//
// AND THE PARSER DOES NOT CARRY POSITIONS EITHER - `vp::node::begin` is set for
// FUNCTIONS only. The offsets are recovered because `node::text` is a
// string_view INTO the source, so a lexeme's address minus the source's address
// is where it started. That is worth knowing before anyone tries to "fix" it by
// reading a field that does not exist.

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s --mlir-print-debuginfo \
// RUN:   | FileCheck %s

function first(a) {
  var n = a + 1;
  var m = n + 2;
  return m;
}

// THREE DIFFERENT LINES FROM ONE FUNCTION, which is the entire assertion: a
// regression to the function span collapses all three into one location rather
// than crashing.
//
// THE NUMBERS ARE LINES IN THIS FILE, not in the snippet - lit hands the WHOLE
// file to ctjs-translate as JavaScript, comments included, so `function first`
// is at line 23 and not at line 1. Editing the prose above this point moves
// them, which is ugly and is still better than a check that cannot fail:
// asserting "some line" would pass against the function span it exists to
// reject.
//
// EACH CHECK-DAG CONSUMES A DISTINCT MATCH, which is why line 25 is asserted
// only through its two columns below: a bare `25:` here would eat one of them
// and the third pattern would then have nothing left to match. That failure
// reads as "the column is missing" when the column is present and the pattern
// was greedy.
// CHECK-DAG: loc("{{[^"]*}}source-lines.mlir":23:
// CHECK-DAG: loc("{{[^"]*}}source-lines.mlir":24:

// AND THE COLUMNS DIFFER WITHIN A LINE. On line 25, `n + 2` and the assignment
// to `m` are not the same place; a table that got the line right and the column
// wrong would satisfy the checks above and still be too coarse to step through.
// CHECK-DAG: loc("{{[^"]*}}source-lines.mlir":25:13
// CHECK-DAG: loc("{{[^"]*}}source-lines.mlir":25:15
