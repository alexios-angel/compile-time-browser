
// --- PROVED BIGINT PLUS ERRORS: independent payload, original categories -----
// Preserve the three measured continuation bodies verbatim. The opaque case
// must remain conservative even when its observed actuals are Number/BigInt.
function primitivePlusEarly(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? 1n : 1;
    var numeric = +input;
    delete source.held; delete target.held;
    return { source: source, target: target, numeric: numeric };
}
function primitivePlusRetained(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source }, saved = target.held;
    var input = choice ? 1n : 1;
    var numeric = +input;
    delete source.held; delete target.held;
    return { source: source, target: target, saved: saved, numeric: numeric };
}
function primitivePlusOpaque(input) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var numeric = +input;
    delete source.held; delete target.held;
    return { source: source, target: target, numeric: numeric };
}
var primitivePlusNormal = primitivePlusEarly(false);
var primitivePlusSaved = primitivePlusRetained(false);
var primitivePlusUnknown = primitivePlusOpaque(1);
H.push(primitivePlusNormal); H.push(primitivePlusSaved); H.push(primitivePlusUnknown);
function primitivePlusCatch(fn, input) {
    try { return fn(input); }
    catch (error) { H.push(error); return error; }
}
var primitivePlusError = primitivePlusCatch(primitivePlusEarly, true);
var primitivePlusSavedError = primitivePlusCatch(primitivePlusRetained, true);
var primitivePlusUnknownError = primitivePlusCatch(primitivePlusOpaque, 1n);
var primitivePlusTrace = 0;
if (primitivePlusNormal.numeric === 1 && primitivePlusNormal.source.held === void 0 &&
    primitivePlusNormal.target.held === void 0) primitivePlusTrace += 1;
if (primitivePlusError instanceof TypeError && primitivePlusError.name === "TypeError") primitivePlusTrace += 2;
if (primitivePlusSaved.numeric === 1 && primitivePlusSaved.saved.id === 1 &&
    primitivePlusSaved.source.held === void 0 && primitivePlusSaved.target.held === void 0) primitivePlusTrace += 4;
if (primitivePlusSavedError instanceof TypeError && primitivePlusSavedError !== primitivePlusError) primitivePlusTrace += 8;
if (primitivePlusUnknown.numeric === 1) primitivePlusTrace += 16;
if (primitivePlusUnknownError instanceof TypeError && primitivePlusUnknownError !== primitivePlusError &&
    primitivePlusUnknownError !== primitivePlusSavedError &&
    typeof primitivePlusError.message === "string" &&
    typeof primitivePlusSavedError.stack === "string") primitivePlusTrace += 32;
if (primitivePlusTrace !== 63) throw "BigInt unary Plus independent TypeError witness";

// --- MIXED BIGINT SUB: INDEPENDENT TYPEERROR, ORIGINAL OPERANDS -----------
function primitiveMixedSubEarly(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? 1n : 1;
    var numeric = input - 0;
    delete source.held; delete target.held;
    return { source: source, target: target, numeric: numeric };
}
function primitiveMixedSubRetained(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source }, saved = target.held;
    var input = choice ? 1n : 1;
    var numeric = input - 0;
    delete source.held; delete target.held;
    return { source: source, target: target, saved: saved, numeric: numeric };
}
function primitiveMixedSubOpaque(input) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var numeric = input - 0;
    delete source.held; delete target.held;
    return { source: source, target: target, numeric: numeric };
}
var primitiveMixedSubNormal = primitiveMixedSubEarly(false);
var primitiveMixedSubSaved = primitiveMixedSubRetained(false);
var primitiveMixedSubUnknown = primitiveMixedSubOpaque(1);
H.push(primitiveMixedSubNormal); H.push(primitiveMixedSubSaved); H.push(primitiveMixedSubUnknown);
function primitiveMixedSubCatch(fn, input) {
    try { return fn(input); }
    catch (error) { H.push(error); return error; }
}
var primitiveMixedSubError = primitiveMixedSubCatch(primitiveMixedSubEarly, true);
var primitiveMixedSubSavedError = primitiveMixedSubCatch(primitiveMixedSubRetained, true);
var primitiveMixedSubUnknownError = primitiveMixedSubCatch(primitiveMixedSubOpaque, 1n);
var primitiveMixedSubTrace = 0;
if (primitiveMixedSubNormal.numeric === 1 && primitiveMixedSubNormal.source.held === void 0 &&
    primitiveMixedSubNormal.target.held === void 0) primitiveMixedSubTrace += 1;
