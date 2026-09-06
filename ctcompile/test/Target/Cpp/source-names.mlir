// Source names are presentation metadata, consumed only by the native printer.
// Compile/run cases below catch accidental C++ rebinding, including code that
// remains syntactically valid after a local shadows a generated global.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --mlir-to-cpp %t/named.mlir > %t/named.cpp
// RUN: FileCheck %s --check-prefix=NAMES --implicit-check-not=phantom_ < %t/named.cpp
// RUN: ctjs-translate --mlir-to-cpp %t/loops.mlir > %t/loops.cpp
// RUN: ctjs-translate --mlir-to-cpp --declare-variables-at-top %t/loops.mlir > %t/loops-top.cpp
// RUN: FileCheck %s --check-prefix=LOOPS < %t/loops.cpp
// RUN: FileCheck %s --check-prefix=HOISTED < %t/loops-top.cpp
// RUN: ctjs-translate --mlir-to-cpp %t/standard-headers.mlir > %t/standard-headers.cpp
// RUN: FileCheck %s --check-prefix=HEADERS < %t/standard-headers.cpp
// RUN: ctjs-translate --mlir-to-cpp %t/opaque-types.mlir > %t/opaque-types.cpp
// RUN: FileCheck %s --check-prefix=OPAQUE < %t/opaque-types.cpp
// RUN: ctjs-translate --mlir-to-cpp %t/isolation.mlir | FileCheck %s --check-prefix=ISOLATION
// RUN: python3 %S/check-source-names.py --fixtures %t --work %t.executables

// First binding, returned snapshot, and other materialized intermediates.
// The prototype and definition must allocate the same parameter spelling.
// NAMES: double naming(double catalog);
// NAMES: double naming(double catalog) {
// NAMES-NEXT: auto score_1 = catalog + 1.0;
// NAMES-NEXT: CTCOMPILE_PIN(score_1, "source-names.js:2:3", double);
// NAMES-NEXT: double score_3 = score_1 * 2.0;
// NAMES-NEXT: auto score_2 = score_3 + 1.0;
// NAMES-NEXT: CTCOMPILE_PIN(score_2, "source-names.js:2:3", double);
// NAMES-NEXT: return score_2;
// A function boundary resets local allocations.
// NAMES: double fresh(double catalog) {
// NAMES-NEXT: return catalog;
// Multi-result hints belong to their individual results.
// NAMES: int32_t pair_sum() {
// NAMES-NEXT: int32_t left;
// NAMES-NEXT: int32_t right;
// NAMES-NEXT: std::tie(left, right) = pair_values();
// NAMES: int32_t fallback_collision() {
// NAMES-NEXT: int32_t [[ANON:v[0-9]+]] = 1;
// NAMES-NEXT: int32_t v1 = 40;
// NAMES-NEXT: int32_t [[SUM:[A-Za-z_][A-Za-z_0-9]*]] = [[ANON]] + v1;
// NAMES-NEXT: return [[SUM]];
// Edge-case sanitized names are deliberately checked by C++ execution below,
// not by pinning one particular escaping or collision-suffix policy.
// NAMES: int32_t collisions(int32_t v1) {
// NAMES: named_helper(v1);
// NAMES: opaque_helper(v1);
// NAMES: library::bump(v1);
// NAMES: int32_t deferred_values() {
// NAMES: Cell object = Cell{7};
// NAMES: int32_t field_read = object.field;
// NAMES: int32_t array_read = entries[1];
// NAMES: return (field_read + array_read) + 5;
// NAMES: int32_t statement_only() {
// NAMES-NEXT: touch();
// NAMES-NEXT: return 0;

