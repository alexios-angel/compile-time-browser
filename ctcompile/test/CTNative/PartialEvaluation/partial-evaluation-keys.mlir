// RUN: split-file %s %t
// RUN: python3 -c "from pathlib import Path; Path(r'%t/long-keys.js').write_text(Path(r'%t/long-keys.js.in').read_text().replace('LONG_KEY', 'x' * 65537))"
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/long-keys.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-partial-evaluate | FileCheck %s --check-prefix=LONG

// Heap keys do not share the primitive execution string budget. Two writes
// through the same long key must replace one entry, and an object read must
// still find the final value. Rejecting a conversion must never mean unequal.
// LONG-LABEL: ctjs.func private @longMapKey$1
// LONG-SAME: ctnative.partial_evaluated =
// LONG: %[[SIZE:.*]] = ctjs.constant #ctjs.number<4607182418800017408>
// LONG-NEXT: ctjs.return %[[SIZE]]
// LONG-LABEL: ctjs.func private @longObjectKey$2
// LONG-SAME: ctnative.partial_evaluated =
// LONG: %[[VALUE:.*]] = ctjs.constant #ctjs.number<4611686018427387904>
// LONG-NEXT: ctjs.return %[[VALUE]]

//--- long-keys.js.in
function longMapKey() {
    const map = new Map();
    map.set("LONG_KEY", 1);
    map.set("LONG_KEY", 2);
    return map.size;
}
function longObjectKey() {
    const object = { "LONG_KEY": 1 };
    object["LONG_KEY"] = 2;
    return object["LONG_KEY"];
}
var long_map_key = longMapKey();
var long_object_key = longObjectKey();
