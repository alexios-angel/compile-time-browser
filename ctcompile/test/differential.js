// THE BODIES THE DIFFERENTIAL TEST COMPILES.
//
// Each is compiled by the build through the real pipeline AND run by the
// interpreter, and the two answers must agree. The interpreter is the
// definition of correct here, which the dialect's own policy states outright:
// "when a CTJS operation and the ctbrowser VM disagree, the VM is correct by
// definition."
//
// THIS FILE MUST STAY TEXTUALLY IDENTICAL to the fixture string in
// Differential.cpp - that is what makes the two tiers compile the same
// function_proto, and the driver checks the function count as a cheap guard
// against them drifting.
//
// EACH BODY IS CHOSEN FOR AN ANSWER THAT DIFFERS IF THE LOWERING IS WRONG in a
// specific way, rather than for coverage. A test that returns the same thing
// whether or not the compiler is right is worse than no test.

// `+` IS THE RE-ENTERING FAMILY. If it were lowered to op::add - the static
// one, which `++` uses - an object with a valueOf would answer NaN instead of
// running it.
function plus(a, b) { return a + b; }

// THE FOUR RELATIONAL OPERATORS ARE NOT NEGATIONS OF ONE ANOTHER, because
// ct_aot_compare can answer UNORDERED and that makes all four false. Lowering
// `>=` as `!(<)` makes `NaN >= NaN` true, which this catches.
function ge(a, b) { return a >= b; }

// STRICT AND LOOSE EQUALITY REACH DIFFERENT HELPERS with different effect
// profiles - one cannot throw at all, the other converts.
// Two functions rather than one returning both: an array literal needs
// ctjs.append, whose helper is one of the rows aot_bridge.cpp does not define.
function strict(a, b) { return a === b; }
function loose(a, b) { return a == b; }

// CONTROL FLOW, whose block arguments are the shape the C++ emitter miscompiles
// unless they are lowered to variables first.
function pick(a, b) { if (a < b) { return a; } return b; }

// GLOBALS, which are a read and a write with no status and no edge.
function globals(a) { DIFF_W = a; return DIFF_R; }

// A CALL, the only operation needing a contiguous run of frame slots.
function apply(k, a, b) { return k(a, b); }

// A CAPTURED BINDING. `counter` itself is NOT compiled - it contains the
// `closure` opcode, which the importer has no operation for - so the
// interpreter builds the closure and the compiled body is the INNER function.
// That split is what makes this testable before closures can be created in
// compiled code.
//
// IT SEPARATES A CAPTURED CELL FROM A COPIED VALUE: a copy answers the same
// thing every time. Before the closure reached the compiled frame it answered
// `undefined` - the write landed on a non-cell and was dropped, and the read
// found no closure - against the interpreter's 12.
function counter(start) { var n = start; return function step() { n = n + 1; return n; }; }

// TWO CLOSURES OVER ONE FUNCTION SEPARATE THE INSTANCE FROM THE PROTO, which is
// the mistake the entry ABI invites: `site` is the function_proto and every
// closure over `tally` shares it. A body that took its upvalues from there
// would have the two counters share a binding, and the second would continue
// the first's count instead of starting again.

// TWO LEVELS OF NESTING SEPARATE THE TWO DESCRIPTOR ARMS. When `middle` builds
// `inner`, `y` is a from_parent_local - a cell in middle's own register - while
// `x` is NOT: it comes from middle's own closure, which is the arm that carries
// a capture down through more than one level. A build that read both from the
// registers, or both from the enclosing closure, gets one of them wrong.
function outer(a) { var x = a; return function middle(b) { var y = b; return function inner() { return x + y; }; }; }

// AN ARROW DECIDES ITS `this` WHERE IT IS WRITTEN, and this is the only shape
// that separates that from the raw receiver an entry is handed.
//
// The compiled body's argument 0 comes from ct_aot_this - which is
// effective_this, the enclosing method's object when the frame's closure is an
// arrow - rather than from the entry ABI's `receiver`. For every ordinary
// function the two are equal, so nothing else in this file can tell them apart.
// Here they differ: `seen()` is called with no receiver at all, so the raw one
// is undefined and the captured one is whatever `methodish` was called on.
function methodish(ignored) { var seen = () => this; return seen(); }

