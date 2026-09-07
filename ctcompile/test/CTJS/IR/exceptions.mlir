// Source exception regions round-trip before any native transformation.
// Native lowering then consumes RegionBranch inference: both calls reach the
// same private function, so both payload/state alternatives remain live. The
// state joins an integer and a fractional number; the catch adds that state
// to its numeric payload and returns the inferred numeric result.
// RUN: split-file %s %t
// RUN: ctjs-opt %t/joined.mlir | ctjs-opt | FileCheck %s --check-prefix=ROUNDTRIP
// RUN: ctjs-opt %t/joined.mlir --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=TYPES --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.try
// RUN: not ctjs-opt %t/empty-block.mlir 2>&1 | FileCheck %s --check-prefix=EMPTY
// RUN: not ctjs-opt %t/state-count.mlir 2>&1 | FileCheck %s --check-prefix=BAD-STATE
// RUN: not ctjs-opt %t/catch-type.mlir 2>&1 | FileCheck %s --check-prefix=CATCH-TYPE
// RUN: not ctjs-opt %t/body-argument.mlir 2>&1 | FileCheck %s --check-prefix=ARGUMENTS
// RUN: not ctjs-opt %t/missing-payload.mlir 2>&1 | FileCheck %s --check-prefix=ARGUMENTS
// RUN: not ctjs-opt %t/wrong-terminator.mlir 2>&1 | FileCheck %s --check-prefix=TERMINATOR
// RUN: not ctjs-opt %t/misplaced-exit.mlir 2>&1 | FileCheck %s --check-prefix=EXIT
// RUN: not ctjs-opt %t/misplaced-yield.mlir 2>&1 | FileCheck %s --check-prefix=YIELD

// ROUNDTRIP-LABEL: ctjs.func private @joined$1(
// ROUNDTRIP: [[RESULT:%[a-zA-Z0-9_]+]] = ctjs.try {
// ROUNDTRIP: [[VALUES:%[a-zA-Z0-9_]+]]:2 = scf.if
// ROUNDTRIP: } else {
// ROUNDTRIP: ctjs.try_exit {{%[a-zA-Z0-9_]+}} normal {{%[a-zA-Z0-9_]+}} caught([[VALUES]]#0, [[VALUES]]#1)
// ROUNDTRIP: } catch {
// ROUNDTRIP: ^bb0([[PAYLOAD:%[a-zA-Z0-9_]+]]: !ctjs.value, [[STATE:%[a-zA-Z0-9_]+]]: !ctjs.value):
// ROUNDTRIP: [[SUM:%[a-zA-Z0-9_]+]] = ctjs.binary add [[PAYLOAD]], [[STATE]]
// ROUNDTRIP: ctjs.try_yield [[SUM]]
// ROUNDTRIP: } : !ctjs.value
// ROUNDTRIP: ctjs.return [[RESULT]]

// TYPES-LABEL: emitc.func @joined_1(
// TYPES-SAME: : i1) -> f64
// TYPES: [[RESULT_SLOT:%[a-zA-Z0-9_]+]] = "emitc.variable"() {{.*}} : () -> !emitc.lvalue<f64>
// TYPES: [[STATE_SLOT:%[a-zA-Z0-9_]+]] = "emitc.variable"() {{.*}} : () -> !emitc.lvalue<f64>
// TYPES: ctnative.cpp_try {
// TYPES: [[VALUES:%[a-zA-Z0-9_]+]]:2 = scf.if {{.*}} -> (f64, f64) {
// TYPES: scf.yield {{.*}} : f64, f64
// TYPES: } else {
// TYPES: scf.yield {{.*}} : f64, f64
// TYPES: assign [[VALUES]]#1 : f64 to [[STATE_SLOT]] : <f64>
// TYPES: ctnative.cpp_throw [[VALUES]]#0 : f64
// TYPES: } catch {
// TYPES: ^bb0([[PAYLOAD:%[a-zA-Z0-9_]+]]: f64):
// TYPES: [[STATE:%[a-zA-Z0-9_]+]] = emitc.load [[STATE_SLOT]] : <f64>
// TYPES: [[SUM:%[a-zA-Z0-9_]+]] = emitc.add [[PAYLOAD]], [[STATE]] : (f64, f64) -> f64
// TYPES: emitc.assign [[SUM]] : f64 to [[RESULT_SLOT]] : <f64>

// EMPTY: error: 'ctjs.try' op requires a nonempty block in each try and catch region
// BAD-STATE: error: 'ctjs.try_exit' op must carry the payload and every catch state value
// CATCH-TYPE: error: 'ctjs.try' op catch arguments must be JavaScript values
// ARGUMENTS: error: 'ctjs.try' op requires an argument-free body and a catch payload argument
// TERMINATOR: error: 'ctjs.try' op requires try_exit in the body and try_yield in the catch
// EXIT: error: 'ctjs.try_exit' op must terminate a try body
// YIELD: error: 'ctjs.try_yield' op must terminate a catch body

