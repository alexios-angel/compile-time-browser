// Lowering of global reads and writes, and the string escape that would be a
// real bug if it were a hex one.
//
// An assignment can fail on an uninitialized lexical binding, so its raw
// store is followed by a status check. An unresolvable read throws ReferenceError
// (ECMA-262 GetValue), so it is a status test with an out-slot. A `typeof` of
// a source identifier uses ct_aot_global_get_soft, which yields undefined
// instead. Both go through context::global_or_named, the code
// VM_CASE(get_global) runs, so the boxed backend and the interpreter cannot
// drift.
//
// Every ABI helper called here must have a body in lib/Script/aot_bridge/:
// aot.hpp declares every row, so a call to a missing one fails only at link.

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
  // Only source IdentifierReference provenance permits a silent absent read.
  %e = ctjs.load_global "exports" {typeof_lookup = true}
  %t = ctjs.unary typeof %e
  // The same sole-TypeOf use after an ordinary GetValue remains throwing.
  %saved = ctjs.load_global "saved"
  %saved_type = ctjs.unary typeof %saved
  // An explicitly ordinary lookup has the same hard behavior.
  %ordinary = ctjs.load_global "ordinary" {typeof_lookup = false}
  %ordinary_type = ctjs.unary typeof %ordinary
  // Its users do not change a source-soft lookup's semantics either.
  %unused = ctjs.load_global "unused" {typeof_lookup = true}
  // A name that is not an identifier, which is legal JavaScript:
  // `globalThis["odFd"] = 1`. The importer carries whatever the source
  // said, so the backend cannot assume otherwise.
  ctjs.store_global "od\01Fd", %v
  ctjs.store_global "initialized", %v {ctjs.initialize}
  ctjs.frame_exit %ctx
  ctjs.return %g
}

// An ordinary name keeps the plain form, with its length beside it. The length
// is emitted rather than left to strlen because the name is bytes: a global
// whose name contains a zero byte is legal and strlen would stop at it.
// CHECK: ctbrowser::aot::ct_aot_global_get({{v[0-9]+}}, "Math", 4, {{v[0-9]+}});
// CHECK: ctbrowser::aot::ct_aot_global_get_soft({{v[0-9]+}}, "exports", 7);
// CHECK-NEXT: {{v[0-9]+}} = ctbrowser::aot::ct_aot_check({{v[0-9]+}});
// CHECK: ctbrowser::aot::ct_aot_global_get({{v[0-9]+}}, "saved", 5, {{v[0-9]+}});
// CHECK: ctbrowser::aot::ct_aot_global_get({{v[0-9]+}}, "ordinary", 8, {{v[0-9]+}});
// CHECK: ctbrowser::aot::ct_aot_global_get_soft({{v[0-9]+}}, "unused", 6);
// CHECK-NEXT: {{v[0-9]+}} = ctbrowser::aot::ct_aot_check({{v[0-9]+}});

// The name that would break under a hex escape: `od`, byte 0x01, `Fd`, five
// characters. Written "od\x01Fd", the C++ compiler reads the escape as \x01F
// followed by `d`, four characters against a length of five, and the emitted
// program would look up a global nobody named. An octal escape is at most three
// digits, so "od\001Fd" cannot run on.
//
// The hazard is real: "od\x01Fd" in C++ source is in fact a hex escape out of
// range and does not compile at all.
// does not compile at all.
// CHECK: ctbrowser::aot::ct_aot_global_set({{v[0-9]+}}, "od\001Fd", 5, {{v[0-9]+}});
// CHECK-NEXT: {{v[0-9]+}} = ctbrowser::aot::ct_aot_check({{v[0-9]+}});

// No raw string is used here, by design. `R"(Math)"` says nothing `"Math"`
// does not, with five more characters, and a raw string cannot carry the 0x01
// above at all, since its content is the literal bytes of this generated file.
// The raw form is used only where escaping is the noisy spelling: a name
// containing a quote or a backslash. Both paths are checked by static_asserts
// beside c_string_literal.
// CHECK: ctbrowser::aot::ct_aot_global_init({{v[0-9]+}}, "initialized", 11, {{v[0-9]+}});
// CHECK-NOT: ct_aot_check
// CHECK-NOT: R"(
