// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../native-partial-closures-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-partial-evaluate | FileCheck %s --check-prefix=HEAP
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../native-partial-closures-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-partial-evaluate --ctnative-partial-evaluate | FileCheck %s --check-prefix=HEAP
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../native-partial-closures-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-binding-time-analysis | FileCheck %s --check-prefix=BTA
// RUN: split-file %s %t
// RUN: ctjs-opt %t/mutable.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=MUTABLE
// RUN: ctjs-opt %t/write.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=WRITE
// RUN: ctjs-opt %t/duplicate.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=DUPLICATE
// RUN: ctjs-opt %t/ambiguous.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=AMBIGUOUS
// RUN: ctjs-opt %t/cycle.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=CYCLE
// RUN: ctjs-opt %t/pending.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=PENDING
// RUN: ctjs-opt %t/unknown.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=UNKNOWN
// RUN: ctjs-opt %t/cycle.mlir '--ctnative-partial-evaluate=max-nodes=1' | FileCheck %s --check-prefix=BUDGET

// HEAP-LABEL: ctjs.func private @makeRetained$1
// HEAP-SAME: ctnative.partial_evaluated =
// HEAP-NOT: ctjs.create_object
// HEAP-NOT: temporary
// HEAP: %[[MAP:[0-9]+]] = ctjs.construct
// HEAP: ctjs.create_cell %[[MAP]]
// HEAP: ctjs.create_closure %arg2
// HEAP: ctjs.return
// HEAP-LABEL: ctjs.func private @makeSnapshot$13
// HEAP-SAME: ctnative.partial_evaluated =
// HEAP-NOT: ctjs.construct
// HEAP: %[[SAVED:[0-9]+]] = ctjs.constant #ctjs.number<4631107791820423168>
// HEAP: ctjs.create_cell %[[SAVED]]
// HEAP: ctjs.create_closure %arg2
// HEAP: ctjs.return
// BTA-LABEL: ctjs.func private @makeRetained$1
// BTA: ctjs.create_closure
// BTA-SAME: ctnative.binding_time = "static"

//--- mutable.mlir
module {
  // MUTABLE-LABEL: ctjs.func private @factory$1
  // MUTABLE-SAME: ctnative.partial_eval_reason = "capture cell is mutable or escapes"
  // MUTABLE-NOT: ctnative.partial_evaluated
  // MUTABLE: ctjs.cell_set
  // MUTABLE: ctjs.cell_set
  ctjs.func private @factory$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %cell = ctjs.create_cell %u
    ctjs.cell_set %cell, %one
    ctjs.cell_set %cell, %one
    %closure = ctjs.create_closure %callee[2] this %this captures %cell
    ctjs.return %closure
  }
  ctjs.func private @body$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %value = ctjs.load_upvalue %callee[0]
    ctjs.return %value
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %value = ctjs.call_direct @factory$1(%u, %u, %u)
    ctjs.return %value
  }
}

//--- cycle.mlir
module {
  // CYCLE-LABEL: ctjs.func private @factory$1
  // CYCLE-SAME: ctnative.partial_eval_reason = "returned heap has an ownership cycle or unsupported value"
  // CYCLE-NOT: ctnative.partial_evaluated
  // CYCLE: ctjs.create_closure
  // CYCLE: ctjs.cell_set
  // BUDGET-LABEL: ctjs.func private @factory$1
  // BUDGET-SAME: ctnative.partial_eval_reason = "heap-node budget exhausted"
  ctjs.func private @factory$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %cell = ctjs.create_cell %u
    %closure = ctjs.create_closure %callee[2] this %this captures %cell
    ctjs.cell_set %cell, %closure
    ctjs.return %closure
  }
  ctjs.func private @body$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %value = ctjs.load_upvalue %callee[0]
    ctjs.return %value
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %value = ctjs.call_direct @factory$1(%u, %u, %u)
    ctjs.return %value
  }
}

