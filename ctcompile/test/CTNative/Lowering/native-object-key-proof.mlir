// RUN: ctjs-opt %s --ctnative-lower-to-emitc | FileCheck %s
// RUN: ctjs-opt %s --ctnative-lower-to-emitc --ctnative-lower-to-emitc | FileCheck %s
// Input identity tags cannot erase properties.
module {
  // CHECK: ctjs.func @open_key
  // CHECK-SAME: ctnative.not_native =
  // CHECK-NOT: ctnative.object_identity
  // CHECK: identity-only Map key has an unsupported use through `ctjs.set_property`
  // CHECK-NOT: ctnative.object_identity
  ctjs.func @open_key$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %ctor = ctjs.load_global "Map"
    %map = ctjs.construct %ctor(%ctor)
    %key = ctjs.create_object {ctnative.object_identity}
    %field = ctjs.constant #ctjs.string<"x">
    %one = ctjs.constant #ctjs.number<1>
    ctjs.set_property %key[%field], %one
    %set = ctjs.constant #ctjs.string<"set">
    %method = ctjs.get_property %map[%set]
    %stored = ctjs.call %method(%map, %key, %one)
    %size = ctjs.constant #ctjs.string<"size">
    %result = ctjs.get_property %map[%size]
    ctjs.return %result
  }
}
