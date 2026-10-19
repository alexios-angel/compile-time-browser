// The bodies the differential test compiles.
//
// Each function here is compiled by the build through the real pipeline and
// also run by the interpreter, and the two answers must agree. The interpreter
// defines correct behaviour, as the CTJS dialect's own policy states: "when a
// CTJS operation and the ctbrowser VM disagree, the VM is correct by
// definition."
//
// Differential.cpp includes this file verbatim (as differential.js.inc), so
// both sides compile the same function_proto. The driver also checks the
// function count, and function order matters because a compiled body bakes
// in the function index of every closure it builds.
//
// Each body is chosen for an answer that differs if the lowering is wrong in a
// specific way, not for coverage. A test that returns the same thing whether
// or not the compiler is right is worse than no test.

// `+` belongs to the re-entering family. Lowered to op::add - the static one,
// which `++` uses - an object with a valueOf would answer NaN instead of
// running it.
function plus(a, b) {
    return a + b;
}

// The four relational operators are not negations of one another, because
// ct_aot_compare can answer UNORDERED and that makes all four false. Lowering
// `>=` as `!(<)` makes `NaN >= NaN` true, which this catches.
function ge(a, b) {
    return a >= b;
}

// Strict and loose equality reach different helpers with different effect
// profiles: one cannot throw at all, the other converts.
//
// Two functions, so each answer comes from exactly one helper.
function strict(a, b) {
    return a === b;
}

function loose(a, b) {
    return a == b;
}

// Control flow, whose block arguments the C++ emitter miscompiles unless they
// are lowered to variables first.
function pick(a, b) {
    if (a < b) {
        return a;
    }
    return b;
}

// Globals, which are a read and a write with no status and no edge.
function globals(a) {
    DIFF_W = a;
    return DIFF_R;
}

// A call, the only operation needing a contiguous run of frame slots.
function apply(k, a, b) {
    return k(a, b);
}

// A captured binding. The compiled `step` must write through to the cell that
// `counter` created, not to a copy of it. A copy gives the same answer on
// every call; a shared cell gives 12 on the second call.
//
// Arm 7 compiles only `step` and lets the interpreter build the closure. Arm 9
// compiles `counter` as well, so ct_aot_make_closure runs in compiled code and
// the closure is built there.
//
// A body that cannot see its closure answers `undefined`: the write lands on
// a non-cell and is dropped, and the read finds no closure.
function counter(start) {
    var n = start;
    return function step() {
        n = n + 1;
        return n;
    };
}

// Two closures over one function separate the instance from the proto, which
// is the mistake the entry ABI invites: `site` is the function_proto and every
// closure over `step` shares it. A body that took its upvalues from there
// would have the two counters share a binding, and the second would continue
// the first's count instead of starting again.

// Two levels of nesting separate the two descriptor arms. When `middle` builds
// `inner`, `y` is a from_parent_local - a cell in middle's own register - while
// `x` is not: it comes from middle's own closure, which is the arm that carries
// a capture down through more than one level. A build that read both from the
// registers, or both from the enclosing closure, gets one of them wrong.
function outer(a) {
    var x = a;
    return function middle(b) {
        var y = b;
        return function inner() {
            return x + y;
        };
    };
}

// An arrow decides its `this` where it is written, and this is the only shape
// that separates that from the raw receiver an entry is handed.
//
// The compiled body's argument 0 comes from ct_aot_this - which is
// effective_this, the enclosing method's object when the frame's closure is an
// arrow - rather than from the entry ABI's raw `receiver`. The ordinary parent
// first binds nullish receivers to its global object and boxes primitives.
// `seen()` is called with no receiver at all, so its raw one is undefined and
// its captured binding is the parent's effective `this`.
function methodish(ignored) {
    var seen = () => this;
    return seen();
}

