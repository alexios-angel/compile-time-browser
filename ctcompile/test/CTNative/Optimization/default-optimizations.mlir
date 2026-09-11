// The native entry owns the conservative defaults, even without a wrapper.
// Precomputation must run before reachability: selecting this branch removes
// the only edge to a private function. Without pruning, that newly uncalled
// body's parameters have no native proof; the unsimplified baseline has a caller.
// RUN: ctjs-opt %s --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DEFAULT --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func --implicit-check-not=ctnative.specialized_from --implicit-check-not=ctnative.partial_evaluated --implicit-check-not=ctnative.supercompile_summary
// RUN: ctjs-opt %s '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=BASELINE --implicit-check-not=ctnative.precompute_summary --implicit-check-not=ctnative.reachability_summary
// RUN: ctjs-opt %s '--ctnative-lower-to-emitc=precompute=false' | FileCheck %s --check-prefix=NO-PRECOMPUTE --implicit-check-not=ctnative.precompute_summary
// RUN: ctjs-opt %s '--ctnative-lower-to-emitc=prune-unreachable=false' | FileCheck %s --check-prefix=NO-PRUNE --implicit-check-not=ctnative.reachability_summary
// RUN: ctjs-opt %s '--ctnative-lower-to-emitc=precompute=false prune-unreachable=false' | FileCheck %s --check-prefix=BASELINE --implicit-check-not=ctnative.precompute_summary --implicit-check-not=ctnative.reachability_summary
// RUN: ctjs-opt %s --ctnative-precompute '--ctnative-lower-to-emitc=optimize=false' | FileCheck %s --check-prefix=NO-PRUNE --implicit-check-not=ctnative.reachability_summary
// RUN: ctjs-opt %s '--ctnative-lower-to-emitc=precompute-max-steps=0 reachability-max-steps=0' | FileCheck %s --check-prefix=BUDGET
// RUN: ctjs-opt %s '--ctnative-lower-to-emitc=optimization-report=true' --mlir-print-op-on-diagnostic=false 2>&1 | FileCheck %s --check-prefix=REPORT

// DEFAULT: ctnative.precompute_summary = {branches = 1 : i64, budget_exhausted = false, expressions = 2 : i64
// DEFAULT: ctnative.reachability_summary = {removed = 1 : i64, retained = 1 : i64
// DEFAULT-LABEL: emitc.func @entry_0() -> f64
// DEFAULT-NOT: scf.if
// DEFAULT-NOT: add
// DEFAULT: value = 4.200000e+01 : f64
// DEFAULT: return

// BASELINE-LABEL: emitc.func @entry_0() -> f64
// BASELINE: add
// BASELINE: scf.if
// BASELINE: emitc.call @dead_1
// BASELINE-LABEL: emitc.func @dead_1

// NO-PRECOMPUTE: ctnative.reachability_summary = {removed = 0 : i64, retained = 2 : i64
// NO-PRECOMPUTE: add
// NO-PRECOMPUTE: emitc.call @dead_1
// NO-PRECOMPUTE: emitc.func @dead_1

// NO-PRUNE: ctnative.precompute_summary = {branches = 1 : i64
// NO-PRUNE: emitc.func @entry_0() -> f64
// NO-PRUNE: ctjs.func private @dead$1
// NO-PRUNE-SAME: ctnative.not_native = "parameter 0 is

// BUDGET: ctnative.precompute_summary = {branches = 0 : i64, budget_exhausted = true, expressions = 0 : i64, steps = 0 : i64}
// BUDGET: ctnative.reachability_reason = "reachability scan budget exhausted"
// BUDGET: ctnative.reachability_summary = {removed = 0 : i64, retained = 2 : i64, steps = 0 : i64}
// BUDGET: add
// BUDGET: emitc.func @dead_1

// REPORT: precomputation: 2 expression(s), 1 structured branch(es)
// REPORT: reachability: 1 private function(s) removed, 1 retained

module {
  ctjs.func @entry$0(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %forty = ctjs.constant #ctjs.number<4630826316843712512>
    %two = ctjs.constant #ctjs.number<4611686018427387904>
    %answer = ctjs.binary add %forty, %two
    %false = ctjs.constant #ctjs.boolean<false>
    %take = ctjs.truthy %false
    scf.if %take {
      %unused = ctjs.call_direct @dead$1(%u, %u, %u, %answer)
    }
    ctjs.return %answer
  }
  ctjs.func private @dead$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    ctjs.store_global "unreachable", %input
    ctjs.return %input
  }
}
