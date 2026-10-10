// The shape a compiled function has to reach, written by hand and compiled.
//
// A hand-written example of the C++ shape the EmitC backend
// (lib/CTJS/Lowering/CTJSToEmitC.cpp) must produce for a compiled function,
// translated and compiled against the real aot.hpp so every choice below is
// checked by a C++ compiler.
//
// The second RUN line matters most. Checking the emitted text proves only the
// weaker half: a backend can emit fluent C++ against a signature it invented,
// code that verifies and prints plausibly and is wrong. The host C++ compiler
// reads the real aot.hpp and settles that question, at no cost to the test
// run.
//
// What it pins:
//
//   The callee is qualified. aot.hpp declares the extern "C" prototypes
//   inside `namespace ctbrowser::aot`, so they have C linkage and unmangled
//   linker symbols but a qualified C++ name. An unqualified `ct_aot_enter(...)`
//   compiles only by ADL off a ct_aot_frame* argument, which helpers without a
//   frame parameter do not have; ct_aot_cell_get fails outright. The helper
//   table's `symbol` is the linker name, not the callee string.
//
//   No declarations are emitted. emitc.call_opaque resolves nothing and needs
//   no prototype, so the translation unit includes aot.hpp and calls what the
//   header declares. Emitting func.func private instead would re-declare the
//   helpers at global scope with C++ linkage (undefined symbols against
//   lib/Script/aot_bridge/), and emitc.declare_func drops parameter types in
//   this LLVM.
//
//   ct_aot_enter fails with a null pointer, not a status. Its row returns
//   `struct ct_aot_frame *` and "returns NULL on the 512-depth raise";
//   RuntimeHelpers.hpp carves it out of both failure tiers for this reason. A
//   status compare here would test a pointer against an enumerator, and the
//   513th frame would dereference null inside the next helper.
//
//   The status is spelled by name. aot.hpp says the statuses' precedence is
//   the contract and their numbers are not (`ok` is 3). emitc.literal carries
//   the static_cast, so no number is baked in and reordering the enum is safe.
//
//   The frame block is a local array passed by decay. ct_aot_enter's storage
//   is caller-allocated so that the layout stays the runtime's to change.
//   Passing the array directly gives `unsigned char*` -> `void*` and needs no
//   subscript, no address-of and no size_t, which matters because
//   !emitc.size_t emits a bare `size_t` that does not compile without
//   <stddef.h>.
//
//   --declare-variables-at-top is mandatory: EmitC refuses a multi-block
//   function without it, and every compiled body has at least two blocks
//   because of the NULL test above.

// RUN: mlir-translate %s --mlir-to-cpp --declare-variables-at-top | FileCheck %s
// RUN: mlir-translate %s --mlir-to-cpp --declare-variables-at-top > %t.cpp && %cxx %t.cpp

emitc.include <"ctbrowser/aot/aot.hpp">
emitc.include <"ctbrowser/aot/aot_entry.h">