// UNARY MINUS AND BITWISE NOT, WHOSE TWO ARMS ARE NOT ONE OPERATION.
//
// The shape invites lowering `-x` as ToNumber plus a negation, and that is
// wrong for a BigInt: it is negated as an unbounded integer and ALLOCATES, so
// `-0n` is `0n` - a BigInt has one zero - and to_number_value's own "Cannot
// convert a BigInt value to a number" TypeError would fire instead. `+1n`
// throws; `-1n` does not.
//
// `~1n` is `-2n` for a related reason: there is no ToInt32 step, because a
// BigInt has no width to truncate to.
function neg(a) { return -a; }
function bnot(a) { return ~a; }

// A PROPERTY WRITE, WHOSE SLOW PATH IS NOT "THE NON-ARRAY CASE". The fast path
// is array AND number, so `a['0'] = ...` on an ARRAY takes the named path
// through store_property and does NOT land in items[0] - which is why reading
// it back as a[0] gives undefined.
// IT READS BACK BOTH KEYS, because reading only o[0] does not separate the two
// paths: a named write leaves items[0] alone, and a fast-path write with a
// nonsense index is DROPPED and leaves it alone too. Both answer 1.
// THE STRING KEY ARRIVES AS AN ARGUMENT, not a literal: a string constant
// reaches ct_aot_new_string, which allocates and is one of the rows with no
// body yet, so a literal here would make the whole function unlowerable.
//
// The answer packs both reads into one number for the same reason - `"" + x`
// would need a string constant too. `| 0` turns a missing property into 0.
function put(o, k, v, s) { o[k] = v; return o[0] * 100 + (o[s] | 0); }

// A STRING LITERAL, AND ITS MEMO.
//
// ct_aot_new_string caches by (site, slot) and that memo is part of the ABI
// rather than an optimisation: `allocations_` counts TOTAL allocations for the
// process lifetime and is never reset, so a literal in a loop that allocated
// per iteration would reach the 40,000,000 ceiling and raise UNCATCHABLY on a
// program the interpreter runs forever.
//
// SHARING ONE OBJECT IS SAFE because string identity is unobservable - strict
// equality compares TEXT - which is also why this case cannot check the memo by
// asking whether two reads are the same object. What it can check is that the
// literal is still the right text after a loop, and that concatenating it works,
// which is what a broken length or a truncating escape would break.
function greet(n) { var out = ""; for (var i = 0; i < n; i++) { out = out + "ab"; } return out + "!"; }

// OBJECT AND ARRAY LITERALS. Both allocate and are RAISE TIER ONLY - allocate()
// raises past the ceiling and still returns a well-formed object - so neither
// has a status to test, only a poll to schedule.
//
// The array is built the way the bytecode builds one: new_array, then one
// append per element.
function pack(a, b) { var o = {}; o[a] = b; var arr = [a, b, a]; return arr[0] * 100 + arr[2] * 10 + (o[a] | 0); }

// `typeof`, WHICH IS TWO CALLS AND NOT MEMOISED. ct_aot_type_of_name answers
// with a LENGTH and a pointer to static storage; ct_aot_new_string turns those
// into a value, with a NULL site meaning do not memoise - because
// VM_CASE(type_of) has no cache and memoising would allocate FEWER times than
// the interpreter, which is a divergence in the raise tier.
function kindOf(a) { return typeof a; }

// A THROW, CAUGHT BY AN INTERPRETED CALLER.
//
// The catch has to be OUTSIDE the compiled body, and not for convenience:
// ctjs.push_handler has no lowering, so a function containing a `try` is
// refused whole and would never run compiled at all. That is also why
// CT_AOT_CAUGHT cannot reach the failure path - the winning handler is never
// this frame's.
//
// ct_aot_throw NEVER RETURNS ok. The throw completes INSIDE the callee: by the
// time it returns there is no exception in flight, only a frame stack shorter
// than it was. So the compiled body tests nothing and branches straight to the
// epilogue, which must NOT call ct_aot_leave when the status is unwound - the
// frame is already gone.
function thrower(a) { throw a; }

