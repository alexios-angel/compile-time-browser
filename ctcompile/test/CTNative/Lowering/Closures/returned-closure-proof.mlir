// RUN: ctjs-opt %s --split-input-file --ctnative-lower-to-emitc | FileCheck %s
// Public return boundaries and callers outside the function-value flow must
// not acquire a new signature. Input proof annotations cannot bypass checks.

module {
  ctjs.func @probe$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined
    %fn = ctjs.call_direct @open$2(%nil, %nil, %nil)
    %value = ctjs.call %fn(%nil)
    ctjs.return %value
  }
  // CHECK: ctjs.func @open$2
  // CHECK-SAME: ctnative.not_native = "a closure used as a value: returned closure return requires a closed function"
  // CHECK: ctjs.create_closure
  // CHECK-SAME: ctnative.closure_reason = "returned closure return requires a closed function"
  // CHECK-NOT: ctnative.environment
  ctjs.func @open$2(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined
    %fn = ctjs.create_closure %callee[3] this %nil {ctnative.environment = "reader$3"}
    ctjs.return %fn
  }
  ctjs.func @reader$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<1>
    ctjs.return %one
  }
}

// -----

module {
  ctjs.func @probe$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined
    %fn = ctjs.call_direct @make$2(%nil, %nil, %nil)
    %value = ctjs.call %fn(%nil)
    // This symbol use does not name the callable value the proof follows.
    %outside = ctjs.call_direct @reader$3(%nil, %nil, %nil)
    ctjs.return %value
  }
  // CHECK: ctjs.func private @make$2
  // CHECK-SAME: ctnative.not_native = "a closure used as a value: returned closure target has a direct caller outside the proved callable flow"
  ctjs.func private @make$2(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined
    %fn = ctjs.create_closure %callee[3] this %nil
    ctjs.return %fn
  }
  ctjs.func @reader$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<1>
    ctjs.return %one
  }
}
