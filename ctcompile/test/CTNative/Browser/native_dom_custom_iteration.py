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


BODY_RETURN_SOURCE = source(True).replace(
    "    node.setAttribute('data-visited', 'yes');",
    "    return anchor.hasAttribute('data-visited');",
)
BODY_RETURN_ORDERED_SOURCE = BODY_RETURN_SOURCE.replace(
    "return anchor.hasAttribute('data-visited');",
    "return (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
    1,
)
BODY_RETURN_BRANCH_SOURCE = source(True).replace(
    "if (anchor.hasAttribute('stop')) break;",
    "if (anchor.hasAttribute('stop')) return anchor.hasAttribute('data-closed');",
)
BODY_RETURN_BRANCH_ORDERED_SOURCE = BODY_RETURN_BRANCH_SOURCE.replace(
    "anchor.setAttribute('data-closed', 'yes');",
    "anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
)
BODY_RETURN_BRANCH_EXPRESSION_SOURCE = BODY_RETURN_BRANCH_SOURCE.replace(
    "    node.setAttribute('data-visited', 'yes');\n"
    "    if (anchor.hasAttribute('stop')) return anchor.hasAttribute('data-closed');",
    "    if (anchor.hasAttribute('stop'))\n"
    "      return (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));\n"
    "    node.setAttribute('data-visited', 'yes');",
).replace(
    "anchor.setAttribute('data-closed', 'yes');",
    "anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
)

BODY_RETURN_BRANCH_EXPRESSION_SNAPSHOT_SOURCE = BODY_RETURN_BRANCH_EXPRESSION_SOURCE.replace(
    "anchor.hasAttribute('data-closed'));",
    "anchor.hasAttribute('data-closed') === anchor.hasAttribute('data-visited'));",
)
BODY_RETURN_ELEMENT_IDENTITY_SOURCE = BODY_RETURN_BRANCH_EXPRESSION_SOURCE.replace(
    "anchor.hasAttribute('data-closed'));",
    "node === anchor && !anchor.hasAttribute('data-closed'));",
)
SELECTOR_SOURCES = {
    "body-return-branch-element-selector": BODY_RETURN_BRANCH_EXPRESSION_SOURCE.replace(
        "anchor.hasAttribute('data-closed'));", "node.matches('button'));"
    ),
    "body-return-branch-selector-before-close": BODY_RETURN_BRANCH_EXPRESSION_SOURCE.replace(
        "anchor.hasAttribute('data-closed'));", "node.matches('[data-closed]'));"
    ),
    "body-return-branch-selector-prototype": BODY_RETURN_BRANCH_EXPRESSION_SOURCE.replace(
        "anchor.hasAttribute('data-closed'));",
        "Element.prototype.matches.call(node, 'button'));",
    ),
}
MIXED_SELECTOR_SOURCE = (
    SELECTOR_SOURCES["body-return-branch-element-selector"]
    .replace("function customElements(anchor)", "function customElements(anchor, other)")
    .replace(
        "  for (const node of values) {",
        "  for (const node of values) {\n"
        "    const selected = anchor.hasAttribute('advance') ? node : other;",
    )
    .replace(
        "return (node.setAttribute('data-visited', 'yes'), node.matches('button'));",
        "return (selected.setAttribute('data-visited', 'yes'), selected.matches('button'));",
    )
)
MIXED_SELECTOR_SOURCES = {
    "body-return-branch-element-selector-mixed-roots": MIXED_SELECTOR_SOURCE,
    "body-return-branch-selector-mixed-before-close": MIXED_SELECTOR_SOURCE.replace(
        "selected.matches('button')", "selected.matches('[data-closed]')"
    ),
    "body-return-branch-selector-mixed-prototype": MIXED_SELECTOR_SOURCE.replace(
        "selected.matches('button')", "Element.prototype.matches.call(selected, 'button')"
    ),
}
BODY_RETURN_BRANCH_NUMBER_SOURCE = (
    BODY_RETURN_BRANCH_SOURCE.replace(
        "  const values = {", "  let count = 0;\n  const values = {", 1
    )
    .replace(
        "anchor.setAttribute('data-closed', 'yes');",
        "count += 10;\n      anchor.setAttribute('data-closed', 'yes');",
        1,
    )
    .replace(
        "    node.setAttribute('data-visited', 'yes');",
        "    node.setAttribute('data-visited', 'yes');\n    count++;",
        1,
    )
    .replace("return anchor.hasAttribute('data-closed');", "return (count += 2, count);", 1)
    .replace("return anchor.hasAttribute('data-visited');", "return count;", 1)
)
BODY_RETURN_LOOP_LOCAL_NUMBER_SOURCE = BODY_RETURN_BRANCH_NUMBER_SOURCE.replace(
    "    if (anchor.hasAttribute('stop')) return (count += 2, count);",
    "    const snapshot = count + 2;\n    if (anchor.hasAttribute('stop')) return snapshot;",
)
BODY_RETURN_MULTIPLE_SOURCE = BODY_RETURN_BRANCH_EXPRESSION_SOURCE.replace(
    "    node.setAttribute('data-visited', 'yes');",
    "    if (anchor.hasAttribute('advance'))\n"
    "      return (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));\n"
    "    node.setAttribute('data-visited', 'yes');",
)


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

SIBLING_RETURNED_LOOP_ZERO_SOURCE = SIBLING_RETURNED_LOOP_JOIN_SOURCE.replace(
    "while (rounds < 2)", "while (rounds < 0)", 1
)
SIBLING_RETURNED_LOOP_SNAPSHOTS_SOURCE = SIBLING_RETURNED_BRANCH_SNAPSHOTS_SOURCE.replace(
    "    if (emitted > 0) selected = other;",
    "    let rounds = 0;\n"
    "    while (rounds < 2) {\n"
    "      if (rounds === 0) selected = writer;\n"
    "      else if (emitted > 0) selected = other;\n"
    "      if (emitted > 0) closed += rounds;\n"
    "      rounds++;\n"
    "    }",
    1,
)
SIBLING_RETURNED_LOOP_CALL_SOURCE = SIBLING_RETURNED_LOOP_JOIN_SOURCE.replace(
    "  const identity =", "  const keep = (writer) => writer;\n  const identity =", 1
).replace("      rounds++;", "      selected = keep(selected);\n      rounds++;", 1)
SIBLING_RETURNED_LOOP_CALL_SNAPSHOTS_SOURCE = SIBLING_RETURNED_LOOP_SNAPSHOTS_SOURCE.replace(
    "  const identity =",
    "  const keep = (writer, prior) => { closed += prior; return writer; };\n" "  const identity =",
    1,
).replace("      rounds++;", "      selected = keep(selected, emitted);\n      rounds++;", 1)
SIBLING_RETURNED_LOOP_CALL_ZERO_SOURCE = SIBLING_RETURNED_LOOP_CALL_SOURCE.replace(
    "const keep = (writer) => writer;",
    "const keep = (writer) => { closed += emitted; return writer; };",
    1,
).replace("while (rounds < 2)", "while (rounds < 0)", 1)
SIBLING_RETURNED_LOOP_FORWARDED_SOURCE = SIBLING_RETURNED_LOOP_CALL_SOURCE.replace(
    "const keep = (writer) => writer;",
    "const forward = (writer) => writer;\n" "  const keep = (writer) => forward(writer);",
    1,
)
SIBLING_RETURNED_LOOP_FORWARDED_SNAPSHOTS_SOURCE = (
    SIBLING_RETURNED_LOOP_CALL_SNAPSHOTS_SOURCE.replace(
        "const keep = (writer, prior) => { closed += prior; return writer; };",
        "const forward = (writer, prior) => { closed += prior; return writer; };\n"
        "  const keep = (writer, prior) => forward(writer, prior);",
        1,
    )
)
SIBLING_RETURNED_LOOP_FORWARDED_ZERO_SOURCE = SIBLING_RETURNED_LOOP_FORWARDED_SOURCE.replace(
    "const forward = (writer) => writer;",
    "const forward = (writer) => { closed += emitted; return writer; };",
    1,
).replace("while (rounds < 2)", "while (rounds < 0)", 1)
SIBLING_RETURNED_LOOP_BRANCH_SOURCE = SIBLING_RETURNED_LOOP_FORWARDED_SOURCE.replace(
    "const forward = (writer) => writer;",
    "const forward = (writer) => { let selected = writer;\n"
    "    if (emitted > 0) selected = other; return selected; };",
    1,
)
SIBLING_RETURNED_LOOP_BRANCH_SNAPSHOTS_SOURCE = (
    SIBLING_RETURNED_LOOP_FORWARDED_SNAPSHOTS_SOURCE.replace(
        "const forward = (writer, prior) => { closed += prior; return writer; };",
        "const forward = (writer, alternate, prior) => { let selected = writer;\n"
        "    if (prior > 0) selected = alternate; closed += prior; return selected; };",
        1,
    ).replace("forward(writer, prior);", "forward(writer, advance, prior);", 1)
)
SIBLING_RETURNED_LOOP_BRANCH_ZERO_SOURCE = SIBLING_RETURNED_LOOP_BRANCH_SOURCE.replace(
    "if (emitted > 0) selected = other; return selected;",
    "if (emitted > 0) selected = other; closed += emitted; return selected;",
    1,
).replace("while (rounds < 2)", "while (rounds < 0)", 1)

SIBLING_RETURNED_FORMAL_CALLEE_SOURCE = SIBLING_RETURNED_LOOP_BRANCH_SOURCE.replace(
    "const keep = (writer) => forward(writer);",
    "const keep = (writer, chooser) => chooser(writer);",
    1,
).replace("selected = keep(selected);", "selected = keep(selected, forward);", 1)
SIBLING_RETURNED_SELECTED_CALLEE_SNAPSHOTS_SOURCE = (
    SIBLING_RETURNED_LOOP_BRANCH_SNAPSHOTS_SOURCE.replace(
        "const keep = (writer, prior) => forward(writer, advance, prior);",
        "const passthrough = (writer, alternate, prior) => { closed += prior; return writer; };\n"
        "  const keep = (writer, prior, chooser) => { let selected = chooser;\n"
        "    if (prior > 0) selected = passthrough; return selected(writer, advance, prior); };",
        1,
    ).replace("keep(selected, emitted);", "keep(selected, emitted, forward);", 1)
)
SIBLING_RETURNED_FORMAL_CALLEE_ZERO_SOURCE = SIBLING_RETURNED_FORMAL_CALLEE_SOURCE.replace(
    "if (emitted > 0) selected = other; return selected;",
    "if (emitted > 0) selected = other; closed += emitted; return selected;",
    1,
).replace("while (rounds < 2)", "while (rounds < 0)", 1)

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
    (
        "entry-captured-sibling-returned-callable-loop-join",
        SIBLING_RETURNED_LOOP_JOIN_SOURCE,
        True,
        ("2729", "3603", "2729"),
        True,
        "false",
    ),
    (
        "entry-captured-sibling-returned-loop-zero",
        SIBLING_RETURNED_LOOP_ZERO_SOURCE,
        True,
        ("2729", "3603", "2729"),
        True,
        "false",
    ),
    (
        # Selection changes across backedges, after earlier scalar arguments
        # were evaluated and before the selected writer reads current state.
        "entry-captured-sibling-returned-loop-snapshots",
        SIBLING_RETURNED_LOOP_SNAPSHOTS_SOURCE,
        True,
        ("181308", "211052", "181308"),
        True,
        "false",
    ),
    (
        "entry-captured-sibling-returned-loop-call-result",
        SIBLING_RETURNED_LOOP_CALL_SOURCE,
        True,
        ("2729", "3603", "2729"),
        True,
        "false",
    ),
    (
        "entry-captured-sibling-returned-loop-call-snapshots",
        SIBLING_RETURNED_LOOP_CALL_SNAPSHOTS_SOURCE,
        True,
        ("886698", "980596", "886698"),
        True,
        "false",
    ),
    (
        # No backedge call runs; the initial identity and prior state survive.
        "entry-captured-sibling-returned-loop-call-zero",
        SIBLING_RETURNED_LOOP_CALL_ZERO_SOURCE,
        True,
        ("2729", "3603", "2729"),
        True,
        "false",
    ),
    (
        "entry-captured-sibling-returned-loop-call-forwarded",
        SIBLING_RETURNED_LOOP_FORWARDED_SOURCE,
        True,
        ("2729", "3603", "2729"),
        True,
        "false",
    ),
    (
        # The nested return helper writes current state using a scalar snapshot.
        "entry-captured-sibling-returned-loop-forwarded-snapshots",
        SIBLING_RETURNED_LOOP_FORWARDED_SNAPSHOTS_SOURCE,
        True,
        ("886698", "980596", "886698"),
        True,
        "false",
    ),
    (
        "entry-captured-sibling-returned-loop-forwarded-zero",
        SIBLING_RETURNED_LOOP_FORWARDED_ZERO_SOURCE,
        True,
        ("2729", "3603", "2729"),
        True,
        "false",
    ),
    (
        "entry-captured-sibling-returned-loop-forwarded-branch",
        SIBLING_RETURNED_LOOP_BRANCH_SOURCE,
        True,
        ("2729", "3603", "2729"),
        True,
        "false",
    ),
    (
        # The inner branch restores advance after the outer branch chose other;
        # the earlier scalar arguments and the nested state write both survive.
        "entry-captured-sibling-returned-loop-branch-snapshots",
        SIBLING_RETURNED_LOOP_BRANCH_SNAPSHOTS_SOURCE,
        True,
        ("494861", "547651", "494861"),
        True,
        "false",
    ),
    (
        # Neither nested branch selection nor its state write runs on zero trips.
        "entry-captured-sibling-returned-loop-branch-zero",
        SIBLING_RETURNED_LOOP_BRANCH_ZERO_SOURCE,
        True,
        ("2729", "3603", "2729"),
        True,
        "false",
    ),
    (
        "entry-captured-sibling-returned-loop-branch-formal-callee",
        SIBLING_RETURNED_FORMAL_CALLEE_SOURCE,
        True,
        ("2729", "3603", "2729"),
        True,
        "false",
    ),
    (
        # The selected callee keeps the outer writer instead of restoring advance;
        # its state effect still follows the earlier scalar argument snapshots.
        "entry-captured-sibling-returned-selected-callee-snapshots",
        SIBLING_RETURNED_SELECTED_CALLEE_SNAPSHOTS_SOURCE,
        True,
        ("886698", "980596", "886698"),
        True,
        "false",
    ),
    (
        "entry-captured-sibling-returned-formal-callee-zero",
        SIBLING_RETURNED_FORMAL_CALLEE_ZERO_SOURCE,
        True,
        ("2729", "3603", "2729"),
        True,
        "false",
    ),
    ("body-return", BODY_RETURN_SOURCE, True, ("false", "false", "false"), False, "yes"),
    (
        "body-return-ordered",
        BODY_RETURN_ORDERED_SOURCE,
        True,
        ("false", "false", "false"),
        False,
        "yes",
    ),
    (
        "body-return-branch",
        BODY_RETURN_BRANCH_SOURCE,
        True,
        ("true", "false", "false"),
        False,
        "yes",
    ),
    (
        "body-return-branch-ordered",
        BODY_RETURN_BRANCH_ORDERED_SOURCE,
        True,
        ("true", "false", "false"),
        False,
        "true",
    ),
    (
        "body-return-branch-expression",
        BODY_RETURN_BRANCH_EXPRESSION_SOURCE,
        True,
        ("true", "false", "false"),
        False,
        "true",
    ),
    (
        "body-return-branch-expression-snapshot",
        BODY_RETURN_BRANCH_EXPRESSION_SNAPSHOT_SOURCE,
        True,
        ("true", "false", "false"),
        False,
        "true",
    ),
    (
        "body-return-element-identity",
        BODY_RETURN_ELEMENT_IDENTITY_SOURCE,
        True,
        ("true", "true", "false"),
        False,
        "true",
    ),
    (
        "body-return-multiple",
        BODY_RETURN_MULTIPLE_SOURCE,
        True,
        ("false", "false", "false"),
        False,
        "true",
    ),
    (
        # The return saves 3 before close changes the captured count to 13.
        # The already-yielded state never enters the body or closes its iterator.
        "body-return-branch-number",
        BODY_RETURN_BRANCH_NUMBER_SOURCE,
        True,
        ("1", "3", "0"),
        False,
        "yes",
    ),
    (
        "body-return-loop-local-number",
        BODY_RETURN_LOOP_LOCAL_NUMBER_SOURCE,
        True,
        ("1", "3", "0"),
        False,
        "yes",
    ),
    (
        "body-return-branch-element-selector",
        SELECTOR_SOURCES["body-return-branch-element-selector"],
        True,
        ("true", "true", "false"),
        False,
        "true",
    ),
    (
        # The selected return reads false before close creates data-closed.
        "body-return-branch-selector-before-close",
        SELECTOR_SOURCES["body-return-branch-selector-before-close"],
        True,
        ("true", "false", "false"),
        False,
        "true",
    ),
    (
        "body-return-branch-selector-prototype",
        SELECTOR_SOURCES["body-return-branch-selector-prototype"],
        True,
        ("true", "true", "false"),
        False,
        "true",
    ),
)


def oracles(args):
    # These receivers record the same source calls. The native clients below
    # separately check those calls against public DOM and document ownership.
    script, observations = "", []
    if any(label in SELECTOR_SOURCES for label, *_ in POSITIVES):
        script = (
            "var Element = {prototype: {matches(selector) { return this.matches(selector); }}};\n"
        )
    for index, (label, text, breaking, results, resetting, closed) in enumerate(POSITIVES):
        selecting = label in SELECTOR_SOURCES
        function = f"customElementsCase{index}"
        script += text.replace("function customElements(", f"function {function}(")
        normal, stopped, exhausted = results
        states = (
            ("normal", normal),
            ("stop", stopped),
            ("already-yielded", exhausted),
            ("stop-without-advance", stopped),
        )
        if label == "body-return-multiple":
            states += (("without-advance", "true"),)
        for state, result in states:
            name = f"customObservation{len(observations)}"
            setup = "saved.advance = '';"
            if state == "stop-without-advance":
                setup = "saved.stop = '';"
            elif state == "without-advance":
                setup = ""
            elif state == "stop":
                setup += "saved.stop = '';"
            elif state == "already-yielded":
                setup += "saved['data-yielded'] = 'yes';"
            selector_method = (
                "matches(selector) { queries++; "
                "if (selector === 'button') return true; "
                "if (selector === '[data-closed]') return 'data-closed' in saved; "
                "throw 'unexpected selector'; },"
                if selecting
                else ""
            )
            query_observation = "queries + ':' + " if selecting else ""
            script += f"""
var {name} = (function() {{
  const saved = {{}};
  {setup}
  let writes = '';
  let queries = 0;
  const anchor = {{
    {selector_method}
    hasAttribute(name) {{ return name in saved; }},
    setAttribute(name, value) {{
      saved[name] = '' + value;
      writes += name + '=' + saved[name] + ';';
    }}
  }};
  const result = {function}(anchor);
  return result + ':' + {query_observation}writes;
}})();
"""
            expected = result + ":data-next=true;data-yielded=yes;"
            if state != "already-yielded" or resetting:
                expected = result + ":data-next=false;data-yielded=yes;data-visited=yes;"
                expected += (
                    f"data-closed={closed};"
                    if breaking
                    and (
                        state.startswith("stop")
                        or label in ("body-return", "body-return-ordered")
                        or (label == "body-return-multiple" and state != "without-advance")
                    )
                    else "data-next=true;data-yielded=yes;"
                )
                if label == "body-return":
                    expected = expected.replace("data-visited=yes;", "")
            if label in ("preloop-captured-read", "extra-captured-closure"):
                expected = expected.replace(":", ":data-extra=true;", 1)
            if selecting:
                expected = expected.replace(":", ":1:" if state.startswith("stop") else ":0:", 1)
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


