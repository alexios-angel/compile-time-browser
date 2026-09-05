// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../native-supercompilation-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf -o %t/source.mlir
// RUN: FileCheck %s --check-prefix=OFF --input-file=%t/source.mlir
// RUN: ctjs-opt %t/source.mlir --ctnative-supercompile -o %t/driven.mlir
// RUN: FileCheck %s --check-prefix=DRIVEN --input-file=%t/driven.mlir
// RUN: ctjs-opt %t/driven.mlir --ctnative-supercompile='report=true' --mlir-print-op-on-diagnostic=false -o %t/repeated.mlir 2>&1 | FileCheck %s --check-prefix=REPEAT
// RUN: FileCheck %s --check-prefix=DRIVEN --input-file=%t/repeated.mlir
// RUN: ctjs-opt %t/source.mlir --ctnative-supercompile='max-contexts=0' | FileCheck %s --check-prefix=CONTEXTS
// RUN: ctjs-opt %t/source.mlir --ctnative-supercompile='max-steps=50' | FileCheck %s --check-prefix=STEPS
// RUN: ctjs-opt %t/source.mlir --ctnative-supercompile='max-residual-ops=1' | FileCheck %s --check-prefix=SIZE
// RUN: ctjs-opt %t/source.mlir --ctnative-supercompile='max-growth=1' | FileCheck %s --check-prefix=SIZE
// RUN: ctjs-opt %t/effects.mlir --ctnative-supercompile | FileCheck %s --check-prefix=EFFECTS
// RUN: ctjs-opt %t/exact.mlir --ctnative-supercompile | FileCheck %s --check-prefix=EXACT

// OFF-NOT: __supercompiled_
// OFF-NOT: ctnative.supercompile_summary
// OFF: ctjs.func private @modeSum$1
// OFF-NOT: __supercompiled_

// Exact promises precede the whistle: stable true/false modes each fold to a
// real recursive residual definition. Unknown count tests still execute.
// DRIVEN-LABEL: ctjs.func private @ordered$8
// DRIVEN: ctjs.call_direct @record$7
// DRIVEN: ctjs.call_direct @modeSum$1__supercompiled_1
// DRIVEN: ctjs.call_direct @record$7
// DRIVEN: ctjs.call_direct @modeSum$1__supercompiled_1
// DRIVEN-LABEL: ctjs.func private @modeSum$1__supercompiled_0
// DRIVEN-NOT: ctjs.truthy %arg3
// DRIVEN: scf.if
// DRIVEN: ctjs.call_direct @modeSum$1__supercompiled_0
// DRIVEN-LABEL: ctjs.func private @modeSum$1__supercompiled_1
// DRIVEN-NOT: ctjs.truthy %arg3
// DRIVEN: scf.if
// DRIVEN: ctjs.call_direct @modeSum$1__supercompiled_1
// DRIVEN-LABEL: ctjs.func private @alternate$3__supercompiled_0
// DRIVEN: ctjs.call_direct @alternate$3__supercompiled_1
// DRIVEN-LABEL: ctjs.func private @alternate$3__supercompiled_1
// DRIVEN: ctjs.call_direct @alternate$3__supercompiled_0
// The shape of 10 and 11 embeds, but they are not equivalent configurations.
// DRIVEN-LABEL: ctjs.func private @growing$5__supercompiled_0
// DRIVEN: ctjs.call_direct @growing$5(
// DRIVEN-NOT: __supercompiled_
// REPEAT: supercompilation: 0 kernel(s), 0 configuration(s), 0 fold(s)

// CONTEXTS: contexts = 0 : i64
// CONTEXTS: configuration budget exhausted; retained identity alternative
// CONTEXTS-NOT: __supercompiled_
// Work spent on a rejected family is charged before considering another one.
// STEPS: contexts = 0 : i64
// STEPS-SAME: steps = 50 : i64
// STEPS: driving budget exhausted; retained identity alternative
// STEPS-NOT: __supercompiled_
// SIZE: contexts = 0 : i64
// SIZE: residual size budget exhausted; retained identity alternative
// SIZE-NOT: __supercompiled_

