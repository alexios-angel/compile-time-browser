// RUN: split-file %s %t
// RUN: ctjs-opt %t/literals.mlir --ctnative-precompute | FileCheck %s --check-prefix=LITERALS
// RUN: ctjs-opt %t/domains.mlir --ctnative-precompute | FileCheck %s --check-prefix=DOMAINS
// RUN: ctjs-opt %t/effects.mlir --ctnative-precompute | FileCheck %s --check-prefix=EFFECTS
// RUN: ctjs-opt %t/effects.mlir --ctnative-precompute --ctnative-precompute | FileCheck %s --check-prefix=EFFECTS
// RUN: ctjs-opt %t/unknown.mlir --ctnative-precompute | FileCheck %s --check-prefix=UNKNOWN
// RUN: ctjs-opt %t/literals.mlir '--ctnative-precompute=max-steps=0' | FileCheck %s --check-prefix=BUDGET
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../native-symbolic-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-precompute | FileCheck %s --check-prefix=SOURCE

// The source fixture also has an independent native/reference pipeline. Calls
// remain even when every consumer of their literal or boolean result folds.
// SOURCE-LABEL: ctjs.func private @symbolicBoolean$2
// SOURCE: ctjs.call_direct @recordBoolean$1
// SOURCE-NOT: ctjs.compare strict_eq
// SOURCE-NOT: scf.if
// SOURCE: ctjs.return
// SOURCE-LABEL: ctjs.func private @resultOfEffect$4
// SOURCE: ctjs.call_direct @recordForty$3
// SOURCE-NOT: ctjs.binary
// SOURCE: ctjs.return
// SOURCE-LABEL: ctjs.func private @selectedEffects$6
// SOURCE-NOT: scf.if
// SOURCE: ctjs.call_direct @recordForty$3
// SOURCE: ctjs.call_direct @recordForty$3
// SOURCE-NOT: ctjs.call_direct
// SOURCE: ctjs.return
// SOURCE-LABEL: ctjs.func private @residualArithmetic$9
// SOURCE: ctjs.unary plus
// SOURCE: ctjs.binary add
// SOURCE: ctjs.binary mul
// SOURCE: ctjs.compare strict_eq
// SOURCE-LABEL: ctjs.func private @runtimeBranch$10
// SOURCE: scf.if

//--- literals.mlir
module {
  // The VM NaN-boxing tag collision still denotes a number at this boundary.
  // LITERALS-LABEL: ctjs.func @nan
  // LITERALS-NOT: ctjs.compare
  // LITERALS: %[[FALSE:.*]] = ctjs.constant #ctjs.boolean<false>
  // LITERALS: ctjs.return %[[FALSE]]
  // BUDGET: budget_exhausted = true
  // BUDGET-LABEL: ctjs.func @nan
  // BUDGET: ctjs.compare strict_eq
  ctjs.func @nan(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %nan = ctjs.constant #ctjs.number<9222246136947933187>
    %equal = ctjs.compare strict_eq %nan, %nan
    ctjs.return %equal
  }
  // -0 + +0 is +0. Treating x + 0 as identity would leave negative zero.
  // LITERALS-LABEL: ctjs.func @zero
  // LITERALS-NOT: ctjs.binary
  // LITERALS: ctjs.constant #ctjs.number<0>
  // LITERALS: %[[ZERO:.*]] = ctjs.constant #ctjs.number<0>
  // LITERALS: ctjs.return %[[ZERO]]
  ctjs.func @zero(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %negative = ctjs.constant #ctjs.number<9223372036854775808>
    %positive = ctjs.constant #ctjs.number<0>
    %sum = ctjs.binary add %negative, %positive
    ctjs.return %sum
  }
  // LITERALS-LABEL: ctjs.func @infinity
  // LITERALS-NOT: ctjs.binary
  // LITERALS: %[[INFINITY:.*]] = ctjs.constant #ctjs.number<18442240474082181120>
  // LITERALS: ctjs.return %[[INFINITY]]
  ctjs.func @infinity(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %negative = ctjs.constant #ctjs.number<9223372036854775808>
    %result = ctjs.binary div %one, %negative
    ctjs.return %result
  }
  // LITERALS-LABEL: ctjs.func @coercion
  // LITERALS-NOT: ctjs.binary
  // LITERALS-NOT: ctjs.compare
  // LITERALS: %[[TRUE:.*]] = ctjs.constant #ctjs.boolean<true>
  // LITERALS: ctjs.return %[[TRUE]]
  ctjs.func @coercion(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %text = ctjs.constant #ctjs.string<"40">
    %two = ctjs.constant #ctjs.number<4611686018427387904>
    %expected = ctjs.constant #ctjs.string<"402">
    %sum = ctjs.binary add %text, %two
    %equal = ctjs.compare strict_eq %sum, %expected
    ctjs.return %equal
  }
}

