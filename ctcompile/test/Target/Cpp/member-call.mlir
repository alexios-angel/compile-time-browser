// RUN: ctjs-translate --mlir-to-cpp %s | FileCheck %s
// RUN: python3 %S/member-call.py --translate ctjs-translate --source %s --work %t.run

// CHECK-LABEL: bool present(
// CHECK: .has_value()
// CHECK-LABEL: std::unique_ptr<int> take(
// CHECK: (std::move({{[^)]+}})).value()
// CHECK-LABEL: int32_t ordered(
// CHECK: .mix<int32_t, 2>({{[^,]+}}, 7, {{[^)]+}})
// CHECK: .touch()
// CHECK-LABEL: int32_t pointer_call(
// CHECK: ->bump(
// CHECK-LABEL: int32_t selected(
// CHECK: ({{[^?]+}} ? {{[^:]+}} : {{[^)]+}}).bump(

module attributes {ctnative.const_bindings} {
  emitc.func @present(%value: !emitc.opaque<"std::optional<std::unique_ptr<int>>&">) -> i1 {
    %has = emitc.member_call_opaque %value "has_value"() : !emitc.opaque<"std::optional<std::unique_ptr<int>>&">, () -> i1
    emitc.return %has : i1
  }
  emitc.func @take(%value: !emitc.opaque<"std::optional<std::unique_ptr<int>>&">) -> !emitc.opaque<"std::unique_ptr<int>"> {
    %owned = emitc.expression %value : (!emitc.opaque<"std::optional<std::unique_ptr<int>>&">) -> !emitc.opaque<"std::unique_ptr<int>"> {
      %moved = emitc.call_opaque "std::move"(%value) : (!emitc.opaque<"std::optional<std::unique_ptr<int>>&">) -> !emitc.opaque<"std::optional<std::unique_ptr<int>>&&">
      %result = emitc.member_call_opaque %moved "value"() : !emitc.opaque<"std::optional<std::unique_ptr<int>>&&">, () -> !emitc.opaque<"std::unique_ptr<int>">
      emitc.yield %result : !emitc.opaque<"std::unique_ptr<int>">
    }
    emitc.return %owned : !emitc.opaque<"std::unique_ptr<int>">
  }
  emitc.func @ordered(%object: !emitc.opaque<"Probe&">, %a: i32, %b: i32) -> i32 {
    %result = emitc.member_call_opaque %object "mix"(%a, %b) <{args = [1 : index, 7 : i32, 0 : index], template_args = [i32, 2 : i32]}> : !emitc.opaque<"Probe&">, (i32, i32) -> i32
    emitc.member_call_opaque %object "touch"(%a) <{args = []}> : !emitc.opaque<"Probe&">, (i32) -> ()
    emitc.return %result : i32
  }
  emitc.func @pointer_call(%object: !emitc.ptr<!emitc.opaque<"Probe">>, %a: i32) -> i32 {
    %result = emitc.member_call_opaque %object "bump"(%a) : !emitc.ptr<!emitc.opaque<"Probe">>, (i32) -> i32
    emitc.return %result : i32
  }
  emitc.func @selected(%which: i1, %left: !emitc.opaque<"Probe&">, %right: !emitc.opaque<"Probe&">, %a: i32) -> i32 {
    %result = emitc.expression %which, %left, %right, %a : (i1, !emitc.opaque<"Probe&">, !emitc.opaque<"Probe&">, i32) -> i32 {
      %object = emitc.conditional %which, %left, %right : !emitc.opaque<"Probe&">
      %value = emitc.member_call_opaque %object "bump"(%a) : !emitc.opaque<"Probe&">, (i32) -> i32
      emitc.yield %value : i32
    }
    emitc.return %result : i32
  }
}
