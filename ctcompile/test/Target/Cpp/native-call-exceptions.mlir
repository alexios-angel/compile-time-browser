// Target prerequisite for throwing source callees. Catch slots contain the
// pre-call register state; a call result is published only on normal return.
// This fixture does not relax source recovery or its live effect admission.
// RUN: split-file %s %t
// RUN: ctjs-translate --mlir-to-cpp %t/valid.mlir > %t/explicit.cpp
// RUN: ctjs-opt %t/valid.mlir --ctnative-print-deduced --mlir-print-debuginfo -o %t/deduced.mlir
// RUN: ctjs-translate --mlir-to-cpp %t/deduced.mlir > %t/deduced.cpp
// RUN: ctjs-translate --mlir-to-cpp --declare-variables-at-top %t/deduced.mlir > %t/hoisted.cpp
// RUN: g++ -std=c++23 -O2 -Wall -Wextra -Werror -Wconversion -pedantic %t/explicit.cpp -o %t/explicit-gcc && %t/explicit-gcc
// RUN: clang++ -std=c++23 -O2 -Wall -Wextra -Werror -Wconversion -pedantic %t/explicit.cpp -o %t/explicit-clang && %t/explicit-clang
// RUN: g++ -std=c++23 -O2 -Wall -Wextra -Werror -Wconversion -pedantic %t/deduced.cpp -o %t/deduced-gcc && %t/deduced-gcc
// RUN: clang++ -std=c++23 -O2 -Wall -Wextra -Werror -Wconversion -pedantic %t/deduced.cpp -o %t/deduced-clang && %t/deduced-clang
// RUN: g++ -std=c++23 -O2 -Wall -Wextra -Werror -Wconversion -pedantic %t/hoisted.cpp -o %t/hoisted-gcc && %t/hoisted-gcc
// RUN: clang++ -std=c++23 -O2 -Wall -Wextra -Werror -Wconversion -pedantic %t/hoisted.cpp -o %t/hoisted-clang && %t/hoisted-clang
// RUN: clang++ -std=c++23 -O1 -g -Wall -Wextra -Werror -Wconversion -pedantic -fno-omit-frame-pointer -fsanitize=address,undefined -fsanitize-address-use-after-scope %t/explicit.cpp -o %t/asan-explicit
// RUN: env ASAN_OPTIONS=detect_stack_use_after_return=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 %t/asan-explicit
// RUN: clang++ -std=c++23 -O1 -g -Wall -Wextra -Werror -Wconversion -pedantic -fno-omit-frame-pointer -fsanitize=address,undefined -fsanitize-address-use-after-scope %t/deduced.cpp -o %t/asan-deduced
// RUN: env ASAN_OPTIONS=detect_stack_use_after_return=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 %t/asan-deduced
// RUN: %python %S/native-call-exceptions.py --opt ctjs-opt --fixture %t/valid.mlir --work %t/controls
// RUN: ctjs-translate --mlir-to-cpp %t/controls/wrong-state.mlir > %t/wrong-state.cpp
// RUN: g++ -std=c++23 -O2 -Wall -Wextra -Werror -Wconversion -pedantic %t/wrong-state.cpp -o %t/wrong-state
// RUN: not %t/wrong-state