if (primitiveMixedSubError instanceof TypeError && primitiveMixedSubError.name === "TypeError") primitiveMixedSubTrace += 2;
if (primitiveMixedSubSaved.numeric === 1 && primitiveMixedSubSaved.saved.id === 1 &&
    primitiveMixedSubSaved.source.held === void 0 && primitiveMixedSubSaved.target.held === void 0) primitiveMixedSubTrace += 4;
if (primitiveMixedSubSavedError instanceof TypeError && primitiveMixedSubSavedError !== primitiveMixedSubError) primitiveMixedSubTrace += 8;
if (primitiveMixedSubUnknown.numeric === 1) primitiveMixedSubTrace += 16;
if (primitiveMixedSubUnknownError instanceof TypeError && primitiveMixedSubUnknownError !== primitiveMixedSubError &&
    primitiveMixedSubUnknownError !== primitiveMixedSubSavedError &&
    typeof primitiveMixedSubError.message === "string" &&
    typeof primitiveMixedSubSavedError.stack === "string") primitiveMixedSubTrace += 32;
if (primitiveMixedSubTrace !== 63) throw "mixed BigInt subtraction independent TypeError witness";

// --- MIXED BIGINT MUL: INDEPENDENT TYPEERROR, ORIGINAL OPERANDS -----------
function primitiveMixedMulEarly(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? 1n : 1;
    var numeric = input * 1;
    delete source.held; delete target.held;
    return { source: source, target: target, numeric: numeric };
}
function primitiveMixedMulRetained(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source }, saved = target.held;
    var input = choice ? 1n : 1;
    var numeric = input * 1;
    delete source.held; delete target.held;
    return { source: source, target: target, saved: saved, numeric: numeric };
}
function primitiveMixedMulOpaque(input) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var numeric = input * 1;
    delete source.held; delete target.held;
    return { source: source, target: target, numeric: numeric };
}
var primitiveMixedMulNormal = primitiveMixedMulEarly(false);
var primitiveMixedMulSaved = primitiveMixedMulRetained(false);
var primitiveMixedMulUnknown = primitiveMixedMulOpaque(1);
H.push(primitiveMixedMulNormal); H.push(primitiveMixedMulSaved); H.push(primitiveMixedMulUnknown);
function primitiveMixedMulCatch(fn, input) {
    try { return fn(input); }
    catch (error) { H.push(error); return error; }
}
var primitiveMixedMulError = primitiveMixedMulCatch(primitiveMixedMulEarly, true);
var primitiveMixedMulSavedError = primitiveMixedMulCatch(primitiveMixedMulRetained, true);
var primitiveMixedMulUnknownError = primitiveMixedMulCatch(primitiveMixedMulOpaque, 1n);
var primitiveMixedMulTrace = 0;
if (primitiveMixedMulNormal.numeric === 1 && primitiveMixedMulNormal.source.held === void 0 &&
    primitiveMixedMulNormal.target.held === void 0) primitiveMixedMulTrace += 1;
if (primitiveMixedMulError instanceof TypeError && primitiveMixedMulError.name === "TypeError") primitiveMixedMulTrace += 2;
if (primitiveMixedMulSaved.numeric === 1 && primitiveMixedMulSaved.saved.id === 1 &&
    primitiveMixedMulSaved.source.held === void 0 && primitiveMixedMulSaved.target.held === void 0) primitiveMixedMulTrace += 4;
if (primitiveMixedMulSavedError instanceof TypeError && primitiveMixedMulSavedError !== primitiveMixedMulError) primitiveMixedMulTrace += 8;
if (primitiveMixedMulUnknown.numeric === 1) primitiveMixedMulTrace += 16;
if (primitiveMixedMulUnknownError instanceof TypeError && primitiveMixedMulUnknownError !== primitiveMixedMulError &&
    primitiveMixedMulUnknownError !== primitiveMixedMulSavedError &&
    typeof primitiveMixedMulError.message === "string" &&
    typeof primitiveMixedMulSavedError.stack === "string") primitiveMixedMulTrace += 32;
