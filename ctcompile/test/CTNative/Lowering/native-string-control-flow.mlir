// CFG structuring can use one poison for dead slots with different carriers.
// Verification must succeed before canonicalization can remove those slots.
// In particular, an empty string in one slot must not retype a numeric use of
// the same poison. While backedges target before-region arguments, whose
// types may differ from the results at the same index.
//
// RUN: ctjs-opt %s --ctnative-lower-to-emitc | FileCheck %s --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func --implicit-check-not=ub.poison
// RUN: ctjs-opt %s "--pass-pipeline=builtin.module(ctnative-lower-to-emitc, emitc.func(canonicalize, convert-scf-to-emitc, convert-arith-to-emitc, canonicalize, ctnative-prune-dead-stores, canonicalize))" | ctjs-translate --mlir-to-cpp > %t.cpp
// RUN: %cxx -Wall -Wextra -Werror -Wconversion %t.cpp

// CHECK-LABEL: emitc.func @if_poison_0() -> !emitc.opaque<"std::string">
ctjs.func @if_poison$0(%receiver: !ctjs.value, %new_target: !ctjs.value,
                       %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %condition = ctjs.truthy %one
  %poison = ub.poison : !ctjs.value
  %pair:2 = scf.if %condition -> (!ctjs.value, !ctjs.value) {
    %string = ctjs.constant #ctjs.string<"kept">
    scf.yield %string, %one : !ctjs.value, !ctjs.value
  } else {
    scf.yield %poison, %poison : !ctjs.value, !ctjs.value
  }
  ctjs.return %pair#0
}

// CHECK-LABEL: emitc.func @for_initial_1() -> !emitc.opaque<"std::string">
ctjs.func @for_initial$1(%receiver: !ctjs.value, %new_target: !ctjs.value,
                         %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %zero = arith.constant 0 : index
  %one = arith.constant 1 : index
  %enabled = ctjs.constant #ctjs.number<4607182418800017408>
  %condition = ctjs.truthy %enabled
  %limit = arith.select %condition, %one, %zero : index
  %poison = ub.poison : !ctjs.value
  %result = scf.for %i = %zero to %limit step %one iter_args(%value = %poison) -> (!ctjs.value) {
    %string = ctjs.constant #ctjs.string<"iteration">
    scf.yield %string : !ctjs.value
  }
  ctjs.return %result
}

// CHECK-LABEL: emitc.func @for_backedge_2() -> !emitc.opaque<"std::string">
ctjs.func @for_backedge$2(%receiver: !ctjs.value, %new_target: !ctjs.value,
                          %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %zero = arith.constant 0 : index
  %one = arith.constant 1 : index
  %disabled = ctjs.constant #ctjs.number<0>
  %condition = ctjs.truthy %disabled
  %limit = arith.select %condition, %one, %zero : index
  %string = ctjs.constant #ctjs.string<"zero iterations">
  %poison = ub.poison : !ctjs.value
  %result = scf.for %i = %zero to %limit step %one iter_args(%value = %string) -> (!ctjs.value) {
    scf.yield %poison : !ctjs.value
  }
  ctjs.return %result
}

// CHECK-LABEL: emitc.func @while_backedge_3() -> !emitc.opaque<"std::string">
ctjs.func @while_backedge$3(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %zero = ctjs.constant #ctjs.number<0>
  %string = ctjs.constant #ctjs.string<"before">
  %poison = ub.poison : !ctjs.value
  %result:2 = scf.while (%s = %string, %n = %zero) : (!ctjs.value, !ctjs.value) -> (!ctjs.value, !ctjs.value) {
    %condition = ctjs.truthy %n
    scf.condition(%condition) %n, %s : !ctjs.value, !ctjs.value
  } do {
  ^bb0(%n: !ctjs.value, %s: !ctjs.value):
    scf.yield %poison, %n : !ctjs.value, !ctjs.value
  }
  ctjs.return %result#1
}

// Use the SAME SSA string as a field key and returned data. Two source-level
// literals could import as separate constants and miss the erasure defect.
// CHECK-LABEL: emitc.func @key_data_5() -> !emitc.opaque<"std::string">
ctjs.func @key_data$5(%receiver: !ctjs.value, %new_target: !ctjs.value,
                      %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %key = ctjs.constant #ctjs.string<"tag">
  %number = ctjs.constant #ctjs.number<4631107791820423168>
  %object = "ctjs.create_object"() : () -> !ctjs.value
  "ctjs.set_property"(%object, %key, %number) : (!ctjs.value, !ctjs.value, !ctjs.value) -> ()
  %read = "ctjs.get_property"(%object, %key) : (!ctjs.value, !ctjs.value) -> !ctjs.value
  %equal = ctjs.compare strict_eq %read, %number
  %condition = ctjs.truthy %equal
  %result = scf.if %condition -> (!ctjs.value) {
    scf.yield %key : !ctjs.value
  } else {
    %wrong = ctjs.constant #ctjs.string<"wrong">
    scf.yield %wrong : !ctjs.value
  }
  ctjs.return %result
}

// The full pipeline's compile-clean check must remove unused truthiness.
// CHECK-LABEL: emitc.func @discarded_truth_6() -> !emitc.opaque<"std::string">
ctjs.func @discarded_truth$6(%receiver: !ctjs.value, %new_target: !ctjs.value,
                             %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %string = ctjs.constant #ctjs.string<"retained">
  %ignored = ctjs.truthy %string
  ctjs.return %string
}

// CHECK-LABEL: emitc.func @while_initial_4() -> !emitc.opaque<"std::string">
ctjs.func @while_initial$4(%receiver: !ctjs.value, %new_target: !ctjs.value,
                           %callee: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %zero = ctjs.constant #ctjs.number<0>
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %poison = ub.poison : !ctjs.value
  %string = ctjs.constant #ctjs.string<"after">
  %result:2 = scf.while (%s = %poison, %n = %one) : (!ctjs.value, !ctjs.value) -> (!ctjs.value, !ctjs.value) {
    %condition = ctjs.truthy %n
    scf.condition(%condition) %n, %string : !ctjs.value, !ctjs.value
  } do {
  ^bb0(%n: !ctjs.value, %s: !ctjs.value):
    scf.yield %string, %zero : !ctjs.value, !ctjs.value
  }
  ctjs.return %result#1
}
