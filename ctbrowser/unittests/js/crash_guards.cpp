// The three ways this engine could be made to DIE rather than answer, and the
// guards that turned each into a value or a catchable error.
//
// test262 found all three on its first run (docs/test262.md, 2026-09-02): 54
// SIGSEGVs from one recursion, 23 SIGABRTs from one allocation, and two
// non-terminating string methods. None of them is a wrong answer, which is why
// none of the differential suites beside this file could ever have found them -
// js_expect compares what an expression PRINTS, and a process that is not there
// prints nothing.
//
// THIS FILE IS THE REGRESSION NET, not test262. That suite is opt-in
// (-DCTBROWSER_TEST262=ON), needs a 273 MB corpus that is deliberately not in
// the repository, and takes five minutes; these are twenty expressions and run
// in milliseconds with the rest of the suite.
//
// EVERY CASE HERE FAILS WITHOUT ITS FIX BY KILLING THE PROCESS - a segfault, an
// abort or a hang - rather than by printing a mismatch. That is the point and
// it is also the hazard: reverting a fix to check that this file goes red must
// be done under `ulimit -v`, or the abort takes the machine with it.

#include "js_expect.hpp"

int main() {
    // ================================================================
    // 1. THE toString CYCLE - 54 SIGSEGVs, 45 of them in Number.prototype
    // ================================================================
    //
    // `Number.prototype.toString()` has an OBJECT receiver, the static
    // to_number of an object is NaN, and the NaN arm asked the context to
    // stringify the receiver - which is ToPrimitive, which calls the receiver's
    // own toString, which is this native. gdb counted 47,000 frames.
    //
    // Number.prototype IS a Number object with [[NumberData]] +0 (21.1.3), so
    // the specified answer is "0" and not a crash.
    js_expect("Number.prototype.toString()", "0");
    js_expect("Number.prototype.valueOf()", "0");
    js_expect("Number.prototype.toPrecision()", "0");
    js_expect("Number.prototype.toFixed(2)", "0.00");
    js_expect("Number.prototype.toString(16)", "0");
    // Any OTHER object receiver has no [[NumberData]] and is a TypeError -
    // there are no wrapper objects in this engine, so this is every other
    // object. It used to be the same infinite recursion.
    js_expect("(function(){try{Number.prototype.toString.call({})}catch(e){return e.name}})()",
              "TypeError");
    js_expect("(function(){try{Number.prototype.toFixed.call([])}catch(e){return e.name}})()",
              "TypeError");
    // An out-of-range radix is a RangeError (21.1.3.6). It used to fall back to
    // the same crashing path.
    js_expect("(function(){try{(5).toString(1)}catch(e){return e.name}})()", "RangeError");
    js_expect("(function(){try{(5).toString(37)}catch(e){return e.name}})()", "RangeError");
    // ...and the radix that works still works, which is what makes the
    // RangeError above a guard rather than a removal.
    js_expect("(220).toString(16)", "dc");
    js_expect("(255).toString(2)", "11111111");
    js_expect("(1.5).toFixed(2)", "1.50");
    js_expect("(0).toString()", "0");
    js_expect("(-1).toString()", "-1");
    js_expect("(1/0).toString()", "Infinity");

    // A CYCLE A PAGE WRITES ITSELF is the same defect without a built-in in it,
    // and it must be a catchable RangeError - which is what every engine throws
    // for stack exhaustion - rather than a fault the page cannot see.
    js_expect("(function(){var o={};o.toString=function(){return String(o)};"
              "try{String(o)}catch(e){return e.name}})()",
              "RangeError");
    // A SELF-REFERENTIAL ARRAY recurses with no call in it at all: to_string
    // walks the elements and finds the array again. V8 answers "" here (its
    // join carries a cycle guard); this engine answers RangeError, which is a
    // deviation and is still the difference between an error and a SIGSEGV.
    js_expect("(function(){var a=[];a[0]=a;try{String(a)}catch(e){return e.name}})()",
              "RangeError");

    // AND THE GUARD MUST NOT FIRE ON WORK THAT TERMINATES. 300 nested arrays
    // is 301 levels of the same conversion against a ceiling of 512, so a
    // legitimate deep-but-finite ToString still answers.
    js_expect("(function(){var a=[1];for(var i=0;i<300;i++){a=[a]}return String(a)})()", "1");
    // ...and so does a deep chain of user toString calls that ends.
    js_expect("(function(){function nest(n){return {toString:function(){"
              "return n===0?'x':String(nest(n-1))}}}return String(nest(100))})()",
              "x");

    // ================================================================
    // 2. THE DENSE ARRAY - 23 SIGABRTs
    // ================================================================
    //
    // `a[4294967295] = "x"` resized a std::vector<value> to 4,294,967,296
    // slots: 34 GB, std::bad_alloc, SIGABRT. 2^32-1 is not an array index
    // (6.1.7), so the specified answer stores it without touching `length`.
    js_expect("(function(){var a=[];a[4294967295]='x';return a[4294967295]})()", "x");
    js_expect("(function(){var a=[];a[4294967295]='x';return a.length})()", "0");
    js_expect("(function(){var a=[0,1,2];a[4294967295]='x';return a.length})()", "3");
    // A VALID index that far out DOES raise the length, and is still not
    // materialised.
    js_expect("(function(){var a=[];a[2147483648]=1;return a.length})()", "2147483649");
    js_expect("(function(){var a=[];a[2147483648]=1;return a[2147483648]})()", "1");
    js_expect("(function(){var a=[];a[4294967294]=1;return a.length})()", "4294967295");
    // `a.length = n` records rather than allocates, and refuses what is not a
    // length at all - 10.4.2.4's RangeError, which used to be swallowed.
    js_expect("(function(){var a=[];a.length=4294967295;return a.length})()", "4294967295");
    js_expect("(function(){try{var a=[];a.length=4294967296}catch(e){return e.name}})()",
              "RangeError");
    js_expect("(function(){try{var a=[];a.length=-1}catch(e){return e.name}})()", "RangeError");
    js_expect("(function(){try{var a=[];a.length=1.5}catch(e){return e.name}})()", "RangeError");
    // `new Array(n)` took the same allocation and the same abort.
    js_expect("new Array(4294967295).length", "4294967295");
    js_expect("(function(){try{new Array(1.5)}catch(e){return e.name}})()", "RangeError");
    js_expect("new Array(3).length", "3");
    // SHRINKING DISCARDS BOTH HALVES: an index at or above the new length is
    // deleted whether it was materialised or not.
    js_expect("(function(){var a=[];a[2147483648]=1;a.length=0;return a.length})()", "0");
    js_expect("(function(){var a=[];a[2147483648]=1;a.length=0;return typeof a[2147483648]})()",
              "undefined");

    // AND AN ORDINARY ARRAY IS STILL DENSE, which is the whole reason the rule
    // is on the size of the JUMP rather than on the index: a sequential fill
    // grows by one slot at a time and never goes sparse.
    js_expect("(function(){var a=[];for(var i=0;i<10;i++){a[i]=i}return a.join(',')})()",
              "0,1,2,3,4,5,6,7,8,9");
    js_expect("(function(){var a=[];a[5]=1;return a.length})()", "6");
    js_expect("(function(){var a=[];a[5]=1;return String(a)})()", ",,,,,1");
    js_expect("(function(){var a=[1,2,3];a.length=1;return a.join(',')})()", "1");
    js_expect("(function(){var a=[1];a.length=3;return a.length+':'+a.join(',')})()", "3:1,,");

    // ================================================================
    // 3. padStart / padEnd / repeat - 2 TIMEOUTs and 6 CRASHes
    // ================================================================
    //
    // All three read the length with the STATIC to_number, which is NaN for an
    // object, and `std::clamp(NaN, 0.0, 1e6)` is NaN - so the cast to size_t
    // was undefined behaviour and landed on 9.2e18. The loop then appended
    // until the process died or the runner's timeout fired.
    js_expect("'abc'.padStart(NaN,'def')", "abc");
    js_expect("'abc'.padEnd(NaN,'def')", "abc");
    js_expect("'abc'.padStart(undefined,'def')", "abc");
    js_expect("'abc'.padStart(null,'def')", "abc");
    js_expect("'abc'.padStart(-Infinity,'def')", "abc");
    js_expect("'abc'.padStart(-1,'def')", "abc");
    js_expect("'abc'.padStart(3.9999,'def')", "abc");
    // An OBSERVABLE coercion - the length is an object with a valueOf - is the
    // shape the two TIMEOUTs had. It has to run the valueOf, which the static
    // to_number could not do.
    js_expect("'abc'.padStart({valueOf:function(){return 11}},'def')", "defdefdeabc");
    js_expect("'abc'.padEnd({valueOf:function(){return 6}},'-')", "abc---");
    js_expect("'x'.repeat({valueOf:function(){return 3}})", "xxx");
    // A length past what this engine will build is a RangeError, not a silent
    // clamp that answers with a shorter string than was asked for.
    js_expect("(function(){try{'x'.repeat(1e9)}catch(e){return e.name}})()", "RangeError");
    js_expect("(function(){try{'abc'.padStart(1e9)}catch(e){return e.name}})()", "RangeError");
    js_expect("(function(){try{'abc'.padStart(Infinity)}catch(e){return e.name}})()", "RangeError");
    // 22.1.3.16 steps 4 and 5: a negative or infinite count is a RangeError,
    // which the clamp turned into "" and into a million characters.
    js_expect("(function(){try{''.repeat(Infinity)}catch(e){return e.name}})()", "RangeError");
    js_expect("(function(){try{'x'.repeat(-1)}catch(e){return e.name}})()", "RangeError");
    // ...and the ordinary cases still answer.
    js_expect("'abc'.repeat(0)", "");
    js_expect("'abc'.repeat(2)", "abcabc");
    js_expect("''.repeat(1e9)", ""); // an empty string is free at any count
    js_expect("'abc'.padStart(6,'12')", "121abc");
    js_expect("'abc'.padEnd(6,'12')", "abc121");
    js_expect("'abc'.padStart(6)", "   abc");
    js_expect("'abc'.padStart(6,'')", "abc"); // an empty filler pads nothing

    REPORT("crash_guards");
}
