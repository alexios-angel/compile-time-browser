#!/usr/bin/env python3
"""Execute closed custom iterators with typed state and public DOM effects."""

import argparse
from pathlib import Path
import shutil

from CTNative.Browser import native_dom as dom
from CTNative.Browser.native_dom_element_iteration import INTRINSICS as SNAPSHOT_INTRINSICS
from CTNative.Browser.native_dom_json import quote
from CTNative.harness import find_compilers

INTRINSICS = [*SNAPSHOT_INTRINSICS, "Symbol", "Object"]
SOURCE = """function customElements(anchor) {
  const values = {
    [Symbol.iterator]() { return this; },
    next() {
      const done = anchor.hasAttribute('data-yielded');
      anchor.setAttribute('data-next', done);
      anchor.setAttribute('data-yielded', 'yes');
      return {done: done, value: anchor};
    },
    return() {
      anchor.setAttribute('data-closed', 'yes');
      return {};
    }
  };
  let count = 0;
  for (const node of values) {
    node.setAttribute('data-visited', 'yes');
    count++;
    @BREAK@
  }
  return count;
}
"""


def source(breaking):
    text = SOURCE.replace("@BREAK@", "if (anchor.hasAttribute('stop')) break;" if breaking else "")
    if breaking:
        text = (
            text.replace("  let count = 0;\n", "")
            .replace("    count++;\n", "")
            .replace("  return count;", "  return anchor.hasAttribute('data-visited');")
        )
    return text


RECEIVER_SOURCE = (
    SOURCE.replace("  const values = {", "  const values = {\n    emitted: 0,")
    .replace(
        "const done = anchor.hasAttribute('data-yielded');",
        "const done = this.emitted > 0;\n      this.emitted++;",
    )
    .replace("@BREAK@", "if (anchor.hasAttribute('stop')) break;")
)
CAPTURE_SOURCE = RECEIVER_SOURCE.replace(
    "  const values = {\n    emitted: 0,", "  let emitted = 0;\n  const values = {"
).replace("this.emitted", "emitted")
CONDITIONAL_RECEIVER_SOURCE = RECEIVER_SOURCE.replace(
    "this.emitted++;", "if (anchor.hasAttribute('advance')) this.emitted++;"
)
CONDITIONAL_CAPTURE_SOURCE = CAPTURE_SOURCE.replace(
    "emitted++;", "if (anchor.hasAttribute('advance')) emitted++;"
)
BRANCH_RECEIVER_SOURCE = (
    RECEIVER_SOURCE.replace("emitted: 0,", "emitted: 0,\n    closed: 1,")
    .replace("this.emitted > 0", "this.closed === 5")
    .replace(
        "this.emitted++;",
        """if (anchor.hasAttribute('advance')) {
        this.emitted++;
        if (anchor.hasAttribute('stop')) this.closed += this.emitted;
        else this.closed += this.emitted + 3;
      } else {
        this.closed += 7;
        this.emitted += this.closed;
      }""",
    )
    .replace(
        "anchor.setAttribute('data-closed', 'yes');",
        """if (anchor.hasAttribute('advance')) {
        this.closed += this.emitted;
        anchor.setAttribute('data-closed', this.closed === 3);
      } else {
        this.emitted += this.closed;
        anchor.setAttribute('data-closed', this.emitted === 16);
      }""",
    )
)
BRANCH_CAPTURE_SOURCE = (
    BRANCH_RECEIVER_SOURCE.replace(
        "  const values = {\n    emitted: 0,\n    closed: 1,",
        "  let emitted = 0;\n  let closed = 1;\n  const values = {",
    )
    .replace("this.emitted", "emitted")
    .replace("this.closed", "closed")
)
LOOP_CAPTURE_SOURCE = CAPTURE_SOURCE.replace("emitted++;", "while (emitted < 1) emitted++;")
LOOP_RECEIVER_SOURCE = (
    RECEIVER_SOURCE.replace("emitted: 0,", "emitted: 0,\n    closed: 1,")
    .replace(
        "this.emitted++;",
        """let rounds = 0;
      while (this.emitted < 2) {
        this.emitted++;
        this.closed += this.emitted;
        rounds++;
      }
      this.closed += rounds;""",
    )
    .replace(
        "anchor.setAttribute('data-closed', 'yes');",
        """while (this.closed < 11) {
        this.closed += this.emitted;
        this.emitted++;
      }
      anchor.setAttribute('data-closed', this.closed === 11);""",
    )
)
LOOP_ORDERED_CAPTURE_SOURCE = (
    LOOP_RECEIVER_SOURCE.replace(
        "  const values = {\n    emitted: 0,\n    closed: 1,",
        "  let emitted = 0;\n  let closed = 1;\n  const values = {",
    )
    .replace("this.emitted", "emitted")
    .replace("this.closed", "closed")
)
LOOP_BREAK_CAPTURE_SOURCE = CAPTURE_SOURCE.replace(
    "emitted++;",
    """while (emitted < 2) {
        emitted++;
        if (anchor.hasAttribute('stop')) break;
      }""",
)
LOOP_BREAK_RECEIVER_SOURCE = (
    RECEIVER_SOURCE.replace("emitted: 0,", "emitted: 0,\n    closed: 1,")
    .replace(
        "const done = this.emitted > 0;\n      this.emitted++;",
        """let rounds = 0;
      while (this.emitted < 2) {
        this.emitted++;
        this.closed += this.emitted;
        rounds++;
        if (anchor.hasAttribute('stop')) break;
        this.closed += 3;
      }
      this.closed += rounds;
      const done = rounds === 0;""",
    )
    .replace(
        "anchor.setAttribute('data-closed', 'yes');",
        """while (this.closed < 11) {
        this.closed += this.emitted;
        this.emitted++;
        if (anchor.hasAttribute('stop')) break;
        this.closed += 5;
      }
      this.closed += this.emitted;
      anchor.setAttribute('data-closed', this.closed === 6);""",
    )
)
LOOP_BREAK_ORDERED_CAPTURE_SOURCE = (
    LOOP_BREAK_RECEIVER_SOURCE.replace(
        "  const values = {\n    emitted: 0,\n    closed: 1,",
        "  let emitted = 0;\n  let closed = 1;\n  const values = {",
    )
    .replace("this.emitted", "emitted")
    .replace("this.closed", "closed")
)
ENTRY_CAPTURE_SOURCE = (
    LOOP_BREAK_ORDERED_CAPTURE_SOURCE.replace(
        "  let count = 0;",
        """  let count = emitted + closed;
  while (emitted < 1) emitted += closed;
  closed += emitted;""",
    )
    .replace(
        "    count++;",
        """    count += emitted + closed;
    if (anchor.hasAttribute('stop')) emitted += 3;
    else emitted += 4;
    closed += emitted;""",
    )
    .replace("closed === 6", "closed === 21")
    .replace(
        "  return count;",
        """  closed += emitted;
  emitted += closed;
  return count + emitted + closed;""",
    )
)
SIBLING_CAPTURE_SOURCE = ENTRY_CAPTURE_SOURCE.replace(
    "  return count + emitted + closed;",
    "  const read = () => emitted;\n  return count + read() + closed;",
)
SIBLING_ORDERED_CAPTURE_SOURCE = (
    ENTRY_CAPTURE_SOURCE.replace(
        "  const values = {", "  const read = () => emitted + closed;\n  const values = {"
    )
    .replace("  let count = emitted + closed;", "  let count = read();")
    .replace(
        "  for (const node of values) {",
        "  count += read();\n  for (const node of values) {",
    )
    .replace("    count += emitted + closed;", "    count += read();")
    .replace(
        "    closed += emitted;\n    if (anchor.hasAttribute('stop')) break;",
        "    closed += emitted;\n    count += read();\n"
        "    if (anchor.hasAttribute('stop')) break;",
    )
    .replace(
        "  closed += emitted;\n  emitted += closed;",
        "  count += read();\n  closed += emitted;\n  emitted += closed;",
    )
    .replace("return count + emitted + closed;", "return count + read() + read();")
)
SIBLING_WRITER_SOURCE = SIBLING_CAPTURE_SOURCE.replace(
    "  const read = () => emitted;",
    "  const read = () => { emitted += closed; return emitted; };",
)
SIBLING_ORDERED_WRITER_SOURCE = (
    SIBLING_ORDERED_CAPTURE_SOURCE.replace(
        "const read = () => emitted + closed;",
        "const read = () => { const before = emitted; closed += emitted; "
        "emitted += closed; return before; };",
    )
    .replace("  count += read();\n  for (const node of values)", "  for (const node of values)")
    .replace("closed === 21", "closed === 74")
)
SIBLING_BRANCH_WRITER_SOURCE = (
    SIBLING_ORDERED_WRITER_SOURCE.replace("let closed = 1;", "let closed = 0;")
    .replace("closed === 74", "closed === 458")
    .replace(
        "const read = () => { const before = emitted; closed += emitted; "
        "emitted += closed; return before; };",
        """const read = () => {
    const before = emitted;
    let rounds = 0;
    while (rounds < 2) {
      closed += emitted;
      emitted += closed;
      rounds++;
    }
    if (closed > 1) emitted++;
    else closed++;
    return rounds + before;
  };""",
    )
)
SIBLING_LOOP_WRITER_SOURCE = SIBLING_BRANCH_WRITER_SOURCE.replace(
    "    }\n    if (closed > 1) emitted++;\n    else closed++;",
    "      if (closed > 1) emitted++;\n      else closed++;\n    }",
).replace("closed === 458", "closed === 541")
SIBLING_BRANCH_RESULT_SOURCE = SIBLING_BRANCH_WRITER_SOURCE.replace(
    "    if (closed > 1) emitted++;\n    else closed++;\n    return rounds + before;",
    """    let result = 0;
    if (closed > 1) {
      emitted++;
      result = before;
    } else {
      closed++;
      result = rounds + before;
    }
    return result;""",
)
SIBLING_ARGUMENT_WRITER_SOURCE = (
    SIBLING_LOOP_WRITER_SOURCE.replace("const read = () => {", "const read = (amount) => {")
    .replace("const before = emitted;", "const before = emitted + amount;")
    .replace("read()", "read(closed)")
)
SIBLING_ARGUMENT_SNAPSHOTS_SOURCE = (
    SIBLING_ARGUMENT_WRITER_SOURCE.replace(
        "const read = (amount) => {", "const read = (amount, prior) => {"
    )
    .replace(
        "return rounds + before;",
        "return rounds + before + amount + prior + prior + emitted + closed;",
    )
    .replace("read(closed)", "read(emitted, emitted += closed)")
    .replace("closed === 541", "closed === 1829")
)
SIBLING_ARGUMENT_CONTROL_SOURCE = (
    SIBLING_ARGUMENT_WRITER_SOURCE.replace(
        "const read = (amount) => {", "const read = (amount, limit) => {"
    )
    .replace("while (rounds < 2)", "while (rounds < limit)")
    .replace(
        "return rounds + before;",
        "if (amount > 1) return rounds + before + amount;\n    return rounds + before;",
    )
    .replace("read(closed)", "read(closed, 2)")
)
SIBLING_BREAK_WRITER_SOURCE = SIBLING_LOOP_WRITER_SOURCE.replace(
    "      rounds++;", "      rounds++;\n      if (rounds === 1) break;"
)
SIBLING_BREAK_FINITE_SOURCE = SIBLING_BREAK_WRITER_SOURCE.replace(
    "let closed = 0;", "let closed = 1;"
)
SIBLING_BREAK_ARGUMENT_SOURCE = (
    SIBLING_ARGUMENT_SNAPSHOTS_SOURCE.replace("let closed = 0;", "let closed = 1;")
    .replace("read(emitted, emitted += closed)", "read(closed, closed += emitted)")
    .replace("      rounds++;", "      rounds++;\n      if (rounds === 1) break;", 1)
)
SIBLING_BREAK_CONTROL_SOURCE = SIBLING_ARGUMENT_CONTROL_SOURCE.replace(
    "let closed = 0;", "let closed = 1;"
).replace("      rounds++;", "      rounds++;\n      if (rounds === amount) break;", 1)

