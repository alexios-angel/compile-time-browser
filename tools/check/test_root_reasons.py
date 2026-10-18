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
    %3 = ctjs.call %y(%x) {ctnative.string_callback = @p$13}
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
  ctjs.func private @p$13(%arg0) -> !ctjs.value attributes {ctnative.not_native = "parameter 0 is !ctnative.boxed - no caller proves it (x)"} {
  }
"""

# Expected rows for PLANTED: (reason, roots, blocks, solely, then, after).
#  - a$1 is a root (regex); b$2 is behind a$1 only; c$3 is behind a$1 (through
#    a$1 and b$2) and f$6's closure, so it is blocked by both and solely by none:
#    fixing either reason leaves it blocked by the other alone ("then").
#  - The closure of e$5 is a root (passed as an argument); e$5 and g$7 (made by
#    a closure filled from e$5's) are behind it. d$4 lowered and is no node.
#  - f$6 and h$8 are behind the closure of h$8 (stored into an object), and
#    i$9 behind its own closure, refused for the same reason: two roots.
#    p$13, the callback of a replace call in f$6, is behind f$6 too: its
#    parameter comes from that call.
#  - k$10 is a refused class's constructor, m$11 a prototype method stored on
#    it and n$12 a static getter: all three are behind the class, not behind
#    their closures' own reasons.
REGEX = "a regular expression literal"
STORED = "it is stored into an object or an array"
EXPECTED = [
    (STORED, 2, 5, 4, 1, (REGEX, 1)),
    ("class: it has static method `X`; static members are the next slice", 1, 3, 3, 0, None),
    (REGEX, 1, 3, 2, 1, (STORED, 1)),
    ("it is passed as an argument", 1, 2, 2, 0, None),
]


def rows_of(text):
    return root_reasons.report(
        root_reasons.blame(*root_reasons.parse(text)), root_reasons.copies_of(text)
    )


def test_planted_rows():
    rows = rows_of(PLANTED)
    assert rows == EXPECTED, f"got {rows}, expected {EXPECTED}"


# Three copies of g$2 (one per call site) repeat its regex root, and h$3, which
# they all call, is behind each. q$5 and its copy q$6 refuse for different
# reasons, so the source q$5 is blocked by both and solely by neither. w$9 and
# its copy w$10 lowered, so they are named in C++ (w_9, w_10), but a refused
# closure of each is still one root.
COPIED = """\
module attributes {ctnative.copies = {g$7 = "g$2", "g$8" = "g$2", q$6 = "q$5", w$10 = "w$9"}} {
  ctjs.func @_script_$0(%arg0) -> !ctjs.value {
    %1 = ctjs.call_direct @g$2(%x)
    %2 = ctjs.call_direct @g$7(%x)
    %3 = ctjs.call_direct @g$8(%x)
    %4 = ctjs.create_closure %arg2[9] this %0 {ctnative.closure_reason = "it is stored into an object or an array"}
  }
  ctjs.func private @g$2(%arg0) -> !ctjs.value attributes {ctnative.not_native = "a regular expression literal"} {
    %1 = ctjs.call_direct @h$3(%x)
  }
  ctjs.func private @g$7(%arg0) -> !ctjs.value attributes {ctnative.not_native = "a regular expression literal"} {
    %1 = ctjs.call_direct @h$3(%x)
  }
  ctjs.func private @g$8(%arg0) -> !ctjs.value attributes {ctnative.not_native = "a regular expression literal"} {
    %1 = ctjs.call_direct @h$3(%x)
  }
  ctjs.func private @h$3(%arg0) -> !ctjs.value attributes {ctnative.not_native = "parameter 0 is !ctnative.boxed - no caller proves it (x)"} {
  }
  ctjs.func private @q$5(%arg0) -> !ctjs.value attributes {ctnative.not_native = "unstructured control flow"} {
  }
  ctjs.func private @q$6(%arg0) -> !ctjs.value attributes {ctnative.not_native = "a for-in loop"} {
  }
  emitc.func @w_9(%arg0) {
  }
  emitc.func @w_10(%arg0) {
  }
  ctjs.func @m$11(%arg0) -> !ctjs.value attributes {ctnative.not_native = "a closure used as a value: it is stored into an object or an array"} {
    %1 = ctjs.create_closure %arg2[10] this %arg0 {ctnative.closure_reason = "it is stored into an object or an array"}
  }
}
"""


def test_copies_fold_into_their_source():
    # Counted per copy, the regex reason would show 3 roots, 4 blocked and 4
    # solely, each q reason 1 solely, and the stored closures 2 roots.
    assert rows_of(COPIED) == [
        (REGEX, 1, 2, 2, 0, None),
        (STORED, 1, 1, 1, 0, None),
        ("a for-in loop", 1, 1, 0, 1, ("unstructured control flow", 1)),
        ("unstructured control flow", 1, 1, 0, 1, ("a for-in loop", 1)),
    ]
    assert root_reasons.copies_of(COPIED) == {7: 2, 8: 2, 6: 5, 10: 9}


# The constructor lift refused S$1's closure, and the class rule recorded no
# reason, so start$2 (on S$1's prototype) and make$3 (a static on S$1) are
# stored closures only because S$1 is refused. run$4 is stored on a plain
# object and stays a root of its own.
SHAPE = (
    "the instance `X` makes does not have a closed shape - every use of it has to be a "
    "constant-key read or write, or a receiver this lift carries"
)
CONSTRUCTED = """\
  ctjs.func @_script_$0(%arg0) -> !ctjs.value {
    %1 = ctjs.create_object {}
    %2 = ctjs.create_closure %arg2[1] this %0 {ctnative.closure_reason = "the instance `new` makes does not have a closed shape - every use of it has to be a constant-key read or write, or a receiver this lift carries"}
    %3 = ctjs.constant #ctjs.string<"prototype"> {}
    ctjs.set_property %2[%3], %1
    %4 = ctjs.create_closure %arg2[2] this %0 {ctnative.closure_reason = "it is stored into an object or an array - Phase 59 slice 2"}
    %5 = ctjs.constant #ctjs.string<"start"> {}
    ctjs.set_property %1[%5], %4
    %6 = ctjs.create_closure %arg2[3] this %0 {ctnative.closure_reason = "it is stored into an object or an array - Phase 59 slice 2"}
    %7 = ctjs.constant #ctjs.string<"make"> {}
    ctjs.set_property %2[%7], %6
    %8 = ctjs.create_object {}
    %9 = ctjs.create_closure %arg2[4] this %0 {ctnative.closure_reason = "it is stored into an object or an array - Phase 59 slice 2"}
    %10 = ctjs.constant #ctjs.string<"run"> {}
    ctjs.set_property %8[%10], %9
  }
  ctjs.func @S$1(%arg0) -> !ctjs.value attributes {ctnative.not_native = "uses `this` - a closure used as a value: the instance `new` makes does not have a closed shape - every use of it has to be a constant-key read or write, or a receiver this lift carries"} {
  }
  ctjs.func @start$2(%arg0) -> !ctjs.value attributes {ctnative.not_native = "uses `this` - a closure used as a value: it is stored into an object or an array - Phase 59 slice 2"} {
  }
  ctjs.func @make$3(%arg0) -> !ctjs.value attributes {ctnative.not_native = "uses `this` - a closure used as a value: it is stored into an object or an array - Phase 59 slice 2"} {
  }
  ctjs.func @run$4(%arg0) -> !ctjs.value attributes {ctnative.not_native = "uses `this` - a closure used as a value: it is stored into an object or an array - Phase 59 slice 2"} {
  }
