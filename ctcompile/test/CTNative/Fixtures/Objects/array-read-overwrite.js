function storedValue() {
  var a = [1, 2];
  a[0] = a[1];
  return a[0];
}
function storedIndex() {
  var a = [0, 1];
  var index = a[0];
  a[index] = -0;
  return 1 / a[0];
}
var value = storedValue();
var reciprocal = storedIndex();
