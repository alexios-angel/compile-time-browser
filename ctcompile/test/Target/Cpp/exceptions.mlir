// Target-level exception scopes must preserve bindings under ordinary,
// deduced and hoisted emission. The executable also distinguishes JS payloads
// from foreign C++ exceptions and checks rethrow through nested handlers.
// RUN: split-file %s %t
// RUN: ctjs-translate --mlir-to-cpp %t/valid.mlir > %t/explicit.cpp
// RUN: FileCheck %s --check-prefixes=COMMON,EXPLICIT < %t/explicit.cpp
// RUN: ctjs-opt %t/valid.mlir --ctnative-print-deduced --mlir-print-debuginfo -o %t/deduced.mlir
// RUN: ctjs-translate --mlir-to-cpp %t/deduced.mlir > %t/deduced.cpp
// RUN: FileCheck %s --check-prefixes=COMMON,DEDUCED < %t/deduced.cpp
// RUN: ctjs-translate --mlir-to-cpp --declare-variables-at-top %t/deduced.mlir > %t/hoisted.cpp
// RUN: FileCheck %s --check-prefix=HOISTED < %t/hoisted.cpp
// RUN: %gxx %t/explicit.cpp -o %t/explicit-gcc
// RUN: %t/explicit-gcc
// RUN: %clangxx %t/explicit.cpp -o %t/explicit-clang
// RUN: %t/explicit-clang
// RUN: %gxx %t/deduced.cpp -o %t/deduced-gcc
// RUN: %t/deduced-gcc
// RUN: %clangxx %t/deduced.cpp -o %t/deduced-clang
// RUN: %t/deduced-clang
// RUN: %gxx %t/hoisted.cpp -o %t/hoisted-gcc
// RUN: %t/hoisted-gcc
// RUN: %clangxx %t/hoisted.cpp -o %t/hoisted-clang
// RUN: %t/hoisted-clang
// RUN: not ctjs-opt %t/empty-region.mlir 2>&1 | FileCheck %s --check-prefix=EMPTY-REGION
// RUN: not ctjs-opt %t/empty-block.mlir 2>&1 | FileCheck %s --check-prefix=EMPTY-BLOCK
// RUN: not ctjs-opt %t/catch-arity.mlir 2>&1 | FileCheck %s --check-prefix=CATCH
// RUN: not ctjs-opt %t/catch-type.mlir 2>&1 | FileCheck %s --check-prefix=CATCH
// RUN: not ctjs-opt %t/unterminated.mlir 2>&1 | FileCheck %s --check-prefix=TRY-END
// RUN: not ctjs-opt %t/wrong-terminator.mlir 2>&1 | FileCheck %s --check-prefix=TRY-END
// RUN: not ctjs-opt %t/wrong-parent.mlir 2>&1 | FileCheck %s --check-prefix=PARENT
// RUN: not ctjs-opt %t/throw-type.mlir 2>&1 | FileCheck %s --check-prefix=THROW-TYPE
// RUN: not ctjs-opt %t/throw-fallthrough.mlir 2>&1 | FileCheck %s --check-prefix=THROW-FALLTHROUGH

// Locals are v<N>; the exception object is its own ctn_exception_<N>.
// COMMON-LABEL: void throw_number(
// COMMON-SAME: js_num const [[THROWN:[A-Za-z_][A-Za-z_0-9]*]]) {
// COMMON-NEXT: throw ctnative::js_exception{[[THROWN]]};
// COMMON-NEXT: return;
// COMMON-NEXT: }
// COMMON-LABEL: js_num caught_value(
// COMMON-SAME: js_num const [[INPUT:[A-Za-z_][A-Za-z_0-9]*]]) {
// COMMON-NEXT: js_num [[STORAGE:v[0-9]+]] = 0.0;
// COMMON-NEXT: try {
// COMMON-NEXT: if (true) {
// EXPLICIT-NEXT: js_num const [[PAYLOAD:v[0-9]+]] = [[INPUT]] + 1.0;
// DEDUCED-NEXT: auto const [[PAYLOAD:v[0-9]+]] = [[INPUT]] + 1.0;
// DEDUCED-NEXT: CTCOMPILE_PIN([[PAYLOAD]], "native-exceptions.js:2:1", js_num const);
// COMMON-NEXT: throw ctnative::js_exception{[[PAYLOAD]]};
// COMMON-NEXT: }{{$}}
// COMMON-NEXT: } catch (ctnative::js_exception<js_num> const & [[EXCEPTION:ctn_exception_[0-9]+]]) {
// COMMON-NEXT: js_num const [[CAUGHT:v[0-9]+]] = [[EXCEPTION]].value;
// EXPLICIT-NEXT: js_num const [[RECOVERED:v[0-9]+]] = [[CAUGHT]] + 2.0;
// DEDUCED-NEXT: auto const [[RECOVERED:v[0-9]+]] = [[CAUGHT]] + 2.0;
// DEDUCED-NEXT: CTCOMPILE_PIN([[RECOVERED]], "native-exceptions.js:3:1", js_num const);
// COMMON-NEXT: [[STORAGE]] = [[RECOVERED]];
// COMMON-NEXT: }{{$}}
// COMMON-NEXT: js_num const [[ANSWER:v[0-9]+]] = [[STORAGE]];
// COMMON-NEXT: return [[ANSWER]];