//--- valid.mlir
module attributes {ctnative.readable_names, ctnative.const_bindings, ctnative.numeric_alias} {
  emitc.include <"string">
  emitc.include <"vector">
  emitc.include <"memory">
  emitc.include <"new">
  emitc.include <"exception">
  emitc.include <"cmath">
  emitc.verbatim "using js_num = double;\0Anamespace ctnative { template <class T> struct js_exception { T value; }; }"
  emitc.include "helpers.h"
  emitc.func @maybe_number(%flag: i1, %payload: f64) -> f64 {
    emitc.if %flag {
      ctnative.cpp_throw %payload : f64
    }
    %normal = emitc.literal "20.0" : f64
    emitc.return %normal : f64
  }
  emitc.func @forward_number(%flag: i1, %payload: f64) -> f64 {
    %value = emitc.call @maybe_number(%flag, %payload) : (i1, f64) -> f64
    emitc.return %value : f64
  }
  emitc.func @guard_number(%flag: i1) -> f64 {
    %zero = emitc.literal "0.0" : f64
    %slot = "emitc.variable"() {value = #emitc.opaque<"{}">} : () -> !emitc.lvalue<f64>
    %state = "emitc.variable"() {value = #emitc.opaque<"{}">} : () -> !emitc.lvalue<f64>
    emitc.assign %zero : f64 to %slot : !emitc.lvalue<f64>
    ctnative.cpp_try {
      %before = emitc.literal "10.0" : f64
      %payload = emitc.literal "32.0" : f64
      emitc.assign %before : f64 to %slot : !emitc.lvalue<f64>
      emitc.assign %before : f64 to %state : !emitc.lvalue<f64>
      %value = emitc.call @forward_number(%flag, %payload) : (i1, f64) -> f64
      emitc.assign %value : f64 to %slot : !emitc.lvalue<f64>
      emitc.call_opaque "published_result"() : () -> ()
      ctnative.cpp_try_end
    } catch {
    ^bb0(%caught: f64):
      %before = emitc.load %state : !emitc.lvalue<f64>
      %answer = emitc.add %before, %caught : (f64, f64) -> f64
      emitc.assign %answer : f64 to %slot : !emitc.lvalue<f64>
      ctnative.cpp_try_end
    }
    %answer = emitc.load %slot : !emitc.lvalue<f64>
    emitc.return %answer : f64
  }
  emitc.func @maybe_boolean(%flag: i1, %payload: i1) -> i1 {
    emitc.if %flag {
      ctnative.cpp_throw %payload : i1
    }
    %normal = emitc.literal "true" : i1
    emitc.return %normal : i1
  }
  emitc.func @guard_boolean(%flag: i1, %payload: i1) -> i1 {
    %slot = "emitc.variable"() {value = #emitc.opaque<"false">} : () -> !emitc.lvalue<i1>
    ctnative.cpp_try {
      %value = emitc.call @maybe_boolean(%flag, %payload) : (i1, i1) -> i1
      emitc.assign %value : i1 to %slot : !emitc.lvalue<i1>
      ctnative.cpp_try_end
    } catch {
    ^bb0(%caught: i1):
      emitc.assign %caught : i1 to %slot : !emitc.lvalue<i1>
      ctnative.cpp_try_end
    }
    %answer = emitc.load %slot : !emitc.lvalue<i1>
    emitc.return %answer : i1
  }
  emitc.func @maybe_string(%flag: i1, %payload: !emitc.opaque<"std::string">) -> !emitc.opaque<"std::string"> {
    %cleanup = emitc.call_opaque "make_cleanup"() : () -> !emitc.opaque<"std::shared_ptr<cleanup_probe>">
    emitc.call_opaque "static_cast<void>"(%cleanup) : (!emitc.opaque<"std::shared_ptr<cleanup_probe>">) -> ()
    %copy = emitc.call_opaque "copy_payload"(%payload) : (!emitc.opaque<"std::string">) -> !emitc.opaque<"std::string">
    emitc.if %flag {
      ctnative.cpp_throw %copy : !emitc.opaque<"std::string">
    }
    emitc.return %payload : !emitc.opaque<"std::string">
  }
  emitc.func @forward_string(%flag: i1, %payload: !emitc.opaque<"std::string">) -> !emitc.opaque<"std::string"> {
    %value = emitc.call @maybe_string(%flag, %payload) : (i1, !emitc.opaque<"std::string">) -> !emitc.opaque<"std::string">
    emitc.return %value : !emitc.opaque<"std::string">
  }
  emitc.func @guard_string(%flag: i1, %payload: !emitc.opaque<"std::string">) -> !emitc.opaque<"std::string"> {
    %slot = "emitc.variable"() {value = #emitc.opaque<"{}">} : () -> !emitc.lvalue<!emitc.opaque<"std::string">>
    %state = "emitc.variable"() {value = #emitc.opaque<"{}">} : () -> !emitc.lvalue<!emitc.opaque<"std::string">>
    ctnative.cpp_try {
      %before = emitc.call_opaque "state_string"() : () -> !emitc.opaque<"std::string">
      emitc.assign %before : !emitc.opaque<"std::string"> to %slot : !emitc.lvalue<!emitc.opaque<"std::string">>
      emitc.assign %before : !emitc.opaque<"std::string"> to %state : !emitc.lvalue<!emitc.opaque<"std::string">>
      %value = emitc.call @forward_string(%flag, %payload) : (i1, !emitc.opaque<"std::string">) -> !emitc.opaque<"std::string">
      emitc.assign %value : !emitc.opaque<"std::string"> to %slot : !emitc.lvalue<!emitc.opaque<"std::string">>
      ctnative.cpp_try_end
    } catch {
    ^bb0(%caught: !emitc.opaque<"std::string">):
      %before = emitc.load %state : !emitc.lvalue<!emitc.opaque<"std::string">>
      %answer = emitc.add %before, %caught : (!emitc.opaque<"std::string">, !emitc.opaque<"std::string">) -> !emitc.opaque<"std::string">
      emitc.assign %answer : !emitc.opaque<"std::string"> to %slot : !emitc.lvalue<!emitc.opaque<"std::string">>
      ctnative.cpp_try_end
    }
    %answer = emitc.load %slot : !emitc.lvalue<!emitc.opaque<"std::string">>
    emitc.return %answer : !emitc.opaque<"std::string">
  }
  // The helper consumes a boolean payload locally. Its catch can throw an
  // owning string that belongs to the caller's string handler.
  emitc.func @rethrow_string(%flag: i1, %payload: !emitc.opaque<"std::string">) -> !emitc.opaque<"std::string"> {
    %boolean = emitc.literal "false" : i1
    ctnative.cpp_try {
      %value = emitc.call @maybe_boolean(%flag, %boolean) : (i1, i1) -> i1
      emitc.call_opaque "static_cast<void>"(%value) : (i1) -> ()
      ctnative.cpp_try_end
    } catch {
    ^bb0(%unused: i1):
      ctnative.cpp_throw %payload : !emitc.opaque<"std::string">
      ctnative.cpp_try_end
    }
    emitc.return %payload : !emitc.opaque<"std::string">
  }
  emitc.func @guard_rethrow(%flag: i1, %payload: !emitc.opaque<"std::string">) -> !emitc.opaque<"std::string"> {
    %slot = "emitc.variable"() {value = #emitc.opaque<"{}">} : () -> !emitc.lvalue<!emitc.opaque<"std::string">>
    ctnative.cpp_try {
      %value = emitc.call @rethrow_string(%flag, %payload) : (i1, !emitc.opaque<"std::string">) -> !emitc.opaque<"std::string">
      emitc.assign %value : !emitc.opaque<"std::string"> to %slot : !emitc.lvalue<!emitc.opaque<"std::string">>
      ctnative.cpp_try_end
    } catch {
    ^bb0(%caught: !emitc.opaque<"std::string">):
      emitc.assign %caught : !emitc.opaque<"std::string"> to %slot : !emitc.lvalue<!emitc.opaque<"std::string">>
      ctnative.cpp_try_end
    }
    %answer = emitc.load %slot : !emitc.lvalue<!emitc.opaque<"std::string">>
    emitc.return %answer : !emitc.opaque<"std::string">
  }
  emitc.func @foreign_helper() {
    emitc.call_opaque "raise_allocation_failure"() : () -> ()
    emitc.return
  }
  emitc.func @guard_foreign() {
    ctnative.cpp_try {
      emitc.call @foreign_helper() : () -> ()
      ctnative.cpp_try_end
    } catch {
    ^bb0(%unused: f64):
      emitc.call_opaque "caught_foreign"() : () -> ()
      ctnative.cpp_try_end
    }
    emitc.return
  }
  emitc.include "main.h"
}

//--- helpers.h
inline int result_publications = 0;
inline int cleanup_live = 0;
inline int cleanup_completed = 0;
inline void published_result() { ++result_publications; }
struct cleanup_probe {
    cleanup_probe() { ++cleanup_live; }
    ~cleanup_probe() { --cleanup_live; ++cleanup_completed; }
};
inline auto make_cleanup() { return std::make_shared<cleanup_probe>(); }
inline std::string copy_payload(std::string const & value) { return value + "|copy"; }
inline std::string state_string() { return std::string(32768, 's') + "|before|"; }
inline void raise_allocation_failure() { throw std::bad_alloc{}; }
inline bool foreign_was_caught = false;
inline void caught_foreign() { foreign_was_caught = true; }
inline std::string payload() {
    return std::string(32768, 'p') + std::string("\0tail\xED\xA0\x80", 8);
}
inline void churn() {
    std::vector<std::string> values(64, std::string(32768, 'x'));
    if (values[31].size() != 32768) { std::terminate(); }
}

//--- main.h
int main() {
    if (guard_number(true) != 42.0 || result_publications != 0) { return 1; }
    if (guard_number(false) != 20.0 || result_publications != 1) { return 2; }
    if (guard_boolean(true, false) || !guard_boolean(true, true) || !guard_boolean(false, false)) {
        return 3;
    }
    const auto original = payload();
    const auto caught = guard_string(true, original);
    const auto temporary = guard_string(true, payload());
    const auto normal = guard_string(false, payload());
    if (cleanup_live != 0 || cleanup_completed != 3) { return 4; }
    const auto rethrown = guard_rethrow(true, payload());
    const auto normal_rethrow = guard_rethrow(false, payload());
    for (int index = 0; index < 32; ++index) { churn(); }
    if (caught != state_string() + copy_payload(original) || caught != temporary) { return 5; }
    if (original != payload() || normal != original || rethrown != original || normal_rethrow != original) {
        return 6;
    }
    bool allocation_propagated = false;
    try { guard_foreign(); } catch (std::bad_alloc const &) { allocation_propagated = true; }
    if (!allocation_propagated || foreign_was_caught) { return 7; }
    bool negative_zero = false;
    try { forward_number(true, -0.0); }
    catch (ctnative::js_exception<js_num> const & value) { negative_zero = std::signbit(value.value); }
    if (!negative_zero) { return 8; }
    return 0;
}
