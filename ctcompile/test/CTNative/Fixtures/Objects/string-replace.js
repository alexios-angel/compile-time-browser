// String.prototype.replace with a RegExp literal and a function: closure
// lifting specialises the function for its one call, and the native runtime
// runs the pattern (Runtime/RegExp.hpp). The interpreter's output is the
// reference for every printed global.
//
//   dashed1 = 1    prefixed1 = 1    groups1 = 1    first1 = 1
//   counted1 = 1   untouched1 = 1   dashedLength11 = 11

// Bootstrap's normalizeDataKey: an upper-case letter becomes a dash and its
// lower case.
function dashed(key) {
    return key.replace(/[A-Z]/g, chr => `-${chr.toLowerCase()}`);
}

// A callback that captures a value from its frame.
function prefixed(text, mark) {
    return text.replace(/x|y/g, function (found) {
        return mark + found;
    });
}

// Groups follow the match, in order.
function swapped(text) {
    return text.replace(/([a-z])=([0-9])/g, function (all, key, value) {
        return value + key;
    });
}

// Without `g` only the first match is replaced.
function first(text) {
    return text.replace(/o/, function () {
        return "0";
    });
}

// A Number result is converted with ToString.
function counted(text) {
    return text.replace(/[a-c]+/g, function () {
        return 2;
    });
}

// 1 when the two Strings are equal. A comparison at the top level would
// thread every function value through the importer's branch.
function same(left, right) {
    if (left === right) {
        return 1;
    }
    return 0;
}

var dashed1 = same(dashed("fooBarBaz"), "foo-bar-baz");
var prefixed1 = same(prefixed("axbyc", "!"), "a!xb!yc");
var groups1 = same(swapped("a=1;b=2"), "1a;2b");
var first1 = same(first("foo"), "f0o");
var counted1 = same(counted("abcxab"), "2x2");
var untouched1 = same(dashed("été"), "été");
var dashedLength11 = dashed("fooBarBaz").length;
