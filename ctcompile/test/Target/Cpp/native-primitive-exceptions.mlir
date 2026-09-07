// Owning primitive exception copies survive the C++ handler and stack/heap
// churn. Catch mutation changes the copy, not the exception's own payload.
// RUN: split-file %s %t
// RUN: ctjs-translate --mlir-to-cpp %t/valid.mlir > %t/explicit.cpp
// RUN: FileCheck %s < %t/explicit.cpp
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
// RUN: not ctjs-opt %t/borrowed-catch.mlir 2>&1 | FileCheck %s --check-prefix=CATCH
// RUN: not ctjs-opt %t/borrowed-throw.mlir 2>&1 | FileCheck %s --check-prefix=THROW
// RUN: not ctjs-opt %t/reference-throw.mlir 2>&1 | FileCheck %s --check-prefix=THROW
// RUN: not ctjs-opt %t/pointer-throw.mlir 2>&1 | FileCheck %s --check-prefix=THROW
// RUN: not ctjs-opt %t/mixed-catch.mlir 2>&1 | FileCheck %s --check-prefix=MIXED
// RUN: not ctjs-opt %t/mixed-rethrow.mlir 2>&1 | FileCheck %s --check-prefix=MIXED

// CHECK-LABEL: std::string caught_string(
// CHECK: } catch (ctnative::js_exception<std::string> const & [[STRING:[A-Za-z_0-9]+]]) {
// CHECK-NEXT: std::string [[COPY:[A-Za-z_0-9]+]] = [[STRING]].value;
// CHECK-NEXT: mutate_string([[COPY]]);
// CHECK-LABEL: bool caught_boolean(
// CHECK: } catch (ctnative::js_exception<bool> const & [[BOOL:[A-Za-z_0-9]+]]) {
// CHECK-NEXT: bool [[COPY:[A-Za-z_0-9]+]] = [[BOOL]].value;
// CHECK-NEXT: mutate_boolean([[COPY]]);
// CATCH: error: 'ctnative.cpp_try' op requires exactly one f64, i1 or owning std::string catch argument
// THROW: error: 'ctnative.cpp_throw' op requires an f64, i1 or owning std::string payload
// MIXED: error: 'ctnative.cpp_try' op requires each protected throw to match the homogeneous catch payload

