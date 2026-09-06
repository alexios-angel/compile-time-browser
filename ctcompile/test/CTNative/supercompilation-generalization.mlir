// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../native-supercompilation-generalization-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf -o %t/source.mlir
// RUN: ctjs-opt %t/source.mlir --ctnative-supercompile -o %t/driven.mlir
// RUN: FileCheck %s --check-prefix=SUMMARY --input-file=%t/driven.mlir
// RUN: FileCheck %s --check-prefix=DRIVEN --input-file=%t/driven.mlir
// RUN: ctjs-opt %t/driven.mlir --ctnative-supercompile='report=true' --mlir-print-op-on-diagnostic=false -o %t/repeated.mlir 2>&1 | FileCheck %s --check-prefix=REPEAT
// RUN: FileCheck %s --check-prefix=DRIVEN --input-file=%t/repeated.mlir
// RUN: ctjs-opt %t/source.mlir --ctnative-supercompile='max-contexts=1' | FileCheck %s --check-prefix=CONTEXTS
// RUN: ctjs-opt %t/source.mlir --ctnative-supercompile='max-steps=50' | FileCheck %s --check-prefix=STEPS
// RUN: ctjs-opt %t/source.mlir --ctnative-supercompile='max-residual-ops=1' | FileCheck %s --check-prefix=SIZE
// RUN: ctjs-opt %t/source.mlir --ctnative-supercompile='max-growth=1' | FileCheck %s --check-prefix=SIZE
// RUN: ctjs-opt %t/exact.mlir --ctnative-supercompile | FileCheck %s --check-prefix=EXACT

// Known growing arguments must actually reach the whistle and generalize;
// merely failing to evaluate them and deriving a dynamic child is insufficient.
// SUMMARY: contexts = 14 : i64
// SUMMARY-SAME: generalizations = 9 : i64
// SUMMARY-SAME: kernels = 3 : i64
// SUMMARY-SAME: whistles = 9 : i64

// The caller evaluates all argument producers once in their original order.
// DRIVEN-LABEL: ctjs.func private @ordered$8
// DRIVEN: ctjs.call_direct @record$7
// DRIVEN: ctjs.call_direct @weighted$1__supercompiled_
// DRIVEN: ctjs.call_direct @record$7
// DRIVEN: ctjs.call_direct @weighted$1__supercompiled_3
// DRIVEN: ctjs.call_direct @record$7
// DRIVEN: ctjs.call_direct @weighted$1__supercompiled_

// The original exact entry retains its seed and calls a freshly driven child.
// A generalized numeric accumulator keeps the exact boolean mode and weight.
// DRIVEN-LABEL: ctjs.func private @weighted$1__supercompiled_0
// DRIVEN: ctjs.constant #ctjs.number<4621819117588971520>
// DRIVEN-NOT: ctjs.binary add %arg5,
// DRIVEN: ctjs.call_direct @weighted$1__supercompiled_1
// DRIVEN-LABEL: ctjs.func private @weighted$1__supercompiled_1
// DRIVEN: ctjs.constant #ctjs.number<4611686018427387904>
// DRIVEN-NOT: ctjs.truthy %arg3
// DRIVEN: scf.if
// DRIVEN: ctjs.binary add %arg5,
// DRIVEN: ctjs.call_direct @weighted$1__supercompiled_1
// DRIVEN-NOT: ctjs.call_direct @weighted$1(
// DRIVEN-LABEL: ctjs.func private @weighted$1__supercompiled_3
// DRIVEN: ctjs.constant #ctjs.number<4613937818241073152>
// DRIVEN-NOT: ctjs.truthy %arg3
// DRIVEN: ctjs.binary sub %arg5,
// DRIVEN: ctjs.call_direct @weighted$1__supercompiled_3
// Independent later seeds share the generalized child, retaining their own body.
// DRIVEN-LABEL: ctjs.func private @weighted$1__supercompiled_4
// DRIVEN: ctjs.call_direct @weighted$1__supercompiled_1

// String growth weakens only the changing text, not the stable suffix/mode.
// DRIVEN-LABEL: ctjs.func private @append$3__supercompiled_0
// DRIVEN: ctjs.call_direct @append$3__supercompiled_1
// DRIVEN-LABEL: ctjs.func private @append$3__supercompiled_1
// DRIVEN: ctjs.constant #ctjs.string<"ab">
// DRIVEN-NOT: ctjs.truthy %arg3
// DRIVEN: ctjs.binary add %arg5,
// DRIVEN: ctjs.call_direct @append$3__supercompiled_1
// DRIVEN-LABEL: ctjs.func private @append$3__supercompiled_3
// DRIVEN: ctjs.constant #ctjs.string<"!">
// DRIVEN-NOT: ctjs.truthy %arg3
// DRIVEN: ctjs.binary add {{%[0-9]+}}, %arg5
// DRIVEN: ctjs.call_direct @append$3__supercompiled_3

