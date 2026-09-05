// RUN: ctjs-opt %s --split-input-file --ctnative-lower-to-emitc | FileCheck %s
// RUN: ctjs-opt %s --split-input-file --ctnative-lower-to-emitc --ctnative-lower-to-emitc | FileCheck %s
// Forged table/callable annotations cannot open a public return boundary.
module {
  ctjs.func @probe$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined
    %table = ctjs.call_direct @open$2(%nil, %nil, %nil)
    %key = ctjs.constant #ctjs.string<"get">
    %method = ctjs.get_property %table[%key] {ctnative.method_table = "forged", ctnative.table_field = "get", ctnative.environment = "reader$3"}
    %value = ctjs.call %method(%table)
    ctjs.return %value
  }
  // CHECK: ctjs.func @open$2
  // CHECK-SAME: ctnative.not_native = "a closure used as a value: returned method table return requires a closed function"
  // CHECK-NOT: ctnative.method_table
  // CHECK-NOT: ctnative.stored_callable
  // CHECK-NOT: ctnative.environment
  ctjs.func @open$2(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined
    %table = ctjs.create_object {ctnative.method_table = "forged"}
    %fn = ctjs.create_closure %callee[3] this %nil {ctnative.environment = "reader$3", ctnative.stored_callable}
    %key = ctjs.constant #ctjs.string<"get">
    ctjs.set_property %table[%key], %fn {ctnative.method_table = "forged", ctnative.table_field = "get"}
    ctjs.return %table
  }
  ctjs.func @reader$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<1>
    ctjs.return %one
  }
}

// -----

// A direct caller outside the field flow prevents changing the target ABI.
module {
  ctjs.func @probe$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined
    %table = ctjs.call_direct @make$2(%nil, %nil, %nil)
    %key = ctjs.constant #ctjs.string<"get">
    %method = ctjs.get_property %table[%key]
    %value = ctjs.call %method(%table)
    %outside = ctjs.call_direct @reader$3(%nil, %nil, %nil)
    ctjs.return %value
  }
  // CHECK: ctjs.func private @make$2
  // CHECK-SAME: ctnative.not_native = "a closure used as a value: returned method table method target has a caller outside the proved table flow"
  ctjs.func private @make$2(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined
    %table = ctjs.create_object
    %fn = ctjs.create_closure %callee[3] this %nil
    %key = ctjs.constant #ctjs.string<"get">
    ctjs.set_property %table[%key], %fn
    ctjs.return %table
  }
  ctjs.func @reader$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<1>
    ctjs.return %one
  }
}