if (primitiveMixedMulTrace !== 63) throw "mixed BigInt multiplication independent TypeError witness";

// --- MIXED BIGINT DIV: INDEPENDENT TYPEERROR, ORIGINAL OPERANDS -----------
function primitiveMixedDivEarly(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? 1n : 1;
    var numeric = input / 1;
    delete source.held; delete target.held;
    return { source: source, target: target, numeric: numeric };
}
function primitiveMixedDivRetained(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source }, saved = target.held;
    var input = choice ? 1n : 1;
    var numeric = input / 1;
    delete source.held; delete target.held;
    return { source: source, target: target, saved: saved, numeric: numeric };
}
function primitiveMixedDivOpaque(input) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var numeric = input / 1;
    delete source.held; delete target.held;
    return { source: source, target: target, numeric: numeric };
}
var primitiveMixedDivNormal = primitiveMixedDivEarly(false);
var primitiveMixedDivSaved = primitiveMixedDivRetained(false);
var primitiveMixedDivUnknown = primitiveMixedDivOpaque(1);
H.push(primitiveMixedDivNormal); H.push(primitiveMixedDivSaved); H.push(primitiveMixedDivUnknown);
function primitiveMixedDivCatch(fn, input) {
    try { return fn(input); }
    catch (error) { H.push(error); return error; }
}
var primitiveMixedDivError = primitiveMixedDivCatch(primitiveMixedDivEarly, true);
var primitiveMixedDivSavedError = primitiveMixedDivCatch(primitiveMixedDivRetained, true);
var primitiveMixedDivUnknownError = primitiveMixedDivCatch(primitiveMixedDivOpaque, 1n);
var primitiveMixedDivTrace = 0;
if (primitiveMixedDivNormal.numeric === 1 && primitiveMixedDivNormal.source.held === void 0 &&
    primitiveMixedDivNormal.target.held === void 0) primitiveMixedDivTrace += 1;
if (primitiveMixedDivError instanceof TypeError && primitiveMixedDivError.name === "TypeError") primitiveMixedDivTrace += 2;
if (primitiveMixedDivSaved.numeric === 1 && primitiveMixedDivSaved.saved.id === 1 &&
    primitiveMixedDivSaved.source.held === void 0 && primitiveMixedDivSaved.target.held === void 0) primitiveMixedDivTrace += 4;
if (primitiveMixedDivSavedError instanceof TypeError && primitiveMixedDivSavedError !== primitiveMixedDivError) primitiveMixedDivTrace += 8;
if (primitiveMixedDivUnknown.numeric === 1) primitiveMixedDivTrace += 16;
if (primitiveMixedDivUnknownError instanceof TypeError && primitiveMixedDivUnknownError !== primitiveMixedDivError &&
    primitiveMixedDivUnknownError !== primitiveMixedDivSavedError &&
    typeof primitiveMixedDivError.message === "string" &&
    typeof primitiveMixedDivSavedError.stack === "string") primitiveMixedDivTrace += 32;
if (primitiveMixedDivTrace !== 63) throw "mixed BigInt division independent TypeError witness";

// --- MIXED BIGINT MOD: INDEPENDENT TYPEERROR, ORIGINAL OPERANDS -----------
function primitiveMixedModEarly(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? 1n : 1;
    var numeric = input % 1;
    delete source.held; delete target.held;
    return { source: source, target: target, numeric: numeric };
}
function primitiveMixedModRetained(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source }, saved = target.held;
    var input = choice ? 1n : 1;
    var numeric = input % 1;
    delete source.held; delete target.held;
    return { source: source, target: target, saved: saved, numeric: numeric };
}
function primitiveMixedModOpaque(input) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var numeric = input % 1;
    delete source.held; delete target.held;
    return { source: source, target: target, numeric: numeric };
}
var primitiveMixedModNormal = primitiveMixedModEarly(false);
var primitiveMixedModSaved = primitiveMixedModRetained(false);
var primitiveMixedModUnknown = primitiveMixedModOpaque(1);
H.push(primitiveMixedModNormal); H.push(primitiveMixedModSaved); H.push(primitiveMixedModUnknown);
function primitiveMixedModCatch(fn, input) {
    try { return fn(input); }
    catch (error) { H.push(error); return error; }
}
var primitiveMixedModError = primitiveMixedModCatch(primitiveMixedModEarly, true);
var primitiveMixedModSavedError = primitiveMixedModCatch(primitiveMixedModRetained, true);
var primitiveMixedModUnknownError = primitiveMixedModCatch(primitiveMixedModOpaque, 1n);
var primitiveMixedModTrace = 0;
if (primitiveMixedModNormal.numeric === 0 && primitiveMixedModNormal.source.held === void 0 &&
    primitiveMixedModNormal.target.held === void 0) primitiveMixedModTrace += 1;
