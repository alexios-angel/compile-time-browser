// Arrays of Numbers that Array.prototype.map makes, as
// std::vector<ctnative::js_num>: Bootstrap's
// `text.split(",").map(t => Number.parseInt(t, 10))`, read in a counted loop,
// at a constant index and through length, then filtered and reduced.
// Number.parseInt itself with radix 10, 16 and none, and Math.min, the other
// built-in static. Arrays stay inside functions; every global is a scalar
// compared with the interpreter.
function numbers(text) {
    return text.split(',').map(function (t) {
        return Number.parseInt(t, 10);
    });
}
function offsetSum(text) {
    var parts = numbers(text);
    var total = 0;
    for (var i = 0; i < parts.length; i++) {
        total = total + parts[i];
    }
    return total * 100 + parts.length;
}
function second(text) {
    return numbers(text)[1];
}
function bigOnes(text) {
    return numbers(text).filter(function (n) {
        return n > 2;
    }).length;
}
function lengthSum() {
    return ['a', 'bb', 'ccc']
        .map(function (s) {
            return s.length;
        })
        .reduce(function (sum, n) {
            return sum + n;
        }, 0);
}
function radices(text) {
    return Number.parseInt(text, 16) + Number.parseInt(' -12px') + Number.parseInt('0x10');
}
function smaller(text) {
    return Math.min(Number.parseInt(text), 3) + Math.min(-1, Number.parseInt(text, 8));
}
var offsets = offsetSum('10,-3,0x7,  8');
var middle = second('4,5,6');
var big = bigOnes('1,2,3,40');
var lengths = lengthSum();
var parsed = radices('ff');
var least = smaller('7');
