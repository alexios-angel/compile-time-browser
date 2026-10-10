//===- ctbrowser/unittests/js/crash_guards.cpp - Crash regression guards --===//
//
// Inputs that once made the engine crash, hang or abort instead of answering,
// and the guards that turned each into a value or a catchable error. test262
// found the first three groups (docs/test262.md): a recursion, an allocation
// and two non-terminating string methods. Section 4 covers natives that a
// page re-enters halfway through. None of these is a wrong answer, so the
// differential suites (which compare what an expression prints) cannot catch
// them.
//
// This file is the regression net, not test262: that suite is opt-in
// (-DCTBROWSER_TEST262=ON), needs a 273 MB corpus kept outside the repository
// and takes minutes, whereas these cases run in milliseconds.
//
// Without its guard each case kills the process (a segfault, an abort or a
// hang) rather than printing a mismatch. So reverting a guard to see this file
// go red must be done under `ulimit -v`, or the abort can take the machine
// with it. Bare section numbers are ECMA-262.
//
//===----------------------------------------------------------------------===//

#include "js_expect.hpp"

int main() {
    // ================================================================
    // 1. The toString cycle (a stack-overflow SIGSEGV)
    // ================================================================
    //
    // `Number.prototype.toString()` has an object receiver. Stringifying that
    // receiver through ToPrimitive would call its own toString, which is this
    // native again, recursing until the stack runs out.
    //
    // Number.prototype is a Number object with [[NumberData]] +0 (21.1.3), so
    // the specified answer is "0".
    js_expect("Number.prototype.toString()", "0");
    js_expect("Number.prototype.valueOf()", "0");
    js_expect("Number.prototype.toPrecision()", "0");
    js_expect("Number.prototype.toFixed(2)", "0.00");
    js_expect("Number.prototype.toString(16)", "0");
    // Any other object receiver (anything but a Number wrapper) has no
    // [[NumberData]] and is a TypeError.
    js_expect("(function(){try{Number.prototype.toString.call({})}catch(e){return e.name}})()",
              "TypeError");
    js_expect("(function(){try{Number.prototype.toFixed.call([])}catch(e){return e.name}})()",
              "TypeError");
    // An out-of-range radix is a RangeError (21.1.3.6).
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

    // A cycle the page writes itself is the same defect without a built-in in
    // it. It must be a catchable RangeError (what every engine throws for stack
    // exhaustion) rather than a fault the page cannot see.
    js_expect("(function(){var o={};o.toString=function(){return String(o)};"
              "try{String(o)}catch(e){return e.name}})()",
              "RangeError");
    // The interpreter's own call/construct limits are catchable too.
    js_expect("(function(){function f(){f()}try{f()}catch(e){return e.name}})()", "RangeError");
    js_expect("(function(){function f(){f.call(null)}try{f()}catch(e){return e.name}})()",
              "RangeError");
    js_expect("(function(){function F(){new F()}try{new F()}catch(e){return e.name}})()",
              "RangeError");
    // A self-referential array recurses with no call in it at all: to_string
    // walks the elements and finds the array again. V8 answers "" here (its
    // join carries a cycle guard); this engine answers RangeError, which is a
    // deviation and is still the difference between an error and a SIGSEGV.
    js_expect("(function(){var a=[];a[0]=a;try{String(a)}catch(e){return e.name}})()",
              "RangeError");

    // The guard must not fire on work that terminates. 300 nested arrays
    // is 301 levels of the same conversion against a ceiling of 512, so a
    // legitimate deep-but-finite ToString still answers.
    js_expect("(function(){var a=[1];for(var i=0;i<300;i++){a=[a]}return String(a)})()", "1");
    // ...and so does a deep chain of user toString calls that ends.
    js_expect("(function(){function nest(n){return {toString:function(){"
              "return n===0?'x':String(nest(n-1))}}}return String(nest(100))})()",
              "x");

    // ================================================================
    // 2. Huge indices on a dense array (an out-of-memory SIGABRT)
    // ================================================================
    //
    // Storing `a[4294967295] = "x"` densely would resize the element vector to
    // 4,294,967,296 slots (34 GB, std::bad_alloc, SIGABRT). 2^32-1 is not an
    // array index (6.1.7), so it is stored as a plain property and `length`
    // does not change.
    js_expect("(function(){var a=[];a[4294967295]='x';return a[4294967295]})()", "x");
    js_expect("(function(){var a=[];a[4294967295]='x';return a.length})()", "0");
    js_expect("(function(){var a=[0,1,2];a[4294967295]='x';return a.length})()", "3");
    // A valid index that far out does raise the length, and is still not
    // materialised.
    js_expect("(function(){var a=[];a[2147483648]=1;return a.length})()", "2147483649");
    js_expect("(function(){var a=[];a[2147483648]=1;return a[2147483648]})()", "1");
    js_expect("(function(){var a=[];a[4294967294]=1;return a.length})()", "4294967295");
    // `a.length = n` records rather than allocates, and refuses what is not a
    // length at all (10.4.2.4's RangeError).
    js_expect("(function(){var a=[];a.length=4294967295;return a.length})()", "4294967295");
    js_expect("(function(){try{var a=[];a.length=4294967296}catch(e){return e.name}})()",
              "RangeError");
    js_expect("(function(){try{var a=[];a.length=-1}catch(e){return e.name}})()", "RangeError");
    js_expect("(function(){try{var a=[];a.length=1.5}catch(e){return e.name}})()", "RangeError");
    // `new Array(n)` must not allocate n slots either.
    js_expect("new Array(4294967295).length", "4294967295");
    js_expect("(function(){try{new Array(1.5)}catch(e){return e.name}})()", "RangeError");
    js_expect("new Array(3).length", "3");
    // Shrinking discards both kinds: an index at or above the new length is
    // deleted whether it was materialised or not.
    js_expect("(function(){var a=[];a[2147483648]=1;a.length=0;return a.length})()", "0");
    js_expect("(function(){var a=[];a[2147483648]=1;a.length=0;return typeof a[2147483648]})()",
              "undefined");

    // An ordinary array is still dense, which is why the rule is on the size
    // of the jump rather than on the index: a sequential fill grows by one slot
    // at a time and never goes sparse.
    js_expect("(function(){var a=[];for(var i=0;i<10;i++){a[i]=i}return a.join(',')})()",
              "0,1,2,3,4,5,6,7,8,9");
    js_expect("(function(){var a=[];a[5]=1;return a.length})()", "6");
    js_expect("(function(){var a=[];a[5]=1;return String(a)})()", ",,,,,1");
    js_expect("(function(){var a=[1,2,3];a.length=1;return a.join(',')})()", "1");
    js_expect("(function(){var a=[1];a.length=3;return a.length+':'+a.join(',')})()", "3:1,,");

    // ================================================================
    // 3. padStart / padEnd / repeat (hangs and crashes)
    // ================================================================
    //
    // The length must go through ToNumber with the page's valueOf, and NaN
    // must become 0. `std::clamp(NaN, 0.0, 1e6)` is NaN, and casting that to
    // size_t is undefined behaviour that can land near 9.2e18, after which the
    // loop appends until the process dies.
    js_expect("'abc'.padStart(NaN,'def')", "abc");
    js_expect("'abc'.padEnd(NaN,'def')", "abc");
    js_expect("'abc'.padStart(undefined,'def')", "abc");
    js_expect("'abc'.padStart(null,'def')", "abc");
    js_expect("'abc'.padStart(-Infinity,'def')", "abc");
    js_expect("'abc'.padStart(-1,'def')", "abc");
    js_expect("'abc'.padStart(3.9999,'def')", "abc");
    // An observable coercion: the length is an object with a valueOf, which
    // must run.
    js_expect("'abc'.padStart({valueOf:function(){return 11}},'def')", "defdefdeabc");
    js_expect("'abc'.padEnd({valueOf:function(){return 6}},'-')", "abc---");
    js_expect("'x'.repeat({valueOf:function(){return 3}})", "xxx");
    // A length past what this engine will build is a RangeError, not a silent
    // clamp that answers with a shorter string than was asked for.
    js_expect("(function(){try{'x'.repeat(1e9)}catch(e){return e.name}})()", "RangeError");
    js_expect("(function(){try{'abc'.padStart(1e9)}catch(e){return e.name}})()", "RangeError");
    js_expect("(function(){try{'abc'.padStart(Infinity)}catch(e){return e.name}})()", "RangeError");
    // 22.1.3.16 steps 4 and 5: a negative or infinite count is a RangeError,
    // not a clamp to "" or to a million characters.
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

    // ================================================================
    // 4. What a page can do to a native halfway through
    // ================================================================
    //
    // A second re-entry overflow in one episode throws nothing (the first one
    // did) and hands back undefined. A native caller that trusts the result
    // (async-from-sync iteration's step, which casts it to the pair array it
    // built) must not read that undefined as its answer. Here the page catches
    // the first RangeError deep in a toString chain and, still past the
    // ceiling, runs a `for await` over an array.
    js_expect("(function(){var out='none';function nest(n){return {toString:function(){"
              "if(n===0)return 'x';try{return String(nest(n-1))}catch(e){"
              "if(out==='none'){out='caught';(async function(){for await(const v of [1]){}})()"
              ".then(function(){},function(){})}throw e}}}}"
              "try{String(nest(700))}catch(e){}return out})()",
              "caught");
    // Object spread reads each property, and a getter that defines more of
    // them grows the accessor table under the walk. OwnPropertyKeys is taken
    // once (CopyDataProperties step 4), so only the key present then is copied.
    js_expect("(function(){var o={};Object.defineProperty(o,'a',{get:function(){"
              "for(var i=0;i<64;i++)Object.defineProperty(o,'k'+i,{get:function(){return 1},"
              "enumerable:true,configurable:true});return 1},enumerable:true,configurable:true});"
              "return Object.keys({...o}).join(',')})()",
              "a");

    REPORT("crash_guards");
}
