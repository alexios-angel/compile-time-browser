// Binding time and C++ constant-expression legality are separate obligations.
// Typed static seeds propagate only through safe scalar operations, and a
// writable binding remains dynamic even when its initializer is a constant.
// RUN: split-file %s %t
// RUN: ctjs-translate --mlir-to-cpp %t/bindings.mlir > %t/bindings.cpp
// RUN: FileCheck %s --check-prefix=BINDINGS < %t/bindings.cpp
// RUN: ctjs-translate --mlir-to-cpp --declare-variables-at-top %t/bindings.mlir > %t/hoisted.cpp
// RUN: FileCheck %s --check-prefix=HOISTED --implicit-check-not='{{constexpr[[:space:]]}}' < %t/hoisted.cpp
// RUN: ctjs-translate --mlir-to-cpp %t/unsafe-integers.mlir | FileCheck %s --check-prefix=UNSAFE
// RUN: ctjs-translate --mlir-to-cpp %t/isolation.mlir > %t/isolation.cpp
// RUN: FileCheck %s --check-prefix=ISOLATION < %t/isolation.cpp
// RUN: python3 %S/check-constexpr-bindings.py --fixtures %t --work %t.executables --translate ctjs-translate --opt ctjs-opt

// Deduced and explicit bindings share a transitive proof. constexpr itself
// implies const, which remains the exact type expected by the existing pin.
// BINDINGS: double static_chain() {
// BINDINGS-NEXT: constexpr double [[TWO:[A-Za-z_][A-Za-z_0-9]*]] = 2.0;
// BINDINGS-NEXT: constexpr double [[THREE:[A-Za-z_][A-Za-z_0-9]*]] = 3.0;
// BINDINGS-NEXT: constexpr auto sum = [[TWO]] + [[THREE]];
// BINDINGS-NEXT: CTCOMPILE_PIN(sum, "constexpr-bindings.js:1:1", double const);
// BINDINGS-NEXT: constexpr double product = sum * [[TWO]];
// BINDINGS-NEXT: return product;
// BINDINGS: int32_t integer_chain() {
// BINDINGS-NEXT: constexpr int32_t [[TWELVE:[A-Za-z_][A-Za-z_0-9]*]] = 12;
// BINDINGS-NEXT: constexpr int32_t [[FIVE:[A-Za-z_][A-Za-z_0-9]*]] = 5;
// BINDINGS-NEXT: constexpr int32_t [[INTEGER_SUM:[A-Za-z_][A-Za-z_0-9]*]] = [[TWELVE]] + [[FIVE]];
// BINDINGS-NEXT: constexpr int32_t [[INTEGER_PRODUCT:[A-Za-z_][A-Za-z_0-9]*]] = [[INTEGER_SUM]] * [[FIVE]];
// BINDINGS-NEXT: return [[INTEGER_PRODUCT]];
// Parameters remain runtime values even under forged source binding-time tags.
// BINDINGS: double runtime_parameter(double const [[INPUT:[A-Za-z_][A-Za-z_0-9]*]]) {
// BINDINGS-NEXT: constexpr double [[EIGHT:[A-Za-z_][A-Za-z_0-9]*]] = 8.0;
// BINDINGS-NEXT: double const [[DYNAMIC:[A-Za-z_][A-Za-z_0-9]*]] = [[INPUT]] + [[EIGHT]];
// BINDINGS-NEXT: return [[DYNAMIC]];
// A supplied static report and a const operand ABI do not authorize evaluation.
// BINDINGS: int32_t runtime_call() {
// BINDINGS-NEXT: int32_t const [[CALL:[A-Za-z_][A-Za-z_0-9]*]] = next_value();
// BINDINGS-NEXT: constexpr int32_t [[ONE:[A-Za-z_][A-Za-z_0-9]*]] = 1;
// BINDINGS-NEXT: int32_t const {{[A-Za-z_][A-Za-z_0-9]*}} = [[CALL]] + [[ONE]];
// BINDINGS: int32_t const_abi_call() {
// BINDINGS-NEXT: constexpr int32_t [[SEVEN:[A-Za-z_][A-Za-z_0-9]*]] = 7;
// BINDINGS-NEXT: int32_t const {{[A-Za-z_][A-Za-z_0-9]*}} = observe_count([[SEVEN]]);
// The writable seed cannot authorize a constexpr dependent expression.
// BINDINGS: int32_t mutated_seed() {
// BINDINGS-NEXT: int32_t [[MUTABLE:[A-Za-z_][A-Za-z_0-9]*]] = 7;
// BINDINGS-NEXT: increment([[MUTABLE]]);
// BINDINGS-NEXT: constexpr int32_t [[AFTER_ONE:[A-Za-z_][A-Za-z_0-9]*]] = 1;
// BINDINGS-NEXT: int32_t const {{[A-Za-z_][A-Za-z_0-9]*}} = [[MUTABLE]] + [[AFTER_ONE]];
// Loads, addresses, and owning strings retain their runtime semantics.
// BINDINGS: int32_t load_snapshot() {
// BINDINGS-NEXT: int32_t [[STORAGE:[A-Za-z_][A-Za-z_0-9]*]] = 5;
// BINDINGS-NEXT: int32_t* const [[ADDRESS:[A-Za-z_][A-Za-z_0-9]*]] = &[[STORAGE]];
// BINDINGS-NEXT: int32_t const [[LOADED:[A-Za-z_][A-Za-z_0-9]*]] = *[[ADDRESS]];
// BINDINGS-NEXT: return [[LOADED]];
// BINDINGS: int32_t heap_value() {
// BINDINGS-NEXT: std::string const [[STRING:[A-Za-z_][A-Za-z_0-9]*]] = "an owning string longer than a small-string buffer";
// BINDINGS-NEXT: int32_t const {{[A-Za-z_][A-Za-z_0-9]*}} = string_length([[STRING]]);
// Exact finite FP arithmetic is eligible; exceptional/inexact operations stay
// runtime expressions even when both operands have static binding time.
// BINDINGS: double exact_division() {
// BINDINGS: constexpr double [[EXACT_RESULT:[A-Za-z_][A-Za-z_0-9]*]] = {{[A-Za-z_][A-Za-z_0-9]*}} / {{[A-Za-z_][A-Za-z_0-9]*}};
// BINDINGS-NEXT: return [[EXACT_RESULT]];
// BINDINGS: double inexact_division() {
// BINDINGS: double const [[INEXACT_RESULT:[A-Za-z_][A-Za-z_0-9]*]] = {{[A-Za-z_][A-Za-z_0-9]*}} / {{[A-Za-z_][A-Za-z_0-9]*}};
// BINDINGS-NEXT: return [[INEXACT_RESULT]];
// BINDINGS: double division_by_zero() {
// BINDINGS: double const [[INFINITE_RESULT:[A-Za-z_][A-Za-z_0-9]*]] = {{[A-Za-z_][A-Za-z_0-9]*}} / {{[A-Za-z_][A-Za-z_0-9]*}};
// BINDINGS-NEXT: return [[INFINITE_RESULT]];
// BINDINGS: double floating_overflow() {
// BINDINGS: double const [[OVERFLOW_RESULT:[A-Za-z_][A-Za-z_0-9]*]] = {{[A-Za-z_][A-Za-z_0-9]*}} * {{[A-Za-z_][A-Za-z_0-9]*}};
// BINDINGS-NEXT: return [[OVERFLOW_RESULT]];
// BINDINGS: double floating_underflow() {
// BINDINGS: double const [[UNDERFLOW_RESULT:[A-Za-z_][A-Za-z_0-9]*]] = {{[A-Za-z_][A-Za-z_0-9]*}} / {{[A-Za-z_][A-Za-z_0-9]*}};
// BINDINGS-NEXT: return [[UNDERFLOW_RESULT]];
// BINDINGS: double negative_zero() {
// BINDINGS: constexpr double [[NEGATIVE_ZERO:[A-Za-z_][A-Za-z_0-9]*]] = -{{[A-Za-z_][A-Za-z_0-9]*}};
// BINDINGS-NEXT: return [[NEGATIVE_ZERO]];
// BINDINGS: double infinite_literal() {
// BINDINGS-NEXT: double const [[INFINITY:[A-Za-z_][A-Za-z_0-9]*]] = INFINITY;
// BINDINGS-NEXT: return [[INFINITY]];
// BINDINGS: double nan_literal() {
// BINDINGS-NEXT: double const [[NAN:[A-Za-z_][A-Za-z_0-9]*]] = NAN;
// BINDINGS-NEXT: return [[NAN]];
// BINDINGS: double scalar_selection() {
// BINDINGS: constexpr bool {{[A-Za-z_][A-Za-z_0-9]*}} = {{[A-Za-z_][A-Za-z_0-9]*}} < {{[A-Za-z_][A-Za-z_0-9]*}};
// BINDINGS: constexpr float {{[A-Za-z_][A-Za-z_0-9]*}} = {{[A-Za-z_][A-Za-z_0-9]*}} ? {{[A-Za-z_][A-Za-z_0-9]*}} : {{[A-Za-z_][A-Za-z_0-9]*}};
// BINDINGS: constexpr double selection_2 = (double) {{[A-Za-z_][A-Za-z_0-9]*}};
// BINDINGS: uint32_t unsigned_wrap() {
// BINDINGS: constexpr uint32_t {{[A-Za-z_][A-Za-z_0-9]*}} = {{[A-Za-z_][A-Za-z_0-9]*}} + {{[A-Za-z_][A-Za-z_0-9]*}};
// BINDINGS: int32_t fractional_conversion() {
// BINDINGS: int32_t const {{[A-Za-z_][A-Za-z_0-9]*}} = (int32_t) {{[A-Za-z_][A-Za-z_0-9]*}};
// BINDINGS: double cancellation() {
// BINDINGS: double const {{[A-Za-z_][A-Za-z_0-9]*}} = {{[A-Za-z_][A-Za-z_0-9]*}} - {{[A-Za-z_][A-Za-z_0-9]*}};

