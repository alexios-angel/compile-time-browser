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

// --- NONCAPTURING LOGICAL NEGATION ----------------------------------------
// A stored negation is a Boolean, not the original opaque input or an alias
// of either container. Both arms release the child before returning their
// distinct identities. Nonempty String "0" distinguishes ! from == false.
function objectFrameNegatedReleased(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var negated = !choice;
    var selected = negated ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target, negated: negated };
}
H.push(objectFrameNegatedReleased(0));
H.push(objectFrameNegatedReleased(1));
H.push(objectFrameNegatedReleased(""));
H.push(objectFrameNegatedReleased("0"));

// --- NONCAPTURING TYPEOF / VOID -------------------------------------------
// Type names contain no reference to the inspected local or external value.
// The undefined, null, Number, Boolean and String cases also distinguish
// typeof from returning the input, a constant type name or a truthiness test.
function objectFrameTypeofReleased(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var named = typeof choice, local = typeof child;
    var selected = named === "undefined" ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target, named: named, local: local };
}
H.push(objectFrameTypeofReleased());
H.push(objectFrameTypeofReleased(null));
H.push(objectFrameTypeofReleased(0));
H.push(objectFrameTypeofReleased(false));
H.push(objectFrameTypeofReleased(""));

// Source void imports as its evaluated assignment plus constant Undefined.
// Keep the selected object's side effect observable in the returned graph;
// discarding the unary result must not discard that prior assignment.
function objectFrameVoidReleased(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var selected = choice ? source : target;
    var discarded = void (selected.mark = 7);
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target, discarded: discarded };
}
H.push(objectFrameVoidReleased(false));
H.push(objectFrameVoidReleased(true));

// --- STATIC BINARY NUMBER PRODUCERS ---------------------------------------
// Every static operand has an independent non-BigInt origin on each path.
// The high-bit input and shift count 33 separate signed/unsigned shifts,
// truncation and the five-bit mask. Source ++ reaches static add, unlike +.
function objectFrameStaticBinaryReleased(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var incremented = choice ? 2147483653 : 12;
    ++incremented;
    var masked = incremented & 7, unioned = incremented | 2, toggled = incremented ^ 5;
    var left = incremented << 33, signed = incremented >> 33, unsigned = incremented >>> 33;
    var selected = masked === 6 ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target, incremented: incremented,
             masked: masked, unioned: unioned, toggled: toggled, left: left,
             signed: signed, unsigned: unsigned };
}
H.push(objectFrameStaticBinaryReleased(false));
H.push(objectFrameStaticBinaryReleased(true));

// Numeric observations cannot prove an opaque formal excludes BigInt. Both
// controls release the child at runtime but retain the conservative Stored
// claim. The BigInt pair also stays outside the Number-only proof, even for
// this operation whose concrete pair succeeds. No object coercion is assumed:
// the VM's static conversion and source JavaScript differ on object inputs.
function objectFrameStaticBinaryOpaque(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var produced = choice | 0;
    var selected = produced === 12 ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target, produced: produced };
}
H.push(objectFrameStaticBinaryOpaque(12));
H.push(objectFrameStaticBinaryOpaque(2147483653));
function objectFrameStaticBinaryBigInt() {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var produced = 1n | 2n;
    delete source.held;
    delete target.held;
    return { source: source, target: target, produced: produced };
}
H.push(objectFrameStaticBinaryBigInt());

// --- ARITHMETIC UNARY PRIMITIVE PRODUCERS ----------------------------------
// Both structural inputs independently exclude objects and BigInt. A trailing
// decimal String, null and the high bit distinguish conversion, signed zero
// and ToInt32 truncation without relying on observed formal argument tags.
function objectFrameArithmeticUnaryReleased(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? "4294967297." : null;
    var negated = -input, numeric = +input, inverted = ~input;
    var selected = inverted === -2 ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             negated: negated, numeric: numeric, inverted: inverted };
}
H.push(objectFrameArithmeticUnaryReleased(false));
H.push(objectFrameArithmeticUnaryReleased(true));

// The input is the old primitive even though its source field now holds a
// BigInt. A fresh contents query must follow the saved SSA read's own origin.
function objectFrameArithmeticUnarySaved(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    source.operand = choice ? "17." : false;
    var saved = source.operand;
    source.operand = 1n;
    var negated = -saved, numeric = +saved, inverted = ~saved;
    var selected = numeric === 17 ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             negated: negated, numeric: numeric, inverted: inverted };
}
H.push(objectFrameArithmeticUnarySaved(false));
H.push(objectFrameArithmeticUnarySaved(true));

