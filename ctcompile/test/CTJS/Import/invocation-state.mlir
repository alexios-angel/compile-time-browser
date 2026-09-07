// RUN: split-file %s %t
// RUN: python3 %S/invocation-state.py --translate ctjs-translate --opt ctjs-opt --fixtures %t --work %t.check

// A throwing assignment must retain the state after argument evaluation and
// before the call. These are source/import boundary tests, not native positives.

//--- assignment.js
function choose(flag) {
    if (flag) { throw 32; }
    return 20;
}
function guarded(flag) {
    var mark = 0;
    try {
        mark = 10;
        mark = choose(flag);
    } catch (value) {
        return mark + value;
    }
    return mark;
}
var caught42 = guarded(true);
var normal20 = guarded(false);

//--- sequential.js
function choose(flag) {
    if (flag) { throw 32; }
    return 20;
}
function guarded(flag) {
    var mark = 0;
    try {
        mark = choose(false);
        mark = choose(flag);
    } catch (value) {
        return mark + value;
    }
    return mark;
}
var caught52 = guarded(true);
var normal20 = guarded(false);

//--- argument.js
function choose(flag, payload) {
    if (flag) { throw payload; }
    return 20;
}
function guarded(flag) {
    var mark = 0;
    try {
        mark = 10;
        mark = choose(flag, (mark = 14));
    } catch (value) {
        return mark + value;
    }
    return mark;
}
var caught28 = guarded(true);
var normal20 = guarded(false);

//--- receiver.js
var order = 0;
function choose(flag) {
    order = order * 10 + 5;
    var delta = this.tag - 7;
    if (flag) { throw 32 + delta; }
    return 20 + delta;
}
function methodReceiver() {
    order = order * 10 + 1;
    return {tag: 7, get method() { order = order * 10 + 3; return choose; }};
}
function methodKey() { order = order * 10 + 2; return "method"; }
function methodArgument(flag) { order = order * 10 + 4; return flag; }
function guarded(flag) {
    var mark = 10;
    try {
        mark = methodReceiver()[methodKey()](methodArgument(flag));
    } catch (value) {
        return mark + value;
    }
    return mark;
}
var caught42 = guarded(true);
var throwingOrder12345 = order;
order = 0;
var normal20 = guarded(false);
var normalOrder12345 = order;