// Unary minus and bitwise not, whose Number and BigInt arms are not one operation.
//
// The shape invites lowering `-x` as ToNumber plus a negation, and that is
// wrong for a BigInt: it is negated as an unbounded integer and allocates, so
// `-0n` is `0n` - a BigInt has one zero - and to_number_value's own "Cannot
// convert a BigInt value to a number" TypeError would fire instead. `+1n`
// throws; `-1n` does not.
//
// `~1n` is `-2n` for a related reason: there is no ToInt32 step, because a
// BigInt has no width to truncate to.
function neg(a) {
    return -a;
}

function bnot(a) {
    return ~a;
}

// A property write, whose slow path is not "the non-array case". The fast path
// is array and number, so `a['0'] = ...` on an array takes the named path
// through store_property and does not land in items[0] - which is why reading
// it back as a[0] gives undefined.
//
// It reads back both keys, because reading only o[0] does not separate the two
// paths: a named write leaves items[0] alone, and a fast-path write with a
// nonsense index is dropped and leaves it alone too. Both answer 1.
//
// The string key arrives as an argument, so the test is only about the store
// path and not about building a string constant. The answer packs both reads
// into one number so a single comparison checks both; `| 0` turns a missing
// property into 0.
function put(o, k, v, s) {
    o[k] = v;
    return o[0] * 100 + (o[s] | 0);
}

// A string literal, and its memo.
//
// ct_aot_new_string caches by (site, slot) and that memo is part of the ABI
// rather than an optimisation: `allocations_` counts total allocations for the
// process lifetime and is never reset, so a literal in a loop that allocated
// per iteration would reach the 40,000,000 ceiling and raise an uncatchable
// error on a program the interpreter runs forever.
//
// Sharing one object is safe because string identity is unobservable - strict
// equality compares text - which is also why this case cannot check the memo by
// asking whether two reads are the same object. What it can check is that the
// literal is still the right text after a loop, and that concatenating it works,
// which is what a broken length or a truncating escape would break.
function greet(n) {
    var out = "";
    for (var i = 0; i < n; i++) {
        out = out + "ab";
    }
    return out + "!";
}

// Object and array literals. Both allocate and fail only by raising: allocate()
// raises past the ceiling and still returns a well-formed object, so neither
// has a status to test, only a later ct_aot_failed poll to schedule.
//
// The array is built the way the bytecode builds one: new_array, then one
// append per element.
function pack(a, b) {
    var o = {};
    o[a] = b;
    var arr = [a, b, a];
    return arr[0] * 100 + arr[2] * 10 + (o[a] | 0);
}

// `typeof`, which is two calls and not memoised. ct_aot_type_of_name answers
// with a length and a pointer to static storage; ct_aot_new_string turns those
// into a value, with a NULL site meaning do not memoise - because
// VM_CASE(type_of) has no cache, and memoising would allocate fewer times than
// the interpreter, which changes when the allocation ceiling is reached.
function kindOf(a) {
    return typeof a;
}

// A throw, caught by an interpreted caller.
//
// The catch is deliberately outside the compiled body. The handler that wins
// belongs to an interpreted frame, so the throw has to cross out of compiled
// code, and CT_AOT_CAUGHT is never the status this frame sees. `guarded`
// (arm 36) is the opposite case, with the try inside the compiled body and
// the handler edge taken there.
//
// ct_aot_throw never returns ok. The throw completes inside the callee: by the
// time it returns there is no exception in flight, only a frame stack shorter
// than it was. So the compiled body tests nothing and branches straight to the
// epilogue, which must not call ct_aot_leave when the status is unwound - the
// frame is already gone.
function thrower(a) {
    throw a;
}

// --- the harness the driver calls -------------------------------------------
//
// It lives here rather than in the C++ so that the file the backend compiles
// and the file the interpreter runs are the same file. The functions above are
// what is compiled; these are what drives them.
var DIFF_R = 0,
    DIFF_W = 0,
    OUT = "";

// The arguments are built here rather than passed from C++, so the harness
// holds no JavaScript value in a C++ local of its own.
var counter2 = {
    valueOf: function () {
        return 3;
    }
};
var nan = 0 / 0;
var nothing;
var undef;
var big = 9007199254740993n;
var zeroBig = 0n;

function counters(a, b) {
    return counter(a)() + counter(b)();
}

