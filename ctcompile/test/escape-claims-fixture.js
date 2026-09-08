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

// --- COMPLETE OWN-ARRAY RETENTION THROUGH REAL IMPORTED FRAMES ------------
// Empty objects avoid named-field operations outside the complete subset.
// Every case executes; returned containers/read values stay in H. The checker
// joins each observed site to its independent claim by program/function/pc.
function arrayFramePrivate() { var child = {}; var container = [child]; return 0; }
arrayFramePrivate(); arrayFramePrivate();
function arrayFrameReturned() { var child = {}; var container = [child]; return container; }
H.push(arrayFrameReturned());
function arrayFrameSavedRead() {
    var child = {}, replacement = {};
    var container = [child], saved = container[0];
    container[0] = replacement;
    return saved;
}
H.push(arrayFrameSavedRead());
function arrayFrameOverwrite() {
    var child = {}, replacement = {};
    var container = [child];
    container[0] = replacement;
    return container;
}
H.push(arrayFrameOverwrite());
function arrayFrameLoadedAlias() {
    var child = {}, inner = [child], outer = [inner];
    var alias = outer[0];
    alias[0] = 0;
    return outer;
}
H.push(arrayFrameLoadedAlias());
// A late publication or call still invalidates the complete query, even when
// the child's first legacy reason is Stored through its local container.
function arrayFramePublished() { var child = {}; var container = [child]; G = container; return 0; }
arrayFramePublished();
function arrayFrameCall() { var child = {}; var container = [child]; hold(container); return 0; }
arrayFrameCall();
// Final and transient cycles both preserve Stored; this increment chooses no
// graph owner even when the oracle observes every instance confined at exit.
function arrayFrameCycle() { var container = [null]; container[0] = container; return 0; }
arrayFrameCycle();
function arrayFrameTransientCycle() {
    var container = [null];
    container[0] = container;
    container[0] = null;
    return 0;
}
arrayFrameTransientCycle();

// --- OWN-OBJECT DELETION THROUGH REAL IMPORTED FRAMES ---------------------
// Erasing a field releases its child only when no saved value retains it.
// Named and computed deletion share this boundary; historical cycle edges
// remain conservative even when deletion empties the final object.
function objectFrameDeletedChild() {
    var child = {}, container = { child: child };
    delete container.child;
    return container;
}
H.push(objectFrameDeletedChild());
function objectFrameDeletedSavedRead() {
    var child = {}, container = { child: child }, key = "child";
    var saved = container[key];
    delete container[key];
    return saved;
}
H.push(objectFrameDeletedSavedRead());
function objectFrameDeletedTransientCycle() {
    var container = {};
    container.self = container;
    delete container.self;
    return 0;
}
objectFrameDeletedTransientCycle();

// --- OWN-DATA OBJECT COPIES THROUGH REAL IMPORTED FRAMES -----------------
// Spread copies the child reference, so erasing the source field cannot
// release a child still reachable through the returned target. Overwrite and
// saved reads distinguish historical copy edges from final retained contents.
function objectFrameCopiedChild() {
    var child = {}, source = { held: child }, target = { ...source };
    delete source.held;
    return target;
}
H.push(objectFrameCopiedChild());
function objectFrameCopiedOverwrite() {
    var old = {}, replacement = {}, source = { held: old }, target = { ...source };
    target.held = replacement;
    delete source.held;
    return target;
}
H.push(objectFrameCopiedOverwrite());
function objectFrameCopiedSavedRead() {
    var child = {}, source = { held: child }, target = { ...source };
    var saved = target.held;
    delete source.held;
    delete target.held;
    return saved;
}
H.push(objectFrameCopiedSavedRead());

// --- OWN-DATA COPIES ACROSS EXECUTED CONDITIONAL AND SWITCH PATHS ---------
// Erasing the selected alias must leave the other object's copied child
// reachable. Both flags execute, so updating both possible targets would
// incorrectly release a child on a path the oracle actually observes. The
// replacement dies on both paths: unused raw predicate registers may forward
// without lending an origin to any unknown root, field or return value.
function objectFrameCopiedConditionalAlias(selectSource) {
    var child = { id: 1 }, replacement = { id: 2 };
    var source = { held: child }, target = { ...source };
    var selected = selectSource ? source : target;
    selected.held = replacement;
    delete selected.held;
    return { source: source, target: target };
}
H.push(objectFrameCopiedConditionalAlias(true));
H.push(objectFrameCopiedConditionalAlias(false));

// Each child is retained once and confined once. Copying the wrong source,
// or sharing its later deletion with the target, loses a real returned child.
function objectFrameCopiedConditionalSource(selectFirst) {
    var left = { id: 1 }, right = { id: 2 };
    var first = { held: left }, second = { held: right };
    var source = selectFirst ? first : second;
    var target = { ...source };
    delete first.held;
    delete second.held;
    return target;
}
H.push(objectFrameCopiedConditionalSource(true));
H.push(objectFrameCopiedConditionalSource(false));