SIBLING_NESTED_WRITER_SOURCE = SIBLING_BREAK_FINITE_SOURCE.replace(
    "  const read = () => {",
    "  const advance = () => { const before = emitted; closed += emitted;\n"
    "    emitted += closed; return before; };\n  const read = () => {",
    1,
).replace(
    "      closed += emitted;\n      emitted += closed;",
    "      rounds += advance();",
    1,
)
SIBLING_SHARED_WRITER_SOURCE = SIBLING_NESTED_WRITER_SOURCE.replace(
    "  const read = () => {",
    "  const forward = () => advance();\n  const relay = () => advance() + 1;\n"
    "  const read = () => {",
    1,
).replace(
    "      rounds += advance();",
    "      if (emitted === 0) rounds += forward();\n      else rounds += relay();",
    1,
)
SIBLING_NESTED_SNAPSHOTS_SOURCE = (
    SIBLING_NESTED_WRITER_SOURCE.replace(
        "const advance = () => { const before = emitted;",
        "const advance = (amount, prior) => { const before = emitted;",
        1,
    )
    .replace(
        "emitted += closed; return before;",
        "emitted += closed; return before + amount + prior + prior + emitted + closed;",
        1,
    )
    .replace("    let rounds = 0;", "    let observed = 0;\n    let rounds = 0;", 1)
    .replace(
        "      rounds += advance();", "      observed += advance(closed, closed += emitted);", 1
    )
    .replace("    return rounds + before;", "    return rounds + before + observed;", 1)
)
SIBLING_CALLABLE_ARGUMENT_SOURCE = SIBLING_NESTED_WRITER_SOURCE.replace(
    "  const read = () => {",
    "  const relay = (writer) => writer();\n  const read = () => {",
    1,
).replace("rounds += advance();", "rounds += relay(advance);", 1)
SIBLING_CALLABLE_SNAPSHOTS_SOURCE = SIBLING_NESTED_SNAPSHOTS_SOURCE.replace(
    "  const read = () => {",
    "  const relay = (amount, writer, prior) => writer(amount, prior);\n  const read = () => {",
    1,
).replace("advance(closed, closed += emitted)", "relay(closed, advance, closed += emitted)", 1)
SIBLING_CALLABLE_SHARED_SOURCE = (
    SIBLING_SHARED_WRITER_SOURCE.replace(
        "const forward = () => advance();", "const forward = (writer) => writer();"
    )
    .replace("const relay = () => advance() + 1;", "const relay = (writer) => forward(writer) + 1;")
    .replace("rounds += forward();", "rounds += forward(advance);")
    .replace("rounds += relay();", "rounds += relay(advance);")
)
SIBLING_RETURNED_CALLABLE_SOURCE = SIBLING_CALLABLE_ARGUMENT_SOURCE.replace(
    "  const relay = (writer) => writer();",
    "  const identity = (writer) => writer;\n" "  const relay = (writer) => identity(writer)();",
)
SIBLING_RETURNED_SNAPSHOTS_SOURCE = SIBLING_CALLABLE_SNAPSHOTS_SOURCE.replace(
    "  const relay = (amount, writer, prior) => writer(amount, prior);",
    "  const identity = (writer) => { closed += emitted; return writer; };\n"
    "  const relay = (amount, writer, prior) => identity(writer)(amount, prior);",
)
SIBLING_RETURNED_FORWARDED_SOURCE = (
    SIBLING_CALLABLE_SHARED_SOURCE.replace(
        "  const forward = (writer) => writer();",
        "  const identity = (writer) => writer;\n"
        "  const forward = (writer) => identity(writer);",
    )
    .replace("forward(writer) + 1", "forward(writer)() + 1")
    .replace("rounds += forward(advance);", "rounds += forward(advance)();")
)
SIBLING_RETURNED_DIFFERENT_TARGETS_SOURCE = (
    SIBLING_RETURNED_CALLABLE_SOURCE.replace(
        "  const identity = (writer) => writer;",
        "  const other = () => { const before = emitted; closed += emitted;\n"
        "    emitted += closed; return before + 1; };\n"
        "  const identity = (writer) => writer;",
    )
    .replace(
        "  const relay = (writer) => identity(writer)();",
        "  const relay = (writer) => identity(writer)();\n"
        "  const alternate = () => identity(other)();",
    )
    .replace(
        "      rounds += relay(advance);",
        "      if (emitted === 0) rounds += relay(advance);\n" "      else rounds += alternate();",
    )
)
SIBLING_RETURNED_DIFFERENT_SNAPSHOTS_SOURCE = SIBLING_RETURNED_SNAPSHOTS_SOURCE.replace(
    "  const identity = (writer) =>",
    "  const other = (amount, prior) => { const before = closed; emitted += closed;\n"
    "    closed += emitted; return before + amount + prior + prior + emitted + closed + 1; };\n"
    "  const identity = (writer) =>",
    1,
).replace(
    "      observed += relay(closed, advance, closed += emitted);",
    "      if (emitted === 0) observed += relay(closed, advance, closed += emitted);\n"
    "      else observed += relay(closed, other, closed += emitted);",
    1,
)
SIBLING_RETURNED_DIFFERENT_FORWARDED_SOURCE = SIBLING_RETURNED_FORWARDED_SOURCE.replace(
    "  const identity = (writer) => writer;",
    "  const other = () => { const before = emitted; closed += emitted;\n"
    "    emitted += closed; return before + 1; };\n"
    "  const identity = (writer) => writer;",
    1,
).replace("rounds += relay(advance);", "rounds += relay(other);", 1)
SIBLING_RETURNED_BRANCH_JOIN_SOURCE = SIBLING_RETURNED_DIFFERENT_TARGETS_SOURCE.replace(
    "  const identity = (writer) => writer;",
    "  const identity = (writer) => {\n"
    "    let selected = writer;\n"
    "    if (emitted > 0) selected = other;\n"
    "    return selected;\n"
    "  };",
    1,
)
SIBLING_RETURNED_BRANCH_SNAPSHOTS_SOURCE = SIBLING_RETURNED_DIFFERENT_SNAPSHOTS_SOURCE.replace(
    "  const identity = (writer) => { closed += emitted; return writer; };",
    "  const identity = (writer) => {\n"
    "    let selected = writer;\n"
    "    if (emitted > 0) selected = other;\n"
    "    closed += emitted;\n"
    "    return selected;\n"
    "  };",
    1,
).replace(
    "      if (emitted === 0) observed += relay(closed, advance, closed += emitted);\n"
    "      else observed += relay(closed, other, closed += emitted);",
    "      observed += relay(closed, advance, closed += emitted);",
    1,
)
SIBLING_RETURNED_BRANCH_FORWARDED_SOURCE = (
    SIBLING_RETURNED_BRANCH_JOIN_SOURCE.replace(
        "emitted += closed; return before + 1;",
        "emitted += closed; closed += emitted; return before + 1;",
        1,
    )
    .replace(
        "  const relay = (writer) => identity(writer)();",
        "  const forward = (writer) => identity(writer);\n"
        "  const relay = (writer) => forward(writer)();",
        1,
    )
    .replace(
        "const alternate = () => identity(other)();", "const alternate = () => relay(advance);"
    )
)
SIBLING_RETURNED_LOOP_JOIN_SOURCE = SIBLING_RETURNED_BRANCH_JOIN_SOURCE.replace(
    "    if (emitted > 0) selected = other;",
    "    let rounds = 0;\n"
    "    while (rounds < 2) {\n"
    "      if (emitted > 0) selected = other;\n"
    "      rounds++;\n"
    "    }",
    1,
)

