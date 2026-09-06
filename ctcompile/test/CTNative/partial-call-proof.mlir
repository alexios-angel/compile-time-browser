// RUN: split-file %s %t
// RUN: ctjs-opt %t/effects.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=EFFECTS
// RUN: ctjs-opt %t/effects.mlir --ctnative-partial-evaluate --ctnative-partial-evaluate | FileCheck %s --check-prefix=EFFECTS
// RUN: ctjs-opt %t/equal-effects.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=EQUAL
// RUN: ctjs-opt %t/anchors.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=ANCHORS
// RUN: ctjs-opt %t/fresh.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=FRESH
// RUN: ctjs-opt %t/fresh.mlir --ctnative-partial-evaluate --ctnative-partial-evaluate | FileCheck %s --check-prefix=FRESH
// RUN: ctjs-opt %t/fresh.mlir '--ctnative-partial-evaluate=max-nodes=3' | FileCheck %s --check-prefix=NODES
// RUN: ctjs-opt %t/sharing.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=SHARING
// RUN: python3 -c "from pathlib import Path; p=Path(r'%t/sharing.mlir'); s=p.read_text(); a='private @original'+chr(36)+'1('; b='private @variant('; Path(r'%t/reverse-sharing.mlir').write_text(s.replace(a, 'private @temporary(').replace(b, a).replace('private @temporary(', b))"
// RUN: ctjs-opt %t/reverse-sharing.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=SHARING
// RUN: ctjs-opt %t/zero.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=ZERO
// RUN: ctjs-opt %t/order.mlir --ctnative-partial-evaluate | FileCheck %s --check-prefix=ORDER

// The boxed callee and native symbol must agree under this call's actual
// arguments. Equal scalar returns do not prove equal heap effects. These
// annotations are deliberately plausible and deliberately untrusted.

//--- effects.mlir
module {
  ctjs.func private @original$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"value">
    %two = ctjs.constant #ctjs.number<4611686018427387904>
    ctjs.set_property %object[%key], %two
    ctjs.return %u
  }
  ctjs.func private @variant(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32, ctnative.specialized_from = @original$1, ctnative.partial_evaluated = "forged"} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
  // EFFECTS-LABEL: ctjs.func private @factory$2
  // EFFECTS-SAME: boundary = "ctjs.call_direct"
  // EFFECTS-SAME: mode = "prefix"
  // EFFECTS: %[[OBJECT:.*]] = ctjs.create_object
  // EFFECTS: %[[ONE:.*]] = ctjs.constant #ctjs.number<4607182418800017408>
  // EFFECTS: ctjs.set_property %[[OBJECT]][{{.*}}], %[[ONE]]
  // EFFECTS: ctjs.call_direct @variant({{.*}}, %[[OBJECT]])
  // EFFECTS-NOT: ctjs.call_direct
  // EFFECTS: %[[OBSERVED:.*]] = ctjs.get_property %[[OBJECT]]
  // EFFECTS: ctjs.return %[[OBSERVED]]
  ctjs.func private @factory$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %object = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.set_property %object[%key], %one
    %actual = ctjs.create_closure %callee[1] this %u
    %ignored = ctjs.call_direct @variant(%u, %u, %actual, %object)
    %observed = ctjs.get_property %object[%key]
    ctjs.return %observed
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %result = ctjs.call_direct @factory$2(%u, %u, %u)
    ctjs.return %result
  }
}

//--- equal-effects.mlir
module {
  ctjs.func private @original$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %two = ctjs.binary add %one, %one
    ctjs.set_property %object[%key], %two
    ctjs.return %u
  }
  // A legitimately folded body needs no provenance attribute to be accepted.
  ctjs.func private @variant(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %object: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"value">
    %two = ctjs.constant #ctjs.number<4611686018427387904>
    ctjs.set_property %object[%key], %two
    ctjs.return %u
  }
  // EQUAL-LABEL: ctjs.func private @factory$2
  // EQUAL-SAME: ctnative.partial_evaluated =
  // EQUAL-NOT: mode = "prefix"
  // EQUAL-NOT: ctjs.call_direct
  // EQUAL: %[[TWO:.*]] = ctjs.constant #ctjs.number<4611686018427387904>
  // EQUAL-NEXT: ctjs.return %[[TWO]]
  ctjs.func private @factory$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %object = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.set_property %object[%key], %one
    %actual = ctjs.create_closure %callee[1] this %u
    %ignored = ctjs.call_direct @variant(%u, %u, %actual, %object)
    %observed = ctjs.get_property %object[%key]
    ctjs.return %observed
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %result = ctjs.call_direct @factory$2(%u, %u, %u)
    ctjs.return %result
  }
}

//--- anchors.mlir
module {
  // Existing, equal-looking identities cannot be renamed. The caller can
  // distinguish these returns even though both objects have empty fields.
  ctjs.func private @original$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %left: !ctjs.value, %right: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    ctjs.return %left
  }
  ctjs.func private @variant(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value, %left: !ctjs.value, %right: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    ctjs.return %right
  }
  // ANCHORS-LABEL: ctjs.func private @factory$2
  // ANCHORS-SAME: boundary = "ctjs.call_direct"
  // ANCHORS: %[[LEFT:.*]] = ctjs.create_object
  // ANCHORS: %[[RIGHT:.*]] = ctjs.create_object
  // ANCHORS: %[[RESULT:.*]] = ctjs.call_direct @variant({{.*}}, %[[LEFT]], %[[RIGHT]])
  // ANCHORS: ctjs.compare strict_eq %[[RESULT]], %[[LEFT]]
  ctjs.func private @factory$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %left = ctjs.create_object
    %right = ctjs.create_object
    %actual = ctjs.create_closure %callee[1] this %u
    %result = ctjs.call_direct @variant(%u, %u, %actual, %left, %right)
    %same = ctjs.compare strict_eq %result, %left
    ctjs.return %same
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %result = ctjs.call_direct @factory$2(%u, %u, %u)
    ctjs.return %result
  }
}

