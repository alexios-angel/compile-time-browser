// RUN: split-file %s %t
// RUN: ctjs-translate --mlir-to-cpp %t/callables.mlir > %t/callables.cpp
// RUN: ctjs-translate --mlir-to-cpp --declare-variables-at-top %t/callables.mlir > %t/hoisted.cpp
// RUN: python3 %S/check-callable-bodies.py --fixtures %t --work %t.executables --translate ctjs-translate

// An immutable capture permits body emission even when a lambda argument is
// mutable: that argument is a fresh value on each invocation. A mutable or
// unknown capture use must keep the lifted function's per-call value copy.

//--- callables.mlir
module attributes {ctnative.readable_names, ctnative.const_bindings} {
  emitc.include "callable-body-fixture.h"
  emitc.declare_func @inline_target
  emitc.declare_func @mutable_target
  emitc.declare_func @unknown_target
  emitc.declare_func @address_target
  emitc.declare_func @literal_target
  emitc.func @inline_target(%seed: i32, %delta: i32) -> i32 attributes {
      ctnative.callable_body = {type = "ctnative::ctn_env_inline", binder = "ctn_bind_inline", name = "ctn_lambda", captures = ["capture_seed"], parameters = ["argument_delta"]}} {
    %changed = emitc.call_opaque "increment"(%delta) : (i32) -> i32
    %sum = emitc.add %seed, %changed : (i32, i32) -> i32
    emitc.return %sum : i32
  }
  emitc.func @direct_caller(%input: i32) -> i32 {
    %one = "emitc.constant"() {value = 1 : i32} : () -> i32
    %sum = emitc.call @inline_target(%input, %one) : (i32, i32) -> i32
    emitc.return %sum : i32
  }
  emitc.func @mutable_target(%seed: i32) -> i32 attributes {
      ctnative.callable_body = {type = "ctnative::ctn_env_mutable", binder = "ctn_bind_mutable", name = "ctn_lambda", captures = ["capture_seed"], parameters = []}} {
    %changed = emitc.call_opaque "increment"(%seed) : (i32) -> i32
    emitc.return %changed : i32
  }
  emitc.func @unknown_target(%seed: i32) -> i32 attributes {
      ctnative.callable_body = {type = "ctnative::ctn_env_unknown", binder = "ctn_bind_unknown", name = "ctn_lambda", captures = ["capture_seed"], parameters = []}} {
    // The C++ helper happens to read only; IR carries no const-operand proof.
    %read = emitc.call_opaque "read_unknown"(%seed) : (i32) -> i32
    emitc.return %read : i32
  }
  emitc.func @address_target(%seed: i32, %delta: i32) -> i32 attributes {
      ctnative.callable_body = {type = "ctnative::ctn_env_address", binder = "ctn_bind_address", name = "ctn_lambda", captures = ["capture_seed"], parameters = ["argument_delta"]}} {
    %sum = emitc.add %seed, %delta : (i32, i32) -> i32
    emitc.return %sum : i32
  }
  emitc.func @address_pointer() -> !emitc.opaque<"binary_function"> {
    // This is the only emitted reference to the ordinary address_target.
    %pointer = "emitc.constant"() {value = #emitc.opaque<"&address_target">} : () -> !emitc.opaque<"binary_function">
    emitc.return %pointer : !emitc.opaque<"binary_function">
  }
  emitc.func @literal_target(%seed: i32) -> i32 attributes {
      ctnative.callable_body = {type = "ctnative::ctn_env_literal", binder = "ctn_bind_literal", name = "ctn_lambda", captures = ["capture_seed"], parameters = []}} {
    emitc.return %seed : i32
  }
  emitc.func @literal_pointer() -> !emitc.opaque<"unary_function"> {
    // Literal text is independent of opaque-attribute and symbol uses.
    %pointer = emitc.literal "&literal_target" : !emitc.opaque<"unary_function">
    emitc.return %pointer : !emitc.opaque<"unary_function">
  }
}

//--- invalid.mlir
module attributes {ctnative.readable_names, ctnative.const_bindings} {
  emitc.func @invalid(%seed: i32, %delta: i32) -> i32 attributes {
      ctnative.callable_body = {type = "ctnative::ctn_env_invalid", binder = "ctn_bind_invalid", name = "ctn_lambda", captures = ["duplicate"], parameters = ["duplicate"]}} {
    %sum = emitc.add %seed, %delta : (i32, i32) -> i32
    emitc.return %sum : i32
  }
}

//--- callable-body-fixture.h
#include <cstdint>
#include <functional>
#include <utility>
namespace ctnative {
using ctn_env_inline = std::function<int32_t(int32_t)>;
using ctn_env_mutable = std::function<int32_t()>;
using ctn_env_unknown = std::function<int32_t()>;
using ctn_env_address = std::function<int32_t(int32_t)>;
using ctn_env_literal = std::function<int32_t()>;
}
using binary_function = int32_t (*)(int32_t, int32_t);
using unary_function = int32_t (*)(int32_t);
inline int32_t increment(int32_t & value) { return ++value; }
inline int32_t read_unknown(int32_t const & value) { return value + 2; }