// The induction variable and its outer bound request the same source family.
// Captured names in the header/body must continue to refer to distinct values.
// LOOPS: int32_t loop_names(size_t [[LIMIT:[A-Za-z_][A-Za-z_0-9]*]]) {
// LOOPS: for (size_t [[OUTER:[A-Za-z_][A-Za-z_0-9]*]] = 0; [[OUTER]] < [[LIMIT]]; [[OUTER]] += 1) {
// LOOPS: (int32_t) [[OUTER]];
// LOOPS: for (size_t [[INNER:[A-Za-z_][A-Za-z_0-9]*]] = 0; [[INNER]] < 1; [[INNER]] += 1) {
// LOOPS: for (size_t [[SIBLING:[A-Za-z_][A-Za-z_0-9]*]] = 0; [[SIBLING]] < [[LIMIT]]; [[SIBLING]] += 1) {
// HOISTED: int32_t loop_names(size_t [[LIMIT:[A-Za-z_][A-Za-z_0-9]*]]) {
// HOISTED: int32_t [[STORAGE:[A-Za-z_][A-Za-z_0-9]*]];
// HOISTED: [[STORAGE]] = 0;
// HOISTED: for (size_t [[OUTER:[A-Za-z_][A-Za-z_0-9]*]] = 0; [[OUTER]] < [[LIMIT]]; [[OUTER]] += 1) {

// These spellings occur only in source metadata, never opaque/verbatim tokens:
// built-in MLIR types and standard includes must reserve their C++ identifiers.
// Compilation checks the suffix policy without prescribing its exact spelling.
// HEADERS: int32_t header_argument(int32_t [[TYPE_ARG:[A-Za-z_][A-Za-z_0-9]*]]) {
// HEADERS-NEXT: int32_t [[MAXIMUM:[A-Za-z_][A-Za-z_0-9]*]] = 7;
// HEADERS-NEXT: int32_t [[SUCCESS:[A-Za-z_][A-Za-z_0-9]*]] = 11;
// HEADERS-NEXT: int32_t [[HEADER_SUM:[A-Za-z_][A-Za-z_0-9]*]] = [[TYPE_ARG]] + [[MAXIMUM]];
// HEADERS-NEXT: int32_t [[HEADER_RESULT:[A-Za-z_][A-Za-z_0-9]*]] = [[HEADER_SUM]] + [[SUCCESS]];
// HEADERS-NEXT: return [[HEADER_RESULT]];
// HEADERS: int32_t header_local() {
// HEADERS-NEXT: int32_t [[TYPE_LOCAL:[A-Za-z_][A-Za-z_0-9]*]] = 40;
// HEADERS-NEXT: int32_t [[AFTER_TYPE:[A-Za-z_][A-Za-z_0-9]*]] = 2;
// HEADERS-NEXT: int32_t [[LOCAL_RESULT:[A-Za-z_][A-Za-z_0-9]*]] = [[TYPE_LOCAL]] + [[AFTER_TYPE]];
// HEADERS-NEXT: return [[LOCAL_RESULT]];
// HEADERS: int32_t header_index(size_t [[INDEX_ARG:[A-Za-z_][A-Za-z_0-9]*]]) {
// HEADERS-NEXT: size_t [[INDEX_OFFSET:[A-Za-z_][A-Za-z_0-9]*]] = 3;
// HEADERS-NEXT: size_t [[INDEX_SUM:[A-Za-z_][A-Za-z_0-9]*]] = [[INDEX_ARG]] + [[INDEX_OFFSET]];

// Counter is defined in a header consumed only by the C++ compiler. It appears in
// this MLIR split only as an opaque type or source hint, not opaque code tokens.
// OPAQUE: int32_t opaque_argument(Counter [[OPAQUE_ARG:[A-Za-z_][A-Za-z_0-9]*]]) {
// OPAQUE-NEXT: Counter [[OPAQUE_COPY:[A-Za-z_][A-Za-z_0-9]*]] = (Counter) [[OPAQUE_ARG]];
// OPAQUE: int32_t opaque_local() {
// OPAQUE-NEXT: Counter [[OPAQUE_LOCAL:[A-Za-z_][A-Za-z_0-9]*]] = 40;
// OPAQUE-NEXT: Counter [[OPAQUE_LATER:[A-Za-z_][A-Za-z_0-9]*]] = 2;