//--- domains.mlir
module {
  // Relational comparison may invoke unknown object coercions. Its boolean
  // result excludes NaN, but folding its consumer must preserve that producer.
  // DOMAINS-LABEL: ctjs.func @boolean
  // DOMAINS: ctjs.compare lt {{.*}}ctnative.symbolic_results = ["boolean"]
  // DOMAINS-NOT: ctjs.compare strict_eq
  // DOMAINS: %[[TRUE:.*]] = ctjs.constant #ctjs.boolean<true>
  // DOMAINS: ctjs.return %[[TRUE]]
  ctjs.func @boolean(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %left: !ctjs.value, %right: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %flag = ctjs.compare lt %left, %right
    %same = ctjs.compare strict_eq %flag, %flag
    ctjs.return %same
  }
  // DOMAINS-LABEL: ctjs.func @string
  // DOMAINS: ctjs.unary typeof {{.*}}ctnative.symbolic_results = ["string"]
  // DOMAINS-NOT: ctjs.compare
  // DOMAINS: ctjs.constant #ctjs.boolean<true>
  // DOMAINS: ctjs.return
  ctjs.func @string(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %kind = ctjs.unary typeof %input
    %same = ctjs.compare strict_eq %kind, %kind
    ctjs.return %same
  }
  // A numeric result can still be NaN. Forged boolean facts are ignored.
  // DOMAINS-LABEL: ctjs.func @number
  // DOMAINS: ctjs.unary plus {{.*}}ctnative.symbolic_results = ["number"]
  // DOMAINS: ctjs.binary add
  // DOMAINS: ctjs.binary mul
  // DOMAINS: ctjs.compare strict_eq
  // DOMAINS-NOT: forged
  ctjs.func @number(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32, ctnative.precompute_summary = "forged"} {
    %number = ctjs.unary plus %input {ctnative.symbolic_results = ["boolean"]}
    %zero = ctjs.constant #ctjs.number<0>
    %sum = ctjs.binary add %number, %zero
    %product = ctjs.binary mul %sum, %zero
    %same = ctjs.compare strict_eq %product, %product
    ctjs.return %same
  }
  // Both possible primitive tags exclude NaN; the condition is still runtime.
  // DOMAINS-LABEL: ctjs.func @nullish
  // DOMAINS: scf.if
  // DOMAINS-NOT: ctjs.compare
  // DOMAINS: ctjs.constant #ctjs.boolean<true>
  // DOMAINS: ctjs.return
  ctjs.func @nullish(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %take = ctjs.truthy %input
    %value = scf.if %take -> !ctjs.value {
      %null = ctjs.constant #ctjs.null
      scf.yield %null : !ctjs.value
    } else {
      %undefined = ctjs.constant #ctjs.undefined
      scf.yield %undefined : !ctjs.value
    }
    %same = ctjs.compare strict_eq %value, %value
    ctjs.return %same
  }
}

