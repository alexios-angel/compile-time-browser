// THE FIRST NATIVE ARTEFACT - ctcompile Phase 62½-D, the compilation-unit gate.
//
// HAND-WRITTEN, AND THAT IS THE POINT. This module is what the Phase 62½-C
// lowering is expected to produce for native-fixture.js, written BEFORE that
// lowering exists so that the gate (check-native-unit.cmake) is proven on
// something before there is anything to gate. It is the specification of the
// lowering's output shape: when the lowering lands, its output for the same
// program should be this module up to value names, and the gate should not
// have to change.
//
// THE SHAPE:
//   * one `emitc.func` per JavaScript function, over `f64` (num<f64>), `i32`
//     (num<i32>) and `i1` (bool); the top level is `emitc.func @main() -> i32`
//   * one `emitc.global` per numeric top-level `var`, UNINITIALISED: a `var`
//     starts undefined and its value comes from `main`, so no initial value is
//     part of the program
//   * a mutable local is an `emitc.variable` (an lvalue), read with `emitc.load`
//     and written with `emitc.assign`; a parameter that is reassigned is copied
//     into one on entry; a global is read through `emitc.get_global` + load
//   * `if`/`else` is `emitc.if`. It has no results, so a value that flows out
//     of a branch goes through an lvalue - and an early `return` is the same
//     thing, because `emitc.return` must sit directly in the function body
//   * a counted `for` is `emitc.for` (its test is `i < ub`, so `i <= n` is
//     `i < n + 1`); a pre-tested `while` is `emitc.if` around `emitc.do`,
//     because EmitC has no while-loop op and `do` tests after the body. The
//     `do` condition is an `emitc.expression` that reloads the variable
//   * `+ - * /` are emitc.add/sub/mul/div; `%` on doubles is a call to
//     std::fmod, because emitc.rem is integer-only and JavaScript `%` IS fmod
//   * comparisons are emitc.cmp; `&&` on booleans is emitc.logical_and; `-x` is
//     emitc.unary_minus; a call is emitc.call - call_opaque is for the C
//     library only; i32 -> f64 is emitc.cast at the use that needs it
//   * callees precede callers, because the C++ needs a declaration first
//
// THE OUTPUT CONVENTION - defined here, checked by check-native-unit.cmake:
//   At the end of `main`, after the last top-level statement, every numeric
//   global is printed, one per line, in ascending bytewise order of its name,
//   as
//       std::printf("%s=%.17g\n", "<name>", (double) <global>);
//   and `main` returns 0. %.17g round-trips a double exactly, so the comparison
//   with the interpreter is on the double's VALUE, not on JavaScript's
//   shortest-representation string - Number-to-String is a runtime concern for
//   a later phase. The reference side (NativeReference.cpp) prints the
//   interpreter's double the same way. One exception: the sign of a NaN is not
//   observable in JavaScript, x86 arithmetic produces negative NaNs and
//   constant folding produces positive ones, so the gate compares `-nan` and
//   `nan` as equal.
//
// WHAT IS NOT HERE, deliberately: nothing from ctbrowser/. No include, no
// helper, no `ct_aot_*`. The gate compiles this standalone and runs `nm` on
// the result; a `ctbrowser::script::` symbol in it is the failure the whole
// phase exists to define.

// AS A LIT TEST this file asserts the SHAPE of the emitted C++ and never a
// value: the values are the interpreter's to judge, in check-native-unit.cmake.
// The implicit check-not is the gate's rule in one word - nothing of
// ctbrowser's reaches the translation unit.
//
// RUN: ctjs-translate --mlir-to-cpp %s | FileCheck %s --implicit-check-not=ctbrowser
//
// CHECK: #include <cstdint>
// CHECK: int32_t fib20;
// CHECK: double clamped;
// CHECK-LABEL: int32_t fib(int32_t
// CHECK: fib(
// CHECK-LABEL: int32_t sum_to(int32_t
// CHECK: for (int32_t
// CHECK-LABEL: int32_t collatz_steps(double
// CHECK: do {
// CHECK: std::fmod(
// CHECK: } while (
// CHECK-LABEL: bool is_between(double
// CHECK: &&
// CHECK-LABEL: int32_t main() {
// CHECK: fib20 = {{v[0-9]+}};
// CHECK: std::printf("%s=%.17g\n", "clamped", {{v[0-9]+}});
// CHECK: std::printf("%s=%.17g\n", "third", {{v[0-9]+}});
// CHECK: return {{v[0-9]+}};