if (primitiveMixedModError instanceof TypeError && primitiveMixedModError.name === "TypeError") primitiveMixedModTrace += 2;
if (primitiveMixedModSaved.numeric === 0 && primitiveMixedModSaved.saved.id === 1 &&
    primitiveMixedModSaved.source.held === void 0 && primitiveMixedModSaved.target.held === void 0) primitiveMixedModTrace += 4;
if (primitiveMixedModSavedError instanceof TypeError && primitiveMixedModSavedError !== primitiveMixedModError) primitiveMixedModTrace += 8;
if (primitiveMixedModUnknown.numeric === 0) primitiveMixedModTrace += 16;
if (primitiveMixedModUnknownError instanceof TypeError && primitiveMixedModUnknownError !== primitiveMixedModError &&
    primitiveMixedModUnknownError !== primitiveMixedModSavedError &&
    typeof primitiveMixedModError.message === "string" &&
    typeof primitiveMixedModSavedError.stack === "string") primitiveMixedModTrace += 32;
if (primitiveMixedModTrace !== 63) throw "mixed BigInt remainder independent TypeError witness";

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

// --- BIGINT UNSIGNED SHIFT: independent TypeError, dense array retention ---
function primitiveUShrEarly(choice) {
    var child = {}, items = [child], value, shift;
    if (choice) { value = 8n; shift = 1n; }
    else { value = 8; shift = 1; }
    var result = value >>> shift;
    items[0] = result;
    return items;
}
function primitiveUShrRetained(choice) {
    var child = {}, items = [child], saved = items[0], value, shift;
    if (choice) { value = 8n; shift = 1n; }
    else { value = 8; shift = 1; }
    var result = value >>> shift;
    items[0] = result;
    return saved;
}
function primitiveUShrOpaque(value, shift) {
    var child = {}, items = [child];
    var result = value >>> shift;
    items[0] = result;
    return items;
}
function primitiveUShrCatch(fn, input, shift) {
    try { return fn(input, shift); }
    catch (error) { H.push(error); return error; }
}
var primitiveUShrNormal = primitiveUShrEarly(false);
var primitiveUShrSaved = primitiveUShrRetained(false);
var primitiveUShrUnknown = primitiveUShrOpaque(8, 1);
H.push(primitiveUShrNormal); H.push(primitiveUShrSaved); H.push(primitiveUShrUnknown);
var primitiveUShrError = primitiveUShrCatch(primitiveUShrEarly, true);
var primitiveUShrSavedError = primitiveUShrCatch(primitiveUShrRetained, true);
var primitiveUShrUnknownError = primitiveUShrCatch(primitiveUShrOpaque, 8n, 1n);
if (primitiveUShrNormal[0] !== 4 || typeof primitiveUShrNormal[0] !== "number" ||
    typeof primitiveUShrSaved !== "object" || Array.isArray(primitiveUShrSaved) ||
    primitiveUShrUnknown[0] !== 4 || typeof primitiveUShrUnknown[0] !== "number" ||
    !(primitiveUShrError instanceof TypeError) || primitiveUShrError.name !== "TypeError" ||
    !(primitiveUShrSavedError instanceof TypeError) ||
    !(primitiveUShrUnknownError instanceof TypeError) ||
    primitiveUShrError === primitiveUShrSavedError || primitiveUShrError === primitiveUShrUnknownError ||
    primitiveUShrSavedError === primitiveUShrUnknownError ||
    typeof primitiveUShrError.message !== "string" ||
    typeof primitiveUShrSavedError.stack !== "string") throw "BigInt unsigned shift independent TypeError witness";

