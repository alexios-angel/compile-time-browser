// RUN: ctjs-opt %s --split-input-file --ctnative-lower-to-emitc | FileCheck %s
// Structural boundaries that ordinary method-call syntax cannot express.

module {
  // CHECK: ctjs.func @wrong_receiver
  // CHECK-SAME: ctnative.not_native = "native Map method is detached, escapes, or has a different receiver"
  ctjs.func @wrong_receiver$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %ctor = ctjs.load_global "Map"
    %left = ctjs.construct %ctor(%ctor)
    %right = ctjs.constant #ctjs.undefined
    %has = ctjs.constant #ctjs.string<"has">
    %key = ctjs.constant #ctjs.number<0>
    %method = ctjs.get_property %left[%has]
    %result = ctjs.call %method(%right, %key)
    ctjs.return %result
  }
}

// -----

module {
  // CHECK: ctjs.func @wrong_target
  // CHECK-SAME: ctnative.not_native = "native Map requires its standard constructor as new.target"
  ctjs.func @wrong_target$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %ctor = ctjs.load_global "Map"
    %nil = ctjs.constant #ctjs.undefined
    %map = ctjs.construct %ctor(%nil)
    %size = ctjs.constant #ctjs.string<"size">
    %result = ctjs.get_property %map[%size]
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
    %nil = ctjs.constant #ctjs.undefined
    %called = ctjs.call %nil(%nil)
    %size = ctjs.constant #ctjs.string<"size">
    %result = ctjs.get_property %map[%size]
    ctjs.return %result
  }
}

// -----

module {
  // CHECK: ctjs.func @unknown_constructor
  // CHECK-SAME: ctnative.not_native = "standard Map identity is unproved across an unknown constructor"
  ctjs.func @unknown_constructor$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %ctor = ctjs.load_global "Map"
    %map = ctjs.construct %ctor(%ctor)
    %nil = ctjs.constant #ctjs.undefined
    %made = ctjs.construct %nil(%nil)
    %size = ctjs.constant #ctjs.string<"size">
    %result = ctjs.get_property %map[%size]
    ctjs.return %result
  }
}

// -----

module {
  // CHECK: ctjs.func @untrusted_annotations
  // CHECK-SAME: ctnative.not_native = "`ctjs.construct` is not native yet"
  // CHECK-NOT: ctnative.map_site
  // CHECK-NOT: ctnative.map_constructor
  ctjs.func @untrusted_annotations$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined {ctnative.map_constructor}
    %made = ctjs.construct %nil(%nil) {ctnative.map_site}
    ctjs.return %made
  }
}
