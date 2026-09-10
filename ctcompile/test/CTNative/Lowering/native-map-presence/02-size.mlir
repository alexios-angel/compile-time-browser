// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/zero_saved_before_growth.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ZERO --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/zero_signed_key.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ZERO --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/zero_both_clearing_arms.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ZERO --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/zero_selected_snapshot.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ZERO --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/zero_size_before_clear.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/zero_size_after_growth.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/zero_one_clearing_arm.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/zero_empty_entry_intersection.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/zero_same_schema_distinct_instance.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/zero_selected_nonzero_arm.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_exact_snapshot.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EXACT --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_duplicate_literal_key.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EXACT --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_saved_before_growth.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EXACT --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_both_same_key_arms.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EXACT --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_selected_snapshot.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EXACT --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_fluent_alias.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EXACT --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_two_exact_keys.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EXACT --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_two_selected_snapshot.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EXACT --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_before_insertion.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_after_second_insertion.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_startup_lower_bound.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_conditional_second_key.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_different_singleton_arms.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=JOIN --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_selected_two_arm.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_fluent_clear_invalidates.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_same_schema_distinct_instance.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/one_unknown_key_equality.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_last.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DELETE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_one_of_two.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DELETE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_absent.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DELETE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_repeated.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DELETE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_snapshot_before.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DELETE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_both_arms.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DELETE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_selected_zero.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DELETE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_fluent_alias.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DELETE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_before_snapshot.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_after_snapshot.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_one_arm.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_different_survivors.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=JOIN --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_selected_nonzero.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_signed_zero.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DELETE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_alias_after_store.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_same_schema_other_instance.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_same_schema_disjoint_key.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DELETE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/delete_possibly_equal_keys.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE

// MUTATION: emitc.func
// MUTATION-DAG: call_opaque "ctnative::map_size"
// MUTATION-DAG: call_opaque "ctnative::map_set"
// MUTATION-DAG: call_opaque "ctnative::map_get_present"

// JOIN: emitc.func
// JOIN-DAG: call_opaque "ctnative::map_size"
// JOIN-DAG: call_opaque "ctnative::map_get_present"

// DELETE: emitc.func
// DELETE-DAG: call_opaque "ctnative::map_delete"
// DELETE-DAG: call_opaque "ctnative::map_get_present"

// EXACT: emitc.func
// EXACT: call_opaque "ctnative::map_get_present"

// ZERO: emitc.func
// ZERO: call_opaque "ctnative::map_get_present"

// NATIVE: emitc.func @ensure_2
// NATIVE: call_opaque "ctnative::map_get_present"
// PRESENCE: ctnative.not_native = "nested native Map get requires presence on every reaching path for the same instance and key; has observations must survive intervening effects"
// FLOW: ctnative.not_native = "unstructured control flow"

//--- zero_saved_before_growth.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    const zero = outer.size;
    outer.set(1, inner);
    outer.set(zero, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- zero_signed_key.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    const zero = outer.size;
    outer.set(zero, inner);
    return outer.get(-0).size;
}
var result = probe(true);

