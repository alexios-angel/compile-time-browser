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

// String length is still a separate property-read lowering boundary. The
// preserved cases above refuse; equality exercises storage independently.
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