# The original zero-state source cannot progress after its early break. Preserve
# its native admission without executing it; the finite counterpart runs below.
COMPILE_ONLY = {"entry-captured-sibling-loop-break-writer": SIBLING_BREAK_WRITER_SOURCE}

# Results for normal, stopping, and already-yielded DOM states, followed by
# whether each invocation resets its iterator state and the close-hook value.
POSITIVES = (
    ("normal", source(False), False, ("1", "1", "0"), False, "yes"),
    ("effect-only-break", source(True), True, ("true", "true", "false"), False, "yes"),
    (
        "counted-break-exit",
        SOURCE.replace("@BREAK@", "if (anchor.hasAttribute('stop')) break;"),
        True,
        ("1", "1", "0"),
        False,
        "yes",
    ),
    (
        # The break must carry the incoming counter, not the continuation's update.
        "counted-after-break",
        SOURCE.replace("let count = 0;", "let count = 7;")
        .replace("    count++;\n", "")
        .replace("@BREAK@", "if (anchor.hasAttribute('stop')) break;\n    count += 3;"),
        True,
        ("10", "7", "7"),
        False,
        "yes",
    ),
    (
        "counted-two-live",
        SOURCE.replace("let count = 0;", "let count = 0;\n  let extra = 7;")
        .replace("@BREAK@", "if (anchor.hasAttribute('stop')) break;\n    extra += 3;")
        .replace("return count;", "return count + extra;"),
        True,
        ("11", "8", "7"),
        False,
        "yes",
    ),
    (
        "two-projected-break-exits",
        SOURCE.replace("let count = 0;", "let count = 0;\n  let extra = 7;")
        .replace("count++;", "count++;\n    extra += 3;")
        .replace("@BREAK@", "if (anchor.hasAttribute('stop')) break;")
        .replace("return count;", "return count + extra;"),
        True,
        ("11", "11", "7"),
        False,
        "yes",
    ),
    (
        # Distinct updates and crossed result order expose reused or swapped exits.
        "three-projected-ordered-exits",
        SOURCE.replace("let count = 0;", "let count = 2;\n  let extra = 7;\n  let third = 40;")
        .replace("count++;", "count++;\n    extra += 3;\n    third += 5;")
        .replace("@BREAK@", "if (anchor.hasAttribute('stop')) break;")
        .replace("return count;", "return third + count + count + extra + extra + extra;"),
        True,
        ("81", "81", "65"),
        False,
        "yes",
    ),
    ("mutable-receiver-counter", RECEIVER_SOURCE, True, ("1", "1", "1"), True, "yes"),
    (
        # The close hook sees both fields after next(), in source update order.
        "mutable-receiver-return-state",
        RECEIVER_SOURCE.replace("emitted: 0,", "emitted: 0,\n    closed: 1,")
        .replace("this.emitted++;", "this.emitted++;\n      this.closed += this.emitted;")
        .replace(
            "anchor.setAttribute('data-closed', 'yes');",
            "anchor.setAttribute('data-closed', this.closed === 2);\n      this.closed = 0;",
        ),
        True,
        ("1", "1", "1"),
        True,
        "true",
    ),
    ("mutable-captured-counter", CAPTURE_SOURCE, True, ("1", "1", "1"), True, "yes"),
    (
        # Both closures share both cells; close observes next's ordered updates.
        "mutable-captured-return-state",
        CAPTURE_SOURCE.replace("let emitted = 0;", "let emitted = 0;\n  let closed = 1;")
        .replace("emitted++;", "emitted++;\n      closed += emitted;")
        .replace(
            "anchor.setAttribute('data-closed', 'yes');",
            "closed += emitted;\n      anchor.setAttribute('data-closed', closed === 3);\n"
            "      emitted = 0;",
        ),
        True,
        ("1", "1", "1"),
        True,
        "true",
    ),
    (
        "conditional-captured-store",
        CONDITIONAL_CAPTURE_SOURCE,
        True,
        ("1", "1", "1"),
        True,
        "yes",
    ),
    (
        "conditional-state-store",
        CONDITIONAL_RECEIVER_SOURCE,
        True,
        ("1", "1", "1"),
        True,
        "yes",
    ),
    (
        # Exhaustion observes the nested else join; close checks each update order.
        "conditional-captured-ordered-close",
        BRANCH_CAPTURE_SOURCE,
        True,
        ("1", "1", "1"),
        True,
        "true",
    ),
    (
        "conditional-receiver-ordered-close",
        BRANCH_RECEIVER_SOURCE,
        True,
        ("1", "1", "1"),
        True,
        "true",
    ),
    ("loop-captured-store", LOOP_CAPTURE_SOURCE, True, ("1", "1", "1"), True, "yes"),
    (
        # First next loops twice, exhaustion loops zero times; close loops twice.
        # The original rounds result survives alongside both carried state values.
        "loop-receiver-ordered-close",
        LOOP_RECEIVER_SOURCE,
        True,
        ("1", "1", "1"),
        True,
        "true",
    ),
    (
        "loop-captured-ordered-close",
        LOOP_ORDERED_CAPTURE_SOURCE,
        True,
        ("1", "1", "1"),
        True,
        "true",
    ),
    ("loop-captured-break", LOOP_BREAK_CAPTURE_SOURCE, True, ("1", "1", "1"), True, "yes"),
    (
        # Break carries both preceding updates and the ordinary rounds result.
        # Exhaustion takes zero trips; close sees the latest state and skips its suffix.
        "loop-receiver-break-ordered-close",
        LOOP_BREAK_RECEIVER_SOURCE,
        True,
        ("1", "1", "1"),
        True,
        "true",
    ),
    (
        "loop-captured-break-ordered-close",
        LOOP_BREAK_ORDERED_CAPTURE_SOURCE,
        True,
        ("1", "1", "1"),
        True,
        "true",
    ),
    (
        "external-captured-read",
        CAPTURE_SOURCE.replace("return count;", "return count + emitted;"),
        True,
        ("3", "2", "3"),
        True,
        "yes",
    ),
    (
        "preloop-captured-read",
        CAPTURE_SOURCE.replace(
            "  for (const node of values) {",
            "  anchor.setAttribute('data-extra', emitted === 0);\n  for (const node of values) {",
        ),
        True,
        ("1", "1", "1"),
        True,
        "yes",
    ),
    (
        "late-captured-store",
        CAPTURE_SOURCE.replace("return count;", "emitted = 0;\n  return count;"),
        True,
        ("1", "1", "1"),
        True,
        "yes",
    ),
    (
        "preloop-captured-store",
        CAPTURE_SOURCE.replace(
            "  for (const node of values) {", "  emitted = 0;\n  for (const node of values) {"
        ),
        True,
        ("1", "1", "1"),
        True,
        "yes",
    ),
    (
        "loop-external-captured-read",
        LOOP_ORDERED_CAPTURE_SOURCE.replace("return count;", "return count + emitted;"),
        True,
        ("3", "5", "3"),
        True,
        "true",
    ),
    (
        # The saved next boundary observes the state after the close-hook break.
        "loop-break-external-captured-read",
        LOOP_BREAK_ORDERED_CAPTURE_SOURCE.replace("return count;", "return count + emitted;"),
        True,
        ("3", "3", "3"),
        True,
        "true",
    ),
    (
        # Entry loop/branch updates feed next and close, then both final cells
        # feed ordered post-loop writes. Distinct sums expose stale or swapped state.
        "entry-captured-ordered-state",
        ENTRY_CAPTURE_SOURCE,
        True,
        ("57", "68", "57"),
        True,
        "true",
    ),
    (
        "entry-captured-sibling-reader",
        SIBLING_CAPTURE_SOURCE,
        True,
        ("57", "68", "57"),
        True,
        "true",
    ),
    (
        # Every call observes the current cells: before iteration, after body
        # writes, after close, and twice after the final ordered writes.
        "entry-captured-sibling-ordered-reader",
        SIBLING_ORDERED_CAPTURE_SOURCE,
        True,
        ("146", "173", "146"),
        True,
        "true",
    ),
    (
        "extra-captured-closure",
        CAPTURE_SOURCE.replace(
            "  for (const node of values) {",
            "  anchor.setAttribute('data-extra', (() => emitted)() === 0);\n"
            "  for (const node of values) {",
        ),
        True,
        ("1", "1", "1"),
        True,
        "yes",
    ),
    (
        "entry-captured-sibling-writer",
        SIBLING_WRITER_SOURCE,
        True,
        ("77", "95", "77"),
        True,
        "true",
    ),
    (
        # Each result precedes the next call's writes; close sees both cells
        # after the body calls, and both final calls observe the close update.
        "entry-captured-sibling-ordered-writer",
        SIBLING_ORDERED_WRITER_SOURCE,
        True,
        ("1582", "1645", "1582"),
        True,
        "true",
    ),
    (
        "entry-captured-sibling-loop-writer",
        SIBLING_LOOP_WRITER_SOURCE,
        True,
        ("61478", "68384", "61478"),
        True,
        "true",
    ),
    (
        # The first call takes else; later calls take if. Both carry their
        # latest cells into the one shared custom-iterator continuation.
        "entry-captured-sibling-preloop-branch-writer",
        SIBLING_BRANCH_WRITER_SOURCE,
        True,
        ("54163", "57856", "54163"),
        True,
        "true",
    ),
    (
        # Different ordinary returns retain the earlier snapshot separately
        # from the current cells, including calls after close and repeated calls.
        "entry-captured-sibling-branch-result-writer",
        SIBLING_BRANCH_RESULT_SOURCE,
        True,
        ("54153", "57846", "54153"),
        True,
        "true",
    ),
    (
        "entry-captured-sibling-branch-return-writer",
        SIBLING_BRANCH_RESULT_SOURCE.replace("    let result = 0;\n", "")
        .replace("result = before;", "return before;")
        .replace("result = rounds + before;", "return rounds + before;")
        .replace("\n    return result;", ""),
        True,
        ("54153", "57846", "54153"),
        True,
        "true",
    ),
    (
        "entry-captured-sibling-argument-writer",
        SIBLING_ARGUMENT_WRITER_SOURCE,
        True,
        ("99520", "111033", "99520"),
        True,
        "true",
    ),
    (
        # The first argument snapshots emitted before the second argument writes
        # it; both survive the helper's writes beside its current captured cells.
        "entry-captured-sibling-argument-snapshots",
        SIBLING_ARGUMENT_SNAPSHOTS_SOURCE,
        True,
        ("10248857", "13305421", "10248857"),
        True,
        "true",
    ),
    (
        # Explicit arguments feed the loop limit and both ordinary return arms.
        "entry-captured-sibling-argument-control",
        SIBLING_ARGUMENT_CONTROL_SOURCE,
        True,
        ("137562", "153682", "137562"),
        True,
        "true",
    ),
    (
        # The exact saved source breaks in both the helper and next method.
        "entry-captured-sibling-loop-break-finite-writer",
        SIBLING_BREAK_FINITE_SOURCE,
        True,
        ("1258", "1651", "1258"),
        True,
        "false",
    ),
    (
        # The second argument changes the first argument's cell before the
        # helper writes both cells; break skips the suffix, retaining all results.
        "entry-captured-sibling-loop-break-argument-snapshots",
        SIBLING_BREAK_ARGUMENT_SOURCE,
        True,
        ("50788", "50856", "50788"),
        True,
        "false",
    ),
    (
        # The first call breaks on its explicit argument; later calls reach the
        # suffix and ordinary return branch with their latest shared state.
        "entry-captured-sibling-loop-break-argument-control",
        SIBLING_BREAK_CONTROL_SOURCE,
        True,
        ("127960", "139790", "127960"),
        True,
        "false",
    ),
    (
        "entry-captured-sibling-nested-call-writer",
        SIBLING_NESTED_WRITER_SOURCE,
        True,
        ("2724", "3598", "2724"),
        True,
        "false",
    ),
    (
        # Both callable-only wrappers reach the same writer; both branches and
        # distinct ordinary results survive their three-level call paths.
        "entry-captured-sibling-shared-call-writer",
        SIBLING_SHARED_WRITER_SOURCE,
        True,
        ("2729", "3603", "2729"),
        True,
        "false",
    ),
    (
        # A later inner argument changes the first argument's captured cell.
        # Keep both snapshots, the ordinary result and ordered current state.
        "entry-captured-sibling-nested-call-snapshots",
        SIBLING_NESTED_SNAPSHOTS_SOURCE,
        True,
        ("42651", "51971", "42651"),
        True,
        "false",
    ),
    (
        "entry-captured-sibling-callable-argument-writer",
        SIBLING_CALLABLE_ARGUMENT_SOURCE,
        True,
        ("2724", "3598", "2724"),
        True,
        "false",
    ),
    (
        # The callable separates two scalar arguments; the later argument writes
        # the first one's cell before the forwarded call reads both snapshots.
        "entry-captured-sibling-callable-argument-snapshots",
        SIBLING_CALLABLE_SNAPSHOTS_SOURCE,
        True,
        ("42651", "51971", "42651"),
        True,
        "false",
    ),
    (
        # Both branches pass the shared writer, one through another parameter.
        "entry-captured-sibling-callable-argument-shared",
        SIBLING_CALLABLE_SHARED_SOURCE,
        True,
        ("2729", "3603", "2729"),
        True,
        "false",
    ),
    (
        "entry-captured-sibling-returned-callable-writer",
        SIBLING_RETURNED_CALLABLE_SOURCE,
        True,
        ("2724", "3598", "2724"),
        True,
        "false",
    ),
    (
        # The returned writer receives earlier scalar snapshots after identity
        # changes the shared state that the writer must read at call time.
        "entry-captured-sibling-returned-callable-snapshots",
        SIBLING_RETURNED_SNAPSHOTS_SOURCE,
        True,
        ("109776", "127479", "109776"),
        True,
        "false",
    ),
    (
        # Both branches invoke the writer returned through two helper results.
        "entry-captured-sibling-returned-callable-forwarded",
        SIBLING_RETURNED_FORWARDED_SOURCE,
        True,
        ("2729", "3603", "2729"),
        True,
        "false",
    ),
    (
        "entry-captured-sibling-returned-callable-different-targets",
        SIBLING_RETURNED_DIFFERENT_TARGETS_SOURCE,
        True,
        ("2729", "3603", "2729"),
        True,
        "false",
    ),
    (
        # Distinct returned writers retain the earlier scalar arguments beside
        # current cells, with opposite write order and different ordinary results.
        "entry-captured-sibling-returned-different-snapshots",
        SIBLING_RETURNED_DIFFERENT_SNAPSHOTS_SOURCE,
        True,
        ("166142", "192687", "166142"),
        True,
        "false",
    ),
    (
        # Each invocation selects its own writer through two returned helpers.
        "entry-captured-sibling-returned-different-forwarded",
        SIBLING_RETURNED_DIFFERENT_FORWARDED_SOURCE,
        True,
        ("2734", "3608", "2734"),
        True,
        "false",
    ),
    (
        "entry-captured-sibling-returned-callable-branch-join",
        SIBLING_RETURNED_BRANCH_JOIN_SOURCE,
        True,
        ("2729", "3603", "2729"),
        True,
        "false",
    ),
    (
        "entry-captured-sibling-returned-branch-snapshots",
        SIBLING_RETURNED_BRANCH_SNAPSHOTS_SOURCE,
        True,
        ("166142", "192687", "166142"),
        True,
        "false",
    ),
    (
        "entry-captured-sibling-returned-branch-forwarded",
        SIBLING_RETURNED_BRANCH_FORWARDED_SOURCE,
        True,
        ("6305", "7671", "6305"),
        True,
        "false",
    ),
)