//--- valid.mlir
module attributes {ctnative.readable_names, ctnative.const_bindings, ctnative.numeric_alias} {
  emitc.include <"string">
  emitc.include <"vector">
  emitc.include <"new">
  emitc.include <"exception">
  emitc.verbatim "using js_num = double;\0Anamespace ctnative { template <class T> struct js_exception { T value; }; }"
  emitc.include "helpers.h"
  emitc.func @caught_string(%input: !emitc.opaque<"std::string">) -> !emitc.opaque<"std::string"> {
    %slot = "emitc.variable"() {value = #emitc.opaque<"{}">} : () -> !emitc.lvalue<!emitc.opaque<"std::string">>
    ctnative.cpp_try {
      ctnative.cpp_throw %input : !emitc.opaque<"std::string">
      ctnative.cpp_try_end
    } catch {
    ^bb0(%caught: !emitc.opaque<"std::string">):
      emitc.call_opaque "mutate_string"(%caught) : (!emitc.opaque<"std::string">) -> ()
      emitc.assign %caught : !emitc.opaque<"std::string"> to %slot : !emitc.lvalue<!emitc.opaque<"std::string">>
      ctnative.cpp_try_end
    }
    %answer = emitc.load %slot : !emitc.lvalue<!emitc.opaque<"std::string">>
    emitc.return %answer : !emitc.opaque<"std::string">
  }
  emitc.func @caught_boolean(%input: i1) -> i1 {
    %slot = "emitc.variable"() {value = #emitc.opaque<"false">} : () -> !emitc.lvalue<i1>
    ctnative.cpp_try {
      ctnative.cpp_throw %input : i1
      ctnative.cpp_try_end
    } catch {
    ^bb0(%caught: i1):
      emitc.call_opaque "mutate_boolean"(%caught) : (i1) -> ()
      emitc.assign %caught : i1 to %slot : !emitc.lvalue<i1>
      ctnative.cpp_try_end
    }
    %answer = emitc.load %slot : !emitc.lvalue<i1>
    emitc.return %answer : i1
  }
  // Distinct nested target payload types are valid when each throw reaches a
  // matching handler. A throw from the inner catch belongs to the outer try.
  // This exercises target verification/emission, not nested source admission.
  emitc.func @rethrow_string(%input: !emitc.opaque<"std::string">) -> !emitc.opaque<"std::string"> {
    %slot = "emitc.variable"() {value = #emitc.opaque<"{}">} : () -> !emitc.lvalue<!emitc.opaque<"std::string">>
    ctnative.cpp_try {
      ctnative.cpp_try {
        %value = emitc.literal "true" : i1
        ctnative.cpp_throw %value : i1
        ctnative.cpp_try_end
      } catch {
      ^bb0(%unused: i1):
        ctnative.cpp_throw %input : !emitc.opaque<"std::string">
        ctnative.cpp_try_end
      }
      ctnative.cpp_try_end
    } catch {
    ^bb0(%caught: !emitc.opaque<"std::string">):
      emitc.assign %caught : !emitc.opaque<"std::string"> to %slot : !emitc.lvalue<!emitc.opaque<"std::string">>
      ctnative.cpp_try_end
    }
    %answer = emitc.load %slot : !emitc.lvalue<!emitc.opaque<"std::string">>
    emitc.return %answer : !emitc.opaque<"std::string">
  }
  emitc.func @foreign_string() {
    ctnative.cpp_try {
      emitc.call_opaque "raise_allocation_failure"() : () -> ()
      ctnative.cpp_try_end
    } catch {
    ^bb0(%unused: !emitc.opaque<"std::string">):
      emitc.call_opaque "caught_foreign"() : () -> ()
      ctnative.cpp_try_end
    }
    emitc.return
  }
  emitc.include "main.h"
}

//--- helpers.h
inline void mutate_string(std::string & value) { value += "|changed"; }
inline void mutate_boolean(bool & value) { value = !value; }
inline void raise_allocation_failure() { throw std::bad_alloc{}; }
inline bool foreign_was_caught = false;
inline void caught_foreign() { foreign_was_caught = true; }
inline std::string payload() {
    auto result = std::string(32768, 'x');
    result += std::string("\0tail\xED\xA0\x80", 8);
    return result;
}
inline void churn() {
    std::vector<std::string> values(64, std::string(32768, 'y'));
    if (values[31].size() != 32768) { std::terminate(); }
}

//--- main.h
int main() {
    const auto original = payload();
    const auto result = caught_string(original);
    const auto temporary = caught_string(payload());
    const auto rethrown = rethrow_string(payload());
    for (int index = 0; index < 32; ++index) { churn(); }
    if (original != payload() || result != original + "|changed" || temporary != result) { return 1; }
    if (rethrown != original) { return 4; }
    if (caught_boolean(true) || !caught_boolean(false)) { return 2; }
    bool allocation_propagated = false;
    try { foreign_string(); } catch (std::bad_alloc const &) { allocation_propagated = true; }
    if (!allocation_propagated || foreign_was_caught) { return 3; }
    return 0;
}

//--- borrowed-catch.mlir
module {
  ctnative.cpp_try {
    ctnative.cpp_try_end
  } catch {
  ^bb0(%caught: !emitc.opaque<"std::string_view">):
    ctnative.cpp_try_end
  }
}

//--- borrowed-throw.mlir
module {
  emitc.func @borrowed(%value: !emitc.opaque<"std::string_view">) {
    ctnative.cpp_throw %value : !emitc.opaque<"std::string_view">
    emitc.return
  }
}

//--- reference-throw.mlir
module {
  emitc.func @borrowed(%value: !emitc.opaque<"std::string const &">) {
    ctnative.cpp_throw %value : !emitc.opaque<"std::string const &">
    emitc.return
  }
}

//--- pointer-throw.mlir
module {
  emitc.func @borrowed(%value: !emitc.ptr<!emitc.opaque<"std::string">>) {
    ctnative.cpp_throw %value : !emitc.ptr<!emitc.opaque<"std::string">>
    emitc.return
  }
}

//--- mixed-catch.mlir
module {
  ctnative.cpp_try {
    %value = emitc.literal "true" : i1
    emitc.if %value {
      ctnative.cpp_throw %value : i1
    }
    ctnative.cpp_try_end
  } catch {
  ^bb0(%caught: f64):
    ctnative.cpp_try_end
  }
}

//--- mixed-rethrow.mlir
module {
  ctnative.cpp_try {
    ctnative.cpp_try {
      %value = emitc.literal "true" : i1
      ctnative.cpp_throw %value : i1
      ctnative.cpp_try_end
    } catch {
    ^bb0(%caught: i1):
      ctnative.cpp_throw %caught : i1
      ctnative.cpp_try_end
    }
    ctnative.cpp_try_end
  } catch {
  ^bb0(%caught: f64):
    ctnative.cpp_try_end
  }
}