//--- effects.mlir
module attributes {ctnative.supercompile_summary = {kernels = 99 : i64}} {
  // An optimization claim cannot erase runtime publication or license effects.
  // EFFECTS: kernels = 0 : i64
  // EFFECTS-LABEL: ctjs.func private @effect$1
  // EFFECTS-SAME: kernel retains unsupported operation `ctjs.store_global`
  // EFFECTS: ctjs.store_global "trace"
  // EFFECTS: ctjs.call_direct @effect$1
  // EFFECTS-NOT: __supercompiled_
  ctjs.func private @effect$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %mode: !ctjs.value, %x: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32, ctnative.supercompile_reason = "proved"} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.store_global "trace", %x
    %r = ctjs.call_direct @effect$1(%u, %u, %u, %mode, %x)
    ctjs.return %r
  }
  ctjs.func @entry$0(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %x: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %true = ctjs.constant #ctjs.boolean<true>
    %a = ctjs.call_direct @effect$1(%u, %u, %u, %true, %x)
    %b = ctjs.call_direct @effect$1(%u, %u, %u, %true, %x)
    ctjs.return %b
  }
}

//--- exact.mlir
// Exact keys must preserve numeric bits even though the whistle collapses all
// numbers. Each call still evaluates its dynamic operand; this infinite kernel
// is inspected, never executed. Folding does not turn divergence into a value.
module {
  ctjs.func private @bits$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %mode: !ctjs.value, %value: !ctjs.value, %x: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %test = ctjs.truthy %mode
    %r = scf.if %test -> !ctjs.value {
      %n = ctjs.binary add %x, %value
      %again = ctjs.call_direct @bits$1(%u, %u, %u, %mode, %value, %n)
      scf.yield %again : !ctjs.value
    } else {
      scf.yield %x : !ctjs.value
    }
    ctjs.return %r
  }
  // EXACT-LABEL: ctjs.func @entry$0
  // EXACT: ctjs.call_direct @bits$1(
  // EXACT: ctjs.call_direct @bits$1__supercompiled_0
  // EXACT: ctjs.call_direct @bits$1__supercompiled_1
  // EXACT: ctjs.call_direct @bits$1__supercompiled_2
  // EXACT: ctjs.call_direct @bits$1__supercompiled_3
  // EXACT: ctjs.call_direct @bits$1__supercompiled_3
  ctjs.func @entry$0(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %x: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %true = ctjs.constant #ctjs.boolean<true>
    %zero = ctjs.constant #ctjs.number<0>
    %negative = ctjs.constant #ctjs.number<9223372036854775808>
    %nan1 = ctjs.constant #ctjs.number<9221120237041090560>
    %nan2 = ctjs.constant #ctjs.number<9221120237041090561>
    %generic = ctjs.call_direct @bits$1(%u, %u, %u, %true, %zero, %x)
    %a = ctjs.call_direct @bits$1(%u, %u, %u, %true, %zero, %x)
    %b = ctjs.call_direct @bits$1(%u, %u, %u, %true, %negative, %x)
    %c = ctjs.call_direct @bits$1(%u, %u, %u, %true, %nan1, %x)
    %d = ctjs.call_direct @bits$1(%u, %u, %u, %true, %nan2, %x)
    %e = ctjs.call_direct @bits$1(%u, %u, %u, %true, %nan2, %x)
    ctjs.return %e
  }
  // EXACT-LABEL: ctjs.func private @bits$1__supercompiled_0
  // EXACT: ctjs.constant #ctjs.number<0>
  // EXACT-NOT: scf.if
  // EXACT: ctjs.call_direct @bits$1__supercompiled_0
  // EXACT-LABEL: ctjs.func private @bits$1__supercompiled_1
  // EXACT: ctjs.constant #ctjs.number<9223372036854775808>
  // EXACT: ctjs.call_direct @bits$1__supercompiled_1
  // EXACT-LABEL: ctjs.func private @bits$1__supercompiled_2
  // EXACT: ctjs.constant #ctjs.number<9221120237041090560>
  // EXACT: ctjs.call_direct @bits$1__supercompiled_2
  // EXACT-LABEL: ctjs.func private @bits$1__supercompiled_3
  // EXACT: ctjs.constant #ctjs.number<9221120237041090561>
  // EXACT: ctjs.call_direct @bits$1__supercompiled_3
  // EXACT-NOT: __supercompiled_4
}
