// RUN: ctjs-opt %s --split-input-file --ctnative-lower-to-emitc | FileCheck %s
// RUN: ctjs-opt %s --split-input-file --ctnative-lower-to-emitc --ctnative-lower-to-emitc | FileCheck %s

module {
  // CHECK: ctjs.func @wrong_receiver
  // CHECK-SAME: ctnative.not_native = "standard Array.from requires its exact Array receiver"
  ctjs.func @wrong_receiver$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %ctor = ctjs.load_global "Map"
    %map = ctjs.construct %ctor(%ctor)
    %keys = ctjs.constant #ctjs.string<"keys">
    %keys_method = ctjs.get_property %map[%keys]
    %snapshot = ctjs.call %keys_method(%map)
    %array = ctjs.load_global "Array"
    %from = ctjs.constant #ctjs.string<"from">
    %copy_method = ctjs.get_property %array[%from]
    %nil = ctjs.constant #ctjs.undefined
    %copy = ctjs.call %copy_method(%nil, %snapshot) {ctnative.map_snapshot_copy}
    %length = ctjs.constant #ctjs.string<"length">
    %result = ctjs.get_property %copy[%length]
    ctjs.return %result
  }
}

// -----

module {
  // CHECK: ctjs.func @unknown_call
  // CHECK-SAME: ctnative.not_native = "standard Map identity is unproved across an unknown call"
  ctjs.func @unknown_call$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %ctor = ctjs.load_global "Map"
    %map = ctjs.construct %ctor(%ctor)
    %keys = ctjs.constant #ctjs.string<"keys">
    %keys_method = ctjs.get_property %map[%keys]
    %snapshot = ctjs.call %keys_method(%map)
    %array = ctjs.load_global "Array"
    %from = ctjs.constant #ctjs.string<"from">
    %copy_method = ctjs.get_property %array[%from]
    %copy = ctjs.call %copy_method(%array, %snapshot)
    %nil = ctjs.constant #ctjs.undefined
    %unknown = ctjs.call %nil(%nil)
    %length = ctjs.constant #ctjs.string<"length">
    %result = ctjs.get_property %copy[%length]
    ctjs.return %result
  }
}

// -----

module {
  // CHECK: ctjs.func @forged_copy
  // CHECK-SAME: ctnative.not_native = "`ctjs.call` is not native yet"
  // CHECK-NOT: ctnative.map_snapshot_copy
  // CHECK-NOT: ctnative.map_snapshot_builtin
  ctjs.func @forged_copy$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined {ctnative.map_snapshot_builtin}
    %copy = ctjs.call %nil(%nil, %nil) {ctnative.map_snapshot_copy}
    ctjs.return %copy
  }
}
