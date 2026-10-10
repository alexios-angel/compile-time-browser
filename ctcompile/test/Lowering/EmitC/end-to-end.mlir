// JavaScript in, a compiled translation unit out: the whole pipeline.
//
// Four stages run here and the last one is a C++ compiler: source is parsed and
// compiled to bytecode by ctjs-translate, imported to CTJS, lowered to EmitC by
// ctjs-opt, turned into C++ by mlir-translate, and then compiled against the
// real aot.hpp. Nothing in this file is a mock.
//
// This is the smallest program that exercises the whole boxed pipeline from
// JavaScript to a compiled translation unit. It is not a demonstration of what
// the backend can do; it checks that the stages are connected end to end,
// rather than each having been tested alone. More capable lowerings are
// covered by the other files in this directory.
//
// The second RUN line matters most. A backend can emit fluent C++ against a
// signature it invented; the host compiler reads the header the runtime
// actually publishes and refuses it. That check has caught: an unqualified
// callee (the extern "C" prototypes live inside `namespace ctbrowser::aot`, so
// unqualified calls resolve only by ADL and the frameless helpers do not
// resolve at all), a `const uint64_t` local that --declare-variables-at-top
// declares and then assigns to, and a `$` in the symbol name that only GCC and
// Clang accept. Each of those produced C++ that read correctly but was not
// portable, valid C++ against the real header.

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-lower-to-emitc \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top \
// RUN:   | FileCheck %s

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-lower-to-emitc \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top > %t.cpp \
// RUN:   && %cxx %t.cpp

function f(a) { return a; }

// The entry is shaped exactly like ct_aot_entry_fn, because this function has
// to be assignable to that pointer type. The `$` the importer puts in every
// symbol is spelled `_` here: it is not in C++'s basic character set, and the
// suffix still separates.
// CHECK: extern "C" int32_t f_1(
// CHECK-SAME: ctbrowser::aot::ct_aot_ctx*
// CHECK-SAME: const ctbrowser::aot::ct_aot_site*
// CHECK-SAME: const uint64_t*
// CHECK-SAME: uint64_t*

// CT_AOT_FRAME_BYTES (64) of caller-allocated space. Checked first because
// --declare-variables-at-top hoists every declaration above every assignment,
// and FileCheck matches in order.
// CHECK: unsigned char [[STORE:v[0-9]+]][64];

// The parameter is read before the frame exists. argv is an interior pointer
// into context::registers_ and ct_aot_enter resizes that vector, so a body that
// read it afterwards would be reading through a pointer that may have been
// reallocated. There is no way to recover it: ct_aot_slots returns the compiled
// frame's own span, not the caller's argument window.
// CHECK: [[ARGV:v[0-9]+]] = (uint64_t*)
// CHECK: [[ARG0:v[0-9]+]] = [[ARGV]][0];

// The register window is the function's own frame_size, not its parameter
// count, which would leave every local without a slot.
// CHECK: [[FRAME:v[0-9]+]] = ctbrowser::aot::ct_aot_enter({{.*}}, [[STORE]]);

// A pointer test, never a status compare: ct_aot_enter reports exceeding the
// 512-frame depth limit by returning NULL, which is neither ABI failure tier.
// CHECK: {{v[0-9]+}} = [[FRAME]] != nullptr;
// CHECK: return static_cast<int32_t>(ctbrowser::aot::ct_aot_status::failed);

// Normalize the constructor result before the frame exit observes it, and
// keep it in flight until the caller receives it.
// CHECK: [[RESULT:v[0-9]+]] = ctbrowser::aot::ct_aot_return_value([[ARG0]],
// CHECK: ctbrowser::aot::ct_aot_leave_return([[FRAME]], [[RESULT]]);
// CHECK: {{v[0-9]+}}[{{v[0-9]+}}] = [[RESULT]];
// CHECK: return static_cast<int32_t>(ctbrowser::aot::ct_aot_status::ok);
