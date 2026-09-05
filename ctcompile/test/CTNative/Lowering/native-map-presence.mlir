// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../native-map-presence-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n01_cached_has_delete.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n02_cached_has_clear.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n03_delete_inside_true_arm.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n04_ssa_alias_delete.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n05_set_alias_clear.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n06_callee_clear.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n07_captured_clear.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n08_same_schema_other_allocation.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n09_wrong_literal_key.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n10_reassigned_key.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n11_captured_key_write.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n12_one_branch_inserts.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n13_has_or_unrelated_true.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n14_negated_has.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n15_cached_has_loop_backedge.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n16_zero_iteration_insert.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n17_clear_then_break.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n18_clear_then_continue.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n19_finally_clears.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=FLOW
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n20_short_circuit_effect.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n21_get_key_callee_effect.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n22_invalidation_one_branch.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n23_early_guard_then_clear.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n24_while_exit_absent.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n25_negated_has_or_set.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n26_has_and_set.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n27_transitive_callee_clear.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n28_recursive_callee_clear.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/n29_returned_closure_clear.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=PRESENCE

// NATIVE: emitc.func @ensure_2
// NATIVE: call_opaque "ctnative::map_get_present"
// PRESENCE: ctnative.not_native = "nested native Map get requires presence on every reaching path for the same instance and key; has observations must survive intervening effects"
// FLOW: ctnative.not_native = "unstructured control flow"

//--- n01_cached_has_delete.js
// Independent presence-proof review: A saved true has does not survive deletion of its key.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    var known = outer.has("key");
    outer.delete("key");
    if (known) { return outer.get("key").size; }
    return 0;
}
var result = probe();

//--- n02_cached_has_clear.js
// Independent presence-proof review: A saved true has does not survive clear.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    var known = outer.has("key");
    outer.clear();
    if (known) { return outer.get("key").size; }
    return 0;
}
var result = probe();

//--- n03_delete_inside_true_arm.js
// Independent presence-proof review: Deleting after the has test invalidates its active true-arm refinement.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    if (outer.has("key")) {
        outer.delete("key");
        return outer.get("key").size;
    }
    return 0;
}
var result = probe();

//--- n04_ssa_alias_delete.js
// Independent presence-proof review: A local alias deletes the same runtime Map.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    var alias = outer;
    var known = outer.has("key");
    alias.delete("key");
    if (known) { return outer.get("key").size; }
    return 0;
}
var result = probe();

//--- n05_set_alias_clear.js
// Independent presence-proof review: The result of set is the same receiver, so clearing it invalidates presence.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    var alias = outer.set("other", inner);
    var known = outer.has("key");
    alias.clear();
    if (known) { return outer.get("key").size; }
    return 0;
}
var result = probe();

//--- n06_callee_clear.js
// Independent presence-proof review: A closed direct callee can mutate the receiver through its parameter.
function erase(map) { map.clear(); return 0; }
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    var known = outer.has("key");
    erase(outer);
    if (known) { return outer.get("key").size; }
    return 0;
}
var result = probe();

//--- n07_captured_clear.js
// Independent presence-proof review: A lifted closure mutates the same captured Map between has and get.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    function erase() { outer.clear(); return 0; }
    var known = outer.has("key");
    erase();
    if (known) { return outer.get("key").size; }
    return 0;
}
var result = probe();

//--- n08_same_schema_other_allocation.js
// Independent presence-proof review: Sharing a formal gives first and second one schema, not one identity.
function touch(map) { return map.size; }
function probe(flag) {
var first = new Map();
    var second = new Map();
    var child = new Map();
    child.set("value", 42);
    first.set("key", child);
    touch(first);
    touch(second);
    if (first.has("key")) { return second.get("key").size; }
    return 0;
}
var result = probe();

//--- n09_wrong_literal_key.js
// Independent presence-proof review: Presence of one string key proves nothing about another string.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    if (outer.has("key")) { return outer.get("absent").size; }
    return 0;
}
var result = probe();

//--- n10_reassigned_key.js
// Independent presence-proof review: The has and get use different SSA values after assigning the key binding.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    var key = "key";
    var known = outer.has(key);
    key = "absent";
    if (known) { return outer.get(key).size; }
    return 0;
}
var result = probe();

//--- n11_captured_key_write.js
// Independent presence-proof review: A captured mutable string key changes after the has observation.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    var key = "key";
    function change() { key = "absent"; return 0; }
    var known = outer.has(key);
    change();
    if (known) { return outer.get(key).size; }
    return 0;
}
var result = probe();

//--- n12_one_branch_inserts.js
// Independent presence-proof review: The join has an absent predecessor even though the other branch inserts.
function probe(flag) {
var outer = new Map();
    var child = new Map();
    child.set("value", 42);
    if (flag) { outer.set("key", child); }
    return outer.get("key").size;
}
var result = probe(false);

//--- n13_has_or_unrelated_true.js
// Independent presence-proof review: A true OR can come from the unrelated flag while has is false.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    outer.clear();
    if (outer.has("key") || flag) { return outer.get("key").size; }
    return 0;
}
var result = probe(true);