// --- the harness the driver calls -------------------------------------------
//
// It lives here rather than in the C++ so that the file the backend compiles
// and the file the interpreter runs are THE SAME FILE. The functions above are
// what is compiled; these are what drives them.
var DIFF_R = 0, DIFF_W = 0, OUT = "";

// THE ARGUMENTS ARE BUILT HERE rather than passed from C++, so the harness
// holds no JavaScript value in a C++ local of its own.
var counter2 = { valueOf: function () { return 3; } };
var nan = 0 / 0;
var nothing;
var undef;
var big = 9007199254740993n;
var zeroBig = 0n;

function counters(a, b) { return counter(a)() + counter(b)(); }

// `new`, AND ITS TWO ANSWERS THAT ARE NOT THE BODY'S.
//
// Point RETURNS A PRIMITIVE, which `new` throws away: the value of
// `new Point(3)` is the fresh instance, not 7. Lowering ctjs.construct as an
// ordinary call - the plausible mistake, since ct_aot_call takes the same
// contiguous window - gives 7 instead, and then `p.twice` is undefined and the
// arm throws rather than answering wrongly.
//
// AND THE INSTANCE HAS TO BE WIRED TO Point.prototype, which is
// make_instance's half of the work rather than the body's: `twice` is on the
// prototype and nothing in Point mentions it.
function Point(x) { this.x = x; return 7; }
Point.prototype.twice = function () { return this.x * 2; };

function build(a) { var p = new Point(a); return p.twice() + p.x; }

// A `new` ON SOMETHING THAT IS NOT A CONSTRUCTOR, which is the only case that
// reads ct_aot_construct's `site`. Every other operand is a value the body
// already had; `site` exists solely to name the ENCLOSING function in the
// TypeError, so if the lowering passes the memo marker instead of the entry's
// own site - two fields of one struct, one letter apart - this is what says so.
//
// THE try IS IN drive AND NOT HERE on purpose: push_handler has no CTJS
// operation, so a catch in this function would make the backend refuse it and
// the case would test the interpreter against itself.
function newBad(f) { return new f(); }

// `??`, `?.` AND A DEFAULT PARAMETER - the three shapes that compile to the two
// conditional jumps the importer's CFG classifier had never heard of.
//
// NOT TRUTHINESS, which is the whole point and what makes these arguments the
// ones to pass. `0 ?? 9` is 0 and `"" ?? 9` is "", because both are DEFINED;
// lowering either jump through ctjs.truthy answers 9 for each and is exactly
// the bug optional chaining exists to avoid. A default parameter separates the
// third: `dflt(0)` is 0 and `dflt()` is 5.
function coalesce(a, b) { return a ?? b; }
function chain(o) { return o?.p; }
function dflt(a) { if (a === undefined) { a = 5; } return a; }

// for-of AND SPREAD, which are the same opcode: both compile to op::iterable,
// whose helper answers with a plain ARRAY that the loop then indexes. There is
// no Symbol.iterator dispatch in this runtime.
//
// `chars` PREPENDS rather than appends, so the answer records the ORDER the
// values came out in: a drain that reverses gives "abc" where "cba" is right,
// and a sum could not tell. `total(7)` is the non-object arm - iterable_values
// yields nothing for a number, so the loop runs zero times and the answer is 0
// rather than a throw, which the row calls out as deliberate.
// THE LEADING `pad` IS LOAD-BEARING, not padding. Written `total(xs)`, the
// iterable is the FIRST parameter and so lives in r0 - and a mutant that reads
// the wrong operand field of op::iterable gets 0, which names r0, which is the
// same register. The case passed with the source taken from `c` instead of `b`
// and said nothing. With a pad in front, r0 is a number and iterating it yields
// nothing.
function total(pad, xs) { var s = 0; for (var v of xs) { s = s + v; } return s; }
function chars(pad, t) { var s = ""; for (var ch of t) { s = ch + s; } return s; }
function spread(pad, xs) { return [...xs].length; }

