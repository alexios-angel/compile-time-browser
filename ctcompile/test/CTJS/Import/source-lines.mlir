// Every instruction carries its own source line, not its function's span.
//
// A function-wide span would give every instruction the same location, so
// nothing downstream could tell two statements apart: there could be no
// `#line` in the emitted C++, no stepping through the JavaScript, and
// diagnostics could only name a function, never a statement.
//
// The lines come from `function_proto::code_offsets`, a per-instruction
// source offset recorded by the bytecode compiler, and `script::line_table`,
// which turns an offset into a line and a column. The importer only wires
// the two together.
//
// The parser does not carry positions: `vp::node::begin` is set for
// functions only. The offsets are recovered because `node::text` is a
// string_view into the source, so a lexeme's address minus the source's
// address is where it started. There is no position field to read
// instead, so do not "fix" this by looking for one.


// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s --mlir-print-debuginfo \
// RUN:   | FileCheck %s

function first(a) {
  var n = a + 1;
  var m = n + 2;
  return m;
}

// Three different lines from one function is the entire assertion: a
// regression to the function span collapses all three into one location rather
// than crashing.
//
// The numbers are lines in this file, not in the snippet: lit hands the whole
// file to ctjs-translate as JavaScript, comments included, so `function first`
// is at line 23 and not at line 1. Editing the prose above this point moves
// them. That is ugly, but better than a check that cannot fail: asserting
// "some line" would pass against the function span this test exists to
// reject.
//
// EACH CHECK-DAG CONSUMES A DISTINCT MATCH, which is why line 25 is asserted
// only through its two columns below: a bare `25:` here would eat one of them
// and the third pattern would then have nothing left to match, which reads as
// "the column is missing" when the column is present and the pattern was
// greedy.
// CHECK-DAG: loc("{{[^"]*}}source-lines.mlir":23:
// CHECK-DAG: loc("{{[^"]*}}source-lines.mlir":24:

// And the columns differ within a line. On line 25, `n + 2` and the assignment
// to `m` are not the same place; a table that got the line right and the column
// wrong would satisfy the checks above and still be too coarse to step through.
// CHECK-DAG: loc("{{[^"]*}}source-lines.mlir":25:13
// CHECK-DAG: loc("{{[^"]*}}source-lines.mlir":25:15