// `new`, and its two answers that are not the body's.
//
// Point returns a primitive, which `new` throws away: the value of
// `new Point(3)` is the fresh instance, not 7. Lowering ctjs.construct as an
// ordinary call - the plausible mistake, since ct_aot_call takes the same
// contiguous window - gives 7 instead, and then `p.twice` is undefined and the
// arm throws rather than answering wrongly.
//
// And the instance has to be wired to Point.prototype, which is
// make_instance's half of the work rather than the body's: `twice` is on the
// prototype and nothing in Point mentions it.
function Point(x) {
    this.x = x;
    return 7;
}
Point.prototype.twice = function () {
    return this.x * 2;
};

function build(a) {
    var p = new Point(a);
    return p.twice() + p.x;
}

// A `new` on something that is not a constructor, which is the only case that
// reads ct_aot_construct's `site`. Every other operand is a value the body
// already had; `site` exists solely to name the enclosing function in the
// TypeError, so if the lowering passes the memo marker instead of the entry's
// own site - two fields of one struct, one letter apart - this is what says so.
//
// The try is in `drive`, not here, so the TypeError crosses out of the
// compiled frame into an interpreted handler, and the message the handler
// reads is the one this compiled frame built.
function newBad(f) {
    return new f();
}

// `??`, `?.` and a default parameter - the three shapes that compile to the
// conditional jumps on null or undefined rather than to a truthiness test.
//
// Not truthiness is the whole point, and what makes these arguments the ones
// to pass. `0 ?? 9` is 0 and `"" ?? 9` is "", because both are defined;
// lowering either jump through ctjs.truthy answers 9 for each and is exactly
// the bug optional chaining exists to avoid. A default parameter separates the
// third: `dflt(0)` is 0 and `dflt()` is 5.
function coalesce(a, b) {
    return a ?? b;
}

function chain(o) {
    return o?.p;
}

function dflt(a) {
    if (a === undefined) {
        a = 5;
    }
    return a;
}

// for-of and spread share op::iterable for an array or a string: the helper
// answers with a plain array that the loop then indexes. for-of first asks
// `__ctbrowser_for_of_open`, which answers undefined for those (the index
// loop) and an iterator record for a generator or an own Symbol.iterator;
// a non-iterable throws the TypeError the specification requires, which
// `nonobj` catches by name.
//
// `chars` prepends rather than appends, so the answer records the order the
// values came out in: a drain that reverses gives "abc" where "cba" is right,
// and a sum could not tell.
//
// The leading `pad` parameter matters. Written `total(xs)`, the iterable is
// the first parameter and so lives in r0, and a mutant that reads the wrong
// operand field of op::iterable gets 0, which names r0 - the same register,
// so the case passes. With a pad in front, r0 is a number and iterating it
// yields nothing.
function total(pad, xs) {
    var s = 0;
    for (var v of xs) {
        s = s + v;
    }
    return s;
}

function chars(pad, t) {
    var s = "";
    for (var ch of t) {
        s = ch + s;
    }
    return s;
}

function nonobj(pad, n) {
    try {
        return total(pad, n);
    } catch (e) {
        return e.name;
    }
}

function spread(pad, xs) {
    return [...xs].length;
}

// `in`, `instanceof` and `delete` - three opcode bodies that the interpreter's
// run_loop and compiled code share as context members, so the two cannot
// implement them differently.
//
// `in` on an array asks about an index, so the key must parse as a whole
// number and consume the whole string: "1x" is not index 1. On a non-object
// it is the TypeError of ECMA-262 13.10.1. `hasItPrimitive` catches it by
// name, because an uncaught throw here would escape `drive` and fail every
// arm driven after this one.
//
// `instanceof` has two chains. A page's own class is found by walking the
// explicit `prototype` links; an array or a function has no such link and is
// found through the implicit tables property lookup falls back to. And
// `5 instanceof Number` must be false however many methods a primitive
// resolves: the implicit pass applies to objects only, and this is the case
// that pins that object-like guard.
function hasIt(pad, o, k) {
    return k in o;
}

