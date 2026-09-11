// RUN: ctjs-opt %s --ctnative-lower-to-emitc | FileCheck %s
// RUN: ctjs-opt %s --ctnative-lower-to-emitc --ctnative-lower-to-emitc | FileCheck %s
// An input presence annotation cannot turn an absent lookup into a Map value.
module {
  // CHECK: ctjs.func @untrusted_presence
  // CHECK-SAME: ctnative.not_native = "nested native Map get requires presence on every reaching path for the same instance and key; has observations must survive intervening effects"
  // CHECK-NOT: ctnative.map_present
  ctjs.func @untrusted_presence$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %ctor = ctjs.load_global "Map"
    %outer = ctjs.construct %ctor(%ctor)
    %inner = ctjs.construct %ctor(%ctor)
    %set = ctjs.constant #ctjs.string<"set">
    %get = ctjs.constant #ctjs.string<"get">
    %size = ctjs.constant #ctjs.string<"size">
    %one = ctjs.constant #ctjs.number<1>
    %two = ctjs.constant #ctjs.number<2>
    %setter = ctjs.get_property %outer[%set]
    %stored = ctjs.call %setter(%outer, %one, %inner)
    %getter = ctjs.get_property %outer[%get]
    %read = ctjs.call %getter(%outer, %two) {ctnative.map_present}
    %result = ctjs.get_property %read[%size]
    ctjs.return %result
  }
}
