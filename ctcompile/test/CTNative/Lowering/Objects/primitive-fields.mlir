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

// Every forwarded parameter needs its own complete call and presence proof.
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

//--- method-saved.js
function method_saved() {
    var value = {text: "a\0b", read: function () {
        var saved = this.text;
        this.text = "changed";
        return saved === "a\0b" && this.text === "changed" ?
            saved.length * 10 + this.text.length : 0;
    }};
    return value.read() + (value.text === "changed" ? 100 : 0);
}
var a = method_saved();

//--- method-multiple.js
function method_multiple() {
    var read = function () { return this.text.length; };
    var first = {text: "x", read: read}, second = {text: "three", read: read};
    return first.read() * 10 + second.read();
}
var a = method_multiple();

//--- method-missing.js
function method_missing() {
    var read = function () { return this.text === void 0 ? 1 : 2; };
    var first = {read: read}, second = {text: "three", read: read};
    return first.read() * 10 + second.read();
}
var a = method_missing();

//--- method-mixed.js
function method_mixed() {
    var value = {text: "x", read: function () {
        var saved = this.text;
        this.text = 4;
        return saved.length * 10 + this.text;
    }};
    return value.read();
}
var a = method_mixed();

//--- method-identity.js
function method_identity() {
    var value = {text: "x", read: function () { return this.text.length; }};
    var saved = value.read;
    return value.read() === 1 && saved === value.read ? 1 : 0;
}
var a = method_identity();

//--- forwarded-chain.js
function forwarded() {
    var read = function (value) { return value.text.length; };
    var first = function (value) { return read(value); };
    var second = function (value) { return first(value); };
    var third = function (value) { return second(value); };
    var value = {text: "deep"};
    return third(value);
}
var a = forwarded();

// The saved String must own its bytes even across a forwarded mutation.
//--- forwarded-saved.js
function forwarded() {
    var read = function (value) {
        var saved = value.text;
        value.text = "changed";
        return saved === "a\0b" && value.text === "changed" ?
            saved.length * 10 + value.text.length : 0;
    };
    var forward = function (value) { return read(value); };
    var value = {text: "a\0b"};
    return forward(value) + (value.text === "changed" ? 100 : 0);
}
var a = forwarded();

//--- forwarded-multiple.js
function forwarded() {
    var read = function (value) { return value.text.length; };
    var forward = function (value) { return read(value); };
    var first = {text: "x"}, second = {text: "three"};
    return forward(first) * 100 + forward(second) * 10 + read(first);
}
var a = forwarded();

//--- forwarded-parameters.js
function forwarded() {
    var read = function (first, second) { return first.text.length * 10 + second.text.length; };
    var forward = function (first, second) { return read(second, first); };
    var first = {text: "x"}, second = {text: "four"};
    return forward(first, second) * 100 + forward(second, first);
}
var a = forwarded();

// An initialized member cannot stand in for another call's actual object.
//--- forwarded-before.js
function forwarded() {
    var read = function (value) { return value.text === void 0 ? 1 : 2; };
    var forward = function (value) { return read(value); };
    var value = {};
    var before = forward(value);
    value.text = "set";
    return before * 10 + forward(value);
}
var a = forwarded();

//--- forwarded-missing.js
function forwarded() {
    var read = function (value) { return value.text === void 0 ? 1 : 2; };
    var forward = function (value) { return read(value); };
    var first = {text: "set"}, second = {};
    return read(first) * 10 + forward(second);
}
var a = forwarded();

//--- forwarded-conditional.js
function forwarded(flag) {
    var read = function (value) { return value.text === void 0 ? 1 : 2; };
    var forward = function (value) { return read(value); };
    var value = {};
    if (flag) value.text = "set";
    return forward(value);
}
var a = forwarded(false) * 10 + forwarded(true);

// All callee writes remain in the join, including explicit absence.
//--- forwarded-mixed.js
function forwarded() {
    var read = function (value) {
        var saved = value.text;
        value.text = 9;
        return saved.length + value.text;
    };
    var forward = function (value) { return read(value); };
    var value = {text: "owned"};
    return forward(value);
}
var a = forwarded();

//--- forwarded-undefined.js
function forwarded() {
    var read = function (value) {
        value.text = void 0;
        return value.text === void 0 ? 1 : 2;
    };
    var forward = function (value) { return read(value); };
    var value = {text: "owned"};
    return forward(value);
}
var a = forwarded();

//--- forwarded-alias-delete.js
function forwarded() {
    var read = function (value, alias) {
        delete alias.text;
        return value.text === void 0 ? 1 : 2;
    };
    var forward = function (value) { return read(value, value); };
    var value = {text: "owned"};
    return forward(value);
}
var a = forwarded();

//--- forwarded-escape.js
function forwarded() {
    var read = function (value) { return value; };
    var forward = function (value) { return read(value); };
    var value = {text: "owned"};
    return forward(value).text.length;
}
var a = forwarded();

// No recursive invocation may use itself as initialization evidence.
//--- forwarded-recursive.js
function forwarded() {
    var read = function (value, count) {
        return count === 0 ? value.text.length : read(value, count - 1);
    };
    var forward = function (value) { return read(value, 2); };
    var value = {text: "owned"};
    return forward(value);
}
var a = forwarded();

// The symbol-only call through forward must contribute its nonobject actual.
//--- forwarded-mixed-actual.js
function forwarded() {
    var read = function (value) { return value.text === void 0 ? 1 : 2; };
    var forward = function (value) { return read(value); };
    return read({text: "owned"}) * 10 + forward(9);
}
var a = forwarded();

//--- forwarded-short-call.js
function forwarded() {
    var read = function (value) { return value === void 0 ? 1 : value.text.length; };
    var forward = function (value) { return read(value); };
    return forward({text: "owned"}) * 10 + forward();
}
var a = forwarded();

// A direct call does not close the census of an escaping callable value.
//--- forwarded-callable-escape.js
var escaped;
function forwarded() {
    var read = function (value) { return value.text.length; };
    var forward = function (value) { return read(value); };
    escaped = read;
    return forward({text: "owned"});
}
var a = forwarded();