function hasItPrimitive(pad, o, k) {
    try {
        return hasIt(pad, o, k);
    } catch (e) {
        return e.name;
    }
}

function isA(pad, x, C) {
    return x instanceof C;
}

function drop(pad, o, k) {
    delete o[k];
    return "" + o.a + o.b;
}

// `super`, which is three opcodes and two shapes.
//
// `super.hello()` is load_home, then get_proto, then an ordinary method call,
// so lowering load_home alone does not let any such function compile.
// `super(...)` is pass_new_target and a call.
//
// Sub shadows hello on purpose, and that is the whole case. `super.hello()`
// must start above the class the running method was written in - Sub's home -
// and so answers "S". Resolved against `this` instead it would start at Sub's
// own prototype, find Sub.hello, and answer "SUB"; in a deeper hierarchy the
// same mistake calls the running method again forever.
//
// Neither hello needs compiling. greetChain is the body holding load_home and
// get_proto, and its name is unique, which is what lets a single patch reach
// it - the build's entry lookup takes a name and no ordinal.
class Sup {
    hello() {
        return "S";
    }
}
class Sub extends Sup {
    hello() {
        return "SUB";
    }
    greetChain() {
        return "B" + super.hello();
    }
}

function useSuper(pad) {
    return new Sub().greetChain();
}

// And the constructor form. `super(v * 2)` hands Parent this frame's
// new.target and calls it.
//
// Parent reads new.target, and that is the only reason this case separates
// pass_new_target at all. Without that read, a pass_new_target lowering that
// emitted nothing would still pass: `this.v` is written by the base
// constructor either way, and new.target is the single observable
// difference.
//
// `v` covers the call and `nt` covers the handoff, so a failure says which.
class Parent {
    constructor(v) {
        this.v = v;
        this.nt = new.target === Kid;
    }
}
class Kid extends Parent {
    constructor(v) {
        super(v * 2);
    }
}

function useSuperCtor(pad, n) {
    var o = new Kid(n);
    return "" + o.v + "/" + o.nt;
}

// Spread calls - op::apply, which is a different opcode from op::call and not a
// variant of it. `f(a, b)` passes a contiguous window of values; `f(...xs)`
// passes one array, because the count was not known until the spread ran.
//
// sum3 takes three parameters and prints all of them, which is what separates
// the two. Passing the array as a single argument - the plausible mistake, and
// what lowering apply as an ordinary call would do - answers "1,2,3/undefined/
// undefined" instead of "1/2/3".
//
// And a non-iterable spread is the TypeError of GetIterator (13.3.8.1
// ArgumentListEvaluation): `sum3(...5)` throws, and `spreadNonIterable`
// catches it by name. A lowering that passed 5 along as the array would
// answer "5/undefined/undefined" instead, and one that skipped the check
// would answer three undefineds.
function sum3(a, b, c) {
    return "" + a + "/" + b + "/" + c;
}

function spreadCall(pad, xs) {
    return sum3(...xs);
}

function spreadNonIterable(pad, n) {
    try {
        return spreadCall(pad, n);
    } catch (e) {
        return e.name;
    }
}

// The receiver is a separate operand from the callee and the array, and only a
// method call can tell: ct_aot_call_spread takes it third, and passing
// undefined instead makes `this.tag` read undefined.
var recvObj = {
    tag: "R",
    m: function (a) {
        return this.tag + a;
    }
};

function spreadMethod(pad, xs) {
    return recvObj.m(...xs);
}

// And the `new` form, which is op::construct_apply - a different opcode again,
// with a different helper, sharing one VM_CASE with apply.
function Pt(a, b) {
    this.s = "" + a + b;
}

function spreadNew(pad, xs) {
    return new Pt(...xs).s;
}

// Object spread - op::copy_props, which is not a call at all: it mutates a
// freshly built object in place and produces nothing.
//
// The order is the assertion. A later key wins, so `{a: 1, ...o, b: 9}` with
// o = {a: 2, b: 3} is a=2 (the spread overwrote the literal) and b=9 (the
// literal overwrote the spread). Copying in the wrong direction, or copying
// before the earlier keys are written, changes both.
//
// And an array source spreads by index: `{...[7, 8]}` is {"0": 7, "1": 8}.
function merge(pad, o) {
    return {
        a: 1,
        ...o,
        b: 9
    };
}