def oracles(args):
    # These receivers record the same source calls. The native clients below
    # separately check those calls against public DOM and document ownership.
    script, observations = "", []
    for index, (label, text, breaking, results, resetting, closed) in enumerate(POSITIVES):
        function = f"customElementsCase{index}"
        script += text.replace("function customElements(", f"function {function}(")
        normal, stopped, exhausted = results
        for state, result in (
            ("normal", normal),
            ("stop", stopped),
            ("already-yielded", exhausted),
            ("stop-without-advance", stopped),
        ):
            name = f"customObservation{len(observations)}"
            setup = "saved.advance = '';"
            if state == "stop-without-advance":
                setup = "saved.stop = '';"
            elif state == "stop":
                setup += "saved.stop = '';"
            elif state == "already-yielded":
                setup += "saved['data-yielded'] = 'yes';"
            script += f"""
var {name} = (function() {{
  const saved = {{}};
  {setup}
  let writes = '';
  const anchor = {{
    hasAttribute(name) {{ return name in saved; }},
    setAttribute(name, value) {{
      saved[name] = '' + value;
      writes += name + '=' + saved[name] + ';';
    }}
  }};
  const result = {function}(anchor);
  return result + ':' + writes;
}})();
"""
            expected = result + ":data-next=true;data-yielded=yes;"
            if state != "already-yielded" or resetting:
                expected = result + ":data-next=false;data-yielded=yes;data-visited=yes;"
                expected += (
                    f"data-closed={closed};"
                    if breaking and state.startswith("stop")
                    else "data-next=true;data-yielded=yes;"
                )
            if label in ("preloop-captured-read", "extra-captured-closure"):
                expected = expected.replace(":", ":data-extra=true;", 1)
            observations.append((name, expected))
    node = args.work / "custom-iteration-node.js"
    node.write_text(script + "".join(f"console.log({name});\n" for name, _ in observations))
    expected = [value for _, value in observations]
    actual = dom.run([args.node, str(node)]).stdout.splitlines()
    if actual != expected:
        raise RuntimeError(f"Node custom iterator effects differ: {actual!r}")
    vm = args.work / "custom-iteration-vm.js"
    vm.write_text(script)
    actual = dom.run([args.reference, str(vm)]).stdout
    expected_vm = "".join(f'{name}="{quote(value)}"\n' for name, value in sorted(observations))
    if actual != expected_vm:
        raise RuntimeError(f"VM custom iterator effects differ: {actual!r}")
    return 2 * len(observations)


