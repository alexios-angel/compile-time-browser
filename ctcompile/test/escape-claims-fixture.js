// PHASE 55'S FIXTURE: one function per row of the sinks-and-carriers table,
// every one called, and EVERY SINK ROUTE FORCED TO RETAIN.
//
// The oracle observes RETENTION at frame exit - whether an object born in a
// frame is still reachable from a GC root after that frame returned - and
// nothing else. So a sink that merely hands an object to something which drops
// it again reads "confined" to the oracle even though the analysis rightly
// calls it an escape (the `transit` row below pins that blind spot on
// purpose). Every other row therefore makes the sunk object REACHABLE from
// `H`, a global, so the recorder sees `escaped` and the claim `escapes:<r>`
// is EXACT rather than merely IMPRECISE. A row whose retention is through a
// getter or a valueOf is the justification of the `converted` and
// `accessor_defined` rows made concrete: user code ran with the object as
// `this` and kept it.
//
// The confined rows are the MVP cut line of 25-escape-analysis.md §6: an
// object or array literal used only through property access, index, `in`,
// `delete`, `===`, `!`, `typeof`, `instanceof` and `for...of` in its own frame.

var H = [];        // the retention sink: anything pushed here is reachable from globals
var G = null;      // a plain global slot
var EXT = {};      // an external object to store into

// --- CONFINED: what the MVP proves ----------------------------------------
function confinedObject() { var p = { x: 1, y: 2 }; p.x += 3; return p.x * p.y; }
function confinedArray() { var a = [1, 2, 3]; var s = 0; for (var i = 0; i < a.length; i++) { s += a[i]; } return s; }
function confinedForOf() { var s = 0; for (var x of [4, 5, 6]) { s += x; } return s; }
function confinedPredicates() {
    var o = { k: 1 }, q = { k: 1 };
    var has = "k" in o, same = o === q, self = o === o, neg = !o, ty = typeof o, inst = o instanceof Object;
    delete o.k;
    return [has, same, self, neg, ty, inst, "k" in o];
}
function confinedLoopCarried(n) {
    var o = { v: 0 };
    for (var i = 0; i < n; i++) { o.v = o.v + i; }   // carried round the back edge as a block argument
    return o.v;
}
confinedObject(); confinedArray(); confinedForOf(); confinedPredicates(); confinedLoopCarried(5);

// --- RETURNED --------------------------------------------------------------
function returned() { return { r: 1 }; }
H.push(returned());
function returnedArray() { return [1]; }
H.push(returnedArray());

// --- THROWN ----------------------------------------------------------------
function thrown() { throw { e: 1 }; }
function catchAndKeep() { try { thrown(); } catch (e) { H.push(e); } }
catchAndKeep();

// --- STORED into something external / into an array literal --------------
function storedIntoExternal() { EXT.held = { s: 1 }; }
storedIntoExternal();
function storedIntoArrayLiteral() { var o = { s: 2 }; H.push([o]); }
storedIntoArrayLiteral();
function storedByIndex() { var o = { s: 3 }; H[H.length] = o; }
storedByIndex();

// --- STORED GLOBAL ---------------------------------------------------------
function storedGlobal() { G = { g: 1 }; }
storedGlobal();

// --- CAPTURED by a closure that outlives the frame ------------------------
function captured() { var o = { c: 1 }; return function () { return o.c; }; }
H.push(captured());

// --- PASSED to a callee that keeps it -------------------------------------
function hold(x) { H.push(x); }
function passed() { var o = { p: 1 }; hold(o); }
passed();
function passedAsReceiver() { var a = [1]; a.push(2); H.push(a); }   // `a.push` is a call with `a` as receiver
passedAsReceiver();

// --- THE PINNED BLIND SPOT: passed, but the callee dropped it -------------
function ident(x) { return x; }
function transit() { var o = { t: 1 }; ident(o); return 1; }   // analysis: escapes:passed; oracle: confined
transit();

// --- CONVERTED: ToPrimitive reaches Object.prototype, which the page owns --
Object.prototype.valueOf = function () { H.push(this); return 1; };
function converted() { var o = { v: 1 }; return o + 1; }
converted();
function convertedCompare() { var o = { v: 1 }; return o < 2; }
convertedCompare();
function convertedKey() { var k = { v: 1 }; var t = {}; t[k] = 1; return t; }   // the KEY's toString runs
convertedKey();

// --- ACCESSOR DEFINED on the object itself --------------------------------
function accessorDefined() { var o = { get x() { H.push(this); return 1; } }; return o.x; }
accessorDefined();

// --- PROTO MUTATED ---------------------------------------------------------
function protoMutated() {
    var o = { m: 1 };
    Object.setPrototypeOf(o, { get y() { H.push(this); return 2; } });
    return o.y;
}
protoMutated();

// --- ARGUMENTS: the site stays confined, the arguments array is boxed ------
function usesArguments(a) { var o = { a: 1 }; return arguments.length + o.a; }
usesArguments(1, 2, 3);
function usesRest() { var o = { a: 1 }; return o.a; }
usesRest(1, 2);

// --- RUNTIME ARRAYS: own_keys and iterable's fresh arrays -----------------
function ownKeys() { var o = { a: 1, b: 2 }; var n = 0; for (var k in o) { n++; } return n; }
ownKeys();

// --- CELLS AND CLOSURES USED LOCALLY: Phase 59's backlog, pinned ----------
function localClosure() { var n = { v: 0 }; var f = function () { return n.v++; }; return f() + f(); }
localClosure();

// --- CYCLES: refused by construction (every store is a sink) --------------
function cycleConfined() { var a = { b: null }, b = { a: null }; a.b = b; b.a = a; return 0; }
cycleConfined();
function cycleLeaked() { var a = { b: null }, b = { a: null }; a.b = b; b.a = a; return a; }
H.push(cycleLeaked());

// --- ENTERED TWICE: made must count 2 -------------------------------------
function twice() { var o = { n: 1 }; return o.n; }
twice(); twice();

// --- NEVER CALLED: no observation, never sound ----------------------------
function neverCalled() { var o = { z: 1 }; return o; }

// --- SUSPENDED: the importer refuses the whole function -------------------
async function suspended() { var o = { s: 1 }; await 0; return o; }
suspended();
