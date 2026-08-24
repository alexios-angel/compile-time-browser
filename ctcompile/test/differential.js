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
var big = 9007199254740993n;
var zeroBig = 0n;

function counters(a, b) { return counter(a)() + counter(b)(); }

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
}