//--- zero_both_clearing_arms.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.set(1, inner);
    if (flag) { outer.clear(); } else { outer.clear(); outer.clear(); }
    const zero = outer.size;
    outer.set(zero, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- zero_selected_snapshot.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    const zero = flag ? outer.size : -0;
    outer.set(zero, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- zero_size_before_clear.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.set(1, inner);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- zero_size_after_growth.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    const saved = outer.size;
    outer.set(saved, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- zero_one_clearing_arm.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.set(1, inner);
    if (flag) { outer.clear(); }
    const saved = outer.size;
    outer.set(saved, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- zero_empty_entry_intersection.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    if (flag) { outer.set(1, inner); } else { outer.set(2, inner); }
    const saved = outer.size;
    outer.set(saved, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- zero_same_schema_distinct_instance.js
function identity(map) { return map; }
function probe(flag) {
    const first = new Map();
    const second = new Map();
    const inner = new Map();
    inner.set(0, 42);
    first.set(1, inner);
    second.set(1, inner);
    identity(first);
    identity(second);
    first.clear();
    const saved = second.size;
    second.set(saved, inner);
    return second.get(0).size;
}
var result = probe(true);

//--- zero_selected_nonzero_arm.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    const saved = flag ? outer.size : 1;
    outer.set(saved, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- one_exact_snapshot.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- one_duplicate_literal_key.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(1, inner);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- one_saved_before_growth.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    const saved = outer.size;
    outer.set(2, inner);
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- one_both_same_key_arms.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    if (flag) { outer.set(1, inner); }
    else { outer.set(1, inner); outer.set(1, inner); }
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- one_selected_snapshot.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    const saved = flag ? outer.size : 1;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- one_fluent_alias.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    const alias = outer.set(1, inner);
    const saved = alias.size;
    alias.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- one_two_exact_keys.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(2).size;
}
var result = probe(true);

//--- one_two_selected_snapshot.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    const saved = flag ? outer.size : 2;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(2).size;
}
var result = probe(true);

//--- one_before_insertion.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    const saved = outer.size;
    outer.set(1, inner);
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- one_after_second_insertion.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- one_startup_lower_bound.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.set(1, inner);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- one_conditional_second_key.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    if (flag) { outer.set(2, inner); }
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- one_different_singleton_arms.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    if (flag) { outer.set(1, inner); } else { outer.set(3, inner); }
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- one_selected_two_arm.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    const saved = flag ? outer.size : 2;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- one_fluent_clear_invalidates.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    const saved = outer.size;
    outer.clear();
    const alias = outer.set(saved, inner);
    alias.clear();
    return outer.get(1).size;
}
var result = probe(true);

//--- one_same_schema_distinct_instance.js
function identity(map) { return map; }
function probe(flag) {
    const first = new Map();
    const second = new Map();
    const inner = new Map();
    inner.set(0, 42);
    identity(first);
    identity(second);
    first.clear();
    first.set(1, inner);
    second.set(2, inner);
    const saved = second.size;
    second.clear();
    second.set(saved, inner);
    return second.get(1).size;
}
var result = probe(true);

//--- one_unknown_key_equality.js
function probe(left, right) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(left, inner);
    outer.set(right, inner);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(2).size;
}
var result = probe("x", "y");

//--- delete_last.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.delete(1);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- delete_one_of_two.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    outer.delete(2);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- delete_absent.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.delete(3);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- delete_repeated.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.delete(1);
    outer.delete(1);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- delete_snapshot_before.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    const saved = outer.size;
    outer.delete(1);
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- delete_both_arms.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(1); outer.delete(1); }
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- delete_selected_zero.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.delete(1);
    const saved = flag ? outer.size : 0;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- delete_fluent_alias.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    const alias = outer.set(1, inner);
    alias.delete(1);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- delete_before_snapshot.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    const saved = outer.size;
    outer.delete(1);
    outer.clear();
    outer.set(saved, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- delete_after_snapshot.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.delete(1);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- delete_one_arm.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    if (flag) { outer.delete(1); }
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- delete_different_survivors.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); } else { outer.delete(2); }
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- delete_selected_nonzero.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.delete(1);
    const saved = flag ? outer.size : 1;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- delete_signed_zero.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(0, inner);
    outer.delete(-0);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- delete_alias_after_store.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.delete(1);
    const saved = outer.size;
    outer.clear();
    const alias = outer.set(saved, inner);
    alias.delete(0);
    return outer.get(0).size;
}
var result = probe(true);

//--- delete_same_schema_other_instance.js
function identity(map) { return map; }
function probe(flag) {
    const first = new Map();
    const second = new Map();
    const inner = new Map();
    inner.set(0, 42);
    identity(first);
    identity(second);
    first.clear();
    first.set(1, inner);
    second.delete(1);
    const saved = first.size;
    first.clear();
    first.set(saved, inner);
    return first.get(0).size;
}
var result = probe(true);

//--- delete_same_schema_disjoint_key.js
function identity(map) { return map; }
function probe(flag) {
    const first = new Map();
    const second = new Map();
    const inner = new Map();
    inner.set(0, 42);
    identity(first);
    identity(second);
    first.clear();
    first.set(1, inner);
    second.delete(2);
    const saved = first.size;
    first.clear();
    first.set(saved, inner);
    return first.get(1).size;
}
var result = probe(true);

//--- delete_possibly_equal_keys.js
function probe(left, right) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(left, inner);
    outer.set(right, inner);
    outer.delete(left);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(0).size;
}
var result = probe("x", "y");
