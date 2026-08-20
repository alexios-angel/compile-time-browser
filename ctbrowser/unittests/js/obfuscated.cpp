// The JavaScript that real pages actually ship, against V8.
//
// Nobody writes the code in this file. Minifiers, bundlers and obfuscators
// emit it: `!![]+!![]` for 2, `void 0` for undefined, `~s.indexOf(x)` for a
// truthiness test, `!function(){}()` for an IIFE, a comma sequence where a
// human would write statements. This engine's whole purpose is running other
// people's bundles - p5.js, Phaser and Babylon are all minified - so these
// shapes are not exotic here, they are the common case.
//
// Differentially tested against node (V8) over ~110 expressions; 9 differed,
// and the four that matter are pinned as KNOWN WRONG at the bottom. The rest
// of the file is behaviour that already works, pinned so it keeps working -
// which is the point: a parser regression in any of these would break every
// bundle at once, and the corpora ratchets would report it as "p5 stopped at
// rung 3" rather than as a missing operator.

#include "js_expect.hpp"

int main() {
    // --- numbers built out of [] and ! ---------------------------------------
    // The JSFuck family. An obfuscator writes every integer this way, and each
    // step is an ordinary coercion: [] is "" is 0, ![] is false, !![] is true,
    // true + true is 2.
    js_expect("+[]", "0");
    js_expect("+!![]", "1");
    js_expect("!![]+!![]", "2");
    js_expect("+!![]+!![]+!![]", "3");
    js_expect("[]+[]", "");
    js_expect("(+[])+\"\"", "0");
    js_expect("!![]+[]", "true");
    js_expect("![]+[]", "false");
    js_expect("(![]+[])[+[]]", "f"); // "false"[0]
    js_expect("typeof(![]+[])", "string");
    js_expect("[][[]]", "undefined"); // [] indexed by "" is undefined
    js_expect("typeof [][[]]", "undefined");

    // --- IIFEs, void, sequences, chained assignment --------------------------
    js_expect("void 0", "undefined");
    js_expect("void \"x\"", "undefined");
    js_expect("(function(){return 1})()", "1");
    js_expect("(function(){return 1}())", "1"); // the other bracketing
    js_expect("!function(){return 1}()", "false");
    js_expect("(()=>1)()", "1");
    js_expect("(1,2,3)", "3"); // a comma sequence yields the last
    js_expect("(1,2,3,\"last\")", "last");
    js_expect("(function(){var a,b,c;a=b=c=7;return a+b+c})()", "21");
    js_expect("(function(){var x=(1,2);return x})()", "2");

    // --- the bit tricks minifiers reach for ----------------------------------
    // `~n` is -(n+1), so `~indexOf(x)` is 0 exactly when indexOf returned -1 -
    // which is why `if (~s.indexOf(x))` is the idiomatic "contains".
    js_expect("~0", "-1");
    js_expect("~-1", "0");
    js_expect("~~3.7", "3");   // double bitwise-not truncates
    js_expect("~~-3.7", "-3"); // toward zero, unlike Math.floor
    js_expect("~\"abc\".indexOf(\"b\")", "-2");
    js_expect("~\"abc\".indexOf(\"z\")", "0");
    js_expect("!~\"abc\".indexOf(\"z\")", "true");
    js_expect("1<<3", "8");
    js_expect("16>>2", "4");
    js_expect("-1>>>28", "15"); // the unsigned shift, which has no C equivalent
    js_expect("5&3", "1");
    js_expect("5|3", "7");
    js_expect("5^3", "6");
    js_expect("0|\"7\"", "7"); // another way to force a number

    // --- escapes and computed member access ----------------------------------
    // An obfuscator hides names by spelling them: `o["\x61"]` is `o.a`.
    js_expect("\"\\x41\"", "A");
    js_expect("\"\\u0041\"", "A");
    js_expect("\"\\0\".length", "1");
    js_expect("\"a\\tb\".length", "3");
    js_expect("String.fromCharCode(97,98,99)", "abc");
    js_expect("({a:1})[\"a\"]", "1");
    js_expect("({a:1})[\"\\x61\"]", "1");
    js_expect("(function(){var o={};o[\"x\"+\"y\"]=5;return o.xy})()", "5");
    js_expect("(function(){var k=\"len\"+\"gth\";return \"abc\"[k]})()", "3");

    // --- literal forms -------------------------------------------------------
    js_expect("0x10", "16");
    js_expect("0xFF", "255");
    js_expect("1e3", "1000");
    js_expect("1E3", "1000");
    js_expect(".5", "0.5");
    js_expect("5.", "5");

    // --- ternaries, short circuit, optional chaining --------------------------
    js_expect("1?2:3", "2");
    js_expect("0?1:2?3:4", "3"); // nested, right-associative
    js_expect("(null||\"d\")", "d");
    js_expect("({a:{b:1}})?.a?.b", "1");
    js_expect("({a:null})?.a?.b", "undefined");
    js_expect("(null)?.x", "undefined");
    js_expect("(undefined)?.x?.y", "undefined");
    js_expect("null??\"d\"", "d");
    js_expect("(void 0)??\"d\"", "d");

    // --- statements, labels, loops -------------------------------------------
    js_expect("(function(){var s=0;outer:for(var i=0;i<3;i++){for(var j=0;j<3;j++){"
              "if(j==1)continue outer;s++}}return s})()",
              "3"); // a labelled continue, which minified loops use
    js_expect("(function(){var s=0;a:{s=1;break a;s=2}return s})()", "1"); // a labelled block
    js_expect("(function(){var s=\"\";for(var i=0,n=3;i<n;i++)s+=i;return s})()", "012");
    js_expect("(function(){var i=0;do{i++}while(i<3);return i})()", "3");
    js_expect("(function(){try{throw 1}catch(e){return \"c\"+e}finally{}})()", "c1");
    js_expect("(function(){try{return \"t\"}finally{}})()", "t");
    js_expect("(function(){switch(2){case 1:return \"a\";case 2:case 3:return \"b\";"
              "default:return \"d\"}})()",
              "b"); // fallthrough into a shared body

    // --- regex against division, which is the classic lexer ambiguity --------
    // `a/b/g` is two divisions; `/b/g` after `=` is a regexp. A lexer that gets
    // this wrong fails on ordinary arithmetic, silently.
    js_expect("(function(){var a=4,b=2,g=1;return a/b/g})()", "2");
    js_expect("\"aXbXc\".split(/X/).join(\"-\")", "a-b-c");
    js_expect("/a/.test(\"bab\")", "true");
    js_expect("/a/g.source", "a");
    js_expect("\"abc\".replace(/b/,function(m){return m.toUpperCase()})", "aBc");
    js_expect("(function(){var re=/(\\d+)/;var m=\"a12b\".match(re);return m[1]})()", "12");

    // --- destructuring, spread, rest, shorthand ------------------------------
    js_expect("(function(){var [a,b=5]=[1];return a+b})()", "6");
    js_expect("(function(){var {x,y=2}={x:1};return x+y})()", "3");
    js_expect("(function(){var [a,...r]=[1,2,3];return r.join(\"\")})()", "23");
    js_expect("(function(){function f(...a){return a.length}return f(1,2,3)})()", "3");
    js_expect("(function(){var k=\"z\";var o={[k]:9};return o.z})()", "9");
    js_expect("(function(){var o={get v(){return 3}};return o.v})()", "3");
    js_expect("(function(){var o={m(){return 4}};return o.m()})()", "4");
    js_expect("(function(){var a=1,o={a};return o.a})()", "1");
    js_expect("Math.max(...[1,5,3])", "5");
    js_expect("[...[1,2],...[3]].join(\"\")", "123");
    js_expect("({...{a:1},b:2}).a", "1");

    // --- closures and hoisting ------------------------------------------------
    js_expect("(function(){var f=[];for(var i=0;i<3;i++)f.push(function(){return i});"
              "return f[0]()})()",
              "3"); // `var` shares ONE binding - correct, and the classic gotcha
    js_expect(
        "(function(){let a=[];for(let i of [0,1,2]){a.push(()=>i)}return a[0]()+\",\"+a[1]()})()",
        "0,1"); // for-of DOES bind per iteration
    js_expect("(function(){return typeof h})()", "undefined");
    js_expect("(function(){function h(){}return typeof h})()", "function"); // hoisted
    js_expect("(function(){var o={v:1,get(){return this.v}};return o.get()})()", "1");

    // --- the module wrapper shapes bundles are built from --------------------
    js_expect("(function(g,f){return f(g)})(this||{},function(g){return \"umd\"})", "umd");
    js_expect(
        "(function(){var m={exports:{}};(function(e){e.v=1})(m.exports);return m.exports.v})()",
        "1");

    // --- fixed, and pinned so a regression trips over them --------------------
    //
    // `for (let i = ...)` gives every iteration its OWN binding, so these
    // capture 0, 1 and 2 where the `var` loop above captures 3 three times.
    // This engine shared one binding - silently, and modern minified output
    // leans on the difference constantly. compile_for copies the boxed loop
    // bindings between the body and the update, which is where
    // ForBodyEvaluation puts it.
    js_expect("(function(){var f=[];for(let i=0;i<3;i++){f.push(function(){return i})}"
              "return f[0]()+\",\"+f[1]()+\",\"+f[2]()})()",
              "0,1,2");
    //
    // These forms did not lex at all: ctjs special-cased 0x and stopped a
    // number token at `_`, so `0o17` came through as `0` then the identifier
    // `o17`. compile.cpp's radix_of already handled 0o and 0b, so the fix was
    // in the submodule's lexer plus stripping the separators before from_chars,
    // which accepts none.
    js_expect("0o17", "15");
    js_expect("0b101", "5");
    js_expect("1_000", "1000"); // numeric separators, ES2021
    js_expect("1_000_000", "1000000");
    js_expect("0xFF_FF", "65535"); // separators inside a radix literal too
    js_expect("1e1_0", "10000000000");

    // --- KNOWN WRONG: legacy octal, Annex B sloppy mode ----------------------
    js_expect("\"\\101\"", "101"); // V8: "A"
    js_expect("017", "17");        // V8: 15

    REPORT("obfuscated");
}