function mergeArray(pad, xs) {
    var m = {
        ...xs
    };
    return "" + m[0] + m[1];
}

function spreadOut(pad, o) {
    var m = merge(0, o);
    return "" + m.a + m.b;
}

// A loop as the first statement, which is the only shape whose back edge
// targets instruction zero - anything ahead of it, a `var` or a default
// parameter, pushes the loop header to 1 and the shape disappears.
//
// The importer must build a block for a jump target at index 0; otherwise it
// refuses the function with "jump target is not a block leader". Functions in
// the vendored corpora have this shape.
//
// It must still loop. A body that ran the test once and fell through answers
// 9; one that skipped the loop answers 9 too, so the input is chosen to make
// the loop run six times and the answer be the number it stops at.
function firstLoop(pad, n) {
    while (n > 3) {
        n = n - 1;
    }
    return n;
}

// Accessors - op::define_getter and op::define_setter, which are one helper and
// one operation. The half is decided by the opcode and by nothing else: no
// operand encodes it, so the importer resolves it and passes undefined for the
// other side.
//
// Which is exactly what this separates. Swap the two and `o.v` stops reading 41
// and starts reading undefined, while `o.v = 1` stops recording anything - so
// the answer moves in both halves at once and cannot be right by accident.
//
// A pair under one name is one entry, because accessor_table::define skips
// undefined halves rather than overwriting the other one. Defining the getter
// and then the setter must leave both working, which a define that replaced the
// entry would break.
function accessors(pad, seed) {
    var box = {
        hidden: seed,
        log: ""
    };
    return {
        get v() {
            return box.hidden;
        },
        set v(x) {
            box.hidden = x;
            box.log = box.log + "s";
        },
        box: box
    };
}
// try/catch (`guarded`, below), and the register file is the point.
//
// `n` is written inside the try, before the throw, and that is the whole
// fixture. context::unwind_to_handler overwrites exactly one register - the
// catch binding - and leaves every other one as the throw left it. So the
// handler must see n == 1.
//
// A lowering that took the handler's register state from push_handler instead
// answers 0:7 rather than 1:7, because at the `try` n was still 0. The handler
// edge starts at the throwing call, and a fixture without the write cannot see
// the difference: with `try { return f(a); } catch ...` the two register files
// are identical and the wrong lowering passes.
//
// And the thrown value has to arrive, which separates a real catch from a frame
// that merely unwound: a lost thrown_ binds undefined.
// Unary plus (`plusOf`) - ToNumber. The importer's unary table must map `+x`
// to this operation; without that entry the helper is reachable only from
// hand-written IR (operators.mlir) and no JavaScript produces it. `coerce`,
// below, drives it.
//
// It is not `x * 1` and not `Number(x)`. This is the non-re-entering
// ToNumber, so an object with a valueOf is where it differs from the generic
// path - and the empty string and the empty array both coerce to 0 while
// undefined coerces to NaN, which is what separates ToNumber from truthiness.
function plusOf(pad, x) {
    return +x;
}

