// The call's result flows only to normal completion. The exceptional edge
// carries the explicit pre-call register snapshot plus an implicit payload.
// This IR prerequisite does not enable source recovery or native lowering.
// RUN: split-file %s %t
// RUN: ctjs-opt %t/roundtrip.mlir | ctjs-opt | FileCheck %s --check-prefix=ROUNDTRIP
// RUN: not ctjs-opt %t/body-work.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-0
// RUN: not ctjs-opt %t/result-state.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-1
// RUN: not ctjs-opt %t/wrong-result.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-2
// RUN: not ctjs-opt %t/normal-argument.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-3
// RUN: not ctjs-opt %t/body-argument.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-4
// RUN: not ctjs-opt %t/payload-type.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-5
// RUN: not ctjs-opt %t/state-count.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-6
// RUN: not ctjs-opt %t/yield-count.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-7
// RUN: not ctjs-opt %t/wrong-yield.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-8
// RUN: not ctjs-opt %t/misplaced-exit.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-9
// RUN: not ctjs-opt %t/misplaced-yield.mlir 2>&1 | FileCheck %s --check-prefix=INVALID-10

// ROUNDTRIP-LABEL: ctjs.func @caller(
// ROUNDTRIP: [[BEFORE:%[a-zA-Z0-9_]+]] = ctjs.constant #ctjs.number<4621819117588971520>
// ROUNDTRIP: [[SAVED:%[a-zA-Z0-9_]+]] = ctjs.constant #ctjs.string<"saved">
// ROUNDTRIP: [[RESULT:%[a-zA-Z0-9_]+]]:2 = ctjs.invoke {
// ROUNDTRIP: [[CALL:%[a-zA-Z0-9_]+]] = ctjs.call_direct @leaf(
// ROUNDTRIP: ctjs.invoke_exit [[CALL]] state([[BEFORE]], [[SAVED]])
// ROUNDTRIP: } normal {
// ROUNDTRIP: ^bb0([[RETURNED:%[a-zA-Z0-9_]+]]: !ctjs.value):
// ROUNDTRIP: ctjs.invoke_yield([[RETURNED]], [[SAVED]])
// ROUNDTRIP: } unwind {
// ROUNDTRIP: ^bb0([[PAYLOAD:%[a-zA-Z0-9_]+]]: !ctjs.value, [[OLD:%[a-zA-Z0-9_]+]]: !ctjs.value, [[COPY:%[a-zA-Z0-9_]+]]: !ctjs.value):
// ROUNDTRIP: ctjs.invoke_yield([[OLD]], [[COPY]])
// ROUNDTRIP: } : !ctjs.value, !ctjs.value
// ROUNDTRIP: ctjs.return [[RESULT]]#0
// ROUNDTRIP-LABEL: ctjs.func @effect_only(
// ROUNDTRIP: ctjs.invoke {
// ROUNDTRIP: ctjs.invoke_exit {{%[a-zA-Z0-9_]+}} state()
// ROUNDTRIP: } normal {
// ROUNDTRIP: ctjs.invoke_yield()
// ROUNDTRIP: } unwind {
// ROUNDTRIP: ctjs.invoke_yield()
// ROUNDTRIP-NEXT: }
// ROUNDTRIP-NEXT: ctjs.return

// INVALID-0: error: {{.*}}requires exactly one call_direct followed by invoke_exit
// INVALID-1: error: {{.*}}call result must be used only by normal completion dispatch
// INVALID-2: error: {{.*}}must dispatch the immediately preceding call_direct result
// INVALID-3: error: {{.*}}requires an argument-free call body, one normal result argument and an unwind payload argument
// INVALID-4: error: {{.*}}requires an argument-free call body, one normal result argument and an unwind payload argument
// INVALID-5: error: {{.*}}continuation arguments must be JavaScript values
// INVALID-6: error: {{.*}}must carry every unwind state value after its implicit payload
// INVALID-7: error: {{.*}}must supply every invocation result
// INVALID-8: error: {{.*}}requires invoke_yield in both continuations
// INVALID-9: error: {{.*}}must terminate an invocation call body
// INVALID-10: error: {{.*}}must terminate a normal or unwind invocation continuation

//--- roundtrip.mlir
module {
  ctjs.func private @leaf(%receiver: !ctjs.value, %new_target: !ctjs.value,
                          %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.throw %receiver
  }
  ctjs.func @caller(%receiver: !ctjs.value, %new_target: !ctjs.value,
                    %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %before = ctjs.constant #ctjs.number<4621819117588971520>
    %saved = ctjs.constant #ctjs.string<"saved">
    %result:2 = ctjs.invoke {
      %called = ctjs.call_direct @leaf(%receiver, %new_target, %callee)
      ctjs.invoke_exit %called state(%before, %saved)
    } normal {
    ^bb0(%returned: !ctjs.value):
      ctjs.invoke_yield(%returned, %saved)
    } unwind {
    ^bb0(%payload: !ctjs.value, %old: !ctjs.value, %saved_copy: !ctjs.value):
      ctjs.invoke_yield(%old, %saved_copy)
    } : !ctjs.value, !ctjs.value
    ctjs.return %result#0
  }
  ctjs.func @effect_only(%receiver: !ctjs.value, %new_target: !ctjs.value,
                         %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.invoke {
      %called = ctjs.call_direct @leaf(%receiver, %new_target, %callee)
      ctjs.invoke_exit %called state()
    } normal {
    ^bb0(%returned: !ctjs.value):
      ctjs.invoke_yield()
    } unwind {
    ^bb0(%payload: !ctjs.value):
      ctjs.invoke_yield()
    }
    ctjs.return %receiver
  }
}

