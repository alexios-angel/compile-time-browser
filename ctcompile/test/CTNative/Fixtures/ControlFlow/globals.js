// End-to-end fixture for the native (GC-free) target: JavaScript in, a binary
// out, its printed globals compared with the interpreter's.
//
// Numbers, booleans, globals, loops and branches, with no functions. Every
// global below is assigned before it is read, because an unassigned numeric
// global is NaN natively and `undefined` in the interpreter, which the
// reference prints differently on purpose.
//
// globals.test runs it through the native differential pipeline (Native.cmake).

var total = 0;
var count = 0;
while (count < 10) {
    total = total + count * 0.5;
    count = count + 1;
}
var ratio = total / count;
var neg = -ratio;
var big = 2 ** 31;
// `**` is not std::pow. Number::exponentiate answers NaN when the base has
// magnitude one and the exponent is NaN or infinite, where C++ answers 1.
// Undefined is NaN in native code, so `1 ** undefined` would reach the same
// difference from ordinary JavaScript. These globals check that the native
// code prints NaN, as the interpreter does.
var one_to_nan = 1 ** (0 / 0);
var one_to_inf = 1 ** (1 / 0);
var minus_one_to_inf = (0 - 1) ** (1 / 0);
var rem = 7.5 % 2;
var quarter = 1 / 4;
var inf = 1 / 0;
var ninf = -1 / 0;
var nan = 0 / 0;
var branch = 0;
if (total > 20) {
    branch = 1;
} else {
    branch = 2;
}
var nested = 0;
var i = 0;
while (i < 5) {
    var j = 0;
    while (j < i) {
        if (j % 2 == 0) {
            nested = nested + j;
        } else {
            nested = nested - 1;
        }
        j = j + 1;
    }
    i = i + 1;
}
var truthy = 0;
if (nan) {
    truthy = truthy + 1;
}
if (0.5) {
    truthy = truthy + 2;
}
if (-0) {
    truthy = truthy + 4;
}
if (!0) {
    truthy = truthy + 8;
}
