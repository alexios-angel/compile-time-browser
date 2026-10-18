// --ctjs-lift-to-scf folds the iteration protocol of a for-of or an array
// destructuring over an array made before the loop (Object.entries here), so
// the loop is a plain index loop and a `throw` in its body no longer keeps
// the function in CFG form behind the handler that closed the iterator
// (lib/CTJS/Lowering/ArrayIteration.cpp). A for-of over a value the caller
// passes keeps the protocol.

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:   | FileCheck %s

// The entries of a literal, destructured: the pair's steps are reads at 0 and
// 1, and nothing of the protocol is left.
// CHECK-LABEL: ctjs.func {{.*}}@sum$
// CHECK-NOT: __ctbrowser_
// CHECK-NOT: ctjs.iterable
// CHECK: scf.while
// CHECK: ctjs.constant #ctjs.number<0>
// CHECK-NEXT: ctjs.get_property
// CHECK: ctjs.constant #ctjs.number<4607182418800017408>
// CHECK-NEXT: ctjs.get_property
// CHECK-NOT: __ctbrowser_
// CHECK: ctjs.return
function sum() {
    var table = { a: 1, b: 2 };
    var total = 0;
    for (const [k, v] of Object.entries(table)) {
        total = total + v + k.length;
    }
    return total;
}

// A throw in the body: the handler that only closed the iterator goes, and
// the function is structured.
// CHECK-LABEL: ctjs.func {{.*}}@check$
// CHECK-NOT: ctjs.not_structured
// CHECK-NOT: ctjs.push_handler
// CHECK-NOT: __ctbrowser_
// CHECK: scf.while
// CHECK: ctjs.throw
// CHECK: ctjs.return
function check(config) {
    var types = { a: "number", b: "string" };
    for (const [k, v] of Object.entries(types)) {
        if (typeof config[k] !== v) {
            throw new TypeError("bad " + k);
        }
    }
    return 1;
}

// Object.entries of any argument is a new Array (null and undefined throw at
// the call), so a parameter's entries fold too.
// CHECK-LABEL: ctjs.func {{.*}}@entries_of$
// CHECK-NOT: __ctbrowser_
// CHECK: scf.while
// CHECK-NOT: __ctbrowser_
// CHECK: ctjs.return
function entries_of(o) {
    var total = 0;
    for (const [k, v] of Object.entries(o)) {
        total = total + v;
    }
    return total;
}

// The caller's value may be any iterable: the protocol stays.
// CHECK-LABEL: ctjs.func {{.*}}@passed$
// CHECK: __ctbrowser_for_of_open
function passed(items) {
    var total = 0;
    for (const item of items) {
        total = total + item;
    }
    return total;
}

var s = sum();
var c = check({ a: 1, b: "x" });
var p = passed([1, 2]);
var e = entries_of({ a: 1 });
