// RUN: split-file %s %t
// RUN: ctjs-opt %t/memo.mlir --ctnative-specialize | FileCheck %s --check-prefix=MEMO
// RUN: ctjs-opt %t/memo.mlir --ctnative-specialize --ctnative-specialize | FileCheck %s --check-prefix=REPEAT
// RUN: ctjs-opt %t/memo.mlir '--ctnative-specialize=max-variants=0' | FileCheck %s --check-prefix=BUDGET --implicit-check-not='ctjs.func private @scale__specialized'
// RUN: ctjs-opt %t/memo.mlir '--ctnative-specialize=max-cloned-ops=1' | FileCheck %s --check-prefix=BUDGET --implicit-check-not='ctjs.func private @scale__specialized'
// RUN: ctjs-opt %t/zeros.mlir --ctnative-specialize | FileCheck %s --check-prefix=ZEROS
// RUN: ctjs-opt %t/zeros.mlir '--ctnative-specialize=max-variants=1' | FileCheck %s --check-prefix=LIMIT
// RUN: ctjs-opt %t/recursive.mlir --ctnative-specialize | FileCheck %s --check-prefix=RECURSIVE
// RUN: ctjs-opt %t/effects.mlir --ctnative-specialize | FileCheck %s --check-prefix=EFFECT
// RUN: ctjs-opt %t/observe.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=OBSERVE
// RUN: ctjs-opt %t/capture.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=CAPTURE
// RUN: ctjs-opt %t/alternate-direct.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=DIRECT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../native-specialization-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-specialize | FileCheck %s --check-prefix=SOURCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../specialization-dispatch.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-specialize --ctnative-partial-evaluate | FileCheck %s --check-prefix=DISPATCH
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../specialization-dispatch.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-specialize --ctnative-partial-evaluate --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func

// SOURCE-LABEL: ctjs.func @_script_$0
// SOURCE: %[[GENERIC:[0-9]+]] = ctjs.load_global "mixed"
// SOURCE: ctjs.call_direct @mixed$2({{.*}}, %[[GENERIC]],
// SOURCE: %[[ORIGINAL:[0-9]+]] = ctjs.load_global "mixed"
// SOURCE: ctjs.call_direct @mixed$2__specialized({{.*}}, %[[ORIGINAL]],
// SOURCE-LABEL: ctjs.func private @recurse$11
// SOURCE-SAME: ctnative.specialization_reason = "recursive call graph remains generic"
// The original body still serves both value-dispatched tuples. Its heap
// prefix may simplify, but its input-dependent arithmetic must stay dynamic.
// DISPATCH-LABEL: ctjs.func private @formula$1(
// DISPATCH: ctjs.binary mul %arg3,
// DISPATCH: ctjs.store_global "effect", %arg3
// DISPATCH-LABEL: ctjs.func private @formula$1__specialized(
// DISPATCH-SAME: ctnative.partial_evaluated =
// NATIVE: emitc.func @formula_1(

//--- memo.mlir
module {
  // MEMO: ctnative.specialization_summary = {cloned_ops = {{[0-9]+}} : i64, limited_calls = 0 : i64, redirected_calls = 2 : i64, variants = 1 : i64}
  // REPEAT: ctnative.specialization_summary = {cloned_ops = 0 : i64, limited_calls = 0 : i64, redirected_calls = 0 : i64, variants = 0 : i64}
  // BUDGET: ctnative.specialization_summary = {cloned_ops = 0 : i64, limited_calls = 2 : i64, redirected_calls = 0 : i64, variants = 0 : i64}
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %two = ctjs.constant #ctjs.number<4611686018427387904>
    // MEMO: ctjs.call_direct @scale(
    %generic = ctjs.call_direct @scale(%u, %u, %u, %one, %input)
    // MEMO: ctjs.call_direct @scale__specialized(
    // BUDGET: ctjs.call_direct @scale({{.*}}ctnative.specialization_reason = "specialization budget exhausted"
    %first = ctjs.call_direct @scale(%u, %u, %u, %two, %input)
    // MEMO: ctjs.call_direct @scale__specialized(
    // BUDGET: ctjs.call_direct @scale({{.*}}ctnative.specialization_reason = "specialization budget exhausted"
    %again = ctjs.call_direct @scale(%u, %u, %u, %two, %input)
    ctjs.return %again
  }
  ctjs.func private @scale(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %factor: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %result = ctjs.binary mul %factor, %input
    ctjs.return %result
  }
  // MEMO-LABEL: ctjs.func private @scale__specialized(
  // MEMO-SAME: ctnative.specialized_arguments = {"3" = #ctjs.number<4611686018427387904>}
  // MEMO: %[[FACTOR:[0-9]+]] = ctjs.constant #ctjs.number<4611686018427387904>
  // MEMO: ctjs.binary mul %[[FACTOR]], %arg4
  // REPEAT-COUNT-1: ctjs.func private @scale__specialized(
  // REPEAT-NOT: ctjs.func private @scale__specialized
}

