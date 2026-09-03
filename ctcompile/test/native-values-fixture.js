// THE FIRST PROGRAM WHOSE GLOBALS ARE NOT ALL NUMBERS - the differential gate
// compares values that are not numbers.
//
// Before this fixture the gate's convention was one line per NUMERIC global,
// `name=%.17g`, and the driver treated any other kind as a fatal error. That
// made a whole class of answers untestable: an array's out-of-bounds read is
// `undefined` (native-array-fixture.js says so, and drops the case for exactly
// that reason), a predicate's result is a Boolean, and a string phase had
// nothing to be proved against. The convention now covers five kinds, and this
// is the program that exercises all five in one comparison.
//
// EVERY VALUE HERE IS COMPUTED WHERE IT CAN BE, not copied. `answer` is a
// multiplication, `third` a division and `no` a comparison, so a native side
// that had memorised the expected text would still have to get the arithmetic
// right; a fixture of nothing but literals would agree with a binary that
// printed from a table. Strings are literals because string arithmetic is a
// later phase - this commit is the convention, not the operations.
var answer = 6 * 7;
var third = 1 / 3;

// BOTH BOOLEANS, and one of them computed. `true` printed where `false`
// belongs is the failure a single-boolean fixture cannot see.
var yes = true;
var no = 1 > 2;

// A STRING THAT LOOKS LIKE ANOTHER KIND, twice. These are why the convention
// quotes strings instead of printing them bare: `looks_null` and a global
// holding `null` would otherwise print the same line, and so would
// `looks_numeric` and the number 42.
var looks_null = "null";
var looks_numeric = "42";

var greeting = "hello";

// THE EMPTY STRING, which is the anti-vacuity case for strings specifically:
// it must print as `empty=""` and not as nothing at all.
var empty = "";

// EVERY BYTE THAT WOULD BREAK A LAYER BETWEEN THE TWO SIDES, in one value:
//   =  ;  \  "  %  the separator, CMake's list separator, CMake's escape
//                  character, the convention's own delimiter, and the escape
//                  introducer
//   \u0000        a legal JavaScript string character, so the print cannot be
//                  NUL-terminated on either side
//   \u00ff        a code point that is two UTF-8 bytes, C3 BF
//   \ud800        a LONE SURROGATE. compile/strings.cpp encodes it as a code
//                  point in its own right, which is the three bytes ED A0 80 -
//                  WTF-8, and NOT valid UTF-8. Percent-encoding is per byte,
//                  so neither side has to decide how to repair it.
var raw = "a=1;b\\c\"d%e\u0000\u00ff\ud800";

var nothing = null;

// DECLARED AND NEVER ASSIGNED, so its value is `undefined`. This is the shape
// the array fixture had to leave out.
var missing;
