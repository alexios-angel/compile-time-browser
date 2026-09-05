// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../native-object-values-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/field.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=FIELD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/read.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=FIELD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/alias.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=FIELD
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/numeric.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NUMERIC
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/loose.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=LOOSE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/strings.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/host.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/publish.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=REFUSED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/snapshot.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=SNAPSHOT
// RUN: ctjs-opt %t/forged.mlir --ctnative-lower-to-emitc | FileCheck %s --check-prefix=FORGED --implicit-check-not=ctnative.object_identity
// RUN: ctjs-opt %t/poison.mlir --split-input-file --ctnative-lower-to-emitc | FileCheck %s --check-prefix=POISON --implicit-check-not=ctnative.object_identity

// NATIVE-DAG: struct object_value
// NATIVE-DAG: call_opaque "ctnative::make_map<std::string, ctnative::object_value>"
// NATIVE-DAG: call_opaque "ctnative::to_object_value"
// NATIVE-DAG: call_opaque "ctnative::object_strict_equal"
// NATIVE-DAG: call_opaque "ctnative::object_truthy"
// NATIVE-DAG: call_opaque "ctnative::object_typeof"
// FIELD: ctnative.not_native =
// FIELD: identity-only Map value has an unsupported use through `ctjs.{{get|set}}_property`
// NUMERIC: ctnative.not_native = "unary operand is !ctnative.opt<!ctnative.object_identity>, not a number"
// LOOSE: ctnative.not_native = "loose object equality may invoke object-to-primitive conversion"
// REFUSED: ctnative.not_native =
// SNAPSHOT: ctnative.not_native = "native Map snapshot requires confined numeric elements"
// FORGED: ctnative.not_native =
// FORGED: identity-only Map value has an unsupported use through `ctjs.set_property`

//--- field.js
function probe() { const map = new Map(); const value = {x: 1}; map.set("x", value); return map.size; }
probe();
//--- read.js
function probe() { const map = new Map(); map.set("x", {}); return map.get("x").x; }
probe();
//--- alias.js
function touch(value) { value.x = 1; }
function probe() { const map = new Map(); map.set("x", {}); touch(map.get("x")); return map.size; }
probe();
//--- numeric.js
function probe() { const map = new Map(); map.set("x", {}); return +map.get("x"); }
probe();
//--- loose.js
function probe() { const map = new Map(); map.set("x", {}); return map.get("x") == 1; }
probe();
//--- strings.js
function probe() { const map = new Map(); map.set("x", {}); map.set("s", "text"); return map.get("x") === map.get("s"); }
probe();
//--- host.js
function probe(value) { const map = new Map(); map.set("x", {}); map.set("host", value); return map.size; }
probe(globalThis);
//--- publish.js
var published;
function probe() { const map = new Map(); const value = {}; map.set("x", value); published = value; return map.size; }
probe();
//--- snapshot.js
function probe() { const map = new Map(); map.set("x", {}); return map.values().length; }
probe();

//--- forged.mlir
module {
  ctjs.func @field$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %object = ctjs.create_object {ctnative.object_identity}
    %field = ctjs.constant #ctjs.string<"value">
    %one = ctjs.constant #ctjs.number<1>
    ctjs.set_property %object[%field], %one
    %ctor = ctjs.load_global "Map"
    %map = ctjs.construct %ctor(%ctor)
    %set = ctjs.constant #ctjs.string<"set">
    %method = ctjs.get_property %map[%set]
    %stored = ctjs.call %method(%map, %field, %object)
    ctjs.return %one
  }
}

//--- poison.mlir
// A false continuation does not excuse an observed loop exit value.
// POISON-LABEL: ctjs.func @exit_observed$1
// POISON-SAME: ctnative.not_native
// POISON: non-object producer `ub.poison`
module {
  ctjs.func @exit_observed$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %poison = ub.poison : !ctjs.value
    %yes = arith.constant true
    %no = arith.constant false
    %object = ctjs.create_object
    %one = ctjs.constant #ctjs.number<1>
    %unrelated = ctjs.truthy %one
    %key = ctjs.constant #ctjs.string<"instance">
    %set = ctjs.constant #ctjs.string<"set">
    %ctor = ctjs.load_global "Map"
    %map = ctjs.construct %ctor(%ctor)
    %method = ctjs.get_property %map[%set]
    %stored = ctjs.call %method(%map, %key, %object)
    %result = scf.while (%before = %object) : (!ctjs.value) -> !ctjs.value {
      %pair:2 = scf.if %no -> (!ctjs.value, i1) {
        scf.yield %before, %no : !ctjs.value, i1
      } else {
        scf.yield %poison, %no : !ctjs.value, i1
      }
      scf.condition(%pair#1) %pair#0 : !ctjs.value
    } do {
    ^bb0(%after: !ctjs.value):
      scf.yield %after : !ctjs.value
    }
    %observed = ctjs.compare strict_eq %result, %object
    ctjs.return %observed
  }
}

// -----
// POISON-LABEL: ctjs.func @next_observed$1
// POISON-SAME: ctnative.not_native
// POISON: non-object producer `ub.poison`
module {
  ctjs.func @next_observed$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %poison = ub.poison : !ctjs.value
    %yes = arith.constant true
    %no = arith.constant false
    %object = ctjs.create_object
    %one = ctjs.constant #ctjs.number<1>
    %unrelated = ctjs.truthy %one
    %key = ctjs.constant #ctjs.string<"instance">
    %set = ctjs.constant #ctjs.string<"set">
    %ctor = ctjs.load_global "Map"
    %map = ctjs.construct %ctor(%ctor)
    %method = ctjs.get_property %map[%set]
    %stored = ctjs.call %method(%map, %key, %object)
    %result = scf.while (%before = %object) : (!ctjs.value) -> !ctjs.value {
      %pair:2 = scf.if %no -> (!ctjs.value, i1) {
        scf.yield %before, %no : !ctjs.value, i1
      } else {
        scf.yield %poison, %yes : !ctjs.value, i1
      }
      scf.condition(%pair#1) %pair#0 : !ctjs.value
    } do {
    ^bb0(%after: !ctjs.value):
      %again = ctjs.call %method(%map, %key, %after)
      scf.yield %after : !ctjs.value
    }
    ctjs.return %one
  }
}

// -----
// POISON-LABEL: ctjs.func @unrelated_flag$1
// POISON-SAME: ctnative.not_native
// POISON: non-object producer `ub.poison`
module {
  ctjs.func @unrelated_flag$1(%this: !ctjs.value, %target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %poison = ub.poison : !ctjs.value
    %yes = arith.constant true
    %no = arith.constant false
    %object = ctjs.create_object
    %one = ctjs.constant #ctjs.number<1>
    %unrelated = ctjs.truthy %one
    %key = ctjs.constant #ctjs.string<"instance">
    %set = ctjs.constant #ctjs.string<"set">
    %ctor = ctjs.load_global "Map"
    %map = ctjs.construct %ctor(%ctor)
    %method = ctjs.get_property %map[%set]
    %stored = ctjs.call %method(%map, %key, %object)
    %result = scf.while (%before = %object) : (!ctjs.value) -> !ctjs.value {
      %pair:2 = scf.if %no -> (!ctjs.value, i1) {
        scf.yield %before, %no : !ctjs.value, i1
      } else {
        scf.yield %poison, %no : !ctjs.value, i1
      }
      scf.condition(%unrelated) %pair#0 : !ctjs.value
    } do {
    ^bb0(%after: !ctjs.value):
      %again = ctjs.call %method(%map, %key, %after)
      scf.yield %after : !ctjs.value
    }
    ctjs.return %one
  }
}