// Numeric observations do not prove this formal excludes user conversion or
// BigInt. Successful literal BigInt negation/complement also remain outside
// the independent Number-result proof. Both controls keep Stored claims.
function objectFrameArithmeticUnaryOpaque(input) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var negated = -input, numeric = +input, inverted = ~input;
    delete source.held;
    delete target.held;
    return { source: source, target: target,
             negated: negated, numeric: numeric, inverted: inverted };
}
H.push(objectFrameArithmeticUnaryOpaque(0));
H.push(objectFrameArithmeticUnaryOpaque("4294967297."));
function objectFrameArithmeticUnaryBigInt() {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var negated = -1n, inverted = ~1n;
    delete source.held;
    delete target.held;
    return { source: source, target: target, negated: negated, inverted: inverted };
}
H.push(objectFrameArithmeticUnaryBigInt());

// --- LOOSE EQUALITY FROM INDEPENDENT PRIMITIVE ORIGINS -----------------------
// Check numeric String conversion, nullish equality, operand reversal and !=
// (the importer's Eq followed by Not). This original witness reads global
// undefined, which refuses; the separate Literal repair below proves both origins.
function objectFrameLooseEqualityReleased(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? "17." : null, other = choice ? 18 : undefined;
    var equal = input == other, different = input != other;
    var reversed = other == input, numeric = input == 17, nullZero = null == 0;
    var selected = equal ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             equal: equal, different: different, reversed: reversed,
             numeric: numeric, nullZero: nullZero };
}
H.push(objectFrameLooseEqualityReleased(false));
H.push(objectFrameLooseEqualityReleased(true));

// A later BigInt in the original field does not change either saved operand.
// The unit controls also cover the reverse: a primitive overwrite cannot
// clean an earlier saved object, opaque value or BigInt.
function objectFrameLooseEqualitySaved(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    source.operand = choice ? "17." : false;
    var saved = source.operand;
    source.operand = 1n;
    var equal = saved == 17, reversed = 17 == saved, different = saved != 17;
    var selected = equal ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             equal: equal, reversed: reversed, different: different };
}
H.push(objectFrameLooseEqualitySaved(false));
H.push(objectFrameLooseEqualitySaved(true));

// Successful observations do not prove an opaque formal primitive. Literal
// BigInt comparisons remain outside this bounded family even when successful.
function objectFrameLooseEqualityOpaque(input) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var equal = input == 17, reversed = 17 == input, different = input != 17;
    delete source.held;
    delete target.held;
    return { source: source, target: target,
             equal: equal, reversed: reversed, different: different };
}
H.push(objectFrameLooseEqualityOpaque(0));
H.push(objectFrameLooseEqualityOpaque("17."));
function objectFrameLooseEqualityBigInt() {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var equal = 1n == 1, reversed = 1 == 1n, different = 1n != 2n;
    delete source.held;
    delete target.held;
    return { source: source, target: target,
             equal: equal, reversed: reversed, different: different };
}
H.push(objectFrameLooseEqualityBigInt());

// Relational comparison has an independent whole-frame retention proof.
// The VM still enters to_primitive's depth guard for these primitive operands.
function objectFrameLooseEqualityRelational(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? "17." : 0;
    var less = input < 17, atMost = input <= 17, greater = input > 17, atLeast = input >= 17;
    var selected = less ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             less: less, atMost: atMost, greater: greater, atLeast: atLeast };
}
H.push(objectFrameLooseEqualityRelational(false));
H.push(objectFrameLooseEqualityRelational(true));

// The original Released function above is preserved as a global-lookup refusal:
// bare undefined imports as load_global. This exact repair uses literal void 0,
// which imports as constant Undefined. Both original runtime observations agree.
function objectFrameLooseEqualityLiteral(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? "17." : null, other = choice ? 18 : void 0;
    var equal = input == other, different = input != other;
    var reversed = other == input, numeric = input == 17, nullZero = null == 0;
    var selected = equal ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             equal: equal, different: different, reversed: reversed,
             numeric: numeric, nullZero: nullZero };
}
H.push(objectFrameLooseEqualityLiteral(false));
H.push(objectFrameLooseEqualityLiteral(true));

