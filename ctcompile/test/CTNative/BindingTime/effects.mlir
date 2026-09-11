// RUN: split-file %s %t
// RUN: ctjs-opt %t/heap.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=HEAP
// RUN: ctjs-opt %t/control.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=CONTROL
// RUN: ctjs-opt %t/escape.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=ESCAPE
// RUN: ctjs-opt %t/host.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=HOST
// RUN: ctjs-opt %t/effectful-call.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=CALL
// RUN: ctjs-opt %t/alias-call.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=ALIAS
// RUN: ctjs-opt %t/getter.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=GETTER
// RUN: ctjs-opt %t/map-missing.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=MAP-MISSING
// RUN: ctjs-opt %t/metadata.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=METADATA
// RUN: ctjs-opt %t/metadata.mlir --ctnative-binding-time-analysis --ctnative-binding-time-analysis | FileCheck %s --check-prefix=METADATA

// Binding-time knowledge is distinct from permission to execute an operation.
// These adversaries require dynamic effects/control even when individual
// operands, a returned scalar or a closure's source code are known.

//--- heap.mlir
module {
  // The saved scalar belongs to the pre-write heap version. The later read
  // must not reuse it after a dynamic write through the same object identity.
  // HEAP-LABEL: ctjs.func @heap
  // HEAP: ctjs.create_object
  // HEAP: ctjs.set_property
  // HEAP: ctjs.get_property
  // HEAP: ctjs.set_property {{.*}}ctnative.binding_time = "dynamic"
  // HEAP: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  // HEAP: ctjs.binary add {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func @heap(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %object = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"answer">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.set_property %object[%key], %one
    %saved = ctjs.get_property %object[%key]
    ctjs.set_property %object[%key], %runtime
    %current = ctjs.get_property %object[%key]
    %result = ctjs.binary add %saved, %current
    ctjs.return %result
  }
}

//--- control.mlir
module {
  // A constant in a dynamic arm is a known SSA value, but executing the arm's
  // operations is conditional. In particular, allocation cannot be hoisted.
  // CONTROL-LABEL: ctjs.func @control
  // CONTROL: scf.if
  // CONTROL: ctjs.constant #ctjs.number<4607182418800017408> {{.*}}ctnative.binding_time = "dynamic"{{.*}}ctnative.result_binding_times = ["static"]
  // CONTROL: ctjs.create_object {{.*}}ctnative.binding_time = "dynamic"
  // CONTROL: ctjs.set_property {{.*}}ctnative.binding_time = "dynamic"
  // CONTROL: ctjs.set_property {{.*}}ctnative.binding_time = "dynamic"
  // CONTROL: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func @control(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %object = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"answer">
    %zero = ctjs.constant #ctjs.number<0>
    ctjs.set_property %object[%key], %zero
    %take = ctjs.truthy %runtime
    scf.if %take {
      %one = ctjs.constant #ctjs.number<4607182418800017408>
      %child = ctjs.create_object
      ctjs.set_property %child[%key], %one
      ctjs.set_property %object[%key], %one
    }
    %result = ctjs.get_property %object[%key]
    ctjs.return %result
  }
}

//--- escape.mlir
module {
  // The visible direct caller supplies one. A ToPrimitive hook can invoke
  // this same numeric-index closure with no argument. Private visibility
  // and MLIR symbol uses therefore do not prove the argument is constant.
  // ESCAPE-LABEL: ctjs.func private @doubled$1
  // ESCAPE-SAME: ctnative.argument_binding_times = ["dynamic", "dynamic", "dynamic", "dynamic"]
  // ESCAPE: ctjs.binary add {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @doubled$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %n: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %result = ctjs.binary add %n, %n
    ctjs.return %result
  }
  ctjs.func @entry$0(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %closure = ctjs.create_closure %callee[1] this %u
    %direct = ctjs.call_direct @doubled$1(%u, %u, %closure, %one)
    %object = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"valueOf">
    ctjs.set_property %object[%key], %closure
    %result = ctjs.binary add %object, %one
    ctjs.return %result
  }
}

//--- host.mlir
module {
  // Unknown calls and global publication remain dynamic even if their result
  // is unused or their published value is a primitive literal.
  // HOST-LABEL: ctjs.func @host
  // HOST: ctjs.load_global "hostCallback" {{.*}}ctnative.binding_time = "dynamic"
  // HOST: ctjs.call {{.*}}ctnative.binding_time = "dynamic"
  // HOST: ctjs.store_global "published"{{.*}}ctnative.binding_time = "dynamic"
  // HOST: ctjs.load_global "published" {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func @host(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %callback = ctjs.load_global "hostCallback"
    %ignored = ctjs.call %callback(%u)
    %answer = ctjs.constant #ctjs.number<4631107791820423168>
    ctjs.store_global "published", %answer
    %result = ctjs.load_global "published"
    ctjs.return %result
  }
}

//--- effectful-call.mlir
module {
  // Knowing a callee's return value does not erase its observable store. The
  // dynamic effect also propagates through a second visible direct callee.
  // CALL-LABEL: ctjs.func private @effect
  // CALL: ctjs.store_global "trace"{{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @effect(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %answer = ctjs.constant #ctjs.number<4631107791820423168>
    ctjs.store_global "trace", %answer
    ctjs.return %answer
  }
  // CALL-LABEL: ctjs.func private @forward
  // CALL: ctjs.call_direct @effect{{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @forward(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %result = ctjs.call_direct @effect(%u, %u, %u)
    ctjs.return %result
  }
  // CALL-LABEL: ctjs.func @entry
  // CALL: ctjs.call_direct @forward{{.*}}ctnative.binding_time = "dynamic"
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %result = ctjs.call_direct @forward(%u, %u, %u)
    ctjs.return %result
  }
}

