// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-binding-time-analysis -o /dev/null
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-partial-evaluate -o /dev/null
// RUN: python3 %S/check-map-mixed.py --fixtures %t --work %t/run --translate ctjs-translate --opt ctjs-opt

//--- mixed.js
function numberKeys() {
    const map = new Map();
    const nan = 0 / 0;
    map.set(false, false);
    map.set(0, 1);
    map.set(nan, 2);
    map.set(-0, 3);
    map.set(0 / 0, 4);
    return map.size * 100 + map.get(0) * 10 + map.get(nan) + (map.has(false) ? 1000 : 0);
}
function savedString() {
    const map = new Map();
    map.set(false, false);
    map.set('keep', 'long-owning-string-with-embedded-\0-bytes-for-independent-storage');
    const saved = map.get('keep');
    map.set('keep', false);
    map.delete('keep');
    map.clear();
    return saved === 'long-owning-string-with-embedded-\0-bytes-for-independent-storage' ? 1 : 0;
}
function branch(flag) {
    const map = new Map();
    map.set(false, false);
    if (flag) { map.set(false, 2); } else { map.set(false, 3); }
    return map.get(false);
}
function deadAlternative() {
    const map = new Map();
    if (false) { map.set(false, false); }
    map.set(false, 2);
    return map.get(false);
}
var traceNumbers = numberKeys();
var traceSaved = savedString();
var traceBranches = branch(true) * 10 + branch(false);
var traceDead = deadAlternative();

//--- snapshot.js
function snapshot() {
    const map = new Map();
    map.set('a', 1);
    const keys = Array.from(map.keys());
    return keys.length;
}
var traceSnapshot = snapshot();

//--- branch-refused.js
function run(flag) {
    const map = new Map();
    map.set(false, false);
    if (flag) { map.set(false, 2); }
    return map.get(false) ? 1 : 0;
}
var trace = run(true) + run(false);

//--- alias-refused.js
function run(key) {
    const map = new Map();
    map.set(false, false);
    map.set(key, 2);
    return map.get(false) ? 1 : 0;
}
var trace = run(false);

//--- call-refused.js
function mutate(map) { map.set(false, 2); }
function run() {
    const map = new Map();
    map.set(false, false);
    mutate(map);
    return map.get(false) ? 1 : 0;
}
var trace = run();

//--- receiver-alias-refused.js
function read(a, b) {
    a.set(false, false);
    b.set(false, 2);
    return a.get(false) ? 1 : 0;
}
function run() {
    const a = new Map();
    const b = new Map();
    return read(a, a) + read(a, b);
}
var trace = run();

//--- has-refused.js
function run(flag) {
    const map = new Map();
    if (flag) { map.set(false, 2); } else { map.set(false, false); }
    if (map.has(false)) { return map.get(false) ? 1 : 0; }
    return 0;
}
var trace = run(true) + run(false);

//--- deleted-refused.js
function run() {
    const map = new Map();
    map.set(false, false);
    map.set(0, 2);
    map.delete(false);
    return map.get(false) ? 1 : 0;
}
var trace = run();

//--- nonliteral-refused.js
function run(value) {
    const map = new Map();
    map.set(false, false);
    map.set(false, value);
    return map.get(false) ? 1 : 0;
}
var trace = run(2);
