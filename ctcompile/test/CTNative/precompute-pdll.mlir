// RUN: split-file %s %t
// RUN: ctjs-opt %t/scalars.mlir --ctnative-precompute | FileCheck %s --check-prefix=SCALARS --implicit-check-not=ctjs.binary --implicit-check-not=ctjs.unary --implicit-check-not=ctjs.compare --implicit-check-not=ctjs.truthy --implicit-check-not=arith.trunci
// RUN: ctjs-opt %t/unknown.mlir --ctnative-precompute | FileCheck %s --check-prefix=UNKNOWN
// RUN: ctjs-opt %t/budget.mlir '--ctnative-precompute=max-steps=11' | FileCheck %s --check-prefix=BUDGET11
// RUN: ctjs-opt %t/budget.mlir '--ctnative-precompute=max-steps=12' | FileCheck %s --check-prefix=BUDGET12
// RUN: ctjs-opt %t/budget.mlir '--ctnative-precompute=max-steps=13' | FileCheck %s --check-prefix=BUDGET13
// RUN: ctjs-opt %t/budget.mlir '--ctnative-precompute=max-steps=14' | FileCheck %s --check-prefix=BUDGET14

// Every PDLL root fires, including the two integer representations. Constant
// construction must preserve exact bits and truncate integers to the result
// width. Unused constants remain because this driver does not run generic DCE.
// SCALARS: ctnative.precompute_summary = {branches = 0 : i64, budget_exhausted = false, expressions = 6 : i64,
// SCALARS-LABEL: ctjs.func @scalars
// SCALARS: ctjs.constant #ctjs.number<4613937818241073152>
// SCALARS: ctjs.constant #ctjs.number<4618441417868443648>
// SCALARS: %[[NEGATIVE:.*]] = ctjs.constant #ctjs.number<13841813454723219456>
// SCALARS: ctjs.constant #ctjs.boolean<true>
// SCALARS: arith.constant {{.*}}true
// SCALARS: arith.constant {{.*}}2 : i8
// SCALARS: ctjs.return %[[NEGATIVE]]

// Analysis facts are required for all six roots. Numeric self-equality cannot
// erase a possible NaN; no candidate becomes a constant just by matching ODS.
// UNKNOWN: ctnative.precompute_summary = {branches = 0 : i64, budget_exhausted = false, expressions = 0 : i64,
// UNKNOWN-LABEL: ctjs.func @unknown
// UNKNOWN: ctjs.binary add
// UNKNOWN: ctjs.binary_static add
// UNKNOWN: ctjs.unary plus
// UNKNOWN: ctjs.compare strict_eq
// UNKNOWN: ctjs.truthy
// UNKNOWN: arith.extui
// UNKNOWN: arith.trunci
// UNKNOWN: ctjs.return

// Ten analysis visits precede four rewrite candidates. Even the unknown unary
// candidate consumes a step, and branch splicing must follow the scalar roots
// in their original postorder. These boundaries catch greedy folding, retries,
// a missing generated root kind, and budget charges inside match predicates.
// BUDGET11: ctnative.precompute_summary = {branches = 0 : i64, budget_exhausted = true, expressions = 0 : i64, steps = 11 : i64}
// BUDGET11: ctjs.unary plus
// BUDGET11: ctjs.binary add
// BUDGET11: ctjs.truthy
// BUDGET11: scf.if
// BUDGET12: ctnative.precompute_summary = {branches = 0 : i64, budget_exhausted = true, expressions = 1 : i64, steps = 12 : i64}
// BUDGET12: ctjs.unary plus
// BUDGET12-NOT: ctjs.binary
// BUDGET12: ctjs.constant #ctjs.number<4611686018427387904>
// BUDGET12: ctjs.truthy
// BUDGET12: scf.if
// BUDGET13: ctnative.precompute_summary = {branches = 0 : i64, budget_exhausted = true, expressions = 2 : i64, steps = 13 : i64}
// BUDGET13: ctjs.unary plus
// BUDGET13-NOT: ctjs.binary
// BUDGET13-NOT: ctjs.truthy
// BUDGET13: arith.constant {{.*}}false
// BUDGET13: scf.if
// BUDGET14: ctnative.precompute_summary = {branches = 1 : i64, budget_exhausted = false, expressions = 2 : i64, steps = 14 : i64}
// BUDGET14: ctjs.unary plus
// BUDGET14-NOT: ctjs.binary
// BUDGET14-NOT: ctjs.truthy
// BUDGET14-NOT: scf.if
// BUDGET14: ctjs.store_global "selected"
// BUDGET14-NOT: ctjs.store_global
// BUDGET14: ctjs.return

//--- scalars.mlir
module {
  ctjs.func @scalars(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %two = ctjs.constant #ctjs.number<4611686018427387904>
    %sum = ctjs.binary add %one, %two
    %product = ctjs.binary_static add %sum, %sum
    %negative = ctjs.unary neg %product
    %compare = ctjs.compare lt %negative, %one
    %truth = ctjs.truthy %compare
    %wide = arith.constant 258 : i32
    %narrow = arith.trunci %wide : i32 to i8
    ctjs.return %negative
  }
}

//--- unknown.mlir
module {
  ctjs.func @unknown(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %sum = ctjs.binary add %input, %one
    %product = ctjs.binary_static add %sum, %one
    %number = ctjs.unary plus %product
    %compare = ctjs.compare strict_eq %number, %number
    %truth = ctjs.truthy %compare
    %wide = arith.extui %truth : i1 to i8
    %narrow = arith.trunci %wide : i8 to i1
    ctjs.return %number
  }
}

//--- budget.mlir
module {
  ctjs.func @budget(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %unknown = ctjs.unary plus %input
    %sum = ctjs.binary add %one, %one
    %false = ctjs.constant #ctjs.boolean<false>
    %take = ctjs.truthy %false
    scf.if %take {
    } else {
      ctjs.store_global "selected", %sum
    }
    ctjs.return %input
  }
}
