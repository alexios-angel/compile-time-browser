// A DIRECT EVAL SEES ITS CALLER'S BINDINGS (19.2.1.1 PerformEval, direct).
//
// `eval(src)` written in a function runs `src` in that function's scope: its
// locals, the enclosing functions' and `arguments` are read and written
// through the same cells the function uses (compiler_impl::compile_direct_eval,
// direct_eval_name). Before, every eval was indirect and a local was a
// ReferenceError - css/mediaqueries/test_media_queries.html evaluates a
// template of a loop variable that way 176 times.

#include "js_expect.hpp"

int main() {
    // Reads, at every depth, and the template shape the WPT file uses.
    js_expect("(function(){ var v = 7; return eval('v + 1'); })()", "8");
    js_expect("(function(){ var a = 1; return (function(){ var b = 2;"
              " return eval('a + b'); })(); })()",
              "3");
    js_expect("(function(){ var value = 3; return eval('`${value}px`'); })()", "3px");
    js_expect("(function(){ let x = 'block'; { let x = 'inner'; return eval('x'); } })()", "inner");
    // Writes land in the caller's binding.
    js_expect("(function(){ var n = 1; eval('n = n + 41'); return n; })()", "42");
    js_expect("(function(){ var n = 1; (function(){ eval('n = 5'); })(); return n; })()", "5");
    // A `var` of a name the caller binds is that binding; `var n;` alone keeps it.
    js_expect("(function(){ var n = 1; eval('var n = 9'); return n; })()", "9");
    js_expect("(function(){ var n = 1; eval('var n;'); return n; })()", "1");
    // `this` and `arguments` are the caller's.
    js_expect("(function(){ return eval('this.tag'); }).call({tag: 'me'})", "me");
    js_expect("(function(){ return eval('arguments.length'); })(1, 2, 3)", "3");
    // The completion value, and an eval that is not %eval% is a plain call.
    js_expect("(function(){ var k = 2; return eval('if (k) { k * 10 } else { 0 }'); })()", "20");
    js_expect("(function(){ var eval = function (s) { return 'mine:' + s; };"
              " return eval('x'); })()",
              "mine:x");
    // A closure made inside the eval keeps the caller's cell.
    js_expect("(function(){ var c = 0; var inc = eval('(function(){ return ++c; })');"
              " inc(); inc(); return c; })()",
              "2");
    REPORT("direct_eval");
}
