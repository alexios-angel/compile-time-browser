// Exercises every value kind the native differential test prints, in one
// comparison: numbers (Number::toString), booleans, quoted strings, null and
// undefined. The convention is defined in
// test/CTNative/Checks/compilation-unit.py.
//
// Without non-Number kinds, whole classes of answers would be untestable: an
// out-of-bounds array read is `undefined`, a predicate's result is a Boolean,
// and a string operation has nothing to be compared against.
//
// The native side for this program is the hand-written values.emitc.mlir,
// whose RUN lines compare it against the interpreter's run of this file.
//
// Values are computed where possible, not copied: `answer` is a
// multiplication, `third` a division and `no` a comparison, so a native
// binary that printed from a table would still have to get the arithmetic
// right. Strings are literals because this fixture tests the print
// convention, not string operations (string.js covers those).
var answer = 6 * 7;
var third = 1 / 3;

// Both booleans, one of them computed: `true` printed where `false` belongs
// is a failure a single-boolean fixture cannot see.
var yes = true;
var no = 1 > 2;

// Strings that look like another kind. This is why the convention quotes
// strings instead of printing them bare: `looks_null` and a global holding
// `null` would otherwise print the same line, and so would `looks_numeric`
// and the number 42.
var looks_null = "null";
var looks_numeric = "42";

var greeting = "hello";

// The empty string must print as `empty=""`, not as nothing at all, so a
// side that drops empty values is caught.
var empty = "";

// Every byte that could break a layer between the two sides, in one value:
//   =  ;  \  "  %  the name/value separator, a list separator, an escape
//                  character, the convention's own string delimiter, and the
//                  percent-escape introducer
//   \u0000        a legal JavaScript string character, so the print cannot be
//                  NUL-terminated on either side
//   \u00ff        a code point that is two UTF-8 bytes, C3 BF
//   \ud800        a lone surrogate. ctbrowser/lib/Script/compile/strings.cpp
//                  encodes it as the three bytes ED A0 80 (WTF-8, not valid
//                  UTF-8). Percent-encoding is per byte, so neither side has
//                  to decide how to repair it.
var raw = "a=1;b\\c\"d%e\u0000\u00ff\ud800";

var nothing = null;

// Declared and never assigned, so its value is `undefined` and it must print
// as `missing=undefined`.
var missing;