// Relational operands retain their original primitive identity when their
// own fields are replaced with BigInt and then deleted. String/String order
// differs from numeric order; every result and selected container is observable.
function objectFrameRelationalSaved(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    source.input = choice ? "20" : "3";
    var saved = source.input;
    source.input = 1n;
    delete source.input;
    var less = saved < "3", atMost = saved <= "3";
    var greater = saved > "3", atLeast = saved >= "3", numeric = saved < 3;
    var reversed = "3" < saved, selected = less ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             less: less, atMost: atMost, greater: greater, atLeast: atLeast,
             numeric: numeric, reversed: reversed };
}
H.push(objectFrameRelationalSaved(false));
H.push(objectFrameRelationalSaved(true));

// Undefined and an invalid numeric String both produce unordered comparison,
// so <= and >= cannot be implemented as negated > and <. All structural arms
// still need a complete contents proof despite both observed selectors false.
function objectFrameRelationalUnordered(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? "not-a-number" : void 0, other = choice ? 1 : 0;
    var less = input < other, atMost = input <= other;
    var greater = input > other, atLeast = input >= other;
    var reversed = other <= input, selected = less ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             less: less, atMost: atMost, greater: greater, atLeast: atLeast,
             reversed: reversed };
}
H.push(objectFrameRelationalUnordered(false));
H.push(objectFrameRelationalUnordered(true));

// Executing primitive actuals cannot prove an opaque formal for future calls.
function objectFrameRelationalOpaque(input) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var less = input < 17, atMost = input <= 17;
    var greater = input > 17, atLeast = input >= 17;
    var selected = less ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             less: less, atMost: atMost, greater: greater, atLeast: atLeast };
}
H.push(objectFrameRelationalOpaque(0));
H.push(objectFrameRelationalOpaque("17."));

// Successful BigInt comparisons remain outside the non-BigInt origin proof.
function objectFrameRelationalBigInt(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? 2n : 1n;
    var less = input < 2n, atMost = input <= 2n;
    var greater = input > 2n, atLeast = input >= 2n;
    var selected = less ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             less: less, atMost: atMost, greater: greater, atLeast: atLeast };
}
H.push(objectFrameRelationalBigInt(false));
H.push(objectFrameRelationalBigInt(true));

// Deleting every container field does not release an independently saved
// child that is itself returned. Primitive selectors cannot erase that alias.
function objectFrameRelationalRetained(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source }, saved = target.held;
    var input = choice ? null : false, less = input < 1;
    var selected = less ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target, saved: saved,
             less: less };
}
H.push(objectFrameRelationalRetained(false));
H.push(objectFrameRelationalRetained(true));

// Saved String operands keep their original value through a BigInt field
// overwrite and deletion. Every arithmetic result and selected alias survives.
function objectFrameArithmeticBinarySaved(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    source.input = choice ? "20" : "-0";
    var saved = source.input;
    source.input = 1n;
    delete source.input;
    var difference = saved - 3, product = saved * -2, quotient = 1 / saved;
    var remainder = saved % 2, power = saved ** 2;
    var selected = difference < 0 ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             difference: difference, product: product, quotient: quotient,
             remainder: remainder, power: power };
}
H.push(objectFrameArithmeticBinarySaved(false));
H.push(objectFrameArithmeticBinarySaved(true));

// Number arithmetic stays primitive for NaN, infinity and signed zero. Pow's
// +/-1 to NaN differs from libm; neither NaN selector chooses its true arm.
function objectFrameArithmeticBinaryNumbers(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? 1 : -1, nan = (void 0) - 1;
    var quotient = input / 0, remainder = input % 0, power = input ** nan;
    var product = (choice ? 0 : -0) * -2;
    var selected = power ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             nan: nan, product: product, quotient: quotient,
             remainder: remainder, power: power };
}
H.push(objectFrameArithmeticBinaryNumbers(false));
H.push(objectFrameArithmeticBinaryNumbers(true));

// Primitive actual observations cannot prove this formal's future conversions.
function objectFrameArithmeticBinaryOpaque(input) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var difference = input - 3, product = input * 2, quotient = input / 2;
    var remainder = input % 2, power = input ** 2;
    var selected = difference < 0 ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             difference: difference, product: product, quotient: quotient,
             remainder: remainder, power: power };
}
H.push(objectFrameArithmeticBinaryOpaque(2));
H.push(objectFrameArithmeticBinaryOpaque("5."));

// Successful BigInt results do not supply a primitive Number origin proof.
function objectFrameArithmeticBinaryBigInt(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? 5n : 2n;
    var difference = input - 3n, product = input * 2n, quotient = input / 2n;
    var remainder = input % 2n, power = input ** 2n;
    var selected = difference < 0n ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             difference: difference, product: product, quotient: quotient,
             remainder: remainder, power: power };
}
H.push(objectFrameArithmeticBinaryBigInt(false));
H.push(objectFrameArithmeticBinaryBigInt(true));

