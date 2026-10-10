// Keep every reaching path in refusal controls; default specialization can
// prove the single closed call even when a different argument would fail.
// RUN: split-file %s %t
// The sole closed flag selects a safe path under the default pipeline.
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/join_no_common_member.js | ctjs-opt --pass-pipeline="builtin.module(ctjs-resolve-globals,ctjs-lift-to-scf,ctnative-lower-to-emitc,emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))" -o %t/selected.mlir
// RUN: FileCheck %s --check-prefix=JOIN --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func < %t/selected.mlir
// RUN: %compilation_unit --module %t/selected.mlir --js %t/join_no_common_member.js --work %t/selected --name join_no_common_member
// RUN: %node -e "const vm = require('node:vm'), fs = require('node:fs'); const cx = vm.createContext({}); vm.runInContext(fs.readFileSync(process.argv[1], 'utf8'), cx); if (cx.result !== 1) process.exit(1);" %t/join_no_common_member.js
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/join_saved_before_reset.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=JOIN --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/join_prebranch_snapshot.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=JOIN --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/join_nested_singletons.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=JOIN --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/join_equal_two.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=JOIN --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/join_after_growth.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/join_uncertain_write.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/join_no_common_member.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/join_alias_delete.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/join_nested_unequal.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutation_insert.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MUTATION --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutation_absent_delete.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MUTATION --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutation_overwrite_common.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MUTATION --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutation_delete_common.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MUTATION --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutation_insert_delete.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MUTATION --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutation_repeated_absent.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MUTATION --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutation_repeated_overwrite.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MUTATION --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutation_saved_before_insert.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MUTATION --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutation_saved_before_delete.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MUTATION --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutation_fluent_insert.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MUTATION --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutation_old_snapshot_growth.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutation_old_snapshot_delete.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutation_uncertain_insert.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutation_uncertain_delete.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutation_one_arm_insert.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=PRESENCE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutation_no_common_member.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false | FileCheck %s --check-prefix=PRESENCE

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

//--- join_saved_before_reset.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    const saved = outer.size;
    outer.set(3, inner);
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- join_prebranch_snapshot.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    const saved = outer.size;
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    outer.clear();
    outer.set(saved, inner);
    return outer.get(2).size;
}
var result = probe(true);

//--- join_nested_singletons.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    if (flag) { outer.set(1, inner); }
    else {
        if (flag) { outer.set(2, inner); }
        else { outer.set(3, inner); }
    }
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- join_equal_two.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    if (flag) { outer.set(1, inner); outer.set(2, inner); }
    else { outer.set(3, inner); outer.set(4, inner); }
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(2).size;
}
var result = probe(true);

//--- join_after_growth.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    outer.set(3, inner);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- join_uncertain_write.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    outer.set(1, inner);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- join_no_common_member.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    const saved = outer.size;
    return outer.get(saved).size;
}
var result = probe(false);

//--- join_alias_delete.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    const alias = outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    alias.delete(1);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- join_nested_unequal.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    if (flag) { outer.set(1, inner); }
    else {
        if (flag) { outer.set(2, inner); }
        else { outer.has(3); }
    }
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);
//--- mutation_insert.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    outer.set(3, inner);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(2).size;
}
var result = probe(true);

//--- mutation_absent_delete.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    outer.delete(3);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- mutation_overwrite_common.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    if (flag) { outer.set(2, inner); }
    else { outer.set(3, inner); }
    outer.set(1, inner);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(2).size;
}
var result = probe(true);

//--- mutation_delete_common.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    if (flag) { outer.set(2, inner); }
    else { outer.set(3, inner); }
    outer.delete(1);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- mutation_insert_delete.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    outer.set(3, inner);
    outer.delete(3);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- mutation_repeated_absent.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    outer.delete(3);
    outer.delete(3);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- mutation_repeated_overwrite.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    outer.set(3, inner);
    outer.set(3, inner);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(2).size;
}
var result = probe(true);

//--- mutation_saved_before_insert.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    const saved = outer.size;
    outer.set(3, inner);
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- mutation_saved_before_delete.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    outer.set(3, inner);
    const saved = outer.size;
    outer.delete(3);
    outer.clear();
    outer.set(saved, inner);
    return outer.get(2).size;
}
var result = probe(true);

//--- mutation_fluent_insert.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    const alias = outer.set(3, inner);
    alias.set(3, inner);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(2).size;
}
var result = probe(true);

//--- mutation_old_snapshot_growth.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    const saved = outer.size;
    outer.set(3, inner);
    outer.clear();
    outer.set(saved, inner);
    return outer.get(2).size;
}
var result = probe(true);

//--- mutation_old_snapshot_delete.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    outer.set(3, inner);
    const saved = outer.size;
    outer.delete(3);
    outer.clear();
    outer.set(saved, inner);
    return outer.get(1).size;
}
var result = probe(true);

//--- mutation_uncertain_insert.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    outer.set(1, inner);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(2).size;
}
var result = probe(true);

//--- mutation_uncertain_delete.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    outer.delete(1);
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(0).size;
}
var result = probe(true);

//--- mutation_one_arm_insert.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    if (flag) { outer.set(3, inner); }
    const saved = outer.size;
    outer.clear();
    outer.set(saved, inner);
    return outer.get(2).size;
}
var result = probe(true);

//--- mutation_no_common_member.js
function probe(flag) {
    const outer = new Map();
    const inner = new Map();
    inner.set(0, 42);
    outer.clear();
    outer.set(1, inner);
    outer.set(2, inner);
    if (flag) { outer.delete(1); }
    else { outer.delete(2); }
    outer.set(3, inner);
    const saved = outer.size;
    return outer.get(saved).size;
}
var result = probe(true);