//--- n14_negated_has.js
// Independent presence-proof review: The true arm of not-has establishes absence, not presence.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    outer.clear();
    if (!outer.has("key")) { return outer.get("key").size; }
    return 0;
}
var result = probe();

//--- n15_cached_has_loop_backedge.js
// Independent presence-proof review: The first iteration clears the map before the same saved guard is reused.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    var known = outer.has("key");
    var total = 0;
    for (var i = 0; i < 2; i = i + 1) {
        if (known) { total = total + outer.get("key").size; }
        outer.clear();
    }
    return total;
}
var result = probe();

//--- n16_zero_iteration_insert.js
// Independent presence-proof review: A loop-body set does not dominate the zero-iteration exit.
function probe(flag) {
var outer = new Map();
    var child = new Map();
    child.set("value", 42);
    for (var i = 0; i < flag; i = i + 1) { outer.set("key", child); }
    return outer.get("key").size;
}
var result = probe(0);

//--- n17_clear_then_break.js
// Independent presence-proof review: The break exit reaches get with the key removed.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    while (outer.has("key")) { outer.clear(); break; }
    return outer.get("key").size;
}
var result = probe();

//--- n18_clear_then_continue.js
// Independent presence-proof review: A continue edge carries the invalidation into a later iteration.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    var known = outer.has("key");
    var total = 0;
    for (var i = 0; i < 2; i = i + 1) {
        if (i === 0) { outer.clear(); continue; }
        if (known) { total = total + outer.get("key").size; }
    }
    return total;
}
var result = probe();

//--- n19_finally_clears.js
// Independent presence-proof review: A finally block runs after the early-return guard and before the later get.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    try { if (!outer.has("key")) { return 0; } }
    finally { outer.clear(); }
    return outer.get("key").size;
}
var result = probe();

//--- n20_short_circuit_effect.js
// Independent presence-proof review: The right side of AND clears the map after has returned true.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    if (outer.has("key") && (outer.clear(), true)) { return outer.get("key").size; }
    return 0;
}
var result = probe();

//--- n21_get_key_callee_effect.js
// Independent presence-proof review: Evaluating the get argument mutates the receiver after the guard.
function erase(map) { map.clear(); return 0; }
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    if (outer.has("key")) { return outer.get((erase(outer), "key")).size; }
    return 0;
}
var result = probe();

//--- n22_invalidation_one_branch.js
// Independent presence-proof review: At the join one predecessor has invalidated the saved has fact.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    var known = outer.has("key");
    if (flag) { outer.clear(); }
    if (known) { return outer.get("key").size; }
    return 0;
}
var result = probe(true);

//--- n23_early_guard_then_clear.js
// Independent presence-proof review: The surviving arm of an early return still loses presence after mutation.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    if (!outer.has("key")) { return 0; }
    outer.clear();
    return outer.get("key").size;
}
var result = probe();

//--- n24_while_exit_absent.js
// Independent presence-proof review: A while loop exits on false has; its body-true fact must not leak outward.
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    while (outer.has("key")) { outer.delete("key"); }
    return outer.get("key").size;
}
var result = probe();

//--- n25_negated_has_or_set.js
// Independent presence-proof review: Not-has short-circuits OR when absent, so set is skipped.
function probe(flag) {
var outer = new Map();
    var child = new Map();
    child.set("value", 42);
    !outer.has("key") || outer.set("key", child);
    return outer.get("key").size;
}
var result = probe();

//--- n26_has_and_set.js
// Independent presence-proof review: has AND set leaves the absent path absent.
function probe(flag) {
var outer = new Map();
    var child = new Map();
    child.set("value", 42);
    outer.has("key") && outer.set("key", child);
    return outer.get("key").size;
}
var result = probe();

//--- n27_transitive_callee_clear.js
// Independent presence-proof review: A direct callee transitively invokes an eraser through another closed function.
function forward(map) { return erase(map); }
function erase(map) { map.clear(); return 0; }
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    var known = outer.has("key");
    forward(outer);
    if (known) { return outer.get("key").size; }
    return 0;
}
var result = probe();

//--- n28_recursive_callee_clear.js
// Independent presence-proof review: A recursive call graph must propagate its reachable clearing effect to the caller.
function first(map, n) { if (n > 0) { return second(map, n - 1); } return 0; }
function second(map, n) { map.clear(); return first(map, n); }
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    var known = outer.has("key");
    first(outer, 1);
    if (known) { return outer.get("key").size; }
    return 0;
}
var result = probe();

//--- n29_returned_closure_clear.js
// Independent presence-proof review: A returned callable mutates its retained Map capture between has and get.
function makeEraser(map) { return function erase() { map.clear(); return 0; }; }
function probe(flag) {
var outer = new Map();
    var inner = new Map();
    inner.set("value", 42);
    outer.set("key", inner);

    var erase = makeEraser(outer);
    var known = outer.has("key");
    erase();
    if (known) { return outer.get("key").size; }
    return 0;
}
var result = probe();
