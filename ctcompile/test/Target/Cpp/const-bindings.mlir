// Const qualifies a proved immutable C++ binding, not every SSA identity.
// The executable cases distinguish overload selection and pointee mutation;
// compiling alone would miss a switch to a const-reference overload.
// RUN: split-file %s %t
// RUN: ctjs-translate --mlir-to-cpp %t/bindings.mlir > %t/bindings.cpp
// RUN: FileCheck %s --check-prefix=BINDINGS < %t/bindings.cpp
// RUN: ctjs-translate --mlir-to-cpp --declare-variables-at-top %t/bindings.mlir > %t/hoisted.cpp
// RUN: FileCheck %s --check-prefix=HOISTED < %t/hoisted.cpp
// RUN: ctjs-translate --mlir-to-cpp %t/isolation.mlir > %t/isolation.cpp
// RUN: FileCheck %s --check-prefix=ISOLATION < %t/isolation.cpp
// RUN: python3 %S/check-const-bindings.py --fixtures %t --work %t.executables

// Explicit types, deduction, source names and exact pins share qualification.
// BINDINGS: double scalar(double const catalog);
// BINDINGS: double scalar(double const catalog) {
// BINDINGS-NEXT: auto const answer = catalog + 2.0;
// BINDINGS-NEXT: CTCOMPILE_PIN(answer, "const-bindings.js:2:1", double const);
// BINDINGS-NEXT: return answer;
// BINDINGS: int32_t safe_call(int32_t const [[READ_ARG:[A-Za-z_][A-Za-z_0-9]*]]) {
// BINDINGS-NEXT: int32_t const [[READ_RESULT:[A-Za-z_][A-Za-z_0-9]*]] = read_only([[READ_ARG]]);
// BINDINGS-NEXT: return [[READ_RESULT]];
// Unknown calls may mutate operands or select a different reference overload.
// Their scalar results can still be immutable snapshots.
// BINDINGS: int32_t unknown_overload(int32_t [[OVERLOAD_ARG:[A-Za-z_][A-Za-z_0-9]*]]) {
// BINDINGS-NEXT: int32_t const [[OVERLOAD_RESULT:[A-Za-z_][A-Za-z_0-9]*]] = choose([[OVERLOAD_ARG]]);
// Mutation/overload constraints on an expression block argument must reach its
// captured outer binding without making an unrelated read-only capture mutable.
// BINDINGS: int32_t captured_overload(int32_t [[CAPTURE:[A-Za-z_][A-Za-z_0-9]*]], int32_t const [[BIAS:[A-Za-z_][A-Za-z_0-9]*]]) {
// BINDINGS-NEXT: return choose([[CAPTURE]]) + [[BIAS]];
// BINDINGS: int32_t unknown_reference() {
// BINDINGS-NEXT: int32_t [[MUTATED:[A-Za-z_][A-Za-z_0-9]*]] = 7;
// BINDINGS-NEXT: increment([[MUTATED]]);
// BINDINGS-NEXT: return [[MUTATED]];
// BINDINGS: int32_t unknown_verbatim(int32_t [[VERBATIM_ARG:[A-Za-z_][A-Za-z_0-9]*]]) {
// BINDINGS-NEXT: [[VERBATIM_ARG]] += 5;
// BINDINGS-NEXT: return [[VERBATIM_ARG]];
// A contract names safe operand indices; it does not mark the whole call pure.
// BINDINGS: int32_t mixed_contract(int32_t [[DESTINATION:[A-Za-z_][A-Za-z_0-9]*]]) {
// BINDINGS-NEXT: int32_t const [[DELTA:[A-Za-z_][A-Za-z_0-9]*]] = 4;
// BINDINGS-NEXT: add_to([[DESTINATION]], [[DELTA]]);
// BINDINGS-NEXT: return [[DESTINATION]];
// Pointer qualification is shallow, including deduced declarations and pins.
// BINDINGS: int32_t pointer_mutation(int32_t* const [[POINTER:[A-Za-z_][A-Za-z_0-9]*]]) {
// BINDINGS-NEXT: auto const [[POINTER_COPY:[A-Za-z_][A-Za-z_0-9]*]] = (int32_t*) [[POINTER]];
// BINDINGS-NEXT: CTCOMPILE_PIN([[POINTER_COPY]], "const-bindings.js:3:1", int32_t* const);
// BINDINGS: int32_t const [[BEFORE:[A-Za-z_][A-Za-z_0-9]*]] = *[[POINTER_COPY]];
// BINDINGS: *[[POINTER_COPY]] = {{[A-Za-z_][A-Za-z_0-9]*}};
// BINDINGS: int32_t local_storage() {
// BINDINGS-NEXT: int32_t [[STORAGE:[A-Za-z_][A-Za-z_0-9]*]] = 4;
// BINDINGS-NEXT: int32_t* const [[ADDRESS:[A-Za-z_][A-Za-z_0-9]*]] = &[[STORAGE]];
// BINDINGS-NEXT: int32_t const {{[A-Za-z_][A-Za-z_0-9]*}} = pointer_mutation([[ADDRESS]]);
// BINDINGS: int32_t pointer_choice(bool const {{[A-Za-z_][A-Za-z_0-9]*}}, int32_t* const {{[A-Za-z_][A-Za-z_0-9]*}}, int32_t* const {{[A-Za-z_][A-Za-z_0-9]*}}) {
// BINDINGS-NEXT: int32_t* const [[SELECTED:[A-Za-z_][A-Za-z_0-9]*]] = {{.*}} ? {{.*}} : {{.*}};
// BINDINGS: *[[SELECTED]] = 42;
// Empty initializers and mutable storage never acquire a declaration qualifier.
// BINDINGS: std::string empty_initializer() {
// BINDINGS-NEXT: std::string [[EMPTY:[A-Za-z_][A-Za-z_0-9]*]];
// BINDINGS-NEXT: std::string const [[EMPTY_SNAPSHOT:[A-Za-z_][A-Za-z_0-9]*]] = [[EMPTY]];
// BINDINGS-NEXT: return [[EMPTY_SNAPSHOT]];
// BINDINGS: int32_t loop_total(size_t const [[LIMIT:[A-Za-z_][A-Za-z_0-9]*]]) {
// BINDINGS-NEXT: int32_t [[TOTAL:[A-Za-z_][A-Za-z_0-9]*]] = 0;
// BINDINGS-NEXT: for (size_t [[ITERATOR:[A-Za-z_][A-Za-z_0-9]*]] = 0; [[ITERATOR]] < [[LIMIT]]; [[ITERATOR]] += 1) {
// BINDINGS: int32_t const {{[A-Za-z_][A-Za-z_0-9]*}} = [[TOTAL]];
// BINDINGS: int32_t tuple_values() {
// BINDINGS-NEXT: int32_t [[LEFT:[A-Za-z_][A-Za-z_0-9]*]];
// BINDINGS-NEXT: int32_t [[RIGHT:[A-Za-z_][A-Za-z_0-9]*]];
// BINDINGS-NEXT: std::tie([[LEFT]], [[RIGHT]]) = pair_values();
// BINDINGS-NEXT: int32_t const {{[A-Za-z_][A-Za-z_0-9]*}} = [[LEFT]] + [[RIGHT]];
// Known value carriers are supported, but arbitrary external types are not
// inferred immutable from an opaque spelling or SSA identity.
// BINDINGS: std::string string_value(std::string const [[TEXT:[A-Za-z_][A-Za-z_0-9]*]]) {
// BINDINGS-NEXT: return [[TEXT]];
// BINDINGS: int32_t external_value(ExternalCounter [[EXTERNAL:[A-Za-z_][A-Za-z_0-9]*]]) {
// BINDINGS-NEXT: int32_t const {{[A-Za-z_][A-Za-z_0-9]*}} = (int32_t) [[EXTERNAL]];
// Inlined expressions do not gain phantom declarations or type pins.
// BINDINGS: double inline_math(double const [[INLINE_ARG:[A-Za-z_][A-Za-z_0-9]*]]) {
// BINDINGS-NEXT: return ([[INLINE_ARG]] + 1.0) * 2.0;
// BINDINGS-NEXT: }