//--- body-work.mlir
module {
  ctjs.func private @leaf(%receiver: !ctjs.value, %new_target: !ctjs.value,
                          %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.throw %receiver
  }
  ctjs.func @caller(%receiver: !ctjs.value, %new_target: !ctjs.value,
                    %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %before = ctjs.constant #ctjs.number<4621819117588971520>
    %saved = ctjs.constant #ctjs.string<"saved">
    %result:2 = ctjs.invoke {
      %extra = ctjs.constant #ctjs.undefined
      %called = ctjs.call_direct @leaf(%receiver, %new_target, %callee)
      ctjs.invoke_exit %called state(%before, %saved)
    } normal {
    ^bb0(%returned: !ctjs.value):
      ctjs.invoke_yield(%returned, %saved)
    } unwind {
    ^bb0(%payload: !ctjs.value, %old: !ctjs.value, %saved_copy: !ctjs.value):
      ctjs.invoke_yield(%old, %saved_copy)
    } : !ctjs.value, !ctjs.value
    ctjs.return %result#0
  }
}

//--- result-state.mlir
module {
  ctjs.func private @leaf(%receiver: !ctjs.value, %new_target: !ctjs.value,
                          %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.throw %receiver
  }
  ctjs.func @caller(%receiver: !ctjs.value, %new_target: !ctjs.value,
                    %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %before = ctjs.constant #ctjs.number<4621819117588971520>
    %saved = ctjs.constant #ctjs.string<"saved">
    %result:2 = ctjs.invoke {
      %called = ctjs.call_direct @leaf(%receiver, %new_target, %callee)
      ctjs.invoke_exit %called state(%called, %saved)
    } normal {
    ^bb0(%returned: !ctjs.value):
      ctjs.invoke_yield(%returned, %saved)
    } unwind {
    ^bb0(%payload: !ctjs.value, %old: !ctjs.value, %saved_copy: !ctjs.value):
      ctjs.invoke_yield(%old, %saved_copy)
    } : !ctjs.value, !ctjs.value
    ctjs.return %result#0
  }
}

//--- wrong-result.mlir
module {
  ctjs.func private @leaf(%receiver: !ctjs.value, %new_target: !ctjs.value,
                          %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.throw %receiver
  }
  ctjs.func @caller(%receiver: !ctjs.value, %new_target: !ctjs.value,
                    %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %before = ctjs.constant #ctjs.number<4621819117588971520>
    %saved = ctjs.constant #ctjs.string<"saved">
    %result:2 = ctjs.invoke {
      %called = ctjs.call_direct @leaf(%receiver, %new_target, %callee)
      ctjs.invoke_exit %before state(%before, %saved)
    } normal {
    ^bb0(%returned: !ctjs.value):
      ctjs.invoke_yield(%returned, %saved)
    } unwind {
    ^bb0(%payload: !ctjs.value, %old: !ctjs.value, %saved_copy: !ctjs.value):
      ctjs.invoke_yield(%old, %saved_copy)
    } : !ctjs.value, !ctjs.value
    ctjs.return %result#0
  }
}

//--- normal-argument.mlir
module {
  ctjs.func private @leaf(%receiver: !ctjs.value, %new_target: !ctjs.value,
                          %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.throw %receiver
  }
  ctjs.func @caller(%receiver: !ctjs.value, %new_target: !ctjs.value,
                    %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %before = ctjs.constant #ctjs.number<4621819117588971520>
    %saved = ctjs.constant #ctjs.string<"saved">
    %result:2 = ctjs.invoke {
      %called = ctjs.call_direct @leaf(%receiver, %new_target, %callee)
      ctjs.invoke_exit %called state(%before, %saved)
    } normal {
    ^bb0:
      ctjs.invoke_yield(%before, %saved)
    } unwind {
    ^bb0(%payload: !ctjs.value, %old: !ctjs.value, %saved_copy: !ctjs.value):
      ctjs.invoke_yield(%old, %saved_copy)
    } : !ctjs.value, !ctjs.value
    ctjs.return %result#0
  }
}

//--- body-argument.mlir
module {
  ctjs.func private @leaf(%receiver: !ctjs.value, %new_target: !ctjs.value,
                          %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.throw %receiver
  }
  ctjs.func @caller(%receiver: !ctjs.value, %new_target: !ctjs.value,
                    %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %before = ctjs.constant #ctjs.number<4621819117588971520>
    %saved = ctjs.constant #ctjs.string<"saved">
    %result:2 = ctjs.invoke {
    ^bb0(%unexpected: !ctjs.value):
      %called = ctjs.call_direct @leaf(%receiver, %new_target, %callee)
      ctjs.invoke_exit %called state(%before, %saved)
    } normal {
    ^bb0(%returned: !ctjs.value):
      ctjs.invoke_yield(%returned, %saved)
    } unwind {
    ^bb0(%payload: !ctjs.value, %old: !ctjs.value, %saved_copy: !ctjs.value):
      ctjs.invoke_yield(%old, %saved_copy)
    } : !ctjs.value, !ctjs.value
    ctjs.return %result#0
  }
}