//--- effects.mlir
module {
  // The same known return at two call sites does not remove either effect.
  // The selected arm and trailing call retain their original order once.
  // EFFECTS-LABEL: ctjs.func @consumer
  // EFFECTS: ctjs.call_direct @effect
  // EFFECTS-NOT: ctjs.binary
  // EFFECTS-NOT: scf.if
  // EFFECTS: ctjs.store_global "selected"
  // EFFECTS-NOT: rejected
  // EFFECTS: ctjs.call_direct @effect
  // EFFECTS-NOT: ctjs.call_direct
  // EFFECTS: ctjs.return
  ctjs.func @consumer(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %first = ctjs.call_direct @effect(%u, %u, %u, %runtime)
    %two = ctjs.constant #ctjs.number<4611686018427387904>
    %answer = ctjs.binary add %first, %two
    %expected = ctjs.constant #ctjs.number<4631107791820423168>
    %same = ctjs.compare strict_eq %answer, %expected
    %take = ctjs.truthy %same
    scf.if %take {
      ctjs.store_global "selected", %answer
    } else {
      ctjs.store_global "rejected", %answer
    }
    %second = ctjs.call_direct @effect(%u, %u, %u, %runtime)
    %unused = ctjs.unary void %second
    ctjs.return %answer
  }
  // EFFECTS-LABEL: ctjs.func private @effect
  // EFFECTS: ctjs.store_global "trace", %arg3
  ctjs.func private @effect(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    ctjs.store_global "trace", %runtime
    %forty = ctjs.constant #ctjs.number<4630826316843712512>
    ctjs.return %forty
  }
}

//--- unknown.mlir
module {
  // Caller constants are not facts about the generic callee's arguments.
  // UNKNOWN-LABEL: ctjs.func private @identity
  // UNKNOWN: ctjs.return %arg3
  ctjs.func private @identity(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    ctjs.return %input
  }
  // UNKNOWN-LABEL: ctjs.func @unknown
  // UNKNOWN: ctjs.call_direct @identity
  // UNKNOWN: ctjs.call_direct @identity
  // UNKNOWN: ctjs.compare strict_eq
  // UNKNOWN: ctjs.binary add
  // UNKNOWN: scf.if
  // UNKNOWN: ctjs.store_global "left"
  // UNKNOWN: ctjs.store_global "right"
  ctjs.func @unknown(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %two = ctjs.constant #ctjs.number<4611686018427387904>
    %first = ctjs.call_direct @identity(%u, %u, %u, %one)
    %second = ctjs.call_direct @identity(%u, %u, %u, %two)
    %same = ctjs.compare strict_eq %first, %second
    %generic = ctjs.binary add %input, %one
    %take = ctjs.truthy %same
    scf.if %take {
      ctjs.store_global "left", %generic
    } else {
      ctjs.store_global "right", %generic
    }
    ctjs.return %generic
  }
  // Recursive return dependencies cannot manufacture a literal/domain fact.
  // UNKNOWN-LABEL: ctjs.func private @recursive
  // UNKNOWN: ctjs.call_direct @recursive
  // UNKNOWN-LABEL: ctjs.func @recursive_consumer
  // UNKNOWN: ctjs.call_direct @recursive
  // UNKNOWN: ctjs.compare strict_eq
  ctjs.func private @recursive(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %result = ctjs.call_direct @recursive(%u, %u, %u)
    ctjs.return %result
  }
  ctjs.func @recursive_consumer(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %result = ctjs.call_direct @recursive(%u, %u, %u)
    %same = ctjs.compare strict_eq %result, %result
    ctjs.return %same
  }
  // Ordinary return summaries do not establish constructor-normalized results.
  // UNKNOWN-LABEL: ctjs.func @constructor_context
  // UNKNOWN: ctjs.call_direct @constant
  // UNKNOWN: ctjs.compare strict_eq
  ctjs.func @constructor_context(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %result = ctjs.call_direct @constant(%u, %target, %u)
    %same = ctjs.compare strict_eq %result, %result
    ctjs.return %same
  }
  ctjs.func private @constant(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.return %one
  }
}