// Deleting both container edges cannot release the independently returned child.
function objectFrameArithmeticBinaryRetained(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var saved = target.held, input = choice ? "5." : "2";
    var difference = input - 3, product = input * 2, quotient = input / 2;
    var remainder = input % 2, power = input ** 2;
    var selected = difference < 0 ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target, saved: saved,
             difference: difference, product: product, quotient: quotient,
             remainder: remainder, power: power };
}
H.push(objectFrameArithmeticBinaryRetained(false));
H.push(objectFrameArithmeticBinaryRetained(true));

// --- ORIGINAL PRIMITIVE ADDITION AND CONCATENATION ------------------------
// Saved String operands retain their bytes after the field acquires a BigInt.
// Generic addition must preserve operand order and its String result category.
function objectFrameAddConcatSaved(choice) {
    var child = { id: 1 };
    var source = { held: child, input: choice ? "5." : "2" }, target = { ...source };
    var saved = source.input;
    source.input = 1n;
    var sum = saved + 3, reverse = 3 + saved;
    var selected = sum === "23" ? source : target;
    delete source.held;
    delete target.held;
    delete source.input;
    delete target.input;
    return { selected: selected, source: source, target: target, sum: sum, reverse: reverse };
}
H.push(objectFrameAddConcatSaved(false));
H.push(objectFrameAddConcatSaved(true));

// No String input: generic addition yields Number, including an independent NaN.
function objectFrameAddConcatNumbers(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? true : null;
    var sum = input + 2, reverse = 2 + input, nan = (void 0) + input;
    var selected = sum === 2 ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             sum: sum, reverse: reverse, nan: nan };
}
H.push(objectFrameAddConcatNumbers(false));
H.push(objectFrameAddConcatNumbers(true));

// Template concat uses ToString on each primitive, without preserving an object
// alias. The saved Null/Undefined input survives a later own-object replacement.
function objectFrameAddConcatTemplate(choice) {
    var child = { id: 1 };
    var source = { held: child, input: choice ? (void 0) : null }, target = { ...source };
    var saved = source.input;
    source.input = child;
    var text = `a${saved}b`, reverse = `${saved}${true}`;
    var selected = text === "anullb" ? source : target;
    delete source.held;
    delete target.held;
    delete source.input;
    delete target.input;
    return { selected: selected, source: source, target: target, text: text, reverse: reverse };
}
H.push(objectFrameAddConcatTemplate(false));
H.push(objectFrameAddConcatTemplate(true));

// Successful Number observations do not prove an opaque future Add operand.
function objectFrameAddConcatOpaqueAdd(input) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var sum = input + 3, reverse = 3 + input;
    var selected = sum === 5 ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target, sum: sum, reverse: reverse };
}
H.push(objectFrameAddConcatOpaqueAdd(2));
H.push(objectFrameAddConcatOpaqueAdd(5));

// This separate refusal exercises Concat without a generic Add in its body.
function objectFrameAddConcatOpaqueTemplate(input) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var text = `a${input}b`, reverse = `${input}${true}`;
    var selected = text === "a2b" ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target, text: text, reverse: reverse };
}
H.push(objectFrameAddConcatOpaqueTemplate(2));
H.push(objectFrameAddConcatOpaqueTemplate(5));

// Successful BigInt addition and conversion remain outside the non-BigInt proof.
function objectFrameAddConcatBigInt(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? 5n : 2n;
    var sum = input + 3n, mixed = input + "!", text = `${input}:${sum}`;
    var selected = sum === 5n ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             sum: sum, mixed: mixed, text: text };
}
H.push(objectFrameAddConcatBigInt(false));
H.push(objectFrameAddConcatBigInt(true));

// Scalar/string results never release a separately returned saved child.
function objectFrameAddConcatRetained(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var saved = target.held, input = choice ? "5." : 2;
    var sum = input + 3, text = `${sum}`;
    var selected = text === "5" ? source : target;
    delete source.held;
    delete target.held;
    return { selected: selected, source: source, target: target,
             saved: saved, sum: sum, text: text };
}
H.push(objectFrameAddConcatRetained(false));
H.push(objectFrameAddConcatRetained(true));

