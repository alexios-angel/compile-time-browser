// RUN: split-file %s %t
// RUN: ctjs-opt %t/missing-own.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=MISSING
// RUN: ctjs-opt %t/dirty-graph.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=DIRTY
// RUN: ctjs-opt %t/map-keys.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=MAPKEY
// RUN: ctjs-opt %t/objects.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=OBJECT
// RUN: ctjs-opt %t/objects.mlir --ctnative-binding-time-analysis --ctnative-binding-time-analysis | FileCheck %s --check-prefix=OBJECT
// RUN: ctjs-opt %t/nested.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=NESTED
// RUN: ctjs-opt %t/maps.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=MAP
// RUN: ctjs-opt %t/captures.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=CAPTURE
// RUN: ctjs-opt %t/ordering.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=ORDER
// RUN: ctjs-opt %t/unknown.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=UNKNOWN
// RUN: ctjs-opt %t/prototype.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=PROTO
// RUN: ctjs-opt %t/recursive.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=RECURSIVE
// RUN: ctjs-opt %t/public.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=PUBLIC
// RUN: ctjs-opt %t/alternate-dynamic.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=ALTERNATE-DYNAMIC
// RUN: ctjs-opt %t/alternate-static.mlir --ctnative-binding-time-analysis | FileCheck %s --check-prefix=ALTERNATE-STATIC
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-binding-time-analysis | FileCheck %s --check-prefix=SOURCE

// A queried call/result stays dynamic. Only the subsequent disjoint heap read
// is static; reading an affected alias is dynamic. No schema family identifies
// a runtime allocation. Handwritten non-bytecode symbols use inert callee slots.

//--- objects.mlir
module {
  // OBJECT-LABEL: ctjs.func private @objects
  // OBJECT: ctjs.call_direct @write{{.*}}ctnative.binding_time = "dynamic"{{.*}}ctnative.binding_time_reason = "runtime call with proved argument-local heap effects"{{.*}}ctnative.result_binding_times = ["dynamic"]
  // OBJECT: ctjs.get_property {{.*}}ctnative.binding_time = "static"
  // OBJECT: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @objects(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %a = ctjs.create_object
    %b = ctjs.create_object
    ctjs.set_property %a[%key], %one
    ctjs.set_property %b[%key], %one
    %call = ctjs.call_direct @write(%u, %u, %u, %a, %runtime)
    %untouched = ctjs.get_property %b[%key]
    %changed = ctjs.get_property %a[%key]
    ctjs.return %untouched
  }
  ctjs.func private @write(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"value">
    ctjs.set_property %object[%key], %value
    ctjs.return %value
  }
}

//--- nested.mlir
module {
  // NESTED-LABEL: ctjs.func private @nested
  // NESTED: ctjs.call_direct @forward{{.*}}ctnative.binding_time_reason = "runtime call with proved argument-local heap effects"
  // NESTED: ctjs.get_property {{.*}}ctnative.binding_time = "static"
  // NESTED: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @nested(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %a = ctjs.create_object
    %b = ctjs.create_object
    ctjs.set_property %a[%key], %one
    ctjs.set_property %b[%key], %one
    %child = ctjs.constant #ctjs.string<"child">
    %parent = ctjs.create_object
    ctjs.set_property %parent[%child], %a
    %call = ctjs.call_direct @forward(%u, %u, %u, %parent, %runtime)
    %untouched = ctjs.get_property %b[%key]
    %sharedChild = ctjs.get_property %a[%key]
    ctjs.return %untouched
  }
  ctjs.func private @forward(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %parent: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %child = ctjs.constant #ctjs.string<"child">
    %object = ctjs.get_property %parent[%child]
    %call = ctjs.call_direct @write(%u, %u, %u, %object, %value)
    ctjs.return %value
  }
  ctjs.func private @write(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"value">
    ctjs.set_property %object[%key], %value
    ctjs.return %value
  }
}

