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

    REPORT("crash_guards");
}