// All cases and default execute. The saved child survives overwrite and
// deletion, while only the branch's returned source/target retains the new
// child. A saved read recomputed from a later field would change this graph.
function objectFrameCopiedSwitchSaved(choice) {
    var child = { id: 1 }, replacement = { id: 2 };
    var source = { held: child }, target = { ...source };
    var saved = target.held;
    switch (choice) {
    case 0:
        delete source.held;
        target.held = replacement;
        return { saved: saved, target: target };
    case 1:
        source.held = replacement;
        delete target.held;
        return { saved: saved, source: source };
    default:
        delete source.held;
        delete target.held;
        return { saved: saved };
    }
}
H.push(objectFrameCopiedSwitchSaved(0));
H.push(objectFrameCopiedSwitchSaved(1));
H.push(objectFrameCopiedSwitchSaved(2));

// The switch's strict comparisons now have a complete noncapturing proof,
// but every Stored site above remains retained on at least one structural arm.
// This parameter-free control independently releases the old child on every
// arm, including the literal condition's untaken arm.
function objectFrameCopiedLiteralOverwrite() {
    var child = { id: 1 }, replacement = { id: 2 };
    var source = { held: child }, target = { ...source };
    var selected = true ? source : target;
    selected.held = replacement;
    delete source.held;
    delete target.held;
    return replacement;
}
H.push(objectFrameCopiedLiteralOverwrite());

// --- NONCAPTURING SOURCE-SWITCH SELECTORS ----------------------------------
// The old child dies on every arm, while the returned container changes by
// case. String "0" must take default: selector equality never coerces it.
// Keep this family separate from the unchanged copy-path observations above.
function objectFrameSwitchReleased(choice) {
    var child = { id: 1 };
    var source = { held: child, side: "source" }, target = { ...source };
    target.side = "target";
    switch (choice) {
    case 0:
        delete source.held;
        delete target.held;
        return target;
    case 1:
        delete target.held;
        delete source.held;
        return source;
    default:
        delete source.held;
        delete target.held;
        return { side: "default" };
    }
}
H.push(objectFrameSwitchReleased(0));
H.push(objectFrameSwitchReleased(1));
H.push(objectFrameSwitchReleased("0"));

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

// R3 composition: the external alternative must not erase a fresh site's
// escape at a phi or a loop header. Each local site is made twice, confined
// once and retained once; the external alternative is the already-global EXT.
function globalAliasJoin(selectLocal) {
    var local = { value: 11 };
    var alias = selectLocal ? local : EXT;
    G = alias;
}
globalAliasJoin(true); globalAliasJoin(false);
function globalAliasLoop(replaceWithExternal) {
    var local = { value: 12 }, alias = local;
    for (var i = 0; i < 1; i++) {
        if (replaceWithExternal) { alias = EXT; }
    }
    G = alias;
}
globalAliasLoop(true); globalAliasLoop(false);

// The inner site escapes by Stored and the outer by StoredGlobal. Neither
// object is returned or handed to a callee: globals is the only retaining root.
function globalRetainedContainer() {
    var child = { value: 13 };
    var outer = { child: child };
    G = outer;
}
globalRetainedContainer();

// Replacing the original global cannot revoke publication: another alias
// loaded from that global still keeps the object alive through an external base.
function globalReplacedPublication() {
    var local = { value: 14 };
    G = local;
    EXT.retained = G;
    G = null;
}
globalReplacedPublication();

// --- CAPTURED by a closure that outlives the frame ------------------------
function captured() { var o = { c: 1 }; return function () { return o.c; }; }
H.push(captured());

// --- PASSED to a callee that keeps it -------------------------------------
function hold(x) { H.push(x); }
function passed() { var o = { p: 1 }; hold(o); }
passed();
function passedAsReceiver() { var a = [1]; a.push(2); H.push(a); }   // `a.push` is a call with `a` as receiver
passedAsReceiver();

// Spread copies elements into the callee window without retaining the argument
// array. The object element must escape even though both the source array and
// the compiler's spread argument array stay confined. These are distinct sites,
// and the oracle checks their lifetimes independently of the compiler claims.
function spreadRetainedCall() {
    var child = { value: 15 };
    var args = [child];
    hold(...args);
}
spreadRetainedCall();
function SpreadRetainer(child) { H.push(child); }
function spreadRetainedConstruct() {
    var child = { value: 16 };
    var args = [child];
    new SpreadRetainer(...args);
}
spreadRetainedConstruct();

// The receiver is a separate sink even on a spread call. Its empty source and
// argument arrays remain confined while the actual receiver is kept globally.
function keepSpreadReceiver() { H.push(this); }
function spreadRetainedReceiver() {
    var receiver = { keep: keepSpreadReceiver };
    receiver.keep(...[]);
}
spreadRetainedReceiver();

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