// Equality compares two independently saved BigInt values without conversion.
// Replacing and deleting their fields cannot alter either saved operand.
function objectFrameBigIntEqualitySaved(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    source.operand = choice ? 9007199254740993n : 9007199254740992n;
    target.operand = 9007199254740993n;
    var lhs = source.operand, rhs = target.operand;
    source.operand = child;
    target.operand = child;
    var equal = lhs == rhs, different = rhs != lhs;
    var selected = equal ? source : target;
    delete source.operand;
    delete target.operand;
    delete source.held;
    delete target.held;
    return { source: source, target: target, selected: selected,
             equal: equal, different: different };
}
H.push(objectFrameBigIntEqualitySaved(false));
H.push(objectFrameBigIntEqualitySaved(true));

// Observing a BigInt actual cannot prove an opaque future parameter's origin.
function objectFrameBigIntEqualityOpaque(input) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var equal = input == 1n, different = 1n != input;
    delete source.held;
    delete target.held;
    return { source: source, target: target, equal: equal, different: different };
}
H.push(objectFrameBigIntEqualityOpaque(1n));
H.push(objectFrameBigIntEqualityOpaque(2n));

// Mixed Number/BigInt equality stays outside the exact same-category proof.
function objectFrameBigIntEqualityMixed(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? 1n : 2n;
    var equal = input == 1, different = 1 != input;
    delete source.held;
    delete target.held;
    return { source: source, target: target, equal: equal, different: different };
}
H.push(objectFrameBigIntEqualityMixed(false));
H.push(objectFrameBigIntEqualityMixed(true));

// An independently returned child remains reachable after every field delete.
function objectFrameBigIntEqualityRetained(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source }, saved = target.held;
    var input = choice ? 1n : 2n;
    var equal = input == 1n, different = 1n != input;
    delete source.held;
    delete target.held;
    return { source: source, target: target, saved: saved,
             equal: equal, different: different };
}
H.push(objectFrameBigIntEqualityRetained(false));
H.push(objectFrameBigIntEqualityRetained(true));

// Exact saved BigInt relations retain both operands across different mutations.
// Comparisons preserve their own Booleans; every structural branch stays live.
function objectFrameBigIntRelationalSaved(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    source.operand = choice ? 9007199254740993n : 9007199254740992n;
    target.operand = 9007199254740993n;
    var lhs = source.operand, rhs = target.operand;
    source.operand = child;
    delete target.operand;
    var less = lhs < rhs, lessEqual = lhs <= rhs;
    var greater = rhs > lhs, greaterEqual = rhs >= lhs;
    var selected = less ? source : target;
    delete source.operand;
    delete source.held;
    delete target.held;
    return { source: source, target: target, selected: selected,
             less: less, lessEqual: lessEqual, greater: greater, greaterEqual: greaterEqual };
}
H.push(objectFrameBigIntRelationalSaved(false));
H.push(objectFrameBigIntRelationalSaved(true));

// The same successful runtime BigInts do not prove an opaque future operand.
function objectFrameBigIntRelationalOpaque(input) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var less = input < 2n, lessEqual = input <= 2n;
    var greater = 2n > input, greaterEqual = 2n >= input;
    delete source.held;
    delete target.held;
    return { source: source, target: target,
             less: less, lessEqual: lessEqual, greater: greater, greaterEqual: greaterEqual };
}
H.push(objectFrameBigIntRelationalOpaque(1n));
H.push(objectFrameBigIntRelationalOpaque(2n));

// Mixed categories require separate conversion evidence on both sides.
function objectFrameBigIntRelationalMixed(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = choice ? 2n : 1n;
    var less = input < 2, lessEqual = input <= 2;
    var greater = 2 > input, greaterEqual = 2 >= input;
    delete source.held;
    delete target.held;
    return { source: source, target: target,
             less: less, lessEqual: lessEqual, greater: greater, greaterEqual: greaterEqual };
}
H.push(objectFrameBigIntRelationalMixed(false));
H.push(objectFrameBigIntRelationalMixed(true));

// Computed BigInts do not borrow constant provenance from their operands.
function objectFrameBigIntRelationalComputed(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var input = (choice ? 1n : 0n) + 1n;
    var less = input < 2n, lessEqual = input <= 2n;
    var greater = 2n > input, greaterEqual = 2n >= input;
    delete source.held;
    delete target.held;
    return { source: source, target: target,
             less: less, lessEqual: lessEqual, greater: greater, greaterEqual: greaterEqual };
}
H.push(objectFrameBigIntRelationalComputed(false));
H.push(objectFrameBigIntRelationalComputed(true));