def mixed_oracles(args):
    script = "var Element = {prototype: {matches(s) { return this.matches(s); }}};\n"
    observations = []
    for index, (label, text) in enumerate(MIXED_SELECTOR_SOURCES.items()):
        function = f"mixedElementsCase{index}"
        script += text.replace("function customElements(", f"function {function}(")
        for mode in range(4):
            name = f"mixedObservation{len(observations)}"
            setup = (
                "anchor.saved.advance = '';",
                "anchor.saved.advance = ''; anchor.saved.stop = '';",
                "anchor.saved['data-yielded'] = 'yes';",
                "anchor.saved.stop = '';",
            )[mode]
            script += f"""
var {name} = (function() {{
  let trace = '';
  const make = function(label, button) {{ return {{
    saved: {{}},
    hasAttribute(name) {{ return name in this.saved; }},
    setAttribute(name, value) {{
      this.saved[name] = '' + value;
      trace += label + '.' + name + '=' + this.saved[name] + ';';
    }},
    matches(selector) {{
      trace += label + '.matches=' + selector + ';';
      if (selector === 'button') return button;
      if (selector === '[data-closed]') return 'data-closed' in this.saved;
      throw 'unexpected selector';
    }}
  }}; }};
  const anchor = make('a', true), other = make('b', false);
  {setup}
  const result = {function}(anchor, other);
  return result + ':' + trace;
}})();
"""
            before_close = "before-close" in label
            selector = "[data-closed]" if before_close else "button"
            expected = "false:a.data-next=true;a.data-yielded=yes;"
            if mode != 2:
                expected = "a.data-next=false;a.data-yielded=yes;"
                if mode == 0:
                    expected = (
                        "true:"
                        + expected
                        + "a.data-visited=yes;a.data-next=true;a.data-yielded=yes;"
                    )
                else:
                    selected = "a" if mode == 1 else "b"
                    result = "true" if mode == 1 and not before_close else "false"
                    closed = "true" if mode == 1 else "false"
                    expected = (
                        result + ":" + expected + f"{selected}.data-visited=yes;"
                        f"{selected}.matches={selector};a.data-closed={closed};"
                    )
            observations.append((name, expected))
    node = args.work / "custom-mixed-node.js"
    node.write_text(script + "".join(f"console.log({name});\n" for name, _ in observations))
    actual = dom.run([args.node, str(node)]).stdout.splitlines()
    if actual != [value for _, value in observations]:
        raise RuntimeError(f"Node mixed selector effects differ: {actual!r}")
    vm = args.work / "custom-mixed-vm.js"
    vm.write_text(script)
    actual = dom.run([args.reference, str(vm)]).stdout
    expected = "".join(f'{name}="{quote(value)}"\n' for name, value in sorted(observations))
    if actual != expected:
        raise RuntimeError(f"VM mixed selector effects differ: {actual!r}")
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


MIXED_CHECKS = r"""
        (void)pressed;
        style::engine selectors{atoms}, foreign_selectors{foreign_atoms};
        const auto exercise = [&](element_ref first, element_ref second, auto && call) {
            auto & target = *first.owner;
            auto & peer = *second.owner;
            target.log_writes(true);
            peer.log_writes(true);
            const auto key = [&](std::string_view name) { return target.atoms().intern(name); };
            const auto check_writes = [](ctbrowser::document & owner,
                std::initializer_list<std::pair<node_id, std::string_view>> expected) {
                const auto writes = owner.take_writes();
                assert(writes.size() == expected.size());
                std::size_t at = 0;
                for (const auto & [id, name] : expected) {
                    assert(writes[at].node == id && writes[at].name == owner.atoms().intern(name)
                           && !writes[at].text);
                    ++at;
                }
            };
            for (unsigned mode : {0u, 1u, 2u, 3u}) {
                for (const auto input : {first, second}) {
                    for (const auto name : {"advance", "stop", "data-next", "data-yielded",
                                           "data-visited", "data-closed"}) {
                        assert(input.owner->remove_attribute(input.id, input.owner->atoms().intern(name)));
                    }
                }
                if (mode < 2) { assert(target.set_attribute(first.id, key("advance"), "")); }
                if (mode == 1 || mode == 3) { assert(target.set_attribute(first.id, key("stop"), "")); }
                if (mode == 2) { assert(target.set_attribute(first.id, key("data-yielded"), "yes")); }
                (void)target.take_writes();
                (void)peer.take_writes();
                const bool stopped = mode == 1 || mode == 3;
                const auto selected = mode == 3 ? second : first;
                const bool expected = mode == 0 ||
                    (stopped && @MATCH_RESULT@);
                assert(static_cast<bool>(call()) == expected);
                assert(target.read().attribute_value(first.id, key("data-next")) ==
                       (mode == 0 || mode == 2 ? "true" : "false"));
                assert(target.read().has_attribute(first.id, key("data-closed")) == stopped);
                if (stopped) {
                    assert(target.read().attribute_value(first.id, key("data-closed")) ==
                           (mode == 1 ? "true" : "false"));
                }
                if (mode == 0) {
                    check_writes(target, {{first.id, "data-next"}, {first.id, "data-yielded"},
                        {first.id, "data-visited"}, {first.id, "data-next"}, {first.id, "data-yielded"}});
                } else if (mode == 2) {
                    check_writes(target, {{first.id, "data-next"}, {first.id, "data-yielded"}});
                } else if (selected.owner == first.owner) {
                    check_writes(target, {{first.id, "data-next"}, {first.id, "data-yielded"},
                        {selected.id, "data-visited"}, {first.id, "data-closed"}});
                } else {
                    check_writes(target, {{first.id, "data-next"}, {first.id, "data-yielded"},
                        {first.id, "data-closed"}});
                }
                if (&peer != &target) {
                    if (mode == 3) { check_writes(peer, {{second.id, "data-visited"}}); }
                    else { check_writes(peer, {}); }
                }
            }
        };
        exercise(alias, foreign, [&] { return @ENTRY@(alias, foreign, selectors, foreign_selectors); });
        exercise(foreign, alias, [&] { return @ENTRY@(foreign, alias, foreign_selectors, selectors); });
        const element_ref descendant{&doc, child};
        exercise(alias, descendant, [&] { return @ENTRY@(alias, descendant, selectors, selectors); });
        assert(doc.remove_child(button));
        (void)doc.take_writes();
        exercise(alias, foreign, [&] { return @ENTRY@(alias, foreign, selectors, foreign_selectors); });
        const auto text = doc.create_text("invalid input");
        for (const element_ref invalid : {element_ref{}, element_ref{&doc, {}},
                 element_ref{&doc, text}, element_ref{&doc, {button.slot, button.generation + 2}}}) {
            for (const bool first : {true, false}) {
                bool rejected = false;
                try { (void)@ENTRY@(first ? invalid : alias, first ? foreign : invalid,
                                   selectors, foreign_selectors); }
                catch (const std::exception &) { rejected = true; }
                assert(rejected && doc.take_writes().empty() && foreign_doc.take_writes().empty());
            }
        }
        for (const bool first : {true, false}) {
            bool rejected = false;
            try { (void)@ENTRY@(alias, foreign, first ? foreign_selectors : selectors,
                               first ? foreign_selectors : selectors); }
            catch (const std::invalid_argument &) { rejected = true; }
            assert(rejected && doc.take_writes().empty() && foreign_doc.take_writes().empty());
        }
"""

MIXED_OWNED_CHECKS = r"""
        @ENTRY@_session session;
        auto & owned = session.document();
        const element_ref first{&owned, owned.create_element(owned.atoms().intern("button"))};
        const element_ref second{&owned, owned.create_element(owned.atoms().intern("span"))};
        exercise(first, second, [&] { return session.invoke(first, second); });
        for (const bool left : {true, false}) {
            bool rejected = false;
            try { (void)session.invoke(left ? foreign : first, left ? second : foreign); }
            catch (const std::invalid_argument &) { rejected = true; }
            assert(rejected && owned.take_writes().empty() && foreign_doc.take_writes().empty());
        }
"""


def mixed_selectors(args, compilers):
    includes, libraries = dom.link_options(args, selectors=True)
    executions = refused = 0
    for label, text in MIXED_SELECTOR_SOURCES.items():
        ir, contract = dom.prepare(args, f"custom-{label}", text, 2, entry_name="customElements")
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
                    raise RuntimeError(f"{name}: mixed selectors retained the VM protocol")
                checks = (MIXED_CHECKS + (MIXED_OWNED_CHECKS if owned else "")).replace(
                    "@MATCH_RESULT@",
                    (
                        "false"
                        if "before-close" in label
                        else 'selected.owner->read().local_name(selected.id) == "button"'
                    ),
                )
                dom.standalone(args, native, name, checks, compilers, includes, libraries)
                executions += 2 * len(compilers)
                for suffix, bad, budget in (
                    ("budget", manifest, 0),
                    ("missing-second", dict(manifest, element_parameters=[0]), None),
                ):
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
    return executions, refused


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
        # Throw completion still needs its cleanup and handler proof.
        "body-throw": text.replace(visited, "    throw 1;"),
    }
    variants.update(
        {
            "body-throw-boolean-snapshot": text.replace(
                visited,
                "    throw (node.setAttribute('data-visited', 'yes'), "
                "anchor.hasAttribute('data-closed'));",
            ),
            "body-throw-number-snapshot": BODY_RETURN_BRANCH_NUMBER_SOURCE.replace(
                "if (anchor.hasAttribute('stop')) return (count += 2, count);",
                "throw (count += 2, count);",
            ),
            "body-throw-close-throws": variants["body-throw"].replace("return {};", "throw 2;"),
            "body-throw-close-getter": variants["body-throw"]
            .replace("return() {", "get return() {")
            .replace("return {};", "throw 2;"),
            "body-throw-branch-boolean-snapshot": BODY_RETURN_BRANCH_EXPRESSION_SOURCE.replace(
                "return (node.setAttribute", "throw (node.setAttribute", 1
            ),
            "body-throw-branch-number-snapshot": BODY_RETURN_BRANCH_NUMBER_SOURCE.replace(
                "return (count += 2, count);", "throw (count += 2, count);", 1
            ),
            "body-throw-or-return-snapshot": BODY_RETURN_MULTIPLE_SOURCE.replace(
                "return (node.setAttribute", "throw (node.setAttribute", 1
            ),
            # Abrupt completion wins even when return() produces a primitive.
            "body-throw-close-primitive": variants["body-throw"].replace("return {};", "return 2;"),
        }
    )
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
            "entry-captured-sibling-returned-loop-unknown-initial": SIBLING_RETURNED_LOOP_JOIN_SOURCE.replace(
                "let selected = writer;", "let selected = unknownWriter;", 1
            ),
            "entry-captured-sibling-returned-loop-scalar-initial": SIBLING_RETURNED_LOOP_JOIN_SOURCE.replace(
                "let selected = writer;", "let selected = 0;", 1
            ),
            "entry-captured-sibling-returned-loop-unknown-backedge": SIBLING_RETURNED_LOOP_JOIN_SOURCE.replace(
                "selected = other;", "selected = unknownWriter;", 1
            ),
            "entry-captured-sibling-returned-loop-scalar-backedge": SIBLING_RETURNED_LOOP_JOIN_SOURCE.replace(
                "selected = other;", "selected = 0;", 1
            ),
            "entry-captured-sibling-returned-loop-observed": SIBLING_RETURNED_LOOP_JOIN_SOURCE.replace(
                "      rounds++;", "      closed += selected === other;\n      rounds++;", 1
            ),
            "entry-captured-sibling-returned-loop-call-unknown": SIBLING_RETURNED_LOOP_CALL_SOURCE.replace(
                "const keep = (writer) => writer;",
                "const keep = (writer) => unknownWriter;",
                1,
            ),
            "entry-captured-sibling-returned-loop-call-mixed": SIBLING_RETURNED_LOOP_CALL_SOURCE.replace(
                "const keep = (writer) => writer;",
                "const keep = (writer) => { if (emitted > 0) return writer; return 0; };",
                1,
            ),
            "entry-captured-sibling-returned-loop-call-escaping": SIBLING_RETURNED_LOOP_CALL_SOURCE.replace(
                "const keep = (writer) => writer;",
                "const keep = (writer) => { anchor.saved = writer; return writer; };",
                1,
            ),
            "entry-captured-sibling-returned-loop-call-observed": SIBLING_RETURNED_LOOP_CALL_SOURCE.replace(
                "const keep = (writer) => writer;",
                "const keep = (writer) => { closed += writer === other; return writer; };",
                1,
            ),
            "entry-captured-sibling-returned-loop-call-missing": SIBLING_RETURNED_LOOP_CALL_SOURCE.replace(
                "selected = keep(selected);", "selected = keep();", 1
            ),
            "entry-captured-sibling-returned-loop-call-scalar-actual": SIBLING_RETURNED_LOOP_CALL_SOURCE.replace(
                "selected = keep(selected);", "selected = keep(closed);", 1
            ),
            "entry-captured-sibling-returned-loop-call-recursive-effect": SIBLING_RETURNED_LOOP_CALL_SOURCE.replace(
                "const keep = (writer) => writer;",
                "const keep = (writer) => { closed += keep(writer)(); return writer; };",
                1,
            ),
            "entry-captured-sibling-returned-loop-forwarded-unknown": SIBLING_RETURNED_LOOP_FORWARDED_SOURCE.replace(
                "forward(writer);", "forward(unknownWriter);", 1
            ),
            "entry-captured-sibling-returned-loop-forwarded-missing": SIBLING_RETURNED_LOOP_FORWARDED_SOURCE.replace(
                "forward(writer);", "forward();", 1
            ),
            "entry-captured-sibling-returned-loop-forwarded-escaping": SIBLING_RETURNED_LOOP_FORWARDED_SOURCE.replace(
                "const forward = (writer) => writer;",
                "const forward = (writer) => { anchor.saved = writer; return writer; };",
                1,
            ),
            "entry-captured-sibling-returned-loop-forwarded-observed": SIBLING_RETURNED_LOOP_FORWARDED_SOURCE.replace(
                "const forward = (writer) => writer;",
                "const forward = (writer) => { closed += writer === other; return writer; };",
                1,
            ),
            "entry-captured-sibling-returned-loop-forwarded-recursive-effect": SIBLING_RETURNED_LOOP_FORWARDED_SOURCE.replace(
                "const forward = (writer) => writer;",
                "const forward = (writer) => { closed += forward(writer)(); return writer; };",
                1,
            ),
            "entry-captured-sibling-returned-loop-branch-unknown-arm": SIBLING_RETURNED_LOOP_BRANCH_SOURCE.replace(
                "if (emitted > 0) selected = other; return selected;",
                "if (emitted > 0) selected = unknownWriter; return selected;",
                1,
            ),
            "entry-captured-sibling-returned-loop-branch-scalar-arm": SIBLING_RETURNED_LOOP_BRANCH_SOURCE.replace(
                "const forward = (writer) => { let selected = writer;",
                "const forward = (writer) => { let selected = 0;",
                1,
            ),
            "entry-captured-sibling-returned-loop-branch-missing": SIBLING_RETURNED_LOOP_BRANCH_SOURCE.replace(
                "forward(writer);", "forward();", 1
            ),
            "entry-captured-sibling-returned-loop-branch-escaping": SIBLING_RETURNED_LOOP_BRANCH_SOURCE.replace(
                "selected = other; return selected;",
                "selected = other; anchor.saved = selected; return selected;",
                1,
            ),
            "entry-captured-sibling-returned-loop-branch-observed": SIBLING_RETURNED_LOOP_BRANCH_SOURCE.replace(
                "selected = other; return selected;",
                "selected = other; closed += selected === other; return selected;",
                1,
            ),
            "entry-captured-sibling-returned-loop-branch-recursive-effect": SIBLING_RETURNED_LOOP_BRANCH_SOURCE.replace(
                "selected = other; return selected;",
                "selected = other; closed += forward(writer)(); return selected;",
                1,
            ),
            "entry-captured-sibling-returned-formal-callee-unknown": SIBLING_RETURNED_FORMAL_CALLEE_SOURCE.replace(
                "keep(selected, forward);", "keep(selected, unknownChooser);", 1
            ),
            "entry-captured-sibling-returned-formal-callee-scalar": SIBLING_RETURNED_FORMAL_CALLEE_SOURCE.replace(
                "keep(selected, forward);", "keep(selected, 0);", 1
            ),
            "entry-captured-sibling-returned-formal-callee-missing": SIBLING_RETURNED_FORMAL_CALLEE_SOURCE.replace(
                "keep(selected, forward);", "keep(selected);", 1
            ),
            "entry-captured-sibling-returned-formal-callee-escaping": SIBLING_RETURNED_FORMAL_CALLEE_SOURCE.replace(
                "=> chooser(writer);", "=> { anchor.saved = chooser; return chooser(writer); };", 1
            ),
            "entry-captured-sibling-returned-formal-callee-observed": SIBLING_RETURNED_FORMAL_CALLEE_SOURCE.replace(
                "=> chooser(writer);",
                "=> { closed += chooser === forward; return chooser(writer); };",
                1,
            ),
            "entry-captured-sibling-returned-formal-callee-recursive": SIBLING_RETURNED_FORMAL_CALLEE_SOURCE.replace(
                "keep(selected, forward);", "keep(selected, keep);", 1
            ),
            "entry-captured-sibling-returned-selected-callee-unknown-arm": SIBLING_RETURNED_SELECTED_CALLEE_SNAPSHOTS_SOURCE.replace(
                "selected = passthrough;", "selected = unknownChooser;", 1
            ),
            "entry-captured-sibling-returned-selected-callee-scalar-arm": SIBLING_RETURNED_SELECTED_CALLEE_SNAPSHOTS_SOURCE.replace(
                "selected = passthrough;", "selected = 0;", 1
            ),
        }
    )
    return variants


def protected_attribute(args, compilers, includes, libraries):
    # Exercise typed suppression separately from the saved source throw.
    source = """module {
  ctjs.func @protectedAttribute$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value, %other: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %same = ctjs.compare strict_eq %element, %other
    %condition = ctjs.truthy %same
    scf.if %condition {
      %key = ctjs.constant #ctjs.string<"setAttribute">
      %name = ctjs.constant #ctjs.string<"data-closed">
      %text = ctjs.constant #ctjs.string<"yes">
      %method = ctjs.get_property %element[%key]
      "ctjs.invoke"() ({
        %called = ctjs.call %method(%element, %name, %text)
        ctjs.invoke_exit %called state()
      }, {
      ^bb0(%ignored: !ctjs.value):
        ctjs.invoke_yield()
      }, {
      ^bb0(%error: !ctjs.value):
        ctjs.invoke_yield()
      }) : () -> ()
      scf.yield
    }
    ctjs.return %same
  }
}
"""
    checks = r"""
        assert(@ENTRY@(alias, alias));
        const auto closed = atoms.intern("@ATTRIBUTE@");
        assert(doc.read().attribute_value(button, closed) == "yes");
        const auto writes = doc.take_writes();
        assert(writes.size() == 1 && writes[0].node == button && writes[0].name == closed);
        assert(!@ENTRY@(alias, foreign) && doc.take_writes().empty());
        bool rejected = false;
        try { (void)@ENTRY@(alias, element_ref{}); }
        catch (const std::bad_expected_access<dom_error> & error) {
            rejected = error.error() == dom_error::no_such_node;
        }
        assert(rejected && doc.take_writes().empty());
        (void)pressed;
"""
    owned_checks = r"""
        @ENTRY@_session session;
        auto & owned = session.document();
        const auto owned_button = owned.create_element(owned.atoms().intern("button"));
        const auto owned_other = owned.create_element(owned.atoms().intern("button"));
        const element_ref owned_element{&owned, owned_button};
        owned.log_writes(true);
        assert(session.invoke(owned_element, owned_element));
        const auto owned_closed = owned.atoms().intern("@ATTRIBUTE@");
        assert(owned.read().attribute_value(owned_button, owned_closed) == "yes");
        const auto owned_writes = owned.take_writes();
        assert(owned_writes.size() == 1 && owned_writes[0].node == owned_button &&
               owned_writes[0].name == owned_closed);
        assert(!session.invoke(owned_element, element_ref{&owned, owned_other}));
        assert(owned.take_writes().empty());
"""
    cases = {
        "valid": source,
        "digit-name": source.replace("data-closed", "1:closed"),
        "invalid-name": source.replace("data-closed", "bad name"),
        "empty-name": source.replace("data-closed", ""),
        "computed-name": source.replace(
            '%name = ctjs.constant #ctjs.string<"data-closed">',
            """%name = scf.if %condition -> (!ctjs.value) {
        %first = ctjs.constant #ctjs.string<"data-closed">
        scf.yield %first : !ctjs.value
      } else {
        %second = ctjs.constant #ctjs.string<"other">
        scf.yield %second : !ctjs.value
      }""",
        ),
        "wrong-receiver": source.replace("%method(%element,", "%method(%text,"),
        "observed-payload": source.replace(
            "^bb0(%ignored: !ctjs.value):",
            "^bb0(%ignored: !ctjs.value):\n        %observed = ctjs.unary typeof %ignored",
        ),
    }
    executions = refused = 0
    for label, text in cases.items():
        accepted = label in ("valid", "digit-name")
        ir = args.work / f"protected-attribute-{label}.mlir"
        ir.write_text(text)
        contract = {
            "version": 1,
            "module_sha256": dom.fingerprint(args.opt, ir),
            "entry": "protectedAttribute$0",
            "element_parameters": [0, 1],
        }
        for owned in (False, True):
            manifest = dict(
                contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            )
            for optimize in (False, True):
                name = f"protected-attribute-{label}-{owned}-{optimize}"
                native = dom.lower(args, ir, manifest, name, optimize=optimize, success=accepted)
                if not accepted:
                    refused += 1
                    continue
                dom.standalone(
                    args,
                    native,
                    name,
                    (checks + (owned_checks if owned else "")).replace(
                        "@ATTRIBUTE@", "1:closed" if label == "digit-name" else "data-closed"
                    ),
                    compilers,
                    includes,
                    libraries,
                )
                executions += 2 * len(compilers)
    print(f"Protected raw DOM attribute: {executions} native executions, {refused} refusals")


