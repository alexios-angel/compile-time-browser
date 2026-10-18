// Values a branch narrowed (ctnative.refine): a Number|String parameter split
// under `typeof t == "string"`, its `!==` else-region, a `&&` on the test,
// a Number read under `typeof u == "number"`, and a String|Null split under
// its own truthiness - Bootstrap's `_getOffset` and `z` shapes. Every global
// is a scalar compared with the interpreter.
function offsets(t) {
    return typeof t == 'string' ? t.split(',').map(function (p) {
        return Number.parseInt(p, 10);
    }).length : -1;
}
function parts(t) {
    return typeof t !== 'string' ? -1 : t.split(',').length;
}
function several(t) {
    return typeof t === 'string' && t.split(',').length > 1;
}
function plusOne(u) {
    return typeof u == 'number' ? u + 1 : 0;
}
function thresholds(t) {
    return typeof t == 'string' ? t.split(',').map(function (p) {
        return Number.parseFloat(p);
    }).length : 0;
}
function pick(flag) {
    return flag ? 'x,y,z' : null;
}
function targets(flag) {
    var e = pick(flag);
    return e ? e.split(',').length : 0;
}
var o1 = offsets('10,20,30');
var o2 = offsets(4);
var p1 = parts('a,b');
var p2 = parts(9);
var s1 = several('a,b');
var s2 = several(3);
var n1 = plusOne(41);
var n2 = plusOne('four');
var f1 = thresholds('0.5, 1');
var f2 = thresholds(2);
var t1 = targets(true);
var t2 = targets(false);
