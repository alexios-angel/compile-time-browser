// RUN: split-file %s %t
// RUN: ctjs-translate --mlir-to-cpp %t/callables.mlir > %t/callables.cpp
// RUN: ctjs-translate --mlir-to-cpp --declare-variables-at-top %t/callables.mlir > %t/hoisted.cpp
// RUN: python3 %S/check-callable-bodies.py --fixtures %t --work %t.executables --translate ctjs-translate

// An immutable capture permits body emission even when a lambda argument is
// mutable: that argument is a fresh value on each invocation. A mutable or
// unknown capture use must keep the lifted function's per-call value copy.

//--- callables.mlir
#text = loc(fused<{ctnative.source_name = "text"}>["callable-creation.js":1:1])
#seed = loc(fused<{ctnative.source_name = "seed"}>["callable-creation.js":2:1])
#first = loc(fused<{ctnative.source_name = "first"}>["callable-creation.js":3:1])
#second = loc(fused<{ctnative.source_name = "second"}>["callable-creation.js":4:1])
#offset = loc(fused<{ctnative.source_name = "offset"}>["callable-creation.js":5:1])
#after = loc(fused<{ctnative.source_name = "after"}>["callable-creation.js":6:1])
#lambda = loc(fused<{ctnative.source_name = "ctn_lambda"}>["callable-creation.js":7:1])
#lambda_version = loc(fused<{ctnative.source_name = "ctn_lambda_1"}>["callable-creation.js":7:2])
module attributes {ctnative.readable_names, ctnative.const_bindings, ctnative.constexpr_bindings} {
  emitc.include "callable-body-fixture.h"
  emitc.declare_func @inline_target
  emitc.declare_func @mutable_target
  emitc.declare_func @unknown_target
  emitc.declare_func @address_target
  emitc.declare_func @literal_target
  emitc.declare_func @classified_target
  emitc.declare_func @string_target
  emitc.declare_func @nested_target
  emitc.declare_func @recursive_target
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
  emitc.func @unmarked_creation(%seed: i32) -> !emitc.opaque<"ctnative::ctn_env_inline"> {
    %closure = emitc.call_opaque "ctn_bind_inline"(%seed) {ctnative.const_operands = array<i32: 0>} : (i32) -> !emitc.opaque<"ctnative::ctn_env_inline">
    emitc.return %closure : !emitc.opaque<"ctnative::ctn_env_inline">
  }
  // Generated names must neither shadow source bindings nor each other.
  emitc.func @anonymous_names(%seed: i32 loc(#lambda), %offset: i32 loc(#lambda_version)) -> i32 {
    %first = emitc.call_opaque "ctn_bind_inline"(%seed) {ctnative.callable_create = @inline_target, ctnative.const_operands = array<i32: 0>} : (i32) -> !emitc.opaque<"ctnative::ctn_env_inline">
    %second = emitc.call_opaque "ctn_bind_inline"(%offset) {ctnative.callable_create = @inline_target, ctnative.const_operands = array<i32: 0>} : (i32) -> !emitc.opaque<"ctnative::ctn_env_inline">
    %a = emitc.call_opaque "std::invoke"(%first, %offset) {ctnative.const_operands = array<i32: 0, 1>} : (!emitc.opaque<"ctnative::ctn_env_inline">, i32) -> i32
    %b = emitc.call_opaque "std::invoke"(%second, %seed) {ctnative.const_operands = array<i32: 0, 1>} : (!emitc.opaque<"ctnative::ctn_env_inline">, i32) -> i32
    %sum = emitc.add %a, %b : (i32, i32) -> i32
    emitc.return %sum : i32
  }
  emitc.func @mutable_target(%seed: i32) -> i32 attributes {
      ctnative.callable_body = {type = "ctnative::ctn_env_mutable", binder = "ctn_bind_mutable", name = "ctn_lambda", captures = ["capture_seed"], parameters = []}} {
    %changed = emitc.call_opaque "increment"(%seed) : (i32) -> i32
    emitc.return %changed : i32
  }
  emitc.func @marked_mutable(%seed: i32) -> !emitc.opaque<"ctnative::ctn_env_mutable"> {
    %closure = emitc.call_opaque "ctn_bind_mutable"(%seed) {ctnative.callable_create = @mutable_target, ctnative.const_operands = array<i32: 0>} : (i32) -> !emitc.opaque<"ctnative::ctn_env_mutable">
    emitc.return %closure : !emitc.opaque<"ctnative::ctn_env_mutable">
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
  emitc.func @classified_target(%seed: f64) -> i32 attributes {
      ctnative.callable_body = {type = "ctnative::ctn_env_classified", binder = "ctn_bind_classified", name = "ctn_lambda", captures = ["capture_seed"], parameters = []}} {
    %result = emitc.call_opaque "classify_capture"(%seed) {ctnative.const_operands = array<i32: 0>} : (f64) -> i32
    emitc.return %result : i32
  }
  emitc.func @deferred_literal_marked() -> !emitc.opaque<"ctnative::ctn_env_classified"> {
    // Literal text need not have its SSA type in C++. The binder converts this
    // integer literal to double; a creation-site capture must do the same.
    %seed = emitc.literal "1" : f64
    %closure = emitc.call_opaque "ctn_bind_classified"(%seed) {ctnative.callable_create = @classified_target, ctnative.const_operands = array<i32: 0>} : (f64) -> !emitc.opaque<"ctnative::ctn_env_classified">
    emitc.return %closure : !emitc.opaque<"ctnative::ctn_env_classified">
  }
  emitc.func @deferred_literal_unmarked() -> !emitc.opaque<"ctnative::ctn_env_classified"> {
    %seed = emitc.literal "1" : f64
    %closure = emitc.call_opaque "ctn_bind_classified"(%seed) {ctnative.const_operands = array<i32: 0>} : (f64) -> !emitc.opaque<"ctnative::ctn_env_classified">
    emitc.return %closure : !emitc.opaque<"ctnative::ctn_env_classified">
  }
  emitc.func @inline_expression_marked() -> !emitc.opaque<"ctnative::ctn_env_classified"> {
    %one = emitc.literal "1" : f64
    %two = emitc.literal "2" : f64
    // Keep creation inside the expression so its capture is emitted as 1 + 2,
    // rather than materializing a double result before constructing the lambda.
    %closure = "emitc.expression"(%one, %two) ({
    ^bb0(%a: f64, %b: f64):
      %sum = emitc.add %a, %b : (f64, f64) -> f64
      %made = emitc.call_opaque "ctn_bind_classified"(%sum) {ctnative.callable_create = @classified_target, ctnative.const_operands = array<i32: 0>} : (f64) -> !emitc.opaque<"ctnative::ctn_env_classified">
      emitc.yield %made : !emitc.opaque<"ctnative::ctn_env_classified">
    }) : (f64, f64) -> !emitc.opaque<"ctnative::ctn_env_classified">
    emitc.return %closure : !emitc.opaque<"ctnative::ctn_env_classified">
  }
  emitc.func @inline_expression_unmarked() -> !emitc.opaque<"ctnative::ctn_env_classified"> {
    %one = emitc.literal "1" : f64
    %two = emitc.literal "2" : f64
    %closure = "emitc.expression"(%one, %two) ({
    ^bb0(%a: f64, %b: f64):
      %sum = emitc.add %a, %b : (f64, f64) -> f64
      %made = emitc.call_opaque "ctn_bind_classified"(%sum) {ctnative.const_operands = array<i32: 0>} : (f64) -> !emitc.opaque<"ctnative::ctn_env_classified">
      emitc.yield %made : !emitc.opaque<"ctnative::ctn_env_classified">
    }) : (f64, f64) -> !emitc.opaque<"ctnative::ctn_env_classified">
    emitc.return %closure : !emitc.opaque<"ctnative::ctn_env_classified">
  }
  emitc.func @string_target(%text: !emitc.opaque<"std::string">) -> i32 attributes {
      ctnative.callable_body = {type = "ctnative::ctn_env_string", binder = "ctn_bind_string", name = "ctn_lambda", captures = ["capture_text"], parameters = []}} {
    %length = emitc.call_opaque "text_length"(%text) {ctnative.const_operands = array<i32: 0>} : (!emitc.opaque<"std::string">) -> i32
    // Deliberately reuse the outer source name in the independent lambda scope.
    %innerSeed = "emitc.constant"() {value = 2 : i32} : () -> i32 loc(#seed)
    %answer = emitc.add %length, %innerSeed : (i32, i32) -> i32
    emitc.return %answer : i32
  }
  emitc.func @creation_sites(%text: !emitc.opaque<"std::string"> loc(#text)) -> i32 {
    %seed = "emitc.constant"() {value = 40 : i32} : () -> i32 loc(#seed)
    %first = emitc.call_opaque "ctn_bind_string"(%text) {ctnative.callable_create = @string_target, ctnative.const_operands = array<i32: 0>} : (!emitc.opaque<"std::string">) -> !emitc.opaque<"ctnative::ctn_env_string"> loc(#first)
    %offset = "emitc.constant"() {value = 2 : i32} : () -> i32 loc(#offset)
    %second = emitc.call_opaque "ctn_bind_string"(%text) {ctnative.callable_create = @string_target, ctnative.const_operands = array<i32: 0>, ctnative.deduced} : (!emitc.opaque<"std::string">) -> !emitc.opaque<"ctnative::ctn_env_string"> loc(#second)
    // This mutable-reference call also prevents const from accidentally hiding
    // an incorrect move at either creation site. Captured snapshots must not
    // observe the later change to the live outer string.
    %live = emitc.call_opaque "append_marker"(%text) : (!emitc.opaque<"std::string">) -> i32
    %a = emitc.call_opaque "invoke_empty"(%first) {ctnative.const_operands = array<i32: 0>} : (!emitc.opaque<"ctnative::ctn_env_string">) -> i32
    %b = emitc.call_opaque "invoke_empty"(%second) {ctnative.const_operands = array<i32: 0>} : (!emitc.opaque<"ctnative::ctn_env_string">) -> i32
    %after = emitc.add %seed, %offset : (i32, i32) -> i32 loc(#after)
    %sum = emitc.add %a, %b : (i32, i32) -> i32
    %withLive = emitc.add %sum, %live : (i32, i32) -> i32
    %answer = emitc.add %withLive, %after : (i32, i32) -> i32
    emitc.return %answer : i32
  }
  emitc.func @nested_target(%text: !emitc.opaque<"std::string">) -> !emitc.opaque<"ctnative::ctn_env_string"> attributes {
      ctnative.callable_body = {type = "ctnative::ctn_env_nested", binder = "ctn_bind_nested", name = "ctn_lambda", captures = ["capture_text"], parameters = []}} {
    %inner = emitc.call_opaque "ctn_bind_string"(%text) {ctnative.callable_create = @string_target, ctnative.const_operands = array<i32: 0>} : (!emitc.opaque<"std::string">) -> !emitc.opaque<"ctnative::ctn_env_string">
    emitc.return %inner : !emitc.opaque<"ctnative::ctn_env_string">
  }
  emitc.func @nested_creation(%text: !emitc.opaque<"std::string"> loc(#text)) -> !emitc.opaque<"ctnative::ctn_env_string"> {
    %outer = emitc.call_opaque "ctn_bind_nested"(%text) {ctnative.callable_create = @nested_target, ctnative.const_operands = array<i32: 0>} : (!emitc.opaque<"std::string">) -> !emitc.opaque<"ctnative::ctn_env_nested">
    %inner = emitc.call_opaque "invoke_factory"(%outer) {ctnative.const_operands = array<i32: 0>} : (!emitc.opaque<"ctnative::ctn_env_nested">) -> !emitc.opaque<"ctnative::ctn_env_string">
    emitc.return %inner : !emitc.opaque<"ctnative::ctn_env_string">
  }
  emitc.func @recursive_target(%seed: i32) -> i32 attributes {
      ctnative.callable_body = {type = "ctnative::ctn_env_recursive", binder = "ctn_bind_recursive", name = "ctn_lambda", captures = ["capture_seed"], parameters = []}} {
    // The metadata graph is recursive; execution only constructs a callable.
    %again = emitc.call_opaque "ctn_bind_recursive"(%seed) {ctnative.callable_create = @recursive_target, ctnative.const_operands = array<i32: 0>} : (i32) -> !emitc.opaque<"ctnative::ctn_env_recursive">
    emitc.call_opaque "static_cast<void>"(%again) {ctnative.const_operands = array<i32: 0>} : (!emitc.opaque<"ctnative::ctn_env_recursive">) -> ()
    emitc.return %seed : i32
  }
  emitc.func @recursive_creation(%seed: i32) -> !emitc.opaque<"ctnative::ctn_env_recursive"> {
    %closure = emitc.call_opaque "ctn_bind_recursive"(%seed) {ctnative.callable_create = @recursive_target, ctnative.const_operands = array<i32: 0>} : (i32) -> !emitc.opaque<"ctnative::ctn_env_recursive">
    emitc.return %closure : !emitc.opaque<"ctnative::ctn_env_recursive">
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

//--- invalid-creation.mlir
module attributes {ctnative.readable_names, ctnative.const_bindings} {
  emitc.func @bad_marker(%seed: i32) -> !emitc.opaque<"ctnative::ctn_env_invalid"> {
    %closure = emitc.call_opaque "ctn_bind_invalid"(%seed) {ctnative.callable_create = "not a symbol reference"} : (i32) -> !emitc.opaque<"ctnative::ctn_env_invalid">
    emitc.return %closure : !emitc.opaque<"ctnative::ctn_env_invalid">
  }
}

//--- invalid-signature.mlir
module attributes {ctnative.readable_names, ctnative.const_bindings} {
  emitc.func @bad_signature(%seed: i32) -> i32 attributes {
      ctnative.callable_body = {type = "ctnative::ctn_env_bad_signature", binder = "ctn_bind_bad_signature", name = "ctn_lambda", captures = ["capture_seed"], parameters = []}} {
    emitc.return %seed : i32
  }
  emitc.func @wrong_capture_type(%seed: f64) -> !emitc.opaque<"ctnative::ctn_env_bad_signature"> {
    %closure = emitc.call_opaque "ctn_bind_bad_signature"(%seed) {ctnative.callable_create = @bad_signature} : (f64) -> !emitc.opaque<"ctnative::ctn_env_bad_signature">
    emitc.return %closure : !emitc.opaque<"ctnative::ctn_env_bad_signature">
  }
}

//--- callable-body-fixture.h
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#ifndef CTCOMPILE_NO_TYPE_PINS
#define CTCOMPILE_PIN(name, site, ...) static_assert(std::is_same_v<decltype(name), __VA_ARGS__>, site)
#else
#define CTCOMPILE_PIN(name, site, ...)
#endif
namespace ctnative {
using ctn_env_inline = std::function<int32_t(int32_t)>;
using ctn_env_mutable = std::function<int32_t()>;
using ctn_env_unknown = std::function<int32_t()>;
using ctn_env_address = std::function<int32_t(int32_t)>;
using ctn_env_literal = std::function<int32_t()>;
using ctn_env_classified = std::function<int32_t()>;
using ctn_env_string = std::function<int32_t()>;
using ctn_env_nested = std::function<ctn_env_string()>;
using ctn_env_recursive = std::function<int32_t()>;
}
using binary_function = int32_t (*)(int32_t, int32_t);
using unary_function = int32_t (*)(int32_t);
inline int32_t increment(int32_t & value) { return ++value; }
inline int32_t read_unknown(int32_t const & value) { return value + 2; }
inline int32_t classify_capture(double value) { return static_cast<int32_t>(value) * 10 + 1; }
inline int32_t classify_capture(int32_t value) { return value * 10 + 2; }
inline int32_t text_length(std::string const & text) { return static_cast<int32_t>(text.size()); }
inline int32_t append_marker(std::string & text) { text += '!'; return text_length(text); }
inline int32_t invoke_empty(ctnative::ctn_env_string const & callable) { return callable(); }
inline ctnative::ctn_env_string invoke_factory(ctnative::ctn_env_nested const & callable) { return callable(); }
