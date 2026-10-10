// Every verifier diagnostic, watched firing.
//
// This file must contain every message produced by a hand-written CTJS
// verifier and must pass under -verify-diagnostics. When you add a verifier
// diagnostic, add a case here; the newest cases are ctjs.create_closure's
// checks of its parallel `enclosing_indices` attribute, which must agree with
// its capture operands slot for slot.
//
// A verifier with no test is a verifier that might not run: the negative
// cases are the point of the file, and a guard nobody watches fail is a guard
// nobody knows is connected.


// RUN: ctjs-opt %s -split-input-file -verify-diagnostics

// ---- ctjs.constant ---------------------------------------------------------
// A builtin attribute is refused, which is the whole reason this dialect
// defines its own: a builtin FloatAttr compares -0.0 equal to 0.0, and
// JavaScript does not.
ctjs.func @builtin_attribute() -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  // expected-error @+1 {{expects a CTJS constant attribute}}
  %bad = ctjs.constant 1.5 : f64
  ctjs.return %bad
}

// -----

// ---- ctjs.func: result count ----------------------------------------------
// expected-error @+1 {{must return exactly one value}}
ctjs.func @no_result() attributes {upvalue_count = 0 : i32} {
  %v = ctjs.constant #ctjs.undefined
  ctjs.return %v
}

// -----

// ---- ctjs.func: result type -----------------------------------------------
// expected-error @+1 {{must return a !ctjs.value}}
ctjs.func @wrong_result() -> i32 attributes {upvalue_count = 0 : i32} {
  %v = ctjs.constant #ctjs.undefined
  ctjs.return %v
}

// -----

// ---- ctjs.func: parameter type --------------------------------------------
// expected-error @+1 {{takes only !ctjs.value parameters}}
ctjs.func @wrong_parameter(%n: i32) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  %v = ctjs.constant #ctjs.undefined
  ctjs.return %v
}

// -----

// ---- ctjs.pop_handler ------------------------------------------------------
// Two pops and no push between them. The runtime's ct_aot_handler_pop takes the
// globally innermost handler without consulting the frame, so a body that pops
// one it never pushed silently takes its caller's catch, and nothing at run
// time reports it. This verifier is what makes it a build error instead.
ctjs.func @unbalanced() -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  ctjs.pop_handler
  // expected-error @+1 {{two pops in one block with no push between them}}
  ctjs.pop_handler
  %v = ctjs.constant #ctjs.undefined
  ctjs.return %v
}

// -----

// ---- ctjs.resume_point: uniqueness ----------------------------------------
// Each resume point is meant to become one arm of a switch over a saved state
// index when generators are lowered, so two with the same index would clash.
// The diagnostic lands on the first resume_point, not the offending one: the
// verifier walks the whole function, so whichever of them is verified first is
// the one that reports, because the message is about the set, not one member.

ctjs.func @duplicate_resume() -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  // expected-error @+1 {{resume point indices must be unique}}
  ctjs.resume_point 0
  ctjs.resume_point 0
  %v = ctjs.constant #ctjs.undefined
  ctjs.return %v
}

// -----

// ---- ctjs.resume_point: density -------------------------------------------
// And a gap is a switch arm that resumes nowhere.
ctjs.func @sparse_resume() -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  // expected-error @+1 {{resume point indices must be dense}}
  ctjs.resume_point 0
  ctjs.resume_point 2
  %v = ctjs.constant #ctjs.undefined
  ctjs.return %v
}

// -----

// ---- ctjs.create_closure: enclosing_indices is parallel, not packed --------
// Two captures and one index. A reader who wrote only the slots the enclosing
// closure fills would get exactly this, and it is wrong silently: index 0 would
// then name slot 1's descriptor, and the native lift would pass a different
// binding into a function that compiles clean and prints a wrong number. That
// is the same trap $upvalues itself carries, checked again where it can fail.
ctjs.func @packed_indices(%a: !ctjs.value, %b: !ctjs.value)
    -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  // expected-error @+1 {{`enclosing_indices` has 1 entries for 2 capture operand(s)}}
  %fn = ctjs.create_closure %a[0] this %b captures %a, %b
        {enclosing_indices = array<i32: 0>}
  ctjs.return %fn
}

// -----

// ---- ctjs.create_closure: -1 is the only sentinel --------------------------
// A second negative number would be a second meaning nobody has defined, and
// the lift reads `k >= 0` to decide whether the slot is filled at all, so -2
// would read as "not filled" while looking deliberate.
ctjs.func @other_sentinel(%a: !ctjs.value, %b: !ctjs.value)
    -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  // expected-error @+1 {{`enclosing_indices[1]` is -2; the only negative entry is -1}}
  %fn = ctjs.create_closure %a[0] this %b captures %a, %b
        {enclosing_indices = array<i32: -1, -2>}
  ctjs.return %fn
}

// -----

// ---- ctjs.create_closure: an index and a cell describe the same slot -------
// The one invariant the native lift depends on and the length check does not
// reach: a slot the enclosing closure fills carries the importer's placeholder,
// because there is no binding of this frame to point at. Here slot 1 says both
// (`enclosing_indices[1] = 0` and a real ctjs.create_cell beside it), and the
// lift asks the operand first, so the index would be silently discarded and the
// wrong binding passed if the index was the truth. The IR would verify, lower,
// compile under -Werror and print a wrong number; this is where it stops.
ctjs.func @index_over_a_cell(%a: !ctjs.value, %b: !ctjs.value)
    -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  %cell = ctjs.create_cell %b
  // expected-error @+1 {{`enclosing_indices[1]` is 0, so slot 1 is filled from the enclosing closure - but its capture operand is a ctjs.create_cell of this frame}}
  %fn = ctjs.create_closure %a[0] this %b captures %a, %cell
        {enclosing_indices = array<i32: -1, 0>}
  ctjs.return %fn
}

// -----

// ---- ctjs.create_closure: an index over a block argument -------------------
// The same disagreement in a more common shape: a capture operand that is a
// block argument (a from_parent_local binding held in a register) rather than
// a ctjs.create_cell. A slot that names an enclosing upvalue and holds one of
// those gives two answers for one slot. Here the lift would take the index
// and drop the operand, which is the mirror image of the case above and just
// as silent.
ctjs.func @index_over_a_register(%a: !ctjs.value, %b: !ctjs.value)
    -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  // expected-error @+1 {{`enclosing_indices[0]` is 1, so slot 0 is filled from the enclosing closure and its capture operand has to be the importer's `ctjs.constant #ctjs.undefined` placeholder}}
  %fn = ctjs.create_closure %a[0] this %b captures %b
        {enclosing_indices = array<i32: 1>}
  ctjs.return %fn
}
