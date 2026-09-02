// Part 24 Stage 53E (the printing policy) and 53F (the pins), as turned on
// at Phase 62½-E. `auto` is an attribute the policy pass sets; the fork's
// emitter is its one consumer and prints the pin after each deduction.
// The pipe carries debug info, or the emitter would see the positions in
// the piped text instead of the locations written here.
//
// RUN: ctjs-opt --ctnative-print-deduced %s | FileCheck %s
// RUN: ctjs-opt --ctnative-print-deduced --mlir-print-debuginfo %s | ctjs-translate --mlir-to-cpp | FileCheck --check-prefix=CPP %s
// RUN: ctjs-opt --ctnative-print-deduced=mutate=1 --mlir-print-debuginfo %s | ctjs-translate --mlir-to-cpp | FileCheck --check-prefix=MUT %s
// RUN: not ctjs-opt --ctnative-print-deduced=mutate=9 %s 2>&1 | FileCheck --check-prefix=NOMUT %s

emitc.include <"cmath">
emitc.func @f(%a: f64, %b: f64) -> f64 {
  // never deduced: a literal initialiser (`auto x = 0;` would be int)
  %k = "emitc.constant"() {value = 2.0 : f64} : () -> f64
  // deduced, pinned at its JavaScript site
  %s = emitc.add %a, %b : (f64, f64) -> f64 loc("fixture.js":3:7)
  // deduced; no loc written, so the site is where this line sits in this file
  %p = emitc.mul %s, %k : (f64, f64) -> f64
  // deduced, a bool
  %c = emitc.cmp lt, %p, %a : (f64, f64) -> i1
  // never deduced: a declaration with no initialiser, and a load
  %v = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<f64>
  emitc.assign %p : f64 to %v : <f64>
  %l = emitc.load %v : <f64>
  // deduced
  %r = emitc.conditional %c, %p, %l : f64
  emitc.return %r : f64
}

// --- the IR keeps every type; only the attribute is added ------------------
//
// (ops inside emitc.func print without their dialect prefix)
//
// CHECK: module attributes {ctnative.deduced_count = 4 : i64}
// CHECK: emitc.include <"cmath">
// CHECK-NEXT: emitc.include <"type_traits">
// CHECK-NEXT: emitc.verbatim "#ifndef CTCOMPILE_NO_TYPE_PINS
// CHECK: "emitc.constant"() <{value = 2.000000e+00 : f64}> : () -> f64
// CHECK-NEXT: {{(emitc\.)?}}add {{.*}} {ctnative.deduced} : (f64, f64) -> f64
// CHECK-NEXT: {{(emitc\.)?}}mul {{.*}} {ctnative.deduced} : (f64, f64) -> f64
// CHECK-NEXT: {{(emitc\.)?}}cmp lt, {{.*}} {ctnative.deduced} : (f64, f64) -> i1
// CHECK-NEXT: "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<f64>
// CHECK-NEXT: {{(emitc\.)?}}assign
// CHECK-NEXT: {{(emitc\.)?}}load
// CHECK-NEXT: {{(emitc\.)?}}conditional {{.*}} {ctnative.deduced} : f64

// --- the C++: auto where marked, a pin after each, the type everywhere else -
//
// CPP: #include <cmath>
// CPP-NEXT: #include <type_traits>
// CPP-NEXT: #ifndef CTCOMPILE_NO_TYPE_PINS
// CPP-NEXT: #define CTCOMPILE_PIN(name, site, ...) static_assert(std::is_same_v<decltype(name), __VA_ARGS__>, "ctcompile: " #name " @ " site)
// CPP-NEXT: #else
// CPP-NEXT: #define CTCOMPILE_PIN(name, site, ...)
// CPP-NEXT: #endif
// CPP: double f(double [[A:v[0-9]+]], double [[B:v[0-9]+]]) {
// CPP-NEXT: double [[K:v[0-9]+]] = {{.*}};
// CPP-NEXT: auto [[S:v[0-9]+]] = [[A]] + [[B]];
// CPP-NEXT: CTCOMPILE_PIN([[S]], "fixture.js:3:7", double);
// CPP-NEXT: auto [[P:v[0-9]+]] = [[S]] * [[K]];
// CPP-NEXT: CTCOMPILE_PIN([[P]], "{{[^"]*}}print-deduced.mlir:{{[0-9]+}}:{{[0-9]+}}", double);
// CPP-NEXT: auto [[C:v[0-9]+]] = [[P]] < [[A]];
// CPP-NEXT: CTCOMPILE_PIN([[C]], "{{[^"]*}}print-deduced.mlir:{{[0-9]+}}:{{[0-9]+}}", bool);
// CPP-NEXT: double [[V:v[0-9]+]];
// CPP-NEXT: [[V]] = [[P]];
// CPP-NEXT: double [[L:v[0-9]+]] = [[V]];
// CPP-NEXT: auto [[R:v[0-9]+]] = [[C]] ? [[P]] : [[L]];
// CPP-NEXT: CTCOMPILE_PIN([[R]], "{{[^"]*}}print-deduced.mlir:{{[0-9]+}}:{{[0-9]+}}", double);
// CPP-NEXT: return [[R]];

// --- the mutation: the first deduced double pinned as int32_t ---------------
//
// MUT: auto [[S:v[0-9]+]] = {{.*}} + {{.*}};
// MUT-NEXT: CTCOMPILE_PIN([[S]], "fixture.js:3:7", int32_t);

// --- a mutation that lands nowhere is a failure, not a pass ---------------
//
// NOMUT: mutate=9 names no deduced double declaration; the module has 3
