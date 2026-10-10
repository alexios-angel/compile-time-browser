// RUN: split-file %s %t
// RUN: split-file %S/constructor-refusals.mlir %t.original
// RUN: cp %t.original/prototype-written.js %t/prototype-written.js
// RUN: python3 %S/constructor_refusals.py --scalars --translate ctjs-translate --opt ctjs-opt --node %node --reference %native_reference --fixtures %t --work %t.executables

// A prototype's scalar defaults are copied into each instance only when the
// prototype is proved immutable. Constructor reads, conditional own writes and
// separate instances catch a missing default, a wrong init order and shared storage.

//--- inherited.js
function inherited() {
    var Shape = function (s) { this.s = s + this.kind; };
    Shape.prototype = {kind: 7};
    var first = new Shape(2), second = new Shape(3);
    first.kind = 11;
    return first.s * 1000 + second.s * 100 + first.kind * 10 + second.kind;
}
var a = inherited();

//--- shadow.js
function shadow(flag) {
    var Shape = function () { if (flag) this.kind = 11; };
    Shape.prototype = {kind: 7};
    var instance = new Shape();
    return instance.kind;
}
var a = shadow(true) * 10 + shadow(false);

//--- borrowed.js
function borrowed() {
    var Shape = function () {};
    var read = function (object) { return object.kind; };
    Shape.prototype = {kind: 7};
    var instance = new Shape();
    return read(instance);
}
var a = borrowed();

//--- primitives.js
function primitives() {
    var Shape = function () {};
    Shape.prototype = {text: "ok", flag: true, empty: null, missing: undefined};
    var instance = new Shape();
    return (instance.text === "ok" ? 1000 : 0) + (instance.flag ? 100 : 0) +
        (instance.empty === null ? 10 : 0) + (instance.missing === undefined ? 1 : 0);
}
var a = primitives();

// primitives.js reads `undefined` as a global, which is still refused.
// literal-primitives.js uses `void 0`, which needs no host-binding provenance
// proof, so its string and nullable scalar fields lower natively.
//--- literal-primitives.js
function primitives() {
    var Shape = function () {};
    Shape.prototype = {text: "ok", flag: true, empty: null, missing: void 0};
    var instance = new Shape();
    return (instance.text === "ok" ? 1000 : 0) + (instance.flag ? 100 : 0) +
        (instance.empty === null ? 10 : 0) + (instance.missing === void 0 ? 1 : 0);
}
var a = primitives();

//--- boolean-default.js
function boolean_default() {
    var Shape = function () {};
    Shape.prototype = {flag: true};
    var first = new Shape(), second = new Shape();
    first.flag = false;
    return (first.flag ? 10 : 0) + (second.flag ? 1 : 0);
}
var a = boolean_default();