// `in`, `instanceof` AND `delete` - three opcode bodies that were inline in
// run_loop and are now shared members, which is what stops the two tiers
// spelling them differently.
//
// `in` ON AN ARRAY ASKS ABOUT AN INDEX, so the key must parse as a whole
// number AND consume the whole string: "1x" is not index 1. On a non-object it
// is simply false rather than a throw.
//
// `instanceof` HAS TWO CHAINS. A page's own class is found by walking the
// explicit `prototype` links; an array or a function has no such link and is
// found through the implicit tables property lookup falls back to. And
// `5 instanceof Number` must be FALSE however many methods a primitive
// resolves - applying the implicit pass to primitives is the mirror image of
// the bug that pass fixed, so it is the case that pins the object-like guard.
function hasIt(pad, o, k) { return k in o; }
function isA(pad, x, C) { return x instanceof C; }
function drop(pad, o, k) { delete o[k]; return "" + o.a + o.b; }

// `super`, WHICH IS THREE OPCODES AND TWO SHAPES.
//
// `super.hello()` is load_home, then get_proto, then an ordinary method call -
// which is why lowering load_home alone moved no functions at all. `super(...)`
// is pass_new_target and a call.
//
// SUB SHADOWS hello ON PURPOSE, and that is the whole case. `super.hello()`
// must start ABOVE the class the running method was WRITTEN in - Sub's home -
// and so answers "S". Resolved against `this` instead it would start at Sub's
// own prototype, find Sub.hello, and answer "SUB"; in a deeper hierarchy the
// same mistake calls the running method again forever.
//
// NEITHER hello NEEDS COMPILING. greetChain is the body holding load_home and
// get_proto, and its name is unique, which is what lets a single patch reach
// it - the build's entry lookup takes a name and no ordinal.
class Sup { hello() { return "S"; } }
class Sub extends Sup {
  hello() { return "SUB"; }
  greetChain() { return "B" + super.hello(); }
}
function useSuper(pad) { return new Sub().greetChain(); }

// AND THE CONSTRUCTOR FORM. `super(v * 2)` hands Parent this frame's
// new.target and calls it.
//
// PARENT READS new.target, AND THAT IS THE ONLY REASON THIS SEPARATES
// pass_new_target AT ALL. Written without it the case passed with the
// pass_new_target lowering emitting nothing whatsoever: `this.v` is written by
// the base constructor either way, and new.target is the single observable
// difference. The mutant found that, not review.
//
// `v` COVERS THE CALL and `nt` covers the handoff, so a failure says which.
class Parent { constructor(v) { this.v = v; this.nt = new.target === Kid; } }
class Kid extends Parent { constructor(v) { super(v * 2); } }
function useSuperCtor(pad, n) { var o = new Kid(n); return "" + o.v + "/" + o.nt; }

// SPREAD CALLS - op::apply, which is a DIFFERENT opcode from op::call and not a
// variant of it. `f(a, b)` passes a contiguous window of values; `f(...xs)`
// passes ONE array, because the count was not known until the spread ran.
//
// sum3 TAKES THREE PARAMETERS AND PRINTS ALL OF THEM, which is what separates
// the two. Passing the array as a single argument - the plausible mistake, and
// what lowering apply as an ordinary call would do - answers "1,2,3/undefined/
// undefined" instead of "1/2/3".
//
// AND A NON-ITERABLE SPREAD YIELDS NOTHING, not one argument: `sum3(...5)` is
// sum3() and answers three undefineds, where passing 5 along would answer
// "5/undefined/undefined".
function sum3(a, b, c) { return "" + a + "/" + b + "/" + c; }
function spreadCall(pad, xs) { return sum3(...xs); }

// THE RECEIVER IS A SEPARATE OPERAND from the callee and the array, and only a
// method call can tell: ct_aot_call_spread takes it third, and passing
// undefined instead makes `this.tag` read undefined.
var recvObj = { tag: "R", m: function (a) { return this.tag + a; } };
function spreadMethod(pad, xs) { return recvObj.m(...xs); }

