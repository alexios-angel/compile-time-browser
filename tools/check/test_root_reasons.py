"""Checks for root-reasons.py, run by pytest and by `root-reasons.py --self-test`.

PLANTED is a lowered module with hand-placed refusals; EXPECTED is the report
worked out by hand from it.
"""

from pathlib import Path

from selftest import load

root_reasons = load(Path(__file__).with_name("root-reasons.py"))

PLANTED = """\
  ctjs.func @_script_$0(%arg0) -> !ctjs.value {
    %1 = ctjs.create_closure %arg2[1] this %0 {ctnative.lifted}
    %2 = ctjs.call_direct @a$1(%x)
    %3 = ctjs.create_closure %arg2[5] this %0 {ctnative.closure_reason = "it is passed as an argument"}
    %4 = ctjs.call_direct @f$6(%x)
    %5 = ctjs.create_closure %arg2[9] this %0 {ctnative.closure_reason = "it is stored into an object or an array"}
    %6 = ctjs.create_closure %arg2[10] this %0 {ctnative.class_reason = "it has static method `go`; static members are the next slice", ctnative.closure_reason = "it is the value of a local binding this tier cannot call directly"}
    %7 = ctjs.constant #ctjs.string<"prototype"> {}
    %8 = ctjs.create_object {}
    ctjs.set_property %6[%7], %8
    %9 = ctjs.create_closure %arg2[11] this %0 {ctnative.closure_reason = "it is stored into an object or an array"}
    %10 = ctjs.constant #ctjs.string<"m"> {}
    ctjs.set_property %8[%10], %9
    %11 = ctjs.create_closure %arg2[12] this %0 {ctnative.closure_reason = "it is stored into an object or an array"}
    ctjs.define_accessor "NAME" on %6 get %11 set %0
  }
  ctjs.func @a$1(%arg0) -> !ctjs.value attributes {ctnative.not_native = "a regular expression literal"} {
    %1 = ctjs.call_direct @b$2(%x)
    %2 = ctjs.call_direct @c$3(%x)
  }
  ctjs.func private @b$2(%arg0) -> !ctjs.value attributes {ctnative.not_native = "parameter 0 is !ctnative.boxed - no caller proves it (x)"} {
    %1 = ctjs.call_direct @c$3(%x)
  }
  ctjs.func private @c$3(%arg0) -> !ctjs.value attributes {ctnative.not_native = "parameter 1 is !ctnative.boxed - no caller proves it (x)"} {
    %1 = ctjs.call_direct @d$4(%x)
  }
  emitc.func @d_4(%arg0) {
  }
  ctjs.func @e$5(%arg0) -> !ctjs.value attributes {ctnative.not_native = "uses its own closure"} {
    %1 = ctjs.create_closure %arg2[7] this %arg0 {ctnative.closure_reason = "capture 0 is filled from the enclosing closure, which did not lift: it is passed as an argument"}
  }
  ctjs.func @f$6(%arg0) -> !ctjs.value attributes {ctnative.not_native = "uses `this` - a closure used as a value: it is stored into an object or an array"} {
    %1 = ctjs.create_closure %arg2[8] this %arg0 {ctnative.closure_reason = "it is stored into an object or an array"}
    %2 = ctjs.call_direct @c$3(%x)
  }
  ctjs.func @g$7(%arg0) -> !ctjs.value attributes {ctnative.not_native = "parameter 0 is !ctnative.boxed - no caller proves it (x)"} {
  }
  ctjs.func @h$8(%arg0) -> !ctjs.value attributes {ctnative.not_native = "uses `this`"} {
  }
  ctjs.func @i$9(%arg0) -> !ctjs.value attributes {ctnative.not_native = "uses `this` - a closure used as a value: it is stored into an object or an array"} {
  }
  ctjs.func @k$10(%arg0) -> !ctjs.value attributes {ctnative.not_native = "uses `this` - a closure used as a value: it is the value of a local binding this tier cannot call directly"} {
  }
  ctjs.func @m$11(%arg0) -> !ctjs.value attributes {ctnative.not_native = "uses `this` - a closure used as a value: it is stored into an object or an array"} {
  }
  ctjs.func @n$12(%arg0) -> !ctjs.value attributes {ctnative.not_native = "uses `this` - a closure used as a value: it is stored into an object or an array"} {
  }
"""

# Expected rows for PLANTED: (reason, roots, blocks, solely).
#  - a$1 is a root (regex); b$2 is behind a$1 only; c$3 is behind a$1 (through
#    a$1 and b$2) and f$6's closure, so it is blocked by both and solely by none.
#  - The closure of e$5 is a root (passed as an argument); e$5 and g$7 (made by
#    a closure filled from e$5's) are behind it. d$4 lowered and is no node.
#  - f$6 and h$8 are behind the closure of h$8 (stored into an object), and
#    i$9 behind its own closure, refused for the same reason: two roots.
#  - k$10 is a refused class's constructor, m$11 a prototype method stored on
#    it and n$12 a static getter: all three are behind the class, not behind
#    their closures' own reasons.
EXPECTED = [
    ("it is stored into an object or an array", 2, 4, 3),
    ("class: it has static method `X`; static members are the next slice", 1, 3, 3),
    ("a regular expression literal", 1, 3, 2),
    ("it is passed as an argument", 1, 2, 2),
]


def test_planted_rows():
    rows = root_reasons.report(root_reasons.blame(*root_reasons.parse(PLANTED)))
    assert rows == EXPECTED, f"got {rows}, expected {EXPECTED}"