//--- maps.mlir
module {
  // MAP-LABEL: ctjs.func private @maps
  // MAP: ctjs.call_direct @writeMap{{.*}}ctnative.binding_time_reason = "runtime call with proved argument-local heap effects"
  // MAP: ctjs.get_property {{.*}}ctnative.binding_time = "static"
  // MAP: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @maps(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %constructor = ctjs.load_global "Map"
    %a = ctjs.construct %constructor(%constructor)
    %b = ctjs.construct %constructor(%constructor)
    %size = ctjs.constant #ctjs.string<"size">
    %call = ctjs.call_direct @writeMap(%u, %u, %u, %a, %runtime)
    %untouched = ctjs.get_property %b[%size]
    %changed = ctjs.get_property %a[%size]
    ctjs.return %untouched
  }
  ctjs.func private @writeMap(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %map: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"value">
    %name = ctjs.constant #ctjs.string<"set">
    %set = ctjs.get_property %map[%name]
    %result = ctjs.call %set(%map, %key, %value)
    ctjs.return %result
  }
}

//--- captures.mlir
module {
  // CAPTURE-LABEL: ctjs.func private @captures
  // CAPTURE: ctjs.call_direct @write{{.*}}ctnative.binding_time_reason = "runtime call with proved argument-local heap effects"
  // CAPTURE: ctjs.get_property {{.*}}ctnative.binding_time = "static"
  // CAPTURE: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @captures(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %a = ctjs.create_object
    %b = ctjs.create_object
    ctjs.set_property %a[%key], %one
    ctjs.set_property %b[%key], %one
    %captured = ctjs.create_object
    ctjs.set_property %captured[%key], %one
    %cell = ctjs.create_cell %captured
    %closure = ctjs.create_closure %callee[7] this %u captures %cell
    %method = ctjs.constant #ctjs.string<"method">
    ctjs.set_property %a[%method], %closure
    %call = ctjs.call_direct @write(%u, %u, %u, %a, %runtime)
    %untouched = ctjs.get_property %b[%key]
    %sharedCapture = ctjs.get_property %captured[%key]
    ctjs.return %untouched
  }
  ctjs.func private @write(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"value">
    ctjs.set_property %object[%key], %value
    ctjs.return %value
  }
  ctjs.func private @body$7(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %cell = ctjs.load_upvalue %callee[0]
    %captured = ctjs.cell_get %cell
    ctjs.return %captured
  }
}

//--- ordering.mlir
module {
  // ORDER-LABEL: ctjs.func private @ordering
  // ORDER: ctjs.call_direct @replaceThenRead{{.*}}ctnative.binding_time_reason = "runtime operands, heap contents or effects"
  // ORDER: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @ordering(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %a = ctjs.create_object
    %b = ctjs.create_object
    ctjs.set_property %a[%key], %one
    ctjs.set_property %b[%key], %one
    %child = ctjs.constant #ctjs.string<"child">
    %parent = ctjs.create_object
    ctjs.set_property %parent[%child], %a
    %call = ctjs.call_direct @replaceThenRead(%u, %u, %u, %parent, %b, %runtime)
    %result = ctjs.get_property %b[%key]
    ctjs.return %result
  }
  ctjs.func private @replaceThenRead(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %parent: !ctjs.value, %other: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %child = ctjs.constant #ctjs.string<"child">
    %key = ctjs.constant #ctjs.string<"value">
    ctjs.set_property %parent[%child], %other
    %newChild = ctjs.get_property %parent[%child]
    ctjs.set_property %newChild[%key], %value
    ctjs.return %value
  }
}

//--- unknown.mlir
module {
  // UNKNOWN-LABEL: ctjs.func private @unknown
  // UNKNOWN: ctjs.call_direct @write{{.*}}ctnative.binding_time_reason = "runtime operands, heap contents or effects"
  // UNKNOWN: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @unknown(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %a = ctjs.create_object
    %b = ctjs.create_object
    ctjs.set_property %a[%key], %one
    ctjs.set_property %b[%key], %one
    %call = ctjs.call_direct @write(%u, %u, %u, %runtime, %one)
    %result = ctjs.get_property %b[%key]
    ctjs.return %result
  }
  ctjs.func private @write(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"value">
    ctjs.set_property %object[%key], %value
    ctjs.return %value
  }
}

//--- prototype.mlir
module {
  // PROTO-LABEL: ctjs.func private @prototype
  // PROTO: ctjs.call_direct @write{{.*}}ctnative.binding_time_reason = "runtime operands, heap contents or effects"
  // PROTO: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @prototype(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %a = ctjs.create_object
    %b = ctjs.create_object
    ctjs.set_property %a[%key], %one
    ctjs.set_property %b[%key], %one
    %call = ctjs.call_direct @write(%u, %u, %u, %a, %runtime)
    %result = ctjs.get_property %b[%key]
    ctjs.return %result
  }
  ctjs.func private @write(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"value">
    ctjs.set_property %object[%key], %value
    ctjs.return %value
  }
  ctjs.func private @prototypeAccess(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"__proto__">
    %result = ctjs.get_property %object[%key]
    ctjs.return %result
  }
}

