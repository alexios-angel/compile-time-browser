// RUN: split-file %s %t
// RUN: python3 %S/array_borrow.py --translate ctjs-translate --opt ctjs-opt --node %node --reference %native_reference --original %S/array-shrink.mlir --fixtures %t --work %t.executables

// The original alias.js in array-shrink.mlir stays byte-for-byte pinned by the
// driver. Different lengths distinguish the selected arm; writes and reads
// through the borrow must still observe the two original owning vectors.

//--- unequal.js
function selected(flag) {
  var a = [1, 2], b = [3, 4, 5];
  var alias = flag ? a : b;
  alias[0] = 9;
  var before = alias.length;
  alias.length = 1;
  return before * 100 + a[0] * 10 + b[0] + a.length + b.length;
}
var left = selected(true);
var right = selected(false);

//--- nested.js
function nested(first, second) {
  var a = [1, 2], b = [3, 4, 5], c = [6, 7, 8, 9];
  var middle = first ? a : b;
  var alias = second ? middle : c;
  var before = alias.length;
  alias.length = 1;
  return before * 1000 + a.length * 100 + b.length * 10 + c.length;
}
var left = nested(true, true);
var middle = nested(false, true);
var right = nested(false, false);

//--- external.js
function blocked(flag, external) {
  var a = [1, 2];
  var alias = flag ? a : external;
  alias.length = 0;
  return a.length;
}
var other = [3, 4];
var observed = blocked(false, other);

//--- returned.js
function blocked(flag) {
  var a = [1, 2], b = [3, 4];
  return flag ? a : b;
}
var observed = blocked(true).length;

//--- captured.js
function blocked(flag) {
  var a = [1, 2], b = [3, 4];
  var alias = flag ? a : b;
  return function() { return alias.length; };
}
var observed = blocked(true)();

//--- stored.js
function blocked(flag) {
  var a = [1, 2], b = [3, 4];
  var alias = flag ? a : b;
  var object = {saved: alias};
  alias.length = 0;
  return object.saved.length;
}
var observed = blocked(true);

//--- call.js
function length(value) { return value.length; }
function blocked(flag) {
  var a = [1, 2], b = [3, 4];
  var alias = flag ? a : b;
  return length(alias);
}
var observed = blocked(true);

//--- region.js
function blocked(flag) {
  var a = [1, 2];
  var alias = flag ? a : [3, 4];
  alias.length = 0;
  return a.length;
}
var observed = blocked(false);

//--- mixed.js
function blocked(flag) {
  var a = [1, 2], b = ["three", "four"];
  var alias = flag ? a : b;
  alias.length = 0;
  return a.length + b.length;
}
var observed = blocked(true);

//--- growth.js
function blocked(flag) {
  var a = [1, 2], b = [3, 4];
  var alias = flag ? a : b;
  alias.length = 3;
  return a.length + b.length;
}
var observed = blocked(true);

//--- owner-returned.js
function blocked(flag) {
  var a = [1, 2], b = [3, 4];
  var alias = flag ? a : b;
  alias.length = 0;
  return a;
}
var observed = blocked(true).length;

//--- loop.js
function blocked(flag) {
  var a = [1, 2], b = [3, 4];
  var alias = a;
  while (flag) {
    alias = b;
    flag = false;
  }
  alias.length = 0;
  return a.length + b.length;
}
var observed = blocked(true);