// HOISTED: double static_chain() {
// HOISTED-NEXT: double [[TWO:[A-Za-z_][A-Za-z_0-9]*]];
// HOISTED-NEXT: double [[THREE:[A-Za-z_][A-Za-z_0-9]*]];
// HOISTED-NEXT: double sum;
// HOISTED-NEXT: double product;
// HOISTED: sum = [[TWO]] + [[THREE]];
// HOISTED-NEXT: product = sum * [[TWO]];
// HOISTED-NEXT: return product;
// HOISTED: double runtime_parameter(double const {{[A-Za-z_][A-Za-z_0-9]*}}) {

// These deliberately undefined integer expressions are printer-only refusal
// witnesses. Executable fixtures above contain no signed overflow or bad shift.
// UNSAFE: int32_t signed_overflow() {
// UNSAFE: int32_t const {{[A-Za-z_][A-Za-z_0-9]*}} = {{[A-Za-z_][A-Za-z_0-9]*}} + {{[A-Za-z_][A-Za-z_0-9]*}};
// UNSAFE: int32_t integer_division_by_zero() {
// UNSAFE: int32_t const {{[A-Za-z_][A-Za-z_0-9]*}} = {{[A-Za-z_][A-Za-z_0-9]*}} / {{[A-Za-z_][A-Za-z_0-9]*}};
// UNSAFE: int32_t invalid_shift() {
// UNSAFE: int32_t const {{[A-Za-z_][A-Za-z_0-9]*}} = {{[A-Za-z_][A-Za-z_0-9]*}} << {{[A-Za-z_][A-Za-z_0-9]*}};
// UNSAFE: int32_t out_of_range_conversion() {
// UNSAFE: int32_t const {{[A-Za-z_][A-Za-z_0-9]*}} = (int32_t) {{[A-Za-z_][A-Za-z_0-9]*}};

