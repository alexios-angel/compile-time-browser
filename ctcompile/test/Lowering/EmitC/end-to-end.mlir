// JAVASCRIPT IN, A COMPILED TRANSLATION UNIT OUT. THE WHOLE PIPELINE, CONNECTED.
//
// Four stages run here and the last one is a C++ compiler: source is parsed and
// compiled to bytecode by ctjs-translate, imported to CTJS, lowered to EmitC by
// ctjs-opt, turned into C++ by mlir-translate, and then COMPILED against the
// real aot.hpp. Nothing in this file is a mock.
//
// THIS IS THE SMALLEST PROGRAM THAT PROVES THE PIPELINE EXISTS. It is not a
// demonstration of what the backend can do - the backend can barely do
// anything, and refuses almost every function it is handed. It is the
// difference between a compiler that is connected end to end and one that is a
// collection of stages that have each been tested alone. Every operation added
// after this is an increment on something that demonstrably works.
//
// THE SECOND RUN LINE IS WHERE THE VALUE IS. A backend can emit fluent C++
// against a signature it invented; the host compiler reads the header the
// runtime actually publishes and refuses it. That check found three real
// defects while this was written: an unqualified callee (the extern "C"
// prototypes live inside `namespace ctbrowser::aot`, so unqualified calls
// resolve only by ADL and the frameless helpers do not resolve at all), a
// `const uint64_t` local that --declare-variables-at-top declares and then
// assigns to, and a `$` in the symbol name that only GCC and Clang accept.

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-lower-to-emitc \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top \
// RUN:   | FileCheck %s

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-lower-to-emitc \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top > %t.cpp \
// RUN:   && %cxx %t.cpp

function f(a) { return a; }

// THE ENTRY, SHAPED EXACTLY LIKE ct_aot_entry_fn - which is the point, because
// this function has to be assignable to that pointer type. The `$` the importer
// puts in every symbol is spelled `_` here; it is not in C++'s basic character
// set, and the suffix still separates.
// CHECK: extern "C" int32_t f_1(
// CHECK-SAME: ctbrowser::aot::ct_aot_ctx*
// CHECK-SAME: const ctbrowser::aot::ct_aot_site*
// CHECK-SAME: const uint64_t*
// CHECK-SAME: uint64_t*

// CT_AOT_FRAME_BYTES OF CALLER-ALLOCATED SPACE. Checked first because
// --declare-variables-at-top hoists every declaration above every assignment,
// and FileCheck matches in order.
// CHECK: unsigned char [[STORE:v[0-9]+]][64];

// THE PARAMETER IS READ BEFORE THE FRAME EXISTS. argv is an interior pointer
// into context::registers_ and ct_aot_enter resizes that vector, so a body that
// read it afterwards would be reading through a pointer that may have been
// reallocated. There is no way to recover it: ct_aot_slots returns the compiled
// frame's own span, not the caller's argument window.
// CHECK: [[ARGV:v[0-9]+]] = (uint64_t*)
// CHECK: [[ARG0:v[0-9]+]] = [[ARGV]][0];

// AND THE REGISTER WINDOW IS THE FUNCTION'S OWN frame_size - not its parameter
// count, which would leave every local without a slot.
// CHECK: [[FRAME:v[0-9]+]] = ctbrowser::aot::ct_aot_enter({{.*}}, [[STORE]]);

// A POINTER TEST, NEVER A STATUS COMPARE: ct_aot_enter reports the 512-depth
// raise by returning NULL, which is neither of the ABI's two failure tiers.
// CHECK: {{v[0-9]+}} = [[FRAME]] != nullptr;
// CHECK: return static_cast<int32_t>(ctbrowser::aot::ct_aot_status::failed);

// AND THE RETURN PROTOCOL. ct_aot_return_value takes no frame handle, which is
// exactly why the entry delivers `receiver` and `constructing` by value: the
// body still needs them after ct_aot_leave has run. The three-argument form is
// not optional - one compiled body serves both `f()` and `new f()`.
// CHECK: ctbrowser::aot::ct_aot_leave([[FRAME]]);
// CHECK: [[RESULT:v[0-9]+]] = ctbrowser::aot::ct_aot_return_value([[ARG0]],
// CHECK: {{v[0-9]+}}[{{v[0-9]+}}] = [[RESULT]];
// CHECK: return static_cast<int32_t>(ctbrowser::aot::ct_aot_status::ok);
