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

    // A LENGTH MAKES HOLES TOO (array_object::holes_begin): `new Array(n)`, a
    // grown `length` and a write past the end leave indices that are not
    // properties, and a fill in either direction turns them into elements.
    js_expect("0 in new Array(3)", "false");
    js_expect("Object.keys(new Array(3)).length", "0");
    js_expect("(function () { var a = [0]; a.length = 3; return (1 in a) + ',' + a.length; })()",
              "false,3");
    js_expect("(function () { var a = []; a[3] = 'x'; return Object.keys(a).join(); })()", "3");
    js_expect("(function () { var a = new Array(10); a[1] = undefined; var n = 0;"
              " a.forEach(function () { n++; }); return n; })()",
              "1");
    js_expect("(function () { var a = new Array(4); for (var i = 0; i < 4; i++) a[i] = i;"
              " return Object.keys(a).join() + '|' + a.join(); })()",
              "0,1,2,3|0,1,2,3");
    js_expect("(function () { var a = new Array(4); for (var i = 3; i >= 0; i--) a[i] = i;"
              " return Object.keys(a).join(); })()",
              "0,1,2,3");
    js_expect("(function () { var a = new Array(5); a[2] = 'm'; a[0] = 'f';"
              " return Object.keys(a).join(); })()",
              "0,2");
    js_expect("new Array(3).fill(7).join()", "7,7,7");
    js_expect("(function () { var a = new Array(3); a.push(1); return Object.keys(a).join(); })()",
              "3");
    // A HOLE, OR AN INDEX PAST THE END, READS THROUGH TO THE PROTOTYPES -
    // getters included (OrdinaryGet step 3).
    js_expect("(function () { Array.prototype[1] = 'p'; var a = [0]; a.length = 2;"
              " var r = a[1] + a.join(); delete Array.prototype[1]; return r; })()",
              "p0,p");
    js_expect("(function () { Object.defineProperty(Array.prototype, '0',"
              " {get: function () { return 11; }, configurable: true});"
              " var r = [, 1][0] + ',' + [][0]; delete Array.prototype[0]; return r; })()",
              "11,11");
    js_expect("1 / [true].indexOf(true, -0)", "Infinity");
    // AND A WRITE to an index the array does not have asks the prototypes
    // first (OrdinarySet step 2): a setter on Array.prototype[0] takes a push
    // and a write into a hole alike. (A string collects them: pushing onto an
    // array in that setter would call it again, in every engine.)
    js_expect("(function () { var got = ''; Object.defineProperty(Array.prototype, '0',"
              " {set: function (v) { got += v; }, configurable: true});"
              " var a = []; a.push(5); var b = new Array(2); b[0] = 6; var c = [1]; c[0] = 7;"
              " delete Array.prototype[0];"
              " return got + '|' + a.length + (0 in a) + (0 in b) + c[0]; })()",
              "56|1falsefalse7");
    // A CALLBACK THAT THROWS AND CATCHES INSIDE ITSELF has not thrown: the
    // walks that stop at a callback's throw ask for a PARKED one, which this
    // is not.
    js_expect("[1, 2, 3].map(function (x) { try { throw x; } catch (e) {} return x * 2; }).join()",
              "2,4,6");
    js_expect("(function () { var n = 0; [1, 2, 3].forEach(function () {"
              " try { null.x; } catch (e) {} n++; }); return n; })()",
              "3");
    js_expect("[3, 1, 2].sort(function (a, b) { try { throw 0; } catch (e) {} return a - b; })"
              ".join()",
              "1,2,3");
    js_expect(
        "JSON.stringify([{toJSON: function () { try { throw 1; } catch (e) {} return 7; }}, 8])",
        "[7,8]");
    js_expect("JSON.stringify(JSON.parse('[1,2]', function (k, v) {"
              " try { throw 0; } catch (e) {} return v; }))",
              "[1,2]");
    REPORT("array_holes");
}
