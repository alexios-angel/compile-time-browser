// An ELISION IN AN ARRAY LITERAL IS A HOLE (13.2.4.1): `[1, , 3]` has length
// 3 and no element 1 - `in`, hasOwnProperty, forEach and Object.keys all tell
// a hole from an undefined element, and test262 has thirty forEach files
// that ask. The literal used to append undefined there.

#include "js_expect.hpp"

int main() {
    js_expect("[1, , 3].length", "3");
    js_expect("1 in [1, , 3]", "false");
    js_expect("0 in [1, , 3]", "true");
    js_expect("[1, , 3].hasOwnProperty(1)", "false");
    js_expect("(function () { var n = 0; [1, , 3].forEach(function () { n++; }); return n; })()",
              "2");
    js_expect("Object.keys([1, , 3]).join(',')", "0,2");
    js_expect("[, , ].length", "2");
    js_expect("[1, , 3][1]", "undefined");
    // A hole written to is an element again.
    js_expect("(function () { var a = [1, , 3]; a[1] = 2; return 1 in a; })()", "true");
    // An explicit undefined is NOT a hole.
    js_expect("1 in [1, undefined, 3]", "true");
    // A spread before the elision moves it; the positions are no longer
    // static, and the slot is appended as undefined then.
    js_expect("[...[1, 2], , 4].length", "4");
    REPORT("array_holes");
}