//--- payload-type.mlir
module {
  ctjs.func private @leaf(%receiver: !ctjs.value, %new_target: !ctjs.value,
                          %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.throw %receiver
  }
  ctjs.func @caller(%receiver: !ctjs.value, %new_target: !ctjs.value,
                    %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %before = ctjs.constant #ctjs.number<4621819117588971520>
    %saved = ctjs.constant #ctjs.string<"saved">
    %result:2 = ctjs.invoke {
      %called = ctjs.call_direct @leaf(%receiver, %new_target, %callee)
      ctjs.invoke_exit %called state(%before, %saved)
    } normal {
    ^bb0(%returned: !ctjs.value):
      ctjs.invoke_yield(%returned, %saved)
    } unwind {
    ^bb0(%payload: f64, %old: !ctjs.value, %saved_copy: !ctjs.value):
      ctjs.invoke_yield(%old, %saved_copy)
    } : !ctjs.value, !ctjs.value
    ctjs.return %result#0
  }
}

//--- state-count.mlir
module {
  ctjs.func private @leaf(%receiver: !ctjs.value, %new_target: !ctjs.value,
                          %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.throw %receiver
  }
  ctjs.func @caller(%receiver: !ctjs.value, %new_target: !ctjs.value,
                    %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %before = ctjs.constant #ctjs.number<4621819117588971520>
    %saved = ctjs.constant #ctjs.string<"saved">
    %result:2 = ctjs.invoke {
      %called = ctjs.call_direct @leaf(%receiver, %new_target, %callee)
      ctjs.invoke_exit %called state(%before)
    } normal {
    ^bb0(%returned: !ctjs.value):
      ctjs.invoke_yield(%returned, %saved)
    } unwind {
    ^bb0(%payload: !ctjs.value, %old: !ctjs.value, %saved_copy: !ctjs.value):
      ctjs.invoke_yield(%old, %saved_copy)
    } : !ctjs.value, !ctjs.value
    ctjs.return %result#0
  }
}

//--- yield-count.mlir
module {
  ctjs.func private @leaf(%receiver: !ctjs.value, %new_target: !ctjs.value,
                          %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.throw %receiver
  }
  ctjs.func @caller(%receiver: !ctjs.value, %new_target: !ctjs.value,
                    %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %before = ctjs.constant #ctjs.number<4621819117588971520>
    %saved = ctjs.constant #ctjs.string<"saved">
    %result:2 = ctjs.invoke {
      %called = ctjs.call_direct @leaf(%receiver, %new_target, %callee)
      ctjs.invoke_exit %called state(%before, %saved)
    } normal {
    ^bb0(%returned: !ctjs.value):
      ctjs.invoke_yield(%returned)
    } unwind {
    ^bb0(%payload: !ctjs.value, %old: !ctjs.value, %saved_copy: !ctjs.value):
      ctjs.invoke_yield(%old, %saved_copy)
    } : !ctjs.value, !ctjs.value
    ctjs.return %result#0
  }
}

//--- wrong-yield.mlir
module {
  ctjs.func private @leaf(%receiver: !ctjs.value, %new_target: !ctjs.value,
                          %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.throw %receiver
  }
  ctjs.func @caller(%receiver: !ctjs.value, %new_target: !ctjs.value,
                    %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %before = ctjs.constant #ctjs.number<4621819117588971520>
    %saved = ctjs.constant #ctjs.string<"saved">
    %result:2 = ctjs.invoke {
      %called = ctjs.call_direct @leaf(%receiver, %new_target, %callee)
      ctjs.invoke_exit %called state(%before, %saved)
    } normal {
    ^bb0(%returned: !ctjs.value):
      ctjs.try_yield %returned
    } unwind {
    ^bb0(%payload: !ctjs.value, %old: !ctjs.value, %saved_copy: !ctjs.value):
      ctjs.invoke_yield(%old, %saved_copy)
    } : !ctjs.value, !ctjs.value
    ctjs.return %result#0
  }
}

//--- misplaced-exit.mlir
module {
  ctjs.func private @leaf(%receiver: !ctjs.value, %new_target: !ctjs.value,
                          %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.throw %receiver
  }
  ctjs.func @misplaced() -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %value = ctjs.constant #ctjs.undefined
    ctjs.invoke_exit %value state()
  }
}

//--- misplaced-yield.mlir
module {
  ctjs.func private @leaf(%receiver: !ctjs.value, %new_target: !ctjs.value,
                          %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.throw %receiver
  }
  ctjs.func @misplaced() -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %value = ctjs.constant #ctjs.undefined
    ctjs.invoke_yield(%value)
  }
}
