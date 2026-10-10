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

var H = []; // the retention sink: anything pushed here is reachable from globals
var G = null; // a plain global slot
var EXT = {}; // an external object to store into

// --- CONFINED: what the MVP proves ----------------------------------------
function confinedObject() {
    var p = {
        x: 1,
        y: 2
    };
    p.x += 3;
    return p.x * p.y;
}

function confinedArray() {
    var a = [1, 2, 3];
    var s = 0;
    for (var i = 0; i < a.length; i++) {
        s += a[i];
    }
    return s;
}

function confinedForOf() {
    var s = 0;
    for (var x of [4, 5, 6]) {
        s += x;
    }
    return s;
}

function confinedPredicates() {
    var o = {
            k: 1
        },
        q = {
            k: 1
        };
    var has = "k" in o,
        same = o === q,
        self = o === o,
        neg = !o,
        ty = typeof o,
        inst = o instanceof Object;
    delete o.k;
    return [has, same, self, neg, ty, inst, "k" in o];
}

function confinedLoopCarried(n) {
    var o = {
        v: 0
    };
    for (var i = 0; i < n; i++) {
        o.v = o.v + i;
    } // carried round the back edge as a block argument
    return o.v;
}
confinedObject();
confinedArray();
confinedForOf();
confinedPredicates();
confinedLoopCarried(5);

// --- COMPLETE OWN-ARRAY RETENTION THROUGH REAL IMPORTED FRAMES ------------
// Empty objects avoid named-field operations outside the complete subset.
// Every case executes; returned containers/read values stay in H. The checker
// joins each observed site to its independent claim by program/function/pc.
function arrayFramePrivate() {
    var child = {};
    var container = [child];
    return 0;
}
arrayFramePrivate();
arrayFramePrivate();

function arrayFrameReturned() {
    var child = {};
    var container = [child];
    return container;
}
H.push(arrayFrameReturned());

function arrayFrameSavedRead() {
    var child = {},
        replacement = {};
    var container = [child],
        saved = container[0];
    container[0] = replacement;
    return saved;
}
H.push(arrayFrameSavedRead());

function arrayFrameOverwrite() {
    var child = {},
        replacement = {};
    var container = [child];
    container[0] = replacement;
    return container;
}
H.push(arrayFrameOverwrite());

function arrayFrameLoadedAlias() {
    var child = {},
        inner = [child],
        outer = [inner];
    var alias = outer[0];
    alias[0] = 0;
    return outer;
}
H.push(arrayFrameLoadedAlias());
// A late publication or call still invalidates the complete query, even when
// the child's first legacy reason is Stored through its local container.
function arrayFramePublished() {
    var child = {};
    var container = [child];
    G = container;
    return 0;
}
arrayFramePublished();

function arrayFrameCall() {
    var child = {};
    var container = [child];
    hold(container);
    return 0;
}
arrayFrameCall();
// Final and transient cycles both preserve Stored; this increment chooses no
// graph owner even when the oracle observes every instance confined at exit.
function arrayFrameCycle() {
    var container = [null];
    container[0] = container;
    return 0;
}
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
    var child = {},
        container = {
            child: child
        };
    delete container.child;
    return container;
}
H.push(objectFrameDeletedChild());

function objectFrameDeletedSavedRead() {
    var child = {},
        container = {
            child: child
        },
        key = "child";
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
    var child = {},
        source = {
            held: child
        },
        target = {
            ...source
        };
    delete source.held;
    return target;
}
H.push(objectFrameCopiedChild());

function objectFrameCopiedOverwrite() {
    var old = {},
        replacement = {},
        source = {
            held: old
        },
        target = {
            ...source
        };
    target.held = replacement;
    delete source.held;
    return target;
}
H.push(objectFrameCopiedOverwrite());

