// The identity proof prepareNativeMaps (Analysis/NativeMap.cpp) asks before
// any native Map: whether a Map method can run anything but the realm's own
// Map.prototype method. mapRealmUnproved proves it without looking at the
// rest of the module when `Map` is the realm's own (its ctjs.globals row has
// host_binding: --ctjs-resolve-globals' clause 5 holds and nothing binds the
// name), no read off the global object may yield `Map` (ctjs.global_map_reads),
// nothing names a property `Map`, and no read reaches a prototype without
// naming it. Each Map family then stands on its own proof, so an unknown call
// elsewhere no longer refuses every Map. Without that proof the old
// whole-module walk decides, and any unknown call or other global read
// refuses them all.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unknown-call.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=UNKNOWN-CALL --implicit-check-not=ctnative.map_reason
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/escape.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ESCAPE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/proto.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=PROTO
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/computed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals \
// RUN:   | FileCheck %s --check-prefix=COMPUTED-READS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/computed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=COMPUTED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/families.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=FAMILIES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/nested.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=NESTED

// --- An unknown call beside the Map ------------------------------------------
// `JSON.parse` is a call the module cannot name, and `JSON` another global;
// neither can reach Map.prototype, so the Map in `probe` is native.
// UNKNOWN-CALL: ctjs.func private @probe$1
// UNKNOWN-CALL: ctjs.construct {{.*}}ctnative.map_site

// --- The global object escapes ------------------------------------------------
// `JSON.stringify(globalThis)` hands the global object to code the module
// cannot see, which could rebind `Map` or reach its prototype: clause 5 fails,
// `Map` has no host_binding, and the whole-module walk refuses the Map.
// ESCAPE: ctjs.func {{(private )?}}@probe$1
// ESCAPE: ctjs.construct {{.*}}ctnative.map_reason = "standard Map identity is unproved with other host/global value reads"

// --- A read that reaches a prototype -------------------------------------------
// Only `__proto__` differs from unknown-call.js.
// PROTO: ctjs.func {{(private )?}}@probe$1
// PROTO: ctjs.construct {{.*}}ctnative.map_reason = "standard Map identity is unproved with other host/global value reads"

// --- A computed read off the global object --------------------------------------
// `globalThis[key]` may be the Map constructor, whose `prototype` the next
// read would reach; clause 5 itself holds.
// COMPUTED-READS: ctjs.global_map_reads = ["ctjs.get_property at {{[^"]*}}"]
// COMPUTED: ctjs.func {{(private )?}}@probe$1
// COMPUTED: ctjs.construct {{.*}}ctnative.map_reason = "standard Map identity is unproved with other host/global value reads"

// --- One family escapes ------------------------------------------------------------
// The Map `leak` hands to `JSON.stringify` stays boxed with its own reason;
// the one in `keep` is native.
// FAMILIES: ctjs.func private @keep$1
// FAMILIES: ctjs.construct {{.*}}ctnative.map_site
// FAMILIES: ctjs.func private @leak$2
// FAMILIES: ctjs.construct {{.*}}ctnative.map_reason = "native Map instance escapes or is mutated through `ctjs.call`"

// --- A family that stores a refused family's Maps ----------------------------------
// `outer` holds `inner`, which escapes, so `outer` stays boxed too; `keep`
// is native.
// NESTED: ctjs.func private @nest$1
// NESTED: ctjs.construct {{.*}}ctnative.map_reason = "native Map instance escapes or is mutated through `ctjs.call`"
// NESTED: ctjs.construct {{.*}}ctnative.map_reason = "native Map instance escapes or is mutated through `ctjs.call`"
// NESTED: ctjs.func private @keep$2
// NESTED: ctjs.construct {{.*}}ctnative.map_site

//--- unknown-call.js
function probe() { var map = new Map(); map.set(1, 2); return map.size; }
function other(text) { return JSON.parse(text); }
var a = probe();
var b = other("1");

//--- escape.js
function probe() { var map = new Map(); map.set(1, 2); return map.size; }
function other(text) { return JSON.parse(text); }
var a = probe();
var b = other("1");
JSON.stringify(globalThis);

//--- proto.js
function probe() { var map = new Map(); map.set(1, 2); return map.size; }
function other(text) { return JSON.parse(text).__proto__; }
var a = probe();
var b = other("1");

//--- computed.js
function probe() { var map = new Map(); map.set(1, 2); return map.size; }
function other(text) { return JSON.parse(text); }
var key = "Ma" + "p";
var a = probe();
var b = other("1");
var c = typeof globalThis[key];

//--- families.js
function keep() { var map = new Map(); map.set(1, 2); return map.size; }
function leak() { var map = new Map(); map.set(1, 2); JSON.stringify(map); return map.size; }
var a = keep();
var b = leak();

//--- nested.js
function nest() {
    var inner = new Map();
    inner.set(1, 2);
    JSON.stringify(inner);
    var outer = new Map();
    outer.set(1, inner);
    return outer.size;
}
function keep() { var map = new Map(); map.set(1, 2); return map.size; }
var a = nest();
var b = keep();