// AND THE `new` FORM, which is op::construct_apply - a different opcode again,
// with a different helper, sharing one VM_CASE with apply.
function Pt(a, b) { this.s = "" + a + b; }
function spreadNew(pad, xs) { return new Pt(...xs).s; }

// OBJECT SPREAD - op::copy_props, which is not a call at all: it mutates a
// freshly built object in place and produces nothing.
//
// THE ORDER IS THE ASSERTION. A later key WINS, so `{a: 1, ...o, b: 9}` with
// o = {a: 2, b: 3} is a=2 (the spread overwrote the literal) and b=9 (the
// literal overwrote the spread). Copying in the wrong direction, or copying
// before the earlier keys are written, changes both.
//
// AND AN ARRAY SOURCE SPREADS BY INDEX: `{...[7, 8]}` is {"0": 7, "1": 8}.
function merge(pad, o) { return { a: 1, ...o, b: 9 }; }
function mergeArray(pad, xs) { var m = { ...xs }; return "" + m[0] + m[1]; }
function spreadOut(pad, o) { var m = merge(0, o); return "" + m.a + m.b; }

// A LOOP AS THE FIRST STATEMENT, which is the ONLY shape whose back edge
// targets instruction ZERO - anything at all ahead of it, a `var` or a default
// parameter, pushes the loop header to 1 and the shape disappears.
//
// The importer marked index 0 a leader and then never built a block for it, so
// this was refused whole with "jump target is not a block leader". Six
// functions across the three vendored corpora have it.
//
// IT MUST STILL LOOP. A body that ran the test once and fell through answers 9;
// one that skipped the loop answers 9 too, so the input is chosen to make the
// loop run four times and the answer be the number it stops at.
function firstLoop(pad, n) { while (n > 3) { n = n - 1; } return n; }

// ACCESSORS - op::define_getter and op::define_setter, which are ONE helper and
// one operation. The half is decided by the OPCODE and by nothing else: no
// operand encodes it, so the importer resolves it and passes undefined for the
// other side.
//
// WHICH IS EXACTLY WHAT THIS SEPARATES. Swap the two and `o.v` stops reading 41
// and starts reading undefined, while `o.v = 1` stops recording anything - so
// the answer moves in both halves at once and cannot be right by accident.
//
// A PAIR UNDER ONE NAME IS ONE ENTRY, because accessor_table::define skips
// undefined halves rather than overwriting the other one. Defining the getter
// and then the setter must leave BOTH working, which a define that replaced the
// entry would break.
function accessors(pad, seed) {
  var box = { hidden: seed, log: "" };
  return {
    get v() { return box.hidden; },
    set v(x) { box.hidden = x; box.log = box.log + "s"; },
    box: box
  };
}
// try/catch, AND THE REGISTER FILE IS THE POINT.
//
// `n` IS WRITTEN INSIDE THE TRY, BEFORE THE THROW, and that is the whole
// fixture. context::unwind_to_handler overwrites exactly ONE register - the
// catch binding - and leaves every other one as the throw left it. So the
// handler must see n == 1.
//
// A LOWERING THAT TOOK THE HANDLER EDGE FROM push_handler INSTEAD answers 0:1
// rather than 1:1, because at the `try` n was still 0. That is the design this
// whole thing is built around and a fixture without the write cannot see it -
// with `try { return f(a); } catch ...` the two register files are identical
// and the wrong lowering passes.
//
// AND THE THROWN VALUE HAS TO ARRIVE, which separates a real catch from a frame
// that merely unwound: a lost thrown_ binds undefined.
// UNARY PLUS - ToNumber, and the ONE operation in this dialect that was fully
// lowered and completely unreachable: the helper had a body, operators.mlir
// exercised it from hand-written IR, and the importer's unary table simply had
// no row, so no JavaScript could ever produce it.
//
// IT IS NOT `x * 1` AND NOT `Number(x)`. The row is the NON-re-entering
// ToNumber, so an object with a valueOf is where it differs from the generic
// path - and the empty string and the empty array both coerce to 0 while
// undefined coerces to NaN, which is what separates ToNumber from truthiness.
function plusOf(pad, x) { return +x; }