// Nearest-module UnitAttr gating; metadata alone and ordinary NameLocs do not
// change upstream spellings. Native scope state cannot leak across functions.
// ISOLATION: int32_t ordinary_before(int32_t v1) {
// ISOLATION-NEXT: return v1;
// ISOLATION: int32_t marked(int32_t catalog) {
// ISOLATION-NEXT: return catalog;
// ISOLATION: int32_t ordinary_nested(int32_t v1) {
// ISOLATION-NEXT: return v1;
// ISOLATION: int32_t marked_after(int32_t catalog) {
// ISOLATION-NEXT: return catalog;
// ISOLATION: int32_t legacy_name_loc(int32_t v1) {
// ISOLATION-NEXT: return v1;
// ISOLATION: int32_t wrong_marker(int32_t v1) {
// ISOLATION-NEXT: return v1;
// ISOLATION: int32_t ordinary_after(int32_t v1) {
// ISOLATION-NEXT: return v1;

//--- named.mlir
#catalog = loc(fused<{ctnative.source_name = "catalog"}>["source-names.js":1:15])
#score = loc(fused<{ctnative.source_name = "score"}>["source-names.js":2:3])
#pair = loc(fused<{ctnative.source_names = ["left", "right"]}>["source-names.js":3:3])
module attributes {ctnative.readable_names} {
  emitc.include <"cstdint">
  emitc.include <"cmath">
  emitc.include <"cstdio">
  emitc.include <"tuple">
  emitc.include <"type_traits">
  emitc.global @g_value : i32 = 17
  emitc.global @g_touches : i32 = 0
  emitc.global @entries : !emitc.array<2xi32> = dense<[11, 13]>
  emitc.verbatim "#define CTCOMPILE_PIN(value, site, ...) static_assert(std::is_same_v<decltype(value), __VA_ARGS__>, site)"
  emitc.verbatim "#define SOURCE_NAME_MACRO 11"
  emitc.verbatim "using Counter = int32_t;\nstruct Cell { int32_t field; };\ninline int32_t opaque_helper(int32_t argument) { return argument + 7; }\nnamespace library { inline int32_t bump(int32_t argument) { return argument + 1; } }\ninline std::tuple<int32_t, int32_t> pair_values() { return {20, 22}; }\ninline int32_t touch() { ++g_touches; return 99; }"

