// Statements: loops, labels, switch, try/catch/finally, and what an exception
// carries. Also the handful of cases that drive the VM from C++ - the
// collector, a native binding, and what an error report says - because each
// needs a `context` of its own rather than a program. Carved out of
// js/vm_basics.cpp on 2026-09-08 - vm_operators.cpp names the family, and
// vm_expect.hpp is the assertion every file in it shares.

#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include "vm_expect.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

using namespace ctbrowser::script;

namespace {

void test_errors() {
    expect_result("const e = new Error('boom'); return e.message;", "boom");
    expect_result("const e = new TypeError('t'); return e.name;", "TypeError");
    expect_result("return new RangeError('r') instanceof Error;", "true");
    expect_result(
        "try { throw new TypeError('t'); } catch (e) { return e.name + ':' + e.message; }",
        "TypeError:t");
    // a page's own error type, which is the shape 239 `throw new` in p5 rely on
    expect_result(
        "class MyError extends Error { constructor(m) { super(m); this.name = 'MyError'; } } "
        "try { throw new MyError('x'); } catch (e) { return e.name + '/' + e.message + '/' + "
        "(e instanceof Error); }",
        "MyError/x/true");
    expect_result("return new Error('z').toString();", "Error: z");
}

void test_variables_and_control_flow() {
    expect_result("let x = 5; return x;", "5");
    expect_result("let x = 1; x = x + 41; return x;", "42");
    expect_result("let x = 0; if (1 < 2) { x = 10; } else { x = 20; } return x;", "10");
    expect_result("let x = 0; if (1 > 2) { x = 10; } else { x = 20; } return x;", "20");
    expect_result("let n = 0; while (n < 5) { n = n + 1; } return n;", "5");
    expect_result("let s = 0; for (let i = 0; i < 5; i = i + 1) { s = s + i; } return s;", "10");
    expect_result("let s = 0; for (let i = 0; i < 10; i++) { s = s + i; } return s;", "45");
    expect_result("return 1 < 2 ? 'yes' : 'no';", "yes");
}

void test_increment_semantics() {
    expect_result("let i = 5; let a = i++; return a;", "5"); // postfix yields the old value
    expect_result("let i = 5; let a = i++; return i;", "6");
    expect_result("let i = 5; let a = ++i; return a;", "6"); // prefix yields the new one
    expect_result("let i = 5; i--; return i;", "4");
}

// A captured cell is reachable only through the closure holding it, so the
// collector has to trace closure upvalues. If it does not, the cell is freed
// while the closure is still live and the closure returns garbage.
void test_gc_traces_captured_cells() {
    context cx;
    const program prog = compiler::compile("function make(v) { return function() { return v; }; }"
                                           "let keep = make(99);"
                                           "for (let i = 0; i < 100; i++) { let junk = make(i); }"
                                           "return 0;");
    const run_result r = cx.run(prog);
    CHECK(r.ok);
    const std::size_t freed = cx.collect();
    CHECK(freed > 0); // the 100 discarded closures and their cells

    // and the surviving closure still works after the sweep
    const program check = compiler::compile("function make(v) { return function() { return v; }; }"
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
    const program prog =
        compiler::compile("let keep = { alive: 1 };"
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
    const std::string out = run_vm("let x = 1; x();", &ok);
    CHECK(!ok);
    // The message NAMES what was called and what it turned out to be, and
    // carries the stack it happened on. "attempted to call a non-function" is
    // true and useless; in a 4.5 MB bundle it is the difference between a
    // diagnostic and a shrug.
    CHECK(out.find("not a function") != std::string::npos);
    CHECK(out.find("`x`") != std::string::npos);
    CHECK(out.find("number") != std::string::npos);
    CHECK(out.find("at <script>") != std::string::npos);

    // A method call names the method; a call on a call names the inner one.
    bool method_ok = true;
    const std::string method_out = run_vm("const o = {}; o.missing();", &method_ok);
    CHECK(method_out.find("`missing`") != std::string::npos);
    bool chain_ok = true;
    const std::string chain_out = run_vm("function f() { return undefined; } f()();", &chain_ok);
    CHECK(chain_out.find("`f()`") != std::string::npos);

    const program bad = compiler::compile("let x = ;");
    CHECK(!bad.ok);
}

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

void test_break_and_continue() {
    // Without these no loop could exit early: every search loop, every guard,
    // every early-out in a game loop.
    expect_result("var t = 0; for (var i = 0; i < 10; i++) { if (i == 5) { break; } t += 1; }"
                  "return t;",
                  "5");
    expect_result("var t = 0; for (var i = 0; i < 5; i++) { if (i == 2) { continue; } t += 1; }"
                  "return t;",
                  "4");
    // The subtle one: `continue` in a for-loop must run the UPDATE. If it jumped
    // back to the condition instead, this would never terminate.
    expect_result("var n = 0; for (var i = 0; i < 4; i++) { if (i < 2) { continue; } n += 1; }"
                  "return n;",
                  "2");
    expect_result("var t = 0; var i = 0; while (i < 10) { i += 1; if (i > 3) { break; } t += 1; }"
                  "return t;",
                  "3");
    expect_result("var t = 0; var i = 0; do { i += 1; t += i; } while (i < 3); return t;", "6");
}

void test_labeled_break() {
    // Without the label, `break` leaves only the inner loop - so this would be
    // 3, one break per outer iteration, rather than 1.
    expect_result("var n = 0;"
                  "outer: for (var i = 0; i < 3; i++) {"
                  "  for (var j = 0; j < 3; j++) { n += 1; if (j == 0) { break outer; } }"
                  "} return n;",
                  "1");
    expect_result("var n = 0;"
                  "outer: for (var i = 0; i < 3; i++) {"
                  "  for (var j = 0; j < 3; j++) { if (j == 0) { continue outer; } n += 1; }"
                  "} return n;",
                  "0");
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
                  "var r = 0; try { middle(); } catch (e) { r = e; } return r;",
                  "42");
    expect_result("function f() { try { throw 1; } catch (e) { return 5; } return 9; }"
                  "return f();",
                  "5");
}

// `finally` RUNS ON EVERY WAY OUT, and the two cases below were the only two it
// used to get right. The block was emitted twice - once after the try body and
// once after the catch - and every other exit left without reaching it. Six of
// these nine were wrong until 2026-08-21, and the worst LOST AN EXCEPTION:
// `try { throw x } finally { }` sent the throw to a catch path that had no catch
// to run, ran the finally, and fell through with x discarded and no error.
void test_finally() {
    expect_result("var r = 0; try { r = 1; } finally { r += 10; } return r;", "11");
    expect_result("var r = 0; try { throw 1; } catch (e) { r = 2; } finally { r += 10; }"
                  "return r;",
                  "12");

    // A FINALLY WITH NO CATCH MUST RETHROW. This one returned 0.
    expect_result("var r = 0;"
                  "try { try { throw 7; } finally { r += 1; } } catch (e) { r += e; }"
                  "return r;",
                  "8");

    // RETURN RUNS THE FINALLY ON ITS WAY OUT. `return` emitted op::ret straight
    // away and never saw the block.
    expect_result("var log = '';"
                  "function g() { try { return 'T'; } finally { log += 'f'; } }"
                  "var got = g(); return log + got;",
                  "fT");

    // AND THE FINALLY CAN REPLACE IT. This returned 'T'.
    expect_result("function g() { try { return 'T'; } finally { return 'F'; } } return g();", "F");

    // BREAK AND CONTINUE ARE EXITS TOO. Both jumped past the block.
    expect_result("var log = '';"
                  "for (;;) { try { break; } finally { log += 'f'; } }"
                  "return log + 'done';",
                  "fdone");
    expect_result("var log = '';"
                  "for (var i = 0; i < 2; i++) { try { continue; } finally { log += 'f'; } }"
                  "return log + 'done';",
                  "ffdone");

    // A THROW FROM A CATCH STILL RUNS THAT TRY'S FINALLY. It did not, because
    // unwinding into the catch had already consumed this try's handler.
    expect_result("var log = '';"
                  "try { try { throw 1; } catch (e) { throw 2; } finally { log += 'f'; } }"
                  "catch (e) { log += 'outer' + e; } return log;",
                  "fouter2");

    // NESTING: a return crossing two finallys runs both, innermost first. The
    // dispatch re-emits the return, which the next open finally then catches.
    expect_result("var log = '';"
                  "function g() { try { try { return 'R'; } finally { log += 'i'; } }"
                  " finally { log += 'o'; } }"
                  "var got = g(); return log + got;",
                  "ioR");

    // AND A BREAK CROSSING TWO OF THEM.
    expect_result(
        "var log = '';"
        "for (;;) { try { try { break; } finally { log += 'i'; } } finally { log += 'o'; } }"
        "return log + 'done';",
        "iodone");

    // A LOOP INSIDE THE TRY IS NOT CROSSED BY THE FINALLY. `break` here leaves
    // the loop and stays in the try, so it must NOT be routed through the
    // completion record - doing so made the dispatch re-emit a jump to a loop
    // that had already been popped, which asan reported as a leaked patch list
    // in the compiler and which the default preset could not see at all.
    expect_result("var log = '';"
                  "try { for (;;) { log += 'b'; break; } log += 't'; } finally { log += 'f'; }"
                  "return log;",
                  "btf");
    // And the same shape with the loop labelled, which takes the other branch
    // of loop_for.
    expect_result("var log = '';"
                  "try { outer: for (;;) { log += 'b'; break outer; } log += 't'; }"
                  " finally { log += 'f'; } return log;",
                  "btf");
    // A break that DOES cross it still does, with the loop outside the try.
    expect_result("var log = '';"
                  "for (;;) { try { log += 'b'; break; } finally { log += 'f'; } }"
                  "return log + 'done';",
                  "bfdone");

    // An async function's return is a promise, and the finally must not lose
    // the wrapping - the dispatch re-applies op::wrap_promise because it IS the
    // function's return.
    expect_result("var log = '';"
                  "async function g() { try { return 5; } finally { log += 'f'; } }"
                  "var p = g(); return log + (typeof p.then);",
                  "ffunction");
}

void test_nested_try() {
    expect_result("var r = 0;"
                  "try { try { throw 1; } catch (e) { r = 1; throw 2; } } catch (e) { r += e; }"
                  "return r;",
                  "3");
}

void test_break_out_of_try() {
    // Jumping out of a try block has to drop its handler. If it does not, the
    // catch stays reachable after the loop and a later throw lands in dead
    // code - a crash, not a wrong answer.
    expect_result("var n = 0;"
                  "for (var i = 0; i < 3; i++) { try { n += 1; break; } catch (e) { n = 99; } }"
                  "var caught = 0; try { throw 5; } catch (e) { caught = e; } return caught;",
                  "5");
}

void test_for_of() {
    expect_result("var t = 0; for (const x of [1,2,3]) { t += x; } return t;", "6");
    expect_result("var s = ''; for (const c of 'abc') { s += c; } return s;", "abc");
    expect_result("var t = 0; for (const x of [1,2,3]) { if (x == 2) { continue; } t += x; }"
                  "return t;",
                  "4");
    expect_result("var t = 0; for (const x of [1,2,3]) { if (x == 2) { break; } t += x; }"
                  "return t;",
                  "1");
    // The loop variable is per-iteration, so a closure made in the body sees
    // THIS element and not the last one.
    expect_result("var fns = []; for (const x of [1,2,3]) { fns.push(function () { return x; }); }"
                  "return fns[0]() + fns[2]();",
                  "4");
}

void test_for_in() {
    expect_result("var keys = ''; for (const k in {a: 1, b: 2}) { keys += k; } return keys;", "ab");
    expect_result("var t = 0; var o = {a: 1, b: 2}; for (const k in o) { t += o[k]; } return t;",
                  "3");
}

void test_switch() {
    expect_result("var r = 0; switch (2) { case 1: r = 10; break; case 2: r = 20; break; }"
                  "return r;",
                  "20");
    expect_result("var r = 0; switch (9) { case 1: r = 10; break; default: r = 99; } return r;",
                  "99");
    // FALLTHROUGH is the behaviour code relies on, so it has to be preserved.
    expect_result("var r = ''; switch (1) { case 1: r += 'a'; case 2: r += 'b'; break;"
                  "  case 3: r += 'c'; } return r;",
                  "ab");
    // switch matches STRICTLY - `switch (1)` does not match `case '1'`.
    expect_result("var r = 'no'; switch (1) { case '1': r = 'yes'; break; } return r;", "no");
}

// `new Error().stack` CARRIES THE FRAMES.
//
// It said "TypeError: whatever" and stopped there - which reads as a stack to
// code that only prints it, and is useless to code that wants to know WHERE.
// Reporting the site of an error is the normal reason to look at one; p5's
// Friendly Error System is exactly that, and so is any page logging what it
// caught. The VM already built a trace when it raised a fault; a constructed
// error simply never asked for one.
void test_error_stacks() {
    expect_result("function inner() { return new Error('boom'); }"
                  "function outer() { return inner(); }"
                  "return outer().stack.indexOf('Error: boom') === 0;",
                  "true");
    // The frame that WROTE `new Error` is named first - a native pushes no
    // frame of its own, so the top of the stack is already the right one.
    expect_result("function inner() { return new Error('boom'); }"
                  "function outer() { return inner(); }"
                  "const s = outer().stack;"
                  "return (s.indexOf('at inner') > 0) + ',' + (s.indexOf('at outer') > 0);",
                  "true,true");
    expect_result("return new TypeError('t').stack.indexOf('TypeError: t') === 0;", "true");
    // No message is the bare name, not "Error: undefined".
    expect_result("return new Error().stack.indexOf('Error') === 0;", "true");
    expect_result("return new Error().stack.indexOf('undefined') < 0;", "true");
    // ...and an error the VM itself threw is no different, so a page can report
    // where a TypeError came from whether it threw it or the engine did.
    expect_result("try { undefined(); } catch (e) { return e.stack.indexOf('at ') > 0; }"
                  "return false;",
                  "true");
}

// CALLING A NON-FUNCTION IS A CATCHABLE TypeError.
//
// It used to end the run outright. Pages catch it - feature detection is
// written as `try { thing() } catch (e) {}` at least as often as a typeof test -
// and an uncatchable fault also unwinds nothing, so a probe wrapped in
// try/catch reported no error and the failure appeared to come from wherever
// the run happened to stop.
void test_calling_a_non_function_throws() {
    expect_result("try { undefined(); } catch (e) { return e.name; } return 'not thrown';",
                  "TypeError");
    expect_result("try { ({}).nope(); } catch (e) { return e.name; } return 'not thrown';",
                  "TypeError");
    expect_result("try { new (undefined)(); } catch (e) { return e.name; } return 'not thrown';",
                  "TypeError");
    expect_result("try { undefined(); } catch (e) { return e instanceof Error; } return false;",
                  "true");
    // ...and it names what was called, which is the whole diagnostic.
    expect_result("try { ({}).missing(); } catch (e) {"
                  "  return e.message.indexOf('missing') >= 0; } return false;",
                  "true");
    // The run CONTINUES afterwards, which is what "catchable" means.
    expect_result("let n = 0; try { undefined(); } catch (e) { n = 1; } n += 1; return n;", "2");
}

} // namespace

int main() {
    test_errors();
    test_variables_and_control_flow();
    test_increment_semantics();
    test_gc_traces_captured_cells();
    test_native_bindings();
    test_gc_collects_unreachable();
    test_errors_are_reported_not_crashes();
    test_compound_assignment();
    test_compound_assignment_evaluates_its_target_once();
    test_update_on_properties_and_indices();
    test_break_and_continue();
    test_labeled_break();
    test_try_catch();
    test_exceptions_unwind_call_frames();
    test_finally();
    test_nested_try();
    test_break_out_of_try();
    test_for_of();
    test_for_in();
    test_switch();
    test_error_stacks();
    test_calling_a_non_function_throws();
    REPORT("vm_control_flow");
}
