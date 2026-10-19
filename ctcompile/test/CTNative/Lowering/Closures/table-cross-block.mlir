// Stores in separate scf.if arms are not one ordered constant table.
// RUN: ctjs-opt %s --ctjs-resolve-globals --ctnative-lower-to-emitc=optimize=false | FileCheck %s
// CHECK: ctjs.func private @keys$1
// CHECK-SAME: ctnative.not_native = "parameter 0 is !ctnative.boxed - no caller proves it
// CHECK: ctjs.func @entry$0
// CHECK-SAME: ctnative.not_native =
// CHECK: scf.if
// CHECK-NEXT: ctjs.set_property
// CHECK-NEXT: } else {
// CHECK-NEXT: ctjs.set_property

module {
  ctjs.func private @keys$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %table: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %Object = ctjs.load_global "Object"
    %key = ctjs.constant #ctjs.string<"keys">
    %method = ctjs.get_property %Object[%key]
    %keys = ctjs.call %method(%Object, %table)
    ctjs.return %keys
  }
  ctjs.func @entry$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %flag: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %undefined = ctjs.constant #ctjs.undefined
    %table = ctjs.create_object
    %a = ctjs.constant #ctjs.string<"a">
    %b = ctjs.constant #ctjs.string<"b">
    %one = ctjs.constant #ctjs.number<1>
    %test = ctjs.truthy %flag
    scf.if %test {
      ctjs.set_property %table[%a], %one {ctjs.define_own}
    } else {
      ctjs.set_property %table[%b], %one {ctjs.define_own}
    }
    %keys = ctjs.call_direct @keys$1(%undefined, %undefined, %undefined, %table)
    ctjs.return %keys
  }
}
