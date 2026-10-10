// Typed String values retain ownership through concatenation, extraction and
// exceptions. Embedded NUL/WTF-8 bytes survive both native printing policies.
// RUN: split-file %s %t
// RUN: ctjs-translate --mlir-to-cpp %t/valid.mlir > %t.explicit.cpp
// RUN: FileCheck %s --check-prefix=CPP < %t.explicit.cpp
// RUN: ctjs-opt --ctnative-print-deduced --mlir-print-debuginfo %t/valid.mlir | ctjs-translate --mlir-to-cpp > %t.deduced.cpp
// RUN: FileCheck %s --check-prefix=DEDUCED < %t.deduced.cpp
// RUN: %gxx -O2 %t.explicit.cpp -o %t.gcc && %t.gcc
// RUN: %clangxx -O2 %t.explicit.cpp -o %t.clang && %t.clang
// RUN: %gxx -O2 %t.deduced.cpp -o %t.gcc && %t.gcc
// RUN: %clangxx -O2 %t.deduced.cpp -o %t.clang && %t.clang
// RUN: not ctjs-opt %t/borrowed-throw.mlir 2>&1 | FileCheck %s --check-prefix=BORROWED

// CPP-LABEL: ctnative::js_string concat_string(
// CPP-SAME: ctnative::js_string const
// CPP: ctnative::js_string const [[JOINED:v[0-9]+]] = {{.*}} + {{.*}};
// CPP-NEXT: return [[JOINED]];
// CPP-LABEL: std::string extract_string(
// CPP-SAME: ctnative::js_string const [[VALUE:v[0-9]+]]) {
// CPP-NEXT: std::string const [[RAW:v[0-9]+]] = [[VALUE]].value();
// CPP-NEXT: return [[RAW]];
// CPP-LABEL: bool starts_with(
// CPP-SAME: ctnative::js_string const [[TEXT:v[0-9]+]], ctnative::js_string const [[PREFIX:v[0-9]+]]) {
// CPP-NEXT: bool const [[MATCH:v[0-9]+]] = [[TEXT]].startsWith([[PREFIX]]);
// CPP-NEXT: return [[MATCH]];
// CPP: catch (ctnative::js_exception<ctnative::js_string> const &
// DEDUCED: auto const [[JOINED:v[0-9]+]] = {{.*}} + {{.*}};
// DEDUCED-NEXT: CTCOMPILE_PIN([[JOINED]], "string.js:1:1", ctnative::js_string const);
// BORROWED: error: 'ctnative.cpp_throw' op requires an f64, i1 or owning std::string payload

//--- valid.mlir
module attributes {ctnative.const_bindings, ctnative.readable_literals} {
  emitc.include "ctcompile/CTNative/Runtime/ctnative.hpp"
  emitc.func @concat_string(%left: !emitc.opaque<"ctnative::js_string">, %right: !emitc.opaque<"ctnative::js_string">) -> !emitc.opaque<"ctnative::js_string"> {
    %joined = emitc.add %left, %right : (!emitc.opaque<"ctnative::js_string">, !emitc.opaque<"ctnative::js_string">) -> !emitc.opaque<"ctnative::js_string"> loc("string.js":1:1)
    emitc.return %joined : !emitc.opaque<"ctnative::js_string">
  }
  emitc.func @extract_string(%value: !emitc.opaque<"ctnative::js_string">) -> !emitc.opaque<"std::string"> {
    %raw = emitc.member_call_opaque %value "value"() : !emitc.opaque<"ctnative::js_string">, () -> !emitc.opaque<"std::string">
    emitc.return %raw : !emitc.opaque<"std::string">
  }
  emitc.func @starts_with(%value: !emitc.opaque<"ctnative::js_string">, %prefix: !emitc.opaque<"ctnative::js_string">) -> i1 {
    %match = emitc.member_call_opaque %value "startsWith"(%prefix) : !emitc.opaque<"ctnative::js_string">, (!emitc.opaque<"ctnative::js_string">) -> i1
    emitc.return %match : i1
  }
  emitc.func @throw_string(%value: !emitc.opaque<"ctnative::js_string">) {
    ctnative.cpp_throw %value : !emitc.opaque<"ctnative::js_string">
    emitc.return
  }
  emitc.func @catch_string(%value: !emitc.opaque<"ctnative::js_string">) -> !emitc.opaque<"ctnative::js_string"> {
    %slot = "emitc.variable"() <{value = #emitc.opaque<"{}">}> : () -> !emitc.lvalue<!emitc.opaque<"ctnative::js_string">>
    ctnative.cpp_try {
      emitc.call @throw_string(%value) : (!emitc.opaque<"ctnative::js_string">) -> ()
      ctnative.cpp_try_end
    } catch {
    ^bb0(%caught: !emitc.opaque<"ctnative::js_string">):
      emitc.assign %caught : !emitc.opaque<"ctnative::js_string"> to %slot : <!emitc.opaque<"ctnative::js_string">>
      ctnative.cpp_try_end
    }
    %result = emitc.load %slot : <!emitc.opaque<"ctnative::js_string">>
    emitc.return %result : !emitc.opaque<"ctnative::js_string">
  }
  emitc.verbatim "int main() {\0A  using string = ctnative::js_string;\0A  static_assert(std::is_same_v<decltype(concat_string(string{}, string{})), string>);\0A  const std::string expected{\22a\\0\\xed\\xa0\\x80\22, 5};\0A  std::string caller = expected;\0A  string value{caller};\0A  caller.assign(8192, 'x');\0A  const auto joined = concat_string(value, string{std::string{\22!\22}});\0A  if (joined.value() != expected + \22!\22) return 1;\0A  const auto saved = extract_string(catch_string(value));\0A  value = string{std::string(8192, 'y')};\0A  if (saved != expected || joined.value() != expected + \22!\22) return 2;\0A  if (!starts_with(joined, string{std::string{\22a\\0\22, 2}})) return 3;\0A  if (starts_with(joined, string{std::string{\22b\22}})) return 4;\0A  if (!starts_with(joined, string{})) return 5;\0A  return 0;\0A}"
}

//--- borrowed-throw.mlir
emitc.func @borrowed(%value: !emitc.opaque<"std::string const &">) {
  ctnative.cpp_throw %value : !emitc.opaque<"std::string const &">
  emitc.return
}
