// GLOBALS, AND THE ESCAPE THAT IS A REAL BUG IF IT IS A HEX ONE.
//
// This file was a REFUSAL test one commit ago, and the reason is worth keeping:
// aot.hpp declares all 69 ABI rows and lib/Script/aot_bridge/ defined 32 of them, so
// emitting a call to ct_aot_global_get COMPILED PERFECTLY and failed at link.
// It now has a body, so the lowering is enabled and this asserts what it emits.
//
// The write is infallible - (0, 0, 0) - so it is a single call with no
// status, no edge and no safepoint. The READ has an edge since 2026-09-12:
// an unresolvable name throws ReferenceError (6.2.5.5 GetValue), so the row
// is may_throw and the call is a status test with an out-slot. Both go
// through context::global_or_named, which is what VM_CASE(get_global) runs,
// so the two tiers cannot drift.

// RUN: ctjs-opt %s --ctjs-lower-to-emitc --emitc-eliminate-block-arguments \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top \
// RUN:   | FileCheck %s

// RUN: ctjs-opt %s --ctjs-lower-to-emitc --emitc-eliminate-block-arguments \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top > %t.cpp \
// RUN:   && %cxx %t.cpp

ctjs.func @globals(%receiver: !ctjs.value, %new_target: !ctjs.value,
                   %callee: !ctjs.value, %v: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %ctx = ctjs.frame_enter 1
  %g = ctjs.load_global "Math"
  // `typeof exports` - a load_global whose ONLY use is typeof - is the one
  // read that must not throw for an unbound name, so it takes the soft row.
  %e = ctjs.load_global "exports"
  %t = ctjs.unary typeof %e
  // A NAME THAT IS NOT AN IDENTIFIER, which is legal JavaScript:
  // `globalThis["odFd"] = 1`. The importer carries whatever the source
  // said, so the backend cannot assume otherwise.
  ctjs.store_global "od\01Fd", %v
  ctjs.frame_exit %ctx
  ctjs.return %g
}

// AN ORDINARY NAME KEEPS THE PLAIN FORM, with its length beside it. The length
// is emitted rather than left to strlen because the name is BYTES: a global
// whose name contains a zero byte is legal and strlen would stop at it.
// CHECK: ctbrowser::aot::ct_aot_global_get({{v[0-9]+}}, "Math", 4, {{v[0-9]+}});
// CHECK: ctbrowser::aot::ct_aot_global_get_soft({{v[0-9]+}}, "exports", 7);

// AND THE ONE THAT WOULD BREAK UNDER A HEX ESCAPE. `od`, byte 0x01, `Fd` -
// five characters. Written "od\x01Fd" the C++ compiler reads the escape as
// \x01F followed by `d`, which is four characters against a length of five, and
// the emitted program would look up a global nobody named. An octal escape is
// exactly three digits and cannot run on.
//
// The hazard is real enough to have bitten the static_assert written to
// demonstrate it: "od\x01Fd" in C++ source is a hex escape out of range and
// does not compile at all.
// CHECK: ctbrowser::aot::ct_aot_global_set({{v[0-9]+}}, "od\001Fd", 5, {{v[0-9]+}});

// THE RAW-STRING FORM IS NOT USED HERE and that is the rule working, not an
// omission. `R"(Math)"` says nothing `"Math"` does not, with five more
// characters, and a raw string cannot carry the 0x01 above at all - its content
// is the literal bytes of this generated file. It is taken only where escaping
// is the noisy spelling: a name containing a quote or a backslash. Both paths
// are checked by static_asserts beside c_string_literal.
// CHECK-NOT: R"(