function objectFrameCopiedSavedRead() {
    var child = {},
        source = {
            held: child
        },
        target = {
            ...source
        };
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
    var child = {
            id: 1
        },
        replacement = {
            id: 2
        };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var selected = selectSource ? source : target;
    selected.held = replacement;
    delete selected.held;
    return {
        source: source,
        target: target
    };
}
H.push(objectFrameCopiedConditionalAlias(true));
H.push(objectFrameCopiedConditionalAlias(false));

// Each child is retained once and confined once. Copying the wrong source,
// or sharing its later deletion with the target, loses a real returned child.
function objectFrameCopiedConditionalSource(selectFirst) {
    var left = {
            id: 1
        },
        right = {
            id: 2
        };
    var first = {
            held: left
        },
        second = {
            held: right
        };
    var source = selectFirst ? first : second;
    var target = {
        ...source
    };
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
    var child = {
            id: 1
        },
        replacement = {
            id: 2
        };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var saved = target.held;
    switch (choice) {
        case 0:
            delete source.held;
            target.held = replacement;
            return {
                saved: saved, target: target
            };
        case 1:
            source.held = replacement;
            delete target.held;
            return {
                saved: saved, source: source
            };
        default:
            delete source.held;
            delete target.held;
            return {
                saved: saved
            };
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
    var child = {
            id: 1
        },
        replacement = {
            id: 2
        };
    var source = {
            held: child
        },
        target = {
            ...source
        };
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
    var child = {
        id: 1
    };
    var source = {
            held: child,
            side: "source"
        },
        target = {
            ...source
        };
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
            return {
                side: "default"
            };
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
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var negated = !choice;
    var selected = negated ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        negated: negated
    };
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
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var named = typeof choice,
        local = typeof child;
    var selected = named === "undefined" ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        named: named,
        local: local
    };
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
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var selected = choice ? source : target;
    var discarded = void(selected.mark = 7);
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        discarded: discarded
    };
}
H.push(objectFrameVoidReleased(false));
H.push(objectFrameVoidReleased(true));

// --- STATIC BINARY NUMBER PRODUCERS ---------------------------------------
// Every static operand has an independent non-BigInt origin on each path.
// The high-bit input and shift count 33 separate signed/unsigned shifts,
// truncation and the five-bit mask. Source ++ reaches static add, unlike +.
function objectFrameStaticBinaryReleased(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var incremented = choice ? 2147483653 : 12;
    ++incremented;
    var masked = incremented & 7,
        unioned = incremented | 2,
        toggled = incremented ^ 5;
    var left = incremented << 33,
        signed = incremented >> 33,
        unsigned = incremented >>> 33;
    var selected = masked === 6 ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        incremented: incremented,
        masked: masked,
        unioned: unioned,
        toggled: toggled,
        left: left,
        signed: signed,
        unsigned: unsigned
    };
}
H.push(objectFrameStaticBinaryReleased(false));
H.push(objectFrameStaticBinaryReleased(true));

// Numeric observations cannot prove an opaque formal excludes BigInt. Both
// controls release the child at runtime but retain the conservative Stored
// claim. The BigInt pair also stays outside the Number-only proof, even for
// this operation whose concrete pair succeeds. No object coercion is assumed:
// the VM's static conversion and source JavaScript differ on object inputs.
function objectFrameStaticBinaryOpaque(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var produced = choice | 0;
    var selected = produced === 12 ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        produced: produced
    };
}
H.push(objectFrameStaticBinaryOpaque(12));
H.push(objectFrameStaticBinaryOpaque(2147483653));

function objectFrameStaticBinaryBigInt() {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var produced = 1n | 2n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        produced: produced
    };
}
H.push(objectFrameStaticBinaryBigInt());

// --- ARITHMETIC UNARY PRIMITIVE PRODUCERS ----------------------------------
// Both structural inputs independently exclude objects and BigInt. A trailing
// decimal String, null and the high bit distinguish conversion, signed zero
// and ToInt32 truncation without relying on observed formal argument tags.
function objectFrameArithmeticUnaryReleased(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var input = choice ? "4294967297." : null;
    var negated = -input,
        numeric = +input,
        inverted = ~input;
    var selected = inverted === -2 ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        negated: negated,
        numeric: numeric,
        inverted: inverted
    };
}
H.push(objectFrameArithmeticUnaryReleased(false));
H.push(objectFrameArithmeticUnaryReleased(true));

