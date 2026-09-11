// THE FIRST PROGRAM THAT GOES ALL THE WAY: JavaScript in, a GC-free binary
// out, its printed globals compared with the interpreter's - part 24 Phase
// 62½, stages C and D together.
//
// No functions yet: a call needs the closed world of stage A to type its
// parameters, and that lands on its own branch. Everything here is what
// stage C proves today - numbers, booleans, globals, loops and branches -
// and every global below is assigned before it is read, because an
// unassigned numeric global is NaN natively and `undefined` in the
// interpreter, which the reference prints differently on purpose.
var total = 0;
var count = 0;
while (count < 10) {
  total = total + count * 0.5;
  count = count + 1;
}
var ratio = total / count;
var neg = -ratio;
var big = 2 ** 31;
// `**` IS NOT std::pow. Number::exponentiate answers NaN when the base has
// magnitude one and the exponent is NaN or infinite; C++ answers 1. Undefined
// is this tier's NaN, so `1 ** undefined` reaches the same difference from
// ordinary JavaScript. Both of these printed 1 natively and NaN in the
// interpreter, on a program nothing refused.
var one_to_nan = 1 ** (0 / 0);
var one_to_inf = 1 ** (1 / 0);
var minus_one_to_inf = (0 - 1) ** (1 / 0);
var rem = 7.5 % 2;
var quarter = 1 / 4;
var inf = 1 / 0;
var ninf = -1 / 0;
var nan = 0 / 0;
var branch = 0;
if (total > 20) { branch = 1; } else { branch = 2; }
var nested = 0;
var i = 0;
while (i < 5) {
  var j = 0;
  while (j < i) {
    if (j % 2 == 0) { nested = nested + j; } else { nested = nested - 1; }
    j = j + 1;
  }
  i = i + 1;
}
var truthy = 0;
if (nan) { truthy = truthy + 1; }
if (0.5) { truthy = truthy + 2; }
if (-0) { truthy = truthy + 4; }
if (!0) { truthy = truthy + 8; }