CHECKS = r"""
        (void)pressed;
        const auto exercise = [&](ctbrowser::document & target, node_id id, auto && call) {
            auto & names = target.atoms();
            const auto next = names.intern("data-next"), yielded = names.intern("data-yielded");
            const auto visited = names.intern("data-visited"), closed = names.intern("data-closed");
            const auto stop = names.intern("stop"), advance = names.intern("advance");
            target.log_writes(true);
            const auto clear = [&] {
                for (const auto name : {next, yielded, visited, closed, stop, advance}) {
                    assert(target.remove_attribute(id, name));
                }
                (void)target.take_writes();
            };
            const auto check_writes = [&](std::initializer_list<atom> expected) {
                const auto writes = target.take_writes();
                assert(writes.size() == expected.size());
                std::size_t at = 0;
                for (const auto name : expected) {
                    assert(writes[at].node == id && writes[at].name == name && !writes[at].text);
                    ++at;
                }
            };
            // No advance requires a break: the exact conditional source can repeat forever.
            for (const unsigned mode : {0u, 1u, 2u}) {
                const bool stopping = mode != 0;
                clear();
                if (mode != 2) { assert(target.set_attribute(id, advance, "")); }
                if (stopping) { assert(target.set_attribute(id, stop, "")); }
                (void)target.take_writes();
                assert(@FIRST_RESULT@);
                assert(target.read().attribute_value(id, yielded) == "yes");
                assert(target.read().attribute_value(id, visited) == "yes");
                if (@BREAKING@ && stopping) {
                    check_writes({next, yielded, visited, closed});
                    assert(target.read().attribute_value(id, next) == "false");
                    assert(target.read().attribute_value(id, closed) == "@CLOSED@");
                } else {
                    check_writes({next, yielded, visited, next, yielded});
                    assert(target.read().attribute_value(id, next) == "true");
                    assert(!target.read().has_attribute(id, closed));
                }
                assert(target.remove_attribute(id, visited));
                assert(target.remove_attribute(id, closed));
                (void)target.take_writes();
                // DOM-backed state stays exhausted. Receiver fields belong to
                // the new iterator and reset for every invocation.
                assert(@SECOND_RESULT@);
                if (@RESETTING@) {
                    assert(target.read().attribute_value(id, visited) == "yes");
                    if (@BREAKING@ && stopping) {
                        check_writes({next, yielded, visited, closed});
                        assert(target.read().attribute_value(id, next) == "false");
                        assert(target.read().attribute_value(id, closed) == "@CLOSED@");
                    } else {
                        check_writes({next, yielded, visited, next, yielded});
                        assert(target.read().attribute_value(id, next) == "true");
                        assert(!target.read().has_attribute(id, closed));
                    }
                } else {
                    check_writes({next, yielded});
                    assert(target.read().attribute_value(id, next) == "true");
                    assert(!target.read().has_attribute(id, visited));
                    assert(!target.read().has_attribute(id, closed));
                }
            }
            clear();
        };
        exercise(doc, button, [&] { return @ENTRY@(alias); });
        assert(doc.remove_child(button));
        assert(!doc.read().parent(button));
        exercise(doc, button, [&] { return @ENTRY@(alias); });
        exercise(foreign_doc, other_button, [&] { return @ENTRY@(foreign); });
        assert(doc.take_writes().empty());
        const auto invalid_text = doc.create_text("invalid input");
        for (const element_ref invalid : {
                 element_ref{}, element_ref{&doc, {}}, element_ref{&doc, invalid_text},
                 element_ref{&doc, {button.slot, button.generation + 2}}}) {
            bool rejected = false;
            try { (void)@ENTRY@(invalid); }
            catch (const std::exception &) { rejected = true; }
            assert(rejected && doc.take_writes().empty());
        }
"""