// --- MIXED STATIC BIGINT: independent TypeError, original dense operands ---
function primitiveMixedStaticEarly(choice) {
    var child = {}, items = [child], value, shift;
    if (choice) { value = 8n; shift = 1; }
    else { value = 8; shift = 1; }
    var result = value >> shift;
    items[0] = result;
    return items;
}
function primitiveMixedStaticRetained(choice) {
    var child = {}, items = [child], saved = items[0], value, shift;
    if (choice) { value = 8; shift = 1n; }
    else { value = 8; shift = 1; }
    var result = value >> shift;
    items[0] = result;
    return saved;
}
function primitiveMixedStaticOpaque(value, shift) {
    var child = {}, items = [child];
    var result = value >> shift;
    items[0] = result;
    return items;
}
var primitiveMixedStaticNormal = primitiveMixedStaticEarly(false);
var primitiveMixedStaticSaved = primitiveMixedStaticRetained(false);
var primitiveMixedStaticUnknown = primitiveMixedStaticOpaque(8, 1);
H.push(primitiveMixedStaticNormal); H.push(primitiveMixedStaticSaved); H.push(primitiveMixedStaticUnknown);
var primitiveMixedStaticError = primitiveUShrCatch(primitiveMixedStaticEarly, true);
var primitiveMixedStaticSavedError = primitiveUShrCatch(primitiveMixedStaticRetained, true);
var primitiveMixedStaticUnknownError = primitiveUShrCatch(primitiveMixedStaticOpaque, 8n, 1);
if (primitiveMixedStaticNormal[0] !== 4 || typeof primitiveMixedStaticNormal[0] !== "number" ||
    typeof primitiveMixedStaticSaved !== "object" || Array.isArray(primitiveMixedStaticSaved) ||
    primitiveMixedStaticUnknown[0] !== 4 || typeof primitiveMixedStaticUnknown[0] !== "number" ||
    !(primitiveMixedStaticError instanceof TypeError) || primitiveMixedStaticError.name !== "TypeError" ||
    !(primitiveMixedStaticSavedError instanceof TypeError) ||
    !(primitiveMixedStaticUnknownError instanceof TypeError) ||
    primitiveMixedStaticError === primitiveMixedStaticSavedError ||
    primitiveMixedStaticError === primitiveMixedStaticUnknownError ||
    primitiveMixedStaticSavedError === primitiveMixedStaticUnknownError ||
    typeof primitiveMixedStaticError.message !== "string" ||
    typeof primitiveMixedStaticSavedError.stack !== "string") throw "mixed static BigInt independent TypeError witness";

// --- MIXED DYNAMIC BIGINT ADD: independent TypeError, original dense operands ---
function primitiveMixedAddEarly(choice) {
    var child = {}, items = [child], value, shift;
    if (choice) { value = 8n; shift = 1; }
    else { value = 8; shift = 1; }
    var result = value + shift;
    items[0] = result;
    return items;
}
function primitiveMixedAddRetained(choice) {
    var child = {}, items = [child], saved = items[0], value, shift;
    if (choice) { value = 8; shift = 1n; }
    else { value = 8; shift = 1; }
    var result = value + shift;
    items[0] = result;
    return saved;
}
function primitiveMixedAddOpaque(value, shift) {
    var child = {}, items = [child];
    var result = value + shift;
    items[0] = result;
    return items;
}
var primitiveMixedAddNormal = primitiveMixedAddEarly(false);
var primitiveMixedAddSaved = primitiveMixedAddRetained(false);
var primitiveMixedAddUnknown = primitiveMixedAddOpaque(8, 1);
H.push(primitiveMixedAddNormal); H.push(primitiveMixedAddSaved); H.push(primitiveMixedAddUnknown);
var primitiveMixedAddError = primitiveUShrCatch(primitiveMixedAddEarly, true);
var primitiveMixedAddSavedError = primitiveUShrCatch(primitiveMixedAddRetained, true);
var primitiveMixedAddUnknownError = primitiveUShrCatch(primitiveMixedAddOpaque, 8n, 1);
if (primitiveMixedAddNormal[0] !== 9 || typeof primitiveMixedAddNormal[0] !== "number" ||
    typeof primitiveMixedAddSaved !== "object" || Array.isArray(primitiveMixedAddSaved) ||
    primitiveMixedAddUnknown[0] !== 9 || typeof primitiveMixedAddUnknown[0] !== "number" ||
    !(primitiveMixedAddError instanceof TypeError) || primitiveMixedAddError.name !== "TypeError" ||
    !(primitiveMixedAddSavedError instanceof TypeError) ||
    !(primitiveMixedAddUnknownError instanceof TypeError) ||
    primitiveMixedAddError === primitiveMixedAddSavedError ||
    primitiveMixedAddError === primitiveMixedAddUnknownError ||
    primitiveMixedAddSavedError === primitiveMixedAddUnknownError ||
    typeof primitiveMixedAddError.message !== "string" ||
    typeof primitiveMixedAddSavedError.stack !== "string") throw "mixed dynamic BigInt Add independent TypeError witness";