// The Boolean result never removes a separately retained child's identity.
function objectFrameBigIntRelationalRetained(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source }, saved = target.held;
    var input = choice ? 2n : 1n;
    var less = input < 2n, lessEqual = input <= 2n;
    var greater = 2n > input, greaterEqual = 2n >= input;
    delete source.held;
    delete target.held;
    return { source: source, target: target, saved: saved,
             less: less, lessEqual: lessEqual, greater: greater, greaterEqual: greaterEqual };
}
H.push(objectFrameBigIntRelationalRetained(false));
H.push(objectFrameBigIntRelationalRetained(true));

// --- COMPUTED BIGINT UNARY ORIGINS -----------------------------------------
// Both BigInt unary operations preserve a separately proved original category.
function objectFrameBigIntUnarySaved(choice) {
    var child = { id: 1 };
    var source = { held: child, operand: choice ? 9007199254740993n : 9007199254740992n };
    var target = { ...source }, saved = target.operand;
    target.operand = 1;
    delete source.operand;
    delete target.operand;
    var negative = -saved, inverse = ~saved;
    var restored = -negative, inverted = ~inverse;
    var equal = restored == saved, less = negative < -9007199254740992n;
    var lessEqual = negative <= -9007199254740993n;
    var greater = -9007199254740992n > negative, greaterEqual = inverse >= ~saved;
    delete source.held;
    delete target.held;
    return { source: source, target: target, negative: negative, inverse: inverse,
             restored: restored, inverted: inverted, equal: equal, less: less,
             lessEqual: lessEqual, greater: greater, greaterEqual: greaterEqual };
}
H.push(objectFrameBigIntUnarySaved(false));
H.push(objectFrameBigIntUnarySaved(true));

// The same unary SSA producers can return Number or BigInt on separate paths.
function objectFrameBigIntUnaryPaths(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var operand = choice ? 1n : 2;
    var negative = -operand, inverse = ~operand;
    delete source.held;
    delete target.held;
    return { source: source, target: target, negative: negative, inverse: inverse };
}
H.push(objectFrameBigIntUnaryPaths(false));
H.push(objectFrameBigIntUnaryPaths(true));

// Runtime BigInt actuals do not establish an opaque future operand's category.
function objectFrameBigIntUnaryOpaque(operand) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var negative = -operand, inverse = ~operand;
    delete source.held;
    delete target.held;
    return { source: source, target: target, negative: negative, inverse: inverse };
}
H.push(objectFrameBigIntUnaryOpaque(1n));
H.push(objectFrameBigIntUnaryOpaque(2n));

// A computed BigInt never borrows the separate primitive non-BigInt proof.
function objectFrameBigIntUnaryMixed(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var operand = choice ? 1n : 2n, negative = -operand;
    var equal = negative == -1, less = negative < -1;
    delete source.held;
    delete target.held;
    return { source: source, target: target, negative: negative, equal: equal, less: less };
}
H.push(objectFrameBigIntUnaryMixed(false));
H.push(objectFrameBigIntUnaryMixed(true));

// Independent primitive results do not discard a separately retained child.
function objectFrameBigIntUnaryRetained(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source }, saved = target.held;
    var operand = choice ? 1n : 2n, negative = -operand, inverse = ~operand;
    delete source.held;
    delete target.held;
    return { source: source, target: target, saved: saved, negative: negative, inverse: inverse };
}
H.push(objectFrameBigIntUnaryRetained(false));
H.push(objectFrameBigIntUnaryRetained(true));

// --- COMPUTED BIGINT BINARY ORIGINS ----------------------------------------
// Both operands keep their original category through saved reads and mutation.
function objectFrameBigIntBinarySaved(choice) {
    var child = { id: 1 };
    var source = { held: child, left: choice ? 9007199254740993n : 9007199254740992n,
                   right: 2n };
    var target = { ...source }, left = target.left, right = source.right;
    target.left = 1; source.right = 0;
    delete source.left; delete target.left;
    delete source.right; delete target.right;
    var sum = left + right, difference = left - right, product = left * right;
    var recovered = sum - right, inverse = ~product;
    var equal = recovered == left, smaller = difference < left;
    delete source.held; delete target.held;
    return { source: source, target: target, sum: sum, difference: difference,
             product: product, recovered: recovered, inverse: inverse,
             equal: equal, smaller: smaller };
}
H.push(objectFrameBigIntBinarySaved(false));
H.push(objectFrameBigIntBinarySaved(true));

