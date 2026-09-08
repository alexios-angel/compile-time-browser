// RUN: rm -rf %t
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
function guardedSavedString(flag) {
    const map = new Map();
    map.set(false, false);
    map.set('keep', 'guarded-owning-\0-string');
    map.set('fallback', 'fallback');
    if (flag) { map.delete('keep'); }
    const saved = map.has('keep') ? map.get('keep') : map.get('fallback');
    map.set('keep', false);
    map.delete('fallback');
    map.set(false, saved);
    const result = map.get(false);
    map.clear();
    return result;
}
function guardedSavedNumber(flag) {
    const map = new Map();
    map.set(false, false);
    map.set(0, 11);
    map.set(1, 23);
    if (flag) { map.delete(0); }
    const saved = map.has(0) ? map.get(0) : map.get(1);
    map.clear();
    map.set(false, saved);
    return map.get(false);
}
function guardedSavedBoolean(flag) {
    const map = new Map();
    map.set(false, false);
    map.set(true, true);
    map.set(0, 11);
    if (flag) { map.delete(true); }
    const saved = map.has(true) ? map.get(true) : map.get(false);
    map.clear();
    map.set(0, saved);
    return map.get(0) ? 1 : 2;
}
function guardedDisjoint(flag) {
    const map = new Map();
    map.set(false, false);
    map.set(0, 11);
    map.set(1, 23);
    if (flag) { map.delete(0); }
    const seen = map.has(0);
    map.delete(1);
    return seen ? map.get(0) : 7;
}
function shortString(flag, empty) {
    const map = new Map();
    map.set(false, true);
    map.set(0, 'fallback');
    map.set(1, empty ? '' : 'selected');
    if (flag) { map.delete(1); }
    const saved = (map.has(1) && map.get(1)) || map.get(0);
    map.clear();
    map.set(false, saved);
    const result = map.get(false);
    map.delete(false);
    return result;
}
function shortNumber(flag, zero) {
    const map = new Map();
    map.set(false, false);
    map.set(0, 3);
    map.set(1, zero ? 0 : 11);
    if (flag) { map.delete(1); }
    const saved = (map.has(1) && map.get(1)) || map.get(0);
    map.clear();
    map.set(false, saved);
    return map.get(false);
}
function shortBoolean(flag) {
    const map = new Map();
    map.set(0, 11);
    map.set(false, true);
    map.set(true, false);
    if (flag) { map.delete(true); }
    const saved = (map.has(true) && map.get(true)) || map.get(false);
    map.clear();
    map.set(0, saved);
    return map.get(0) ? 1 : 2;
}
function shortSaved(flag) {
    const map = new Map();
    map.set(false, true);
    map.set(0, 'fallback');
    map.set(1, 'owning-short-\0-string');
    if (flag) { map.delete(1); }
    const intermediate = map.has(1) && map.get(1);
    map.delete(1);
    const saved = intermediate || map.get(0);
    map.clear();
    map.set(false, saved);
    return map.get(false);
}
function shortFalsy(flag) {
    const map = new Map();
    map.set('tag', 'sentinel');
    const intermediate = flag ? 'truthy' : false;
    if (!intermediate) { map.set('result', intermediate); }
    else { map.set('result', true); }
    return map.get('result') ? 1 : 2;
}
function shortTemporary(flag) {
    const intermediate = flag ? 'a-local-owning-string-without-any-map' : false;
    return intermediate ? 1 : 2;
}
function nullableTags() {
    const map = new Map();
    map.set(null, 'null-value');
    map.set(void 0, false);
    map.set('', 'empty-value');
    map.set('null', true);
    map.set('undefined', 'undefined-text');
    const initial = (map.size === 5 ? 1 : 0)
        + (map.get(null) === 'null-value' ? 2 : 0)
        + (map.get(void 0) ? 0 : 4)
        + (map.get('') === 'empty-value' ? 8 : 0)
        + (map.get('null') ? 16 : 0)
        + (map.get('undefined') === 'undefined-text' ? 32 : 0);
    const saved = map.get(null);
    map.set(null, true);
    const replaced = map.get(null);
    const removed = map.delete(void 0);
    const repeated = map.delete(void 0);
    const distinct = map.has(null) && !map.has(void 0) && map.has('')
        && map.has('null') && map.has('undefined');
    map.set(void 0, 'undefined-value');
    const rewritten = map.get(void 0);
    map.clear();
    return initial + (saved === 'null-value' ? 64 : 0) + (replaced ? 128 : 0)
        + (removed && !repeated ? 256 : 0) + (distinct ? 512 : 0)
        + (rewritten === 'undefined-value' ? 1024 : 0) + (map.size === 0 ? 2048 : 0);
}
function nullableNumberKeys() {
    const map = new Map();
    map.set(null, 1);
    map.set(void 0, 2);
    map.set('', 3);
    map.set('null', 4);
    return map.size * 10000 + map.get(null) * 1000 + map.get(void 0) * 100
        + map.get('') * 10 + map.get('null');
}
function nullableOwned(flag, missing) {
    const map = new Map();
    let key = flag ? 'long-owning-key-with-embedded-\0-bytes-outliving-the-input'
        : (missing ? void 0 : null);
    map.set(key, 'saved-payload-with-independent-owning-\0-bytes');
    const saved = map.get(key);
    key = 'replacement';
    const lookup = flag ? 'long-owning-key-with-embedded-\0-bytes-outliving-the-input'
        : (missing ? void 0 : null);
    const retained = map.has(lookup) && !map.has(key);
    const removed = map.delete(lookup);
    const empty = map.size === 0 && !map.has(lookup);
    return (saved === 'saved-payload-with-independent-owning-\0-bytes' ? 1 : 0)
        + (retained ? 2 : 0) + (removed ? 4 : 0) + (empty ? 8 : 0);
}
function nullablePerUse(flag) {
    const map = new Map();
    const key = flag ? '' : null;
    map.set(key || 'missing', true);
    map.set(key, false);
    const saved = map.get(key);
    return (map.size === 2 ? 1 : 0) + (map.has('missing') ? 2 : 0)
        + (map.has('') === flag ? 4 : 0) + (map.has(null) !== flag ? 8 : 0)
        + (saved ? 0 : 16);
}
function mixedNullableTags() {
    const map = new Map();
    map.set(false, true);
    map.set(true, false);
    map.set(null, 'null');
    map.set(void 0, 'undefined');
    map.set('', 'empty');
    map.set('false', 'false-text');
    map.set('true', 'true-text');
    const saved = map.get(null);
    const initial = (map.size === 7 ? 1 : 0) + (map.get(false) ? 2 : 0)
        + (map.get(true) ? 0 : 4) + (saved === 'null' ? 8 : 0)
        + (map.get(void 0) === 'undefined' ? 16 : 0) + (map.get('') === 'empty' ? 32 : 0)
        + (map.get('false') === 'false-text' ? 64 : 0)
        + (map.get('true') === 'true-text' ? 128 : 0);
    map.set(false, 'replaced');
    const rewritten = map.get(false);
    const removed = map.delete(null);
    const distinct = !map.has(null) && map.has(void 0) && map.has('') && map.has(false);
    map.clear();
    return initial + (rewritten === 'replaced' ? 256 : 0) + (removed && distinct ? 512 : 0)
        + (saved === 'null' ? 1024 : 0);
}
function nullablePayloadTags() {
    const map = new Map();
    map.set(0, 'owning-nullable-payload-with-embedded-\0-bytes');
    map.set(1, null);
    map.set(2, void 0);
    map.set(3, '');
    map.set(4, 'null');
    map.set(5, 'undefined');
    const saved = map.get(0);
    const absent = map.get(1);
    const undefinedValue = map.get(2);
    const empty = map.get(3);
    const initial = (map.size === 6 ? 1 : 0)
        + (absent === null && absent !== void 0 ? 2 : 0)
        + (undefinedValue === void 0 && undefinedValue !== null ? 4 : 0)
        + (empty === '' && empty !== null && empty !== void 0 ? 8 : 0)
        + (map.get(4) === 'null' && map.get(5) === 'undefined' ? 16 : 0)
        + (map.has(2) && !map.has(6) && map.get(6) === void 0 ? 32 : 0);
    map.set(0, null);
    map.set(1, 'replacement');
    map.set(2, '');
    map.set(3, void 0);
    const changed = map.get(0) === null && map.get(1) === 'replacement'
        && map.get(2) === '' && map.get(3) === void 0;
    map.delete(0);
    map.clear();
    return initial + (changed ? 64 : 0)
        + (saved === 'owning-nullable-payload-with-embedded-\0-bytes' ? 128 : 0)
        + (absent === null && undefinedValue === void 0 && empty === '' ? 256 : 0)
        + (map.size === 0 ? 512 : 0);
}
function nullablePayloadOwned(flag, missing) {
    const map = new Map();
    let value = flag ? 'returned-owning-nullable-payload-with-embedded-\0-bytes'
        : (missing ? void 0 : null);
    const key = value;
    map.set(key, value);
    const saved = map.get(key);
    value = 'replacement-input';
    map.set(key, value);
    map.delete(key);
    map.clear();
    return saved;
}
function mixedNullablePayload(flag) {
    const map = new Map();
    map.set('flag', false);
    map.set('keep', 'saved-mixed-nullable-payload-with-embedded-\0-bytes');
    map.set('null', null);
    map.set('undefined', void 0);
    const nullable = flag ? 'temporary' : null;
    map.set('nullable', nullable);
    const saved = map.get('keep');
    const boolean = map.get('flag');
    map.set('keep', null);
    map.set('flag', 'replacement');
    map.set('saved', saved);
    map.set('boolean', boolean);
    const rewritten = map.get('flag');
    const copied = map.get('saved');
    const copiedBoolean = map.get('boolean');
    const removed = map.delete('null');
    map.clear();
    return (saved === 'saved-mixed-nullable-payload-with-embedded-\0-bytes' ? 1 : 0)
        + (copied === saved ? 2 : 0) + (rewritten === 'replacement' ? 4 : 0)
        + (!boolean && !copiedBoolean ? 8 : 0) + (removed && map.size === 0 ? 16 : 0);
}
function mixedNullableRead(flag, empty) {
    const map = new Map();
    map.set('flag', false);
    let value = flag ? (empty ? '' : 'exact-nullable-read-with-owned-\0-bytes')
        : (empty ? void 0 : null);
    map.set('value', value);
    value = 'replaced-input';
    const result = map.get('value');
    const present = map.has('value');
    map.delete('value');
    return (result === 'exact-nullable-read-with-owned-\0-bytes' ? 1 : 0)
        + (result === '' ? 2 : 0) + (result === null ? 4 : 0)
        + (result === void 0 ? 8 : 0) + (present && !map.get('flag') ? 16 : 0);
}
function mixedNullableJoin(left, flag) {
    const map = new Map();
    map.set('flag', false);
    map.set('left', flag ? 'saved-nullable-join-with-owned-\0-bytes' : null);
    map.set('right', flag ? '' : void 0);
    const selected = left ? map.get('left') : map.get('right');
    map.set('left', false);
    map.delete('right');
    map.set('saved', selected);
    const result = map.get('saved');
    map.set('saved', true);
    map.clear();
    return result;
}
function mixedNullableBranches(left, flag) {
    const map = new Map();
    map.set('flag', false);
    map.set('value', flag ? 'branch-nullable-with-owned-\0-bytes' : null);
    if (!left) { map.set('value', flag ? '' : void 0); }
    const result = map.get('value');
    map.set('value', true);
    map.delete('value');
    map.clear();
    return result;
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

var traceGuardString = (guardedSavedString(false) === 'guarded-owning-\0-string' ? 10 : 0)
    + (guardedSavedString(true) === 'fallback' ? 2 : 0);
var traceGuardNumber = guardedSavedNumber(false) * 10 + guardedSavedNumber(true);
var traceGuardBoolean = guardedSavedBoolean(false) * 10 + guardedSavedBoolean(true);
var traceGuardDisjoint = guardedDisjoint(false) * 10 + guardedDisjoint(true);

var traceShortString = (shortString(false, false) === 'selected' ? 100 : 0)
    + (shortString(false, true) === 'fallback' ? 10 : 0)
    + (shortString(true, false) === 'fallback' ? 1 : 0);
var traceShortNumber = shortNumber(false, false) * 100 + shortNumber(false, true) * 10
    + shortNumber(true, false);
var traceShortBoolean = shortBoolean(false) * 10 + shortBoolean(true);
var traceShortSaved = (shortSaved(false) === 'owning-short-\0-string' ? 10 : 0)
    + (shortSaved(true) === 'fallback' ? 1 : 0);
var traceShortFalsy = shortFalsy(true) * 10 + shortFalsy(false);
var traceShortTemporary = shortTemporary(true) * 10 + shortTemporary(false);
var traceNullableTags = nullableTags();
var traceNullableNumbers = nullableNumberKeys();
var traceNullableOwned = nullableOwned(true, false) * 10000 + nullableOwned(false, false) * 100
    + nullableOwned(false, true);
var traceNullablePerUse = nullablePerUse(true) * 100 + nullablePerUse(false);
var traceMixedNullableTags = mixedNullableTags();
var traceNullablePayloadTags = nullablePayloadTags();
var traceNullablePayloadOwned = (nullablePayloadOwned(true, false)
        === 'returned-owning-nullable-payload-with-embedded-\0-bytes' ? 100 : 0)
    + (nullablePayloadOwned(false, false) === null ? 10 : 0)
    + (nullablePayloadOwned(false, true) === void 0 ? 1 : 0);
var traceMixedNullablePayload = mixedNullablePayload(true) * 100 + mixedNullablePayload(false);
var traceMixedNullableRead = mixedNullableRead(true, false) * 1000000
    + mixedNullableRead(true, true) * 10000 + mixedNullableRead(false, false) * 100
    + mixedNullableRead(false, true);
var traceMixedNullableJoin = (mixedNullableJoin(true, true)
        === 'saved-nullable-join-with-owned-\0-bytes' ? 1000 : 0)
    + (mixedNullableJoin(true, false) === null ? 100 : 0)
    + (mixedNullableJoin(false, true) === '' ? 10 : 0)
    + (mixedNullableJoin(false, false) === void 0 ? 1 : 0);
var traceMixedNullableBranches = (mixedNullableBranches(true, true)
        === 'branch-nullable-with-owned-\0-bytes' ? 1000 : 0)
    + (mixedNullableBranches(true, false) === null ? 100 : 0)
    + (mixedNullableBranches(false, true) === '' ? 10 : 0)
    + (mixedNullableBranches(false, false) === void 0 ? 1 : 0);

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

//--- mixed-nullable-branch-callee-refused.js
function mixedNullableBranches(left, flag) {
    const map = new Map();
    map.set('flag', false);
    if (left) { map.set('value', flag ? 'branch-nullable-with-owned-\0-bytes' : null); }
    else { map.set('value', flag ? '' : void 0); }
    const result = map.get('value');
    map.set('value', true);
    map.delete('value');
    map.clear();
    return result;
}
var trace = (mixedNullableBranches(true, true)
        === 'branch-nullable-with-owned-\0-bytes' ? 1000 : 0)
    + (mixedNullableBranches(true, false) === null ? 100 : 0)
    + (mixedNullableBranches(false, true) === '' ? 10 : 0)
    + (mixedNullableBranches(false, false) === void 0 ? 1 : 0);

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

//--- guarded-stale-refused.js
function run(flag) {
    const map = new Map();
    map.set(false, false);
    map.set(0, 11);
    const seen = map.has(0);
    if (flag) { map.delete(0); }
    return seen ? (map.get(0) ? 1 : 2) : 3;
}
var trace = run(false) * 10 + run(true);

//--- guarded-wrong-key-refused.js
function run(flag) {
    const map = new Map();
    map.set(false, false);
    map.set(0, 11);
    if (flag) { map.delete(0); }
    return map.has(false) ? (map.get(0) ? 1 : 2) : 3;
}
var trace = run(false) * 10 + run(true);

//--- guarded-unknown-arm-refused.js
function run(flag) {
    const map = new Map();
    map.set(false, false);
    if (flag) { map.set(0, 11); }
    return map.has(0) ? (map.get(0) ? 1 : 2) : 3;
}
var trace = run(false) * 10 + run(true);

//--- guarded-write-refused.js
function run(flag) {
    const map = new Map();
    map.set(false, false);
    map.set(0, 11);
    if (flag) { map.delete(0); } else { map.set(0, false); }
    return map.has(0) ? (map.get(0) ? 1 : 2) : 3;
}
var trace = run(false) * 10 + run(true);

//--- guarded-call-refused.js
function mutate(map) { map.set(0, false); }
function run(flag) {
    const map = new Map();
    map.set(false, false);
    map.set(0, 11);
    if (flag) { map.delete(0); }
    mutate(map);
    return map.has(0) ? (map.get(0) ? 1 : 2) : 3;
}
var trace = run(false) * 10 + run(true);

//--- short-truthy-bool-refused.js
function run(flag) {
    const map = new Map();
    map.set(false, true);
    map.set(0, 'fallback');
    map.set(1, 'selected');
    if (flag) { map.delete(1); }
    const intermediate = flag ? true : map.get(0);
    const saved = intermediate || map.get(0);
    map.set(false, saved);
    return map.get(false) ? 1 : 2;
}
var trace = run(false) * 10 + run(true);

//--- short-wrong-condition-refused.js
function run(flag) {
    const map = new Map();
    map.set(false, true);
    map.set(0, 'fallback');
    map.set(1, 'selected');
    if (flag) { map.delete(1); }
    const intermediate = map.has(1) && map.get(1);
    const saved = map.has(0) ? intermediate : map.get(0);
    map.set(false, saved);
    return map.get(false) ? 1 : 2;
}
var trace = run(false) * 10 + run(true);

//--- short-stale-refused.js
function run(flag) {
    const map = new Map();
    map.set(false, true);
    map.set(0, 'fallback');
    map.set(1, 'selected');
    const seen = map.has(1);
    if (flag) { map.delete(1); }
    const intermediate = seen && map.get(1);
    const saved = intermediate || map.get(0);
    map.set(false, saved);
    return map.get(false) ? 1 : 2;
}
var trace = run(false) * 10 + run(true);

//--- short-unknown-refused.js
function run(flag) {
    const map = new Map();
    map.set(false, true);
    map.set(0, 'fallback');
    map.set(1, 'selected');
    if (flag) { map.delete(1); }
    const intermediate = map.has(0) && map.get(2);
    const saved = intermediate || map.get(0);
    map.set(false, saved);
    return map.get(false) ? 1 : 2;
}
var trace = run(false) * 10 + run(true);

//--- nullable-number-key-refused.js
function run() {
    const map = new Map();
    map.set(null, 1);
    map.set(0, 2);
    return map.size;
}
var trace = run();

//--- nullable-boolean-key-refused.js
function run() {
    const map = new Map();
    map.set(null, 1);
    map.set(false, 2);
    return map.size;
}
var trace = run();

//--- nullable-snapshot-refused.js
function run() {
    const map = new Map();
    map.set('', 1);
    map.set(null, 2);
    return Array.from(map.keys()).length;
}
var trace = run();

//--- mixed-nullable-snapshot-refused.js
function run() {
    const map = new Map();
    map.set('', 1);
    map.set(null, 2);
    map.set(false, 3);
    return Array.from(map.keys()).length;
}
var trace = run();

//--- nullable-stale-read-refused.js
function run(flag) {
    const map = new Map();
    map.set('', false);
    map.set(null, 'null-value');
    map.set(void 0, 'undefined-value');
    const seen = map.has(null);
    if (flag) { map.delete(null); }
    return seen ? (map.get(null) ? 1 : 2) : 3;
}
var trace = run(false) * 10 + run(true);

//--- mixed-nullable-temporary-refused.js
function run(flag, other) {
    const map = new Map();
    const key = flag ? false : (other ? '' : null);
    map.set(key, true);
    return map.size;
}
var trace = run(true, false) + run(false, true) + run(false, false);

//--- nullable-number-payload-refused.js
function run() {
    const map = new Map();
    map.set(0, null);
    map.set(1, 2);
    return map.size;
}
var trace = run();

//--- nullable-boolean-payload-refused.js
function run() {
    const map = new Map();
    map.set(0, null);
    map.set(1, false);
    return map.size;
}
var trace = run();

//--- nullable-payload-snapshot-refused.js
function run() {
    const map = new Map();
    map.set(0, 'value');
    map.set(1, null);
    return Array.from(map.values()).length;
}
var trace = run();

//--- mixed-nullable-payload-snapshot-refused.js
function run() {
    const map = new Map();
    map.set(0, false);
    map.set(1, null);
    map.set(2, 'value');
    return Array.from(map.values()).length;
}
var trace = run();

//--- mixed-nullable-payload-read-refused.js
function run(flag) {
    const map = new Map();
    map.set('key', false);
    map.set('nullable', null);
    if (flag) { map.set('key', 'value'); }
    return map.get('key') ? 1 : 2;
}
var trace = run(true) * 10 + run(false);

//--- mixed-nullable-payload-stale-refused.js
function run(flag) {
    const map = new Map();
    map.set('flag', false);
    map.set('nullable', null);
    map.set('key', 'value');
    const seen = map.has('key');
    if (flag) { map.delete('key'); }
    return seen ? (map.get('key') ? 1 : 2) : 3;
}
var trace = run(false) * 10 + run(true);

//--- mixed-nullable-payload-temporary-refused.js
function run(flag, other) {
    const map = new Map();
    const payload = flag ? false : (other ? '' : null);
    map.set(0, payload);
    return map.size;
}
var trace = run(true, false) + run(false, true) + run(false, false);

//--- mixed-nullable-unknown-payload-refused.js
function payload(flag) { return flag ? 'unproved-return' : null; }
function run(flag) {
    const map = new Map();
    map.set('flag', false);
    map.set('nullable', null);
    map.set('key', payload(flag));
    return map.get('key') === null ? 1 : 2;
}
var trace = run(true) * 10 + run(false);

//--- mixed-nullable-instance-refused.js
function read(a, b, flag) {
    a.set('flag', false);
    a.set('key', flag ? 'value' : null);
    b.set('key', false);
    return a.get('key') === null ? 1 : 2;
}
function run(alias) {
    const a = new Map();
    const b = new Map();
    return alias ? read(a, a, false) : read(a, b, false);
}
var trace = run(true) * 10 + run(false);

//--- mixed-nullable-presence-refused.js
function run(flag) {
    const map = new Map();
    map.set('flag', false);
    map.set('key', flag ? 'value' : null);
    if (flag) { map.delete('key'); }
    return map.get('key') === null ? 1 : 2;
}
var trace = run(true) * 10 + run(false);

//--- mixed-nullable-call-refused.js
function mutate(map, flag) { map.set('key', flag ? 'after' : null); }
function run(flag) {
    const map = new Map();
    map.set('flag', false);
    map.set('key', flag ? 'before' : null);
    mutate(map, flag);
    return map.get('key') === null ? 1 : 2;
}
var trace = run(true) * 10 + run(false);