// for-in and named delete (`keysOf`, `dropNamed`) - op::own_keys, op::delete_prop.
//
// `for (k in o)` compiles to a for-of over own_keys, so this exercises the
// own_keys helper and the iterable machinery together. The keys arrive in
// definition order, which is why concatenating them is the assertion: a set
// that answered the right keys in the wrong order would have the right length.
//
// An array enumerates its indices as strings, so `"0" + "1"` is "01", not 1.
//
// And delete is the named form here - `delete o.b`, not `delete o[k]` - which
// is a different opcode with a different helper: a name cannot run a to_string
// the way a value key can.
// BigInt literals (`bigLits`, below) - op::load_bigint, which is parsed at run
// time from the source text and memoised per (site, slot).
//
// Two different literals in one function, which is the whole point: the memo is
// keyed by slot, and this backend numbers its slots in walk order while the
// interpreter numbers them by constant-pool index. A compiled body that shared
// the interpreter's key would read a slot the interpreter filled with the other
// literal - so the two must differ, and both must be returned.
//
// And `0x` is not the same parse as decimal. bigint_from_literal owns the hex,
// binary and octal forms; a lowering that parsed the digits itself would get
// 0x10n wrong and nothing else would notice.
// `arguments` and a rest parameter, which are one problem: both need the
// arguments past the declared parameters, which a compiled frame does not hold
// - ct_aot_enter puts the frame's own registers above the caller's window and
// the entry prologue reads argv only for the declared ones.
//
// Every call passes more than it declares, which is the whole point: a body
// that could only see its declared parameters answers 2 and [] here.
function howMany(a, b) {
    return arguments.length;
}

function sumAll() {
    var t = 0;
    for (var i = 0; i < arguments.length; i = i + 1) {
        t = t + arguments[i];
    }
    return t;
}

function restOf(a, ...xs) {
    return xs.length + ":" + xs.join(",");
}

// And the path that only exists when both run. Building `arguments` claims a
// register an extra argument may be sitting in, so gather_rest reads the
// frame's copy rather than the window once it has - and a body with both is the
// only shape that reaches it. No corpus contains one; this fixture is the only
// coverage that arm gets.
function bothOf(a, ...xs) {
    return arguments.length + "/" + xs.join(",");
}

function bigLits(pad) {
    var a = 900000000000000000009n;
    var b = 0x10n;
    return "" + a + "/" + b + "/" + (a + 1n);
}

function keysOf(pad, o) {
    var out = "";
    for (var k in o) {
        out = out + k;
    }
    return out;
}