// Hoisted results stay mutable, while the catch argument still initializes
// within the handler. A try block must not hide the hoisted result mappings.
// HOISTED-LABEL: js_num caught_value(
// HOISTED-SAME: js_num const [[INPUT:[A-Za-z_][A-Za-z_0-9]*]]) {
// HOISTED-NEXT: js_num [[STORAGE:v[0-9]+]];
// HOISTED-NEXT: js_num [[PAYLOAD:v[0-9]+]];
// HOISTED-NEXT: js_num [[RECOVERED:v[0-9]+]];
// HOISTED-NEXT: js_num [[ANSWER:v[0-9]+]];
// HOISTED-NEXT: [[STORAGE]] = 0.0;
// HOISTED-NEXT: try {
// HOISTED-NEXT: if (true) {
// HOISTED-NEXT: [[PAYLOAD]] = [[INPUT]] + 1.0;
// HOISTED-NEXT: throw ctnative::js_exception{[[PAYLOAD]]};
// HOISTED-NEXT: }{{$}}
// HOISTED-NEXT: } catch (ctnative::js_exception<js_num> const & [[EXCEPTION:ctn_exception_[0-9]+]]) {
// HOISTED-NEXT: js_num const [[CAUGHT:v[0-9]+]] = [[EXCEPTION]].value;
// HOISTED-NEXT: [[RECOVERED]] = [[CAUGHT]] + 2.0;
// HOISTED-NEXT: [[STORAGE]] = [[RECOVERED]];
// HOISTED-NEXT: }{{$}}
// HOISTED-NEXT: [[ANSWER]] = [[STORAGE]];
// HOISTED-NEXT: return [[ANSWER]];

// EMPTY-REGION: error: 'ctnative.cpp_try' op requires one block in each try and catch region
// EMPTY-BLOCK: error: {{.*(empty block|non-empty block|terminator).*}}
// CATCH: error: 'ctnative.cpp_try' op requires exactly one f64, i1 or owning std::string catch argument
// TRY-END: error: 'ctnative.cpp_try' op requires ctnative.cpp_try_end as each try and catch terminator
// PARENT: error: 'ctnative.cpp_try_end' op
// PARENT-SAME: 'ctnative.cpp_try'
// THROW-TYPE: error: 'ctnative.cpp_throw' op requires an f64, i1 or owning std::string payload
// THROW-FALLTHROUGH: error: 'ctnative.cpp_throw' op must be immediately followed by the enclosing region terminator