//--- fresh.mlir
module {
  // Fresh allocation order and unreachable scratch nodes may differ. The
  // returned two-node graph must retain one child shared by both fields.
  ctjs.func private @original$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %root = ctjs.create_object
    %child = ctjs.create_object
    %left = ctjs.constant #ctjs.string<"left">
    %right = ctjs.constant #ctjs.string<"right">
    ctjs.set_property %root[%left], %child
    ctjs.set_property %root[%right], %child
    ctjs.return %root
  }
  ctjs.func private @variant(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %scratch = ctjs.create_object
    %child = ctjs.create_object
    %root = ctjs.create_object
    %left = ctjs.constant #ctjs.string<"left">
    %right = ctjs.constant #ctjs.string<"right">
    ctjs.set_property %root[%left], %child
    ctjs.set_property %root[%right], %child
    ctjs.return %root
  }
  // FRESH-LABEL: ctjs.func private @factory$2
  // FRESH-SAME: residual_nodes = 2 : i64
  // FRESH: %[[ROOT:.*]] = ctjs.create_object
  // FRESH: %[[CHILD:.*]] = ctjs.create_object
  // FRESH-NOT: ctjs.create_object
  // FRESH: ctjs.set_property %[[ROOT]][{{.*}}], %[[CHILD]]
  // FRESH: ctjs.set_property %[[ROOT]][{{.*}}], %[[CHILD]]
  // FRESH-NOT: ctjs.call_direct
  // FRESH: ctjs.return %[[ROOT]]
  // NODES-LABEL: ctjs.func private @factory$2
  // NODES-SAME: ctnative.partial_eval_reason = "heap-node budget exhausted"
  // NODES: ctjs.call_direct @variant
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

//--- sharing.mlir
module {
  // Equal fields are insufficient: two distinct fresh children cannot both
  // map to one shared child. Mutation through left distinguishes the graph.
  ctjs.func private @original$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %root = ctjs.create_object
    %child = ctjs.create_object
    %left = ctjs.constant #ctjs.string<"left">
    %right = ctjs.constant #ctjs.string<"right">
    ctjs.set_property %root[%left], %child
    ctjs.set_property %root[%right], %child
    ctjs.return %root
  }
  ctjs.func private @variant(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %root = ctjs.create_object
    %first = ctjs.create_object
    %second = ctjs.create_object
    %left = ctjs.constant #ctjs.string<"left">
    %right = ctjs.constant #ctjs.string<"right">
    ctjs.set_property %root[%left], %first
    ctjs.set_property %root[%right], %second
    ctjs.return %root
  }
  // SHARING-LABEL: ctjs.func private @factory$2
  // SHARING-SAME: boundary = "ctjs.call_direct"
  // SHARING: %[[RESULT:.*]] = ctjs.call_direct @variant
  // SHARING: ctjs.return %[[RESULT]]
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

//--- zero.mlir
module {
  // JavaScript equality and Map SameValueZero merge these numbers, but their
  // reciprocals differ. Compare primitive attributes exactly in this proof.
  ctjs.func private @original$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %negative = ctjs.constant #ctjs.number<9223372036854775808>
    ctjs.return %negative
  }
  ctjs.func private @variant(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %positive = ctjs.constant #ctjs.number<0>
    ctjs.return %positive
  }
  // ZERO-LABEL: ctjs.func private @factory$2
  // ZERO-SAME: boundary = "ctjs.call_direct"
  // ZERO: %[[RESULT:.*]] = ctjs.call_direct @variant
  // ZERO: ctjs.return %[[RESULT]]
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

//--- order.mlir
module {
  // Both Maps have identical key/value pairs and size. Their iteration order
  // differs, so a scalar or unordered-map comparison would be unsound.
  ctjs.func private @original$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %constructor = ctjs.load_global "Map"
    %map = ctjs.construct %constructor(%constructor)
    %setKey = ctjs.constant #ctjs.string<"set">
    %setter = ctjs.get_property %map[%setKey]
    %left = ctjs.constant #ctjs.string<"left">
    %right = ctjs.constant #ctjs.string<"right">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %a = ctjs.call %setter(%map, %left, %one)
    %b = ctjs.call %setter(%map, %right, %one)
    ctjs.return %map
  }
  ctjs.func private @variant(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %constructor = ctjs.load_global "Map"
    %map = ctjs.construct %constructor(%constructor)
    %setKey = ctjs.constant #ctjs.string<"set">
    %setter = ctjs.get_property %map[%setKey]
    %left = ctjs.constant #ctjs.string<"left">
    %right = ctjs.constant #ctjs.string<"right">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %b = ctjs.call %setter(%map, %right, %one)
    %a = ctjs.call %setter(%map, %left, %one)
    ctjs.return %map
  }
  // ORDER-LABEL: ctjs.func private @factory$2
  // ORDER-SAME: boundary = "ctjs.call_direct"
  // ORDER: %[[RESULT:.*]] = ctjs.call_direct @variant
  // ORDER: ctjs.return %[[RESULT]]
  ctjs.func private @factory$2(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %actual = ctjs.create_closure %callee[1] this %u
    %result = ctjs.call_direct @variant(%u, %u, %actual)
    ctjs.return %result
  }
  ctjs.func @entry(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %result = ctjs.call_direct @factory$2(%u, %u, %u)
    ctjs.return %u
  }
}
