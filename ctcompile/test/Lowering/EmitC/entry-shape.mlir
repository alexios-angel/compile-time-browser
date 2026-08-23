// THE SHAPE A COMPILED FUNCTION HAS TO REACH, WRITTEN BY HAND AND COMPILED.
//
// This is the target of the EmitC backend, not its output - no pass produces it
// yet. It is here because every decision in it was contested and several were
// wrong on the first attempt, and a target nobody has compiled is a guess.
//
// THE SECOND RUN LINE IS THE POINT. Checking the emitted text proves the weaker
// half: a backend can emit fluent C++ against a signature it invented, which is
// this project's recurring failure - "code that verifies, prints plausibly and
// is wrong". The host C++ compiler reads the REAL aot.hpp and settles it, and
// it costs nothing.
//
// WHAT IT PINS, each of which was got wrong before it was got right:
//
//   THE CALLEE IS QUALIFIED. aot.hpp:58 puts the extern "C" prototypes INSIDE
//   `namespace ctbrowser::aot`, so they have C linkage and the unmangled
//   linker symbol - but their C++ NAME is qualified. `ct_aot_enter(...)`
//   unqualified compiles only by ADL off a ct_aot_frame* argument, which the
//   eleven frameless rows do not have; ct_aot_cell_get fails outright. The
//   helper table's `symbol` is the LINKER name and is not the callee string.
//
//   NO DECLARATIONS ARE EMITTED. emitc.call_opaque resolves nothing and needs
//   no prototype, so the translation unit includes aot.hpp and calls what the
//   header declares. Emitting func.func private instead would re-declare the
//   helpers at global scope with C++ linkage - undefined symbols against
//   aot_bridge.cpp - and emitc.declare_func drops parameter types outright in
//   this LLVM.
//
//   ct_aot_enter FAILS WITH A NULL POINTER, NOT A STATUS. Its row returns
//   `struct ct_aot_frame *` and "returns NULL on the 512-depth raise";
//   RuntimeHelpers.hpp carves it out of both failure tiers for this reason. A
//   status compare here would test a pointer against an enumerator, and the
//   513th frame would dereference null inside the next helper.
//
//   THE STATUS IS SPELLED BY NAME. aot.hpp: "THE PRECEDENCE IS THE CONTRACT;
//   THE NUMBERS ARE NOT" - nothing may depend on `ok` being any particular
//   value, and it is 3. emitc.literal carries the static_cast so no number is
//   baked and a reordering of the enum moves this with it.
//
//   THE FRAME BLOCK IS A LOCAL ARRAY PASSED BY DECAY. ct_aot_enter's storage
//   is CALLER-allocated precisely so the layout stays the runtime's to change;
//   passing the array directly gives `unsigned char*` -> `void*` and needs no
//   subscript, no address-of and no size_t - which mattered, because
//   !emitc.size_t emits a bare `size_t` that does not compile without
//   <stddef.h>.
//
//   --declare-variables-at-top IS MANDATORY, not a preference: EmitC refuses a
//   multi-block function without it, and every compiled body has at least two
//   blocks because of the NULL test above.

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

  // A POINTER TEST, NOT A STATUS TEST.
  // CHECK: = {{.*}} != nullptr;
  %null = emitc.literal "nullptr" : !emitc.ptr<!emitc.opaque<"ctbrowser::aot::ct_aot_frame">>
  %entered = emitc.cmp ne, %fr, %null
      : (!emitc.ptr<!emitc.opaque<"ctbrowser::aot::ct_aot_frame">>,
         !emitc.ptr<!emitc.opaque<"ctbrowser::aot::ct_aot_frame">>) -> i1
  cf.cond_br %entered, ^body, ^depth_raise

^depth_raise:
  // THE UNCATCHABLE TIER. The frame was never entered, so there is nothing to
  // leave and no handler to reach.
  // CHECK: return static_cast<int32_t>(ctbrowser::aot::ct_aot_status::failed);
  %failed = emitc.literal "static_cast<int32_t>(ctbrowser::aot::ct_aot_status::failed)" : i32
  emitc.return %failed : i32

^body:
  // CHECK: ctbrowser::aot::ct_aot_leave(
  emitc.call_opaque "ctbrowser::aot::ct_aot_leave"(%fr)
      : (!emitc.ptr<!emitc.opaque<"ctbrowser::aot::ct_aot_frame">>) -> ()

  // ct_aot_return_value TAKES NO FRAME HANDLE, which is why the entry has to
  // deliver `receiver` and `constructing` by value: the body must still hold
  // them after ct_aot_leave has run.
  // CHECK: = ctbrowser::aot::ct_aot_return_value(
  %returned =
      emitc.call_opaque "ctbrowser::aot::ct_aot_return_value"(%receiver, %receiver, %constructing)
      : (!emitc.opaque<"uint64_t">, !emitc.opaque<"uint64_t">, !emitc.opaque<"uint32_t">)
      -> !emitc.opaque<"uint64_t">

  // CHECK: *{{v[0-9]+}} = {{v[0-9]+}};
  %slot = emitc.dereference %out : !emitc.ptr<!emitc.opaque<"uint64_t">>
  emitc.assign %returned : !emitc.opaque<"uint64_t"> to %slot
      : !emitc.lvalue<!emitc.opaque<"uint64_t">>

  // CHECK: return static_cast<int32_t>(ctbrowser::aot::ct_aot_status::ok);
  %ok = emitc.literal "static_cast<int32_t>(ctbrowser::aot::ct_aot_status::ok)" : i32
  emitc.return %ok : i32
}