//--- joined.mlir
module {
  ctjs.func @_script_$0(%receiver: !ctjs.value, %new_target: !ctjs.value,
                       %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined
    %yes = ctjs.constant #ctjs.boolean<true>
    %no = ctjs.constant #ctjs.boolean<false>
    %left = ctjs.call_direct @joined$1(%nil, %nil, %nil, %yes)
    %right = ctjs.call_direct @joined$1(%nil, %nil, %nil, %no)
    ctjs.store_global "left", %left
    ctjs.store_global "right", %right
    ctjs.return %nil
  }
  ctjs.func private @joined$1(%receiver: !ctjs.value, %new_target: !ctjs.value,
                             %callee: !ctjs.value, %condition: !ctjs.value)
      -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %condition_bit = ctjs.truthy %condition
    %result = ctjs.try {
      %values:2 = scf.if %condition_bit -> (!ctjs.value, !ctjs.value) {
        %payload = ctjs.constant #ctjs.number<4629700416936869888>
        %state = ctjs.constant #ctjs.number<4621819117588971520>
        scf.yield %payload, %state : !ctjs.value, !ctjs.value
      } else {
        %payload = ctjs.constant #ctjs.number<4619567317775286272>
        %state = ctjs.constant #ctjs.number<4636772475726725120>
        scf.yield %payload, %state : !ctjs.value, !ctjs.value
      }
      %throws = arith.constant true
      %no_normal_result = ub.poison : !ctjs.value
      ctjs.try_exit %throws normal %no_normal_result caught(%values#0, %values#1)
    } catch {
    ^bb0(%payload: !ctjs.value, %state: !ctjs.value):
      %sum = ctjs.binary add %payload, %state
      ctjs.try_yield %sum
    } : !ctjs.value
    ctjs.return %result
  }
}

//--- empty-block.mlir
ctjs.func @empty_block() -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  %result = "ctjs.try"() ({
  ^bb0:
  }, {
  ^bb0(%payload: !ctjs.value):
    "ctjs.try_yield"(%payload) : (!ctjs.value) -> ()
  }) : () -> !ctjs.value
  ctjs.return %result
}

//--- state-count.mlir
ctjs.func @state_count() -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  %result = ctjs.try {
    %throws = arith.constant true
    %value = ctjs.constant #ctjs.number<0>
    ctjs.try_exit %throws normal %value caught(%value)
  } catch {
  ^bb0(%payload: !ctjs.value, %state: !ctjs.value):
    ctjs.try_yield %state
  } : !ctjs.value
  ctjs.return %result
}

//--- catch-type.mlir
ctjs.func @catch_type() -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  %value = ctjs.constant #ctjs.number<0>
  %result = ctjs.try {
    %throws = arith.constant true
    ctjs.try_exit %throws normal %value caught(%value)
  } catch {
  ^bb0(%payload: f64):
    ctjs.try_yield %value
  } : !ctjs.value
  ctjs.return %result
}

//--- body-argument.mlir
ctjs.func @body_argument() -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  %result = ctjs.try {
  ^bb0(%value: !ctjs.value):
    %throws = arith.constant true
    ctjs.try_exit %throws normal %value caught(%value)
  } catch {
  ^bb0(%payload: !ctjs.value):
    ctjs.try_yield %payload
  } : !ctjs.value
  ctjs.return %result
}

//--- missing-payload.mlir
ctjs.func @missing_payload() -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  %value = ctjs.constant #ctjs.number<0>
  %result = ctjs.try {
    %throws = arith.constant true
    ctjs.try_exit %throws normal %value caught()
  } catch {
    ctjs.try_yield %value
  } : !ctjs.value
  ctjs.return %result
}

//--- wrong-terminator.mlir
ctjs.func @wrong_terminator() -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  %value = ctjs.constant #ctjs.number<0>
  %result = ctjs.try {
    ctjs.return %value
  } catch {
  ^bb0(%payload: !ctjs.value):
    ctjs.try_yield %payload
  } : !ctjs.value
  ctjs.return %result
}

//--- misplaced-exit.mlir
ctjs.func @misplaced_exit() -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  %value = ctjs.constant #ctjs.number<0>
  %throws = arith.constant true
  ctjs.try_exit %throws normal %value caught(%value)
}

//--- misplaced-yield.mlir
ctjs.func @misplaced_yield() -> !ctjs.value attributes {upvalue_count = 0 : i32} {
  %value = ctjs.constant #ctjs.number<0>
  ctjs.try_yield %value
}
