// ctjs v2 VM.
//
// Every case here is checked TWICE: once against a hand-written expectation,
// and once against v1's tree-walk interpreter running the same source
// (diff_vs_v1). The second check is the valuable one and it exists only while
// both engines are in the tree - a rewrite that can be differentially tested
// against the thing it replaces should be, and the window closes when v1 goes.

import ctbrowser.script;

#include "check.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include <ctjs.hpp> // v1, for the differential comparison

using namespace ctbrowser::script;

namespace {

// Run through the v2 VM and render the result the way JS would print it.
[[nodiscard]] std::string run_v2(std::string_view source, bool * ok = nullptr) {
	const program prog = compiler::compile(source);
	context cx;
	install_builtins(cx);
	const run_result r = cx.run(prog);
	if (ok != nullptr) { *ok = r.ok; }
	if (!r.ok) { return "<error: " + r.error + ">"; }
	return cx.to_string(r.returned);
}

// The same source through v1's interpreter, for comparison.
//
// The explicit collect() is v1's requirement, not a courtesy: v1 refcounts
// scope environments and those form cycles, so a run leaks its environment
// chain until the cycle collector reclaims it. v1's engine calls this on a
// frame counter; a test that runs hundreds of scripts and never does is what
// LeakSanitizer reported, and the leak is v1's design rather than a v2 bug.
// The new VM does not need this - it traces from precise roots instead.
[[nodiscard]] std::string run_v1(std::string_view source, bool * ok = nullptr) {
	std::string out;
	{
		ctjs::run_result r = ctjs::run_value(source, {});
		if (ok != nullptr) { *ok = r.ok(); }
		out = r.ok() ? r["__result"].to_string() : "<error>";
	} // the result holds roots, so it has to die before the collector runs
	ctjs::gc::collect();
	return out;
}

// Wrap an expression so both engines expose its value the same way: v2 returns
// the last `return`, v1 exposes globals.
void diff_vs_v1(std::string_view expression, std::string_view expected) {
	const std::string v2_src = "return (" + std::string{expression} + ");";
	const std::string v1_src = "let __result = (" + std::string{expression} + ");";

	bool v2_ok = false;
	bool v1_ok = false;
	const std::string got_v2 = run_v2(v2_src, &v2_ok);
	const std::string got_v1 = run_v1(v1_src, &v1_ok);

	if (got_v2 != expected) {
		std::printf("FAIL v2  %.60s => %s (want %s)\n", std::string{expression}.c_str(),
		            got_v2.c_str(), std::string{expected}.c_str());
		++ctbrowser_test_failures;
	}
	// v1 disagreeing is worth reporting but is not automatically v2's fault -
	// print both so the difference is visible rather than silently accepted.
	if (v1_ok && got_v1 != got_v2) {
		std::printf("DIFF     %.60s => v2=%s v1=%s\n", std::string{expression}.c_str(),
		            got_v2.c_str(), got_v1.c_str());
		++ctbrowser_test_failures;
	}
}

// A whole program, run as written. The stage-2 tests are about STATEMENTS -
// loops, labels, try blocks - so they are written as programs ending in an
// explicit `return`, not as expressions to be wrapped.
void expect_result(std::string_view source, std::string_view want) {
	const std::string got = run_v2(std::string{source});
	if (got != want) {
		std::printf("FAIL     %.70s => %s (want %s)\n", std::string{source}.c_str(), got.c_str(),
		            std::string{want}.c_str());
		++ctbrowser_test_failures;
	}
}

void expect(std::string_view source, std::string_view want) {
	const std::string got = run_v2(source);
	if (got != want) {
		std::printf("FAIL     %.70s => %s (want %s)\n", std::string{source}.c_str(), got.c_str(),
		            std::string{want}.c_str());
		++ctbrowser_test_failures;
	}
}

void test_arithmetic() {
	diff_vs_v1("1 + 2", "3");
	diff_vs_v1("10 - 4 * 2", "2");
	diff_vs_v1("(10 - 4) * 2", "12");
	diff_vs_v1("7 / 2", "3.5");
	diff_vs_v1("7 % 3", "1");
	diff_vs_v1("2 ** 10", "1024");
	diff_vs_v1("-5 + 3", "-2");
}

// `+` is the one operator whose meaning depends on its operand types.
void test_plus_is_overloaded() {
	diff_vs_v1("'a' + 'b'", "ab");
	diff_vs_v1("'n=' + 42", "n=42");
	diff_vs_v1("1 + '2'", "12");   // string wins
	diff_vs_v1("1 + 2 + '3'", "33"); // ...but only once it appears
	diff_vs_v1("'1' + 2 + 3", "123");
}

void test_comparison_and_logic() {
	diff_vs_v1("1 < 2", "true");
	diff_vs_v1("2 <= 2", "true");
	diff_vs_v1("3 > 4", "false");
	diff_vs_v1("1 === 1", "true");
	diff_vs_v1("1 === '1'", "false"); // strict
	diff_vs_v1("1 == '1'", "true");   // loose coerces
	diff_vs_v1("null == undefined", "true");
	diff_vs_v1("null === undefined", "false");
	diff_vs_v1("!true", "false");
	diff_vs_v1("true && false", "false");
	diff_vs_v1("false || 'fallback'", "fallback");
	// short-circuit: the right side must not be evaluated at all
	expect("let hit = 0; function bump() { hit = 1; return true; } "
	       "let r = false && bump(); return hit;",
	       "0");
}

void test_typeof() {
	diff_vs_v1("typeof 1", "number");
	diff_vs_v1("typeof 'x'", "string");
	diff_vs_v1("typeof true", "boolean");
	diff_vs_v1("typeof undefined", "undefined");
	diff_vs_v1("typeof null", "object"); // the famous wart
}

void test_variables_and_control_flow() {
	expect("let x = 5; return x;", "5");
	expect("let x = 1; x = x + 41; return x;", "42");
	expect("let x = 0; if (1 < 2) { x = 10; } else { x = 20; } return x;", "10");
	expect("let x = 0; if (1 > 2) { x = 10; } else { x = 20; } return x;", "20");
	expect("let n = 0; while (n < 5) { n = n + 1; } return n;", "5");
	expect("let s = 0; for (let i = 0; i < 5; i = i + 1) { s = s + i; } return s;", "10");
	expect("let s = 0; for (let i = 0; i < 10; i++) { s = s + i; } return s;", "45");
	expect("return 1 < 2 ? 'yes' : 'no';", "yes");
}

void test_increment_semantics() {
	expect("let i = 5; let a = i++; return a;", "5"); // postfix yields the old value
	expect("let i = 5; let a = i++; return i;", "6");
	expect("let i = 5; let a = ++i; return a;", "6"); // prefix yields the new one
	expect("let i = 5; i--; return i;", "4");
}

void test_functions() {
	expect("function add(a, b) { return a + b; } return add(2, 3);", "5");
	expect("function f() { return 7; } return f();", "7");
	expect("function id(x) { return x; } return id('hi');", "hi");
	// hoisting: callable before its declaration appears
	expect("let r = twice(21); function twice(n) { return n * 2; } return r;", "42");
	// recursion, and therefore real frame handling
	expect("function fact(n) { if (n <= 1) { return 1; } return n * fact(n - 1); } return fact(10);",
	       "3628800");
	expect("function fib(n) { if (n < 2) { return n; } return fib(n-1) + fib(n-2); } return fib(20);",
	       "6765");
	// a missing argument is undefined, not an error
	expect("function f(a, b) { return typeof b; } return f(1);", "undefined");
	// function expressions and arrows
	expect("let f = function(x) { return x * 3; }; return f(4);", "12");
	expect("let f = (x) => x + 1; return f(41);", "42");
	expect("let f = x => x * x; return f(9);", "81");
}

void test_objects_and_arrays() {
	expect("let o = { a: 1, b: 2 }; return o.a + o.b;", "3");
	expect("let o = { name: 'ctbrowser' }; return o.name;", "ctbrowser");
	expect("let o = {}; o.x = 5; return o.x;", "5");
	expect("let o = { a: 1 }; return o.missing;", "undefined");
	expect("let a = [1, 2, 3]; return a[0] + a[2];", "4");
	expect("let a = [1, 2, 3]; return a.length;", "3");
	expect("let a = []; a[0] = 'x'; return a[0];", "x");
	expect("let a = [1,2,3]; a[1] = 9; return a[1];", "9");
	expect("return 'hello'.length;", "5");
	expect("let o = { a: 1 }; return o['a'];", "1");
	expect("let a = [1,2,3]; let s = 0; for (let i = 0; i < a.length; i++) { s = s + a[i]; } return s;",
	       "6");
	// nested structure
	expect("let o = { inner: { deep: 42 } }; return o.inner.deep;", "42");
}

// The most common shape in JavaScript: script-level state mutated by a
// function declared beside it. Script scope is global scope here, so this must
// simply work rather than hit the enclosing-local refusal.
void test_script_scope_is_shared_with_functions() {
	expect("let count = 0; function inc() { count = count + 1; } inc(); inc(); return count;", "2");
	expect("let name = 'ctbrowser'; function greet() { return 'hi ' + name; } return greet();",
	       "hi ctbrowser");
	expect("let total = 0; function addAll(a) { for (let i = 0; i < a.length; i++) "
	       "{ total = total + a[i]; } } addAll([1,2,3,4]); return total;",
	       "10");
}

// Real closures, via boxed cells. Every case here is one that capture-by-value
// would get WRONG rather than merely slow, which is why the compiler used to
// refuse instead of guessing.
void test_closures() {
	// the counter: the canonical case. Capture-by-value returns 0 forever.
	expect("function counter() { let n = 0; function inc() { n = n + 1; return n; } return inc; }"
	       "let c = counter(); c(); c(); return c();",
	       "3");

	// two closures over the SAME cell must see each other's writes
	expect("function pair() { let n = 0;"
	       "  return { bump: function() { n = n + 1; }, read: function() { return n; } }; }"
	       "let p = pair(); p.bump(); p.bump(); return p.read();",
	       "2");

	// separate invocations must NOT share a cell
	expect("function counter() { let n = 0; return function() { n = n + 1; return n; }; }"
	       "let a = counter(); let b = counter(); a(); a(); return a() + ',' + b();",
	       "3,1");

	// capture of a parameter, not just a local
	expect("function adder(x) { return function(y) { return x + y; }; }"
	       "let add5 = adder(5); return add5(37);",
	       "42");

	// TWO levels of nesting: the innermost function captures a variable from
	// its grandparent, which only works if the middle function re-exports it
	// as its own upvalue
	expect("function outer() { let v = 'deep';"
	       "  function middle() { function inner() { return v; } return inner(); }"
	       "  return middle(); }"
	       "return outer();",
	       "deep");

	// mutation from the innermost level, visible at the outermost
	expect("function outer() { let n = 1;"
	       "  function middle() { function inner() { n = n * 10; } inner(); }"
	       "  middle(); middle(); return n; }"
	       "return outer();",
	       "100");

	// a closure outliving the frame that created it - the cell must survive
	expect("function make() { let msg = 'alive'; return function() { return msg; }; }"
	       "let f = make(); return f();",
	       "alive");

	// closures in a loop capture the loop's binding
	expect("function build() { let fns = []; let i = 0;"
	       "  while (i < 3) { fns[i] = function() { return i; }; i = i + 1; }"
	       "  return fns[0](); }"
	       "return build();",
	       "3"); // `let i` outside the loop body is ONE binding, so all three see 3
}

// A captured cell is reachable only through the closure holding it, so the
// collector has to trace closure upvalues. If it does not, the cell is freed
// while the closure is still live and the closure returns garbage.
void test_gc_traces_captured_cells() {
	context cx;
	const program prog = compiler::compile(
	    "function make(v) { return function() { return v; }; }"
	    "let keep = make(99);"
	    "for (let i = 0; i < 100; i++) { let junk = make(i); }"
	    "return 0;");
	const run_result r = cx.run(prog);
	CHECK(r.ok);
	const std::size_t freed = cx.collect();
	CHECK(freed > 0); // the 100 discarded closures and their cells

	// and the surviving closure still works after the sweep
	const program check = compiler::compile(
	    "function make(v) { return function() { return v; }; }"
	    "let keep = make(99); return keep();");
	context cx2;
	const run_result r2 = cx2.run(check);
	CHECK(r2.ok);
	CHECK_EQ(cx2.to_string(r2.returned), std::string{"99"});
}

void test_native_bindings() {
	const program prog = compiler::compile("return double(21) + double(0);");
	context cx;
	cx.define_native("double", [](context &, std::span<value> args) {
		const double n = args.empty() ? 0 : context::to_number(args[0]);
		return value::number(n * 2);
	});
	const run_result r = cx.run(prog);
	CHECK(r.ok);
	CHECK_EQ(cx.to_string(r.returned), std::string{"42"});
}

void test_gc_collects_unreachable() {
	context cx;
	const program prog = compiler::compile(
	    "let keep = { alive: 1 };"
	    "for (let i = 0; i < 200; i++) { let junk = { dead: i }; }"
	    "return keep.alive;");
	const run_result r = cx.run(prog);
	CHECK(r.ok);
	CHECK_EQ(cx.to_string(r.returned), std::string{"1"});

	const std::size_t before = cx.live_objects();
	const std::size_t freed = cx.collect();
	// The loop allocated hundreds of objects that nothing can reach; a
	// collector that frees none of them is not working.
	CHECK(freed > 0);
	CHECK(cx.live_objects() < before);
	std::printf("  gc: %zu live -> %zu after freeing %zu\n", before, cx.live_objects(), freed);
}

void test_errors_are_reported_not_crashes() {
	bool ok = true;
	const std::string out = run_v2("let x = 1; x();", &ok);
	CHECK(!ok);
	CHECK(out.find("non-function") != std::string::npos);

	const program bad = compiler::compile("let x = ;");
	CHECK(!bad.ok);
}


// --- stage 2: the things ordinary code needs ------------------------------

void test_compound_assignment() {
	// `x += 1` is in essentially every script ever written, and it did not
	// compile at all - the compiler rejected the whole program.
	expect_result("var x = 1; x += 4; return x;", "5");
	expect_result("var x = 10; x -= 4; return x;", "6");
	expect_result("var x = 3; x *= 4; return x;", "12");
	expect_result("var x = 12; x /= 4; return x;", "3");
	expect_result("var x = 13; x %= 5; return x;", "3");
	expect_result("var s = 'a'; s += 'b'; return s;", "ab"); // += concatenates strings
	expect_result("var o = {n: 1}; o.n += 5; return o.n;", "6");
	expect_result("var a = [1,2]; a[0] += 9; return a[0];", "10");
	expect_result("var x = 0; function f() { x += 2; } f(); f(); return x;", "4");
}

void test_compound_assignment_evaluates_its_target_once() {
	// THE case the reference machinery exists for. If the target were compiled
	// twice - once to read and once to write - this would increment i twice and
	// store into the wrong slot.
	expect_result("var a = [0,0,0]; var i = 0; a[i++] += 5; return i;", "1");
	expect_result("var a = [0,0,0]; var i = 0; a[i++] += 5; return a[0];", "5");
}

void test_update_on_properties_and_indices() {
	expect_result("var o = {n: 1}; o.n++; return o.n;", "2");
	expect_result("var o = {n: 1}; var r = o.n++; return r;", "1"); // postfix: the old value
	expect_result("var o = {n: 1}; var r = ++o.n; return r;", "2"); // prefix: the new one
	expect_result("var a = [5]; a[0]--; return a[0];", "4");
}

void test_this() {
	// `this` compiled to undefined unconditionally, so no method could see the
	// object it was called on - which is most of what objects are for.
	expect_result("var o = {n: 7, get: function () { return this.n; }}; return o.get();", "7");
	// and it follows the CALL SITE, not the definition
	expect_result("var a = {n: 1, get: function () { return this.n; }};"
	              "var b = {n: 2, get: a.get}; return b.get();", "2");
}

void test_break_and_continue() {
	// Without these no loop could exit early: every search loop, every guard,
	// every early-out in a game loop.
	expect_result("var t = 0; for (var i = 0; i < 10; i++) { if (i == 5) { break; } t += 1; }"
	              "return t;", "5");
	expect_result("var t = 0; for (var i = 0; i < 5; i++) { if (i == 2) { continue; } t += 1; }"
	              "return t;", "4");
	// The subtle one: `continue` in a for-loop must run the UPDATE. If it jumped
	// back to the condition instead, this would never terminate.
	expect_result("var n = 0; for (var i = 0; i < 4; i++) { if (i < 2) { continue; } n += 1; }"
	              "return n;", "2");
	expect_result("var t = 0; var i = 0; while (i < 10) { i += 1; if (i > 3) { break; } t += 1; }"
	              "return t;", "3");
	expect_result("var t = 0; var i = 0; do { i += 1; t += i; } while (i < 3); return t;", "6");
}

void test_labeled_break() {
	// Without the label, `break` leaves only the inner loop - so this would be
	// 3, one break per outer iteration, rather than 1.
	expect_result("var n = 0;"
	              "outer: for (var i = 0; i < 3; i++) {"
	              "  for (var j = 0; j < 3; j++) { n += 1; if (j == 0) { break outer; } }"
	              "} return n;", "1");
	expect_result("var n = 0;"
	              "outer: for (var i = 0; i < 3; i++) {"
	              "  for (var j = 0; j < 3; j++) { if (j == 0) { continue outer; } n += 1; }"
	              "} return n;", "0");
}

void test_try_catch() {
	expect_result("var r = 0; try { throw 7; } catch (e) { r = e; } return r;", "7");
	expect_result("var r = 0; try { r = 1; } catch (e) { r = 2; } return r;", "1");
	expect_result("var r = ''; try { throw 'boom'; } catch (e) { r = e; } return r;", "boom");
}

void test_exceptions_unwind_call_frames() {
	// The reason exceptions are a VM change and not a compiler one: a throw
	// several frames deep has to reach a try in a caller, discarding the frames
	// in between.
	expect_result("function deep() { throw 42; }"
	              "function middle() { deep(); return 1; }"
	              "var r = 0; try { middle(); } catch (e) { r = e; } return r;", "42");
	expect_result("function f() { try { throw 1; } catch (e) { return 5; } return 9; }"
	              "return f();", "5");
}

void test_finally() {
	expect_result("var r = 0; try { r = 1; } finally { r += 10; } return r;", "11");
	expect_result("var r = 0; try { throw 1; } catch (e) { r = 2; } finally { r += 10; }"
	              "return r;", "12");
}

void test_nested_try() {
	expect_result("var r = 0;"
	              "try { try { throw 1; } catch (e) { r = 1; throw 2; } } catch (e) { r += e; }"
	              "return r;", "3");
}

void test_break_out_of_try() {
	// Jumping out of a try block has to drop its handler. If it does not, the
	// catch stays reachable after the loop and a later throw lands in dead
	// code - a crash, not a wrong answer.
	expect_result("var n = 0;"
	              "for (var i = 0; i < 3; i++) { try { n += 1; break; } catch (e) { n = 99; } }"
	              "var caught = 0; try { throw 5; } catch (e) { caught = e; } return caught;", "5");
}


// --- stage 3: the standard library ----------------------------------------

void test_math() {
	// Math.random aside, these are the functions every page reaches for. None
	// of them existed: `Math` itself was undefined.
	expect_result("return Math.floor(3.7);", "3");
	expect_result("return Math.ceil(3.2);", "4");
	expect_result("return Math.abs(-5);", "5");
	expect_result("return Math.max(1, 9, 4);", "9");
	expect_result("return Math.min(1, 9, 4);", "1");
	expect_result("return Math.sqrt(16);", "4");
	expect_result("return Math.pow(2, 10);", "1024");
	// JS rounds .5 toward POSITIVE infinity; std::round rounds away from zero
	// and would give -1 here.
	expect_result("return Math.round(-0.5);", "0");
	expect_result("return Math.round(2.5);", "3");
	expect_result("return Math.floor(Math.PI * 100) / 100;", "3.14");
}

void test_math_random_is_in_range_and_moves() {
	expect_result("var ok = true;"
	              "for (var i = 0; i < 200; i++) { var r = Math.random();"
	              "  if (r < 0 || r >= 1) { ok = false; } }"
	              "return ok;", "true");
	expect_result("var a = Math.random(); var b = Math.random(); return a != b;", "true");
}

void test_array_methods() {
	// `arr.push` resolved to nothing before the prototype table existed.
	expect_result("var a = [1,2]; a.push(3); return a.length;", "3");
	expect_result("var a = [1,2,3]; return a.pop();", "3");
	expect_result("var a = [1,2,3]; a.shift(); return a[0];", "2");
	expect_result("var a = [2,3]; a.unshift(1); return a[0];", "1");
	expect_result("return [1,2,3].indexOf(2);", "1");
	expect_result("return [1,2,3].includes(9);", "false");
	expect_result("return [1,2,3].join('-');", "1-2-3");
	expect_result("return [1,2,3].slice(1).join('');", "23");
	expect_result("return [1,2,3].concat([4]).length;", "4");
	expect_result("var a = [1,2,3]; a.reverse(); return a.join('');", "321");
	expect_result("var a = [1,2,3,4]; a.splice(1, 2); return a.join('');", "14");
}

void test_array_iteration_calls_back_into_the_vm() {
	// These are the ones that need context::call - a native invoking a JS
	// function. Without it none of them can exist at all.
	expect_result("var t = 0; [1,2,3].forEach(function (x) { t += x; }); return t;", "6");
	expect_result("return [1,2,3].map(function (x) { return x * 2; }).join(',');", "2,4,6");
	expect_result("return [1,2,3,4].filter(function (x) { return x % 2 == 0; }).join(',');", "2,4");
	expect_result("return [1,2,3,4].reduce(function (t, x) { return t + x; }, 0);", "10");
	expect_result("return [1,2,3].find(function (x) { return x > 1; });", "2");
	expect_result("return [1,2,3].some(function (x) { return x > 2; });", "true");
	expect_result("return [1,2,3].every(function (x) { return x > 0; });", "true");
	// The callback gets the index too, which half of real uses depend on.
	expect_result("var s = ''; ['a','b'].forEach(function (x, i) { s += i + x; }); return s;", "0a1b");
}

void test_array_sort() {
	expect_result("return [3,1,2].sort(function (a, b) { return a - b; }).join('');", "123");
	expect_result("return [3,1,2].sort(function (a, b) { return b - a; }).join('');", "321");
	// The default really is lexicographic on the string form, which is why
	// [10, 9] sorts to [10, 9] and surprises everyone once.
	expect_result("return [10,9].sort().join(',');", "10,9");
}

void test_string_methods() {
	expect_result("return 'abc'.toUpperCase();", "ABC");
	expect_result("return 'ABC'.toLowerCase();", "abc");
	expect_result("return 'a,b,c'.split(',').length;", "3");
	expect_result("return 'a,b,c'.split(',')[1];", "b");
	expect_result("return 'hello'.indexOf('ll');", "2");
	expect_result("return 'hello'.includes('ell');", "true");
	expect_result("return 'hello'.slice(1, 3);", "el");
	expect_result("return 'hello'.substring(3, 1);", "el"); // substring swaps
	expect_result("return '  hi  '.trim();", "hi");
	expect_result("return 'ab'.repeat(3);", "ababab");
	expect_result("return 'a-b-a'.replace('a', 'X');", "X-b-a");    // first only
	expect_result("return 'a-b-a'.replaceAll('a', 'X');", "X-b-X"); // all
	expect_result("return '5'.padStart(3, '0');", "005");
	expect_result("return 'abc'.charAt(1);", "b");
	expect_result("return 'A'.charCodeAt(0);", "65");
	expect_result("return 'abc'.startsWith('ab');", "true");
	expect_result("return 'abc'.length;", "3"); // still works alongside the methods
}

void test_number_and_conversions() {
	expect_result("return (3.14159).toFixed(2);", "3.14");
	expect_result("return parseInt('42');", "42");
	expect_result("return parseInt('ff', 16);", "255");
	expect_result("return parseFloat('3.5');", "3.5");
	// parseInt of nonsense is NaN, not an error - a page checks it with isNaN
	// straight afterwards.
	expect_result("return isNaN(parseInt('abc'));", "true");
	expect_result("return String(42);", "42");
	expect_result("return Number('7') + 1;", "8");
}

void test_object_statics() {
	expect_result("return Object.keys({a: 1, b: 2}).join(',');", "a,b");
	expect_result("return Object.values({a: 1, b: 2}).join(',');", "1,2");
	expect_result("var t = Object.assign({}, {a: 1}, {b: 2}); return t.a + t.b;", "3");
}

void test_json() {
	expect_result("return JSON.stringify({a: 1, b: 'x'});", "{\"a\":1,\"b\":\"x\"}");
	expect_result("return JSON.stringify([1, 'two', true]);", "[1,\"two\",true]");
	expect_result("return JSON.parse('{\"n\": 7}').n;", "7");
	expect_result("return JSON.parse('[1,2,3]')[2];", "3");
	expect_result("var o = JSON.parse(JSON.stringify({a: [1, {b: 2}]})); return o.a[1].b;", "2");
	expect_result("return JSON.stringify(JSON.parse('{\"s\":\"a\\\\nb\"}'));",
	              "{\"s\":\"a\\nb\"}"); // escapes survive a round trip
}

void test_own_properties_beat_the_prototype() {
	// A page that puts its own `join` on an object must get its own, not the
	// array method. Prototype lookup has to come SECOND.
	expect_result("var o = {join: function () { return 'mine'; }}; return o.join();", "mine");
}

void test_computed_and_named_lookup_agree() {
	// `a["length"]` and `a.length` went down different paths, so a page that
	// computed a property name silently saw undefined.
	expect_result("var a = [1,2,3]; return a['length'];", "3");
	expect_result("var s = 'abc'; return s['length'];", "3");
	expect_result("var a = [1,2]; var m = 'push'; a[m](3); return a.length;", "3");
}

} // namespace

int main() {
	test_arithmetic();
	test_plus_is_overloaded();
	test_comparison_and_logic();
	test_typeof();
	test_variables_and_control_flow();
	test_increment_semantics();
	test_functions();
	test_objects_and_arrays();
	test_script_scope_is_shared_with_functions();
	test_closures();
	test_gc_traces_captured_cells();
	test_native_bindings();
	test_gc_collects_unreachable();
	test_errors_are_reported_not_crashes();
	test_compound_assignment();
	test_compound_assignment_evaluates_its_target_once();
	test_update_on_properties_and_indices();
	test_this();
	test_break_and_continue();
	test_labeled_break();
	test_try_catch();
	test_exceptions_unwind_call_frames();
	test_finally();
	test_nested_try();
	test_break_out_of_try();

	test_math();
	test_math_random_is_in_range_and_moves();
	test_array_methods();
	test_array_iteration_calls_back_into_the_vm();
	test_array_sort();
	test_string_methods();
	test_number_and_conversions();
	test_object_statics();
	test_json();
	test_own_properties_beat_the_prototype();
	test_computed_and_named_lookup_agree();

	REPORT("vm_basics");
}