//--- alias-call.mlir
module {
  // Passing a fresh object to a dynamic callee transfers mutation authority.
  // The caller must not reuse its earlier static field after that call, even
  // though the callee's returned value is known and ignored.
  ctjs.func private @overwrite(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"answer">
    ctjs.set_property %object[%key], %value
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
  // ALIAS-LABEL: ctjs.func @aliases
  // ALIAS: ctjs.set_property
  // ALIAS: ctjs.call_direct @overwrite{{.*}}ctnative.binding_time = "dynamic"
  // ALIAS: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func @aliases(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %object = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"answer">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.set_property %object[%key], %one
    %ignored = ctjs.call_direct @overwrite(%u, %u, %u, %object, %runtime)
    %result = ctjs.get_property %object[%key]
    ctjs.return %result
  }
}

//--- getter.mlir
module {
  // A missing own field can consult a prototype getter. The getter receives
  // this fresh object as its receiver and can mutate its existing answer
  // field, even though the fresh object has never previously escaped.
  // GETTER-LABEL: ctjs.func @getter
  // GETTER: ctjs.set_property
  // GETTER: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  // GETTER: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func @getter(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %object = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"answer">
    %missing = ctjs.constant #ctjs.string<"missing">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.set_property %object[%key], %one
    %ignored = ctjs.get_property %object[%missing]
    %result = ctjs.get_property %object[%key]
    ctjs.return %result
  }
}

//--- map-missing.mlir
module {
  // A Map's aggregate payload is not the value returned by every key. Missing
  // and deleted lookups produce undefined; neither can prove the property
  // name "answer" just because a different entry stored that string.
  // MAP-MISSING-LABEL: ctjs.func @missing_key
  // MAP-MISSING: ctjs.call {{.*}}ctnative.binding_time = "static"
  // MAP-MISSING: ctjs.call {{.*}}ctnative.binding_time = "static"
  // MAP-MISSING: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func @missing_key(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %object = ctjs.create_object
    %answer = ctjs.constant #ctjs.string<"answer">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.set_property %object[%answer], %one
    %ctor = ctjs.load_global "Map"
    %map = ctjs.construct %ctor(%ctor)
    %set_name = ctjs.constant #ctjs.string<"set">
    %setter = ctjs.get_property %map[%set_name]
    %present = ctjs.constant #ctjs.string<"present">
    %written = ctjs.call %setter(%map, %present, %answer)
    %get_name = ctjs.constant #ctjs.string<"get">
    %getter = ctjs.get_property %map[%get_name]
    %missing = ctjs.constant #ctjs.string<"missing">
    %key = ctjs.call %getter(%map, %missing)
    %result = ctjs.get_property %object[%key]
    ctjs.return %result
  }
  // MAP-MISSING-LABEL: ctjs.func @deleted_key
  // MAP-MISSING: ctjs.call {{.*}}ctnative.binding_time = "static"
  // MAP-MISSING: ctjs.call {{.*}}ctnative.binding_time = "static"
  // MAP-MISSING: ctjs.call {{.*}}ctnative.binding_time = "static"
  // MAP-MISSING: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func @deleted_key(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %object = ctjs.create_object
    %answer = ctjs.constant #ctjs.string<"answer">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.set_property %object[%answer], %one
    %ctor = ctjs.load_global "Map"
    %map = ctjs.construct %ctor(%ctor)
    %set_name = ctjs.constant #ctjs.string<"set">
    %setter = ctjs.get_property %map[%set_name]
    %present = ctjs.constant #ctjs.string<"present">
    %written = ctjs.call %setter(%map, %present, %answer)
    %delete_name = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %map[%delete_name]
    %deleted = ctjs.call %deleter(%map, %present)
    %get_name = ctjs.constant #ctjs.string<"get">
    %getter = ctjs.get_property %map[%get_name]
    %key = ctjs.call %getter(%map, %present)
    %result = ctjs.get_property %object[%key]
    ctjs.return %result
  }
}

//--- metadata.mlir
module {
  // Every fact is rederived; a second analysis run must not promote a forged
  // literal fact on a host read or trust the input function-argument facts.
  // METADATA-LABEL: ctjs.func @metadata
  // METADATA-SAME: ctnative.argument_binding_times = ["dynamic", "dynamic", "dynamic", "dynamic"]
  // METADATA: ctjs.load_global "host" {{.*}}ctnative.binding_time = "dynamic"
  // METADATA: ctjs.binary add {{.*}}ctnative.binding_time = "dynamic"
  // METADATA-NOT: forged
  ctjs.func @metadata(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32, ctnative.argument_binding_times = ["static", "static", "static", "static"], ctnative.binding_time_summary = {forged = true}} {
    %host = ctjs.load_global "host" {ctnative.binding_time = "static", ctnative.binding_time_reason = "forged", ctnative.result_binding_times = ["static"]}
    %result = ctjs.binary add %runtime, %host {ctnative.binding_time = "static", ctnative.binding_time_reason = "forged", ctnative.result_binding_times = ["static"]}
    ctjs.return %result
  }
}
