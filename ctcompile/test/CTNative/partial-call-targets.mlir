// RUN: split-file %s %t
// RUN: ctjs-opt %t/globals.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=GLOBAL
// RUN: python3 -c "from pathlib import Path; p=Path(r'%t/globals.mlir'); s=p.read_text(); Path(r'%t/rebound.mlir').write_text(s.replace('    ctjs.return %result', '    ctjs.store_global \"helper\", %u\n    ctjs.return %result'))"
// RUN: ctjs-opt %t/rebound.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=UNPROVED
// RUN: python3 -c "from pathlib import Path; p=Path(r'%t/globals.mlir'); s=p.read_text(); Path(r'%t/late.mlir').write_text(s.replace('    ctjs.store_global \"helper\", %closure', '    %effect = ctjs.binary add %u, %u\n    ctjs.store_global \"helper\", %closure'))"
// RUN: ctjs-opt %t/late.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=UNPROVED
// RUN: python3 -c "from pathlib import Path; p=Path(r'%t/globals.mlir'); s=p.read_text(); Path(r'%t/missing.mlir').write_text(s.replace('    ctjs.store_global \"helper\", %closure', ''))"
// RUN: ctjs-opt %t/missing.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=UNPROVED
// RUN: ctjs-opt %t/budget.mlir '--ctnative-partial-evaluate=max-steps=11' | FileCheck %s --check-prefix=ENOUGH
// RUN: ctjs-opt %t/budget.mlir '--ctnative-partial-evaluate=max-steps=6' | FileCheck %s --check-prefix=EXECUTION
// RUN: ctjs-opt %t/budget.mlir '--ctnative-partial-evaluate=max-steps=9' | FileCheck %s --check-prefix=PROOF
// RUN: ctjs-opt %t/budget.mlir '--ctnative-partial-evaluate=max-depth=1' | FileCheck %s --check-prefix=DEPTH
// RUN: python3 -c "from pathlib import Path; p=Path(r'%t/budget.mlir'); Path(r'%t/exact.mlir').write_text(p.read_text().replace('ctjs.call_direct @variant', 'ctjs.call_direct @original'+chr(36)+'1'))"
// RUN: ctjs-opt %t/exact.mlir '--ctnative-partial-evaluate=max-steps=6' | FileCheck %s --check-prefix=ENOUGH
// RUN: python3 -c "from pathlib import Path; p=Path(r'%t/budget.mlir'); Path(r'%t/unknown.mlir').write_text(p.read_text().replace('ctjs.constant #ctjs.number<4631107791820423168>', 'ub.poison : !ctjs.value', 1))"
// RUN: ctjs-opt %t/unknown.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=UNKNOWN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/specialized.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-specialize --ctnative-partial-evaluate | FileCheck %s --check-prefix=SPECIALIZED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/specialized.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-specialize --ctnative-partial-evaluate --ctnative-partial-evaluate | FileCheck %s --check-prefix=SPECIALIZED

//--- globals.mlir
module {
  ctjs.func private @helper$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %answer = ctjs.constant #ctjs.number<4631107791820423168>
    ctjs.return %answer
  }
  // A single declaration in the script's hoisting prologue establishes the
  // loaded callee's identity. Missing, reassigned or late bindings do not.
  // GLOBAL-LABEL: ctjs.func private @factory$2
  // GLOBAL-SAME: ctnative.partial_evaluated =
  // GLOBAL: %[[ANSWER:.*]] = ctjs.constant #ctjs.number<4631107791820423168>
  // GLOBAL-NEXT: ctjs.return %[[ANSWER]]
  // UNPROVED-LABEL: ctjs.func private @factory$2
  // UNPROVED-SAME: ctnative.partial_eval_reason = "direct call boxed callee identity is unproved"
  // UNPROVED: %[[LOADED:.*]] = ctjs.load_global "helper"
  // UNPROVED: ctjs.call_direct @helper$1({{.*}}, %[[LOADED]])
  ctjs.func private @factory$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %loaded = ctjs.load_global "helper"
    %result = ctjs.call_direct @helper$1(%u, %u, %loaded)
    ctjs.return %result
  }
  ctjs.func @entry$0(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %closure = ctjs.create_closure %callee[1] this %u
    ctjs.store_global "helper", %closure
    %result = ctjs.call_direct @factory$2(%u, %u, %u)
    ctjs.return %result
  }
}

//--- budget.mlir
module {
  ctjs.func private @original$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %answer = ctjs.constant #ctjs.number<4631107791820423168>
    ctjs.return %answer
  }
  ctjs.func private @variant(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %answer = ctjs.constant #ctjs.number<4631107791820423168>
    ctjs.return %answer
  }
  // Both executions and comparison share one step limit. A proof limit must
  // roll back the entire attempt, rather than retrying as a cheaper prefix.
  // Exact target identity avoids the second execution and graph comparison.
  // ENOUGH-LABEL: ctjs.func private @factory$2
  // ENOUGH-SAME: ctnative.partial_evaluated =
  // ENOUGH: %[[ANSWER:.*]] = ctjs.constant #ctjs.number<4631107791820423168>
  // ENOUGH-NEXT: ctjs.return %[[ANSWER]]
  // EXECUTION-LABEL: ctjs.func private @factory$2
  // EXECUTION-SAME: ctnative.partial_eval_reason = "step budget exhausted"
  // EXECUTION: ctjs.call_direct @variant
  // PROOF-LABEL: ctjs.func private @factory$2
  // PROOF-SAME: ctnative.partial_eval_reason = "direct call equivalence budget exhausted"
  // PROOF: ctjs.call_direct @variant
  // DEPTH-LABEL: ctjs.func private @factory$2
  // DEPTH-SAME: ctnative.partial_eval_reason = "call-depth budget exhausted"
  // DEPTH: ctjs.call_direct @variant
  // UNKNOWN-LABEL: ctjs.func private @factory$2
  // UNKNOWN-SAME: boundary = "ctjs.call_direct"
  // UNKNOWN: %[[RESULT:.*]] = ctjs.call_direct @variant
  // UNKNOWN: ctjs.return %[[RESULT]]
  ctjs.func private @factory$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %actual = ctjs.create_closure %callee[1] this %u
    %result = ctjs.call_direct @variant(%u, %u, %actual)
    ctjs.return %result
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %result = ctjs.call_direct @factory$2(%u, %u, %u)
    ctjs.return %result
  }
}

//--- specialized.js
// This invokes a real variant emitted by specialization, with its original
// boxed global callee intact. On a repeat PE run, the specialized body has
// already changed again; equivalence must be proved from the current bodies.
// SPECIALIZED-LABEL: ctjs.func private @factory$2
// SPECIALIZED-SAME: ctnative.partial_evaluated =
// SPECIALIZED: %[[ANSWER:.*]] = ctjs.constant #ctjs.number<4631107791820423168>
// SPECIALIZED-NEXT: ctjs.return %[[ANSWER]]
// SPECIALIZED: ctjs.func private @scale$1__specialized
function scale(factor, value) { return factor * value; }
function factory() { return scale(6, 7); }
var generic = scale(2, 3);
var result = factory();