// An externally seeded dynamic configuration and a generalized child share the
// same promise/policy. Both growing and literal-reset edges keep the seed dynamic.
// DRIVEN-LABEL: ctjs.func private @reset$5__supercompiled_0
// DRIVEN-NOT: ctjs.truthy %arg3
// DRIVEN: ctjs.compare lt %arg4,
// DRIVEN: ctjs.call_direct @reset$5__supercompiled_0
// DRIVEN: ctjs.call_direct @reset$5__supercompiled_0
// DRIVEN-LABEL: ctjs.func private @reset$5__supercompiled_1
// DRIVEN: ctjs.call_direct @reset$5__supercompiled_0
// DRIVEN-NOT: __supercompiled_
// REPEAT: supercompilation: 0 kernel(s), 0 configuration(s), 0 fold(s), 0 whistle(s), 0 generalization(s)

// Reject the complete family after creating its first promise or after driving
// every generalized child. No original call or symbol may be partly published.
// CONTEXTS: contexts = 0 : i64
// CONTEXTS-SAME: generalizations = 0 : i64
// CONTEXTS: configuration budget exhausted; retained identity alternative
// CONTEXTS-NOT: __supercompiled_
// STEPS: contexts = 0 : i64
// STEPS-SAME: generalizations = 0 : i64
// STEPS-SAME: steps = 50 : i64
// STEPS: driving budget exhausted; retained identity alternative
// STEPS-NOT: __supercompiled_
// SIZE: contexts = 0 : i64
// SIZE-SAME: generalizations = 0 : i64
// SIZE: residual size budget exhausted; retained identity alternative
// SIZE-NOT: __supercompiled_

//--- exact.mlir
// A changed bit pattern must be generalized, even for signed zero or NaN. The
// new body must return its runtime value; retaining the ancestor's return would
// silently substitute +0 for -0 or one NaN payload for another.
// EXACT: contexts = 4 : i64
// EXACT-SAME: generalizations = 2 : i64
module {
  ctjs.func private @zero$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %mode: !ctjs.value, %value: !ctjs.value, %count: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %test = ctjs.truthy %mode
    %r = scf.if %test -> !ctjs.value {
      %more = ctjs.truthy %count
      %result = scf.if %more -> !ctjs.value {
        %one = ctjs.constant #ctjs.number<4607182418800017408>
        %next = ctjs.binary sub %count, %one
        %negative = ctjs.constant #ctjs.number<9223372036854775808>
        %again = ctjs.call_direct @zero$1(%u, %u, %u, %mode, %negative, %next)
        scf.yield %again : !ctjs.value
      } else {
        scf.yield %value : !ctjs.value
      }
      scf.yield %result : !ctjs.value
    } else {
      scf.yield %value : !ctjs.value
    }
    ctjs.return %r
  }
  ctjs.func private @nan$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %mode: !ctjs.value, %value: !ctjs.value, %count: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %test = ctjs.truthy %mode
    %r = scf.if %test -> !ctjs.value {
      %more = ctjs.truthy %count
      %result = scf.if %more -> !ctjs.value {
        %one = ctjs.constant #ctjs.number<4607182418800017408>
        %next = ctjs.binary sub %count, %one
        %payload = ctjs.constant #ctjs.number<9221120237041090561>
        %again = ctjs.call_direct @nan$2(%u, %u, %u, %mode, %payload, %next)
        scf.yield %again : !ctjs.value
      } else {
        scf.yield %value : !ctjs.value
      }
      scf.yield %result : !ctjs.value
    } else {
      scf.yield %value : !ctjs.value
    }
    ctjs.return %r
  }
  ctjs.func @entry$0(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %count: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %true = ctjs.constant #ctjs.boolean<true>
    %zero = ctjs.constant #ctjs.number<0>
    %nan = ctjs.constant #ctjs.number<9221120237041090560>
    %genericZero = ctjs.call_direct @zero$1(%u, %u, %u, %true, %zero, %count)
    %a = ctjs.call_direct @zero$1(%u, %u, %u, %true, %zero, %count)
    %genericNaN = ctjs.call_direct @nan$2(%u, %u, %u, %true, %nan, %count)
    %b = ctjs.call_direct @nan$2(%u, %u, %u, %true, %nan, %count)
    ctjs.return %b
  }
  // EXACT-LABEL: ctjs.func private @zero$1__supercompiled_0
  // EXACT: ctjs.constant #ctjs.number<0>
  // EXACT: ctjs.call_direct @zero$1__supercompiled_1
  // EXACT-LABEL: ctjs.func private @zero$1__supercompiled_1
  // EXACT-NOT: ctjs.constant #ctjs.number<0>
  // EXACT: ctjs.call_direct @zero$1__supercompiled_1
  // EXACT: scf.yield %arg4 : !ctjs.value
  // EXACT-LABEL: ctjs.func private @nan$2__supercompiled_0
  // EXACT: ctjs.constant #ctjs.number<9221120237041090560>
  // EXACT: ctjs.call_direct @nan$2__supercompiled_1
  // EXACT-LABEL: ctjs.func private @nan$2__supercompiled_1
  // EXACT-NOT: ctjs.constant #ctjs.number<9221120237041090560>
  // EXACT: ctjs.call_direct @nan$2__supercompiled_1
  // EXACT: scf.yield %arg4 : !ctjs.value
}
