// RUN: ctjs-opt %s --split-input-file --ctnative-lower-to-emitc | FileCheck %s
// Call/return proof boundaries, independent of the source resolver's choices.

module {
  // CHECK: ctjs.func @probe$1
  // CHECK-SAME: ctnative.not_native = "native Map argument requires a closed callee"
  ctjs.func @probe$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %ctor = ctjs.load_global "Map"
    %map = ctjs.construct %ctor(%ctor)
    %nil = ctjs.constant #ctjs.undefined
    %result = ctjs.call_direct @open$2(%nil, %nil, %nil, %map)
    ctjs.return %result
  }
  ctjs.func @open$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %map: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %size = ctjs.constant #ctjs.string<"size">
    %result = ctjs.get_property %map[%size]
    ctjs.return %result
  }
}

// -----

module {
  // CHECK: ctjs.func @probe$1
  // A short JS call reaches CTJS with an explicit undefined argument. The
  // verifier already rejects mismatched CallDirect operand/parameter counts.
  // CHECK-SAME: ctnative.not_native = "native Map flow contains a non-Map producer `ctjs.constant`"
  ctjs.func @probe$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %ctor = ctjs.load_global "Map"
    %map = ctjs.construct %ctor(%ctor)
    %nil = ctjs.constant #ctjs.undefined
    %result = ctjs.call_direct @extra$2(%nil, %nil, %nil, %map)
    %missing = ctjs.call_direct @extra$2(%nil, %nil, %nil, %nil)
    ctjs.return %result
  }
  ctjs.func private @extra$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %map: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %size = ctjs.constant #ctjs.string<"size">
    %result = ctjs.get_property %map[%size]
    ctjs.return %result
  }
}

// -----

module {
  // A standard constructor reaching one return does not prove the other.
  // Check the proof itself; control-flow admission also refuses this raw CFG.
  ctjs.func @probe$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined
    %condition = ctjs.constant #ctjs.number<1>
    %result = ctjs.call_direct @mixed$2(%nil, %nil, %nil, %condition)
    %size = ctjs.constant #ctjs.string<"size">
    %read = ctjs.get_property %result[%size]
    ctjs.return %read
  }
  // CHECK: ctjs.func private @mixed$2
  // CHECK: ctjs.construct {{.*}}ctnative.map_reason = "native Map flow contains a non-Map producer `ctjs.constant`"
  ctjs.func private @mixed$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %condition: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %ctor = ctjs.load_global "Map"
    %map = ctjs.construct %ctor(%ctor)
    %test = ctjs.truthy %condition
    cf.cond_br %test, ^left, ^right
  ^left:
    ctjs.return %map
  ^right:
    %zero = ctjs.constant #ctjs.number<0>
    ctjs.return %zero
  }
}

// -----

module {
  // CHECK: ctjs.func @untrusted$1
  // CHECK-SAME: ctnative.not_native = "`ctjs.construct` is not native yet"
  // CHECK-NOT: ctnative.map_group
  // CHECK-NOT: ctnative.map_arg_groups
  // CHECK-NOT: ctnative.map_site
  ctjs.func @untrusted$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {ctnative.map_arg_groups = array<i64: 0, 0, 0>, upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined {ctnative.map_constructor}
    %made = ctjs.construct %nil(%nil) {ctnative.map_site, ctnative.map_group = 0 : i64}
    ctjs.return %made
  }
}
