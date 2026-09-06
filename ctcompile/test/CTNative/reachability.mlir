// RUN: split-file %s %t
// RUN: ctjs-opt %t/roots.mlir --ctnative-prune-unreachable | FileCheck %s --check-prefix=ROOTS --implicit-check-not='ctjs.func private @dead'
// RUN: ctjs-opt %t/roots.mlir --ctnative-prune-unreachable --ctnative-prune-unreachable | FileCheck %s --check-prefix=REPEAT --implicit-check-not='ctjs.func private @dead'
// RUN: ctjs-opt %t/roots.mlir '--ctnative-prune-unreachable=max-steps=0' | FileCheck %s --check-prefix=BUDGET
// RUN: ctjs-opt %t/pe.mlir --ctnative-partial-evaluate --ctnative-prune-unreachable | FileCheck %s --check-prefix=PE --implicit-check-not='ctjs.func private @helper'
// RUN: ctjs-opt %t/boxed.mlir --ctnative-prune-unreachable | FileCheck %s --check-prefix=BOXED --implicit-check-not='ctjs.func private @orphan'
// RUN: ctjs-opt %t/ambiguous.mlir --ctnative-prune-unreachable | FileCheck %s --check-prefix=AMBIGUOUS
// RUN: ctjs-opt %t/unknown-index.mlir --ctnative-prune-unreachable | FileCheck %s --check-prefix=INDEX
// RUN: ctjs-opt %t/unknown-symbol.mlir --ctnative-prune-unreachable | FileCheck %s --check-prefix=SYMBOL
// RUN: ctjs-opt %t/unknown-op.mlir --allow-unregistered-dialect --ctnative-prune-unreachable | FileCheck %s --check-prefix=UNKNOWN
// RUN: ctjs-opt %t/nested.mlir --ctnative-prune-unreachable | FileCheck %s --check-prefix=NESTED
// RUN: ctjs-opt %t/control.mlir --ctnative-prune-unreachable | FileCheck %s --check-prefix=CONTROL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../native-reachability-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-specialize --ctnative-partial-evaluate | FileCheck %s --check-prefix=BEFORE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../native-reachability-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-specialize --ctnative-partial-evaluate --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=UNPRUNED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../native-reachability-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-specialize --ctnative-partial-evaluate --ctnative-prune-unreachable | FileCheck %s --check-prefix=SOURCE --implicit-check-not='ctjs.func private @seed$1__specialized'
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../native-reachability-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-specialize --ctnative-partial-evaluate --ctnative-prune-unreachable --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func

// ROOTS: ctnative.reachability_summary = {removed = 3 : i64, retained = 8 : i64
// ROOTS: ctjs.func @entry
// ROOTS: ctjs.store_global "published"
// ROOTS: ctjs.func private @live
// ROOTS: ctjs.store_global "effect"
// ROOTS: ctjs.func private @numeric$1
// ROOTS: ctjs.func private @unused$2
// ROOTS: ctjs.func private @attribute_helper
// ROOTS: ctjs.func private @metadata
// ROOTS: ctjs.func private @external
// ROOTS: ctjs.func private @_script_$0
// REPEAT: ctnative.reachability_summary = {removed = 0 : i64, retained = 8 : i64
// BUDGET: ctnative.reachability_reason = "reachability scan budget exhausted"
// BUDGET: ctnative.reachability_summary = {removed = 0 : i64, retained = 11 : i64
// BUDGET: ctjs.func private @deadA
// BUDGET: ctjs.store_global "unobserved"
// BUDGET: ctjs.func private @deadB
// BUDGET: ctjs.func private @dead$3

// BEFORE-LABEL: ctjs.func private @initialize$3
// BEFORE-SAME: ctnative.partial_evaluated =
// BEFORE-NOT: ctjs.call_direct
// BEFORE: ctjs.return
// BEFORE: ctjs.func private @seed$1__specialized
// UNPRUNED: ctjs.func private @seed$1__specialized
// UNPRUNED-SAME: ctnative.not_native = "parameter 0 is
// UNPRUNED-SAME: no caller proves it
// SOURCE: ctnative.reachability_summary = {removed = 2 : i64, retained = 4 : i64
// SOURCE: ctjs.store_global "seed"
// SOURCE-LABEL: ctjs.func private @seed$1
// SOURCE-LABEL: ctjs.func private @runtime$2
// SOURCE: ctjs.store_global "effect"
// SOURCE: ctjs.call_direct @seed$1
// SOURCE-LABEL: ctjs.func private @initialize$3
// SOURCE-SAME: ctnative.partial_evaluated =
// NATIVE: emitc.func @seed_1
// NATIVE: emitc.func @runtime_2
// NATIVE: emitc.func @initialize_3