// One binary SSA result can have different categories on independent paths.
function objectFrameBigIntBinaryPaths(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var operand = choice ? 1n : 2;
    var sum = operand + operand, difference = sum - operand, product = sum * operand;
    delete source.held; delete target.held;
    return { source: source, target: target, sum: sum, difference: difference, product: product };
}
H.push(objectFrameBigIntBinaryPaths(false));
H.push(objectFrameBigIntBinaryPaths(true));

// Successful runtime BigInt actuals never prove an opaque future operand.
function objectFrameBigIntBinaryOpaque(operand) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var sum = operand + 2n, difference = operand - 2n, product = operand * 2n;
    delete source.held; delete target.held;
    return { source: source, target: target, sum: sum, difference: difference, product: product };
}
H.push(objectFrameBigIntBinaryOpaque(1n));
H.push(objectFrameBigIntBinaryOpaque(2n));

// An independently computed BigInt never supplies mixed-comparison permission.
function objectFrameBigIntBinaryMixed(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var operand = choice ? 1n : 2n, product = operand * 2n;
    var equal = product == 2, smaller = product < 3;
    delete source.held; delete target.held;
    return { source: source, target: target, product: product, equal: equal, smaller: smaller };
}
H.push(objectFrameBigIntBinaryMixed(false));
H.push(objectFrameBigIntBinaryMixed(true));

// Independent BigInt results do not remove a separately retained object edge.
function objectFrameBigIntBinaryRetained(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source }, saved = target.held;
    var operand = choice ? 1n : 2n;
    var sum = operand + 2n, difference = operand - 2n, product = operand * 2n;
    delete source.held; delete target.held;
    return { source: source, target: target, saved: saved,
             sum: sum, difference: difference, product: product };
}
H.push(objectFrameBigIntBinaryRetained(false));
H.push(objectFrameBigIntBinaryRetained(true));

// --- INDEPENDENT STATIC BIGINT RESULT CATEGORIES ----------------------------
// The earlier objectFrameStaticBinaryBigInt source now has its own BigInt
// category proof. Its historical source stays unchanged; only the claim moves.
function objectFrameBigIntStaticSaved(choice) {
    var child = { id: 1 };
    var source = { held: child, left: choice ? 9007199254740993n : 9007199254740992n,
                   right: 3n };
    var target = { ...source }, left = target.left, right = source.right;
    target.left = 1; source.right = 0;
    delete source.left; delete target.left;
    delete source.right; delete target.right;
    var masked = left & right, unioned = left | right, toggled = left ^ right;
    var recovered = toggled ^ right, inverse = ~unioned, sum = masked + 2n;
    var equal = recovered == left, smaller = masked < unioned;
    delete source.held; delete target.held;
    return { source: source, target: target, masked: masked, unioned: unioned,
             toggled: toggled, recovered: recovered, inverse: inverse, sum: sum,
             equal: equal, smaller: smaller };
}
H.push(objectFrameBigIntStaticSaved(false));
H.push(objectFrameBigIntStaticSaved(true));

// One static SSA producer keeps separate Number and BigInt path categories.
function objectFrameBigIntStaticPaths(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var operand = choice ? 1n : 2;
    var masked = operand & operand, unioned = operand | operand, toggled = operand ^ operand;
    delete source.held; delete target.held;
    return { source: source, target: target, masked: masked, unioned: unioned, toggled: toggled };
}
H.push(objectFrameBigIntStaticPaths(false));
H.push(objectFrameBigIntStaticPaths(true));

// Runtime BigInt actuals cannot establish an opaque future operand's category.
function objectFrameBigIntStaticOpaque(operand) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var masked = operand & 3n, unioned = operand | 3n, toggled = operand ^ 3n;
    delete source.held; delete target.held;
    return { source: source, target: target, masked: masked, unioned: unioned, toggled: toggled };
}
H.push(objectFrameBigIntStaticOpaque(1n));
H.push(objectFrameBigIntStaticOpaque(2n));

// A static BigInt result cannot authorize a mixed-category comparison.
function objectFrameBigIntStaticMixed(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var operand = choice ? 1n : 2n, masked = operand & 3n;
    var equal = masked == 2, smaller = masked < 3;
    delete source.held; delete target.held;
    return { source: source, target: target, masked: masked, equal: equal, smaller: smaller };
}
H.push(objectFrameBigIntStaticMixed(false));
H.push(objectFrameBigIntStaticMixed(true));

