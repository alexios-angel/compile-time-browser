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

#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include <string>
#include <string_view>

using namespace ctbrowser::script;

namespace {

[[nodiscard]] std::string run(std::string_view expression) {
    const program prog = compiler::compile("return (" + std::string{expression} + ");");
    context cx;
    install_builtins(cx);
    const run_result r = cx.run(prog);
    if (!r.ok) { return "THREW"; }
    return cx.to_string(r.returned);
}

void expect(std::string_view expression, std::string_view want) {
    const std::string got = run(expression);
    if (got != want) {
        std::printf("FAIL     %-58s => %s (want %s)\n", std::string{expression}.c_str(),
                    got.c_str(), std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}

} // namespace

int main() {
    // --- numbers built out of [] and ! ---------------------------------------
    // The JSFuck family. An obfuscator writes every integer this way, and each
    // step is an ordinary coercion: [] is "" is 0, ![] is false, !![] is true,
    // true + true is 2.
    expect("+[]", "0");
    expect("+!![]", "1");
    expect("!![]+!![]", "2");
    expect("+!![]+!![]+!![]", "3");
    expect("[]+[]", "");
    expect("(+[])+\"\"", "0");
    expect("!![]+[]", "true");
    expect("![]+[]", "false");
    expect("(![]+[])[+[]]", "f"); // "false"[0]
    expect("typeof(![]+[])", "string");
    expect("[][[]]", "undefined"); // [] indexed by "" is undefined
    expect("typeof [][[]]", "undefined");

    // --- IIFEs, void, sequences, chained assignment --------------------------
    expect("void 0", "undefined");
    expect("void \"x\"", "undefined");
    expect("(function(){return 1})()", "1");
    expect("(function(){return 1}())", "1"); // the other bracketing
    expect("!function(){return 1}()", "false");
    expect("(()=>1)()", "1");
    expect("(1,2,3)", "3"); // a comma sequence yields the last
    expect("(1,2,3,\"last\")", "last");
    expect("(function(){var a,b,c;a=b=c=7;return a+b+c})()", "21");
    expect("(function(){var x=(1,2);return x})()", "2");

    // --- the bit tricks minifiers reach for ----------------------------------
    // `~n` is -(n+1), so `~indexOf(x)` is 0 exactly when indexOf returned -1 -
    // which is why `if (~s.indexOf(x))` is the idiomatic "contains".
    expect("~0", "-1");
    expect("~-1", "0");
    expect("~~3.7", "3");   // double bitwise-not truncates
    expect("~~-3.7", "-3"); // toward zero, unlike Math.floor
    expect("~\"abc\".indexOf(\"b\")", "-2");
    expect("~\"abc\".indexOf(\"z\")", "0");
    expect("!~\"abc\".indexOf(\"z\")", "true");
    expect("1<<3", "8");
    expect("16>>2", "4");
    expect("-1>>>28", "15"); // the unsigned shift, which has no C equivalent
    expect("5&3", "1");
    expect("5|3", "7");
    expect("5^3", "6");
    expect("0|\"7\"", "7"); // another way to force a number

    // --- escapes and computed member access ----------------------------------
    // An obfuscator hides names by spelling them: `o["\x61"]` is `o.a`.
    expect("\"\\x41\"", "A");
    expect("\"\\u0041\"", "A");
    expect("\"\\0\".length", "1");
    expect("\"a\\tb\".length", "3");
    expect("String.fromCharCode(97,98,99)", "abc");
    expect("({a:1})[\"a\"]", "1");
    expect("({a:1})[\"\\x61\"]", "1");
    expect("(function(){var o={};o[\"x\"+\"y\"]=5;return o.xy})()", "5");
    expect("(function(){var k=\"len\"+\"gth\";return \"abc\"[k]})()", "3");

    // --- literal forms -------------------------------------------------------
    expect("0x10", "16");
    expect("0xFF", "255");
    expect("1e3", "1000");
    expect("1E3", "1000");
    expect(".5", "0.5");
    expect("5.", "5");

    // --- ternaries, short circuit, optional chaining --------------------------
    expect("1?2:3", "2");
    expect("0?1:2?3:4", "3"); // nested, right-associative
    expect("(null||\"d\")", "d");
    expect("({a:{b:1}})?.a?.b", "1");
    expect("({a:null})?.a?.b", "undefined");
    expect("(null)?.x", "undefined");
    expect("(undefined)?.x?.y", "undefined");
    expect("null??\"d\"", "d");
    expect("(void 0)??\"d\"", "d");

    // --- statements, labels, loops -------------------------------------------
    expect("(function(){var s=0;outer:for(var i=0;i<3;i++){for(var j=0;j<3;j++){"
           "if(j==1)continue outer;s++}}return s})()",
           "3"); // a labelled continue, which minified loops use
    expect("(function(){var s=0;a:{s=1;break a;s=2}return s})()", "1"); // a labelled block
    expect("(function(){var s=\"\";for(var i=0,n=3;i<n;i++)s+=i;return s})()", "012");
    expect("(function(){var i=0;do{i++}while(i<3);return i})()", "3");
    expect("(function(){try{throw 1}catch(e){return \"c\"+e}finally{}})()", "c1");
    expect("(function(){try{return \"t\"}finally{}})()", "t");
    expect("(function(){switch(2){case 1:return \"a\";case 2:case 3:return \"b\";"
           "default:return \"d\"}})()",
           "b"); // fallthrough into a shared body

    // --- regex against division, which is the classic lexer ambiguity --------
    // `a/b/g` is two divisions; `/b/g` after `=` is a regexp. A lexer that gets
    // this wrong fails on ordinary arithmetic, silently.
    expect("(function(){var a=4,b=2,g=1;return a/b/g})()", "2");
    expect("\"aXbXc\".split(/X/).join(\"-\")", "a-b-c");
    expect("/a/.test(\"bab\")", "true");
    expect("/a/g.source", "a");
    expect("\"abc\".replace(/b/,function(m){return m.toUpperCase()})", "aBc");
    expect("(function(){var re=/(\\d+)/;var m=\"a12b\".match(re);return m[1]})()", "12");

    // --- destructuring, spread, rest, shorthand ------------------------------
    expect("(function(){var [a,b=5]=[1];return a+b})()", "6");
    expect("(function(){var {x,y=2}={x:1};return x+y})()", "3");
    expect("(function(){var [a,...r]=[1,2,3];return r.join(\"\")})()", "23");
    expect("(function(){function f(...a){return a.length}return f(1,2,3)})()", "3");
    expect("(function(){var k=\"z\";var o={[k]:9};return o.z})()", "9");
    expect("(function(){var o={get v(){return 3}};return o.v})()", "3");
    expect("(function(){var o={m(){return 4}};return o.m()})()", "4");
    expect("(function(){var a=1,o={a};return o.a})()", "1");
    expect("Math.max(...[1,5,3])", "5");
    expect("[...[1,2],...[3]].join(\"\")", "123");
    expect("({...{a:1},b:2}).a", "1");

    // --- closures and hoisting ------------------------------------------------
    expect("(function(){var f=[];for(var i=0;i<3;i++)f.push(function(){return i});"
           "return f[0]()})()",
           "3"); // `var` shares ONE binding - correct, and the classic gotcha
    expect(
        "(function(){let a=[];for(let i of [0,1,2]){a.push(()=>i)}return a[0]()+\",\"+a[1]()})()",
        "0,1"); // for-of DOES bind per iteration
    expect("(function(){return typeof h})()", "undefined");
    expect("(function(){function h(){}return typeof h})()", "function"); // hoisted
    expect("(function(){var o={v:1,get(){return this.v}};return o.get()})()", "1");

    // --- the module wrapper shapes bundles are built from --------------------
    expect("(function(g,f){return f(g)})(this||{},function(g){return \"umd\"})", "umd");
    expect("(function(){var m={exports:{}};(function(e){e.v=1})(m.exports);return m.exports.v})()",
           "1");

    // --- KNOWN WRONG, pinned so a fix trips over them -------------------------
    //
    // `for (let i = ...)` MUST create a fresh binding per iteration, so the
    // closures capture 0, 1 and 2. This engine shares one binding, exactly as
    // `var` does, so all three capture the final value. `for (let x of ...)`
    // above is correct, so it is specifically the C-style loop. Modern minified
    // code relies on this constantly, and getting it wrong is silent.
    expect("(function(){var f=[];for(let i=0;i<3;i++){f.push(function(){return i})}"
           "return f[0]()+\",\"+f[1]()+\",\"+f[2]()})()",
           "3,3,3"); // V8: 0,1,2
    //
    // The lexer does not tokenise these literal forms. Note that
    // `src/script/compile.cpp`'s `radix_of` ALREADY handles 0o and 0b - the gap
    // is in ctjs's lexer (external/compile-time-javascript), which never emits
    // the token, so the fix belongs in the submodule.
    expect("0o17", "THREW");  // V8: 15
    expect("0b101", "THREW"); // V8: 5
    expect("1_000", "THREW"); // V8: 1000 - numeric separators, ES2021
    //
    // Legacy octal escapes and literals, Annex B sloppy mode.
    expect("\"\\101\"", "101"); // V8: "A"
    expect("017", "17");        // V8: 15

    REPORT("obfuscated");
}