//--- roots.mlir
module attributes {ctnative.exports = {functions = [@metadata]}, ctnative.reachability_summary = {removed = 999 : i64}} {
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined {table = [@attribute_helper]}
    %result = ctjs.call_direct @live(%u, %u, %u)
    %published = ctjs.create_closure %callee[1] this %this
    ctjs.store_global "published", %published
    // Even an unused closure instruction is a retained numeric reference.
    %unused = ctjs.create_closure %callee[2] this %this
    ctjs.return %result
  }
  ctjs.func private @live(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32, ctnative.unreachable = true} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.store_global "effect", %u
    ctjs.return %u
  }
  ctjs.func private @numeric$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
  ctjs.func private @unused$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
  ctjs.func private @attribute_helper(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
  ctjs.func private @metadata(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
  ctjs.func private @external(!ctjs.value, !ctjs.value, !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32}
  // Bytecode entry identity is a root even if its visibility was changed.
  ctjs.func private @_script_$0(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
  // Neither effects nor an internal symbol/numeric cycle creates a root.
  ctjs.func private @deadA(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.store_global "unobserved", %u
    %r = ctjs.call_direct @deadB(%u, %u, %u)
    ctjs.return %r
  }
  ctjs.func private @deadB(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %closure = ctjs.create_closure %callee[3] this %this
    ctjs.return %closure
  }
  ctjs.func private @dead$3(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %r = ctjs.call_direct @deadA(%u, %u, %u)
    ctjs.return %r
  }
}

//--- pe.mlir
module {
  // PE: ctnative.reachability_summary = {removed = 1 : i64, retained = 2 : i64
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %result = ctjs.call_direct @initialize(%u, %u, %u)
    ctjs.return %result
  }
  // PE-LABEL: ctjs.func private @initialize
  // PE-SAME: ctnative.partial_evaluated =
  // PE-NOT: ctjs.call_direct
  // PE: ctjs.return
  ctjs.func private @initialize(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %result = ctjs.call_direct @helper(%u, %u, %u, %one)
    ctjs.return %result
  }
  ctjs.func private @helper(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %two = ctjs.constant #ctjs.number<4611686018427387904>
    %result = ctjs.binary mul %input, %two
    ctjs.return %result
  }
}

//--- boxed.mlir
module {
  // BOXED: removed = 1 : i64, retained = 3 : i64
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %original = ctjs.create_closure %callee[1] this %this
    ctjs.store_global "callable", %original
    %loaded = ctjs.load_global "callable"
    %result = ctjs.call_direct @variant(%u, %u, %loaded)
    ctjs.return %result
  }
  // No symbolic call names this body. Boxed dispatch still invokes it.
  // BOXED-LABEL: ctjs.func private @original$1
  // BOXED: ctjs.store_global "boxedEffect"
  ctjs.func private @original$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.store_global "boxedEffect", %one
    ctjs.return %one
  }
  // BOXED-LABEL: ctjs.func private @variant
  ctjs.func private @variant(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.return %one
  }
  ctjs.func private @orphan(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32, ctnative.specialized_from = "original$1"} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.return %one
  }
}

//--- ambiguous.mlir
module {
  // AMBIGUOUS: removed = 0 : i64, retained = 3 : i64
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %closure = ctjs.create_closure %callee[7] this %this
    ctjs.return %closure
  }
  // AMBIGUOUS: ctjs.func private @first$7
  ctjs.func private @first$7(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
  // AMBIGUOUS: ctjs.func private @second$7
  ctjs.func private @second$7(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
}

//--- unknown-index.mlir
module {
  // INDEX: ctnative.reachability_reason = "numeric closure reference has no visible CTJS target"
  // INDEX: removed = 0 : i64, retained = 2 : i64
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %closure = ctjs.create_closure %callee[999] this %this
    ctjs.return %closure
  }
  // INDEX: ctjs.func private @retained$1
  ctjs.func private @retained$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
}

//--- unknown-symbol.mlir
module attributes {ctnative.external_reference = @absent} {
  // SYMBOL: ctnative.reachability_reason = "symbol reference has no visible CTJS target"
  // SYMBOL: removed = 0 : i64, retained = 1 : i64
  // SYMBOL: ctjs.func private @retained
  ctjs.func private @retained(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
}

//--- unknown-op.mlir
module {
  // UNKNOWN: ctnative.reachability_reason = "operation has no CTJS reachability contract"
  // UNKNOWN: removed = 0 : i64, retained = 2 : i64
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    "mystery.invoke"() {target = "retained"} : () -> ()
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
  // UNKNOWN: ctjs.func private @retained
  ctjs.func private @retained(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
}

//--- nested.mlir
module {
  // NESTED: ctnative.reachability_reason = "nested symbol tables require a separate reachability proof"
  // NESTED: removed = 0 : i64, retained = 1 : i64
  module @scope {}
  // NESTED: ctjs.func private @retained
  ctjs.func private @retained(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
}

//--- control.mlir
module {
  // CONTROL: removed = 0 : i64, retained = 2 : i64
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %false = arith.constant false
    scf.if %false {
      %u = ctjs.constant #ctjs.undefined
      %result = ctjs.call_direct @effect(%u, %u, %u)
    }
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
  // CONTROL: ctjs.func private @effect
  // CONTROL: ctjs.store_global "observed"
  ctjs.func private @effect(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.store_global "observed", %u
    ctjs.return %u
  }
}