def saved_throws(args, compilers, includes, libraries):
    # Retain the historical source inventory; execute every promoted body here.
    original = refusals()["body-throw"]
    conditional_source = refusals()["body-throw-branch-boolean-snapshot"]
    mutable_throwing_source = refusals()["body-throw-number-snapshot"].replace(
        "return {};", "throw 2;"
    )
    nonliteral_throwing_source = mutable_throwing_source.replace("throw 2;", "throw count;")
    postwrite_throwing_source = conditional_source.replace(
        "return {};", "throw anchor.hasAttribute('data-closed');"
    )
    second_postwrite_throwing_source = postwrite_throwing_source.replace(
        "throw anchor.hasAttribute('data-closed');",
        "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed')); throw false;",
    )
    selector_throwing_source = second_postwrite_throwing_source.replace(
        "throw false;", "throw anchor.matches('[data-closed]');"
    )
    third_postselector_throwing_source = selector_throwing_source.replace(
        "throw anchor.matches('[data-closed]');",
        "anchor.matches('[data-closed]'); "
        "anchor.setAttribute('data-closed', false); throw false;",
    )
    selector_result_throwing_source = third_postselector_throwing_source.replace(
        "anchor.matches('[data-closed]'); anchor.setAttribute('data-closed', false);",
        "const matched = anchor.matches('[data-closed]'); "
        "anchor.setAttribute('data-closed', matched);",
    )
    postselector_read_throwing_source = third_postselector_throwing_source.replace(
        "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
        "anchor.setAttribute('data-closed', anchor.hasAttribute('data-unvisited'));",
    )
    postselector_read_throwing_source = postselector_read_throwing_source.replace(
        "'[data-closed]'", "'[data-closed=false]'"
    ).replace(
        "anchor.setAttribute('data-closed', false);",
        "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
    )
    fourth_postselector_throwing_source = postselector_read_throwing_source.replace(
        "throw false;", "anchor.setAttribute('data-closed', false); throw false;"
    )
    fourth_read_throwing_source = fourth_postselector_throwing_source.replace(
        "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
        "anchor.setAttribute('data-after-selector', anchor.hasAttribute('data-closed'));",
    ).replace(
        "anchor.setAttribute('data-closed', false);",
        "anchor.setAttribute('data-closed', anchor.hasAttribute('data-after-selector'));",
    )
    sequence_throwing_source = fourth_postselector_throwing_source.replace(
        "throw false;",
        "anchor.setAttribute('data-after-selector', anchor.hasAttribute('data-closed')); "
        "anchor.setAttribute('data-closed', anchor.hasAttribute('data-after-selector')); throw false;",
    )
    terminal_postselector_throwing_source = fourth_postselector_throwing_source.replace(
        "throw false;", "throw anchor.hasAttribute('data-closed');"
    )
    terminal_match_throwing_source = terminal_postselector_throwing_source.replace(
        "throw anchor.hasAttribute('data-closed');",
        "throw anchor.matches('[data-closed=false]');",
    )
    terminal_match_read_throwing_source = terminal_match_throwing_source.replace(
        "throw anchor.matches('[data-closed=false]');",
        "anchor.matches('[data-closed=false]'); throw anchor.hasAttribute('data-closed');",
    )
    terminal_read_sequence_source = terminal_match_read_throwing_source.replace(
        "throw anchor.hasAttribute('data-closed');",
        "anchor.hasAttribute('data-closed'); throw anchor.hasAttribute('data-unvisited');",
    )
    terminal_write_source = terminal_read_sequence_source.replace(
        "throw anchor.hasAttribute('data-unvisited');",
        "anchor.hasAttribute('data-unvisited'); anchor.setAttribute('data-after-terminal', false); throw false;",
    )
    terminal_write_value_source = terminal_write_source.replace(
        "anchor.hasAttribute('data-unvisited'); anchor.setAttribute('data-after-terminal', false);",
        "anchor.setAttribute('data-after-terminal', anchor.hasAttribute('data-unvisited'));",
    )
    terminal_write_earlier_value_source = terminal_write_value_source.replace(
        "anchor.hasAttribute('data-closed'); anchor.setAttribute('data-after-terminal', anchor.hasAttribute('data-unvisited'));",
        "const present = anchor.hasAttribute('data-closed'); anchor.hasAttribute('data-unvisited'); anchor.setAttribute('data-after-terminal', present);",
    )
    terminal_write_selector_value_source = terminal_write_earlier_value_source.replace(
        "anchor.matches('[data-closed=false]'); const present = anchor.hasAttribute('data-closed');",
        "const present = anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-closed');",
    )
    before_selector_read_source = terminal_write_earlier_value_source.replace(
        "anchor.matches('[data-closed=false]'); const present = anchor.hasAttribute('data-closed');",
        "const present = anchor.hasAttribute('data-closed'); anchor.matches('[data-closed=false]');",
    )
    before_first_selector_read_source = before_selector_read_source.replace(
        "const present = anchor.hasAttribute('data-closed'); ", ""
    ).replace(
        "anchor.matches('[data-closed=false]');",
        "const present = anchor.hasAttribute('data-closed'); anchor.matches('[data-closed=false]');",
        1,
    )
    before_second_write_read_source = before_first_selector_read_source.replace(
        "const present = anchor.hasAttribute('data-closed'); ", ""
    ).replace(
        "anchor.setAttribute('data-closed', anchor.hasAttribute('data-unvisited'));",
        "const present = anchor.hasAttribute('data-closed'); anchor.setAttribute('data-closed', anchor.hasAttribute('data-unvisited'));",
    )
    before_first_write_read_source = before_second_write_read_source.replace(
        "const present = anchor.hasAttribute('data-closed'); ", ""
    ).replace(
        "anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
        "const present = anchor.hasAttribute('data-closed'); anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
    )
    inside_first_write_read_source = before_first_write_read_source.replace(
        "const present = anchor.hasAttribute('data-closed'); anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
        "let present; anchor.setAttribute('data-closed', (present = anchor.hasAttribute('data-closed'), anchor.hasAttribute('data-visited')));",
    )
    feeding_first_read_source = inside_first_write_read_source.replace(
        "let present; anchor.setAttribute('data-closed', (present = anchor.hasAttribute('data-closed'), anchor.hasAttribute('data-visited')));",
        "let present, feeding; anchor.setAttribute('data-closed', (feeding = anchor.hasAttribute('data-visited'), present = anchor.hasAttribute('data-closed'), feeding));",
    )
    final_argument_read_source = feeding_first_read_source.replace(
        "anchor.setAttribute('data-after-terminal', present);",
        "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
    )
    selector_final_argument_read_source = terminal_write_selector_value_source.replace(
        "anchor.setAttribute('data-after-terminal', present);",
        "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
    )
    selector_inside_final_argument_source = selector_final_argument_read_source.replace(
        "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
        "anchor.setAttribute('data-after-terminal', (anchor.matches('[data-closed=false]'), present));",
    )
    selector_inside_first_argument_source = selector_inside_final_argument_source.replace(
        "anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
        "anchor.setAttribute('data-closed', anchor.matches('[data-closed=false]'));",
    )
    selector_before_first_write_source = selector_inside_first_argument_source.replace(
        "anchor.setAttribute('data-closed', anchor.matches('[data-closed=false]'));",
        "const first = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', first);",
    )
    selector_reused_second_write_source = selector_before_first_write_source.replace(
        "anchor.setAttribute('data-closed', anchor.hasAttribute('data-unvisited'));",
        "anchor.setAttribute('data-closed', first);",
    )
    selector_guarded_second_write_source = selector_reused_second_write_source.replace(
        "anchor.setAttribute('data-closed', first); anchor.matches",
        "if (first) anchor.setAttribute('data-closed', first); anchor.matches",
    )
    selector_guarded_second_read_source = selector_guarded_second_write_source.replace(
        "if (first) anchor.setAttribute('data-closed', first);",
        "if (first) anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
    )
    selector_guarded_second_two_reads_source = selector_guarded_second_read_source.replace(
        "if (first) anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
        "if (first) anchor.setAttribute('data-closed', (anchor.hasAttribute('data-visited'), anchor.hasAttribute('data-unvisited')));",
    )
    selector_guarded_second_postread_source = selector_guarded_second_two_reads_source.replace(
        "if (first) anchor.setAttribute('data-closed', (anchor.hasAttribute('data-visited'), anchor.hasAttribute('data-unvisited')));",
        "if (first) { anchor.setAttribute('data-closed', (anchor.hasAttribute('data-visited'), anchor.hasAttribute('data-unvisited'))); anchor.hasAttribute('data-closed'); }",
    )
    cases = (
        ("number", original, "js_num", "error.value.value() == 1.0", "yes"),
        (
            "number-primitive-close",
            refusals()["body-throw-close-primitive"],
            "js_num",
            "error.value.value() == 1.0",
            "yes",
        ),
        (
            "number-throwing-close",
            refusals()["body-throw-close-throws"],
            "js_num",
            "error.value.value() == 1.0",
            "yes",
        ),
        (
            "number-getter-close",
            refusals()["body-throw-close-getter"],
            "js_num",
            "error.value.value() == 1.0",
            "yes",
        ),
        (
            "boolean",
            original.replace("throw 1;", "throw false;"),
            "js_boolean_t",
            "!error.value",
            "yes",
        ),
        (
            "boolean-snapshot",
            refusals()["body-throw-boolean-snapshot"],
            "js_boolean_t",
            "!error.value",
            "yes",
        ),
        (
            "conditional-boolean-snapshot",
            conditional_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
                "anchor.setAttribute('data-closed', 'yes');",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "yes",
        ),
        (
            "conditional-boolean-read",
            conditional_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-primitive-close",
            conditional_source.replace("return {};", "return 2;"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-throwing-close",
            conditional_source.replace("return {};", "throw 2;"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-getter-close",
            conditional_source.replace("return() {", "get return() {").replace(
                "return {};", "throw 2;"
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "boolean-snapshot-primitive-close",
            refusals()["body-throw-boolean-snapshot"].replace("return {};", "return 2;"),
            "js_boolean_t",
            "!error.value",
            "yes",
        ),
        (
            "boolean-snapshot-throwing-close",
            refusals()["body-throw-boolean-snapshot"].replace("return {};", "throw 2;"),
            "js_boolean_t",
            "!error.value",
            "yes",
        ),
        (
            "boolean-snapshot-getter-close",
            refusals()["body-throw-boolean-snapshot"]
            .replace("return() {", "get return() {")
            .replace("return {};", "throw 2;")
            .replace("'data-closed', 'yes'", "'data-closed', 'getter'"),
            "js_boolean_t",
            "!error.value",
            "getter",
        ),
        (
            "conditional-boolean-missing-read",
            conditional_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-unvisited'));",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-throw-or-return",
            refusals()["body-throw-or-return-snapshot"],
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "string",
            original.replace("throw 1;", "throw 'saved';"),
            "js_string",
            'error.value.value() == "saved"',
            "yes",
        ),
        (
            "number-snapshot",
            refusals()["body-throw-number-snapshot"],
            "js_num",
            "error.value.value() == 3.0",
            "yes",
        ),
        (
            "conditional-number-snapshot",
            refusals()["body-throw-branch-number-snapshot"],
            "js_num",
            "error.value.value() == 3.0",
            "yes",
        ),
        (
            "mutable-number-throwing-close",
            mutable_throwing_source,
            "js_num",
            "error.value.value() == 3.0",
            "yes",
        ),
        (
            "conditional-mutable-number-throwing-close",
            refusals()["body-throw-branch-number-snapshot"].replace("return {};", "throw 2;"),
            "js_num",
            "error.value.value() == 3.0",
            "yes",
        ),
        (
            "mutable-number-throwing-close-observes-state",
            mutable_throwing_source.replace(
                "anchor.setAttribute('data-closed', 'yes');",
                "anchor.setAttribute('data-closed', count === 13);",
            ),
            "js_num",
            "error.value.value() == 3.0",
            "true",
        ),
        (
            "mutable-number-nonliteral-close",
            nonliteral_throwing_source,
            "js_num",
            "error.value.value() == 3.0",
            "yes",
        ),
        (
            "mutable-number-nonliteral-close-observes-state",
            nonliteral_throwing_source.replace(
                "anchor.setAttribute('data-closed', 'yes');",
                "anchor.setAttribute('data-closed', count === 13);",
            ),
            "js_num",
            "error.value.value() == 3.0",
            "true",
        ),
        (
            "mutable-number-nonliteral-getter-close",
            nonliteral_throwing_source.replace("return() {", "get return() {"),
            "js_num",
            "error.value.value() == 3.0",
            "yes",
        ),
        (
            "mutable-number-nonliteral-close-producer",
            nonliteral_throwing_source.replace(
                "anchor.setAttribute('data-closed', 'yes');\n      throw count;",
                "throw (anchor.setAttribute('data-closed', count === 13), count);",
            ),
            "js_num",
            "error.value.value() == 3.0",
            "true",
        ),
        (
            "conditional-boolean-nonliteral-close",
            postwrite_throwing_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-postwrite-getter-close",
            postwrite_throwing_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-postwrite-missing-read",
            postwrite_throwing_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-unvisited'));",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-close",
            second_postwrite_throwing_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-second-postwrite-getter-close",
            second_postwrite_throwing_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-second-postwrite-missing-read",
            second_postwrite_throwing_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-unvisited'));",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-close",
            selector_throwing_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-second-postwrite-selector-getter-close",
            selector_throwing_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-second-postwrite-selector-missing-read",
            selector_throwing_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-unvisited'));",
            ).replace("'[data-closed]'", "'[data-closed=false]'"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-close",
            third_postselector_throwing_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-getter-close",
            third_postselector_throwing_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-missing-read",
            third_postselector_throwing_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-unvisited'));",
            )
            .replace("'[data-closed]'", "'[data-closed=false]'")
            .replace(
                "anchor.setAttribute('data-closed', false);",
                "anchor.setAttribute('data-closed', true);",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-result-close",
            selector_result_throwing_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-result-getter-close",
            selector_result_throwing_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-result-missing-read",
            selector_result_throwing_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-unvisited'));",
            ).replace("'[data-closed]'", "'[data-closed=true]'"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-close",
            postselector_read_throwing_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-getter-close",
            postselector_read_throwing_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-missing-read",
            postselector_read_throwing_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-unvisited'));",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-close",
            fourth_postselector_throwing_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-getter-close",
            fourth_postselector_throwing_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-read-close",
            fourth_read_throwing_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-sixth-read-close",
            sequence_throwing_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-close",
            terminal_postselector_throwing_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-getter-close",
            terminal_postselector_throwing_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-missing-read",
            terminal_postselector_throwing_source.replace(
                "throw anchor.hasAttribute('data-closed');",
                "throw anchor.hasAttribute('data-unvisited');",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-read-terminal-close",
            fourth_read_throwing_source.replace(
                "throw false;", "throw anchor.hasAttribute('data-after-selector');"
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-close",
            terminal_match_throwing_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-getter-close",
            terminal_match_throwing_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-order-close",
            terminal_match_throwing_source.replace(
                "anchor.setAttribute('data-closed', false);",
                "anchor.setAttribute('data-closed', true);",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-close",
            terminal_match_read_throwing_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-getter-close",
            terminal_match_read_throwing_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-missing-read",
            terminal_match_read_throwing_source.replace(
                "throw anchor.hasAttribute('data-closed');",
                "throw anchor.hasAttribute('data-unvisited');",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-order-close",
            terminal_match_read_throwing_source.replace(
                "anchor.setAttribute('data-closed', false);",
                "anchor.setAttribute('data-closed', true);",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "true",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-close",
            terminal_read_sequence_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-getter-close",
            terminal_read_sequence_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-missing-read",
            terminal_read_sequence_source.replace(
                "anchor.hasAttribute('data-closed'); throw",
                "anchor.hasAttribute('data-unvisited'); throw",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-order-close",
            terminal_read_sequence_source.replace(
                "anchor.hasAttribute('data-closed'); throw anchor.hasAttribute('data-unvisited');",
                "anchor.hasAttribute('data-unvisited'); throw anchor.hasAttribute('data-closed');",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-wider-close",
            terminal_read_sequence_source.replace(
                "throw anchor.hasAttribute('data-unvisited');",
                "anchor.hasAttribute('data-unvisited'); anchor.hasAttribute('data-visited'); "
                "throw anchor.hasAttribute('data-yielded');",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-alternating-close",
            terminal_read_sequence_source.replace(
                "throw anchor.hasAttribute('data-unvisited');",
                "anchor.matches('[data-closed=false]'); throw anchor.hasAttribute('data-unvisited');",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-close",
            terminal_write_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-getter-close",
            terminal_write_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-order-close",
            terminal_write_source.replace(
                "anchor.hasAttribute('data-closed'); anchor.hasAttribute('data-unvisited');",
                "anchor.hasAttribute('data-unvisited'); anchor.hasAttribute('data-closed');",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-value-close",
            terminal_write_value_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-value-getter-close",
            terminal_write_value_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-value-order-close",
            terminal_write_value_source.replace(
                "anchor.hasAttribute('data-closed'); anchor.setAttribute('data-after-terminal', anchor.hasAttribute('data-unvisited'));",
                "anchor.hasAttribute('data-unvisited'); anchor.setAttribute('data-after-terminal', anchor.hasAttribute('data-closed'));",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-earlier-value-close",
            terminal_write_earlier_value_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-earlier-value-getter-close",
            terminal_write_earlier_value_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-earlier-value-order-close",
            terminal_write_earlier_value_source.replace(
                "const present = anchor.hasAttribute('data-closed'); anchor.hasAttribute('data-unvisited');",
                "const present = anchor.hasAttribute('data-unvisited'); anchor.hasAttribute('data-closed');",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-value-close",
            terminal_write_selector_value_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-value-getter-close",
            terminal_write_selector_value_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-value-order-close",
            terminal_write_selector_value_source.replace(
                "anchor.setAttribute('data-closed', false); const present = anchor.matches('[data-closed=false]');",
                "const present = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', false);",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-selector-value-close",
            before_selector_read_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-selector-value-getter-close",
            before_selector_read_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-selector-value-order-close",
            before_selector_read_source.replace(
                "const present = anchor.hasAttribute('data-closed'); anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-unvisited');",
                "const present = anchor.hasAttribute('data-unvisited'); anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-closed');",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-first-selector-value-close",
            before_first_selector_read_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-first-selector-value-getter-close",
            before_first_selector_read_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-first-selector-value-order-close",
            before_first_selector_read_source.replace(
                "const present = anchor.hasAttribute('data-closed');",
                "const present = anchor.hasAttribute('data-unvisited');",
            ).replace(
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-unvisited');",
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-closed');",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-second-write-value-close",
            before_second_write_read_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-second-write-value-getter-close",
            before_second_write_read_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-second-write-value-order-close",
            before_second_write_read_source.replace(
                "const present = anchor.hasAttribute('data-closed');",
                "const present = anchor.hasAttribute('data-unvisited');",
            ).replace(
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-unvisited');",
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-closed');",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-first-write-value-close",
            before_first_write_read_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-first-write-value-getter-close",
            before_first_write_read_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-first-write-value-order-close",
            before_first_write_read_source.replace(
                "const present = anchor.hasAttribute('data-closed');",
                "const present = anchor.hasAttribute('data-unvisited');",
            ).replace(
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-unvisited');",
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-closed');",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-inside-first-write-value-close",
            inside_first_write_read_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-inside-first-write-value-getter-close",
            inside_first_write_read_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-inside-first-write-value-order-close",
            inside_first_write_read_source.replace(
                "present = anchor.hasAttribute('data-closed')",
                "present = anchor.hasAttribute('data-unvisited')",
            ).replace(
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-unvisited');",
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-closed');",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-feeding-first-value-close",
            feeding_first_read_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-feeding-first-value-getter-close",
            feeding_first_read_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-feeding-first-value-order-close",
            feeding_first_read_source.replace(
                "present = anchor.hasAttribute('data-closed')",
                "present = anchor.hasAttribute('data-unvisited')",
            ).replace(
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-unvisited');",
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-closed');",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-final-argument-value-close",
            final_argument_read_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-final-argument-value-getter-close",
            final_argument_read_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-final-argument-value-order-close",
            final_argument_read_source.replace(
                "present = anchor.hasAttribute('data-closed')",
                "present = anchor.hasAttribute('data-unvisited')",
            ).replace(
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-unvisited');",
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-closed');",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-final-argument-value-close",
            selector_final_argument_read_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-final-argument-value-getter-close",
            selector_final_argument_read_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-final-argument-value-order-close",
            selector_final_argument_read_source.replace(
                "anchor.setAttribute('data-closed', false); const present = anchor.matches('[data-closed=false]');",
                "const present = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', false);",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-inside-final-argument-value-close",
            selector_inside_final_argument_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-inside-final-argument-value-getter-close",
            selector_inside_final_argument_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-inside-final-argument-value-order-close",
            selector_inside_final_argument_source.replace(
                "anchor.setAttribute('data-closed', false); const present = anchor.matches('[data-closed=false]');",
                "const present = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', false);",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-inside-first-argument-value-close",
            selector_inside_first_argument_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-inside-first-argument-value-getter-close",
            selector_inside_first_argument_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-inside-first-argument-value-order-close",
            selector_inside_first_argument_source.replace(
                "anchor.setAttribute('data-closed', false); const present = anchor.matches('[data-closed=false]');",
                "const present = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', false);",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-before-first-value-close",
            selector_before_first_write_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-before-first-value-getter-close",
            selector_before_first_write_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-before-first-value-order-close",
            selector_before_first_write_source.replace(
                "anchor.setAttribute('data-closed', false); const present = anchor.matches('[data-closed=false]');",
                "const present = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', false);",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-reused-second-value-close",
            selector_reused_second_write_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-reused-second-value-getter-close",
            selector_reused_second_write_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-reused-second-value-order-close",
            selector_reused_second_write_source.replace(
                "anchor.setAttribute('data-closed', false); const present = anchor.matches('[data-closed=false]');",
                "const present = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', false);",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-value-close",
            selector_guarded_second_write_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-value-getter-close",
            selector_guarded_second_write_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-value-order-close",
            selector_guarded_second_write_source.replace(
                "anchor.setAttribute('data-closed', false); const present = anchor.matches('[data-closed=false]');",
                "const present = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', false);",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-read-value-close",
            selector_guarded_second_read_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-read-value-getter-close",
            selector_guarded_second_read_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-read-value-order-close",
            selector_guarded_second_read_source.replace(
                "anchor.setAttribute('data-closed', false); const present = anchor.matches('[data-closed=false]');",
                "const present = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', false);",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-read-value-matches-close",
            selector_guarded_second_read_source.replace(
                "if (first) anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
                "if (first) anchor.setAttribute('data-closed', anchor.matches('[data-visited]'));",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-two-reads-value-close",
            selector_guarded_second_two_reads_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-two-reads-value-getter-close",
            selector_guarded_second_two_reads_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-two-reads-value-order-close",
            selector_guarded_second_two_reads_source.replace(
                "anchor.setAttribute('data-closed', false); const present = anchor.matches('[data-closed=false]');",
                "const present = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', false);",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-two-reads-value-matches-close",
            selector_guarded_second_two_reads_source.replace(
                "if (first) anchor.setAttribute('data-closed', (anchor.hasAttribute('data-visited'), anchor.hasAttribute('data-unvisited')));",
                "if (first) anchor.setAttribute('data-closed', (anchor.matches('[data-visited]'), anchor.hasAttribute('data-visited'), anchor.hasAttribute('data-unvisited')));",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-postread-value-close",
            selector_guarded_second_postread_source,
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-postread-value-getter-close",
            selector_guarded_second_postread_source.replace("return() {", "get return() {"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-postread-value-order-close",
            selector_guarded_second_postread_source.replace(
                "anchor.setAttribute('data-closed', false); const present = anchor.matches('[data-closed=false]');",
                "const present = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', false);",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-postread-value-matches-close",
            selector_guarded_second_postread_source.replace(
                "anchor.hasAttribute('data-closed'); }",
                "anchor.matches('[data-closed=true]'); }",
            ),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-reused-second-value-throw-close",
            selector_reused_second_write_source.replace("throw false;", "throw first;"),
            "js_boolean_t",
            "static_cast<bool>(error.value) == (repetition != 0)",
            "false",
        ),
        (
            "number-close-observes-state",
            refusals()["body-throw-number-snapshot"].replace(
                "anchor.setAttribute('data-closed', 'yes');",
                "anchor.setAttribute('data-closed', count === 13);",
            ),
            "js_num",
            "error.value.value() == 3.0",
            "true",
        ),
    )
    checks = r"""
        (void)pressed;
        const auto exercise = [&](ctbrowser::document & target, node_id id, auto && call) {
            const auto next = target.atoms().intern("data-next");
            const auto yielded = target.atoms().intern("data-yielded");
            const auto closed = target.atoms().intern("data-closed");
            const auto visited = target.atoms().intern("data-visited");
            target.log_writes(true);
            for (unsigned repetition = 0; repetition < 2; ++repetition) {
                for (auto name : {next, yielded, closed, visited}) {
                    assert(target.remove_attribute(id, name));
                }
                @SETUP@
                (void)target.take_writes();
                bool caught = false;
                try { (void)call(); }
                catch (const ctnative::js_exception<ctnative::@TYPE@> & error) {
                    caught = @PAYLOAD@;
                }
                assert(caught);
                const auto writes = target.take_writes();
                assert(writes.size() == @WRITE_COUNT@ && writes[0].name == next &&
                       writes[1].name == yielded && writes[@CLOSED_INDEX@].name == closed);
                @SECOND_CLOSE@
                for (const auto & write : writes) { assert(write.node == id && !write.text); }
                assert(target.read().attribute_value(id, next) == "false");
                assert(target.read().attribute_value(id, closed) == "@CLOSED@");
                assert(target.read().has_attribute(id, visited) == @VISITED@);
                if (@VISITED@) {
                    assert(writes[2].name == visited);
                    assert(target.read().attribute_value(id, visited) == "yes");
                }
                assert(target.remove_attribute(id, closed));
                (void)target.take_writes();
                assert(@EXHAUSTED@);
                const auto exhausted = target.take_writes();
                assert(exhausted.size() == 2 && exhausted[0].name == next &&
                       exhausted[1].name == yielded);
                assert(target.read().attribute_value(id, next) == "true");
                assert(!target.read().has_attribute(id, closed));
                @NORMAL@
            }
        };
        exercise(doc, button, [&] { return @ENTRY@(alias); });
        exercise(foreign_doc, other_button, [&] { return @ENTRY@(foreign); });
"""
    executions = refused = observations = 0
    for label, text, kind, payload, closed in cases:
        mixed = label == "conditional-boolean-throw-or-return"
        second_close = label.startswith("conditional-boolean-second-postwrite-")
        third_close = "-selector-third-" in label
        postselector_read = "-selector-third-read-" in label
        terminal_match = "-terminal-match-" in label
        terminal_match_read = "-terminal-match-read-" in label
        terminal_write = "-terminal-match-read-sequence-write-" in label
        terminal_write_value = "-terminal-match-read-sequence-write-value-" in label
        selector_guarded_second_postread = "-write-selector-guarded-second-postread-value-" in label
        guarded_postread_matches = selector_guarded_second_postread and label.endswith(
            "-matches-close"
        )
        selector_guarded_second_two_reads = (
            selector_guarded_second_postread
            or "-write-selector-guarded-second-two-reads-value-" in label
        )
        selector_guarded_second_read = (
            selector_guarded_second_two_reads
            or "-write-selector-guarded-second-read-value-" in label
        )
        guarded_second_matches = (
            selector_guarded_second_read
            and not selector_guarded_second_postread
            and label.endswith("-matches-close")
        )
        selector_guarded_second_write = (
            selector_guarded_second_read or "-write-selector-guarded-second-value-" in label
        )
        selector_reused_second_write = (
            selector_guarded_second_write or "-write-selector-reused-second-value-" in label
        )
        selector_inside_first_argument = (
            selector_reused_second_write
            or "-write-selector-before-first-value-" in label
            or "-write-selector-inside-first-argument-value-" in label
        )
        selector_inside_final_argument = (
            selector_inside_first_argument
            or "-write-selector-inside-final-argument-value-" in label
        )
        selector_final_argument_read = (
            selector_inside_final_argument or "-write-selector-final-argument-value-" in label
        )
        terminal_selector_value = selector_final_argument_read or "-write-selector-value-" in label
        before_selector_read = "-write-before-selector-value-" in label
        before_first_selector_read = "-write-before-first-selector-value-" in label
        before_second_write_read = "-write-before-second-write-value-" in label
        final_argument_read = (
            selector_final_argument_read or "-write-final-argument-value-" in label
        )
        feeding_first_read = (
            "-write-final-argument-value-" in label or "-write-feeding-first-value-" in label
        )
        before_first_write_read = (
            feeding_first_read
            or "-write-before-first-write-value-" in label
            or "-write-inside-first-write-value-" in label
        )
        early_terminal_selector = terminal_selector_value and label.endswith("-order-close")
        terminal_sequence = ()
        if "-terminal-match-read-sequence-" in label:
            terminal_sequence = ("data-closed", "data-unvisited")
            if label.endswith("-missing-read"):
                terminal_sequence = ("data-unvisited", "data-unvisited")
            elif label.endswith("-order-close") and not early_terminal_selector:
                terminal_sequence = ("data-unvisited", "data-closed")
            elif label.endswith("-wider-close"):
                terminal_sequence = (
                    "data-closed",
                    "data-unvisited",
                    "data-visited",
                    "data-yielded",
                )
            elif label.endswith("-alternating-close"):
                terminal_sequence = ("data-closed", None, "data-unvisited")
        terminal_read = "-terminal-" in label and not terminal_match
        following_writes = ()
        if "-fourth-" in label:
            following_writes = (
                (
                    ("data-after-selector", "true", "data-closed"),
                    ("data-closed", "true", "data-after-selector"),
                )
                if label.endswith(("-fourth-read-close", "-fourth-read-terminal-close"))
                else (("data-closed", "true", "data-closed"), ("data-closed", closed, None))
            )
        elif label.endswith("-sixth-read-close"):
            following_writes = (
                ("data-closed", "true", "data-closed"),
                ("data-closed", "false", None),
                ("data-after-selector", "true", "data-closed"),
                ("data-closed", "true", "data-after-selector"),
            )
        before_selector = (
            ("false" if postselector_read or label.endswith("-missing-read") else "true")
            if third_close
            else closed
        )
        close_writes = (
            "data-closed=true;" if second_close else ""
        ) + f"data-closed={before_selector};"
        if selector_inside_first_argument:
            close_writes = "matches=[data-closed=false]:false;" + close_writes.replace(
                "data-closed=true;", "data-closed=false;", 1
            )
        if selector_guarded_second_write:
            close_writes = close_writes.replace(
                "data-closed=false;data-closed=false;", "data-closed=false;", 1
            )
        if before_first_selector_read:
            read_name = terminal_sequence[0]
            read_value = "false" if read_name == "data-unvisited" else "true"
            close_writes += f"hasAttribute={read_name}:{read_value};"
        if before_second_write_read:
            read_name = terminal_sequence[0]
            read_value = "false" if read_name == "data-unvisited" else "true"
            close_writes = close_writes.replace(
                "data-closed=true;", f"data-closed=true;hasAttribute={read_name}:{read_value};", 1
            )
        if before_first_write_read:
            close_writes = f"hasAttribute={terminal_sequence[0]}:false;" + close_writes
            if feeding_first_read:
                close_writes = "hasAttribute=data-visited:true;" + close_writes
        selecting = "-selector-" in label
        if selecting:
            selector = "[data-closed=false]" if before_selector == "false" else "[data-closed]"
            selector_result = "-selector-third-result-" in label
            if selector_result and before_selector == "false":
                selector = "[data-closed=true]"
            matched = closed if selector_result else "true"
            close_writes += f"matches={selector}:{matched};"
        if following_writes:
            for name, value, read_name in following_writes:
                if read_name:
                    close_writes += f"hasAttribute={read_name}:{value};"
                close_writes += f"{name}={value};"
        else:
            if postselector_read:
                read_name = "data-unvisited" if label.endswith("-missing-read") else "data-closed"
                close_writes += f"hasAttribute={read_name}:{closed};"
            if third_close:
                close_writes += f"data-closed={closed};"
        if terminal_read:
            terminal_name = (
                "data-unvisited"
                if label.endswith("-missing-read")
                else (
                    "data-after-selector"
                    if label.endswith("-fourth-read-terminal-close")
                    else "data-closed"
                )
            )
            terminal_value = "false" if label.endswith("-missing-read") else "true"
            close_writes += f"hasAttribute={terminal_name}:{terminal_value};"
        if before_selector_read:
            read_name = terminal_sequence[0]
            read_value = "false" if read_name == "data-unvisited" else "true"
            close_writes += f"hasAttribute={read_name}:{read_value};"
        if terminal_match:
            terminal_value = "false" if closed == "true" else "true"
            close_writes += f"matches=[data-closed=false]:{terminal_value};"
        if early_terminal_selector:
            close_writes = (
                close_writes.removesuffix("data-closed=false;matches=[data-closed=false]:true;")
                + "matches=[data-closed=false]:false;data-closed=false;"
            )
        if terminal_sequence:
            for terminal_name in terminal_sequence[
                int(
                    before_selector_read
                    or before_first_selector_read
                    or before_second_write_read
                    or before_first_write_read
                ) :
            ]:
                if terminal_name is None:
                    close_writes += "matches=[data-closed=false]:true;"
                else:
                    terminal_value = "false" if terminal_name == "data-unvisited" else "true"
                    close_writes += f"hasAttribute={terminal_name}:{terminal_value};"
        elif terminal_match_read:
            terminal_name = "data-unvisited" if label.endswith("-missing-read") else "data-closed"
            terminal_value = "false" if label.endswith("-missing-read") else "true"
            close_writes += f"hasAttribute={terminal_name}:{terminal_value};"
        if final_argument_read:
            close_writes += (
                "matches=[data-closed=false]:true;"
                if selector_inside_final_argument
                else "hasAttribute=data-closed:true;"
            )
        if terminal_write:
            after_terminal_value = terminal_value if terminal_write_value else "false"
            if (
                "-write-earlier-value-" in label
                or before_selector_read
                or before_first_selector_read
                or before_second_write_read
            ):
                after_terminal_value = (
                    "false" if terminal_sequence[0] == "data-unvisited" else "true"
                )
            if before_first_write_read:
                after_terminal_value = "false"
            if terminal_selector_value:
                after_terminal_value = "false" if early_terminal_selector else "true"
            close_writes += f"data-after-terminal={after_terminal_value};"
        conditional = label.startswith("conditional-")
        numeric_snapshot = kind == "js_num" and label not in (
            "number",
            "number-primitive-close",
            "number-throwing-close",
            "number-getter-close",
        )
        snapshot = conditional or label.startswith("boolean-snapshot") or numeric_snapshot
        js_kind = (
            "number" if numeric_snapshot else "boolean" if conditional else label.partition("-")[0]
        )
        script = text + """
var savedThrow, savedExhausted;
(function() {
  const saved = {};
  let writes = '';
  const anchor = {
    hasAttribute(name) { return name in saved; },
    setAttribute(name, value) {
      saved[name] = '' + value;
      writes += name + '=' + saved[name] + ';';
    }
  };
  try { customElements(anchor); }
  catch (error) { savedThrow = typeof error + ':' + error + ':' + writes; }
  writes = '';
  savedExhausted = '' + customElements(anchor) + ':' + writes;
})();
"""
        if selecting:
            script = script.replace(
                "    hasAttribute(name) { return name in saved; },",
                """
    matches(selector) {
      let matched;
      if (selector === '[data-closed]') matched = 'data-closed' in saved;
      else if (selector === '[data-closed=false]') matched = saved['data-closed'] === 'false';
      else if (selector === '[data-closed=true]') matched = saved['data-closed'] === 'true';
      else throw 'unexpected selector';
      writes += 'matches=' + selector + ':' + matched + ';';
      return matched;
    },
    hasAttribute(name) { return name in saved; },""",
            )
        if postselector_read:
            script = (
                script.replace(
                    "  let writes = '';", "  let writes = '';\n  let selectorReadPending = false;"
                )
                .replace(
                    "      return matched;",
                    "      selectorReadPending = true;\n      return matched;",
                )
                .replace(
                    "    hasAttribute(name) { return name in saved; },",
                    """
    hasAttribute(name) {
      const present = name in saved;
      if (selectorReadPending) {
        writes += 'hasAttribute=' + name + ':' + present + ';';
        selectorReadPending = false;
      }
      return present;
    },""",
                )
            )
        if conditional:
            script = (
                script.replace(
                    "var savedThrow, savedExhausted;",
                    "var savedThrow, savedExhausted, savedNormal, savedPrior;",
                )
                .replace(
                    "  try { customElements(anchor); }",
                    "  saved.stop = 'yes';\n  try { customElements(anchor); }",
                )
                .replace(
                    "})();",
                    """
  delete saved.stop;
  delete saved['data-yielded'];
  delete saved['data-visited'];
  delete saved['data-closed'];
  writes = '';
  savedNormal = '' + customElements(anchor) + ':' + writes;
  saved.stop = 'yes';
  saved['data-closed'] = 'before';
  delete saved['data-yielded'];
  writes = '';
  try { customElements(anchor); }
  catch (error) { savedPrior = typeof error + ':' + error + ':' + writes; }
})();
""",
                )
            )
        if selector_guarded_second_write:
            script = script.replace(
                "saved['data-closed'] = 'before';", "saved['data-closed'] = 'false';"
            )
        if following_writes:
            read_count = sum(read_name is not None for _, _, read_name in following_writes)
            read_count += terminal_read
            script = (
                script.replace("let selectorReadPending = false;", "let selectorReadPending = 0;")
                .replace("selectorReadPending = true;", f"selectorReadPending = {read_count};")
                .replace("selectorReadPending = false;", "--selectorReadPending;")
                .replace(
                    "delete saved['data-closed'];",
                    "delete saved['data-closed']; delete saved['data-after-selector'];",
                )
            )
        if terminal_match:
            script = script.replace(
                "let selectorReadPending = 0;",
                "let selectorReadPending = 0; let closeMatches = 0;",
            ).replace(
                f"selectorReadPending = {read_count};",
                f"selectorReadPending = ++closeMatches % 2 ? {read_count} : {int(terminal_match_read)};",
            )
        if terminal_sequence:
            selector_reads = (
                [
                    int(selector_guarded_second_read and not guarded_second_matches)
                    + selector_guarded_second_two_reads * (1 + guarded_second_matches)
                    + (selector_guarded_second_postread and not guarded_postread_matches)
                ]
                if selector_inside_first_argument
                else []
            ) + [
                read_count + int(before_selector_read),
                0,
            ]
            for terminal_name in terminal_sequence[
                int(
                    before_selector_read
                    or before_first_selector_read
                    or before_second_write_read
                    or before_first_write_read
                ) :
            ]:
                if terminal_name is None:
                    selector_reads.append(0)
                else:
                    selector_reads[-1] += 1
            if selector_inside_final_argument:
                selector_reads.append(0)
            elif final_argument_read:
                selector_reads[-1] += 1
            script = script.replace(
                f"selectorReadPending = ++closeMatches % 2 ? {read_count} : 1;",
                f"selectorReadPending = {selector_reads}[closeMatches++ % {len(selector_reads)}];",
            )
        if guarded_second_matches:
            script = script.replace(
                "      else throw 'unexpected selector';",
                "      else if (selector === '[data-visited]') matched = 'data-visited' in saved;"
                "\n      else throw 'unexpected selector';",
            ).replace(
                "      writes += 'matches=' + selector + ':' + matched + ';';",
                "      writes += 'matches=' + selector + ':' + matched + ';';"
                "\n      if (selector === '[data-visited]') return matched;",
            )
        if guarded_postread_matches:
            script = script.replace(
                "      writes += 'matches=' + selector + ':' + matched + ';';",
                "      writes += 'matches=' + selector + ':' + matched + ';';"
                "\n      if (selector === '[data-closed=true]') return matched;",
            )
        if before_first_selector_read:
            script = script.replace(
                "let closeMatches = 0;", "let closeMatches = 0; let closeWrites = 0;"
            ).replace(
                "      saved[name] = '' + value;",
                "      saved[name] = '' + value;\n"
                "      if (name === 'data-closed' && ++closeWrites % 4 === 2) selectorReadPending = 1;",
            )
        if before_second_write_read:
            script = script.replace(
                "let closeMatches = 0;", "let closeMatches = 0; let closeWrites = 0;"
            ).replace(
                "      saved[name] = '' + value;",
                "      saved[name] = '' + value;\n"
                "      if (name === 'data-closed' && ++closeWrites % 4 === 1) selectorReadPending = 1;",
            )
        if before_first_write_read:
            script = (
                script.replace(
                    "let closeMatches = 0;", "let closeMatches = 0; let beforeWriteReads = 0;"
                )
                .replace(
                    "      saved[name] = '' + value;",
                    "      saved[name] = '' + value;\n"
                    "      if (name === 'data-visited' && saved.stop) beforeWriteReads = 2;",
                )
                .replace(
                    "      const present = name in saved;",
                    "      const present = name in saved;\n"
                    "      if (beforeWriteReads && --beforeWriteReads === 0) writes += 'hasAttribute=' + name + ':' + present + ';';",
                )
            )
        if feeding_first_read:
            script = script.replace("beforeWriteReads = 2;", "beforeWriteReads = 3;").replace(
                "--beforeWriteReads === 0", "--beforeWriteReads < 2"
            )
        if terminal_write:
            script = script.replace(
                "delete saved['data-closed'];",
                "delete saved['data-closed']; delete saved['data-after-terminal'];",
            )
        if mixed:
            script = (
                script.replace(
                    "var savedThrow, savedExhausted, savedNormal, savedPrior;",
                    "var savedThrow, savedExhausted, savedNormal, savedPrior, "
                    "savedReturn, savedReturnPrior, savedReturnExhausted, savedReturnPriorExhausted;",
                )
                .replace(
                    "  saved.stop = 'yes';",
                    "  saved.stop = 'yes';\n  saved.advance = 'yes';",
                )
                .replace("  delete saved.stop;", "  delete saved.stop;\n  delete saved.advance;")
                .replace(
                    "})();",
                    """
  delete saved.stop;
  saved.advance = 'yes';
  for (let repetition = 0; repetition < 2; ++repetition) {
    delete saved['data-yielded'];
    delete saved['data-visited'];
    delete saved['data-closed'];
    if (repetition) saved['data-closed'] = 'before';
    writes = '';
    const returned = '' + customElements(anchor) + ':' + writes;
    if (repetition) savedReturnPrior = returned;
    else savedReturn = returned;
    delete saved['data-closed'];
    writes = '';
    const exhausted = '' + customElements(anchor) + ':' + writes;
    if (repetition) savedReturnPriorExhausted = exhausted;
    else savedReturnExhausted = exhausted;
  }
})();
""",
                )
            )
        expected = {
            "savedThrow": f"{js_kind}:"
            + (
                "3"
                if numeric_snapshot
                else {"number": "1", "boolean": "false", "string": "saved"}[js_kind]
            )
            + ":data-next=false;data-yielded=yes;"
            + ("data-visited=yes;" if snapshot else "")
            + close_writes,
            "savedExhausted": ("0" if numeric_snapshot else "true" if snapshot else "false")
            + ":data-next=true;data-yielded=yes;",
        }
        if conditional:
            expected.update(
                savedNormal=("1" if numeric_snapshot else "true")
                + ":data-next=false;data-yielded=yes;data-visited=yes;data-next=true;data-yielded=yes;",
                savedPrior=("number:3" if numeric_snapshot else "boolean:true")
                + ":data-next=false;data-yielded=yes;data-visited=yes;"
                + close_writes,
            )
        if selector_guarded_second_write:
            expected["savedPrior"] = expected["savedPrior"].replace(
                "matches=[data-closed=false]:false;data-closed=false;matches=[data-closed=false]:true;",
                "matches=[data-closed=false]:true;data-closed=true;data-closed=true;matches=[data-closed=false]:false;",
                1,
            )
        if selector_guarded_second_read:
            expected["savedPrior"] = expected["savedPrior"].replace(
                "data-closed=true;data-closed=true;",
                "data-closed=true;"
                + (
                    "matches=[data-visited]:true;"
                    if guarded_second_matches
                    else "hasAttribute=data-visited:true;"
                )
                + "data-closed=true;",
                1,
            )
        if selector_guarded_second_two_reads:
            expected["savedPrior"] = expected["savedPrior"].replace(
                (
                    "data-visited]:true;data-closed=true;matches=[data-closed=false]:false;"
                    if guarded_second_matches
                    else "data-visited:true;data-closed=true;matches=[data-closed=false]:false;"
                ),
                (
                    "data-visited]:true;hasAttribute=data-visited:true;"
                    if guarded_second_matches
                    else "data-visited:true;"
                )
                + "hasAttribute=data-unvisited:false;data-closed=false;matches=[data-closed=false]:true;",
                1,
            )
        if selector_guarded_second_postread:
            expected["savedPrior"] = expected["savedPrior"].replace(
                "hasAttribute=data-unvisited:false;data-closed=false;",
                "hasAttribute=data-unvisited:false;data-closed=false;"
                + (
                    "matches=[data-closed=true]:false;"
                    if guarded_postread_matches
                    else "hasAttribute=data-closed:true;"
                ),
                1,
            )
        if before_first_write_read and terminal_sequence[0] == "data-closed":
            expected["savedPrior"] = (
                expected["savedPrior"]
                .replace("hasAttribute=data-closed:false;", "hasAttribute=data-closed:true;", 1)
                .replace("data-after-terminal=false;", "data-after-terminal=true;")
            )
        if mixed:
            expected.update(
                savedReturn="false:data-next=false;data-yielded=yes;data-visited=yes;"
                "data-closed=true;",
                savedReturnPrior="true:data-next=false;data-yielded=yes;data-visited=yes;"
                "data-closed=true;",
                savedReturnExhausted="true:data-next=true;data-yielded=yes;",
                savedReturnPriorExhausted="true:data-next=true;data-yielded=yes;",
            )
        node = args.work / f"saved-throw-{label}-node.js"
        node.write_text(script + "".join(f"console.log({key});\n" for key in expected))
        assert dom.run([args.node, str(node)]).stdout.splitlines() == list(expected.values())
        vm = args.work / f"saved-throw-{label}-vm.js"
        vm.write_text(script)
        assert dom.run([args.reference, str(vm)]).stdout == "".join(
            f'{name}="{quote(value)}"\n' for name, value in sorted(expected.items())
        )
        observations += 2 * len(expected)
        ir, contract = dom.prepare(
            args, f"saved-throw-{label}", text, 1, entry_name="customElements"
        )
        contract.update(initial_intrinsics=INTRINSICS)
        for owned in (False, True):
            manifest = dict(
                contract, provider="ctbrowser-dom-session-v1" if owned else "ctbrowser-dom-v1"
            )
            for optimize in (False, True):
                name = f"saved-throw-{label}-{owned}-{optimize}"
                native = dom.lower(args, ir, manifest, name, optimize=optimize)
                cpp = dom.run([args.translate, "--mlir-to-cpp", str(native)]).stdout
                if "throw ctnative::js_exception{" not in cpp or "if (true)" in cpp:
                    raise RuntimeError(f"{name}: saved throw lost its ordinary terminating scope")
                selected_checks = checks + (OWNED_CHECKS if owned else "")
                if any(name == "data-after-selector" for name, _, _ in following_writes):
                    selected_checks = (
                        selected_checks.replace(
                            'const auto visited = target.atoms().intern("data-visited");',
                            'const auto visited = target.atoms().intern("data-visited");'
                            'const auto after_selector = target.atoms().intern("data-after-selector");',
                        )
                        .replace(
                            "{next, yielded, closed, visited}",
                            "{next, yielded, closed, visited, after_selector}",
                        )
                        .replace(
                            'assert(target.read().attribute_value(id, closed) == "@CLOSED@");',
                            'assert(target.read().attribute_value(id, closed) == "@CLOSED@");'
                            'assert(target.read().attribute_value(id, after_selector) == "true");',
                        )
                    )
                if terminal_write:
                    selected_checks = (
                        selected_checks.replace(
                            'const auto visited = target.atoms().intern("data-visited");',
                            'const auto visited = target.atoms().intern("data-visited");'
                            'const auto after_terminal = target.atoms().intern("data-after-terminal");',
                        )
                        .replace(
                            "{next, yielded, closed, visited}",
                            "{next, yielded, closed, visited, after_terminal}",
                        )
                        .replace(
                            'assert(target.read().attribute_value(id, closed) == "@CLOSED@");',
                            'assert(target.read().attribute_value(id, closed) == "@CLOSED@");'
                            f'assert(target.read().attribute_value(id, after_terminal) == "{after_terminal_value}");',
                        )
                    )
                if before_first_write_read and terminal_sequence[0] == "data-closed":
                    selected_checks = selected_checks.replace(
                        'attribute_value(id, after_terminal) == "false"',
                        'attribute_value(id, after_terminal) == (repetition ? "true" : "false")',
                    )
                if selector_guarded_second_write:
                    selected_checks = selected_checks.replace(
                        "@WRITE_COUNT@", "(7 + repetition)"
                    ).replace(
                        "@SECOND_CLOSE@",
                        "assert(writes[4 + repetition].name == closed);"
                        "assert(writes[5 + repetition].name == closed);"
                        "if (repetition) { assert(writes[4].name == closed); }"
                        "assert(writes.back().name == after_terminal);",
                    )
                selected_includes, selected_libraries = includes, libraries
                if selecting:
                    selected_checks = (
                        "style::engine selectors{atoms}, foreign_selectors{foreign_atoms};\n"
                        + selected_checks.replace(
                            "@ENTRY@(alias)", "@ENTRY@(alias, selectors)"
                        ).replace("@ENTRY@(foreign)", "@ENTRY@(foreign, foreign_selectors)")
                    )
                    selected_includes, selected_libraries = dom.link_options(args, selectors=True)
                    if "ctnative::Element.prototype.matches.call" not in cpp:
                        raise RuntimeError(f"{name}: ignored throw lost selector evaluation")
                    if terminal_read:
                        close_body = cpp[
                            cpp.index("ctnative::Element.prototype.matches.call") : cpp.index(
                                "throw ctnative::js_exception{"
                            )
                        ]
                        if close_body.rfind("ctnative::has_attribute(") <= close_body.rfind(
                            "ctnative::set_attribute("
                        ):
                            raise RuntimeError(f"{name}: final read moved before its close writes")
                    if terminal_match:
                        selector_call = "ctnative::Element.prototype.matches.call"
                        close_body = cpp[
                            cpp.index(selector_call) : cpp.index("throw ctnative::js_exception{")
                        ]
                        if selector_inside_first_argument:
                            first_selector = close_body[
                                : close_body.index(
                                    selector_call,
                                    (
                                        close_body.index(selector_call, len(selector_call))
                                        + len(selector_call)
                                        if guarded_second_matches or guarded_postread_matches
                                        else len(selector_call)
                                    ),
                                )
                            ]
                            first_operations = [
                                (
                                    "matches"
                                    if selector_call in line
                                    else "read" if "ctnative::has_attribute(" in line else "write"
                                )
                                for line in first_selector.splitlines()
                                if selector_call in line
                                or "ctnative::has_attribute(" in line
                                or "ctnative::set_attribute(" in line
                            ]
                            expected_first_operations = (
                                ["matches", "write", "write"]
                                if selector_reused_second_write
                                else ["matches", "write", "read", "write"]
                            )
                            if selector_guarded_second_read:
                                expected_first_operations = [
                                    "matches",
                                    "write",
                                    "matches" if guarded_second_matches else "read",
                                    "write",
                                ]
                            if selector_guarded_second_two_reads:
                                expected_first_operations[-1:-1] = ["read"] * (
                                    1 + guarded_second_matches
                                )
                                second_write_position = first_selector.rfind(
                                    "ctnative::set_attribute("
                                )
                                last_read = first_selector.rfind(
                                    "ctnative::has_attribute(", 0, second_write_position
                                )
                                feeding_value = (
                                    first_selector[:last_read].rpartition("=")[0].split()[-1]
                                )
                                second_write = first_selector[
                                    first_selector.rfind("ctnative::set_attribute(") :
                                ].splitlines()[0]
                                if not second_write.endswith(f", {feeding_value});"):
                                    raise RuntimeError(
                                        f"{name}: second write lost its last read value"
                                    )
                            if selector_guarded_second_postread:
                                expected_first_operations.append(
                                    "matches" if guarded_postread_matches else "read"
                                )
                                postread_body = first_selector[second_write_position:]
                                if postread_body.index(
                                    "matches.call"
                                    if guarded_postread_matches
                                    else "ctnative::has_attribute("
                                ) > postread_body.index("}"):
                                    raise RuntimeError(f"{name}: post-write read escaped its guard")
                            if first_operations != expected_first_operations:
                                raise RuntimeError(
                                    f"{name}: first selector lost its feeding write order"
                                )
                        if before_first_selector_read:
                            before_first_selector = cpp[: cpp.index(selector_call)]
                            if before_first_selector.rfind(
                                "ctnative::has_attribute("
                            ) <= before_first_selector.rfind("ctnative::set_attribute("):
                                raise RuntimeError(
                                    f"{name}: saved read moved after its first selector"
                                )
                        if before_second_write_read:
                            before_first_selector = cpp[: cpp.index(selector_call)]
                            second_write = before_first_selector.rfind("ctnative::set_attribute(")
                            first_write = before_first_selector.rfind(
                                "ctnative::set_attribute(", 0, second_write
                            )
                            between_writes = before_first_selector[first_write:second_write]
                            if (
                                between_writes.count("ctnative::has_attribute(") != 2
                                or "ctnative::has_attribute("
                                in before_first_selector[second_write:]
                            ):
                                raise RuntimeError(
                                    f"{name}: saved read moved across its second write"
                                )
                        if before_first_write_read:
                            before_first_selector = cpp[: cpp.index(selector_call)]
                            second_write = before_first_selector.rfind("ctnative::set_attribute(")
                            first_write = before_first_selector.rfind(
                                "ctnative::set_attribute(", 0, second_write
                            )
                            body_write = before_first_selector.rfind(
                                "ctnative::set_attribute(", 0, first_write
                            )
                            if (
                                before_first_selector[body_write:first_write].count(
                                    "ctnative::has_attribute("
                                )
                                != 3
                                or before_first_selector[first_write:second_write].count(
                                    "ctnative::has_attribute("
                                )
                                != 1
                            ):
                                raise RuntimeError(
                                    f"{name}: saved read moved across its first write"
                                )
                        if before_selector_read:
                            before_last_selector = close_body[: close_body.rfind(selector_call)]
                            if before_last_selector.rfind(
                                "ctnative::has_attribute("
                            ) <= before_last_selector.rfind("ctnative::set_attribute("):
                                raise RuntimeError(f"{name}: saved read moved after its selector")
                        if early_terminal_selector:
                            last_selector = close_body.rfind(selector_call)
                            if selector_inside_final_argument:
                                last_selector = close_body.rfind(selector_call, 0, last_selector)
                            last_closed_write = close_body.rfind(
                                "ctnative::set_attribute(",
                                0,
                                close_body.rfind("ctnative::set_attribute("),
                            )
                            if last_selector >= last_closed_write:
                                raise RuntimeError(f"{name}: saved selector moved after its write")
                        final_write = close_body.rfind("ctnative::set_attribute(")
                        before_terminal_write = (
                            close_body[:final_write] if terminal_write else close_body
                        )
                        if close_body.count(selector_call) != (
                            2
                            + terminal_sequence.count(None)
                            + selector_inside_final_argument
                            + selector_inside_first_argument
                            + guarded_second_matches
                            + guarded_postread_matches
                        ) or (
                            not early_terminal_selector
                            and close_body.rfind(selector_call)
                            <= before_terminal_write.rfind("ctnative::set_attribute(")
                        ):
                            raise RuntimeError(f"{name}: final selector lost its close write order")
                        if (
                            terminal_match_read
                            and not selector_inside_final_argument
                            and close_body.rfind("ctnative::has_attribute(")
                            <= close_body.rfind(selector_call)
                        ):
                            raise RuntimeError(
                                f"{name}: final read moved before its close selector"
                            )
                        if terminal_sequence:
                            tail = close_body.split(
                                selector_call,
                                2
                                + selector_inside_first_argument
                                + guarded_second_matches
                                + guarded_postread_matches,
                            )[
                                2
                                + selector_inside_first_argument
                                + guarded_second_matches
                                + guarded_postread_matches
                            ]
                            operations = [
                                (
                                    "read"
                                    if "ctnative::has_attribute(" in line
                                    else (
                                        "write" if "ctnative::set_attribute(" in line else "matches"
                                    )
                                )
                                for line in tail.splitlines()
                                if "ctnative::has_attribute(" in line
                                or "ctnative::set_attribute(" in line
                                or selector_call in line
                            ]
                            expected_operations = (
                                (["write"] if early_terminal_selector else [])
                                + [
                                    "matches" if item is None else "read"
                                    for item in terminal_sequence[
                                        int(
                                            before_selector_read
                                            or before_first_selector_read
                                            or before_second_write_read
                                            or before_first_write_read
                                        ) :
                                    ]
                                ]
                                + (
                                    ["matches" if selector_inside_final_argument else "read"]
                                    if final_argument_read
                                    else []
                                )
                                + (["write"] if terminal_write else [])
                            )
                            if operations != expected_operations:
                                raise RuntimeError(
                                    f"{name}: terminal read/selector sequence changed"
                                )
                dom.standalone(
                    args,
                    native,
                    name,
                    selected_checks.replace(
                        "@SETUP@",
                        (
                            'assert(target.set_attribute(id, target.atoms().intern("stop"), "yes"));'
                            f'if (repetition) {{ assert(target.set_attribute(id, closed, "{"false" if selector_guarded_second_write else "before"}")); }}'
                            if conditional
                            else ""
                        )
                        + (
                            'assert(target.set_attribute(id, target.atoms().intern("advance"), "yes"));'
                            if mixed
                            else ""
                        ),
                    )
                    .replace(
                        "@NORMAL@",
                        (
                            r"""
                assert(target.remove_attribute(id, target.atoms().intern("stop")));
                assert(target.remove_attribute(id, target.atoms().intern("advance")));
                assert(target.remove_attribute(id, yielded));
                assert(target.remove_attribute(id, visited));
                (void)target.take_writes();
                assert(@NATURAL@);
                const auto normal = target.take_writes();
                assert(normal.size() == 5 && normal[0].name == next && normal[1].name == yielded &&
                       normal[2].name == visited && normal[3].name == next && normal[4].name == yielded);
                assert(!target.read().has_attribute(id, closed));
"""
                            if conditional
                            else ""
                        )
                        + (
                            r"""
                assert(target.set_attribute(id, target.atoms().intern("advance"), "yes"));
                assert(target.remove_attribute(id, yielded));
                assert(target.remove_attribute(id, visited));
                if (repetition) { assert(target.set_attribute(id, closed, "before")); }
                (void)target.take_writes();
                assert(static_cast<bool>(call()) == (repetition != 0));
                const auto returned = target.take_writes();
                assert(returned.size() == 4 && returned[0].name == next &&
                       returned[1].name == yielded && returned[2].name == visited &&
                       returned[3].name == closed);
                for (const auto & write : returned) { assert(write.node == id && !write.text); }
                assert(target.read().attribute_value(id, next) == "false");
                assert(target.read().attribute_value(id, visited) == "yes");
                assert(target.read().attribute_value(id, closed) == "true");
                assert(target.remove_attribute(id, closed));
                (void)target.take_writes();
                assert(static_cast<bool>(call()));
                const auto return_exhausted = target.take_writes();
                assert(return_exhausted.size() == 2 && return_exhausted[0].name == next &&
                       return_exhausted[1].name == yielded);
                assert(target.read().attribute_value(id, next) == "true");
                assert(!target.read().has_attribute(id, closed));
"""
                            if mixed
                            else ""
                        ),
                    )
                    .replace("@TYPE@", kind)
                    .replace("@PAYLOAD@", payload)
                    .replace("@CLOSED@", closed)
                    .replace(
                        "@EXHAUSTED@",
                        (
                            "call().value() == 0.0"
                            if numeric_snapshot
                            else "static_cast<bool>(call()) == @VISITED@"
                        ),
                    )
                    .replace(
                        "@NATURAL@",
                        (
                            "call().value() == 1.0"
                            if numeric_snapshot
                            else "static_cast<bool>(call())"
                        ),
                    )
                    .replace("@VISITED@", "true" if snapshot else "false")
                    .replace(
                        "@WRITE_COUNT@",
                        (
                            str(5 + len(following_writes) + terminal_write)
                            if following_writes
                            else (
                                "6"
                                if third_close
                                else "5" if second_close else "4" if snapshot else "3"
                            )
                        ),
                    )
                    .replace(
                        "@SECOND_CLOSE@",
                        ("assert(writes[4].name == closed);" if second_close else "")
                        + (
                            "".join(
                                f"assert(writes[{index}].name == "
                                + ("closed" if name == "data-closed" else "after_selector")
                                + ");"
                                for index, (name, _, _) in enumerate(following_writes, 5)
                            )
                            if following_writes
                            else "assert(writes[5].name == closed);" if third_close else ""
                        )
                        + (
                            "assert(writes.back().name == after_terminal);"
                            if terminal_write
                            else ""
                        ),
                    )
                    .replace("@CLOSED_INDEX@", "3" if snapshot else "2"),
                    compilers,
                    selected_includes,
                    selected_libraries,
                )
                executions += 2 * len(compilers)
                if (
                    label
                    in (
                        "number",
                        "number-primitive-close",
                        "number-throwing-close",
                        "number-getter-close",
                        "conditional-boolean-read",
                        "conditional-boolean-primitive-close",
                        "conditional-boolean-throwing-close",
                        "conditional-boolean-getter-close",
                        "conditional-boolean-nonliteral-close",
                        "conditional-boolean-postwrite-getter-close",
                        "conditional-boolean-postwrite-missing-read",
                        "conditional-boolean-second-postwrite-close",
                        "conditional-boolean-second-postwrite-getter-close",
                        "conditional-boolean-second-postwrite-missing-read",
                        "conditional-boolean-second-postwrite-selector-close",
                        "conditional-boolean-second-postwrite-selector-getter-close",
                        "conditional-boolean-second-postwrite-selector-missing-read",
                        "conditional-boolean-second-postwrite-selector-third-close",
                        "conditional-boolean-second-postwrite-selector-third-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-missing-read",
                        "conditional-boolean-second-postwrite-selector-third-result-close",
                        "conditional-boolean-second-postwrite-selector-third-result-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-result-missing-read",
                        "conditional-boolean-second-postwrite-selector-third-read-close",
                        "conditional-boolean-second-postwrite-selector-third-read-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-missing-read",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-read-close",
                        "conditional-boolean-second-postwrite-selector-third-read-sixth-read-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-read-terminal-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-missing-read",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-missing-read",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-alternating-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-missing-read",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-wider-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-earlier-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-earlier-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-earlier-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-selector-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-selector-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-selector-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-first-selector-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-first-selector-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-first-selector-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-second-write-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-second-write-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-second-write-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-first-write-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-first-write-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-before-first-write-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-inside-first-write-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-inside-first-write-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-inside-first-write-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-feeding-first-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-feeding-first-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-feeding-first-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-final-argument-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-final-argument-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-final-argument-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-final-argument-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-final-argument-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-final-argument-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-inside-final-argument-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-inside-final-argument-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-inside-final-argument-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-inside-first-argument-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-inside-first-argument-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-inside-first-argument-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-before-first-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-before-first-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-before-first-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-reused-second-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-reused-second-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-reused-second-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-read-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-read-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-read-value-matches-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-read-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-two-reads-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-two-reads-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-two-reads-value-matches-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-two-reads-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-postread-value-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-postread-value-getter-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-postread-value-matches-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-guarded-second-postread-value-order-close",
                        "conditional-boolean-second-postwrite-selector-third-read-fourth-terminal-match-read-sequence-write-selector-reused-second-value-throw-close",
                        "boolean-snapshot-primitive-close",
                        "boolean-snapshot-throwing-close",
                        "boolean-snapshot-getter-close",
                    )
                    or numeric_snapshot
                ):
                    for suffix, bad, budget in (
                        ("budget", manifest, 0),
                        ("missing-element", dict(manifest, element_parameters=[]), None),
                        (
                            "missing-close",
                            dict(
                                manifest,
                                initial_intrinsics=[
                                    value
                                    for value in INTRINSICS
                                    if value != "__ctbrowser_iter_close"
                                ],
                            ),
                            None,
                        ),
                    ):
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
    throwing_close = refusals()["body-throw-close-throws"]
    getter_close = refusals()["body-throw-close-getter"]
    for label, text in (
        (
            "normal-postwrite-close",
            postwrite_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-postwrite-close",
            postwrite_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "normal-postwrite-getter-close",
            postwrite_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-postwrite-getter-close",
            postwrite_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "unsupported-postwrite-read",
            postwrite_throwing_source.replace(
                "throw anchor.hasAttribute('data-closed');",
                "throw anchor.matches('[data-closed]');",
            ),
        ),
        (
            "bad-postwrite-receiver",
            postwrite_throwing_source.replace(
                "throw anchor.hasAttribute('data-closed');",
                "throw (0).hasAttribute('data-closed');",
            ),
        ),
        (
            "normal-second-postwrite-close",
            second_postwrite_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-second-postwrite-close",
            second_postwrite_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "normal-second-postwrite-getter-close",
            second_postwrite_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-second-postwrite-getter-close",
            second_postwrite_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-second-postwrite-name",
            second_postwrite_throwing_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
                "anchor.setAttribute('bad name', anchor.hasAttribute('data-closed'));",
            ),
        ),
        (
            "bad-second-postwrite-receiver",
            second_postwrite_throwing_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
                "(0).setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
            ),
        ),
        (
            "escaping-second-postwrite-result",
            second_postwrite_throwing_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
                "anchor.setAttribute('data-closed', "
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed')));",
            ),
        ),
        (
            "normal-second-postwrite-selector-close",
            selector_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-second-postwrite-selector-close",
            selector_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "normal-second-postwrite-selector-getter-close",
            selector_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-second-postwrite-selector-getter-close",
            selector_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-second-postwrite-selector",
            selector_throwing_source.replace("'[data-closed]'", "'['"),
        ),
        (
            "dynamic-second-postwrite-selector",
            selector_throwing_source.replace("'[data-closed]'", "anchor"),
        ),
        (
            "bad-second-postwrite-selector-receiver",
            selector_throwing_source.replace("anchor.matches(", "(0).matches("),
        ),
        (
            "normal-third-postselector-close",
            third_postselector_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-third-postselector-close",
            third_postselector_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "normal-third-postselector-getter-close",
            third_postselector_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-third-postselector-getter-close",
            third_postselector_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-third-postselector-selector",
            third_postselector_throwing_source.replace("'[data-closed]'", "'['"),
        ),
        (
            "dynamic-third-postselector-selector",
            third_postselector_throwing_source.replace("'[data-closed]'", "anchor"),
        ),
        (
            "invalid-third-postselector-name",
            third_postselector_throwing_source.replace(
                "anchor.setAttribute('data-closed', false);",
                "anchor.setAttribute('bad name', false);",
            ),
        ),
        (
            "bad-third-postselector-receiver",
            third_postselector_throwing_source.replace(
                "anchor.setAttribute('data-closed', false);",
                "(0).setAttribute('data-closed', false);",
            ),
        ),
        (
            "bad-third-postselector-selector-receiver",
            third_postselector_throwing_source.replace("anchor.matches(", "(0).matches("),
        ),
        (
            "normal-third-postselector-read-close",
            postselector_read_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-third-postselector-read-close",
            postselector_read_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "normal-third-postselector-read-getter-close",
            postselector_read_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-third-postselector-read-getter-close",
            postselector_read_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-third-postselector-read-selector",
            postselector_read_throwing_source.replace("'[data-closed=false]'", "'['"),
        ),
        (
            "dynamic-third-postselector-read-selector",
            postselector_read_throwing_source.replace("'[data-closed=false]'", "anchor"),
        ),
        (
            "invalid-third-postselector-read-name",
            postselector_read_throwing_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
                "anchor.setAttribute('bad name', anchor.hasAttribute('data-closed'));",
            ),
        ),
        (
            "bad-third-postselector-read-receiver",
            postselector_read_throwing_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
                "(0).setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
            ),
        ),
        (
            "bad-third-postselector-read-value-receiver",
            postselector_read_throwing_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
                "anchor.setAttribute('data-closed', (0).hasAttribute('data-closed'));",
            ),
        ),
        (
            "dynamic-third-postselector-read-value-name",
            postselector_read_throwing_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
                "anchor.setAttribute('data-closed', anchor.hasAttribute(anchor));",
            ),
        ),
        (
            "unsupported-third-postselector-read-escape",
            postselector_read_throwing_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-closed'));",
                "const present = anchor.hasAttribute('data-closed'); "
                "anchor.setAttribute('data-closed', present); external(present);",
            ),
        ),
        (
            "normal-fourth-postselector-close",
            fourth_postselector_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-fourth-postselector-close",
            fourth_postselector_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "normal-fourth-postselector-getter-close",
            fourth_postselector_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-fourth-postselector-getter-close",
            fourth_postselector_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-fourth-postselector-name",
            fourth_postselector_throwing_source.replace(
                "anchor.setAttribute('data-closed', false);",
                "anchor.setAttribute('bad name', false);",
            ),
        ),
        (
            "dynamic-fourth-postselector-name",
            fourth_postselector_throwing_source.replace(
                "anchor.setAttribute('data-closed', false);", "anchor.setAttribute(anchor, false);"
            ),
        ),
        (
            "bad-fourth-postselector-receiver",
            fourth_postselector_throwing_source.replace(
                "anchor.setAttribute('data-closed', false);",
                "(0).setAttribute('data-closed', false);",
            ),
        ),
        (
            "bad-fourth-postselector-value-receiver",
            fourth_read_throwing_source.replace(
                "anchor.hasAttribute('data-after-selector')",
                "(0).hasAttribute('data-after-selector')",
            ),
        ),
        (
            "dynamic-fourth-postselector-value-name",
            fourth_read_throwing_source.replace(
                "anchor.hasAttribute('data-after-selector')", "anchor.hasAttribute(anchor)"
            ),
        ),
        (
            "unsupported-fourth-postselector-effect-call",
            fourth_postselector_throwing_source.replace(
                "throw false;", "external(anchor); throw false;"
            ),
        ),
        (
            "unsupported-fourth-postselector-read-escape",
            fourth_read_throwing_source.replace(
                "anchor.setAttribute('data-closed', anchor.hasAttribute('data-after-selector'));",
                "const present = anchor.hasAttribute('data-after-selector'); "
                "anchor.setAttribute('data-closed', present); external(present);",
            ),
        ),
        (
            "normal-terminal-postselector-close",
            terminal_postselector_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-terminal-postselector-close",
            terminal_postselector_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "normal-terminal-postselector-getter-close",
            terminal_postselector_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-terminal-postselector-getter-close",
            terminal_postselector_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-terminal-postselector-write-name",
            terminal_postselector_throwing_source.replace(
                "anchor.setAttribute('data-closed', false);",
                "anchor.setAttribute('bad name', false);",
            ),
        ),
        (
            "dynamic-terminal-postselector-read-name",
            terminal_postselector_throwing_source.replace(
                "throw anchor.hasAttribute('data-closed');", "throw anchor.hasAttribute(anchor);"
            ),
        ),
        (
            "bad-terminal-postselector-read-receiver",
            terminal_postselector_throwing_source.replace(
                "throw anchor.hasAttribute('data-closed');",
                "throw (0).hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-terminal-postselector-read-arity",
            terminal_postselector_throwing_source.replace(
                "throw anchor.hasAttribute('data-closed');", "throw anchor.hasAttribute();"
            ),
        ),
        (
            "unsupported-terminal-postselector-effect-call",
            terminal_postselector_throwing_source.replace(
                "throw anchor.hasAttribute('data-closed');",
                "external(anchor); throw anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "unsupported-terminal-postselector-read-escape",
            terminal_postselector_throwing_source.replace(
                "throw anchor.hasAttribute('data-closed');",
                "const present = anchor.hasAttribute('data-closed'); external(present); throw present;",
            ),
        ),
        (
            "unsupported-terminal-postselector-read-order",
            terminal_postselector_throwing_source.replace(
                "anchor.setAttribute('data-closed', false); throw anchor.hasAttribute('data-closed');",
                "const present = anchor.hasAttribute('data-closed'); "
                "anchor.setAttribute('data-closed', false); throw present;",
            ),
        ),
        (
            "normal-terminal-match-close",
            terminal_match_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-terminal-match-close",
            terminal_match_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "normal-terminal-match-getter-close",
            terminal_match_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-terminal-match-getter-close",
            terminal_match_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-terminal-match-selector",
            terminal_match_throwing_source.replace(
                "throw anchor.matches('[data-closed=false]');", "throw anchor.matches('[');"
            ),
        ),
        (
            "dynamic-terminal-match-selector",
            terminal_match_throwing_source.replace(
                "throw anchor.matches('[data-closed=false]');", "throw anchor.matches(anchor);"
            ),
        ),
        (
            "bad-terminal-match-receiver",
            terminal_match_throwing_source.replace(
                "throw anchor.matches('[data-closed=false]');",
                "throw (0).matches('[data-closed=false]');",
            ),
        ),
        (
            "invalid-terminal-match-arity",
            terminal_match_throwing_source.replace(
                "throw anchor.matches('[data-closed=false]');", "throw anchor.matches();"
            ),
        ),
        (
            "unsupported-terminal-match-escape",
            terminal_match_throwing_source.replace(
                "throw anchor.matches('[data-closed=false]');",
                "const matched = anchor.matches('[data-closed=false]'); external(matched); throw matched;",
            ),
        ),
        (
            "normal-terminal-match-read-close",
            terminal_match_read_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "normal-terminal-match-read-getter-close",
            terminal_match_read_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-terminal-match-read-close",
            terminal_match_read_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "return-terminal-match-read-getter-close",
            terminal_match_read_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "bad-terminal-match-read-receiver",
            terminal_match_read_throwing_source.replace(
                "throw anchor.hasAttribute('data-closed');",
                "throw (0).hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-terminal-match-read-arity",
            terminal_match_read_throwing_source.replace(
                "throw anchor.hasAttribute('data-closed');",
                "throw anchor.hasAttribute();",
            ),
        ),
        (
            "invalid-terminal-match-read-selector",
            terminal_match_read_throwing_source.replace(
                "anchor.matches('[data-closed=false]'); throw",
                "anchor.matches('['); throw",
            ),
        ),
        (
            "dynamic-terminal-match-read-selector",
            terminal_match_read_throwing_source.replace(
                "anchor.matches('[data-closed=false]'); throw",
                "anchor.matches(anchor); throw",
            ),
        ),
        (
            "unsupported-terminal-match-read-escape",
            terminal_match_read_throwing_source.replace(
                "throw anchor.hasAttribute('data-closed');",
                "const present = anchor.hasAttribute('data-closed'); external(present); throw present;",
            ),
        ),
        (
            "unsupported-terminal-match-read-order",
            terminal_match_read_throwing_source.replace(
                "anchor.matches('[data-closed=false]'); throw anchor.hasAttribute('data-closed');",
                "const present = anchor.hasAttribute('data-closed'); anchor.matches('[data-closed=false]'); throw present;",
            ),
        ),
        (
            "unsupported-terminal-match-read-effect",
            terminal_match_read_throwing_source.replace(
                "throw anchor.hasAttribute('data-closed');",
                "external(anchor); throw anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "normal-terminal-read-sequence-close",
            terminal_read_sequence_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "normal-terminal-read-sequence-getter-close",
            terminal_read_sequence_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-terminal-read-sequence-close",
            terminal_read_sequence_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "return-terminal-read-sequence-getter-close",
            terminal_read_sequence_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "bad-terminal-read-sequence-first-receiver",
            terminal_read_sequence_source.replace(
                "anchor.hasAttribute('data-closed'); throw",
                "(0).hasAttribute('data-closed'); throw",
            ),
        ),
        (
            "bad-terminal-read-sequence-last-receiver",
            terminal_read_sequence_source.replace(
                "throw anchor.hasAttribute('data-unvisited');",
                "throw (0).hasAttribute('data-unvisited');",
            ),
        ),
        (
            "invalid-terminal-read-sequence-arity",
            terminal_read_sequence_source.replace(
                "anchor.hasAttribute('data-closed'); throw",
                "anchor.hasAttribute(); throw",
            ),
        ),
        (
            "invalid-terminal-read-sequence-selector",
            terminal_read_sequence_source.replace(
                "throw anchor.hasAttribute('data-unvisited');",
                "anchor.matches('['); throw anchor.hasAttribute('data-unvisited');",
            ),
        ),
        (
            "unsupported-terminal-read-sequence-escape",
            terminal_read_sequence_source.replace(
                "anchor.hasAttribute('data-closed'); throw",
                "const present = anchor.hasAttribute('data-closed'); external(present); throw",
            ),
        ),
        (
            "unsupported-terminal-read-sequence-effect",
            terminal_read_sequence_source.replace(
                "throw anchor.hasAttribute('data-unvisited');",
                "external(anchor); throw anchor.hasAttribute('data-unvisited');",
            ),
        ),
        (
            "dynamic-terminal-read-sequence-name",
            terminal_read_sequence_source.replace(
                "throw anchor.hasAttribute('data-unvisited');",
                "throw anchor.hasAttribute(anchor);",
            ),
        ),
        (
            "normal-terminal-write-close",
            terminal_write_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "normal-terminal-write-getter-close",
            terminal_write_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-terminal-write-close",
            terminal_write_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "return-terminal-write-getter-close",
            terminal_write_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-terminal-write-name",
            terminal_write_source.replace(
                "anchor.setAttribute('data-after-terminal', false);",
                "anchor.setAttribute('bad name', false);",
            ),
        ),
        (
            "bad-terminal-write-receiver",
            terminal_write_source.replace(
                "anchor.setAttribute('data-after-terminal', false);",
                "(0).setAttribute('data-after-terminal', false);",
            ),
        ),
        (
            "invalid-terminal-write-arity",
            terminal_write_source.replace(
                "anchor.setAttribute('data-after-terminal', false);",
                "anchor.setAttribute('data-after-terminal');",
            ),
        ),
        (
            "unsupported-terminal-write-value",
            terminal_write_source.replace(
                "anchor.setAttribute('data-after-terminal', false);",
                "anchor.setAttribute('data-after-terminal', anchor);",
            ),
        ),
        (
            "unsupported-terminal-write-effect",
            terminal_write_source.replace(
                "anchor.setAttribute('data-after-terminal', false);",
                "external(anchor); anchor.setAttribute('data-after-terminal', false);",
            ),
        ),
        (
            "normal-terminal-read-write-close",
            terminal_write_value_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "normal-terminal-read-write-getter-close",
            terminal_write_value_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-terminal-read-write-close",
            terminal_write_value_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "return-terminal-read-write-getter-close",
            terminal_write_value_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-terminal-read-write-name",
            terminal_write_value_source.replace(
                "anchor.setAttribute('data-after-terminal', anchor.hasAttribute('data-unvisited'));",
                "anchor.setAttribute('bad name', anchor.hasAttribute('data-unvisited'));",
            ),
        ),
        (
            "bad-terminal-read-write-receiver",
            terminal_write_value_source.replace(
                "anchor.setAttribute('data-after-terminal', anchor.hasAttribute('data-unvisited'));",
                "(0).setAttribute('data-after-terminal', anchor.hasAttribute('data-unvisited'));",
            ),
        ),
        (
            "invalid-terminal-read-write-arity",
            terminal_write_value_source.replace(
                "anchor.setAttribute('data-after-terminal', anchor.hasAttribute('data-unvisited'));",
                "anchor.setAttribute('data-after-terminal', anchor.hasAttribute('data-unvisited'), false);",
            ),
        ),
        (
            "bad-terminal-read-write-read-receiver",
            terminal_write_value_source.replace(
                "anchor.setAttribute('data-after-terminal', anchor.hasAttribute('data-unvisited'));",
                "anchor.setAttribute('data-after-terminal', (0).hasAttribute('data-unvisited'));",
            ),
        ),
        (
            "unsupported-terminal-read-write-effect",
            terminal_write_value_source.replace(
                "anchor.setAttribute('data-after-terminal', anchor.hasAttribute('data-unvisited'));",
                "external(anchor); anchor.setAttribute('data-after-terminal', anchor.hasAttribute('data-unvisited'));",
            ),
        ),
        (
            "normal-terminal-earlier-read-write-close",
            terminal_write_earlier_value_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-terminal-earlier-read-write-close",
            terminal_write_earlier_value_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-terminal-earlier-read-write-name",
            terminal_write_earlier_value_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('bad name', present);",
            ),
        ),
        (
            "bad-terminal-earlier-read-write-receiver",
            terminal_write_earlier_value_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "(0).setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "invalid-terminal-earlier-read-write-arity",
            terminal_write_earlier_value_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('data-after-terminal', present, false);",
            ),
        ),
        (
            "bad-terminal-earlier-read-write-read-receiver",
            terminal_write_earlier_value_source.replace(
                "const present = anchor.hasAttribute('data-closed');",
                "const present = (0).hasAttribute('data-closed');",
            ),
        ),
        (
            "unsupported-terminal-earlier-read-write-reuse",
            terminal_write_earlier_value_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('data-after-terminal', present); anchor.setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "unsupported-terminal-earlier-read-write-effect",
            terminal_write_earlier_value_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "external(anchor); anchor.setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "normal-before-selector-read-write-close",
            before_selector_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-before-selector-read-write-close",
            before_selector_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-before-selector-read-write-name",
            before_selector_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('bad name', present);",
            ),
        ),
        (
            "bad-before-selector-read-write-receiver",
            before_selector_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "(0).setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "invalid-before-selector-read-write-arity",
            before_selector_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('data-after-terminal', present, false);",
            ),
        ),
        (
            "bad-before-selector-read-write-read-receiver",
            before_selector_read_source.replace(
                "const present = anchor.hasAttribute('data-closed');",
                "const present = (0).hasAttribute('data-closed');",
            ),
        ),
        (
            "bad-before-selector-read-write-ignored-receiver",
            before_selector_read_source.replace(
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-unvisited');",
                "anchor.matches('[data-closed=false]'); (0).hasAttribute('data-unvisited');",
            ),
        ),
        (
            "unsupported-before-selector-read-write-reuse",
            before_selector_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('data-after-terminal', present); anchor.setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "unsupported-before-selector-read-write-extra-use",
            before_selector_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "external(present); anchor.setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "normal-before-first-selector-read-write-close",
            before_first_selector_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-before-first-selector-read-write-close",
            before_first_selector_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-before-first-selector-read-write-name",
            before_first_selector_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('bad name', present);",
            ),
        ),
        (
            "bad-before-first-selector-read-write-receiver",
            before_first_selector_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "(0).setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "invalid-before-first-selector-read-write-arity",
            before_first_selector_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('data-after-terminal', present, false);",
            ),
        ),
        (
            "bad-before-first-selector-read-write-read-receiver",
            before_first_selector_read_source.replace(
                "const present = anchor.hasAttribute('data-closed');",
                "const present = (0).hasAttribute('data-closed');",
            ),
        ),
        (
            "bad-before-first-selector-read-write-ignored-receiver",
            before_first_selector_read_source.replace(
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-unvisited');",
                "anchor.matches('[data-closed=false]'); (0).hasAttribute('data-unvisited');",
            ),
        ),
        (
            "unsupported-before-first-selector-read-write-reuse",
            before_first_selector_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('data-after-terminal', present); anchor.setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "unsupported-before-first-selector-read-write-extra-use",
            before_first_selector_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "external(present); anchor.setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "normal-before-second-write-read-write-close",
            before_second_write_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-before-second-write-read-write-close",
            before_second_write_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-before-second-write-read-write-name",
            before_second_write_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('bad name', present);",
            ),
        ),
        (
            "bad-before-second-write-read-write-receiver",
            before_second_write_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "(0).setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "invalid-before-second-write-read-write-arity",
            before_second_write_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('data-after-terminal', present, false);",
            ),
        ),
        (
            "bad-before-second-write-read-write-read-receiver",
            before_second_write_read_source.replace(
                "const present = anchor.hasAttribute('data-closed');",
                "const present = (0).hasAttribute('data-closed');",
            ),
        ),
        (
            "bad-before-second-write-read-write-ignored-receiver",
            before_second_write_read_source.replace(
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-unvisited');",
                "anchor.matches('[data-closed=false]'); (0).hasAttribute('data-unvisited');",
            ),
        ),
        (
            "unsupported-before-second-write-read-write-reuse",
            before_second_write_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('data-after-terminal', present); anchor.setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "unsupported-before-second-write-read-write-extra-use",
            before_second_write_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "external(present); anchor.setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "normal-before-first-write-read-write-close",
            before_first_write_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-before-first-write-read-write-close",
            before_first_write_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-before-first-write-read-write-name",
            before_first_write_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('bad name', present);",
            ),
        ),
        (
            "bad-before-first-write-read-write-receiver",
            before_first_write_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "(0).setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "invalid-before-first-write-read-write-arity",
            before_first_write_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('data-after-terminal', present, false);",
            ),
        ),
        (
            "bad-before-first-write-read-write-read-receiver",
            before_first_write_read_source.replace(
                "const present = anchor.hasAttribute('data-closed');",
                "const present = (0).hasAttribute('data-closed');",
            ),
        ),
        (
            "bad-before-first-write-read-write-ignored-receiver",
            before_first_write_read_source.replace(
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-unvisited');",
                "anchor.matches('[data-closed=false]'); (0).hasAttribute('data-unvisited');",
            ),
        ),
        (
            "unsupported-before-first-write-read-write-reuse",
            before_first_write_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('data-after-terminal', present); anchor.setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "unsupported-before-first-write-read-write-extra-use",
            before_first_write_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "external(present); anchor.setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "normal-inside-first-write-read-write-close",
            inside_first_write_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-inside-first-write-read-write-close",
            inside_first_write_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-inside-first-write-read-write-name",
            inside_first_write_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('bad name', present);",
            ),
        ),
        (
            "bad-inside-first-write-read-write-receiver",
            inside_first_write_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "(0).setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "invalid-inside-first-write-read-write-arity",
            inside_first_write_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('data-after-terminal', present, false);",
            ),
        ),
        (
            "bad-inside-first-write-read-write-read-receiver",
            inside_first_write_read_source.replace(
                "present = anchor.hasAttribute('data-closed')",
                "present = (0).hasAttribute('data-closed')",
            ),
        ),
        (
            "bad-inside-first-write-read-write-ignored-receiver",
            inside_first_write_read_source.replace(
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-unvisited');",
                "anchor.matches('[data-closed=false]'); (0).hasAttribute('data-unvisited');",
            ),
        ),
        (
            "unsupported-inside-first-write-read-write-reuse",
            inside_first_write_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('data-after-terminal', present); anchor.setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "unsupported-inside-first-write-read-write-extra-use",
            inside_first_write_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "external(present); anchor.setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "normal-feeding-first-read-write-close",
            feeding_first_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-feeding-first-read-write-close",
            feeding_first_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-feeding-first-read-write-name",
            feeding_first_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('bad name', present);",
            ),
        ),
        (
            "bad-feeding-first-read-write-receiver",
            feeding_first_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "(0).setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "invalid-feeding-first-read-write-arity",
            feeding_first_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('data-after-terminal', present, false);",
            ),
        ),
        (
            "bad-feeding-first-read-write-read-receiver",
            feeding_first_read_source.replace(
                "present = anchor.hasAttribute('data-closed')",
                "present = (0).hasAttribute('data-closed')",
            ),
        ),
        (
            "bad-feeding-first-read-write-ignored-receiver",
            feeding_first_read_source.replace(
                "anchor.matches('[data-closed=false]'); anchor.hasAttribute('data-unvisited');",
                "anchor.matches('[data-closed=false]'); (0).hasAttribute('data-unvisited');",
            ),
        ),
        (
            "unsupported-feeding-first-read-write-reuse",
            feeding_first_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('data-after-terminal', present); anchor.setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "unsupported-feeding-first-read-write-extra-use",
            feeding_first_read_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "external(present); anchor.setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "normal-final-argument-read-write-close",
            final_argument_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-final-argument-read-write-close",
            final_argument_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-final-argument-read-write-name",
            final_argument_read_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
                "anchor.setAttribute('bad name', (anchor.hasAttribute('data-closed'), present));",
            ),
        ),
        (
            "bad-final-argument-read-write-receiver",
            final_argument_read_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
                "(0).setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
            ),
        ),
        (
            "invalid-final-argument-read-write-arity",
            final_argument_read_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
                "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present), false);",
            ),
        ),
        (
            "bad-final-argument-read-write-read-receiver",
            final_argument_read_source.replace(
                "present = anchor.hasAttribute('data-closed')",
                "present = (0).hasAttribute('data-closed')",
            ),
        ),
        (
            "bad-final-argument-read-write-ignored-receiver",
            final_argument_read_source.replace(
                "anchor.hasAttribute('data-closed'), present));",
                "(0).hasAttribute('data-closed'), present));",
            ),
        ),
        (
            "unsupported-final-argument-read-write-reuse",
            final_argument_read_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
                "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present)); anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
            ),
        ),
        (
            "unsupported-final-argument-read-write-extra-use",
            final_argument_read_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
                "external(present); anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
            ),
        ),
        (
            "normal-selector-final-argument-read-write-close",
            selector_final_argument_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-selector-final-argument-read-write-close",
            selector_final_argument_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-selector-final-argument-read-write-name",
            selector_final_argument_read_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
                "anchor.setAttribute('bad name', (anchor.hasAttribute('data-closed'), present));",
            ),
        ),
        (
            "bad-selector-final-argument-read-write-receiver",
            selector_final_argument_read_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
                "(0).setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
            ),
        ),
        (
            "invalid-selector-final-argument-read-write-arity",
            selector_final_argument_read_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
                "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present), false);",
            ),
        ),
        (
            "bad-selector-final-argument-read-write-read-receiver",
            selector_final_argument_read_source.replace(
                "const present = anchor.matches('[data-closed=false]')",
                "const present = (0).matches('[data-closed=false]')",
            ),
        ),
        (
            "bad-selector-final-argument-read-write-ignored-receiver",
            selector_final_argument_read_source.replace(
                "anchor.hasAttribute('data-closed'), present));",
                "(0).hasAttribute('data-closed'), present));",
            ),
        ),
        (
            "unsupported-selector-final-argument-read-write-reuse",
            selector_final_argument_read_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
                "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present)); anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
            ),
        ),
        (
            "unsupported-selector-final-argument-read-write-extra-use",
            selector_final_argument_read_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
                "external(present); anchor.setAttribute('data-after-terminal', (anchor.hasAttribute('data-closed'), present));",
            ),
        ),
        (
            "invalid-selector-final-argument-read-selector",
            selector_final_argument_read_source.replace(
                "const present = anchor.matches('[data-closed=false]');",
                "const present = anchor.matches('[');",
            ),
        ),
        (
            "normal-selector-inside-final-argument-close",
            selector_inside_final_argument_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-selector-inside-final-argument-close",
            selector_inside_final_argument_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-selector-inside-final-argument-selector",
            selector_inside_final_argument_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.matches('[data-closed=false]'), present));",
                "anchor.setAttribute('data-after-terminal', (anchor.matches('['), present));",
            ),
        ),
        (
            "dynamic-selector-inside-final-argument-selector",
            selector_inside_final_argument_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.matches('[data-closed=false]'), present));",
                "anchor.setAttribute('data-after-terminal', (anchor.matches(anchor), present));",
            ),
        ),
        (
            "bad-selector-inside-final-argument-receiver",
            selector_inside_final_argument_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.matches('[data-closed=false]'), present));",
                "anchor.setAttribute('data-after-terminal', ((0).matches('[data-closed=false]'), present));",
            ),
        ),
        (
            "invalid-selector-inside-final-argument-arity",
            selector_inside_final_argument_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.matches('[data-closed=false]'), present));",
                "anchor.setAttribute('data-after-terminal', (anchor.matches('[data-closed=false]', false), present));",
            ),
        ),
        (
            "unsupported-selector-inside-final-argument-reuse",
            selector_inside_final_argument_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.matches('[data-closed=false]'), present));",
                "anchor.setAttribute('data-after-terminal', (anchor.matches('[data-closed=false]'), present)); anchor.setAttribute('data-after-terminal', (anchor.matches('[data-closed=false]'), present));",
            ),
        ),
        (
            "unsupported-selector-inside-final-argument-extra-use",
            selector_inside_final_argument_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.matches('[data-closed=false]'), present));",
                "external(present); anchor.setAttribute('data-after-terminal', (anchor.matches('[data-closed=false]'), present));",
            ),
        ),
        (
            "invalid-selector-inside-final-argument-write-name",
            selector_inside_final_argument_source.replace(
                "anchor.setAttribute('data-after-terminal', (anchor.matches('[data-closed=false]'), present));",
                "anchor.setAttribute('bad name', (anchor.matches('[data-closed=false]'), present));",
            ),
        ),
        (
            "normal-selector-inside-first-argument-close",
            selector_inside_first_argument_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-selector-inside-first-argument-close",
            selector_inside_first_argument_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-selector-inside-first-argument-selector",
            selector_inside_first_argument_source.replace(
                "anchor.setAttribute('data-closed', anchor.matches('[data-closed=false]'));",
                "anchor.setAttribute('data-closed', anchor.matches('['));",
            ),
        ),
        (
            "dynamic-selector-inside-first-argument-selector",
            selector_inside_first_argument_source.replace(
                "anchor.setAttribute('data-closed', anchor.matches('[data-closed=false]'));",
                "anchor.setAttribute('data-closed', anchor.matches(anchor));",
            ),
        ),
        (
            "bad-selector-inside-first-argument-receiver",
            selector_inside_first_argument_source.replace(
                "anchor.setAttribute('data-closed', anchor.matches('[data-closed=false]'));",
                "anchor.setAttribute('data-closed', (0).matches('[data-closed=false]'));",
            ),
        ),
        (
            "invalid-selector-inside-first-argument-arity",
            selector_inside_first_argument_source.replace(
                "anchor.setAttribute('data-closed', anchor.matches('[data-closed=false]'));",
                "anchor.setAttribute('data-closed', anchor.matches('[data-closed=false]', false));",
            ),
        ),
        (
            "unsupported-selector-inside-first-argument-reuse",
            selector_inside_first_argument_source.replace(
                "anchor.setAttribute('data-closed', anchor.matches('[data-closed=false]'));",
                "let first; anchor.setAttribute('data-closed', (first = anchor.matches('[data-closed=false]'))); anchor.setAttribute('data-closed', first);",
            ),
        ),
        (
            "unsupported-selector-inside-first-argument-extra-use",
            selector_inside_first_argument_source.replace(
                "anchor.setAttribute('data-closed', anchor.matches('[data-closed=false]'));",
                "let first; anchor.setAttribute('data-closed', (first = anchor.matches('[data-closed=false]'))); external(first);",
            ),
        ),
        (
            "invalid-selector-inside-first-argument-write-name",
            selector_inside_first_argument_source.replace(
                "anchor.setAttribute('data-closed', anchor.matches('[data-closed=false]'));",
                "anchor.setAttribute('bad name', anchor.matches('[data-closed=false]'));",
            ),
        ),
        (
            "normal-selector-before-first-write-close",
            selector_before_first_write_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-selector-before-first-write-close",
            selector_before_first_write_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-selector-before-first-write-selector",
            selector_before_first_write_source.replace(
                "const first = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', first);",
                "const first = anchor.matches('['); anchor.setAttribute('data-closed', first);",
            ),
        ),
        (
            "dynamic-selector-before-first-write-selector",
            selector_before_first_write_source.replace(
                "const first = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', first);",
                "const first = anchor.matches(anchor); anchor.setAttribute('data-closed', first);",
            ),
        ),
        (
            "bad-selector-before-first-write-receiver",
            selector_before_first_write_source.replace(
                "const first = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', first);",
                "const first = (0).matches('[data-closed=false]'); anchor.setAttribute('data-closed', first);",
            ),
        ),
        (
            "invalid-selector-before-first-write-arity",
            selector_before_first_write_source.replace(
                "const first = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', first);",
                "const first = anchor.matches('[data-closed=false]', false); anchor.setAttribute('data-closed', first);",
            ),
        ),
        (
            "unsupported-selector-before-first-write-reuse",
            selector_before_first_write_source.replace(
                "const first = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', first);",
                "const first = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', first); anchor.setAttribute('data-closed', first);",
            ),
        ),
        (
            "unsupported-selector-before-first-write-extra-use",
            selector_before_first_write_source.replace(
                "const first = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', first);",
                "const first = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', first); external(first);",
            ),
        ),
        (
            "invalid-selector-before-first-write-name",
            selector_before_first_write_source.replace(
                "const first = anchor.matches('[data-closed=false]'); anchor.setAttribute('data-closed', first);",
                "const first = anchor.matches('[data-closed=false]'); anchor.setAttribute('bad name', first);",
            ),
        ),
        (
            "normal-selector-reused-second-write-close",
            selector_reused_second_write_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-selector-reused-second-write-close",
            selector_reused_second_write_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-selector-reused-second-write-selector",
            selector_reused_second_write_source.replace(
                "const first = anchor.matches('[data-closed=false]');",
                "const first = anchor.matches('[');",
            ),
        ),
        (
            "dynamic-selector-reused-second-write-selector",
            selector_reused_second_write_source.replace(
                "const first = anchor.matches('[data-closed=false]');",
                "const first = anchor.matches(anchor);",
            ),
        ),
        (
            "bad-selector-reused-second-write-receiver",
            selector_reused_second_write_source.replace(
                "const first = anchor.matches('[data-closed=false]');",
                "const first = (0).matches('[data-closed=false]');",
            ),
        ),
        (
            "invalid-selector-reused-second-write-arity",
            selector_reused_second_write_source.replace(
                "const first = anchor.matches('[data-closed=false]');",
                "const first = anchor.matches('[data-closed=false]', false);",
            ),
        ),
        (
            "unsupported-selector-reused-second-write-extra-use",
            selector_reused_second_write_source.replace(
                "const first = anchor.matches('[data-closed=false]');",
                "const first = anchor.matches('[data-closed=false]'); external(first);",
            ),
        ),
        (
            "invalid-selector-reused-second-write-name",
            selector_reused_second_write_source.replace(
                "anchor.setAttribute('data-closed', first); anchor.matches",
                "anchor.setAttribute('bad name', first); anchor.matches",
            ),
        ),
        (
            "unsupported-selector-reused-second-write-name-use",
            selector_reused_second_write_source.replace(
                "anchor.setAttribute('data-closed', first); anchor.matches",
                "anchor.setAttribute(first, first); anchor.matches",
            ),
        ),
        (
            "normal-selector-guarded-second-write-close",
            selector_guarded_second_write_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-selector-guarded-second-write-close",
            selector_guarded_second_write_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-selector-guarded-second-write-selector",
            selector_guarded_second_write_source.replace(
                "const first = anchor.matches('[data-closed=false]');",
                "const first = anchor.matches('[');",
            ),
        ),
        (
            "dynamic-selector-guarded-second-write-selector",
            selector_guarded_second_write_source.replace(
                "const first = anchor.matches('[data-closed=false]');",
                "const first = anchor.matches(anchor);",
            ),
        ),
        (
            "bad-selector-guarded-second-write-receiver",
            selector_guarded_second_write_source.replace(
                "if (first) anchor.setAttribute('data-closed', first);",
                "if (first) (0).setAttribute('data-closed', first);",
            ),
        ),
        (
            "invalid-selector-guarded-second-write-arity",
            selector_guarded_second_write_source.replace(
                "if (first) anchor.setAttribute('data-closed', first);",
                "if (first) anchor.setAttribute('data-closed', first, false);",
            ),
        ),
        (
            "unsupported-selector-guarded-second-write-extra-use",
            selector_guarded_second_write_source.replace(
                "if (first) anchor.setAttribute('data-closed', first);",
                "if (first) { external(first); anchor.setAttribute('data-closed', first); }",
            ),
        ),
        (
            "invalid-selector-guarded-second-write-name",
            selector_guarded_second_write_source.replace(
                "if (first) anchor.setAttribute('data-closed', first);",
                "if (first) anchor.setAttribute('bad name', first);",
            ),
        ),
        (
            "unsupported-selector-guarded-second-write-name-use",
            selector_guarded_second_write_source.replace(
                "if (first) anchor.setAttribute('data-closed', first);",
                "if (first) anchor.setAttribute(first, first);",
            ),
        ),
        (
            "normal-selector-guarded-second-read-close",
            selector_guarded_second_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-selector-guarded-second-read-close",
            selector_guarded_second_read_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-selector-guarded-second-read-selector",
            selector_guarded_second_read_source.replace(
                "if (first) anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
                "if (first) anchor.setAttribute('data-closed', anchor.matches('['));",
            ),
        ),
        (
            "dynamic-selector-guarded-second-read-selector",
            selector_guarded_second_read_source.replace(
                "if (first) anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
                "if (first) anchor.setAttribute('data-closed', anchor.matches(anchor));",
            ),
        ),
        (
            "bad-selector-guarded-second-read-receiver",
            selector_guarded_second_read_source.replace(
                "if (first) anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
                "if (first) anchor.setAttribute('data-closed', (0).hasAttribute('data-visited'));",
            ),
        ),
        (
            "invalid-selector-guarded-second-read-arity",
            selector_guarded_second_read_source.replace(
                "if (first) anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
                "if (first) anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited', false));",
            ),
        ),
        (
            "unsupported-selector-guarded-second-read-extra-use",
            selector_guarded_second_read_source.replace(
                "if (first) anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
                "if (first) { const read = anchor.hasAttribute('data-visited'); external(read); anchor.setAttribute('data-closed', read); }",
            ),
        ),
        (
            "invalid-selector-guarded-second-read-name",
            selector_guarded_second_read_source.replace(
                "if (first) anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
                "if (first) anchor.setAttribute('bad name', anchor.hasAttribute('data-visited'));",
            ),
        ),
        (
            "unsupported-selector-guarded-second-read-name-use",
            selector_guarded_second_read_source.replace(
                "if (first) anchor.setAttribute('data-closed', anchor.hasAttribute('data-visited'));",
                "if (first) { const read = anchor.hasAttribute('data-visited'); anchor.setAttribute(read, read); }",
            ),
        ),
        (
            "normal-selector-guarded-second-two-reads-close",
            selector_guarded_second_two_reads_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-selector-guarded-second-two-reads-close",
            selector_guarded_second_two_reads_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-selector-guarded-second-two-reads-ignored-selector",
            selector_guarded_second_two_reads_source.replace(
                "if (first) anchor.setAttribute('data-closed', (anchor.hasAttribute('data-visited'), anchor.hasAttribute('data-unvisited')));",
                "if (first) anchor.setAttribute('data-closed', (anchor.matches('['), anchor.hasAttribute('data-unvisited')));",
            ),
        ),
        (
            "dynamic-selector-guarded-second-two-reads-ignored-selector",
            selector_guarded_second_two_reads_source.replace(
                "if (first) anchor.setAttribute('data-closed', (anchor.hasAttribute('data-visited'), anchor.hasAttribute('data-unvisited')));",
                "if (first) anchor.setAttribute('data-closed', (anchor.matches(anchor), anchor.hasAttribute('data-unvisited')));",
            ),
        ),
        (
            "invalid-selector-guarded-second-two-reads-feeding-selector",
            selector_guarded_second_two_reads_source.replace(
                "if (first) anchor.setAttribute('data-closed', (anchor.hasAttribute('data-visited'), anchor.hasAttribute('data-unvisited')));",
                "if (first) anchor.setAttribute('data-closed', (anchor.hasAttribute('data-visited'), anchor.matches('[')));",
            ),
        ),
        (
            "bad-selector-guarded-second-two-reads-receiver",
            selector_guarded_second_two_reads_source.replace(
                "if (first) anchor.setAttribute('data-closed', (anchor.hasAttribute('data-visited'), anchor.hasAttribute('data-unvisited')));",
                "if (first) anchor.setAttribute('data-closed', ((0).hasAttribute('data-visited'), anchor.hasAttribute('data-unvisited')));",
            ),
        ),
        (
            "invalid-selector-guarded-second-two-reads-arity",
            selector_guarded_second_two_reads_source.replace(
                "if (first) anchor.setAttribute('data-closed', (anchor.hasAttribute('data-visited'), anchor.hasAttribute('data-unvisited')));",
                "if (first) anchor.setAttribute('data-closed', (anchor.hasAttribute('data-visited', false), anchor.hasAttribute('data-unvisited')));",
            ),
        ),
        (
            "unsupported-selector-guarded-second-two-reads-extra-use",
            selector_guarded_second_two_reads_source.replace(
                "if (first) anchor.setAttribute('data-closed', (anchor.hasAttribute('data-visited'), anchor.hasAttribute('data-unvisited')));",
                "if (first) { const read = anchor.hasAttribute('data-visited'); external(read); anchor.setAttribute('data-closed', (read, anchor.hasAttribute('data-unvisited'))); }",
            ),
        ),
        (
            "invalid-selector-guarded-second-two-reads-name",
            selector_guarded_second_two_reads_source.replace(
                "if (first) anchor.setAttribute('data-closed', (anchor.hasAttribute('data-visited'), anchor.hasAttribute('data-unvisited')));",
                "if (first) anchor.setAttribute('bad name', (anchor.hasAttribute('data-visited'), anchor.hasAttribute('data-unvisited')));",
            ),
        ),
        (
            "unsupported-selector-guarded-second-two-reads-name-use",
            selector_guarded_second_two_reads_source.replace(
                "if (first) anchor.setAttribute('data-closed', (anchor.hasAttribute('data-visited'), anchor.hasAttribute('data-unvisited')));",
                "if (first) { const read = anchor.hasAttribute('data-visited'); anchor.setAttribute(read, anchor.hasAttribute('data-unvisited')); }",
            ),
        ),
        (
            "normal-selector-guarded-second-postread-close",
            selector_guarded_second_postread_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-selector-guarded-second-postread-close",
            selector_guarded_second_postread_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-selector-guarded-second-postread-selector",
            selector_guarded_second_postread_source.replace(
                "anchor.hasAttribute('data-closed'); }",
                "anchor.matches('['); }",
            ),
        ),
        (
            "dynamic-selector-guarded-second-postread-selector",
            selector_guarded_second_postread_source.replace(
                "anchor.hasAttribute('data-closed'); }",
                "anchor.matches(anchor); }",
            ),
        ),
        (
            "bad-selector-guarded-second-postread-receiver",
            selector_guarded_second_postread_source.replace(
                "anchor.hasAttribute('data-closed'); }",
                "(0).hasAttribute('data-closed'); }",
            ),
        ),
        (
            "invalid-selector-guarded-second-postread-arity",
            selector_guarded_second_postread_source.replace(
                "anchor.hasAttribute('data-closed'); }",
                "anchor.hasAttribute('data-closed', false); }",
            ),
        ),
        (
            "unsupported-selector-guarded-second-postread-observer",
            selector_guarded_second_postread_source.replace(
                "anchor.hasAttribute('data-closed'); }",
                "external(anchor.hasAttribute('data-closed')); }",
            ),
        ),
        (
            "unsupported-selector-guarded-second-postread-method",
            selector_guarded_second_postread_source.replace(
                "anchor.hasAttribute('data-closed'); }",
                "anchor.hasAttribute; }",
            ),
        ),
        (
            "unsupported-selector-guards-third-write-postread",
            selector_guarded_second_postread_source.replace(
                "anchor.hasAttribute('data-closed'); }",
                "anchor.setAttribute('data-after-second', anchor.hasAttribute('data-closed')); }",
            ),
        ),
        (
            "normal-terminal-selector-write-close",
            terminal_write_selector_value_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-terminal-selector-write-close",
            terminal_write_selector_value_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-terminal-selector-write-name",
            terminal_write_selector_value_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('bad name', present);",
            ),
        ),
        (
            "bad-terminal-selector-write-receiver",
            terminal_write_selector_value_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "(0).setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "invalid-terminal-selector-write-arity",
            terminal_write_selector_value_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('data-after-terminal', present, false);",
            ),
        ),
        (
            "bad-terminal-selector-write-selector-receiver",
            terminal_write_selector_value_source.replace(
                "const present = anchor.matches('[data-closed=false]');",
                "const present = (0).matches('[data-closed=false]');",
            ),
        ),
        (
            "invalid-terminal-selector-write-selector",
            terminal_write_selector_value_source.replace(
                "const present = anchor.matches('[data-closed=false]');",
                "const present = anchor.matches('[');",
            ),
        ),
        (
            "unsupported-terminal-selector-write-reuse",
            terminal_write_selector_value_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "anchor.setAttribute('data-after-terminal', present); anchor.setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "unsupported-terminal-selector-write-value-effect",
            terminal_write_selector_value_source.replace(
                "anchor.setAttribute('data-after-terminal', present);",
                "external(present); anchor.setAttribute('data-after-terminal', present);",
            ),
        ),
        (
            "normal-third-postselector-result-close",
            selector_result_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-third-postselector-result-close",
            selector_result_throwing_source.replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "normal-third-postselector-result-getter-close",
            selector_result_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "break;",
            ),
        ),
        (
            "return-third-postselector-result-getter-close",
            selector_result_throwing_source.replace("return() {", "get return() {").replace(
                "throw (node.setAttribute('data-visited', 'yes'), anchor.hasAttribute('data-closed'));",
                "return anchor.hasAttribute('data-closed');",
            ),
        ),
        (
            "invalid-third-postselector-result-selector",
            selector_result_throwing_source.replace("'[data-closed]'", "'['"),
        ),
        (
            "dynamic-third-postselector-result-selector",
            selector_result_throwing_source.replace("'[data-closed]'", "anchor"),
        ),
        (
            "invalid-third-postselector-result-name",
            selector_result_throwing_source.replace(
                "anchor.setAttribute('data-closed', matched);",
                "anchor.setAttribute('bad name', matched);",
            ),
        ),
        (
            "bad-third-postselector-result-receiver",
            selector_result_throwing_source.replace(
                "anchor.setAttribute('data-closed', matched);",
                "(0).setAttribute('data-closed', matched);",
            ),
        ),
        (
            "bad-third-postselector-result-selector-receiver",
            selector_result_throwing_source.replace("anchor.matches(", "(0).matches("),
        ),
        (
            "unsupported-third-postselector-result-escape",
            selector_result_throwing_source.replace(
                "anchor.setAttribute('data-closed', matched);",
                "anchor.setAttribute('data-closed', matched); external(matched);",
            ),
        ),
        (
            "unsupported-third-postselector-result-type",
            selector_result_throwing_source.replace(
                "anchor.setAttribute('data-closed', matched);",
                "anchor.setAttribute('data-closed', {matched: matched});",
            ),
        ),
        (
            "invalid-postwrite-close-name",
            postwrite_throwing_source.replace(
                "anchor.setAttribute('data-closed',", "anchor.setAttribute('bad name',"
            ),
        ),
        ("invalid-close-name", throwing_close.replace("data-closed", "bad name")),
        ("unknown-close-throw", throwing_close.replace("throw 2;", "throw anchor;")),
        ("normal-primitive-close", refusals()["primitive-return-result"]),
        (
            "return-primitive-close",
            BODY_RETURN_BRANCH_EXPRESSION_SOURCE.replace("return {};", "return 2;"),
        ),
        ("normal-throwing-close", source(True).replace("return {};", "throw 2;")),
        (
            "return-throwing-close",
            BODY_RETURN_BRANCH_EXPRESSION_SOURCE.replace("return {};", "throw 2;"),
        ),
        (
            "normal-getter-close",
            getter_close.replace("throw 1;", "node.setAttribute('data-visited', 'yes');"),
        ),
        ("return-getter-close", getter_close.replace("throw 1;", "return 1;")),
        ("returning-getter-close", getter_close.replace("throw 2;", "return {};")),
        ("unknown-getter-throw", getter_close.replace("throw 2;", "throw anchor;")),
        (
            "normal-mutable-throwing-close",
            mutable_throwing_source.replace("throw (count += 2, count);", "break;"),
        ),
        (
            "return-mutable-throwing-close",
            mutable_throwing_source.replace("throw (count += 2, count);", "return count;"),
        ),
        (
            "normal-mutable-nonliteral-close",
            nonliteral_throwing_source.replace("throw (count += 2, count);", "break;"),
        ),
        (
            "return-mutable-nonliteral-close",
            nonliteral_throwing_source.replace("throw (count += 2, count);", "return count;"),
        ),
        (
            "unknown-mutable-close-producer",
            nonliteral_throwing_source.replace("throw count;", "throw external(count);"),
        ),
        (
            "invalid-mutable-nonliteral-close-name",
            nonliteral_throwing_source.replace("data-closed", "bad name"),
        ),
        (
            "invalid-mutable-close-name",
            mutable_throwing_source.replace("data-closed", "bad name"),
        ),
    ):
        ir, contract = dom.prepare(args, label, text, 1, entry_name="customElements")
        contract.update(initial_intrinsics=INTRINSICS)
        for owned in (False, True):
            manifest = dict(contract, provider="ctbrowser-dom-session-v1") if owned else contract
            for optimize in (False, True):
                dom.lower(
                    args,
                    ir,
                    manifest,
                    f"{label}-{owned}-{optimize}",
                    optimize=optimize,
                    success=False,
                )
                refused += 1
    print(
        f"Saved primitive DOM throw: {executions} native executions, {refused} refusals, "
        f"{observations} Node/VM observations"
    )


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
    observations = oracles(args) + mixed_oracles(args)
    compilers = find_compilers()
    compilers[1] = args.clang
    includes, libraries = dom.link_options(args)
    protected_attribute(args, compilers, includes, libraries)
    saved_throws(args, compilers, includes, libraries)
    executions, refused = mixed_selectors(args, compilers)
    admitted = 0
    for label, text, breaking, results, resetting, closed in POSITIVES:
        selecting = label in SELECTOR_SOURCES
        selected_includes, selected_libraries = (
            dom.link_options(args, selectors=True) if selecting else (includes, libraries)
        )
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
                if selecting:
                    checks = (
                        "style::engine selectors{atoms}, foreign_selectors{foreign_atoms};\n"
                        + checks.replace("@ENTRY@(alias)", "@ENTRY@(alias, selectors)")
                        .replace("@ENTRY@(foreign)", "@ENTRY@(foreign, foreign_selectors)")
                        .replace("@ENTRY@(invalid)", "@ENTRY@(invalid, selectors)")
                    )
                    checks += r"""
        bool incompatible = false;
        try { (void)@ENTRY@(alias, foreign_selectors); }
        catch (const std::invalid_argument &) { incompatible = true; }
        assert(incompatible && doc.take_writes().empty());
"""
                if label in ("body-return", "body-return-ordered"):
                    checks = checks.replace("@BREAKING@ && stopping", "true")
                if label == "body-return-multiple":
                    checks = (
                        checks.replace("{0u, 1u, 2u}", "{0u, 1u, 2u, 3u}")
                        .replace("mode != 0", "mode == 1 || mode == 2")
                        .replace("mode != 2", "mode < 2")
                        .replace("@BREAKING@ && stopping", "stopping || mode == 0")
                    )
                if label == "body-return":
                    checks = checks.replace(
                        "check_writes({next, yielded, visited,", "check_writes({next, yielded,"
                    ).replace(
                        'assert(target.read().attribute_value(id, visited) == "yes");',
                        "assert(!target.read().has_attribute(id, visited));",
                    )
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
                value = (
                    "static_cast<bool>(call())" if normal in ("true", "false") else "call().value()"
                )
                if label == "body-return-multiple":
                    checks = checks.replace("@FIRST_RESULT@", f"{value} == (mode == 3)")
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
                dom.standalone(
                    args, native, name, checks, compilers, selected_includes, selected_libraries
                )
                executions += 2 * len(compilers)
                controls = [("budget", manifest, 0)]
                required_intrinsics = ["Symbol", "Object", *SNAPSHOT_INTRINSICS[4:]]
                if selecting and "Element.prototype" in text:
                    required_intrinsics += ["Element", "Function"]
                for intrinsic in required_intrinsics:
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
        if name in (
            "body-throw",
            "body-throw-boolean-snapshot",
            "body-throw-branch-boolean-snapshot",
            "body-throw-or-return-snapshot",
            "body-throw-number-snapshot",
            "body-throw-branch-number-snapshot",
            "body-throw-close-primitive",
            "body-throw-close-throws",
            "body-throw-close-getter",
        ):
            continue  # Executed above, with the original source unchanged.
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
        f"{observations} Node/VM source-double observations; public DOM/Style/Core"
    )


if __name__ == "__main__":
    main()
