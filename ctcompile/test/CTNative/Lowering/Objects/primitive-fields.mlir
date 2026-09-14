// RUN: split-file %s %t
// RUN: python3 %S/primitive_fields.py --translate ctjs-translate --opt ctjs-opt --node %node --reference %native_reference --fixtures %t --work %t.controls

//--- strings.js
function strings() {
    var left = {text: "a\0b"}, right = {text: "other"};
    var saved = left.text;
    left.text = "changed";
    return saved.length * 100 + left.text.length * 10 + right.text.length;
}
var a = strings();

//--- mixed.js
function mixed() {
    var value = {text: "before"};
    var saved = value.text;
    value.text = 9;
    return saved.length + value.text;
}
var a = mixed();

//--- absent.js
function absent(flag) {
    var value = {};
    if (flag) value.text = "set";
    return value.text === void 0 ? 1 : 2;
}
var a = absent(false) * 10 + absent(true);

//--- borrowed.js
function borrowed() {
    var read = function (value) { return value.text.length; };
    var value = {text: "owned"};
    return read(value);
}
var a = borrowed();

// Forwarded parameters still need a presence proof across another call boundary.
//--- borrowed-forwarded.js
function borrowed() {
    var read = function (value) { return value.text.length; };
    var forward = function (value) { return read(value); };
    var value = {text: "owned"};
    return forward(value);
}
var a = borrowed();

// One alias can remove a field the other parameter reads.
//--- borrowed-alias-delete.js
function borrowed() {
    var read = function (value, alias) {
        delete alias.text;
        return value.text === void 0 ? 1 : 2;
    };
    var value = {text: "owned"};
    return read(value, value);
}
var a = borrowed();

// Every call has its own initialization, even when receivers share a schema.
//--- borrowed-multiple.js
function borrowed() {
    var read = function (value) { return value.text.length; };
    var first = {text: "x"}, second = {text: "three"};
    return read(first) * 100 + read(second) * 10 + read(first);
}
var a = borrowed();

//--- borrowed-saved.js
function borrowed() {
    var read = function (value) {
        var saved = value.text;
        value.text = "changed";
        return saved === "a\0b" && value.text === "changed" ?
            saved.length * 10 + value.text.length : 0;
    };
    var value = {text: "a\0b"};
    return read(value) + (value.text === "changed" ? 100 : 0);
}
var a = borrowed();

//--- borrowed-method.js
function borrowed() {
    var value = {text: "owned", read: function () { return this.text.length; }};
    return value.read();
}
var a = borrowed();

//--- borrowed-parameters.js
function borrowed() {
    var read = function (first, second) { return first.text.length * 10 + second.text.length; };
    var first = {text: "x"}, second = {text: "four"};
    return read(first, second) * 100 + read(second, first);
}
var a = borrowed();

// A later store cannot initialize an earlier call, nor another schema member.
//--- borrowed-before.js
function borrowed() {
    var read = function (value) { return value.text === void 0 ? 1 : 2; };
    var value = {};
    var before = read(value);
    value.text = "set";
    return before * 10 + read(value);
}
var a = borrowed();

//--- borrowed-missing.js
function borrowed() {
    var read = function (value) { return value.text === void 0 ? 1 : 2; };
    var first = {text: "set"}, second = {};
    return read(first) * 10 + read(second);
}
var a = borrowed();

//--- borrowed-conditional.js
function borrowed(flag) {
    var read = function (value) { return value.text === void 0 ? 1 : 2; };
    var value = {};
    if (flag) value.text = "set";
    return read(value);
}
var a = borrowed(false) * 10 + borrowed(true);

// Callee writes remain in the schema join, including explicit absence.
//--- borrowed-mixed.js
function borrowed() {
    var read = function (value) {
        var saved = value.text;
        value.text = 9;
        return saved.length + value.text;
    };
    var value = {text: "owned"};
    return read(value);
}
var a = borrowed();

//--- borrowed-undefined.js
function borrowed() {
    var read = function (value) {
        value.text = void 0;
        return value.text === void 0 ? 1 : 2;
    };
    var value = {text: "owned"};
    return read(value);
}
var a = borrowed();

// Removal and escaping receivers never get a closed field proof.
//--- borrowed-delete.js
function borrowed() {
    var read = function (value) {
        delete value.text;
        return value.text === void 0 ? 1 : 2;
    };
    var value = {text: "owned"};
    return read(value);
}
var a = borrowed();

//--- borrowed-escape.js
function borrowed() {
    var read = function (value) { return value; };
    var value = {text: "owned"};
    return read(value).text.length;
}
var a = borrowed();

// Equality checks the saved owning bytes independently of length lowering.
//--- equality.js
function strings() {
    var left = {text: "a\0b"}, right = {text: "other"};
    var saved = left.text;
    left.text = "changed";
    return (saved === "a\0b" ? 100 : 0) +
        (left.text === "changed" ? 10 : 0) + (right.text === "other" ? 1 : 0);
}
var a = strings();

//--- borrowed-equality.js
function borrowed() {
    var read = function (value) { return value.text === "owned"; };
    var value = {text: "owned"};
    return read(value) ? 5 : 0;
}
var a = borrowed();

// ND-1 pins the engine's byte count separately from Node's UTF-16 count.
//--- utf8-length.js
function length() {
    var value = {text: "é😀\0"};
    return value.text.length;
}
var a = length();

//--- empty-length.js
function length() { var value = {text: ""}; return value.text.length; }
var a = length();

//--- unknown-property.js
function property() { var value = {text: "abc"}; return value.text.other === void 0 ? 0 : 1; }
var a = property();
