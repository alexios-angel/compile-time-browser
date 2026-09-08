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
function savedRewrite() {
    const map = new Map();
    map.set(false, false);
    map.set('keep', 'saved-string-with-embedded-\0-bytes');
    const saved = map.get('keep');
    map.set('keep', true);
    map.delete('keep');
    map.set(false, saved);
    const result = map.get(false);
    map.set(false, false);
    map.clear();
    return result === 'saved-string-with-embedded-\0-bytes' ? 1 : 0;
}
function savedNumber() {
    const map = new Map();
    map.set(false, false);
    map.set(0, 41);
    const saved = map.get(0);
    map.delete(0);
    map.set(false, saved);
    return map.get(false) + 1;
}
function savedBoolean() {
    const map = new Map();
    map.set(false, false);
    map.set(0, 41);
    const saved = map.get(false);
    map.set(false, 99);
    map.set(0, saved);
    return map.get(0) ? 0 : 1;
}
function savedBranch(flag) {
    const map = new Map();
    map.set(false, 'saved');
    const saved = map.get(false);
    if (flag) { map.set(false, true); } else { map.delete(false); }
    map.set(false, saved);
    return map.get(false) === 'saved' ? 1 : 0;
}
function clearSaved(map) { map.clear(); }
function savedCall() {
    const map = new Map();
    map.set(false, false);
    map.set('keep', 'call');
    const saved = map.get('keep');
    clearSaved(map);
    map.set(false, saved);
    return map.get(false) === 'call' ? 1 : 0;
}
function savedAlias(key) {
    const map = new Map();
    map.set(false, false);
    map.set(0, 41);
    const saved = map.get(0);
    map.set(key, false);
    map.set(0, saved);
    return map.get(0);
}
function savedJoinString(flag) {
    const map = new Map();
    map.set(false, false);
    map.set('left', 'left-with-owned-\0-bytes');
    map.set('right', 'right-with-owned-\0-bytes');
    const saved = flag ? map.get('left') : map.get('right');
    map.set('left', true);
    map.delete('right');
    map.set(false, saved);
    const result = map.get(false);
    map.clear();
    return result;
}
function savedJoinNumber(flag, other) {
    const map = new Map();
    map.set(false, false);
    map.set(0, 11);
    map.set(1, 23);
    const saved = flag ? (other ? map.get(0) : map.get(1)) : map.get(0);
    map.delete(0);
    map.delete(1);
    map.set(false, saved);
    return map.get(false);
}
function savedJoinBoolean(flag) {
    const map = new Map();
    map.set(false, false);
    map.set(true, true);
    map.set(0, 11);
    const saved = flag ? map.get(true) : map.get(false);
    map.clear();
    map.set(0, saved);
    return map.get(0) ? 1 : 2;
}
var traceNumbers = numberKeys();
var traceSaved = savedString();
var traceBranches = branch(true) * 10 + branch(false);
var traceDead = deadAlternative();
var traceRewrite = savedRewrite();
var traceSavedNumber = savedNumber();
var traceSavedBoolean = savedBoolean();
var traceSavedBranch = savedBranch(true) * 10 + savedBranch(false);
var traceSavedCall = savedCall();
var traceSavedAlias = savedAlias(0) + savedAlias(1);
var traceSavedJoinString = (savedJoinString(true) === 'left-with-owned-\0-bytes' ? 10 : 0)
    + (savedJoinString(false) === 'right-with-owned-\0-bytes' ? 2 : 0);
var traceSavedJoinNumber = savedJoinNumber(true, true) + savedJoinNumber(true, false)
    + savedJoinNumber(false, true) + savedJoinNumber(false, false);
var traceSavedJoinBoolean = savedJoinBoolean(true) * 10 + savedJoinBoolean(false);

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

//--- saved-missing-refused.js
function run() {
    const map = new Map();
    map.set(false, false);
    map.set(0, 2);
    map.delete(0);
    const saved = map.get(0);
    map.set(false, saved);
    return map.get(false) ? 1 : 0;
}
var trace = run();

//--- saved-alias-refused.js
function run(key) {
    const map = new Map();
    map.set(false, false);
    map.set(0, 2);
    const saved = map.get(0);
    map.set(key, saved);
    return map.get(false) ? 1 : 0;
}
var trace = run(false) * 10 + run(true);

//--- saved-branch-refused.js
function run(flag) {
    const map = new Map();
    map.set(false, false);
    map.set(0, 2);
    const saved = map.get(0);
    if (flag) { map.set(false, saved); }
    return map.get(false) ? 1 : 0;
}
var trace = run(true) * 10 + run(false);

//--- saved-join-missing-refused.js
function run(flag) {
    const map = new Map();
    map.set(false, false);
    map.set(0, 42);
    const saved = flag ? map.get(0) : map.get(1);
    map.set(false, saved);
    return map.get(false) ? 1 : 2;
}
var trace = run(true) * 10 + run(false);

//--- saved-join-tags-refused.js
function run(flag) {
    const map = new Map();
    map.set(false, false);
    map.set(0, 42);
    const saved = flag ? map.get(0) : map.get(false);
    map.set(false, saved);
    return map.get(false) ? 1 : 2;
}
var trace = run(true) * 10 + run(false);
