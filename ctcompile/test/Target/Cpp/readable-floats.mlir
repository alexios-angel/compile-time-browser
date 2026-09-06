// RUN: split-file %s %t
// RUN: ctjs-translate --mlir-to-cpp %t/native.mlir | FileCheck %s --check-prefix=NATIVE
// RUN: ctjs-translate --mlir-to-cpp %t/unmarked.mlir | FileCheck %s --check-prefix=UPSTREAM
// RUN: ctjs-translate --mlir-to-cpp %t/scopes.mlir | FileCheck %s --check-prefix=SCOPES
// RUN: ctjs-translate --mlir-to-cpp %t/wrong-marker.mlir | FileCheck %s --check-prefix=UPSTREAM
// RUN: python3 %S/check-readable-float-bits.py --translate ctjs-translate --work %t.bits

// NATIVE: double price = 100.0;
// NATIVE: double fraction = 0.1;
// NATIVE: double negative_zero = -0.0;
// NATIVE: float single_price = 100.0f;
// NATIVE: float single_fraction = 0.1f;
// NATIVE: float single_negative_zero = -0.0f;
// NATIVE: double array[3] = {0.0, 100.0, -0.0};
// Unsupported formats and non-finite spelling retain upstream behavior.
// NATIVE: _Float16 half_value = 2.00000e+00f16;
// NATIVE: __bf16 brain_value = 4.0000e+00bf16;
// NATIVE: double not_a_number = NAN;
// NATIVE: double positive_infinity = INFINITY;
// NATIVE: double negative_infinity = -INFINITY;
// NATIVE: float single_not_a_number = NAN;
// NATIVE: float single_infinity = INFINITY;

// UPSTREAM: double price = 1.00000000000000000e+02;
// UPSTREAM: float single_price = 1.000000000e+02f;

// SCOPES: double outer_before = 100.0;
// SCOPES: double inner_unmarked = 1.00000000000000000e+02;
// SCOPES: double outer_after = 100.0;
// SCOPES: double unmarked_before = 1.00000000000000000e+02;
// SCOPES: double inner_marked = 100.0;
// SCOPES: double unmarked_after = 1.00000000000000000e+02;

//--- native.mlir
module attributes {ctnative.readable_literals} {
  emitc.global @price : f64 = 100.0
  emitc.global @fraction : f64 = 0.1
  emitc.global @negative_zero : f64 = 0x8000000000000000
  emitc.global @single_price : f32 = 100.0
  emitc.global @single_fraction : f32 = 0.1
  emitc.global @single_negative_zero : f32 = 0x80000000
  emitc.global @array : !emitc.array<3xf64> = dense<[0.0, 100.0, -0.0]>
  emitc.global @half_value : f16 = 2.0
  emitc.global @brain_value : bf16 = 4.0
  emitc.global @not_a_number : f64 = 0x7FF8000000000000
  emitc.global @positive_infinity : f64 = 0x7FF0000000000000
  emitc.global @negative_infinity : f64 = 0xFFF0000000000000
  emitc.global @single_not_a_number : f32 = 0x7FC00000
  emitc.global @single_infinity : f32 = 0x7F800000
}

//--- unmarked.mlir
module {
  emitc.global @price : f64 = 100.0
  emitc.global @single_price : f32 = 100.0
}

//--- scopes.mlir
module {
  module @native attributes {ctnative.readable_literals} {
    emitc.global @outer_before : f64 = 100.0
    module @ordinary {
      emitc.global @inner_unmarked : f64 = 100.0
    }
    emitc.global @outer_after : f64 = 100.0
  }
  module @ordinary {
    emitc.global @unmarked_before : f64 = 100.0
    module @native attributes {ctnative.readable_literals} {
      emitc.global @inner_marked : f64 = 100.0
    }
    emitc.global @unmarked_after : f64 = 100.0
  }
}

//--- wrong-marker.mlir
module attributes {ctnative.readable_literals = false} {
  emitc.global @price : f64 = 100.0
  emitc.global @single_price : f32 = 100.0
}