//--- zeros.mlir
module {
  // ZEROS: redirected_calls = 3 : i64, variants = 2 : i64
  // LIMIT: limited_calls = 1 : i64, redirected_calls = 2 : i64, variants = 1 : i64
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %positive = ctjs.constant #ctjs.number<0>
    %negative = ctjs.constant #ctjs.number<9223372036854775808>
    %generic = ctjs.call_direct @combine(%u, %u, %u, %one, %input)
    %first = ctjs.call_direct @combine(%u, %u, %u, %positive, %input)
    %second = ctjs.call_direct @combine(%u, %u, %u, %negative, %input)
    %again = ctjs.call_direct @combine(%u, %u, %u, %positive, %input)
    ctjs.return %again
  }
  ctjs.func private @combine(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %seed: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %result = ctjs.binary add %seed, %input
    ctjs.return %result
  }
  // ZEROS: ctnative.specialized_arguments = {"3" = #ctjs.number<0>}
  // ZEROS: ctnative.specialized_arguments = {"3" = #ctjs.number<9223372036854775808>}
}

//--- recursive.mlir
module {
  // RECURSIVE: redirected_calls = 0 : i64, variants = 0 : i64
  // RECURSIVE-LABEL: ctjs.func private @recursive(
  // RECURSIVE-SAME: ctnative.specialization_reason = "recursive call graph remains generic"
  ctjs.func private @recursive(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %result = ctjs.call_direct @recursive(%u, %u, %u, %input)
    ctjs.return %result
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %first = ctjs.call_direct @recursive(%u, %u, %u, %one)
    %again = ctjs.call_direct @recursive(%u, %u, %u, %one)
    ctjs.return %again
  }
}

//--- effects.mlir
module {
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %two = ctjs.constant #ctjs.number<4611686018427387904>
    %generic = ctjs.call_direct @scale(%u, %u, %u, %one, %input)
    // EFFECT: %[[PRODUCED:[0-9]+]] = ctjs.call_direct @record(
    // EFFECT-NEXT: {{.*}}ctjs.call_direct @scale__specialized({{.*}}, %[[PRODUCED]])
    %produced = ctjs.call_direct @record(%u, %u, %u, %input)
    %result = ctjs.call_direct @scale(%u, %u, %u, %two, %produced)
    ctjs.return %result
  }
  ctjs.func private @scale(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %factor: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    ctjs.store_global "effect", %input
    %result = ctjs.binary mul %factor, %input
    ctjs.return %result
  }
  ctjs.func private @record(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    ctjs.store_global "argument", %input
    ctjs.return %input
  }
  // EFFECT-LABEL: ctjs.func private @scale__specialized(
  // EFFECT: ctjs.store_global "effect", %arg4
  // EFFECT: ctjs.binary mul {{.*}}, %arg4
}

//--- observe.mlir
module {
  ctjs.func @_script_$0(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %closure = ctjs.create_closure %callee[1] this %u
    ctjs.store_global "factory", %closure
    %load = ctjs.load_global "factory"
    %generic = ctjs.call_direct @factory$1(%u, %u, %load)
    %other = ctjs.call_direct @observe(%u, %u, %load)
    ctjs.return %generic
  }
  // OBSERVE-LABEL: ctjs.func private @factory$1(
  // OBSERVE-SAME: ctnative.partial_eval_reason = "function closure escapes through `ctjs.store_global`"
  ctjs.func private @factory$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.return %one
  }
  ctjs.func private @observe(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32, ctnative.specialized_from = "factory$1", ctnative.specialized_arguments = {}} {
    %kind = ctjs.unary typeof %callee
    ctjs.return %kind
  }
}

//--- capture.mlir
module {
  ctjs.func @_script_$0(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %closure = ctjs.create_closure %callee[1] this %u
    ctjs.store_global "factory", %closure
    %load = ctjs.load_global "factory"
    %generic = ctjs.call_direct @factory$1(%u, %u, %load)
    %other = ctjs.call_direct @capture(%u, %u, %load)
    ctjs.return %generic
  }
  // CAPTURE-LABEL: ctjs.func private @factory$1(
  // CAPTURE-SAME: ctnative.partial_eval_reason = "function closure escapes through `ctjs.store_global`"
  ctjs.func private @factory$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.return %one
  }
  ctjs.func private @capture(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32, ctnative.specialized_from = "factory$1", ctnative.specialized_arguments = {}} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.return %one
  }
}

//--- alternate-direct.mlir
module {
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %closure = ctjs.create_closure %callee[1] this %u
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %two = ctjs.constant #ctjs.number<4611686018427387904>
    %first = ctjs.call_direct @formula$1(%u, %u, %closure, %one)
    %second = ctjs.call_direct @variant(%u, %u, %closure, %two)
    ctjs.return %second
  }
  // DIRECT-LABEL: ctjs.func private @formula$1(
  // DIRECT: ctjs.binary mul %arg3,
  ctjs.func private @formula$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %ten = ctjs.constant #ctjs.number<4621819117588971520>
    %value = ctjs.binary mul %input, %ten
    ctjs.return %value
  }
  ctjs.func private @variant(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %twenty = ctjs.constant #ctjs.number<4626322717216342016>
    ctjs.return %twenty
  }
}
