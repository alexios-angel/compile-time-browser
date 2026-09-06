// RUN: split-file %s %t
// RUN: ctjs-translate --mlir-to-cpp %t/parameters.mlir > %t/parameters.cpp
// RUN: ctjs-translate --mlir-to-cpp --declare-variables-at-top %t/parameters.mlir > %t/hoisted.cpp
// RUN: ctjs-opt --canonicalize %t/cleanup.mlir | ctjs-translate --mlir-to-cpp > %t/cleanup.cpp
// RUN: python3 %S/check-unused-parameters.py --fixtures %t --work %t.executables --translate ctjs-translate --opt ctjs-opt

// A suppression marker is compiler-owned. Used entry parameters need no cast;
// unused parameters still need one after cleanup. Explicit calls, other opaque
// callees, malformed markers and non-parameter values retain their behavior.

//--- parameters.mlir
#input = loc(fused<{ctnative.source_name = "input"}>["unused-parameters.js":1:1])
#ignored = loc(fused<{ctnative.source_name = "ignored"}>["unused-parameters.js":2:1])
#answer = loc(fused<{ctnative.source_name = "answer"}>["unused-parameters.js":3:1])
module attributes {ctnative.readable_names, ctnative.const_bindings} {
  emitc.include "parameter-fixture.h"
  emitc.func @used(%input: i32 loc(#input)) -> i32 {
    emitc.call_opaque "static_cast<void>"(%input) {ctnative.parameter_suppression, ctnative.const_operands = array<i32: 0>} : (i32) -> ()
    emitc.return %input : i32
  }
  emitc.func @unused(%ignored: i32 loc(#ignored), %input: i32 loc(#input)) -> i32 {
    emitc.call_opaque "static_cast<void>"(%ignored) {ctnative.parameter_suppression, ctnative.const_operands = array<i32: 0>} : (i32) -> ()
    emitc.call_opaque "static_cast<void>"(%input) {ctnative.parameter_suppression, ctnative.const_operands = array<i32: 0>} : (i32) -> ()
    %seven = "emitc.constant"() {value = 7 : i32} : () -> i32
    %answer = "emitc.add"(%input, %seven) {ctnative.deduced} : (i32, i32) -> i32 loc(#answer)
    emitc.return %answer : i32
  }
  emitc.func @explicit_void(%input: i32 loc(#input)) -> i32 {
    emitc.call_opaque "static_cast<void>"(%input) {ctnative.const_operands = array<i32: 0>} : (i32) -> ()
    emitc.return %input : i32
  }
  emitc.func @wrong_marker(%input: i32 loc(#input)) -> i32 {
    emitc.call_opaque "static_cast<void>"(%input) {ctnative.parameter_suppression = false, ctnative.const_operands = array<i32: 0>} : (i32) -> ()
    emitc.return %input : i32
  }
  emitc.func @other_callee(%input: i32 loc(#input)) -> i32 {
    emitc.call_opaque "static_cast<void>"(%input) {ctnative.parameter_suppression, ctnative.const_operands = array<i32: 0>} : (i32) -> ()
    emitc.call_opaque "touch"(%input) {ctnative.parameter_suppression} : (i32) -> ()
    emitc.return %input : i32
  }
  emitc.func @non_parameter() -> i32 {
    %value = "emitc.constant"() {value = 7 : i32} : () -> i32
    emitc.call_opaque "static_cast<void>"(%value) {ctnative.parameter_suppression, ctnative.const_operands = array<i32: 0>} : (i32) -> ()
    emitc.return %value : i32
  }
  emitc.func @opaque_parameter(%input: !emitc.opaque<"ExternalValue"> loc(#input)) -> i32 {
    emitc.call_opaque "static_cast<void>"(%input) {ctnative.parameter_suppression, ctnative.const_operands = array<i32: 0>} : (!emitc.opaque<"ExternalValue">) -> ()
    %out = emitc.call_opaque "read_external"(%input) {ctnative.const_operands = array<i32: 0>} : (!emitc.opaque<"ExternalValue">) -> i32
    emitc.return %out : i32
  }
  emitc.func @verbatim_use(%input: i32 loc(#input)) -> i32 {
    emitc.call_opaque "static_cast<void>"(%input) {ctnative.parameter_suppression, ctnative.const_operands = array<i32: 0>} : (i32) -> ()
    emitc.verbatim "{} += 1;" args %input : i32
    emitc.return %input : i32
  }
  emitc.func @opaque_use(%input: i32 loc(#input)) -> i32 {
    emitc.call_opaque "static_cast<void>"(%input) {ctnative.parameter_suppression, ctnative.const_operands = array<i32: 0>} : (i32) -> ()
    %out = emitc.call_opaque "read_number"(%input) {ctnative.const_operands = array<i32: 0>} : (i32) -> i32
    emitc.return %out : i32
  }
  emitc.func @captured_use(%input: i32 loc(#input)) -> i32 {
    emitc.call_opaque "static_cast<void>"(%input) {ctnative.parameter_suppression, ctnative.const_operands = array<i32: 0>} : (i32) -> ()
    %out = "emitc.expression"(%input) ({
    ^entry(%captured: i32):
      %read = emitc.call_opaque "read_number"(%captured) {ctnative.const_operands = array<i32: 0>} : (i32) -> i32
      emitc.yield %read : i32
    }) : (i32) -> i32
    emitc.return %out : i32
  }
  emitc.func @unused_capture(%ignored: i32 loc(#ignored)) -> i32 {
    emitc.call_opaque "static_cast<void>"(%ignored) {ctnative.parameter_suppression, ctnative.const_operands = array<i32: 0>} : (i32) -> ()
    %out = "emitc.expression"(%ignored) ({
    ^entry(%captured: i32):
      %answer = "emitc.constant"() {value = 42 : i32} : () -> i32
      emitc.yield %answer : i32
    }) : (i32) -> i32
    emitc.return %out : i32
  }
  emitc.func @omitted_argument(%ignored: i32 loc(#ignored)) -> i32 {
    emitc.call_opaque "static_cast<void>"(%ignored) {ctnative.parameter_suppression, ctnative.const_operands = array<i32: 0>} : (i32) -> ()
    %out = emitc.call_opaque "constant_value"(%ignored) {args = [], ctnative.const_operands = array<i32: 0>} : (i32) -> i32
    emitc.return %out : i32
  }
  emitc.func @loop_use(%limit: index) -> i32 {
    emitc.call_opaque "static_cast<void>"(%limit) {ctnative.parameter_suppression, ctnative.const_operands = array<i32: 0>} : (index) -> ()
    %zero = "emitc.constant"() {value = 0 : index} : () -> index
    %one = "emitc.constant"() {value = 1 : index} : () -> index
    %total = "emitc.variable"() {value = 0 : i32} : () -> !emitc.lvalue<i32>
    emitc.for %index = %zero to %limit step %one {
      %part = emitc.cast %index : index to i32
      %old = emitc.load %total : <i32>
      %next = emitc.add %old, %part : (i32, i32) -> i32
      emitc.assign %next : i32 to %total : <i32>
    }
    %out = emitc.load %total : <i32>
    emitc.return %out : i32
  }
}

//--- cleanup.mlir
module attributes {ctnative.const_bindings} {
  emitc.include "parameter-fixture.h"
  emitc.func @after_cleanup(%input: i32) -> i32 {
    emitc.call_opaque "static_cast<void>"(%input) {ctnative.parameter_suppression, ctnative.const_operands = array<i32: 0>} : (i32) -> ()
    %dead = emitc.cast %input {pure} : i32 to i64
    %answer = "emitc.constant"() {value = 42 : i32} : () -> i32
    emitc.return %answer : i32
  }
}

//--- parameter-fixture.h
#include <cstddef>
#include <cstdint>
#include <type_traits>
#ifndef CTCOMPILE_NO_TYPE_PINS
#define CTCOMPILE_PIN(name, site, ...) static_assert(std::is_same_v<decltype(name), __VA_ARGS__>, site)
#else
#define CTCOMPILE_PIN(name, site, ...)
#endif
inline void touch(int32_t & value) { ++value; }
inline int32_t read_number(int32_t const & value) { return value + 1; }
inline int32_t constant_value() { return 42; }
struct ExternalValue { int32_t value; };
inline int32_t read_external(ExternalValue const & value) { return value.value; }

//--- native-source.js
function add(left, right) { return left + right; }
function ignored(unused, value) { return value + 1; }
function erased(flag, value) { if (flag) return value; return value; }
function makeAdder(base) { return step => base + step; }
function retained() { const fn = makeAdder(40); return fn(2); }
var addResult = add(10, 32);
var ignoredResult = ignored(999, 41);
var erasedResult = erased(true, 42) + erased(false, 0);
var retainedResult = retained();
