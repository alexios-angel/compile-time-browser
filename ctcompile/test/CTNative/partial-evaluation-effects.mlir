// RUN: split-file %s %t
// RUN: ctjs-opt %t/accessor.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=ACCESSOR
// RUN: ctjs-opt %t/coercion.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=COERCION
// RUN: ctjs-opt %t/returned.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=RETURNED
// RUN: ctjs-opt %t/rebound.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=REBOUND
// RUN: ctjs-opt %t/declaration.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=DECLARATION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/accessor.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-partial-evaluate | FileCheck %s --check-prefix=SOURCE

// Numeric-index closure references are not MLIR symbol uses. Marking the
// function private must not hide invocations through getters, coercion hooks,
// returned values, or a global whose binding is reassigned.

//--- accessor.mlir
module {
  // A getter calls doubled with undefined, and the setter supplies 2. The
  // visible direct call supplies 1. Specializing only from that call would
  // change the getter's observable result from NaN to 2.
  // ACCESSOR-LABEL: ctjs.func private @doubled$1
  // ACCESSOR-SAME: ctnative.partial_eval_reason = "module has implicit invocation or prototype effects"
  // ACCESSOR-NOT: ctnative.partial_evaluated
  // ACCESSOR: ctjs.binary add
  ctjs.func private @doubled$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %result = ctjs.binary add %n, %n
    ctjs.return %result
  }
  ctjs.func @entry$0(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %two = ctjs.constant #ctjs.number<4611686018427387904>
    %f = ctjs.create_closure %callee[1] this %u
    %direct = ctjs.call_direct @doubled$1(%u, %u, %f, %one)
    %object = ctjs.create_object
    ctjs.define_accessor "x" on %object get %f set %f
    %key = ctjs.constant #ctjs.string<"x">
    ctjs.set_property %object[%key], %two
    %observed = ctjs.get_property %object[%key]
    ctjs.return %observed
  }
}

//--- coercion.mlir
module {
  // ToPrimitive invokes valueOf with no explicit arguments. There is no
  // CallOp for that call, and the generic property write is not an accessor.
  // COERCION-LABEL: ctjs.func private @doubled$1
  // COERCION-SAME: ctnative.partial_eval_reason = "function closure escapes through `ctjs.set_property`"
  // COERCION-NOT: ctnative.partial_evaluated
  // COERCION: ctjs.binary add
  ctjs.func private @doubled$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %result = ctjs.binary add %n, %n
    ctjs.return %result
  }
  ctjs.func @entry$0(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %f = ctjs.create_closure %callee[1] this %u
    %direct = ctjs.call_direct @doubled$1(%u, %u, %f, %one)
    %object = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"valueOf">
    ctjs.set_property %object[%key], %f
    %observed = ctjs.binary add %object, %one
    ctjs.return %observed
  }
}

//--- returned.mlir
module {
  // RETURNED-LABEL: ctjs.func private @doubled$1
  // RETURNED-SAME: ctnative.partial_eval_reason = "function closure escapes through `ctjs.return`"
  // RETURNED: ctjs.binary add
  ctjs.func private @doubled$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %result = ctjs.binary add %n, %n
    ctjs.return %result
  }
  ctjs.func @entry$0(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %f = ctjs.create_closure %callee[1] this %u
    %direct = ctjs.call_direct @doubled$1(%u, %u, %f, %one)
    ctjs.return %f
  }
}

//--- rebound.mlir
module {
  // A store in the hoisting prologue is insufficient when a second store can
  // rebind the same name, even if every visible read feeds a direct call.
  // REBOUND-LABEL: ctjs.func private @doubled$1
  // REBOUND-SAME: ctnative.partial_eval_reason = "function closure escapes through `ctjs.store_global`"
  // REBOUND: ctjs.binary add
  ctjs.func private @doubled$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %result = ctjs.binary add %n, %n
    ctjs.return %result
  }
  ctjs.func @entry$0(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %f = ctjs.create_closure %callee[1] this %u
    ctjs.store_global "doubled", %f
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %loaded = ctjs.load_global "doubled"
    %direct = ctjs.call_direct @doubled$1(%u, %u, %loaded, %one)
    ctjs.store_global "doubled", %u
    ctjs.return %direct
  }
}

//--- declaration.mlir
module {
  // A singly bound declaration with closed load uses remains eligible. The
  // closure, global store, and load are all present rather than elided by a
  // trusted annotation.
  // DECLARATION-LABEL: ctjs.func private @doubled$1
  // DECLARATION-SAME: ctnative.partial_evaluated =
  // DECLARATION-NOT: ctjs.binary
  // DECLARATION: ctjs.constant #ctjs.number<4611686018427387904>
  ctjs.func private @doubled$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %result = ctjs.binary add %n, %n
    ctjs.return %result
  }
  ctjs.func @entry$0(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %f = ctjs.create_closure %callee[1] this %u
    ctjs.store_global "doubled", %f
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %loaded = ctjs.load_global "doubled"
    %direct = ctjs.call_direct @doubled$1(%u, %u, %loaded, %one)
    ctjs.return %direct
  }
}

//--- accessor.js
// SOURCE-LABEL: ctjs.func private @seed$1
// SOURCE-SAME: ctnative.partial_eval_reason = "module has implicit invocation or prototype effects"
// SOURCE: ctjs.binary add
function seed() { return 21 + 21; }
var result = seed() + ({ get x() { return 0; } }).x;