//--- recursive.mlir
module {
  // RECURSIVE-LABEL: ctjs.func private @recursive
  // RECURSIVE: ctjs.call_direct @recur{{.*}}ctnative.binding_time_reason = "runtime operands, heap contents or effects"
  // RECURSIVE: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @recursive(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %a = ctjs.create_object
    %b = ctjs.create_object
    ctjs.set_property %a[%key], %one
    ctjs.set_property %b[%key], %one
    %call = ctjs.call_direct @recur(%u, %u, %u, %a, %runtime)
    %result = ctjs.get_property %b[%key]
    ctjs.return %result
  }
  ctjs.func private @recur(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %call = ctjs.call_direct @recur(%u, %u, %u, %object, %value)
    ctjs.return %value
  }
}

//--- public.mlir
module {
  // PUBLIC-LABEL: ctjs.func @publicInput
  // PUBLIC: ctjs.call_direct @write{{.*}}ctnative.binding_time_reason = "runtime operands, heap contents or effects"
  // PUBLIC: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func @publicInput(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %a = ctjs.create_object
    %b = ctjs.create_object
    ctjs.set_property %a[%key], %one
    ctjs.set_property %b[%key], %one
    %call = ctjs.call_direct @write(%u, %u, %u, %a, %runtime)
    %result = ctjs.get_property %b[%key]
    ctjs.return %result
  }
  ctjs.func private @write(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"value">
    ctjs.set_property %object[%key], %value
    ctjs.return %value
  }
}

//--- source.js
// SOURCE-LABEL: ctjs.func private @untouched$2
// SOURCE: ctjs.call_direct @write$1{{.*}}ctnative.binding_time_reason = "runtime call with proved argument-local heap effects"
// SOURCE: ctjs.get_property {{.*}}ctnative.binding_time = "static"
function write(object, value) { object.value = value; return value; }
function untouched(value) {
  const changed = {value: 1};
  const kept = {value: 42};
  write(changed, value);
  return kept.value;
}
var result = untouched(9);

//--- alternate-dynamic.mlir
// BTA must respect both call target representations. Diagnostic specialization
// provenance cannot prove an alternate boxed callee equivalent to the symbol.
// This is a BTA regression, not a claim that whole-factory PE admits this IR.
module {
  // ALTERNATE-DYNAMIC-LABEL: ctjs.func @entry
  // ALTERNATE-DYNAMIC: ctjs.call_direct @noop{{.*}}ctnative.binding_time = "dynamic"{{.*}}ctnative.binding_time_reason = "runtime operands, heap contents or effects"
  // ALTERNATE-DYNAMIC: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"{{.*}}test.observation = true
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %payload = ctjs.call_direct @dynamic(%u, %u, %u)
    %original = ctjs.create_closure %callee[1] this %this
    %object = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.set_property %object[%key], %one
    %ignored = ctjs.call_direct @noop(%u, %u, %original, %object, %payload)
    %observed = ctjs.get_property %object[%key] {test.observation = true}
    ctjs.return %observed
  }
  ctjs.func private @dynamic(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %two = ctjs.constant #ctjs.number<4611686018427387904>
    ctjs.store_global "effect", %two
    ctjs.return %two
  }
  ctjs.func private @original$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value, %payload: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"value">
    ctjs.set_property %object[%key], %payload
    ctjs.return %payload
  }
  ctjs.func private @noop(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value, %payload: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32, ctnative.specialized_from = "original$1"} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
}