function dropNamed(pad) {
    var o = {
        a: 1,
        b: 2,
        c: 3
    };
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

// Async without await - op::wrap_promise, the only half of async that does not
// suspend, and therefore the only half a compiled C++ stack frame can do.
//
// A body with an `await` in it is refused whole by the importer, with a reason
// of its own that says why it is a design decision rather than a missing case.
// These three have none, so they compile like any other function and the
// promise they hand back is the ordinary settled object the standard library
// builds.
//
// They are read through __value and __settled on purpose. Those are own
// properties of the promise the runtime's own factory makes - not an API - and
// reading them is what lets this compare the object rather than whatever a
// `.then` chain would eventually deliver. A driver that awaited would need the
// microtask queue and would be testing the event loop instead.
async function wrapped(a) {
    return a + 1;
}

// Returning a promise must not nest it, which is the already-a-promise test.
// `passes(p) === p` is the separator: a lowering that dropped the test answers
// false and every other field here stays right.
//
// And it is not enough on its own. A lowering that wraps nothing still answers
// `true` here - two unwrapped values are trivially identical - so `===`
// catches a lowering that nests and says nothing about one that does
// nothing. The other two fields of arm 51 are what catch a lowering that
// skips the wrap.
async function passes(p) {
    return p;
}

// And is_object() is heap_kind::object exactly, so an array returned from an
// async function is always re-wrapped. A lowering that used is_object_like -
// which is what "is it already a promise" reads like in English - would pass
// the array straight through, and then __value is undefined instead of the
// array. This is the one case that separates the exact shape test.
async function arrayOut() {
    return [7, 8];
}

function drive(which) {
    // A sentinel, so an arm that throws or never matches is visible. Without it
    // OUT keeps the previous case's answer, the interpreter and the compiled
    // code read the same stale value and agree, and a broken case reports success.
    OUT = "<the arm did not run>";
    DIFF_R = 41;
    DIFF_W = 0;
    if (which === 0) {
        OUT = plus(counter2, 1);
    }
    if (which === 1) {
        OUT = ge(nan, nan);
    }
    if (which === 2) {
        OUT = strict(0, "0");
    }
    if (which === 3) {
        OUT = loose(0, "0");
    }
    if (which === 4) {
        OUT = pick(2, 7);
    }
    if (which === 5) {
        OUT = "" + globals(7) + "/" + DIFF_W;
    }
    if (which === 6) {
        OUT = apply(plus, counter2, 1);
    }
    // Called twice, because one call cannot tell a captured cell from a copy:
    // both answer 11. The second answer is 12 only if the binding persisted.
    if (which === 7) {
        var c = counter(10);
        c();
        OUT = c();
    }
    // 101 + 201 = 302 if the two closures capture separately; 101 + 102 = 203 if
    // they share, which is what taking upvalues from the proto would do.
    if (which === 8) {
        OUT = counters(100, 200);
    }
    // Arm 9 compiles `counter`, so the closure is built in compiled code.
    if (which === 9) {
        var d = counter(50);
        d();
        OUT = d();
    }
    // 1 + 2 = 3, and only if both descriptor arms picked the right binding.
    if (which === 10) {
        OUT = outer(1)(2)();
    }
    // .call gives methodish a primitive receiver, which the arrow must capture;
    // the arrow call itself has no receiver at all.
    if (which === 11) {
        OUT = methodish.call("captured", 0);
    }
    if (which === 43) {
        OUT = methodish(0) === globalThis;
    }
    if (which === 44) {
        var boxed = Reflect.apply(methodish, 5, [0]);
        OUT = [typeof boxed, boxed instanceof Number, boxed.valueOf()].join();
    }
    // The Number arm, and -0 is the case a naive negation gets right by accident
    // while `Object.is` can still tell: 1/-0 is -Infinity.
    if (which === 12) {
        OUT = "" + neg(5) + "/" + (1 / neg(0));
    }
    // The BigInt arm, which allocates; -0n is 0n because a BigInt has one zero.
    // The value is passed as an argument; BigInt literals are covered by
    // `bigLits` (arm 40).
    if (which === 13) {
        OUT = "" + neg(big) + "/" + neg(zeroBig);
    }
    if (which === 14) {
        OUT = "" + bnot(5) + "/" + bnot(big);
    }
    // A numeric key writes items[0]: 9*100 + 0 = 900. A string key does not -
    // store_property's array arm drops everything but `length` - so items[0] is
    // still 1 and nothing reads back: 1*100 + 0 = 100.
    //
    // What this does not separate, said plainly: removing `key.is_number()` from
    // store_index's guard leaves the answer unchanged. The fast path would then
    // compute its index from as_number() of a string, which is undefined
    // behaviour, and on this target it lands out of range and the write is
    // dropped - the same observable result. A case that pins that guard would
    // have to rely on what the UB happens to do, which is worse than not pinning
    // it.
    if (which === 15) {
        OUT = "" + put([1, 2], 0, 9, "0") + "/" + put([1, 2], "0", 9, "0");
    }
    if (which === 16) {
        OUT = greet(3);
    }
    // 1*100 + 1*10 + 2 = 112, with the object read through `| 0` so a missing
    // property is 0 rather than a string.
    if (which === 17) {
        OUT = pack(1, 2);
    }
    if (which === 18) {
        OUT = kindOf(1) + "/" + kindOf(nan) + "/" + kindOf(big);
    }
    // The value must arrive intact, which is what distinguishes a real throw from
    // a frame that merely unwound: a lost `thrown_` would catch undefined.
    if (which === 19) {
        try {
            thrower(7);
            OUT = "not thrown";
        } catch (e) {
            OUT = "caught " + e;
        }
    }
    // 3*2 + 3 = 9, and only if `new` answered the instance rather than the 7
    // Point returns and the prototype came from Point.prototype.
    if (which === 20) {
        OUT = build(3);
    }
    // The message is the assertion, because the function it names is the operand
    // under test. `in \`newBad\`` is ct_aot_construct's `site`.
    if (which === 21) {
        try {
            newBad(5);
            OUT = "not thrown";
        } catch (e) {
            OUT = "" + e;
        }
    }
    // Arm 23 drives for-of through `total`, `chars` and `nonobj`. The `??` and
    // `?.` arm, 22, is the last one in this function.
    if (which === 23) {
        OUT = "" + total(0, [1, 2, 3]) + "/" + chars(0, "abc") + "/" + nonobj(0, 7);
    }
    if (which === 24) {
        OUT = "" + spread(0, "hey") + "/" + spread(0, [1, 2]);
    }
    if (which === 25) {
        OUT = "" + hasIt(0, [7, 8], 0) + "/" + hasIt(0, [7, 8], "1x") + "/" + hasIt(0, [7, 8], 2) +
            "/" + hasIt(0, {
                a: 1
            }, "a") + "/" + hasItPrimitive(0, 5, "x");
    }
    if (which === 26) {
        OUT = "" + isA(0, new Point(1), Point) + "/" + isA(0, [], Array) + "/" + isA(0, 5, Number);
    }
    if (which === 27) {
        OUT = drop(0, {
            a: 1,
            b: 2
        }, "a");
    }
    if (which === 28) {
        OUT = useSuper(0);
    }
    if (which === 29) {
        OUT = useSuperCtor(0, 10);
    }
    if (which === 30) {
        OUT = spreadCall(0, [1, 2, 3]) + "|" + spreadNonIterable(0, 5);
    }
    if (which === 31) {
        OUT = spreadMethod(0, [7]);
    }
    if (which === 32) {
        OUT = spreadNew(0, [1, 2]);
    }
    if (which === 33) {
        OUT = spreadOut(0, {
            a: 2,
            b: 3
        }) + "/" + mergeArray(0, [7, 8]);
    }
    if (which === 34) {
        OUT = "" + firstLoop(0, 9) + "/" + firstLoop(0, 1);
    }
    if (which === 35) {
        OUT = useAccessor(0, 41);
    }
    // thrower throws and neg does not, so one arm takes the caught edge and the
    // other runs the try to its end - the same compiled body, both ways through.
    if (which === 36) {
        OUT = guarded(0, thrower, 7) + "/" + guarded(0, neg, 5);
    }
    // NaN !== NaN, so the third field is `true` only if `+undefined` really is
    // NaN - a coercion that answered 0 would make it false.
    if (which === 37) {
        OUT = coerce(0);
    }
    if (which === 38) {
        OUT = keysOf(0, {
            x: 1,
            y: 2
        }) + "/" + keysOf(0, [7, 8]) + "/" + keysOf(0, 5);
    }
    if (which === 39) {
        OUT = dropNamed(0);
    }
    if (which === 40) {
        OUT = bigLits(0);
    }
    if (which === 41) {
        OUT = howMany(1, 2, 3, 4) + "/" + howMany(1) + "/" + sumAll(5, 6, 7);
    }
    if (which === 42) {
        OUT = restOf(1, 2, 3) + "/" + restOf(1) + "/" + bothOf(1, 2, 3);
    }
    // Arms 50 and 51 cover async functions. The numbers are only labels the C++
    // driver passes to `drive`, so the gap after 42 is harmless; keep each
    // arm's number stable, because Differential.cpp names its cases by number
    // and expects each answer under that number.
    //
    // typeof is in the answer because "object" is what separates a promise from
    // the number 2. A lowering that skipped the wrap entirely would answer
    // "number/2/undefined", with the __value field looking almost right.
    if (which === 50) {
        var p = wrapped(1);
        OUT = "" + (typeof p) + "/" + p.__value + "/" + p.__settled + "/" + p.__rejected;
    }
    // The two shape questions in one line. `passes(p) === p` is the
    // already-a-promise test; `arrayOut().__value[0]` is the exactness of
    // is_object(), because an array must be re-wrapped and not passed through.
    if (which === 51) {
        var q = wrapped(2);
        OUT = "" + (passes(q) === q) + "/" + arrayOut().__value[0] + "/" +
            (typeof arrayOut().__value);
    }
    if (which === 22) {
        OUT = "" + coalesce(0, 9) + "/" + coalesce("", 9) + "/" + coalesce(nothing, 9) + "/" +
            chain(null) + "/" + chain({
                p: 4
            }) + "/" + dflt(0);
    }
}