//--- valid.mlir
#namespace = loc("native-exceptions.js":1:1)
#input = loc("native-exceptions.js":1:2)
#caught = loc("native-exceptions.js":1:3)
#payload = loc("native-exceptions.js":2:1)
#recovered = loc("native-exceptions.js":3:1)
#storage = loc("native-exceptions.js":4:1)
#answer = loc("native-exceptions.js":5:1)
module attributes {ctnative.const_bindings, ctnative.numeric_alias} {
  emitc.include <"cmath">
  emitc.include <"stdexcept">
  emitc.verbatim "using js_num = double;\0Anamespace ctnative { template <class T> struct js_exception { T value; }; }"
  emitc.verbatim "void raise_foreign() { throw std::runtime_error(\22foreign\22); }"
  emitc.verbatim "void mutate_number(js_num & value) { value += 2.0; }"
  emitc.func @throw_number(%number: f64 loc(#namespace)) {
    ctnative.cpp_throw %number : f64
    emitc.return
  }
  emitc.func @caught_value(%input: f64 loc(#input)) -> f64 {
    %storage = "emitc.variable"() {value = #emitc.opaque<"0.0">} : () -> !emitc.lvalue<f64> loc(#storage)
    ctnative.cpp_try {
      %yes = emitc.literal "true" : i1
      emitc.if %yes {
        %one = emitc.literal "1.0" : f64
        %payload = emitc.add %input, %one : (f64, f64) -> f64 loc(#payload)
        ctnative.cpp_throw %payload : f64
      }
      ctnative.cpp_try_end
    } catch {
    ^bb0(%caught: f64 loc(#caught)):
      %two = emitc.literal "2.0" : f64
      %recovered = emitc.add %caught, %two : (f64, f64) -> f64 loc(#recovered)
      emitc.assign %recovered : f64 to %storage : !emitc.lvalue<f64>
      ctnative.cpp_try_end
    }
    %answer = emitc.load %storage : !emitc.lvalue<f64> loc(#answer)
    emitc.return %answer : f64
  }
  emitc.func @rethrow_value(%input: f64) -> f64 {
    %storage = "emitc.variable"() {value = #emitc.opaque<"0.0">} : () -> !emitc.lvalue<f64>
    ctnative.cpp_try {
      ctnative.cpp_try {
        ctnative.cpp_throw %input : f64
        ctnative.cpp_try_end
      } catch {
      ^bb0(%inner: f64):
        %one = emitc.literal "1.0" : f64
        %next = emitc.add %inner, %one : (f64, f64) -> f64
        ctnative.cpp_throw %next : f64
        ctnative.cpp_try_end
      }
      ctnative.cpp_try_end
    } catch {
    ^bb0(%outer: f64):
      emitc.assign %outer : f64 to %storage : !emitc.lvalue<f64>
      ctnative.cpp_try_end
    }
    %answer = emitc.load %storage : !emitc.lvalue<f64>
    emitc.return %answer : f64
  }
  // Catch copies use the same binding-mutability analysis as other SSA values.
  // An opaque reference-taking ABI may mutate the copy, never the exception.
  emitc.func @mutated_catch(%input: f64) -> f64 {
    %storage = "emitc.variable"() {value = #emitc.opaque<"0.0">} : () -> !emitc.lvalue<f64>
    ctnative.cpp_try {
      ctnative.cpp_throw %input : f64
      ctnative.cpp_try_end
    } catch {
    ^bb0(%caught: f64):
      emitc.call_opaque "mutate_number"(%caught) : (f64) -> ()
      emitc.assign %caught : f64 to %storage : !emitc.lvalue<f64>
      ctnative.cpp_try_end
    }
    %answer = emitc.load %storage : !emitc.lvalue<f64>
    emitc.return %answer : f64
  }
  emitc.func @foreign_boundary() -> f64 {
    %storage = "emitc.variable"() {value = #emitc.opaque<"0.0">} : () -> !emitc.lvalue<f64>
    ctnative.cpp_try {
      emitc.call_opaque "raise_foreign"() : () -> ()
      ctnative.cpp_try_end
    } catch {
    ^bb0(%unused: f64):
      %one = emitc.literal "1.0" : f64
      emitc.assign %one : f64 to %storage : !emitc.lvalue<f64>
      ctnative.cpp_try_end
    }
    %answer = emitc.load %storage : !emitc.lvalue<f64>
    emitc.return %answer : f64
  }
  emitc.verbatim "int main() {\0A  if (caught_value(39.0) != 42.0) return 1;\0A  if (rethrow_value(7.0) != 8.0) return 2;\0A  bool caught = false;\0A  try { throw_number(-0.0); } catch (ctnative::js_exception<js_num> const & e) { caught = std::signbit(e.value); }\0A  if (!caught) return 3;\0A  if (mutated_catch(40.0) != 42.0) return 5;\0A  try { foreign_boundary(); } catch (std::runtime_error const &) { return 0; }\0A  return 4;\0A}"
}

//--- empty-region.mlir
module {
  "ctnative.cpp_try"() ({}, {}) : () -> ()
}

//--- empty-block.mlir
module {
  "ctnative.cpp_try"() ({
  ^bb0:
  }, {
  ^bb0(%caught: f64):
    "ctnative.cpp_try_end"() : () -> ()
  }) : () -> ()
}

//--- catch-arity.mlir
module {
  ctnative.cpp_try {
    ctnative.cpp_try_end
  } catch {
    ctnative.cpp_try_end
  }
}

//--- catch-type.mlir
module {
  ctnative.cpp_try {
    ctnative.cpp_try_end
  } catch {
  ^bb0(%caught: f32):
    ctnative.cpp_try_end
  }
}

//--- unterminated.mlir
module {
  ctnative.cpp_try {
    %value = emitc.literal "1.0" : f64
  } catch {
  ^bb0(%caught: f64):
    ctnative.cpp_try_end
  }
}

//--- wrong-parent.mlir
module {
  emitc.func @wrong_parent() {
    ctnative.cpp_try_end
  }
}

//--- wrong-terminator.mlir
// A valid terminator from another dialect is not native try completion.
module {
  ctnative.cpp_try {
    %value = ctjs.constant #ctjs.number<4631107791820423168>
    ctjs.throw %value
  } catch {
  ^bb0(%caught: f64):
    ctnative.cpp_try_end
  }
}

//--- throw-type.mlir
module {
  emitc.func @wrong_type(%value: f32) {
    ctnative.cpp_throw %value : f32
    emitc.return
  }
}

//--- throw-fallthrough.mlir
module {
  emitc.func @wrong_fallthrough(%value: f64) {
    ctnative.cpp_throw %value : f64
    %later = emitc.literal "1.0" : f64
    emitc.return
  }
}