//--- alternate-static.mlir
// BTA must respect both call target representations. Diagnostic specialization
// provenance cannot prove an alternate boxed callee equivalent to the symbol.
// This is a BTA regression, not a claim that whole-factory PE admits this IR.
module {
  // ALTERNATE-STATIC-LABEL: ctjs.func @entry
  // ALTERNATE-STATIC: ctjs.call_direct @noop{{.*}}ctnative.binding_time = "dynamic"{{.*}}ctnative.binding_time_reason = "runtime operands, heap contents or effects"
  // ALTERNATE-STATIC: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"{{.*}}test.observation = true
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %payload = ctjs.constant #ctjs.number<4611686018427387904>
    %original = ctjs.create_closure %callee[1] this %this
    %object = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.set_property %object[%key], %one
    %ignored = ctjs.call_direct @noop(%u, %u, %original, %object, %payload)
    %observed = ctjs.get_property %object[%key] {test.observation = true}
    ctjs.return %observed
  }
  ctjs.func private @dynamic(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %two = ctjs.constant #ctjs.number<4611686018427387904>
    ctjs.store_global "effect", %two
    ctjs.return %two
  }
  ctjs.func private @original$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value, %payload: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"value">
    ctjs.set_property %object[%key], %payload
    ctjs.return %payload
  }
  ctjs.func private @noop(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value, %payload: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32, ctnative.specialized_from = "original$1"} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
}

//--- missing-own.mlir
module {
  // MISSING-LABEL: ctjs.func private @missing_own
  // MISSING: ctjs.call_direct @write{{.*}}ctnative.binding_time = "dynamic"{{.*}}ctnative.binding_time_reason = "runtime operands, heap contents or effects"{{.*}}ctnative.result_binding_times = ["dynamic"]
  // MISSING: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  // MISSING: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @missing_own(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %a = ctjs.create_object
    %b = ctjs.create_object
    ctjs.set_property %a[%key], %one
    ctjs.set_property %b[%key], %one
    %call = ctjs.call_direct @write(%u, %u, %u, %a, %runtime)
    %untouched = ctjs.get_property %b[%key]
    %changed = ctjs.get_property %a[%key]
    ctjs.return %untouched
  }
  ctjs.func private @write(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"absent">
    ctjs.set_property %object[%key], %value
    ctjs.return %value
  }
}

//--- dirty-graph.mlir
module {
  // DIRTY-LABEL: ctjs.func private @dirty_graph
  // DIRTY: ctjs.call_direct @write{{.*}}ctnative.binding_time = "dynamic"{{.*}}ctnative.binding_time_reason = "runtime operands, heap contents or effects"{{.*}}ctnative.result_binding_times = ["dynamic"]
  // DIRTY: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  // DIRTY: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @dirty_graph(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %child = ctjs.create_object
    ctjs.set_property %child[%key], %runtime
    %a = ctjs.create_object
    %b = ctjs.create_object
    ctjs.set_property %a[%key], %one
    ctjs.set_property %b[%key], %one
    %childKey = ctjs.constant #ctjs.string<"child">
    ctjs.set_property %a[%childKey], %child
    %call = ctjs.call_direct @write(%u, %u, %u, %a, %runtime)
    %untouched = ctjs.get_property %b[%key]
    %changed = ctjs.get_property %a[%key]
    ctjs.return %untouched
  }
  ctjs.func private @write(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"value">
    ctjs.set_property %object[%key], %value
    ctjs.return %value
  }
}

//--- map-keys.mlir
module {
  // MAPKEY-LABEL: ctjs.func private @mapKeys
  // MAPKEY: ctjs.call_direct @writeMap{{.*}}ctnative.binding_time_reason = "runtime call with proved argument-local heap effects"
  // MAPKEY: ctjs.get_property {{.*}}ctnative.binding_time = "static"
  // MAPKEY: ctjs.get_property {{.*}}ctnative.binding_time = "dynamic"
  ctjs.func private @mapKeys(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %runtime: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %objectKey = ctjs.create_object
    %untouched = ctjs.create_object
    ctjs.set_property %objectKey[%key], %one
    ctjs.set_property %untouched[%key], %one
    %constructor = ctjs.load_global "Map"
    %map = ctjs.construct %constructor(%constructor)
    %name = ctjs.constant #ctjs.string<"set">
    %set = ctjs.get_property %map[%name]
    %seed = ctjs.call %set(%map, %objectKey, %one)
    %call = ctjs.call_direct @writeMap(%u, %u, %u, %map, %runtime)
    %kept = ctjs.get_property %untouched[%key]
    %shared = ctjs.get_property %objectKey[%key]
    ctjs.return %kept
  }
  ctjs.func private @writeMap(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %map: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"value">
    %name = ctjs.constant #ctjs.string<"set">
    %set = ctjs.get_property %map[%name]
    %result = ctjs.call %set(%map, %key, %value)
    ctjs.return %result
  }
}
