// Which object spreads --ctjs-lift-to-scf writes out key by key, and which it
// leaves as ctjs.copy_props. A spread is written out only when its source's own
// keys at the spread are known: a literal made in the same block whose only
// earlier uses are constant-key reads and writes on it. Every other source
// keeps the copy, which native lowering then refuses as an open shape. The
// positive cases run natively in CTNative/Fixtures/Objects/spread.test.

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:   | FileCheck %s

// Written out: two keys, in first-write order, each read from the source and
// defined on the target; the second write to `a` keeps its first place.
// CHECK-LABEL: ctjs.func {{.*}}@known$
// CHECK-NOT: ctjs.copy_props
// CHECK: [[VA:%[0-9]+]] = ctjs.get_property [[SRC:%[0-9]+]]{{\[}}[[A:%[0-9]+]]{{\]}}
// CHECK-NEXT: ctjs.set_property [[DST:%[0-9]+]]{{\[}}[[A]]{{\]}}, [[VA]] {ctjs.define_own}
// CHECK-NEXT: [[VB:%[0-9]+]] = ctjs.get_property [[SRC]]{{\[}}[[B:%[0-9]+]]{{\]}}
// CHECK-NEXT: ctjs.set_property [[DST]]{{\[}}[[B]]{{\]}}, [[VB]] {ctjs.define_own}
// CHECK-NOT: ctjs.copy_props
// CHECK: ctjs.return
function known() {
    var s = { a: 1, b: 2 };
    s.a = 3;
    var c = { ...s };
    return c.a + c.b;
}

// Null, undefined and `{}` copy nothing, so the copies are gone.
// CHECK-LABEL: ctjs.func {{.*}}@empty$
// CHECK-NOT: ctjs.copy_props
// CHECK: ctjs.return
function empty() {
    var c = { x: 1, ...null, ...undefined, ...{} };
    return c.x;
}

// A parameter's keys are whatever the caller passed.
// CHECK-LABEL: ctjs.func {{.*}}@parameter$
// CHECK: ctjs.copy_props
function parameter(o) {
    var c = { ...o };
    return c.x;
}

// A key written on only one path may or may not be copied.
// CHECK-LABEL: ctjs.func {{.*}}@conditional$
// CHECK: ctjs.copy_props
function conditional(k) {
    var s = { a: 1 };
    if (k) { s.b = 2; }
    var c = { ...s };
    return c.a;
}

// A callee could add keys to the source before the copy.
// CHECK-LABEL: ctjs.func {{.*}}@passed$
// CHECK: ctjs.copy_props
function passed(f) {
    var s = { a: 1 };
    f(s);
    var c = { ...s };
    return c.a;
}

// A getter runs during the copy.
// CHECK-LABEL: ctjs.func {{.*}}@getter$
// CHECK: ctjs.copy_props
function getter() {
    var s = { get a() { return 1; } };
    var c = { ...s };
    return c.a;
}

// An array-index key is enumerated before every string key, whatever order
// the literal wrote them in.
// CHECK-LABEL: ctjs.func {{.*}}@indexed$
// CHECK: ctjs.copy_props
function indexed() {
    var s = { b: 1, "1": 2 };
    var c = { ...s };
    return c.b;
}

// A dynamic key may be anything.
// CHECK-LABEL: ctjs.func {{.*}}@dynamic$
// CHECK: ctjs.copy_props
function dynamic(k) {
    var s = { a: 1 };
    s[k] = 2;
    var c = { ...s };
    return c.a;
}
