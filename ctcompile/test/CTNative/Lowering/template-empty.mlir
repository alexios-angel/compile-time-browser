// `template <>` IS NOT A DECLARATION - part 24 Phase 56C, the zero-field guard.
//
// A family of object literals that disagree on a field type is emitted as one
// class template, and the parameter list rides on the class as
// `ctnative.template_params`. The empty shape - `var e = {};`, which
// hasClosedShape admits because its loop over the literal's uses is vacuously
// true - has no field, so it has nothing to parametrise. If it ever arrived
// here with an empty list the emitter would print `template <>`, which is the
// explicit-specialisation syntax and is ill-formed on a primary template: a
// C++ syntax error in a file ctcompile had already declared native, found by
// the -Werror gate three steps later instead of by the pass that emitted it.
//
// The lowering never builds one - one-shape-one-definition.mlir's EMPTY case
// pins the empty shape arriving with no attribute at all - so this is the belt
// under that, and it is a diagnostic rather than an assertion because the
// module is hand-writable IR. The second half is what keeps the guard honest:
// the same class with a parameter in the list must be accepted, or "refuse
// every template" would pass this file too.

// RUN: split-file %s %t
// RUN: not ctjs-translate --mlir-to-cpp %t/empty.mlir 2>&1 | FileCheck %s --check-prefix=EMPTY
// RUN: ctjs-translate --mlir-to-cpp %t/one.mlir | FileCheck %s --check-prefix=ONE

// EMPTY: is empty
// EMPTY-SAME: `template <>` is not a declaration

// ONE: template <class T0>
// ONE-NEXT: class ctn_only {
// ONE: T0 only;

//--- empty.mlir
emitc.class @ctn_only attributes {ctnative.template_params = []} {
  emitc.field @only : f64
}

//--- one.mlir
emitc.class @ctn_only attributes {ctnative.template_params = ["T0"]} {
  emitc.field @only : !emitc.opaque<"T0">
}