OWNED_CHECKS = r"""
        @ENTRY@_session session;
        auto & owned = session.document();
        const auto owned_node = owned.create_element(owned.atoms().intern("button"));
        const element_ref input{&owned, owned_node};
        exercise(owned, owned_node, [&] { return session.invoke(input); });
        owned.set_document_element(owned_node);
        exercise(owned, owned_node, [&] { return session.invoke(input); });
        bool foreign_rejected = false;
        try { (void)session.invoke(foreign); }
        catch (const std::invalid_argument &) { foreign_rejected = true; }
        assert(foreign_rejected && owned.take_writes().empty());
"""


def refusals():
    text = source(True)
    loop = "  for (const node of values) {"
    visited = "    node.setAttribute('data-visited', 'yes');"
    variants = {
        "escaping-holder": text.replace(loop, "  external(values);\n" + loop),
        "retained-holder": text.replace(loop, "  anchor.saved=values;\n" + loop),
        "mutated-next": text.replace(loop, "  values.next=anchor;\n" + loop),
        "mutated-return": text.replace(loop, "  values.return=anchor;\n" + loop),
        "mutated-iterator": text.replace(loop, "  values[Symbol.iterator]=anchor;\n" + loop),
        "nonidentity-iterator": text.replace("return this;", "return anchor;"),
        "lexical-this-iterator": text.replace(
            "[Symbol.iterator]() { return this; }", "[Symbol.iterator]: () => this"
        ),
        "fresh-iterator-factory": text.replace(
            "return this;", "return {next() {return {done:true,value:anchor};}};"
        ),
        "primitive-next-result": text.replace("return {done: done, value: anchor};", "return 1;"),
        "primitive-return-result": text.replace("return {};", "return 1;"),
        "missing-done": text.replace(
            "return {done: done, value: anchor};", "return {value:anchor};"
        ),
        "missing-value": text.replace("return {done: done, value: anchor};", "return {done:done};"),
        "invalid-next": text.replace("next() {\n      const done", "other() {\n      const done"),
        "replaced-symbol": text.replace("  const values", "  Symbol=anchor;\n  const values"),
        "replaced-symbol-iterator": text.replace(
            "  const values", "  Symbol.iterator=anchor;\n  const values"
        ),
        "unknown-next-effect": text.replace(
            "      const done", "      external(anchor); const done"
        ),
        "escaping-value": text.replace(visited, "    anchor.saved=node;"),
        # The source compiler does not close iterators on body return or throw.
        # Keep both refused until that source completion boundary is proved.
        "body-return": text.replace(visited, "    return anchor.hasAttribute('data-visited');"),
        "body-throw": text.replace(visited, "    throw 1;"),
    }
    for helper in SNAPSHOT_INTRINSICS[4:]:
        variants["replaced-" + helper] = text.replace(
            "  const values", f"  {helper}=anchor;\n  const values"
        )
    variants.update(
        {
            "lexical-this-next": RECEIVER_SOURCE.replace("next() {", "next: () => {"),
            "lexical-this-return": RECEIVER_SOURCE.replace("return() {", "return: () => {").replace(
                "anchor.setAttribute('data-closed', 'yes');",
                "anchor.setAttribute('data-closed', this.emitted === 1);",
            ),
            "receiver-alias-escape": RECEIVER_SOURCE.replace(
                "      const done", "      const alias = this; external(alias);\n      const done"
            ),
            "retained-receiver": RECEIVER_SOURCE.replace(
                "      const done", "      anchor.saved = this;\n      const done"
            ),
            "receiver-as-value": RECEIVER_SOURCE.replace("value: anchor", "value: this"),
            "dynamic-state-read": RECEIVER_SOURCE.replace(
                "this.emitted > 0", "this[anchor.getAttribute('data-key')] > 0"
            ),
            "dynamic-state-store": RECEIVER_SOURCE.replace(
                "this.emitted++;", "this[anchor.getAttribute('data-key')]++;"
            ),
            "nonnumber-state-initializer": RECEIVER_SOURCE.replace("emitted: 0", "emitted: anchor"),
            "nonnumber-state-store": RECEIVER_SOURCE.replace(
                "this.emitted++;", "this.emitted = anchor;"
            ),
            "undeclared-state-store": RECEIVER_SOURCE.replace(
                "this.emitted++;", "this.emitted++;\n      this.extra = 1;"
            ),
            "external-state-read": RECEIVER_SOURCE.replace(
                "return count;", "return count + values.emitted;"
            ),
            "late-state-store": RECEIVER_SOURCE.replace(
                "return count;", "values.emitted = 0;\n  return count;"
            ),
            "preloop-state-store": RECEIVER_SOURCE.replace(loop, "  values.emitted = 0;\n" + loop),
        }
    )
    variants.update(
        {
            "escaping-captured-cell": CAPTURE_SOURCE.replace(
                loop, "  external(() => emitted);\n" + loop
            ),
            "retained-captured-cell": CAPTURE_SOURCE.replace(
                loop, "  anchor.saved = () => emitted;\n" + loop
            ),
            "nonnumber-captured-initializer": CAPTURE_SOURCE.replace(
                "let emitted = 0;", "let emitted = anchor;"
            ),
            "nonnumber-captured-store": CAPTURE_SOURCE.replace("emitted++;", "emitted = anchor;"),
            "conditional-nonnumber-captured-store": CONDITIONAL_CAPTURE_SOURCE.replace(
                "emitted++;", "emitted++; else emitted = anchor;"
            ),
            "conditional-nonnumber-state-store": CONDITIONAL_RECEIVER_SOURCE.replace(
                "this.emitted++;", "this.emitted++; else this.emitted = anchor;"
            ),
            "conditional-unknown-effect": CONDITIONAL_CAPTURE_SOURCE.replace(
                "emitted++;", "{ emitted++; external(anchor); }"
            ),
            "conditional-receiver-escape": CONDITIONAL_RECEIVER_SOURCE.replace(
                "this.emitted++;", "{ this.emitted++; anchor.saved = this; }"
            ),
            "conditional-nested-capture": CONDITIONAL_CAPTURE_SOURCE.replace(
                "emitted++;", "{ emitted++; const change = () => { emitted++; }; change(); }"
            ),
            "loop-nonnumber-captured-store": LOOP_ORDERED_CAPTURE_SOURCE.replace(
                "closed += emitted;", "closed = anchor;", 1
            ),
            "loop-nonnumber-state-store": LOOP_RECEIVER_SOURCE.replace(
                "this.closed += this.emitted;", "this.closed = anchor;", 1
            ),
            "loop-unknown-effect": LOOP_ORDERED_CAPTURE_SOURCE.replace(
                "        emitted++;", "        emitted++; external(anchor);", 1
            ),
            "loop-receiver-escape": LOOP_RECEIVER_SOURCE.replace(
                "        this.emitted++;", "        this.emitted++; anchor.saved = this;", 1
            ),
            "loop-nested-capture": LOOP_ORDERED_CAPTURE_SOURCE.replace(
                "        emitted++;",
                "        emitted++; const change = () => { emitted++; }; change();",
                1,
            ),
            "loop-break-nonnumber-captured-store": LOOP_BREAK_ORDERED_CAPTURE_SOURCE.replace(
                "closed += emitted;", "closed = anchor;", 1
            ),
            "loop-break-nonnumber-state-store": LOOP_BREAK_RECEIVER_SOURCE.replace(
                "this.closed += this.emitted;", "this.closed = anchor;", 1
            ),
            "loop-break-unknown-effect": LOOP_BREAK_CAPTURE_SOURCE.replace(
                "if (anchor.hasAttribute('stop')) break;",
                "if (anchor.hasAttribute('stop')) { external(anchor); break; }",
                1,
            ),
            "loop-break-receiver-escape": LOOP_BREAK_RECEIVER_SOURCE.replace(
                "if (anchor.hasAttribute('stop')) break;",
                "if (anchor.hasAttribute('stop')) { anchor.saved = this; break; }",
                1,
            ),
            "loop-break-nested-capture": LOOP_BREAK_ORDERED_CAPTURE_SOURCE.replace(
                "if (anchor.hasAttribute('stop')) break;",
                "if (anchor.hasAttribute('stop')) { const read = () => emitted; read(); break; }",
                1,
            ),
            "entry-nonnumber-preloop-store": ENTRY_CAPTURE_SOURCE.replace(
                "  while (emitted < 1)", "  closed = anchor;\n  while (emitted < 1)"
            ),
            "entry-nonnumber-body-store": ENTRY_CAPTURE_SOURCE.replace(
                "emitted += 3;", "emitted = anchor;"
            ),
            "entry-nonnumber-postloop-store": ENTRY_CAPTURE_SOURCE.replace(
                "  return count + emitted + closed;",
                "  emitted = anchor;\n  return count + emitted + closed;",
            ),
            "entry-captured-sibling-escaping-reader": SIBLING_CAPTURE_SOURCE.replace(
                "  return count + read() + closed;",
                "  anchor.saved = read;\n  return count + read() + closed;",
            ),
            "entry-captured-sibling-indirect-reader": SIBLING_CAPTURE_SOURCE.replace(
                "  return count + read() + closed;",
                "  const readers = {read: read};\n"
                "  return count + readers[anchor.getAttribute('data-reader')]() + closed;",
            ),
            "entry-captured-sibling-recursive-reader": SIBLING_CAPTURE_SOURCE.replace(
                "  const read = () => emitted;",
                "  const read = () => anchor.hasAttribute('stop') ? read() : emitted;",
            ),
            "entry-captured-sibling-escaping-writer": SIBLING_WRITER_SOURCE.replace(
                "  return count + read() + closed;",
                "  anchor.saved = read;\n  return count + read() + closed;",
            ),
            "entry-captured-sibling-indirect-writer": SIBLING_WRITER_SOURCE.replace(
                "  return count + read() + closed;",
                "  const writers = {read: read};\n"
                "  return count + writers[anchor.getAttribute('data-writer')]() + closed;",
            ),
            "entry-captured-sibling-recursive-writer": SIBLING_WRITER_SOURCE.replace(
                "emitted += closed; return emitted;",
                "emitted += closed; return emitted > closed ? read() : emitted;",
            ),
            "entry-captured-sibling-nonnumber-writer": SIBLING_WRITER_SOURCE.replace(
                "emitted += closed; return emitted;", "emitted = true; return emitted;"
            ),
            "entry-captured-sibling-loop-nonnumber-writer": SIBLING_LOOP_WRITER_SOURCE.replace(
                "      closed += emitted;", "      closed = true;"
            ),
            "entry-captured-sibling-branch-nonnumber-writer": SIBLING_BRANCH_WRITER_SOURCE.replace(
                "else closed++;", "else closed = true;"
            ),
            "entry-captured-sibling-argument-missing": SIBLING_ARGUMENT_WRITER_SOURCE.replace(
                "read(closed)", "read()"
            ),
            "entry-captured-sibling-argument-extra": SIBLING_ARGUMENT_WRITER_SOURCE.replace(
                "read(closed)", "read(closed, emitted)"
            ),
            "entry-captured-sibling-argument-unknown": SIBLING_ARGUMENT_WRITER_SOURCE.replace(
                "read(closed)", "read(external(closed))"
            ),
            "entry-captured-sibling-argument-object": SIBLING_ARGUMENT_WRITER_SOURCE.replace(
                "read(closed)", "read(anchor)"
            ).replace("      closed += emitted;", "      closed = amount;"),
            "entry-captured-sibling-argument-coercing": SIBLING_ARGUMENT_WRITER_SOURCE.replace(
                "read(closed)", "read('1')"
            ).replace("      closed += emitted;", "      closed += amount;"),
            "entry-captured-sibling-argument-escaping": SIBLING_ARGUMENT_WRITER_SOURCE.replace(
                "const before = emitted + amount;",
                "anchor.saved = () => amount;\n    const before = emitted + amount;",
            ),
            "entry-captured-sibling-loop-break-nonnumber": SIBLING_BREAK_FINITE_SOURCE.replace(
                "if (rounds === 1) break;",
                "if (rounds === 1) { closed = true; break; }",
                1,
            ),
            "entry-captured-sibling-loop-break-unknown": SIBLING_BREAK_FINITE_SOURCE.replace(
                "if (rounds === 1) break;",
                "if (rounds === 1) { external(emitted); break; }",
                1,
            ),
            "entry-captured-sibling-loop-break-nested": SIBLING_BREAK_FINITE_SOURCE.replace(
                "if (rounds === 1) break;",
                "if (rounds === 1) { const read = () => emitted; read(); break; }",
                1,
            ),
            "entry-captured-sibling-nested-call-recursive": SIBLING_NESTED_WRITER_SOURCE.replace(
                "emitted += closed; return before;",
                "emitted += closed; return before + advance();",
                1,
            ),
            "entry-captured-sibling-nested-call-mutual": SIBLING_SHARED_WRITER_SOURCE.replace(
                "const forward = () => advance();", "const forward = () => relay();"
            ).replace("const relay = () => advance() + 1;", "const relay = () => forward() + 1;"),
            "entry-captured-sibling-nested-call-escaping": SIBLING_NESTED_WRITER_SOURCE.replace(
                "  let count = read();", "  anchor.saved = advance;\n  let count = read();"
            ),
            "entry-captured-sibling-nested-call-receiver": SIBLING_NESTED_WRITER_SOURCE.replace(
                "rounds += advance();", "rounds += advance.call(anchor);", 1
            ),
            "entry-captured-sibling-nested-call-mutation": SIBLING_NESTED_WRITER_SOURCE.replace(
                "const advance =", "let advance =", 1
            ).replace("  let count = read();", "  advance = () => emitted;\n  let count = read();"),
            "entry-captured-sibling-nested-call-nonnumber": SIBLING_NESTED_WRITER_SOURCE.replace(
                "emitted += closed; return before;", "emitted = true; return before;", 1
            ),
            "entry-captured-sibling-callable-argument-escaping": SIBLING_CALLABLE_ARGUMENT_SOURCE.replace(
                "(writer) => writer();", "(writer) => { anchor.saved = writer; return writer(); };"
            ),
            "entry-captured-sibling-callable-argument-mutation": SIBLING_CALLABLE_ARGUMENT_SOURCE.replace(
                "const advance =", "let advance =", 1
            ).replace(
                "  count += read();\n  closed += emitted;",
                "  advance = () => emitted;\n  count += read();\n  closed += emitted;",
                1,
            ),
            "entry-captured-sibling-callable-argument-object": SIBLING_CALLABLE_ARGUMENT_SOURCE.replace(
                "rounds += relay(advance);", "rounds += relay(anchor);", 1
            ),
            "entry-captured-sibling-callable-argument-missing": SIBLING_CALLABLE_ARGUMENT_SOURCE.replace(
                "rounds += relay(advance);", "rounds += relay();", 1
            ),
            "entry-captured-sibling-callable-argument-recursive": SIBLING_CALLABLE_ARGUMENT_SOURCE.replace(
                "(writer) => writer();", "(writer) => writer(writer);"
            ).replace(
                "rounds += relay(advance);", "rounds += relay(relay);", 1
            ),
            "entry-captured-sibling-callable-argument-nonnumber": SIBLING_CALLABLE_ARGUMENT_SOURCE.replace(
                "emitted += closed; return before;", "emitted = true; return before;", 1
            ),
            "entry-captured-sibling-returned-callable-escaping": SIBLING_RETURNED_CALLABLE_SOURCE.replace(
                "(writer) => identity(writer)();",
                "(writer) => { const returned = identity(writer);\n"
                "    anchor.saved = returned; return returned(); };",
            ),
            "entry-captured-sibling-returned-callable-observed": SIBLING_RETURNED_CALLABLE_SOURCE.replace(
                "(writer) => identity(writer)();",
                "(writer) => identity(writer) === writer;",
            ),
            "entry-captured-sibling-returned-callable-object": SIBLING_RETURNED_CALLABLE_SOURCE.replace(
                "(writer) => writer;", "(writer) => anchor;"
            ),
            "entry-captured-sibling-returned-callable-missing": SIBLING_RETURNED_CALLABLE_SOURCE.replace(
                "(writer) => writer;", "(writer) => {};"
            ),
            "entry-captured-sibling-returned-callable-recursive": SIBLING_RETURNED_CALLABLE_SOURCE.replace(
                "(writer) => writer;", "(writer) => identity(writer);"
            ),
            "entry-captured-sibling-returned-callable-nonnumber": SIBLING_RETURNED_CALLABLE_SOURCE.replace(
                "(writer) => writer;", "(writer) => { emitted = true; return writer; };"
            ),
            "entry-captured-sibling-returned-different-unknown": SIBLING_RETURNED_DIFFERENT_TARGETS_SOURCE.replace(
                "const alternate = () => identity(other)();",
                "const alternate = () => identity(unknownWriter)();",
            ),
            "entry-captured-sibling-returned-different-mutation": SIBLING_RETURNED_DIFFERENT_TARGETS_SOURCE.replace(
                "const other =", "let other =", 1
            ).replace(
                "  count += read();\n  closed += emitted;",
                "  other = advance;\n  count += read();\n  closed += emitted;",
                1,
            ),
            "entry-captured-sibling-returned-different-escaping": SIBLING_RETURNED_DIFFERENT_TARGETS_SOURCE.replace(
                "const alternate = () => identity(other)();",
                "const alternate = () => { const returned = identity(other);\n"
                "    anchor.saved = returned; return returned(); };",
            ),
            "entry-captured-sibling-returned-different-observed": SIBLING_RETURNED_DIFFERENT_TARGETS_SOURCE.replace(
                "const alternate = () => identity(other)();",
                "const alternate = () => identity(other) === other;",
            ),
            "entry-captured-sibling-returned-different-missing": SIBLING_RETURNED_DIFFERENT_TARGETS_SOURCE.replace(
                "const alternate = () => identity(other)();",
                "const alternate = () => identity()();",
            ),
            "entry-captured-sibling-returned-different-recursive": SIBLING_RETURNED_DIFFERENT_TARGETS_SOURCE.replace(
                "const identity = (writer) => writer;",
                "const identity = (writer) => identity(writer);",
            ),
            "entry-captured-sibling-returned-branch-escaping": SIBLING_RETURNED_BRANCH_JOIN_SOURCE.replace(
                "    return selected;", "    anchor.saved = selected;\n    return selected;", 1
            ),
            "entry-captured-sibling-returned-branch-observed": SIBLING_RETURNED_BRANCH_JOIN_SOURCE.replace(
                "    return selected;",
                "    closed += selected === other;\n    return selected;",
                1,
            ),
            "entry-captured-sibling-returned-branch-unknown": SIBLING_RETURNED_BRANCH_JOIN_SOURCE.replace(
                "selected = other;", "selected = unknownWriter;", 1
            ),
            "entry-captured-sibling-returned-branch-object": SIBLING_RETURNED_BRANCH_JOIN_SOURCE.replace(
                "selected = other;", "selected = anchor;", 1
            ),
            "entry-captured-sibling-returned-branch-recursive": SIBLING_RETURNED_BRANCH_JOIN_SOURCE.replace(
                "selected = other;", "selected = identity(writer);", 1
            ),
            "entry-captured-sibling-returned-branch-mutation": SIBLING_RETURNED_BRANCH_JOIN_SOURCE.replace(
                "const other =", "let other =", 1
            ).replace(
                "  count += read();\n  closed += emitted;",
                "  other = advance;\n  count += read();\n  closed += emitted;",
                1,
            ),
            "entry-captured-sibling-returned-callable-loop-join": SIBLING_RETURNED_LOOP_JOIN_SOURCE,
        }
    )
    return variants


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("translate", "opt", "clang", "node", "reference"):
        parser.add_argument("--" + name, required=True)
    for name in ("build", "include", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--nm", default=shutil.which("nm"))
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    observations = oracles(args)
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args)
    executions = refused = admitted = 0
    for label, text, breaking, results, resetting, closed in POSITIVES:
        ir, contract = dom.prepare(args, f"custom-{label}", text, 1, entry_name="customElements")
        contract.update(initial_intrinsics=INTRINSICS)
        for owned in (False, True):
            manifest = dict(
                contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            )
            for optimize in (False, True):
                name = f"custom-iteration-{label}-{owned}-{optimize}"
                native = dom.lower(args, ir, manifest, name, optimize=optimize)
                cpp = dom.run([args.translate, "--mlir-to-cpp", str(native)]).stdout
                if any(helper in cpp for helper in SNAPSHOT_INTRINSICS[4:]):
                    raise RuntimeError(f"{name}: custom iteration retained the VM protocol")
                checks = CHECKS + (OWNED_CHECKS if owned else "")
                checks = checks.replace("@BREAKING@", "true" if breaking else "false")
                checks = checks.replace("@RESETTING@", "true" if resetting else "false")
                checks = checks.replace("@CLOSED@", closed)
                if label in ("preloop-captured-read", "extra-captured-closure"):
                    checks = checks.replace(
                        'const auto stop = names.intern("stop")',
                        'const auto extra = names.intern("data-extra");\n'
                        '            const auto stop = names.intern("stop")',
                    ).replace("check_writes({", "check_writes({extra, ")
                    checks = checks.replace(
                        "{next, yielded, visited, closed, stop, advance}",
                        "{next, yielded, visited, closed, stop, advance, extra}",
                    )
                    checks = checks.replace(
                        "assert(@SECOND_RESULT@);",
                        'assert(target.read().attribute_value(id, extra) == "true");\n'
                        "                assert(@SECOND_RESULT@);\n"
                        '                assert(target.read().attribute_value(id, extra) == "true");',
                    )
                normal, stopped, exhausted = results
                value = "static_cast<bool>(call())" if normal == "true" else "call().value()"
                checks = checks.replace(
                    "@FIRST_RESULT@",
                    f"{value} == (stopping ? {stopped} : {normal})",
                ).replace(
                    "@SECOND_RESULT@",
                    (
                        f"{value} == (stopping ? {stopped} : {exhausted})"
                        if resetting
                        else f"{value} == {exhausted}"
                    ),
                )
                dom.standalone(args, native, name, checks, compilers, includes, libraries)
                executions += 2 * len(compilers)
                controls = [("budget", manifest, 0)]
                for intrinsic in ["Symbol", "Object", *SNAPSHOT_INTRINSICS[4:]]:
                    controls.append(
                        (
                            "missing-" + intrinsic,
                            dict(
                                manifest,
                                initial_intrinsics=[
                                    item for item in INTRINSICS if item != intrinsic
                                ],
                            ),
                            None,
                        )
                    )
                for suffix, bad, budget in controls:
                    dom.lower(
                        args,
                        ir,
                        bad,
                        name + "-" + suffix,
                        optimize=optimize,
                        max_steps=budget,
                        success=False,
                    )
                    refused += 1
    for name, text in COMPILE_ONLY.items():
        ir, contract = dom.prepare(args, name, text, 1, entry_name="customElements")
        contract.update(initial_intrinsics=INTRINSICS)
        for optimize in (False, True):
            dom.lower(args, ir, contract, f"compile-only-{name}-{optimize}", optimize=optimize)
            admitted += 1
    for name, text in refusals().items():
        ir, contract = dom.prepare(args, name, text, 1, entry_name="customElements")
        contract.update(initial_intrinsics=INTRINSICS)
        for optimize in (False, True):
            dom.lower(
                args, ir, contract, f"refused-{name}-{optimize}", optimize=optimize, success=False
            )
            refused += 1
    print(
        f"Closed custom DOM iteration: {executions} native executions, {refused} refusals; "
        f"{admitted} nonexecuted source admissions; "
        f"{observations} Node/VM source-double observations; DOM/Core only"
    )


if __name__ == "__main__":
    main()