"""


def test_members_of_a_refused_constructor_are_behind_it():
    # Without the link, the stored reason would show 3 roots and 3 solely, and
    # the constructor's own reason 1.
    assert rows_of(CONSTRUCTED) == [
        (SHAPE, 1, 3, 3, 0, None),
        ("it is stored into an object or an array - Phase N slice N", 1, 1, 1, 0, None),
    ]


# Four roots that each free one function alone. x$5 and y$6 are behind three
# of them; z$7 is behind b$2 and c$3 only, so fixing either leaves it one fix
# away. Ranked by blocks after solely, a$1 (3 blocked) would come before b$2
# (2 blocked), though fixing a$1 brings nothing else closer.
RANKED = """\
  ctjs.func @a$1(%arg0) -> !ctjs.value attributes {ctnative.not_native = "a regular expression literal"} {
    %1 = ctjs.call_direct @x$5(%x)
    %2 = ctjs.call_direct @y$6(%x)
  }
  ctjs.func @b$2(%arg0) -> !ctjs.value attributes {ctnative.not_native = "unstructured control flow"} {
    %1 = ctjs.call_direct @z$7(%x)
  }
  ctjs.func @c$3(%arg0) -> !ctjs.value attributes {ctnative.not_native = "a for-in loop"} {
    %1 = ctjs.call_direct @x$5(%x)
    %2 = ctjs.call_direct @y$6(%x)
    %3 = ctjs.call_direct @z$7(%x)
  }
  ctjs.func @d$4(%arg0) -> !ctjs.value attributes {ctnative.not_native = "a tagged template"} {
    %1 = ctjs.call_direct @x$5(%x)
    %2 = ctjs.call_direct @y$6(%x)
  }
  ctjs.func private @x$5(%arg0) -> !ctjs.value attributes {ctnative.not_native = "parameter 0 is !ctnative.boxed - no caller proves it (x)"} {
  }
  ctjs.func private @y$6(%arg0) -> !ctjs.value attributes {ctnative.not_native = "parameter 0 is !ctnative.boxed - no caller proves it (x)"} {
  }
  ctjs.func private @z$7(%arg0) -> !ctjs.value attributes {ctnative.not_native = "parameter 0 is !ctnative.boxed - no caller proves it (x)"} {
  }
"""


def test_rank_by_what_a_fix_opens_next():
    assert rows_of(RANKED) == [
        ("a for-in loop", 1, 4, 1, 1, ("unstructured control flow", 1)),
        ("unstructured control flow", 1, 2, 1, 1, ("a for-in loop", 1)),
        (REGEX, 1, 3, 1, 0, None),
        ("a tagged template", 1, 3, 1, 0, None),
    ]
