#pragma once

#include <string_view>

namespace ctcompile::test::oracle {

inline constexpr std::string_view oracle_probe = R"JS(
function ident(x) { return x; }
function two(a, b) { return a + b; }
function never_called(q) { return q * 2; }

// AN INSTRUCTION THAT THROWS BEFORE IT WRITES. `o.missing` is undefined and
// CALLING it is a TypeError, so `op::call_method`'s destination register is
// never written - it still holds whatever the frame started with. A recorder
// that flushes a def without checking that the def actually happened observes
// that stale slot and reports a type the program never put there.
//
// IT IS THE CALL AND NOT THE PROPERTY READ, which the first version of this
// fixture got wrong: `o.missing.deeper` on this engine answers undefined rather
// than raising, so the whole probe ran without ever throwing and the guard it
// was written to exercise was never reached. Both spellings are a TypeError in
// V8; only one of them is here.
function throws_before_writing(o) {
    try { return o.missing(); } catch (e) { return 0; }
}

// AND THE SAME THING ACROSS A CALL, which is the case that actually reaches the
// guard. `op::call` writes its result into a register of the CALLER, so the
// caller's pending def is armed while the callee runs; when the callee throws,
// the caller resumes at its handler instead of at the instruction after the
// call, and the register the call would have written was never written.
function throwing_callee(o) { return o.missing(); }
function catching_caller(o) {
    try { return throwing_callee(o); } catch (e) { return 0; }
}

ident(1);
ident("s");
ident(null);
ident(undefined);
ident(true);
ident(-0);
ident(0/0);
ident(1/0);
ident(3000000000);
ident(0.5);

two(1, 2);
two(1.5, 2);

throws_before_writing({});
catching_caller({});
)JS";

inline constexpr std::string_view escape_probe = R"JS(
var G = null;
var H = [];
var T = null;

// CONFINED: made, used, dropped.
function mk() { var o = {x: 1}; return o.x; }
function arr() { var a = [1, 2, 3]; var s = 0; for (var i = 0; i < a.length; i++) { s += a[i]; } return s; }

// ESCAPED, one route each.
function ret() { return {}; }                       // temporaries: the in-flight return value
function glob() { G = {}; }                         // globals
function hold(x) { H.push(x); }
function retain() { var o = {}; hold(o); }          // globals, through a callee that retained it

// THE BLIND SPOT, PINNED. `ident` receives the object and drops it before
// `transit` returns, so the oracle sees it confined - while the analysis says
// escapes:passed, and is right to for a by-value lowering. Retention at frame
// exit is not transit.
function ident(x) { return x; }
function transit() { var o = {}; ident(o); return 1; }

// THROWN out of its frame and caught by the caller, which stores it.
function thrower() { throw {e: 1}; }
function catcher() { try { thrower(); } catch (e) { T = e; } }

// A closure and the cell it captures: all three leave with the closure...
function counter() { var n = {v: 0}; return function () { return n.v++; }; }
// ...and all three stay when the closure is only ever called locally.
function local() { var n = {v: 0}; var f = function () { return n.v++; }; f(); return f(); }

// `arguments` is an array the prologue builds; it dies with the frame too.
function args() { var o = {}; return arguments.length; }

// A cycle that stays, and the same cycle returned.
function cyc() { var a = {}; var b = {}; a.b = b; b.a = a; return 0; }
function leak() { var a = {}; var b = {}; a.b = b; b.a = a; return a; }

// Entered twice: made 2. Never entered: no observation at all.
function twice() { var o = {}; return 0; }
function never_called() { var o = {}; return o; }

// THE BOUNDED-WALK ROW. The caller holds `kept` in its own register window
// across the call; the callee stores its object INTO it. At the callee's
// return the caller's window lies BELOW the callee's base, so the walk must
// still reach `kept` and report the callee's object escaped `via registers`.
// A bound that excluded too much would read it confined.
function fills(target) { var t = {tmp: 1}; target.child = t; return 0; }
function holds_across() { var kept = {k: 1}; fills(kept); return kept.child.tmp; }

// AN UNCLAIMED SITE. Object.keys is a native; the array it makes lands on the
// CALL's pc, which no static inventory names - so it is observed (confined),
// never claimed, and reported as such rather than as anything sound.
function unclaimed_site() { var o = {a: 1}; var k = Object.keys(o); return k.length; }

mk(); arr(); ret(); glob(); retain(); transit(); catcher(); counter(); local();
args(1, 2); cyc(); leak(); twice(); twice(); holds_across(); unclaimed_site();
)JS";

} // namespace ctcompile::test::oracle
