// A flat array destructuring shares one helper load between both next calls.
// Removing the calls must collect that load only once before erasing it.
//
// RUN: ctjs-opt %s --ctnative-lower-to-emitc="optimize=false" | FileCheck %s --implicit-check-not=ctnative.not_native --implicit-check-not=__ctbrowser_iter --implicit-check-not=ctjs.func

// CHECK-LABEL: emitc.func @shared_
// CHECK: call_opaque "ctnative::vec_at"
// CHECK: call_opaque "ctnative::vec_at"
// CHECK: return

module attributes {ctjs.globals = []} {
  ctjs.func @shared$1(%receiver: !ctjs.value, %new_target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %undefined = ctjs.constant #ctjs.undefined
    %false = ctjs.constant #ctjs.boolean<false>
    %one = ctjs.constant #ctjs.string<"one">
    %two = ctjs.constant #ctjs.string<"two">
    %array = ctjs.create_array[]
    ctjs.append %one to %array
    ctjs.append %two to %array
    %open = ctjs.load_global "__ctbrowser_iter_open"
    %record = ctjs.call %open(%undefined, %array)
    %next = ctjs.load_global "__ctbrowser_iter_next"
    %first = ctjs.call %next(%undefined, %record)
    %second = ctjs.call %next(%undefined, %record)
    %close = ctjs.load_global "__ctbrowser_iter_close"
    %closed = ctjs.call %close(%undefined, %record, %false)
    %sum = ctjs.binary add %first, %second
    ctjs.return %sum
  }
}
