// Native binary64 NaNs use the explicit Number token; ordinary EmitC and f32
// keep their existing representation. Deduction must retain binary64 storage.
// RUN: split-file %s %t
// RUN: ctjs-translate --mlir-to-cpp %t/native.mlir > %t/native.cpp
// RUN: FileCheck %s --check-prefix=NATIVE < %t/native.cpp
// RUN: %gxx -O2 %t/native.cpp -o %t/gcc && %t/gcc
// RUN: %clangxx -O2 %t/native.cpp -o %t/clang && %t/clang
// RUN: ctjs-translate --mlir-to-cpp %t/ordinary.mlir | FileCheck %s --check-prefix=ORDINARY
// RUN: ctjs-translate --mlir-to-cpp %t/wrong-marker.mlir | FileCheck %s --check-prefix=ORDINARY

// NATIVE: js_num native_nan = ctnative::js_num{ctnative::js_nan_t{}}.value();
// NATIVE: float single_nan = NAN;
// NATIVE: ctnative::js_num{ctnative::js_nan_t{}}.value()
// NATIVE: auto
// NATIVE-SAME: ctnative::js_num{ctnative::js_nan_t{}}.value()
// ORDINARY-NOT: ctnative::
// ORDINARY: double plain_nan = NAN;
// ORDINARY-NOT: ctnative::

//--- native.mlir
module attributes {ctnative.numeric_alias, ctnative.readable_literals} {
  emitc.include "ctcompile/CTNative/Runtime/ctnative.hpp"
  emitc.verbatim "#define CTCOMPILE_PIN(name, site, ...) static_assert(std::is_same_v<decltype(name), __VA_ARGS__>, site)"
  emitc.global @native_nan : f64 = 0x7FF8000000000000
  emitc.global @single_nan : f32 = 0x7FC00000
  emitc.global @pair : !emitc.array<2xf64> = dense<[0x7FF8000000000000, -0.0]>
  emitc.func @deduced_nan() -> f64 {
    %nan = "emitc.constant"() {value = 0x7FF8000000000000 : f64, ctnative.deduced} : () -> f64
    emitc.return %nan : f64
  }
  emitc.verbatim "int main() { return std::isnan(native_nan) && std::isnan(single_nan) && std::isnan(pair[0]) && std::signbit(pair[1]) && std::isnan(deduced_nan()) ? 0 : 1; }"
}

//--- ordinary.mlir
module {
  emitc.global @plain_nan : f64 = 0x7FF8000000000000
}

//--- wrong-marker.mlir
module attributes {ctnative.numeric_alias = false} {
  emitc.global @plain_nan : f64 = 0x7FF8000000000000
}