// Successful observed shifts do not prove their exceptional future cases.
function objectFrameBigIntStaticShift(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var operand = choice ? 1n : 2n, masked = operand & 3n, shifted = masked << 1n;
    delete source.held; delete target.held;
    return { source: source, target: target, masked: masked, shifted: shifted };
}
H.push(objectFrameBigIntStaticShift(false));
H.push(objectFrameBigIntStaticShift(true));

// Independent static results leave a separately retained object edge intact.
function objectFrameBigIntStaticRetained(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source }, saved = target.held;
    var operand = choice ? 1n : 2n;
    var masked = operand & 3n, unioned = operand | 3n, toggled = operand ^ 3n;
    delete source.held; delete target.held;
    return { source: source, target: target, saved: saved,
             masked: masked, unioned: unioned, toggled: toggled };
}
H.push(objectFrameBigIntStaticRetained(false));
H.push(objectFrameBigIntStaticRetained(true));

// --- INDEPENDENT SIGNED BIGINT SHIFT CATEGORIES -----------------------------
// Saved operands keep their original category after own fields are overwritten.
function objectFrameBigIntShiftSaved(choice) {
    var child = { id: 1 };
    var source = { held: child, left: choice ? -9n : 9007199254740993n, count: 2n };
    var target = { ...source }, left = target.left, count = source.count;
    target.left = 1; source.count = 0;
    delete source.left; delete target.left;
    delete source.count; delete target.count;
    var expanded = left << count, reduced = left >> count;
    var reverseLeft = left << -count, reverseRight = left >> -count;
    var recovered = expanded >> count, far = left >> 9007199254740993n;
    var equal = recovered == left;
    delete source.held; delete target.held;
    return { source: source, target: target, expanded: expanded, reduced: reduced,
             reverseLeft: reverseLeft, reverseRight: reverseRight, recovered: recovered,
             far: far, equal: equal };
}
H.push(objectFrameBigIntShiftSaved(false));
H.push(objectFrameBigIntShiftSaved(true));

// A shared shift producer separately records its Number and BigInt paths.
function objectFrameBigIntShiftPaths(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var operand = choice ? 1n : 2;
    var expanded = operand << operand, reduced = operand >> operand;
    delete source.held; delete target.held;
    return { source: source, target: target, expanded: expanded, reduced: reduced };
}
H.push(objectFrameBigIntShiftPaths(false));
H.push(objectFrameBigIntShiftPaths(true));

// Observed BigInt actuals cannot prove an opaque future operand's category.
function objectFrameBigIntShiftOpaque(operand) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var expanded = operand << 1n, reduced = operand >> 1n;
    delete source.held; delete target.held;
    return { source: source, target: target, expanded: expanded, reduced: reduced };
}
H.push(objectFrameBigIntShiftOpaque(1n));
H.push(objectFrameBigIntShiftOpaque(2n));

// The BigInt result still cannot authorize a mixed-category comparison.
function objectFrameBigIntShiftMixed(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var operand = choice ? 1n : 2n, expanded = operand << 1n, reduced = operand >> 1n;
    var equal = expanded == 2, smaller = reduced < 2;
    delete source.held; delete target.held;
    return { source: source, target: target, expanded: expanded, reduced: reduced,
             equal: equal, smaller: smaller };
}
H.push(objectFrameBigIntShiftMixed(false));
H.push(objectFrameBigIntShiftMixed(true));

// An oversized left shift throws an independent Error before this return.
// The caller keeps that Error, while this frame's unpublished locals die.
function objectFrameBigIntShiftEarly(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source };
    var count = choice ? 9007199254740993n : 1n, expanded = 1n << count;
    delete source.held; delete target.held;
    return { source: source, target: target, expanded: expanded };
}
H.push(objectFrameBigIntShiftEarly(false));
function objectFrameBigIntShiftCatch() {
    try { H.push(objectFrameBigIntShiftEarly(true)); }
    catch (error) { H.push(error); }
}
objectFrameBigIntShiftCatch();

// A separately retained object edge remains live across both shift results.
function objectFrameBigIntShiftRetained(choice) {
    var child = { id: 1 };
    var source = { held: child }, target = { ...source }, saved = target.held;
    var operand = choice ? 1n : 2n;
    var expanded = operand << 1n, reduced = operand >> 1n;
    delete source.held; delete target.held;
    return { source: source, target: target, saved: saved,
             expanded: expanded, reduced: reduced };
}
H.push(objectFrameBigIntShiftRetained(false));
H.push(objectFrameBigIntShiftRetained(true));

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
