// Typed Number arithmetic, const extraction, deduction and exception payloads
// retain the class and preserve NaN and signed zero under both C++ compilers.
// RUN: ctjs-translate --mlir-to-cpp %s > %t.explicit.cpp
// RUN: FileCheck %s --check-prefix=CPP < %t.explicit.cpp
// RUN: ctjs-opt --ctnative-print-deduced --mlir-print-debuginfo %s | ctjs-translate --mlir-to-cpp > %t.deduced.cpp
// RUN: FileCheck %s --check-prefix=DEDUCED < %t.deduced.cpp
// RUN: ctjs-opt --ctnative-print-deduced --mlir-print-debuginfo %s | ctjs-translate --mlir-to-cpp --declare-variables-at-top > %t.hoisted.cpp
// RUN: %gxx -O2 %t.explicit.cpp -o %t.gcc && %t.gcc
// RUN: %clangxx -O2 %t.explicit.cpp -o %t.clang && %t.clang
// RUN: %gxx -O2 %t.deduced.cpp -o %t.gcc && %t.gcc
// RUN: %clangxx -O2 %t.deduced.cpp -o %t.clang && %t.clang
// RUN: %gxx -O2 %t.hoisted.cpp -o %t.gcc && %t.gcc
// RUN: %clangxx -O2 %t.hoisted.cpp -o %t.clang && %t.clang
// RUN: ctjs-opt --ctnative-print-deduced=mutate=1 --mlir-print-debuginfo %s | ctjs-translate --mlir-to-cpp > %t.mutated.cpp
// RUN: not %gxx -O2 %t.mutated.cpp -o %t.mutated 2>&1 | FileCheck %s --check-prefix=MUTATED

// CPP-LABEL: ctnative::js_num add_number(
// CPP-SAME: ctnative::js_num const
// CPP: ctnative::js_num const [[SUM:v[0-9]+]] = {{.*}} + {{.*}};
// CPP-NEXT: return [[SUM]];
// CPP-LABEL: double extract_number(
// CPP-SAME: ctnative::js_num const [[VALUE:v[0-9]+]]) {
// CPP-NEXT: double const [[RAW:v[0-9]+]] = [[VALUE]].value();
// CPP-NEXT: return [[RAW]];
// CPP: catch (ctnative::js_exception<ctnative::js_num> const &
// DEDUCED: auto const [[SUM:v[0-9]+]] = {{.*}} + {{.*}};
// DEDUCED-NEXT: CTCOMPILE_PIN([[SUM]], "number.js:1:1", ctnative::js_num const);
// MUTATED: static assertion failed
// MUTATED-SAME: ctcompile:

module attributes {ctnative.const_bindings, ctnative.readable_literals} {
  emitc.include "ctcompile/CTNative/Runtime/ctnative.hpp"
  emitc.func @add_number(%left: !emitc.opaque<"ctnative::js_num">, %right: !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num"> {
    %sum = emitc.add %left, %right : (!emitc.opaque<"ctnative::js_num">, !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num"> loc("number.js":1:1)
    emitc.return %sum : !emitc.opaque<"ctnative::js_num">
  }
  emitc.func @extract_number(%value: !emitc.opaque<"ctnative::js_num">) -> f64 {
    %raw = emitc.member_call_opaque %value "value"() : !emitc.opaque<"ctnative::js_num">, () -> f64
    emitc.return %raw : f64
  }
  emitc.func @throw_number(%value: !emitc.opaque<"ctnative::js_num">) {
    ctnative.cpp_throw %value : !emitc.opaque<"ctnative::js_num">
    emitc.return
  }
  emitc.func @throw_boolean(%value: !emitc.opaque<"ctnative::js_boolean_t">) {
    ctnative.cpp_throw %value : !emitc.opaque<"ctnative::js_boolean_t">
    emitc.return
  }
  emitc.func @catch_number(%value: !emitc.opaque<"ctnative::js_num">) -> !emitc.opaque<"ctnative::js_num"> {
    %slot = "emitc.variable"() <{value = #emitc.opaque<"{}">}> : () -> !emitc.lvalue<!emitc.opaque<"ctnative::js_num">>
    ctnative.cpp_try {
      emitc.call @throw_number(%value) : (!emitc.opaque<"ctnative::js_num">) -> ()
      ctnative.cpp_try_end
    } catch {
    ^bb0(%caught: !emitc.opaque<"ctnative::js_num">):
      emitc.assign %caught : !emitc.opaque<"ctnative::js_num"> to %slot : <!emitc.opaque<"ctnative::js_num">>
      ctnative.cpp_try_end
    }
    %result = emitc.load %slot : <!emitc.opaque<"ctnative::js_num">>
    emitc.return %result : !emitc.opaque<"ctnative::js_num">
  }
  emitc.verbatim "int main() {\0A  using number = ctnative::js_num;\0A  static_assert(std::is_same_v<decltype(add_number(number{}, number{})), number>);\0A  if (extract_number(add_number(number{40.0}, number{2.0})) != 42.0) return 1;\0A  if (!std::signbit(catch_number(number{-0.0}).value())) return 2;\0A  if (!std::isnan(catch_number(number{ctnative::js_nan_t{}}).value())) return 3;\0A  try { throw_boolean(ctnative::js_boolean_t{false}); }\0A  catch (ctnative::js_exception<ctnative::js_boolean_t> const & value) { return static_cast<bool>(value.value) ? 4 : 0; }\0A  return 5;\0A}"
}
