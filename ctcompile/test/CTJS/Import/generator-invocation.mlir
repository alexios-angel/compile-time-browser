// A generator's invocation differs even when its body never suspends. The
// skipped rows retain all three source identities and the deferred global write.
// An ordinary async return still imports its explicit promise wrapper.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | ctjs-opt | FileCheck %s

var touched = 0;
function* constant() { touched = 1; return 42; }
function* empty() {}
async function* deferred() { return 42; }
async function settled() { return 42; }
var iterator = constant();

// CHECK: ctjs.skipped
// CHECK-SAME: function = 1 : i32
// CHECK-SAME: reason = "a generator invocation creates a deferred iterator
// CHECK-SAME: stores = ["touched"]
// CHECK-SAME: function = 2 : i32
// CHECK-SAME: reason = "a generator invocation creates a deferred iterator
// CHECK-SAME: function = 3 : i32
// CHECK-SAME: reason = "a generator invocation creates a deferred iterator
// CHECK-LABEL: ctjs.func @_script_$0
// CHECK-NOT: ctjs.func @constant$
// CHECK-NOT: ctjs.func @empty$
// CHECK-NOT: ctjs.func @deferred$
// CHECK-LABEL: ctjs.func @settled$4
// CHECK: ctjs.wrap_promise
// CHECK-NOT: ctjs.func