module {
  emitc.include <"cstdint">
  emitc.include <"cstdio">
  emitc.include <"cmath">

  // --- the globals: one per numeric top-level `var`, in source order ---------
  emitc.global @fib20 : i32
  emitc.global @sum100 : i32
  emitc.global @collatz27 : i32
  emitc.global @third : f64
  emitc.global @negzero : f64
  emitc.global @inf_pos : f64
  emitc.global @inf_neg : f64
  emitc.global @not_a_number : f64
  emitc.global @mod_neg : f64
  emitc.global @clamped : f64

  // function fib(n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); }
  //
  // RECURSION, and an early return: both arms assign the result lvalue and the
  // one `emitc.return` reads it after the `if`.
  emitc.func @fib(%n: i32) -> i32 {
    %r = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<i32>
    %two = "emitc.constant"() <{value = 2 : i32}> : () -> i32
    %base = emitc.cmp lt, %n, %two : (i32, i32) -> i1
    emitc.if %base {
      emitc.assign %n : i32 to %r : !emitc.lvalue<i32>
    } else {
      %one = "emitc.constant"() <{value = 1 : i32}> : () -> i32
      %n1 = emitc.sub %n, %one : (i32, i32) -> i32
      %f1 = emitc.call @fib(%n1) : (i32) -> i32
      %n2 = emitc.sub %n, %two : (i32, i32) -> i32
      %f2 = emitc.call @fib(%n2) : (i32) -> i32
      %sum = emitc.add %f1, %f2 : (i32, i32) -> i32
      emitc.assign %sum : i32 to %r : !emitc.lvalue<i32>
    }
    %out = emitc.load %r : !emitc.lvalue<i32>
    emitc.return %out : i32
  }

  // function sum_to(n) { var s = 0; for (var i = 1; i <= n; i = i + 1) { s = s + i; } return s; }
  //
  // A COUNTED LOOP is `emitc.for`; its test is `<`, so `i <= n` is `i < n + 1`.
  emitc.func @sum_to(%n: i32) -> i32 {
    %s = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<i32>
    %zero = "emitc.constant"() <{value = 0 : i32}> : () -> i32
    emitc.assign %zero : i32 to %s : !emitc.lvalue<i32>
    %one = "emitc.constant"() <{value = 1 : i32}> : () -> i32
    %stop = emitc.add %n, %one : (i32, i32) -> i32
    emitc.for %i = %one to %stop step %one : i32 {
      %cur = emitc.load %s : !emitc.lvalue<i32>
      %next = emitc.add %cur, %i : (i32, i32) -> i32
      emitc.assign %next : i32 to %s : !emitc.lvalue<i32>
    }
    %out = emitc.load %s : !emitc.lvalue<i32>
    emitc.return %out : i32
  }

  // function collatz_steps(n) {
  //     var steps = 0;
  //     while (n !== 1) {
  //         if (n % 2 === 0) { n = n / 2; } else { n = 3 * n + 1; }
  //         steps = steps + 1;
  //     }
  //     return steps;
  // }
  //
  // A PRE-TESTED LOOP is `if (c) do { body } while (c)`, and the parameter is
  // reassigned so it is copied into an lvalue on entry. `n / 2` makes `n` a
  // double even though every value it takes is integral - JavaScript division
  // is not integer division, and the inference must not pretend it is.
  emitc.func @collatz_steps(%n0: f64) -> i32 {
    %n = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<f64>
    emitc.assign %n0 : f64 to %n : !emitc.lvalue<f64>
    %steps = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<i32>
    %zero = "emitc.constant"() <{value = 0 : i32}> : () -> i32
    emitc.assign %zero : i32 to %steps : !emitc.lvalue<i32>
    %one = "emitc.constant"() <{value = 1 : i32}> : () -> i32
    %fzero = "emitc.constant"() <{value = 0.0 : f64}> : () -> f64
    %fone = "emitc.constant"() <{value = 1.0 : f64}> : () -> f64
    %ftwo = "emitc.constant"() <{value = 2.0 : f64}> : () -> f64
    %fthree = "emitc.constant"() <{value = 3.0 : f64}> : () -> f64
    %first = emitc.load %n : !emitc.lvalue<f64>
    %enter = emitc.cmp ne, %first, %fone : (f64, f64) -> i1
    emitc.if %enter {
      emitc.do {
        %cur = emitc.load %n : !emitc.lvalue<f64>
        %rem = emitc.call_opaque "std::fmod"(%cur, %ftwo) : (f64, f64) -> f64
        %even = emitc.cmp eq, %rem, %fzero : (f64, f64) -> i1
        emitc.if %even {
          %half = emitc.div %cur, %ftwo : (f64, f64) -> f64
          emitc.assign %half : f64 to %n : !emitc.lvalue<f64>
        } else {
          %triple = emitc.mul %fthree, %cur : (f64, f64) -> f64
          %next = emitc.add %triple, %fone : (f64, f64) -> f64
          emitc.assign %next : f64 to %n : !emitc.lvalue<f64>
        }
        %count = emitc.load %steps : !emitc.lvalue<i32>
        %count1 = emitc.add %count, %one : (i32, i32) -> i32
        emitc.assign %count1 : i32 to %steps : !emitc.lvalue<i32>
      } while {
        %again = emitc.expression %n, %fone : (!emitc.lvalue<f64>, f64) -> i1 {
          %now = emitc.load %n : !emitc.lvalue<f64>
          %c = emitc.cmp ne, %now, %fone : (f64, f64) -> i1
          emitc.yield %c : i1
        }
        emitc.yield %again : i1
      }
    }
    %out = emitc.load %steps : !emitc.lvalue<i32>
    emitc.return %out : i32
  }

  // function ratio(a, b) { return a / b; }
  emitc.func @ratio(%a: f64, %b: f64) -> f64 {
    %q = emitc.div %a, %b : (f64, f64) -> f64
    emitc.return %q : f64
  }

  // function negate(x) { return -x; }
  emitc.func @negate(%x: f64) -> f64 {
    %m = emitc.unary_minus %x : (f64) -> f64
    emitc.return %m : f64
  }

  // function modulo(a, b) { return a % b; }
  emitc.func @modulo(%a: f64, %b: f64) -> f64 {
    %m = emitc.call_opaque "std::fmod"(%a, %b) : (f64, f64) -> f64
    emitc.return %m : f64
  }

  // function is_between(x, lo, hi) { return x >= lo && x < hi; }
  //
  // A BOOLEAN-VALUED FUNCTION: `i1` in, `bool` out in the C++.
  emitc.func @is_between(%x: f64, %lo: f64, %hi: f64) -> i1 {
    %ge = emitc.cmp ge, %x, %lo : (f64, f64) -> i1
    %lt = emitc.cmp lt, %x, %hi : (f64, f64) -> i1
    %both = emitc.logical_and %ge, %lt : i1, i1
    emitc.return %both : i1
  }

  // function clamp01(x) {
  //     if (is_between(x, 0, 1)) { return x; }
  //     if (x < 0) { return 0; }
  //     return 1;
  // }
  //
  // THREE RETURNS become three assignments to one lvalue, nested so that every
  // path assigns it exactly once - the C++ compiler's -Wsometimes-uninitialized
  // is part of the gate and a path that skips the assignment would trip it.
  emitc.func @clamp01(%x: f64) -> f64 {
    %r = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<f64>
    %zero = "emitc.constant"() <{value = 0.0 : f64}> : () -> f64
    %one = "emitc.constant"() <{value = 1.0 : f64}> : () -> f64
    %inside = emitc.call @is_between(%x, %zero, %one) : (f64, f64, f64) -> i1
    emitc.if %inside {
      emitc.assign %x : f64 to %r : !emitc.lvalue<f64>
    } else {
      %below = emitc.cmp lt, %x, %zero : (f64, f64) -> i1
      emitc.if %below {
        emitc.assign %zero : f64 to %r : !emitc.lvalue<f64>
      } else {
        emitc.assign %one : f64 to %r : !emitc.lvalue<f64>
      }
    }
    %out = emitc.load %r : !emitc.lvalue<f64>
    emitc.return %out : f64
  }

  // THE TOP LEVEL. Every `var x = ...` is a call and a store to the global; the
  // constants are typed by what the callee's parameter was proved to be.
  emitc.func @main() -> i32 {
    // var fib20 = fib(20);
    %c20 = "emitc.constant"() <{value = 20 : i32}> : () -> i32
    %v_fib20 = emitc.call @fib(%c20) : (i32) -> i32
    %g_fib20 = emitc.get_global @fib20 : !emitc.lvalue<i32>
    emitc.assign %v_fib20 : i32 to %g_fib20 : !emitc.lvalue<i32>

    // var sum100 = sum_to(100);
    %c100 = "emitc.constant"() <{value = 100 : i32}> : () -> i32
    %v_sum100 = emitc.call @sum_to(%c100) : (i32) -> i32
    %g_sum100 = emitc.get_global @sum100 : !emitc.lvalue<i32>
    emitc.assign %v_sum100 : i32 to %g_sum100 : !emitc.lvalue<i32>

    // var collatz27 = collatz_steps(27);
    %c27 = "emitc.constant"() <{value = 27.0 : f64}> : () -> f64
    %v_collatz27 = emitc.call @collatz_steps(%c27) : (f64) -> i32
    %g_collatz27 = emitc.get_global @collatz27 : !emitc.lvalue<i32>
    emitc.assign %v_collatz27 : i32 to %g_collatz27 : !emitc.lvalue<i32>

    // var third = ratio(1, 3);
    %f0 = "emitc.constant"() <{value = 0.0 : f64}> : () -> f64
    %f1 = "emitc.constant"() <{value = 1.0 : f64}> : () -> f64
    %f2 = "emitc.constant"() <{value = 2.0 : f64}> : () -> f64
    %f3 = "emitc.constant"() <{value = 3.0 : f64}> : () -> f64
    %v_third = emitc.call @ratio(%f1, %f3) : (f64, f64) -> f64
    %g_third = emitc.get_global @third : !emitc.lvalue<f64>
    emitc.assign %v_third : f64 to %g_third : !emitc.lvalue<f64>

    // var negzero = negate(0);
    %v_negzero = emitc.call @negate(%f0) : (f64) -> f64
    %g_negzero = emitc.get_global @negzero : !emitc.lvalue<f64>
    emitc.assign %v_negzero : f64 to %g_negzero : !emitc.lvalue<f64>

    // var inf_pos = ratio(1, 0);
    %v_inf_pos = emitc.call @ratio(%f1, %f0) : (f64, f64) -> f64
    %g_inf_pos = emitc.get_global @inf_pos : !emitc.lvalue<f64>
    emitc.assign %v_inf_pos : f64 to %g_inf_pos : !emitc.lvalue<f64>

    // var inf_neg = ratio(-1, 0);
    %fm1 = "emitc.constant"() <{value = -1.0 : f64}> : () -> f64
    %v_inf_neg = emitc.call @ratio(%fm1, %f0) : (f64, f64) -> f64
    %g_inf_neg = emitc.get_global @inf_neg : !emitc.lvalue<f64>
    emitc.assign %v_inf_neg : f64 to %g_inf_neg : !emitc.lvalue<f64>

    // var not_a_number = ratio(0, 0);
    %v_nan = emitc.call @ratio(%f0, %f0) : (f64, f64) -> f64
    %g_nan = emitc.get_global @not_a_number : !emitc.lvalue<f64>
    emitc.assign %v_nan : f64 to %g_nan : !emitc.lvalue<f64>

    // var mod_neg = modulo(-7.5, 2);
    %fm75 = "emitc.constant"() <{value = -7.5 : f64}> : () -> f64
    %v_mod_neg = emitc.call @modulo(%fm75, %f2) : (f64, f64) -> f64
    %g_mod_neg = emitc.get_global @mod_neg : !emitc.lvalue<f64>
    emitc.assign %v_mod_neg : f64 to %g_mod_neg : !emitc.lvalue<f64>

    // var clamped = clamp01(negate(third)) + clamp01(2) + clamp01(0.25);
    //
    // A GLOBAL READ BACK: `third` is the global, not the temporary above.
    %third_now = emitc.load %g_third : !emitc.lvalue<f64>
    %neg_third = emitc.call @negate(%third_now) : (f64) -> f64
    %c_a = emitc.call @clamp01(%neg_third) : (f64) -> f64
    %c_b = emitc.call @clamp01(%f2) : (f64) -> f64
    %f025 = "emitc.constant"() <{value = 0.25 : f64}> : () -> f64
    %c_c = emitc.call @clamp01(%f025) : (f64) -> f64
    %s_ab = emitc.add %c_a, %c_b : (f64, f64) -> f64
    %s_abc = emitc.add %s_ab, %c_c : (f64, f64) -> f64
    %g_clamped = emitc.get_global @clamped : !emitc.lvalue<f64>
    emitc.assign %s_abc : f64 to %g_clamped : !emitc.lvalue<f64>

    // --- THE OUTPUT CONVENTION: every numeric global, sorted by name --------
    %fmt = emitc.literal "\"%s=%.17g\\n\"" : !emitc.ptr<!emitc.opaque<"const char">>

    %o_clamped = emitc.load %g_clamped : !emitc.lvalue<f64>
    %n_clamped = emitc.literal "\"clamped\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "std::printf"(%fmt, %n_clamped, %o_clamped) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.ptr<!emitc.opaque<"const char">>, f64) -> ()

    %o_collatz27 = emitc.load %g_collatz27 : !emitc.lvalue<i32>
    %d_collatz27 = emitc.cast %o_collatz27 : i32 to f64
    %n_collatz27 = emitc.literal "\"collatz27\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "std::printf"(%fmt, %n_collatz27, %d_collatz27) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.ptr<!emitc.opaque<"const char">>, f64) -> ()

    %o_fib20 = emitc.load %g_fib20 : !emitc.lvalue<i32>
    %d_fib20 = emitc.cast %o_fib20 : i32 to f64
    %n_fib20 = emitc.literal "\"fib20\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "std::printf"(%fmt, %n_fib20, %d_fib20) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.ptr<!emitc.opaque<"const char">>, f64) -> ()

    %o_inf_neg = emitc.load %g_inf_neg : !emitc.lvalue<f64>
    %n_inf_neg = emitc.literal "\"inf_neg\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "std::printf"(%fmt, %n_inf_neg, %o_inf_neg) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.ptr<!emitc.opaque<"const char">>, f64) -> ()

    %o_inf_pos = emitc.load %g_inf_pos : !emitc.lvalue<f64>
    %n_inf_pos = emitc.literal "\"inf_pos\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "std::printf"(%fmt, %n_inf_pos, %o_inf_pos) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.ptr<!emitc.opaque<"const char">>, f64) -> ()

    %o_mod_neg = emitc.load %g_mod_neg : !emitc.lvalue<f64>
    %n_mod_neg = emitc.literal "\"mod_neg\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "std::printf"(%fmt, %n_mod_neg, %o_mod_neg) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.ptr<!emitc.opaque<"const char">>, f64) -> ()

    %o_negzero = emitc.load %g_negzero : !emitc.lvalue<f64>
    %n_negzero = emitc.literal "\"negzero\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "std::printf"(%fmt, %n_negzero, %o_negzero) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.ptr<!emitc.opaque<"const char">>, f64) -> ()

    %o_nan = emitc.load %g_nan : !emitc.lvalue<f64>
    %n_nan = emitc.literal "\"not_a_number\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "std::printf"(%fmt, %n_nan, %o_nan) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.ptr<!emitc.opaque<"const char">>, f64) -> ()

    %o_sum100 = emitc.load %g_sum100 : !emitc.lvalue<i32>
    %d_sum100 = emitc.cast %o_sum100 : i32 to f64
    %n_sum100 = emitc.literal "\"sum100\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "std::printf"(%fmt, %n_sum100, %d_sum100) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.ptr<!emitc.opaque<"const char">>, f64) -> ()

    %o_third = emitc.load %g_third : !emitc.lvalue<f64>
    %n_third = emitc.literal "\"third\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "std::printf"(%fmt, %n_third, %o_third) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.ptr<!emitc.opaque<"const char">>, f64) -> ()

    %rc = "emitc.constant"() <{value = 0 : i32}> : () -> i32
    emitc.return %rc : i32
  }
}