// Hoisted locals are declared without initialization and assigned later.
// Parameters still initialize on entry and can remain const.
// HOISTED: double scalar(double const catalog) {
// HOISTED-NEXT: double answer;
// HOISTED-NEXT: answer = catalog + 2.0;
// HOISTED-NEXT: return answer;
// HOISTED: int32_t pointer_mutation(int32_t* const [[POINTER:[A-Za-z_][A-Za-z_0-9]*]]) {
// HOISTED-NEXT: int32_t* [[POINTER_COPY:[A-Za-z_][A-Za-z_0-9]*]];
// HOISTED: [[POINTER_COPY]] = (int32_t*) [[POINTER]];
// HOISTED: int32_t loop_total(size_t const [[LIMIT:[A-Za-z_][A-Za-z_0-9]*]]) {
// HOISTED: for (size_t [[ITERATOR:[A-Za-z_][A-Za-z_0-9]*]] = 0; [[ITERATOR]] < [[LIMIT]]; [[ITERATOR]] += 1) {

// The nearest module must carry the UnitAttr. Names/deduced policy markers,
// a parent's marker, and a non-unit attribute do not enable const bindings.
// ISOLATION: int32_t ordinary_before(int32_t v1) {
// ISOLATION-NEXT: return v1;
// ISOLATION: int32_t marked(int32_t const v1) {
// ISOLATION-NEXT: return v1;
// ISOLATION: int32_t ordinary_nested(int32_t v1) {
// ISOLATION-NEXT: return v1;
// ISOLATION: int32_t marked_after(int32_t const v1) {
// ISOLATION-NEXT: return v1;
// ISOLATION: int32_t wrong_marker(int32_t v1) {
// ISOLATION-NEXT: return v1;
// ISOLATION: int32_t names_only(int32_t catalog) {
// ISOLATION-NEXT: return catalog;
// ISOLATION: int32_t ordinary_after(int32_t v1) {
// ISOLATION-NEXT: return v1;

