// EVERY VERIFIER DIAGNOSTIC, WATCHED FIRING.
//
// The Phase 8 gate: "invalid.mlir covers every verifier diagnostic and passes
// under -verify-diagnostics". Four operations have a hand-written verifier -
// the plan names exactly those four - and each of their messages appears below.
//
// A VERIFIER WITH NO TEST IS A VERIFIER THAT MIGHT NOT RUN. That is the same
// discipline the runtime side of this project uses everywhere: the negative
// cases are the point of the file, and a guard nobody watches fail is a guard
// nobody knows is connected.

// RUN: ctjs-opt %s -split-input-file -verify-diagnostics

// ---- ctjs.constant ---------------------------------------------------------
// A BUILTIN ATTRIBUTE IS REFUSED, which is the whole reason this dialect
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
// TWO POPS AND NO PUSH BETWEEN THEM. The runtime's ct_aot_handler_pop takes the
// GLOBALLY innermost handler without consulting the frame, so a body that pops
// one it never pushed silently takes its CALLER's catch - and nothing at run
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
// Phase 14 turns these into a switch over a saved state, so a duplicate index
// is two states that cannot be told apart.
// THE DIAGNOSTIC LANDS ON THE FIRST resume_point, not the offending one: the
// verifier walks the whole function, so whichever of them is verified first is
// the one that reports. That is worth knowing rather than working around - a
// message about the set is not a message about one member of it.
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