// ISOLATION: int32_t ordinary_before() {
// ISOLATION-NEXT: int32_t v1 = 1;
// ISOLATION: int32_t marked() {
// ISOLATION-NEXT: constexpr int32_t v1 = 2;
// ISOLATION: int32_t ordinary_nested() {
// ISOLATION-NEXT: int32_t v1 = 3;
// ISOLATION: int32_t marked_after() {
// ISOLATION-NEXT: constexpr int32_t v1 = 4;
// ISOLATION: int32_t const_only() {
// ISOLATION-NEXT: int32_t const v1 = 5;
// ISOLATION: int32_t constexpr_only() {
// ISOLATION-NEXT: int32_t v1 = 6;
// ISOLATION: int32_t wrong_marker() {
// ISOLATION-NEXT: int32_t const v1 = 7;
// ISOLATION: int32_t ordinary_after() {
// ISOLATION-NEXT: int32_t v1 = 8;

//--- bindings.mlir
#sum = loc(fused<{ctnative.source_name = "sum"}>["constexpr-bindings.js":1:1])
#product = loc(fused<{ctnative.source_name = "product"}>["constexpr-bindings.js":2:1])
#selection = loc(fused<{ctnative.source_name = "selection"}>["constexpr-bindings.js":3:1])
module attributes {ctnative.constexpr_bindings, ctnative.const_bindings, ctnative.readable_names, ctnative.readable_literals} {
  emitc.include "constexpr-fixture.h"
  emitc.func @static_chain() -> f64 {
    %two = "emitc.constant"() {value = 2.0 : f64} : () -> f64
    %three = "emitc.constant"() {value = 3.0 : f64} : () -> f64
    %sum = "emitc.add"(%two, %three) {ctnative.deduced} : (f64, f64) -> f64 loc(#sum)
    %product = emitc.mul %sum, %two : (f64, f64) -> f64 loc(#product)
    emitc.return %product : f64
  }
  emitc.func @integer_chain() -> i32 {
    %twelve = "emitc.constant"() {value = 12 : i32} : () -> i32
    %five = "emitc.constant"() {value = 5 : i32} : () -> i32
    %sum = emitc.add %twelve, %five : (i32, i32) -> i32
    %product = emitc.mul %sum, %five : (i32, i32) -> i32
    emitc.return %product : i32
  }
  emitc.func @runtime_parameter(%input: f64) -> f64 attributes {ctnative.argument_binding_times = ["static"]} {
    %eight = "emitc.constant"() {value = 8.0 : f64} : () -> f64
    %sum = "emitc.add"(%input, %eight) {ctnative.binding_time = "static", ctnative.result_binding_times = ["static"]} : (f64, f64) -> f64
    emitc.return %sum : f64
  }
  emitc.func @runtime_call() -> i32 {
    %called = emitc.call_opaque "next_value"() {ctnative.binding_time = "static", ctnative.result_binding_times = ["static"], ctnative.const_operands = array<i32>} : () -> i32
    %one = "emitc.constant"() {value = 1 : i32} : () -> i32
    %sum = emitc.add %called, %one : (i32, i32) -> i32
    emitc.return %sum : i32
  }
  emitc.func @const_abi_call() -> i32 {
    %seven = "emitc.constant"() {value = 7 : i32} : () -> i32
    %result = emitc.call_opaque "observe_count"(%seven) {ctnative.const_operands = array<i32: 0>, ctnative.binding_time = "static"} : (i32) -> i32
    emitc.return %result : i32
  }
  emitc.func @mutated_seed() -> i32 {
    %seven = "emitc.constant"() {value = 7 : i32} : () -> i32
    emitc.call_opaque "increment"(%seven) : (i32) -> ()
    %one = "emitc.constant"() {value = 1 : i32} : () -> i32
    %sum = emitc.add %seven, %one : (i32, i32) -> i32
    emitc.return %sum : i32
  }
  emitc.func @load_snapshot() -> i32 {
    %storage = "emitc.variable"() {value = 5 : i32} : () -> !emitc.lvalue<i32>
    %address = emitc.address_of %storage : !emitc.lvalue<i32>
    %place = emitc.dereference %address : !emitc.ptr<i32>
    %loaded = emitc.load %place : !emitc.lvalue<i32>
    emitc.return %loaded : i32
  }
  emitc.func @heap_value() -> i32 {
    %text = "emitc.constant"() {value = #emitc.opaque<"\22an owning string longer than a small-string buffer\22">} : () -> !emitc.opaque<"std::string">
    %size = emitc.call_opaque "string_length"(%text) {ctnative.const_operands = array<i32: 0>} : (!emitc.opaque<"std::string">) -> i32
    emitc.return %size : i32
  }
  emitc.func @exact_division() -> f64 {
    %three = "emitc.constant"() {value = 3.0 : f64} : () -> f64
    %two = "emitc.constant"() {value = 2.0 : f64} : () -> f64
    %ratio = emitc.div %three, %two : (f64, f64) -> f64
    emitc.return %ratio : f64
  }
  emitc.func @inexact_division() -> f64 {
    %one = "emitc.constant"() {value = 1.0 : f64} : () -> f64
    %three = "emitc.constant"() {value = 3.0 : f64} : () -> f64
    %ratio = emitc.div %one, %three : (f64, f64) -> f64
    emitc.return %ratio : f64
  }
  emitc.func @division_by_zero() -> f64 {
    %one = "emitc.constant"() {value = 1.0 : f64} : () -> f64
    %zero = "emitc.constant"() {value = 0.0 : f64} : () -> f64
    %ratio = emitc.div %one, %zero : (f64, f64) -> f64
    emitc.return %ratio : f64
  }
  emitc.func @floating_overflow() -> f64 {
    %maximum = "emitc.constant"() {value = 0x7FEFFFFFFFFFFFFF : f64} : () -> f64
    %two = "emitc.constant"() {value = 2.0 : f64} : () -> f64
    %product = emitc.mul %maximum, %two : (f64, f64) -> f64
    emitc.return %product : f64
  }
  emitc.func @floating_underflow() -> f64 {
    %minimum = "emitc.constant"() {value = 0x0010000000000000 : f64} : () -> f64
    %three = "emitc.constant"() {value = 3.0 : f64} : () -> f64
    %ratio = emitc.div %minimum, %three : (f64, f64) -> f64
    emitc.return %ratio : f64
  }
  emitc.func @negative_zero() -> f64 {
    %zero = "emitc.constant"() {value = 0.0 : f64} : () -> f64
    %negative = emitc.unary_minus %zero : (f64) -> f64
    emitc.return %negative : f64
  }
  emitc.func @infinite_literal() -> f64 {
    %infinite = "emitc.constant"() {value = 0x7FF0000000000000 : f64} : () -> f64
    emitc.return %infinite : f64
  }
  emitc.func @nan_literal() -> f64 {
    %nan = "emitc.constant"() {value = 0x7FF8000000000000 : f64} : () -> f64
    emitc.return %nan : f64
  }
  emitc.func @scalar_selection() -> f64 {
    %integer = "emitc.constant"() {value = -3 : i32} : () -> i32
    %wide = emitc.cast %integer : i32 to f64
    %narrow = emitc.cast %wide : f64 to f32
    %zero = "emitc.constant"() {value = 0.0 : f32} : () -> f32
    %condition = emitc.cmp lt, %narrow, %zero : (f32, f32) -> i1
    %selected = emitc.conditional %condition, %narrow, %zero : f32
    %selection = emitc.cast %selected : f32 to f64 loc(#selection)
    emitc.return %selection : f64
  }
  emitc.func @unsigned_wrap() -> ui32 {
    %maximum = "emitc.constant"() {value = 4294967295 : ui32} : () -> ui32
    %one = "emitc.constant"() {value = 1 : ui32} : () -> ui32
    %wrapped = emitc.add %maximum, %one : (ui32, ui32) -> ui32
    emitc.return %wrapped : ui32
  }
  emitc.func @fractional_conversion() -> i32 {
    %fraction = "emitc.constant"() {value = 1.5 : f64} : () -> f64
    %integer = emitc.cast %fraction : f64 to i32
    emitc.return %integer : i32
  }
  emitc.func @cancellation() -> f64 {
    %one = "emitc.constant"() {value = 1.0 : f64} : () -> f64
    %zero = emitc.sub %one, %one : (f64, f64) -> f64
    emitc.return %zero : f64
  }
}

//--- unsafe-integers.mlir
module attributes {ctnative.constexpr_bindings, ctnative.const_bindings} {
  emitc.func @signed_overflow() -> i32 {
    %maximum = "emitc.constant"() {value = 2147483647 : i32} : () -> i32
    %one = "emitc.constant"() {value = 1 : i32} : () -> i32
    %overflow = emitc.add %maximum, %one : (i32, i32) -> i32
    emitc.return %overflow : i32
  }
  emitc.func @integer_division_by_zero() -> i32 {
    %one = "emitc.constant"() {value = 1 : i32} : () -> i32
    %zero = "emitc.constant"() {value = 0 : i32} : () -> i32
    %ratio = emitc.div %one, %zero : (i32, i32) -> i32
    emitc.return %ratio : i32
  }
  emitc.func @invalid_shift() -> i32 {
    %one = "emitc.constant"() {value = 1 : i32} : () -> i32
    %width = "emitc.constant"() {value = 32 : i32} : () -> i32
    %shifted = emitc.bitwise_left_shift %one, %width : (i32, i32) -> i32
    emitc.return %shifted : i32
  }
  emitc.func @out_of_range_conversion() -> i32 {
    %huge = "emitc.constant"() {value = 4294967296.0 : f64} : () -> f64
    %integer = emitc.cast %huge : f64 to i32
    emitc.return %integer : i32
  }
}

//--- isolation.mlir
module {
  emitc.include <"cstdint">
  emitc.func @ordinary_before() -> i32 {
    %value = "emitc.constant"() {value = 1 : i32} : () -> i32
    emitc.return %value : i32
  }
  module @native attributes {ctnative.constexpr_bindings, ctnative.const_bindings} {
    emitc.func @marked() -> i32 {
      %value = "emitc.constant"() {value = 2 : i32} : () -> i32
      emitc.return %value : i32
    }
    module @ordinary {
      emitc.func @ordinary_nested() -> i32 {
        %value = "emitc.constant"() {value = 3 : i32} : () -> i32
        emitc.return %value : i32
      }
    }
    emitc.func @marked_after() -> i32 {
      %value = "emitc.constant"() {value = 4 : i32} : () -> i32
      emitc.return %value : i32
    }
  }
  module @constant attributes {ctnative.const_bindings} {
    emitc.func @const_only() -> i32 {
      %value = "emitc.constant"() {value = 5 : i32} : () -> i32
      emitc.return %value : i32
    }
  }
  module @static attributes {ctnative.constexpr_bindings} {
    emitc.func @constexpr_only() -> i32 {
      %value = "emitc.constant"() {value = 6 : i32} : () -> i32
      emitc.return %value : i32
    }
  }
  module @wrong attributes {ctnative.constexpr_bindings = false, ctnative.const_bindings} {
    emitc.func @wrong_marker() -> i32 {
      %value = "emitc.constant"() {value = 7 : i32} : () -> i32
      emitc.return %value : i32
    }
  }
  emitc.func @ordinary_after() -> i32 {
    %value = "emitc.constant"() {value = 8 : i32} : () -> i32
    emitc.return %value : i32
  }
}

//--- constexpr-fixture.h
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <type_traits>
#ifndef CTCOMPILE_NO_TYPE_PINS
#define CTCOMPILE_PIN(name, site, ...) static_assert(std::is_same_v<decltype(name), __VA_ARGS__>, "ctcompile: " #name " @ " site)
#else
#define CTCOMPILE_PIN(name, site, ...)
#endif
inline int32_t runtime_counter = 0;
inline int32_t next_value() { return ++runtime_counter; }
inline int32_t observe_count(int32_t const & value) { return value + ++runtime_counter; }
inline void increment(int32_t & value) { ++value; }
inline int32_t string_length(std::string const & value) { return static_cast<int32_t>(value.size()); }

//--- constexpr-source.js
function runtimeMix(input) {
    const staticSeed = 8;
    const dynamicSum = input + staticSeed;
    return dynamicSum;
}
var first = runtimeMix(3);
var second = runtimeMix(9);
