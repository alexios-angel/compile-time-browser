// RUN: ctjs-opt %s --ctnative-lower-to-emitc | FileCheck %s
// RUN: ctjs-opt %s --ctnative-lower-to-emitc --ctnative-lower-to-emitc | FileCheck %s
// A forged annotation cannot make a has snapshot survive deletion.
module {
  // CHECK: ctjs.func @stale_guard
  // CHECK-SAME: ctnative.not_native = "nested native Map get requires presence on every reaching path for the same instance and key; has observations must survive intervening effects"
  // CHECK-NOT: ctnative.map_present
  ctjs.func @stale_guard$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %ctor = ctjs.load_global "Map"
    %outer = ctjs.construct %ctor(%ctor)
    %inner = ctjs.construct %ctor(%ctor)
    %set = ctjs.constant #ctjs.string<"set">
    %get = ctjs.constant #ctjs.string<"get">
    %has = ctjs.constant #ctjs.string<"has">
    %delete = ctjs.constant #ctjs.string<"delete">
    %size = ctjs.constant #ctjs.string<"size">
    %one = ctjs.constant #ctjs.number<1>
    %zero = ctjs.constant #ctjs.number<0>
    %setter = ctjs.get_property %outer[%set]
    %stored = ctjs.call %setter(%outer, %one, %inner)
    %hasMethod = ctjs.get_property %outer[%has]
    %had = ctjs.call %hasMethod(%outer, %one)
    %deleteMethod = ctjs.get_property %outer[%delete]
    %deleted = ctjs.call %deleteMethod(%outer, %one)
    %condition = ctjs.truthy %had
    %result = scf.if %condition -> (!ctjs.value) {
      %getter = ctjs.get_property %outer[%get]
      %child = ctjs.call %getter(%outer, %one) {ctnative.map_present}
      %count = ctjs.get_property %child[%size]
      scf.yield %count : !ctjs.value
    } else {
      scf.yield %zero : !ctjs.value
    }
    ctjs.return %result
  }
}