//--- pending.mlir
module {
  // PENDING-LABEL: ctjs.func private @factory$1
  // PENDING-SAME: ctnative.partial_eval_reason = "prefix live heap has an ownership cycle or unsupported value"
  // PENDING-NOT: ctnative.partial_evaluated
  // PENDING: ctjs.create_closure
  // PENDING: ctjs.cell_set {{.*}}, %arg3
  ctjs.func private @factory$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %cell = ctjs.create_cell %u
    %closure = ctjs.create_closure %callee[2] this %this captures %cell
    ctjs.cell_set %cell, %input
    ctjs.return %closure
  }
  ctjs.func private @body$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %value = ctjs.load_upvalue %callee[0]
    ctjs.return %value
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %input: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %value = ctjs.call_direct @factory$1(%u, %u, %u, %input)
    ctjs.return %value
  }
}

//--- unknown.mlir
module {
  // UNKNOWN-LABEL: ctjs.func private @factory$1
  // UNKNOWN-SAME: ctnative.partial_eval_reason = "module has an unproved closure target"
  // UNKNOWN: ctjs.create_closure
  ctjs.func private @factory$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %closure = ctjs.create_closure %callee[999] this %this
    ctjs.return %closure
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %value = ctjs.call_direct @factory$1(%u, %u, %u)
    ctjs.return %value
  }
}

//--- duplicate.mlir
module {
  // DUPLICATE-LABEL: ctjs.func private @factory$1
  // DUPLICATE-SAME: ctnative.partial_eval_reason = "capture cell is mutable or escapes"
  // DUPLICATE-NOT: ctnative.partial_evaluated
  // DUPLICATE: ctjs.create_closure
  // DUPLICATE: ctjs.create_closure
  ctjs.func private @factory$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %cell = ctjs.create_cell %one
    %first = ctjs.create_closure %callee[2] this %this captures %cell
    %second = ctjs.create_closure %callee[2] this %this captures %cell
    ctjs.return %second
  }
  ctjs.func private @body$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %value = ctjs.load_upvalue %callee[0]
    ctjs.return %value
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %value = ctjs.call_direct @factory$1(%u, %u, %u)
    ctjs.return %value
  }
}

//--- ambiguous.mlir
module {
  // AMBIGUOUS-LABEL: ctjs.func private @factory$1
  // AMBIGUOUS-SAME: ctnative.partial_eval_reason = "module has ambiguous numeric function identities"
  // AMBIGUOUS: ctjs.create_closure
  ctjs.func private @factory$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %cell = ctjs.create_cell %one
    %closure = ctjs.create_closure %callee[2] this %this captures %cell
    ctjs.return %closure
  }
  ctjs.func private @body$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %value = ctjs.load_upvalue %callee[0]
    ctjs.return %value
  }
  ctjs.func private @other$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %value = ctjs.load_upvalue %callee[0]
    ctjs.return %value
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %value = ctjs.call_direct @factory$1(%u, %u, %u)
    ctjs.return %value
  }
}

//--- write.mlir
module {
  // WRITE-LABEL: ctjs.func private @factory$1
  // WRITE-SAME: ctnative.partial_eval_reason = "capture cell is mutable or escapes"
  // WRITE-NOT: ctnative.partial_evaluated
  // WRITE: ctjs.create_cell
  // WRITE: ctjs.create_closure
  // WRITE-LABEL: ctjs.func private @body$2
  // WRITE: ctjs.store_upvalue
  ctjs.func private @factory$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %cell = ctjs.create_cell %one
    %closure = ctjs.create_closure %callee[2] this %this captures %cell
    ctjs.return %closure
  }
  ctjs.func private @body$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %value = ctjs.load_upvalue %callee[0]
    ctjs.store_upvalue %callee[0], %value
    ctjs.return %value
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %value = ctjs.call_direct @factory$1(%u, %u, %u)
    ctjs.return %value
  }
}