// for-in AND NAMED delete - op::own_keys and op::delete_prop.
//
// `for (k in o)` COMPILES TO A for-of OVER own_keys, so this exercises the new
// helper and the iterable machinery together. The keys arrive in DEFINITION
// ORDER, which is why concatenating them is the assertion: a set that answered
// the right keys in the wrong order would still have the right length.
//
// AN ARRAY ENUMERATES ITS INDICES AS STRINGS, so `"0" + "1"` is "01" and not 1.
//
// AND delete IS THE NAMED FORM HERE - `delete o.b`, not `delete o[k]` - which
// is a different opcode with a different helper: a name cannot run a to_string
// the way a value key can.
// BIGINT LITERALS - op::load_bigint, which is parsed at RUN TIME from the
// source text and memoised per (site, slot).
//
// TWO DIFFERENT LITERALS IN ONE FUNCTION, which is the whole point: the memo is
// keyed by SLOT, and this backend numbers its slots in walk order while the
// interpreter numbers them by constant-pool index. A compiled body that shared
// the interpreter's key would read a slot the interpreter filled with the OTHER
// literal - so the two must differ, and both must be returned.
//
// AND `0x` IS NOT THE SAME PARSE AS DECIMAL. bigint_from_literal owns the hex,
// binary and octal forms; a lowering that parsed the digits itself would get
// 0x10n wrong and nothing else would notice.
function bigLits(pad) {
  var a = 900000000000000000009n;
  var b = 0x10n;
  return "" + a + "/" + b + "/" + (a + 1n);
}

function keysOf(pad, o) {
  var out = "";
  for (var k in o) { out = out + k; }
  return out;
}
function dropNamed(pad) {
  var o = { a: 1, b: 2, c: 3 };
  delete o.b;
  return keysOf(0, o) + "/" + o.b;
}
function coerce(pad) {
  return "" + plusOf(0, "42") + "/" + plusOf(0, "") + "/" + (plusOf(0, undef) !== plusOf(0, undef));
}

function guarded(pad, f, a) {
  var n = 0;
  try {
    n = 1;
    f(a);
    n = 2;
  } catch (e) {
    return "" + n + ":" + e;
  }
  return "" + n;
}

function useAccessor(pad, seed) {
  var o = accessors(0, seed);
  var first = o.v;
  o.v = 7;
  return "" + first + "/" + o.v + "/" + o.box.log;
}

// ASYNC WITHOUT await - op::wrap_promise, the only half of async that does not
// suspend, and therefore the only half a compiled C++ stack frame can do.
//
// A body with an `await` in it is refused whole by the importer, with a reason
// of its own that says why it is a design decision rather than a missing case.
// These three have none, so they compile like any other function and the
// promise they hand back is the ordinary settled object the standard library
// builds.
//
// THEY ARE READ THROUGH __value AND __settled ON PURPOSE. Those are own
// properties of the promise the runtime's own factory makes - not an API - and
// reading them is what lets this compare the OBJECT rather than whatever a
// `.then` chain would eventually deliver. A driver that awaited would need the
// microtask queue and would be testing the event loop instead.
async function wrapped(a) { return a + 1; }

// RETURNING A PROMISE MUST NOT NEST IT, which is the already-a-promise test.
// `passes(p) === p` is the separator: a lowering that dropped the test answers
// false and every other field here stays right.
//
// AND IT IS NOT ENOUGH ON ITS OWN, which was measured rather than reasoned. A
// compiled tier that wraps NOTHING still answers `true` here - two unwrapped
// values are trivially identical - so `===` separates a lowering that NESTS
// and says nothing about one that no-ops. The other two fields of arm 51 are
// what caught that mutation.
async function passes(p) { return p; }

// AND is_object() IS heap_kind::object EXACTLY, so an ARRAY returned from an
// async function is ALWAYS re-wrapped. A lowering that used is_object_like -
// which is what "is it already a promise" reads like in English - would pass
// the array straight through, and then __value is undefined instead of the
// array. This is the one case that separates the exact shape test.
async function arrayOut() { return [7, 8]; }