//--- bindings.mlir
#catalog = loc(fused<{ctnative.source_name = "catalog"}>["const-bindings.js":1:1])
#answer = loc(fused<{ctnative.source_name = "answer"}>["const-bindings.js":2:1])
module attributes {ctnative.const_bindings, ctnative.readable_names} {
  emitc.include "const-fixture.h"
  emitc.declare_func @scalar
  emitc.func @scalar(%input: f64 loc(#catalog)) -> f64 {
    %two = emitc.literal "2.0" : f64
    %answer = "emitc.add"(%input, %two) {ctnative.deduced} : (f64, f64) -> f64 loc(#answer)
    emitc.return %answer : f64
  }
  emitc.func @safe_call(%input: i32) -> i32 {
    %result = emitc.call_opaque "read_only"(%input) {ctnative.const_operands = array<i32: 0>} : (i32) -> i32
    emitc.return %result : i32
  }
  emitc.func @unknown_overload(%input: i32) -> i32 {
    %result = emitc.call_opaque "choose"(%input) : (i32) -> i32
    emitc.return %result : i32
  }
  emitc.func @captured_overload(%input: i32, %bias: i32) -> i32 {
    %expression = "emitc.expression"(%input, %bias) ({
    ^entry(%captured: i32, %capturedBias: i32):
      %selected = emitc.call_opaque "choose"(%captured) : (i32) -> i32
      %result = emitc.add %selected, %capturedBias : (i32, i32) -> i32
      emitc.yield %result : i32
    }) : (i32, i32) -> i32
    emitc.return %expression : i32
  }
  emitc.func @unknown_reference() -> i32 {
    %value = "emitc.constant"() {value = 7 : i32} : () -> i32
    emitc.call_opaque "increment"(%value) : (i32) -> ()
    emitc.return %value : i32
  }
  emitc.func @unknown_verbatim(%input: i32) -> i32 {
    emitc.verbatim "{} += 5;" args %input : i32
    emitc.return %input : i32
  }
  emitc.func @mixed_contract(%input: i32) -> i32 {
    %delta = "emitc.constant"() {value = 4 : i32} : () -> i32
    emitc.call_opaque "add_to"(%input, %delta) {ctnative.const_operands = array<i32: 1>} : (i32, i32) -> ()
    emitc.return %input : i32
  }
  emitc.func @pointer_mutation(%pointer: !emitc.ptr<i32>) -> i32 {
    %copy = "emitc.cast"(%pointer) {ctnative.deduced} : (!emitc.ptr<i32>) -> !emitc.ptr<i32> loc("const-bindings.js":3:1)
    %place = emitc.dereference %copy : !emitc.ptr<i32>
    %one = emitc.literal "1" : i32
    %before = emitc.load %place : !emitc.lvalue<i32>
    %after = emitc.add %before, %one : (i32, i32) -> i32
    emitc.assign %after : i32 to %place : !emitc.lvalue<i32>
    emitc.return %after : i32
  }
  emitc.func @local_storage() -> i32 {
    %storage = "emitc.variable"() {value = 4 : i32} : () -> !emitc.lvalue<i32>
    %address = emitc.address_of %storage : !emitc.lvalue<i32>
    %result = emitc.call @pointer_mutation(%address) : (!emitc.ptr<i32>) -> i32
    %loaded = emitc.load %storage : !emitc.lvalue<i32>
    %sum = emitc.add %result, %loaded : (i32, i32) -> i32
    emitc.return %sum : i32
  }
  emitc.func @pointer_choice(%which: i1, %left: !emitc.ptr<i32>, %right: !emitc.ptr<i32>) -> i32 {
    %pointer = emitc.conditional %which, %left, %right : !emitc.ptr<i32>
    %place = emitc.dereference %pointer : !emitc.ptr<i32>
    %value = emitc.literal "42" : i32
    emitc.assign %value : i32 to %place : !emitc.lvalue<i32>
    %loaded = emitc.load %place : !emitc.lvalue<i32>
    emitc.return %loaded : i32
  }
  emitc.func @empty_initializer() -> !emitc.opaque<"std::string"> {
    %empty = "emitc.variable"() {value = #emitc.opaque<"">} : () -> !emitc.lvalue<!emitc.opaque<"std::string">>
    %snapshot = emitc.load %empty : !emitc.lvalue<!emitc.opaque<"std::string">>
    emitc.return %snapshot : !emitc.opaque<"std::string">
  }
  emitc.func @loop_total(%limit: index) -> i32 {
    %zero = emitc.literal "0" : index
    %step = emitc.literal "1" : index
    %total = "emitc.variable"() {value = 0 : i32} : () -> !emitc.lvalue<i32>
    emitc.for %i = %zero to %limit step %step {
      %before = emitc.load %total : !emitc.lvalue<i32>
      %index = emitc.cast %i : index to i32
      %after = emitc.add %before, %index : (i32, i32) -> i32
      emitc.assign %after : i32 to %total : !emitc.lvalue<i32>
    }
    %result = emitc.load %total : !emitc.lvalue<i32>
    emitc.return %result : i32
  }
  emitc.func @tuple_values() -> i32 {
    %values:2 = emitc.call_opaque "pair_values"() : () -> (i32, i32)
    %sum = emitc.add %values#0, %values#1 : (i32, i32) -> i32
    emitc.return %sum : i32
  }
  emitc.func @string_value(%text: !emitc.opaque<"std::string">) -> !emitc.opaque<"std::string"> {
    emitc.return %text : !emitc.opaque<"std::string">
  }
  emitc.func @external_value(%value: !emitc.opaque<"ExternalCounter">) -> i32 {
    %result = emitc.cast %value : !emitc.opaque<"ExternalCounter"> to i32
    emitc.return %result : i32
  }
  emitc.func @inline_math(%input: f64) -> f64 {
    %one = emitc.literal "1.0" : f64
    %two = emitc.literal "2.0" : f64
    %expression = emitc.expression %input, %one, %two : (f64, f64, f64) -> f64 {
      %sum = emitc.add %input, %one : (f64, f64) -> f64
      %result = emitc.mul %sum, %two : (f64, f64) -> f64
      emitc.yield %result : f64
    }
    emitc.return %expression : f64
  }
}

//--- isolation.mlir
module {
  emitc.include <"cstdint">
  emitc.func @ordinary_before(%value: i32) -> i32 {
    emitc.return %value : i32
  }
  module @native attributes {ctnative.const_bindings} {
    emitc.func @marked(%value: i32) -> i32 {
      emitc.return %value : i32
    }
    module @ordinary {
      emitc.func @ordinary_nested(%value: i32) -> i32 {
        emitc.return %value : i32
      }
    }
    emitc.func @marked_after(%value: i32) -> i32 {
      emitc.return %value : i32
    }
  }
  module @wrong attributes {ctnative.const_bindings = false} {
    emitc.func @wrong_marker(%value: i32) -> i32 {
      emitc.return %value : i32
    }
  }
  module @named attributes {ctnative.readable_names} {
    emitc.func @names_only(%value: i32 loc(fused<{ctnative.source_name = "catalog"}>["const-bindings.js":20:1])) -> i32 {
      emitc.return %value : i32
    }
  }
  emitc.func @ordinary_after(%value: i32) -> i32 {
    emitc.return %value : i32
  }
}

//--- const-fixture.h
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <type_traits>
#ifndef CTCOMPILE_NO_TYPE_PINS
#define CTCOMPILE_PIN(name, site, ...) static_assert(std::is_same_v<decltype(name), __VA_ARGS__>, "ctcompile: " #name " @ " site)
#else
#define CTCOMPILE_PIN(name, site, ...)
#endif
inline int32_t read_only(int32_t const & value) { return value + 4; }
inline int32_t choose(int32_t & value) { return value + 10; }
inline int32_t choose(int32_t const & value) { return value + 100; }
inline void increment(int32_t & value) { value += 3; }
inline void add_to(int32_t & destination, int32_t const & delta) { destination += delta; }
inline std::tuple<int32_t, int32_t> pair_values() { return {20, 22}; }
struct ExternalCounter {
    int32_t value;
    explicit operator int32_t() { return value + 1; }
    explicit operator int32_t() const { return value + 100; }
};