// The input is the old primitive even though its source field now holds a
// BigInt. A fresh contents query must follow the saved SSA read's own origin.
function objectFrameArithmeticUnarySaved(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    source.operand = choice ? "17." : false;
    var saved = source.operand;
    source.operand = 1n;
    var negated = -saved,
        numeric = +saved,
        inverted = ~saved;
    var selected = numeric === 17 ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        negated: negated,
        numeric: numeric,
        inverted: inverted
    };
}
H.push(objectFrameArithmeticUnarySaved(false));
H.push(objectFrameArithmeticUnarySaved(true));

// Numeric observations do not prove this formal excludes user conversion or
// BigInt. Successful literal BigInt negation/complement also remain outside
// the independent Number-result proof. Both controls keep Stored claims.
function objectFrameArithmeticUnaryOpaque(input) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var negated = -input,
        numeric = +input,
        inverted = ~input;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        negated: negated,
        numeric: numeric,
        inverted: inverted
    };
}
H.push(objectFrameArithmeticUnaryOpaque(0));
H.push(objectFrameArithmeticUnaryOpaque("4294967297."));

function objectFrameArithmeticUnaryBigInt() {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var negated = -1n,
        inverted = ~1n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        negated: negated,
        inverted: inverted
    };
}
H.push(objectFrameArithmeticUnaryBigInt());

// --- LOOSE EQUALITY FROM INDEPENDENT PRIMITIVE ORIGINS -----------------------
// Check numeric String conversion, nullish equality, operand reversal and !=
// (the importer's Eq followed by Not). This original witness reads global
// undefined, which refuses; the separate Literal repair below proves both origins.
function objectFrameLooseEqualityReleased(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var input = choice ? "17." : null,
        other = choice ? 18 : undefined;
    var equal = input == other,
        different = input != other;
    var reversed = other == input,
        numeric = input == 17,
        nullZero = null == 0;
    var selected = equal ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        equal: equal,
        different: different,
        reversed: reversed,
        numeric: numeric,
        nullZero: nullZero
    };
}
H.push(objectFrameLooseEqualityReleased(false));
H.push(objectFrameLooseEqualityReleased(true));

// A later BigInt in the original field does not change either saved operand.
// The unit controls also cover the reverse: a primitive overwrite cannot
// clean an earlier saved object, opaque value or BigInt.
function objectFrameLooseEqualitySaved(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    source.operand = choice ? "17." : false;
    var saved = source.operand;
    source.operand = 1n;
    var equal = saved == 17,
        reversed = 17 == saved,
        different = saved != 17;
    var selected = equal ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        equal: equal,
        reversed: reversed,
        different: different
    };
}
H.push(objectFrameLooseEqualitySaved(false));
H.push(objectFrameLooseEqualitySaved(true));

// Successful observations do not prove an opaque formal primitive. Literal
// BigInt comparisons remain outside this bounded family even when successful.
function objectFrameLooseEqualityOpaque(input) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var equal = input == 17,
        reversed = 17 == input,
        different = input != 17;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        equal: equal,
        reversed: reversed,
        different: different
    };
}
H.push(objectFrameLooseEqualityOpaque(0));
H.push(objectFrameLooseEqualityOpaque("17."));

function objectFrameLooseEqualityBigInt() {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var equal = 1n == 1,
        reversed = 1 == 1n,
        different = 1n != 2n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        equal: equal,
        reversed: reversed,
        different: different
    };
}
H.push(objectFrameLooseEqualityBigInt());

// Relational comparison has an independent whole-frame retention proof.
// The VM still enters to_primitive's depth guard for these primitive operands.