function drive(which) {
  // A SENTINEL, so an arm that throws or never matches is visible. Without it
  // OUT keeps the PREVIOUS case's answer, both tiers read the same stale value
  // and agree - which is a broken case reporting success. That happened.
  OUT = "<the arm did not run>";
  DIFF_R = 41; DIFF_W = 0;
  if (which === 0) { OUT = plus(counter2, 1); }
  if (which === 1) { OUT = ge(nan, nan); }
  if (which === 2) { OUT = strict(0, "0"); }
  if (which === 3) { OUT = loose(0, "0"); }
  if (which === 4) { OUT = pick(2, 7); }
  if (which === 5) { OUT = "" + globals(7) + "/" + DIFF_W; }
  if (which === 6) { OUT = apply(plus, counter2, 1); }
  // CALLED TWICE, because one call cannot tell a captured cell from a copy:
  // both answer 1. The second answer is 2 only if the binding persisted.
  if (which === 7) { var c = counter(10); c(); OUT = c(); }
  // 101 + 201 = 302 if the two closures capture separately; 101 + 102 = 203 if
  // they share, which is what taking upvalues from the proto would do.
  if (which === 8) { OUT = counters(100, 200); }
  // BUILDING a closure rather than reading one.
  if (which === 9) { var d = counter(50); d(); OUT = d(); }
  // 1 + 2 = 3, and only if both descriptor arms picked the right binding.
  if (which === 10) { OUT = outer(1)(2)(); }
  // .call GIVES methodish A RECEIVER without needing an object literal, which
  // would reach ct_aot_set_index - one of the rows with no body yet.
  if (which === 11) { OUT = methodish.call("captured", 0); }
  // THE NUMBER ARM, and -0 is the case a naive negation gets right by accident
  // while `Object.is` can still tell: 1/-0 is -Infinity.
  if (which === 12) { OUT = "" + neg(5) + "/" + (1 / neg(0)); }
  // AND THE BIGINT ARM, which allocates. It reaches the compiled body as an
  // ARGUMENT rather than a literal, because ct_aot_new_bigint_literal is one
  // of the rows with no body yet.
  if (which === 13) { OUT = "" + neg(big) + "/" + neg(zeroBig); }
  if (which === 14) { OUT = "" + bnot(5) + "/" + bnot(big); }
  // A NUMERIC key writes items[0]: 9*100 + 0 = 900. A STRING key does not -
  // store_property's array arm drops everything but `length` - so items[0] is
  // still 1 and nothing reads back: 1*100 + 0 = 100.
  //
  // WHAT THIS DOES NOT SEPARATE, said plainly: removing `key.is_number()` from
  // store_index's guard leaves the answer unchanged. The fast path would then
  // compute its index from as_number() of a STRING, which is undefined
  // behaviour, and on this target it lands out of range and the write is
  // dropped - the same observable result. A case that pins that guard would
  // have to rely on what the UB happens to do, which is worse than not pinning
  // it.
  if (which === 15) { OUT = "" + put([1, 2], 0, 9, "0") + "/" + put([1, 2], "0", 9, "0"); }
  if (which === 16) { OUT = greet(3); }
  // 1*100 + 1*10 + 2 = 112, with the object read through `| 0` so a missing
  // property is 0 rather than a string.
  if (which === 17) { OUT = pack(1, 2); }
  if (which === 18) { OUT = kindOf(1) + "/" + kindOf(nan) + "/" + kindOf(big); }
  // THE VALUE MUST ARRIVE INTACT, which is what distinguishes a real throw from
  // a frame that merely unwound: a lost `thrown_` would catch undefined.
  if (which === 19) { try { thrower(7); OUT = "not thrown"; } catch (e) { OUT = "caught " + e; } }
  // 3*2 + 3 = 9, and only if `new` answered the instance rather than the 7
  // Point returns and the prototype came from Point.prototype.
  if (which === 20) { OUT = build(3); }
  // THE MESSAGE IS THE ASSERTION, because the function it names is the operand
  // under test. `in \`newBad\`` is ct_aot_construct's `site`.
  if (which === 21) { try { newBad(5); OUT = "not thrown"; } catch (e) { OUT = "" + e; } }
  // 0 and "" are DEFINED, so `??` keeps them; a nullish left side takes the
  // right. `?.` on null is undefined rather than a throw.
  if (which === 23) { OUT = "" + total(0, [1, 2, 3]) + "/" + chars(0, "abc") + "/" + total(0, 7); }
  if (which === 24) { OUT = "" + spread(0, "hey") + "/" + spread(0, [1, 2]); }
  if (which === 25) {
    OUT = "" + hasIt(0, [7, 8], 0) + "/" + hasIt(0, [7, 8], "1x") + "/" + hasIt(0, [7, 8], 2) +
          "/" + hasIt(0, { a: 1 }, "a") + "/" + hasIt(0, 5, "x");
  }
  if (which === 26) {
    OUT = "" + isA(0, new Point(1), Point) + "/" + isA(0, [], Array) + "/" + isA(0, 5, Number);
  }
  if (which === 27) { OUT = drop(0, { a: 1, b: 2 }, "a"); }
  if (which === 28) { OUT = useSuper(0); }
  if (which === 29) { OUT = useSuperCtor(0, 10); }
  if (which === 30) { OUT = spreadCall(0, [1, 2, 3]) + "|" + spreadCall(0, 5); }
  if (which === 31) { OUT = spreadMethod(0, [7]); }
  if (which === 32) { OUT = spreadNew(0, [1, 2]); }
  if (which === 33) { OUT = spreadOut(0, { a: 2, b: 3 }) + "/" + mergeArray(0, [7, 8]); }
  if (which === 34) { OUT = "" + firstLoop(0, 9) + "/" + firstLoop(0, 1); }
  if (which === 35) { OUT = useAccessor(0, 41); }
  // thrower THROWS AND neg DOES NOT, so one arm takes the caught edge and the
  // other runs the try to its end - the same compiled body, both ways through.
  if (which === 36) { OUT = guarded(0, thrower, 7) + "/" + guarded(0, neg, 5); }
  // NaN !== NaN, so the third field is `true` only if `+undefined` really is
  // NaN - a coercion that answered 0 would make it false.
  if (which === 37) { OUT = coerce(0); }
  if (which === 38) { OUT = keysOf(0, { x: 1, y: 2 }) + "/" + keysOf(0, [7, 8]) + "/" + keysOf(0, 5); }
  if (which === 39) { OUT = dropNamed(0); }
  if (which === 40) { OUT = bigLits(0); }
  // 50 AND 51, NOT 41 AND 42, and the gap is deliberate: the `arguments` and
  // rest-parameter work is landing 40-43 on a branch of its own, and two tracks
  // numbering the same arm differently is a merge that compiles and tests the
  // wrong thing.
  //
  // typeof IS IN THE ANSWER because "object" is what separates a promise from
  // the number 2 - a lowering that skipped the wrap entirely would answer
  // "number/2/undefined", with the __value field looking almost right.
  if (which === 50) {
    var p = wrapped(1);
    OUT = "" + (typeof p) + "/" + p.__value + "/" + p.__settled + "/" + p.__rejected;
  }
  // THE TWO SHAPE QUESTIONS IN ONE LINE. `passes(p) === p` is the
  // already-a-promise test; `arrayOut().__value[0]` is the exactness of
  // is_object(), because an array must be re-wrapped and not passed through.
  if (which === 51) {
    var q = wrapped(2);
    OUT = "" + (passes(q) === q) + "/" + arrayOut().__value[0] + "/" +
          (typeof arrayOut().__value);
  }
  if (which === 22) {
    OUT = "" + coalesce(0, 9) + "/" + coalesce("", 9) + "/" + coalesce(nothing, 9) + "/" +
          chain(null) + "/" + chain({ p: 4 }) + "/" + dflt(0);
  }
}