// --- CANONICAL STRING INDICES: exact dense slots, saved identity, lookalikes ---
function canonicalStringReleased() {
    var child = {}, items = [child];
    items["0"] = 9;
    return items;
}
function canonicalStringSaved() {
    var child = {}, items = [child], saved = items["0"];
    items["0"] = 9;
    return saved;
}
function canonicalStringLookalike() {
    var child = {}, items = [child];
    items["00"] = 9;
    return items;
}
function canonicalStringLoaded() {
    var child = {}, items = [child], keys = ["0"];
    var key = keys[0], saved = items[key];
    items[key] = 9;
    return [items, saved === child];
}
var canonicalStringNormal = canonicalStringReleased();
var canonicalStringRetained = canonicalStringSaved();
var canonicalStringNamed = canonicalStringLookalike();
var canonicalStringForwarded = canonicalStringLoaded();
H.push(canonicalStringNormal); H.push(canonicalStringRetained);
H.push(canonicalStringNamed); H.push(canonicalStringForwarded);
if (canonicalStringNormal[0] !== 9 || typeof canonicalStringNormal[0] !== "number" ||
    typeof canonicalStringRetained !== "object" || canonicalStringRetained === null ||
    Array.isArray(canonicalStringRetained) ||
    typeof canonicalStringNamed[0] !== "object" || canonicalStringNamed[0] === null ||
    canonicalStringNamed["00"] !== 9 || canonicalStringNamed.length !== 1 ||
    canonicalStringForwarded[0][0] !== 9 || typeof canonicalStringForwarded[0][0] !== "number" ||
    canonicalStringForwarded[1] !== true) throw "canonical String index and saved identity witness";

// --- DECIMAL BIGINT INDICES: exact dense slots, saved identity, literal origins ---
function decimalBigIntReleased() {
    var child = {}, items = [child];
    items[0n] = 9;
    return items;
}
function decimalBigIntSaved() {
    var child = {}, items = [child], saved = items[0n];
    items[0n] = 9;
    return saved;
}
function decimalBigIntSecond() {
    var child = {}, items = [0, child];
    items[1n] = 9;
    return items;
}
function decimalBigIntLoaded() {
    var child = {}, items = [child], keys = [0n];
    var key = keys[0], saved = items[key];
    items[key] = 9;
    return [items, saved === child];
}
function decimalBigIntNegative() {
    var child = {}, items = [child];
    items[-1n] = 9;
    return items;
}
var decimalBigIntNormal = decimalBigIntReleased();
var decimalBigIntRetained = decimalBigIntSaved();
var decimalBigIntOne = decimalBigIntSecond();
var decimalBigIntForwarded = decimalBigIntLoaded();
var decimalBigIntNamed = decimalBigIntNegative();
H.push(decimalBigIntNormal); H.push(decimalBigIntRetained); H.push(decimalBigIntOne);
H.push(decimalBigIntForwarded); H.push(decimalBigIntNamed);
if (decimalBigIntNormal[0] !== 9 || typeof decimalBigIntNormal[0] !== "number" ||
    typeof decimalBigIntRetained !== "object" || decimalBigIntRetained === null ||
    Array.isArray(decimalBigIntRetained) ||
    decimalBigIntOne[0] !== 0 || decimalBigIntOne[1] !== 9 || decimalBigIntOne.length !== 2 ||
    decimalBigIntForwarded[0][0] !== 9 || typeof decimalBigIntForwarded[0][0] !== "number" ||
    decimalBigIntForwarded[1] !== true ||
    typeof decimalBigIntNamed[0] !== "object" || decimalBigIntNamed[0] === null ||
    decimalBigIntNamed["-1"] !== 9 || decimalBigIntNamed.length !== 1)
    throw "decimal BigInt index and saved identity witness";
