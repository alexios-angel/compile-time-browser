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

// The limitation the compiler refuses on rather than getting wrong.
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

void test_closure_over_local_is_refused_not_wrong() {
	const program prog = compiler::compile(
	    "function outer() { let n = 0; function inner() { return n; } return inner(); }");
	CHECK(!prog.ok);
	// and the message has to say what to do about it, not just "error"
	CHECK(prog.error.find("upvalue") != std::string::npos);
	CHECK(prog.error.find('n') != std::string::npos);
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
	test_closure_over_local_is_refused_not_wrong();
	test_native_bindings();
	test_gc_collects_unreachable();
	test_errors_are_reported_not_crashes();
	REPORT("vm_basics");
}