// CHECK: extern "C" int32_t f(ctbrowser::aot::ct_aot_ctx* {{.*}}, const ctbrowser::aot::ct_aot_site*
emitc.func @f(%ctx: !emitc.ptr<!emitc.opaque<"ctbrowser::aot::ct_aot_ctx">>,
              %site: !emitc.ptr<!emitc.opaque<"const ctbrowser::aot::ct_aot_site">>,
              %argv: !emitc.ptr<!emitc.opaque<"const uint64_t">>,
              %argc: !emitc.opaque<"uint32_t">,
              %receiver: !emitc.opaque<"uint64_t">,
              %constructing: !emitc.opaque<"uint32_t">,
              %out: !emitc.ptr<!emitc.opaque<"uint64_t">>) -> i32
              attributes {specifiers = ["extern \22C\22"]} {

  // CHECK: unsigned char [[STORE:v[0-9]+]][64];
  %store = "emitc.variable"() <{value = #emitc.opaque<"">}>
      : () -> !emitc.array<64 x !emitc.opaque<"unsigned char">>

  %regs = "emitc.constant"() <{value = #emitc.opaque<"1">}> : () -> !emitc.opaque<"uint32_t">
  // CHECK: = ctbrowser::aot::ct_aot_enter({{.*}}, [[STORE]]);
  %fr = emitc.call_opaque "ctbrowser::aot::ct_aot_enter"(%ctx, %site, %regs, %receiver, %store)
      : (!emitc.ptr<!emitc.opaque<"ctbrowser::aot::ct_aot_ctx">>,
         !emitc.ptr<!emitc.opaque<"const ctbrowser::aot::ct_aot_site">>,
         !emitc.opaque<"uint32_t">, !emitc.opaque<"uint64_t">,
         !emitc.array<64 x !emitc.opaque<"unsigned char">>)
      -> !emitc.ptr<!emitc.opaque<"ctbrowser::aot::ct_aot_frame">>

  // A pointer test, not a status test.
  // CHECK: = {{.*}} != nullptr;
  %null = emitc.literal "nullptr" : !emitc.ptr<!emitc.opaque<"ctbrowser::aot::ct_aot_frame">>
  %entered = emitc.cmp ne, %fr, %null
      : (!emitc.ptr<!emitc.opaque<"ctbrowser::aot::ct_aot_frame">>,
         !emitc.ptr<!emitc.opaque<"ctbrowser::aot::ct_aot_frame">>) -> i1
  cf.cond_br %entered, ^body, ^depth_raise

^depth_raise:
  // The uncatchable failure tier. The frame was never entered, so there is
  // nothing to leave and no handler to reach.
  // CHECK: return static_cast<int32_t>(ctbrowser::aot::ct_aot_status::failed);
  %failed = emitc.literal "static_cast<int32_t>(ctbrowser::aot::ct_aot_status::failed)" : i32
  emitc.return %failed : i32

^body:
  // CHECK: ctbrowser::aot::ct_aot_leave(
  emitc.call_opaque "ctbrowser::aot::ct_aot_leave"(%fr)
      : (!emitc.ptr<!emitc.opaque<"ctbrowser::aot::ct_aot_frame">>) -> ()

  // ct_aot_return_value takes no frame handle, which is why the entry has to
  // deliver `receiver` and `constructing` by value: the body must still hold
  // them after ct_aot_leave has run.
  // CHECK: = ctbrowser::aot::ct_aot_return_value(
  %returned =
      emitc.call_opaque "ctbrowser::aot::ct_aot_return_value"(%receiver, %receiver, %constructing)
      : (!emitc.opaque<"uint64_t">, !emitc.opaque<"uint64_t">, !emitc.opaque<"uint32_t">)
      -> !emitc.opaque<"uint64_t">

  // `out[0] = ...`, not `*out = ...`: LLVM 23's emitter refuses an assignment
  // through an emitc.dereference result under --declare-variables-at-top and
  // accepts the same store through emitc.subscript. Same C++ for a pointer to
  // one slot; the index is a declared variable under this flag.
  // CHECK: {{v[0-9]+}}[{{v[0-9]+}}] = {{v[0-9]+}};
  %zero = "emitc.constant"() {value = 0 : i32} : () -> i32
  %slot = emitc.subscript %out[%zero]
      : (!emitc.ptr<!emitc.opaque<"uint64_t">>, i32) -> !emitc.lvalue<!emitc.opaque<"uint64_t">>
  emitc.assign %returned : !emitc.opaque<"uint64_t"> to %slot
      : !emitc.lvalue<!emitc.opaque<"uint64_t">>

  // CHECK: return static_cast<int32_t>(ctbrowser::aot::ct_aot_status::ok);
  %ok = emitc.literal "static_cast<int32_t>(ctbrowser::aot::ct_aot_status::ok)" : i32
  emitc.return %ok : i32
}