  emitc.declare_func @naming
  emitc.func @naming(%catalog: f64 loc(#catalog)) -> f64 {
    %one = emitc.literal "1.0" : f64
    %two = emitc.literal "2.0" : f64
    %initial = "emitc.add"(%catalog, %one) {ctnative.deduced} : (f64, f64) -> f64 loc(#score)
    %intermediate = emitc.mul %initial, %two : (f64, f64) -> f64
    %returned = "emitc.add"(%intermediate, %one) {ctnative.deduced} : (f64, f64) -> f64 loc(#score)
    emitc.return %returned : f64
  }
  emitc.func @fresh(%catalog: f64 loc(#catalog)) -> f64 {
    emitc.return %catalog : f64
  }
  emitc.func @pair_sum() -> i32 {
    %pair:2 = emitc.call_opaque "pair_values"() : () -> (i32, i32) loc(#pair)
    %sum = emitc.add %pair#0, %pair#1 : (i32, i32) -> i32
    emitc.return %sum : i32
  }
  emitc.func @fallback_collision() -> i32 {
    %anonymous = "emitc.constant"() {value = 1 : i32} : () -> i32
    %named = "emitc.constant"() {value = 40 : i32} : () -> i32 loc(fused<{ctnative.source_name = "v1"}>["source-names.js":5:3])
    %sum = emitc.add %anonymous, %named : (i32, i32) -> i32
    emitc.return %sum : i32
  }
  emitc.func @named_helper(%argument: i32) -> i32 {
    %two = emitc.literal "2" : i32
    %out = emitc.mul %argument, %two : (i32, i32) -> i32
    emitc.return %out : i32
  }
  emitc.func @collisions(%input: i32 loc(fused<{ctnative.source_name = "v1"}>["source-names.js":7:1])) -> i32 {
    %keyword = "emitc.constant"() {value = 1 : i32} : () -> i32 loc(fused<{ctnative.source_name = "double"}>["source-names.js":8:1])
    %macro = "emitc.constant"() {value = 2 : i32} : () -> i32 loc(fused<{ctnative.source_name = "NAN"}>["source-names.js":9:1])
    %eof = "emitc.constant"() {value = 3 : i32} : () -> i32 loc(fused<{ctnative.source_name = "EOF"}>["source-names.js":10:1])
    %function = "emitc.constant"() {value = 4 : i32} : () -> i32 loc(fused<{ctnative.source_name = "named_helper"}>["source-names.js":11:1])
    %global = "emitc.constant"() {value = 5 : i32} : () -> i32 loc(fused<{ctnative.source_name = "g_value"}>["source-names.js":12:1])
    %opaque = "emitc.constant"() {value = 6 : i32} : () -> i32 loc(fused<{ctnative.source_name = "opaque_helper"}>["source-names.js":13:1])
    %type = "emitc.constant"() {value = 7 : i32} : () -> i32 loc(fused<{ctnative.source_name = "Counter"}>["source-names.js":14:1])
    %namespace = "emitc.constant"() {value = 8 : i32} : () -> i32 loc(fused<{ctnative.source_name = "library"}>["source-names.js":15:1])
    %dollar = "emitc.constant"() {value = 9 : i32} : () -> i32 loc(fused<{ctnative.source_name = "cash$"}>["source-names.js":16:1])
    %encoded = "emitc.constant"() {value = 10 : i32} : () -> i32 loc(fused<{ctnative.source_name = "cashu24"}>["source-names.js":17:1])
    %customMacro = "emitc.constant"() {value = 11 : i32} : () -> i32 loc(fused<{ctnative.source_name = "SOURCE_NAME_MACRO"}>["source-names.js":18:1])
    %a = emitc.add %keyword, %macro : (i32, i32) -> i32
    %b = emitc.add %eof, %function : (i32, i32) -> i32
    %c = emitc.add %global, %opaque : (i32, i32) -> i32
    %d = emitc.add %type, %namespace : (i32, i32) -> i32
    %e = emitc.add %dollar, %encoded : (i32, i32) -> i32
    %ab = emitc.add %a, %b : (i32, i32) -> i32
    %cd = emitc.add %c, %d : (i32, i32) -> i32
    %abcd = emitc.add %ab, %cd : (i32, i32) -> i32
    %abcde = emitc.add %abcd, %e : (i32, i32) -> i32
    %all = emitc.add %abcde, %customMacro : (i32, i32) -> i32
    %globalRef = emitc.get_global @g_value : !emitc.lvalue<i32>
    %globalValue = emitc.load %globalRef : <i32>
    %direct = emitc.call @named_helper(%input) : (i32) -> i32
    %opaqueResult = emitc.call_opaque "opaque_helper"(%input) : (i32) -> i32
    %namespaceResult = emitc.call_opaque "library::bump"(%input) : (i32) -> i32
    %typed = emitc.call_opaque "static_cast<Counter>"(%all) : (i32) -> !emitc.opaque<"Counter">
    %untyped = emitc.call_opaque "static_cast<int32_t>"(%typed) : (!emitc.opaque<"Counter">) -> i32
    %plusGlobal = emitc.add %untyped, %globalValue : (i32, i32) -> i32
    %plusDirect = emitc.add %plusGlobal, %direct : (i32, i32) -> i32
    %plusOpaque = emitc.add %plusDirect, %opaqueResult : (i32, i32) -> i32
    %out = emitc.add %plusOpaque, %namespaceResult : (i32, i32) -> i32
    emitc.return %out : i32
  }
  emitc.func @deferred_values() -> i32 {
    %object = "emitc.variable"() {value = #emitc.opaque<"Cell{7}">} : () -> !emitc.lvalue<!emitc.opaque<"Cell">> loc(fused<{ctnative.source_name = "object"}>["source-names.js":20:1])
    %field = "emitc.member"(%object) {member = "field"} : (!emitc.lvalue<!emitc.opaque<"Cell">>) -> !emitc.lvalue<i32> loc(fused<{ctnative.source_name = "phantom_member"}>["source-names.js":21:1])
    %fieldRead = emitc.load %field : <i32> loc(fused<{ctnative.source_name = "field_read"}>["source-names.js":22:1])
    %array = emitc.get_global @entries : !emitc.array<2xi32> loc(fused<{ctnative.source_name = "phantom_global"}>["source-names.js":23:1])
    %index = emitc.literal "1" : index loc(fused<{ctnative.source_name = "phantom_index"}>["source-names.js":24:1])
    %slot = emitc.subscript %array[%index] : (!emitc.array<2xi32>, index) -> !emitc.lvalue<i32> loc(fused<{ctnative.source_name = "phantom_slot"}>["source-names.js":25:1])
    %arrayRead = emitc.load %slot : <i32> loc(fused<{ctnative.source_name = "array_read"}>["source-names.js":26:1])
    %five = emitc.literal "5" : i32 loc(fused<{ctnative.source_name = "phantom_literal"}>["source-names.js":27:1])
    %expression = emitc.expression %fieldRead, %arrayRead, %five : (i32, i32, i32) -> i32 {
      %sum = emitc.add %fieldRead, %arrayRead : (i32, i32) -> i32 loc(fused<{ctnative.source_name = "phantom_inline"}>["source-names.js":28:1])
      %out = emitc.add %sum, %five : (i32, i32) -> i32
      emitc.yield %out : i32
    } loc(fused<{ctnative.source_name = "phantom_expression"}>["source-names.js":29:1])
    emitc.return %expression : i32
  }
  emitc.func @statement_only() -> i32 {
    %unused = emitc.call_opaque "touch"() {ctnative.statement} : () -> i32 loc(fused<{ctnative.source_name = "phantom_statement"}>["source-names.js":30:1])
    %zero = emitc.literal "0" : i32
    emitc.return %zero : i32
  }
}

//--- loops.mlir
#iterator = loc(fused<{ctnative.source_name = "i1"}>["source-names.js":40:1])
#score = loc(fused<{ctnative.source_name = "score"}>["source-names.js":41:1])
module attributes {ctnative.readable_names} {
  emitc.include <"cstddef">
  emitc.include <"cstdint">
  emitc.func @loop_names(%limit: index loc(#iterator)) -> i32 {
    %zero = emitc.literal "0" : index
    %step = emitc.literal "1" : index
    %one = emitc.literal "1" : i32
    %sum = "emitc.variable"() {value = 0 : i32} : () -> !emitc.lvalue<i32> loc(#score)
    "emitc.for"(%zero, %limit, %step) ({
    ^outer(%i: index loc(#iterator)):
      %before = emitc.load %sum : <i32>
      %index = emitc.cast %i : index to i32
      %after = emitc.add %before, %index : (i32, i32) -> i32
      emitc.assign %after : i32 to %sum : <i32>
      "emitc.for"(%zero, %step, %step) ({
      ^inner(%j: index loc(#iterator)):
        %beforeInner = emitc.load %sum : <i32>
        %afterInner = emitc.add %beforeInner, %one : (i32, i32) -> i32
        emitc.assign %afterInner : i32 to %sum : <i32>
        emitc.yield
      }) : (index, index, index) -> ()
      emitc.yield
    }) : (index, index, index) -> ()
    "emitc.for"(%zero, %limit, %step) ({
    ^sibling(%k: index loc(#iterator)):
      %beforeSibling = emitc.load %sum : <i32>
      %afterSibling = emitc.add %beforeSibling, %one : (i32, i32) -> i32
      emitc.assign %afterSibling : i32 to %sum : <i32>
      emitc.yield
    }) : (index, index, index) -> ()
    %loaded = emitc.load %sum : <i32>
    %bound = emitc.cast %limit : index to i32
    %result = emitc.add %loaded, %bound : (i32, i32) -> i32
    emitc.return %result : i32
  }
}

//--- standard-headers.mlir
module attributes {ctnative.readable_names} {
  emitc.include <"cstdint">
  emitc.include <"cstddef">
  emitc.include <"cstdlib">
  emitc.func @header_argument(%input: i32 loc(fused<{ctnative.source_name = "int32_t"}>["source-names.js":60:1])) -> i32 {
    %maximum = "emitc.constant"() {value = 7 : i32} : () -> i32 loc(fused<{ctnative.source_name = "INT32_MAX"}>["source-names.js":61:1])
    %success = "emitc.constant"() {value = 11 : i32} : () -> i32 loc(fused<{ctnative.source_name = "EXIT_SUCCESS"}>["source-names.js":62:1])
    %sum = emitc.add %input, %maximum : (i32, i32) -> i32
    %result = emitc.add %sum, %success : (i32, i32) -> i32
    emitc.return %result : i32
  }
  emitc.func @header_local() -> i32 {
    %named = "emitc.constant"() {value = 40 : i32} : () -> i32 loc(fused<{ctnative.source_name = "int32_t"}>["source-names.js":63:1])
    %after = "emitc.constant"() {value = 2 : i32} : () -> i32
    %result = emitc.add %named, %after : (i32, i32) -> i32
    emitc.return %result : i32
  }
  emitc.func @header_index(%input: index loc(fused<{ctnative.source_name = "size_t"}>["source-names.js":64:1])) -> i32 {
    %offset = "emitc.constant"() {value = 3 : index} : () -> index
    %sum = emitc.add %input, %offset : (index, index) -> index
    %result = emitc.cast %sum : index to i32
    emitc.return %result : i32
  }
}

//--- opaque-types.mlir
module attributes {ctnative.readable_names} {
  emitc.include <"cstdint">
  emitc.include "source-names-external.h"
  emitc.func @opaque_argument(%input: !emitc.opaque<"Counter"> loc(fused<{ctnative.source_name = "Counter"}>["source-names.js":70:1])) -> i32 {
    %copy = emitc.cast %input : !emitc.opaque<"Counter"> to !emitc.opaque<"Counter">
    %result = emitc.cast %copy : !emitc.opaque<"Counter"> to i32
    emitc.return %result : i32
  }
  emitc.func @opaque_local() -> i32 {
    %named = "emitc.constant"() {value = #emitc.opaque<"40">} : () -> !emitc.opaque<"Counter"> loc(fused<{ctnative.source_name = "Counter"}>["source-names.js":71:1])
    %later = "emitc.constant"() {value = #emitc.opaque<"2">} : () -> !emitc.opaque<"Counter">
    %left = emitc.cast %named : !emitc.opaque<"Counter"> to i32
    %right = emitc.cast %later : !emitc.opaque<"Counter"> to i32
    %result = emitc.add %left, %right : (i32, i32) -> i32
    emitc.return %result : i32
  }
}

//--- source-names-external.h
#pragma once
using Counter = int;

//--- isolation.mlir
#catalog = loc(fused<{ctnative.source_name = "catalog"}>["source-names.js":50:1])
module {
  emitc.func @ordinary_before(%a: i32 loc(#catalog)) -> i32 {
    emitc.return %a : i32
  }
  module @native attributes {ctnative.readable_names} {
    emitc.func @marked(%a: i32 loc(#catalog)) -> i32 {
      emitc.return %a : i32
    }
    module @ordinary {
      emitc.func @ordinary_nested(%a: i32 loc(#catalog)) -> i32 {
        emitc.return %a : i32
      }
    }
    emitc.func @marked_after(%a: i32 loc(#catalog)) -> i32 {
      emitc.return %a : i32
    }
    emitc.func @legacy_name_loc(%a: i32 loc("legacy")) -> i32 {
      emitc.return %a : i32
    }
  }
  module @wrong attributes {ctnative.readable_names = false} {
    emitc.func @wrong_marker(%a: i32 loc(#catalog)) -> i32 {
      emitc.return %a : i32
    }
  }
  emitc.func @ordinary_after(%a: i32 loc(#catalog)) -> i32 {
    emitc.return %a : i32
  }
}
