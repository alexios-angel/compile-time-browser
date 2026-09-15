// --- ORIGINAL PRIMITIVE ADDITION AND CONCATENATION ------------------------
// Saved String operands retain their bytes after the field acquires a BigInt.
// Generic addition must preserve operand order and its String result category.
function objectFrameAddConcatSaved(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child,
            input: choice ? "5." : "2"
        },
        target = {
            ...source
        };
    var saved = source.input;
    source.input = 1n;
    var sum = saved + 3,
        reverse = 3 + saved;
    var selected = sum === "23" ? source : target;
    delete source.held;
    delete target.held;
    delete source.input;
    delete target.input;
    return {
        selected: selected,
        source: source,
        target: target,
        sum: sum,
        reverse: reverse
    };
}
H.push(objectFrameAddConcatSaved(false));
H.push(objectFrameAddConcatSaved(true));

// No String input: generic addition yields Number, including an independent NaN.
function objectFrameAddConcatNumbers(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var input = choice ? true : null;
    var sum = input + 2,
        reverse = 2 + input,
        nan = (void 0) + input;
    var selected = sum === 2 ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        sum: sum,
        reverse: reverse,
        nan: nan
    };
}
H.push(objectFrameAddConcatNumbers(false));
H.push(objectFrameAddConcatNumbers(true));

// Template concat uses ToString on each primitive, without preserving an object
// alias. The saved Null/Undefined input survives a later own-object replacement.
function objectFrameAddConcatTemplate(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child,
            input: choice ? (void 0) : null
        },
        target = {
            ...source
        };
    var saved = source.input;
    source.input = child;
    var text = `a${saved}b`,
        reverse = `${saved}${true}`;
    var selected = text === "anullb" ? source : target;
    delete source.held;
    delete target.held;
    delete source.input;
    delete target.input;
    return {
        selected: selected,
        source: source,
        target: target,
        text: text,
        reverse: reverse
    };
}
H.push(objectFrameAddConcatTemplate(false));
H.push(objectFrameAddConcatTemplate(true));

// Successful Number observations do not prove an opaque future Add operand.
function objectFrameAddConcatOpaqueAdd(input) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var sum = input + 3,
        reverse = 3 + input;
    var selected = sum === 5 ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        sum: sum,
        reverse: reverse
    };
}
H.push(objectFrameAddConcatOpaqueAdd(2));
H.push(objectFrameAddConcatOpaqueAdd(5));

// This separate refusal exercises Concat without a generic Add in its body.
function objectFrameAddConcatOpaqueTemplate(input) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var text = `a${input}b`,
        reverse = `${input}${true}`;
    var selected = text === "a2b" ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        text: text,
        reverse: reverse
    };
}
H.push(objectFrameAddConcatOpaqueTemplate(2));
H.push(objectFrameAddConcatOpaqueTemplate(5));

// Successful BigInt addition and conversion remain outside the non-BigInt proof.
function objectFrameAddConcatBigInt(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var input = choice ? 5n : 2n;
    var sum = input + 3n,
        mixed = input + "!",
        text = `${input}:${sum}`;
    var selected = sum === 5n ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        sum: sum,
        mixed: mixed,
        text: text
    };
}
H.push(objectFrameAddConcatBigInt(false));
H.push(objectFrameAddConcatBigInt(true));

// Scalar/string results never release a separately returned saved child.
function objectFrameAddConcatRetained(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var saved = target.held,
        input = choice ? "5." : 2;
    var sum = input + 3,
        text = `${sum}`;
    var selected = text === "5" ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        saved: saved,
        sum: sum,
        text: text
    };
}
H.push(objectFrameAddConcatRetained(false));
H.push(objectFrameAddConcatRetained(true));

// Equality compares two independently saved BigInt values without conversion.
// Replacing and deleting their fields cannot alter either saved operand.
function objectFrameBigIntEqualitySaved(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    source.operand = choice ? 9007199254740993n : 9007199254740992n;
    target.operand = 9007199254740993n;
    var lhs = source.operand,
        rhs = target.operand;
    source.operand = child;
    target.operand = child;
    var equal = lhs == rhs,
        different = rhs != lhs;
    var selected = equal ? source : target;
    delete source.operand;
    delete target.operand;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        selected: selected,
        equal: equal,
        different: different
    };
}
H.push(objectFrameBigIntEqualitySaved(false));
H.push(objectFrameBigIntEqualitySaved(true));

// Observing a BigInt actual cannot prove an opaque future parameter's origin.
function objectFrameBigIntEqualityOpaque(input) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var equal = input == 1n,
        different = 1n != input;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        equal: equal,
        different: different
    };
}
H.push(objectFrameBigIntEqualityOpaque(1n));
H.push(objectFrameBigIntEqualityOpaque(2n));

// Mixed Number/BigInt equality stays outside the exact same-category proof.
function objectFrameBigIntEqualityMixed(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var input = choice ? 1n : 2n;
    var equal = input == 1,
        different = 1 != input;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        equal: equal,
        different: different
    };
}
H.push(objectFrameBigIntEqualityMixed(false));
H.push(objectFrameBigIntEqualityMixed(true));

// An independently returned child remains reachable after every field delete.
function objectFrameBigIntEqualityRetained(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        },
        saved = target.held;
    var input = choice ? 1n : 2n;
    var equal = input == 1n,
        different = 1n != input;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        saved: saved,
        equal: equal,
        different: different
    };
}
H.push(objectFrameBigIntEqualityRetained(false));
H.push(objectFrameBigIntEqualityRetained(true));

// Exact saved BigInt relations retain both operands across different mutations.
// Comparisons preserve their own Booleans; every structural branch stays live.
function objectFrameBigIntRelationalSaved(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    source.operand = choice ? 9007199254740993n : 9007199254740992n;
    target.operand = 9007199254740993n;
    var lhs = source.operand,
        rhs = target.operand;
    source.operand = child;
    delete target.operand;
    var less = lhs < rhs,
        lessEqual = lhs <= rhs;
    var greater = rhs > lhs,
        greaterEqual = rhs >= lhs;
    var selected = less ? source : target;
    delete source.operand;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        selected: selected,
        less: less,
        lessEqual: lessEqual,
        greater: greater,
        greaterEqual: greaterEqual
    };
}
H.push(objectFrameBigIntRelationalSaved(false));
H.push(objectFrameBigIntRelationalSaved(true));

// The same successful runtime BigInts do not prove an opaque future operand.
function objectFrameBigIntRelationalOpaque(input) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var less = input < 2n,
        lessEqual = input <= 2n;
    var greater = 2n > input,
        greaterEqual = 2n >= input;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        less: less,
        lessEqual: lessEqual,
        greater: greater,
        greaterEqual: greaterEqual
    };
}
H.push(objectFrameBigIntRelationalOpaque(1n));
H.push(objectFrameBigIntRelationalOpaque(2n));

// Mixed categories require separate conversion evidence on both sides.
function objectFrameBigIntRelationalMixed(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var input = choice ? 2n : 1n;
    var less = input < 2,
        lessEqual = input <= 2;
    var greater = 2 > input,
        greaterEqual = 2 >= input;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        less: less,
        lessEqual: lessEqual,
        greater: greater,
        greaterEqual: greaterEqual
    };
}
H.push(objectFrameBigIntRelationalMixed(false));
H.push(objectFrameBigIntRelationalMixed(true));

// Computed BigInts do not borrow constant provenance from their operands.
function objectFrameBigIntRelationalComputed(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var input = (choice ? 1n : 0n) + 1n;
    var less = input < 2n,
        lessEqual = input <= 2n;
    var greater = 2n > input,
        greaterEqual = 2n >= input;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        less: less,
        lessEqual: lessEqual,
        greater: greater,
        greaterEqual: greaterEqual
    };
}
H.push(objectFrameBigIntRelationalComputed(false));
H.push(objectFrameBigIntRelationalComputed(true));

// The Boolean result never removes a separately retained child's identity.
function objectFrameBigIntRelationalRetained(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        },
        saved = target.held;
    var input = choice ? 2n : 1n;
    var less = input < 2n,
        lessEqual = input <= 2n;
    var greater = 2n > input,
        greaterEqual = 2n >= input;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        saved: saved,
        less: less,
        lessEqual: lessEqual,
        greater: greater,
        greaterEqual: greaterEqual
    };
}
H.push(objectFrameBigIntRelationalRetained(false));
H.push(objectFrameBigIntRelationalRetained(true));

// --- COMPUTED BIGINT UNARY ORIGINS -----------------------------------------
// Both BigInt unary operations preserve a separately proved original category.
function objectFrameBigIntUnarySaved(choice) {
    var child = {
        id: 1
    };
    var source = {
        held: child,
        operand: choice ? 9007199254740993n : 9007199254740992n
    };
    var target = {
            ...source
        },
        saved = target.operand;
    target.operand = 1;
    delete source.operand;
    delete target.operand;
    var negative = -saved,
        inverse = ~saved;
    var restored = -negative,
        inverted = ~inverse;
    var equal = restored == saved,
        less = negative < -9007199254740992n;
    var lessEqual = negative <= -9007199254740993n;
    var greater = -9007199254740992n > negative,
        greaterEqual = inverse >= ~saved;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        negative: negative,
        inverse: inverse,
        restored: restored,
        inverted: inverted,
        equal: equal,
        less: less,
        lessEqual: lessEqual,
        greater: greater,
        greaterEqual: greaterEqual
    };
}
H.push(objectFrameBigIntUnarySaved(false));
H.push(objectFrameBigIntUnarySaved(true));

// The same unary SSA producers can return Number or BigInt on separate paths.
function objectFrameBigIntUnaryPaths(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? 1n : 2;
    var negative = -operand,
        inverse = ~operand;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        negative: negative,
        inverse: inverse
    };
}
H.push(objectFrameBigIntUnaryPaths(false));
H.push(objectFrameBigIntUnaryPaths(true));

// Runtime BigInt actuals do not establish an opaque future operand's category.
function objectFrameBigIntUnaryOpaque(operand) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var negative = -operand,
        inverse = ~operand;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        negative: negative,
        inverse: inverse
    };
}
H.push(objectFrameBigIntUnaryOpaque(1n));
H.push(objectFrameBigIntUnaryOpaque(2n));

// A computed BigInt never borrows the separate primitive non-BigInt proof.
function objectFrameBigIntUnaryMixed(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? 1n : 2n,
        negative = -operand;
    var equal = negative == -1,
        less = negative < -1;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        negative: negative,
        equal: equal,
        less: less
    };
}
H.push(objectFrameBigIntUnaryMixed(false));
H.push(objectFrameBigIntUnaryMixed(true));

// Independent primitive results do not discard a separately retained child.
function objectFrameBigIntUnaryRetained(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        },
        saved = target.held;
    var operand = choice ? 1n : 2n,
        negative = -operand,
        inverse = ~operand;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        saved: saved,
        negative: negative,
        inverse: inverse
    };
}
H.push(objectFrameBigIntUnaryRetained(false));
H.push(objectFrameBigIntUnaryRetained(true));

// --- STRING/BIGINT ADD AND CONCAT ORIGINS ---------------------------------
// Saved String and BigInt categories survive overwrites, deletion and copies.
function objectFrameStringBigIntSaved(choice) {
    var child = {
        id: 1
    };
    var source = {
        held: child,
        text: choice ? "saved:\u0000\u00e9" : "",
        number: choice ? 9007199254740993n : -2n
    };
    var target = {
            ...source
        },
        text = target.text,
        number = source.number;
    target.text = 0;
    source.number = "changed";
    delete source.text;
    delete target.text;
    delete source.number;
    delete target.number;
    var left = text + number,
        right = number + text,
        chained = left + 3n;
    var typed = typeof child + number,
        template = `${number}:${left}`;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        left: left,
        right: right,
        chained: chained,
        typed: typed,
        template: template
    };
}
H.push(objectFrameStringBigIntSaved(false));
H.push(objectFrameStringBigIntSaved(true));

// One source Add has independent String and BigInt results on its live paths.
function objectFrameStringBigIntPaths(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? "prefix:" : 2n,
        produced = operand + 3n;
    var converted = `${produced}`,
        chained = converted + 4n;
    var again = chained + -1n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        produced: produced,
        converted: converted,
        chained: chained,
        again: again
    };
}
H.push(objectFrameStringBigIntPaths(false));
H.push(objectFrameStringBigIntPaths(true));

// Primitive template conversions stay independent even without a String input.
function objectFrameStringBigIntTemplate(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var number = choice ? 9007199254740993n : -2n;
    var converted = `${number}${number + 1n}${true}${null}${void 0}${-0}`;
    var chained = number + converted + 5n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        converted: converted,
        chained: chained
    };
}
H.push(objectFrameStringBigIntTemplate(false));
H.push(objectFrameStringBigIntTemplate(true));

// Successful actuals cannot prove an opaque future Add operand to be String.
function objectFrameStringBigIntOpaqueAdd(operand) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var produced = operand + 1n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        produced: produced
    };
}
H.push(objectFrameStringBigIntOpaqueAdd("saved:"));
H.push(objectFrameStringBigIntOpaqueAdd(""));

// Template syntax cannot authorize an opaque object's conversion callbacks.
function objectFrameStringBigIntOpaqueTemplate(operand) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var produced = `${operand}:` + 1n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        produced: produced
    };
}
H.push(objectFrameStringBigIntOpaqueTemplate(1n));
H.push(objectFrameStringBigIntOpaqueTemplate(2n));

// The object path remains refused even when its current default conversion succeeds.
function objectFrameStringBigIntObject(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? child : "saved:",
        produced = `${operand}` + 1n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        produced: produced
    };
}
H.push(objectFrameStringBigIntObject(false));
H.push(objectFrameStringBigIntObject(true));

// A proved String result supplies no mixed String/BigInt comparison contract.
function objectFrameStringBigIntMixed(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var number = choice ? 1n : 2n,
        produced = "" + number;
    var equal = produced == number,
        less = produced < number;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        produced: produced,
        equal: equal,
        less: less
    };
}
H.push(objectFrameStringBigIntMixed(false));
H.push(objectFrameStringBigIntMixed(true));

// Independent String digits do not discard a separately saved object identity.
function objectFrameStringBigIntRetained(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        },
        saved = target.held;
    var number = choice ? 9007199254740993n : -2n;
    var produced = "saved:" + number,
        chained = number + produced;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        saved: saved,
        produced: produced,
        chained: chained
    };
}
H.push(objectFrameStringBigIntRetained(false));
H.push(objectFrameStringBigIntRetained(true));

// --- MIXED PRIMITIVE BIGINT COMPARISON RETENTION ---------------------------
// Saved primitive originals survive replacement and deletion of both slots.
// Boolean results are observed separately; retention never chooses an arm.
function objectFrameBigIntMixedSaved(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    source.operand = choice ? "9007199254740993" : "1.5";
    target.operand = choice ? 9007199254740993n : 1n;
    var lhs = source.operand,
        rhs = target.operand;
    source.operand = child;
    delete target.operand;
    var equal = lhs == rhs,
        less = lhs < rhs,
        lessEqual = lhs <= rhs;
    var greater = rhs > lhs,
        greaterEqual = rhs >= lhs;
    delete source.operand;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        equal: equal,
        less: less,
        lessEqual: lessEqual,
        greater: greater,
        greaterEqual: greaterEqual
    };
}
H.push(objectFrameBigIntMixedSaved(false));
H.push(objectFrameBigIntMixedSaved(true));

// Nullish and Boolean originals are primitive, including VM value differences.
function objectFrameBigIntMixedPrimitives(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? 1n : 0n;
    var equal = operand == true,
        reverse = false == operand;
    var less = operand < true,
        absent = operand < void 0,
        nullish = null <= operand;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        equal: equal,
        reverse: reverse,
        less: less,
        absent: absent,
        nullish: nullish
    };
}
H.push(objectFrameBigIntMixedPrimitives(false));
H.push(objectFrameBigIntMixedPrimitives(true));

// One incoming SSA slot can contain independently proved String or Number.
function objectFrameBigIntMixedPaths(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? "" + 1n : +2,
        big = 1n + 1n;
    var equal = big == operand,
        less = operand < big;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        equal: equal,
        less: less
    };
}
H.push(objectFrameBigIntMixedPaths(false));
H.push(objectFrameBigIntMixedPaths(true));

// Exact mixed numeric ordering includes fractions, infinities and NaN.
function objectFrameBigIntMixedNumbers(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? 9007199254740993n : 1n;
    var equal = operand == 9007199254740992,
        fraction = operand < 1.5;
    var infinity = operand < 1 / 0,
        unordered = 0 / 0 >= operand;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        equal: equal,
        fraction: fraction,
        infinity: infinity,
        unordered: unordered
    };
}
H.push(objectFrameBigIntMixedNumbers(false));
H.push(objectFrameBigIntMixedNumbers(true));

// Observed primitive actuals cannot prove an opaque future argument's category.
function objectFrameBigIntMixedOpaque(operand) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var equal = operand == 1n,
        less = 1n < operand;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        equal: equal,
        less: less
    };
}
H.push(objectFrameBigIntMixedOpaque(1n));
H.push(objectFrameBigIntMixedOpaque("2"));

// Object conversion stays refused even when these actual objects have no hook.
function objectFrameBigIntMixedObject(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? source : target;
    var equal = operand == 1n,
        less = 1n < operand;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        equal: equal,
        less: less
    };
}
H.push(objectFrameBigIntMixedObject(false));
H.push(objectFrameBigIntMixedObject(true));

// A live child on either structural arm cannot disappear behind a comparison.
function objectFrameBigIntMixedRetained(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        },
        saved = target.held;
    var operand = choice ? "1" : "2",
        equal = operand == 1n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        saved: saved,
        equal: equal
    };
}
H.push(objectFrameBigIntMixedRetained(false));
H.push(objectFrameBigIntMixedRetained(true));

// Invalid, empty and prefixed Strings add no object identity or value proof.
function objectFrameBigIntMixedStrings(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? "invalid" : "0x10";
    var equal = operand == 16n,
        less = 16n < operand;
    var empty = "" == 0n,
        blank = 0n >= "  ";
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        equal: equal,
        less: less,
        empty: empty,
        blank: blank
    };
}
H.push(objectFrameBigIntMixedStrings(false));
H.push(objectFrameBigIntMixedStrings(true));

// --- COMPUTED BIGINT BINARY ORIGINS ----------------------------------------
// Both operands keep their original category through saved reads and mutation.
function objectFrameBigIntBinarySaved(choice) {
    var child = {
        id: 1
    };
    var source = {
        held: child,
        left: choice ? 9007199254740993n : 9007199254740992n,
        right: 2n
    };
    var target = {
            ...source
        },
        left = target.left,
        right = source.right;
    target.left = 1;
    source.right = 0;
    delete source.left;
    delete target.left;
    delete source.right;
    delete target.right;
    var sum = left + right,
        difference = left - right,
        product = left * right;
    var recovered = sum - right,
        inverse = ~product;
    var equal = recovered == left,
        smaller = difference < left;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        sum: sum,
        difference: difference,
        product: product,
        recovered: recovered,
        inverse: inverse,
        equal: equal,
        smaller: smaller
    };
}
H.push(objectFrameBigIntBinarySaved(false));
H.push(objectFrameBigIntBinarySaved(true));

// One binary SSA result can have different categories on independent paths.
function objectFrameBigIntBinaryPaths(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? 1n : 2;
    var sum = operand + operand,
        difference = sum - operand,
        product = sum * operand;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        sum: sum,
        difference: difference,
        product: product
    };
}
H.push(objectFrameBigIntBinaryPaths(false));
H.push(objectFrameBigIntBinaryPaths(true));

// Successful runtime BigInt actuals never prove an opaque future operand.
function objectFrameBigIntBinaryOpaque(operand) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var sum = operand + 2n,
        difference = operand - 2n,
        product = operand * 2n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        sum: sum,
        difference: difference,
        product: product
    };
}
H.push(objectFrameBigIntBinaryOpaque(1n));
H.push(objectFrameBigIntBinaryOpaque(2n));

// An independently computed BigInt never supplies mixed-comparison permission.
function objectFrameBigIntBinaryMixed(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? 1n : 2n,
        product = operand * 2n;
    var equal = product == 2,
        smaller = product < 3;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        product: product,
        equal: equal,
        smaller: smaller
    };
}
H.push(objectFrameBigIntBinaryMixed(false));
H.push(objectFrameBigIntBinaryMixed(true));

// Independent BigInt results do not remove a separately retained object edge.
function objectFrameBigIntBinaryRetained(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        },
        saved = target.held;
    var operand = choice ? 1n : 2n;
    var sum = operand + 2n,
        difference = operand - 2n,
        product = operand * 2n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        saved: saved,
        sum: sum,
        difference: difference,
        product: product
    };
}
H.push(objectFrameBigIntBinaryRetained(false));
H.push(objectFrameBigIntBinaryRetained(true));

// --- INDEPENDENT STATIC BIGINT RESULT CATEGORIES ----------------------------
// The earlier objectFrameStaticBinaryBigInt source now has its own BigInt
// category proof. Its historical source stays unchanged; only the claim moves.
function objectFrameBigIntStaticSaved(choice) {
    var child = {
        id: 1
    };
    var source = {
        held: child,
        left: choice ? 9007199254740993n : 9007199254740992n,
        right: 3n
    };
    var target = {
            ...source
        },
        left = target.left,
        right = source.right;
    target.left = 1;
    source.right = 0;
    delete source.left;
    delete target.left;
    delete source.right;
    delete target.right;
    var masked = left & right,
        unioned = left | right,
        toggled = left ^ right;
    var recovered = toggled ^ right,
        inverse = ~unioned,
        sum = masked + 2n;
    var equal = recovered == left,
        smaller = masked < unioned;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        masked: masked,
        unioned: unioned,
        toggled: toggled,
        recovered: recovered,
        inverse: inverse,
        sum: sum,
        equal: equal,
        smaller: smaller
    };
}
H.push(objectFrameBigIntStaticSaved(false));
H.push(objectFrameBigIntStaticSaved(true));

// One static SSA producer keeps separate Number and BigInt path categories.
function objectFrameBigIntStaticPaths(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? 1n : 2;
    var masked = operand & operand,
        unioned = operand | operand,
        toggled = operand ^ operand;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        masked: masked,
        unioned: unioned,
        toggled: toggled
    };
}
H.push(objectFrameBigIntStaticPaths(false));
H.push(objectFrameBigIntStaticPaths(true));

// Runtime BigInt actuals cannot establish an opaque future operand's category.
function objectFrameBigIntStaticOpaque(operand) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var masked = operand & 3n,
        unioned = operand | 3n,
        toggled = operand ^ 3n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        masked: masked,
        unioned: unioned,
        toggled: toggled
    };
}
H.push(objectFrameBigIntStaticOpaque(1n));
H.push(objectFrameBigIntStaticOpaque(2n));

// A static BigInt result cannot authorize a mixed-category comparison.
function objectFrameBigIntStaticMixed(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? 1n : 2n,
        masked = operand & 3n;
    var equal = masked == 2,
        smaller = masked < 3;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        masked: masked,
        equal: equal,
        smaller: smaller
    };
}
H.push(objectFrameBigIntStaticMixed(false));
H.push(objectFrameBigIntStaticMixed(true));

// Successful observed shifts do not prove their exceptional future cases.
function objectFrameBigIntStaticShift(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? 1n : 2n,
        masked = operand & 3n,
        shifted = masked << 1n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        masked: masked,
        shifted: shifted
    };
}
H.push(objectFrameBigIntStaticShift(false));
H.push(objectFrameBigIntStaticShift(true));

// Independent static results leave a separately retained object edge intact.
function objectFrameBigIntStaticRetained(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        },
        saved = target.held;
    var operand = choice ? 1n : 2n;
    var masked = operand & 3n,
        unioned = operand | 3n,
        toggled = operand ^ 3n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        saved: saved,
        masked: masked,
        unioned: unioned,
        toggled: toggled
    };
}
H.push(objectFrameBigIntStaticRetained(false));
H.push(objectFrameBigIntStaticRetained(true));

// --- INDEPENDENT SIGNED BIGINT SHIFT CATEGORIES -----------------------------
// Saved operands keep their original category after own fields are overwritten.
function objectFrameBigIntShiftSaved(choice) {
    var child = {
        id: 1
    };
    var source = {
        held: child,
        left: choice ? -9n : 9007199254740993n,
        count: 2n
    };
    var target = {
            ...source
        },
        left = target.left,
        count = source.count;
    target.left = 1;
    source.count = 0;
    delete source.left;
    delete target.left;
    delete source.count;
    delete target.count;
    var expanded = left << count,
        reduced = left >> count;
    var reverseLeft = left << -count,
        reverseRight = left >> -count;
    var recovered = expanded >> count,
        far = left >> 9007199254740993n;
    var equal = recovered == left;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        expanded: expanded,
        reduced: reduced,
        reverseLeft: reverseLeft,
        reverseRight: reverseRight,
        recovered: recovered,
        far: far,
        equal: equal
    };
}
H.push(objectFrameBigIntShiftSaved(false));
H.push(objectFrameBigIntShiftSaved(true));

// A shared shift producer separately records its Number and BigInt paths.
function objectFrameBigIntShiftPaths(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? 1n : 2;
    var expanded = operand << operand,
        reduced = operand >> operand;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        expanded: expanded,
        reduced: reduced
    };
}
H.push(objectFrameBigIntShiftPaths(false));
H.push(objectFrameBigIntShiftPaths(true));

// Observed BigInt actuals cannot prove an opaque future operand's category.
function objectFrameBigIntShiftOpaque(operand) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var expanded = operand << 1n,
        reduced = operand >> 1n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        expanded: expanded,
        reduced: reduced
    };
}
H.push(objectFrameBigIntShiftOpaque(1n));
H.push(objectFrameBigIntShiftOpaque(2n));

// The BigInt result still cannot authorize a mixed-category comparison.
function objectFrameBigIntShiftMixed(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? 1n : 2n,
        expanded = operand << 1n,
        reduced = operand >> 1n;
    var equal = expanded == 2,
        smaller = reduced < 2;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        expanded: expanded,
        reduced: reduced,
        equal: equal,
        smaller: smaller
    };
}
H.push(objectFrameBigIntShiftMixed(false));
H.push(objectFrameBigIntShiftMixed(true));

// An oversized left shift throws an independent Error before this return.
// The caller keeps that Error, while this frame's unpublished locals die.
function objectFrameBigIntShiftEarly(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var count = choice ? 9007199254740993n : 1n,
        expanded = 1n << count;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        expanded: expanded
    };
}
H.push(objectFrameBigIntShiftEarly(false));

function objectFrameBigIntShiftCatch() {
    try {
        H.push(objectFrameBigIntShiftEarly(true));
    } catch (error) {
        H.push(error);
    }
}
objectFrameBigIntShiftCatch();

// A separately retained object edge remains live across both shift results.
function objectFrameBigIntShiftRetained(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        },
        saved = target.held;
    var operand = choice ? 1n : 2n;
    var expanded = operand << 1n,
        reduced = operand >> 1n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        saved: saved,
        expanded: expanded,
        reduced: reduced
    };
}
H.push(objectFrameBigIntShiftRetained(false));
H.push(objectFrameBigIntShiftRetained(true));

// --- INDEPENDENT BIGINT DIVISION AND REMAINDER CATEGORIES -------------------
// Saved operands keep their original category after own fields are overwritten.
function objectFrameBigIntDivModSaved(choice) {
    var child = {
        id: 1
    };
    var source = {
        held: child,
        left: choice ? -9n : 9007199254740993n,
        right: 2n
    };
    var target = {
            ...source
        },
        left = target.left,
        right = source.right;
    target.left = 1;
    source.right = 0;
    delete source.left;
    delete target.left;
    delete source.right;
    delete target.right;
    var quotient = left / right,
        remainder = left % right;
    var negative = left / -right,
        signed = left % -right;
    var recovered = quotient * right + remainder,
        equal = recovered == left;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        quotient: quotient,
        remainder: remainder,
        negative: negative,
        signed: signed,
        recovered: recovered,
        equal: equal
    };
}
H.push(objectFrameBigIntDivModSaved(false));
H.push(objectFrameBigIntDivModSaved(true));

// Shared producers separately record their Number and BigInt paths.
function objectFrameBigIntDivModPaths(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? 9n : 9;
    var quotient = operand / operand,
        remainder = operand % operand;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        quotient: quotient,
        remainder: remainder
    };
}
H.push(objectFrameBigIntDivModPaths(false));
H.push(objectFrameBigIntDivModPaths(true));

// Observed BigInt actuals cannot prove an opaque future operand's category.
function objectFrameBigIntDivModOpaque(operand) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var quotient = operand / 2n,
        remainder = operand % 2n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        quotient: quotient,
        remainder: remainder
    };
}
H.push(objectFrameBigIntDivModOpaque(9n));
H.push(objectFrameBigIntDivModOpaque(-9n));

// The BigInt result still cannot authorize a mixed-category comparison.
function objectFrameBigIntDivModMixed(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? -9n : 9n,
        quotient = operand / 2n,
        remainder = operand % 2n;
    var equal = quotient == 4,
        smaller = remainder < 0;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        quotient: quotient,
        remainder: remainder,
        equal: equal,
        smaller: smaller
    };
}
H.push(objectFrameBigIntDivModMixed(false));
H.push(objectFrameBigIntDivModMixed(true));

// A zero divisor throws an independent Error before the result literal.
// Each caller keeps that Error while the callee's unpublished locals die.
function objectFrameBigIntDivModDivEarly(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var divisor = choice ? 0n : 2n,
        quotient = 9n / divisor;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        quotient: quotient
    };
}
H.push(objectFrameBigIntDivModDivEarly(false));

function objectFrameBigIntDivModDivCatch() {
    try {
        H.push(objectFrameBigIntDivModDivEarly(true));
    } catch (error) {
        H.push(error);
    }
}
objectFrameBigIntDivModDivCatch();

function objectFrameBigIntDivModModEarly(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var divisor = choice ? 0n : 2n,
        remainder = 9n % divisor;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        remainder: remainder
    };
}
H.push(objectFrameBigIntDivModModEarly(false));

function objectFrameBigIntDivModModCatch() {
    try {
        H.push(objectFrameBigIntDivModModEarly(true));
    } catch (error) {
        H.push(error);
    }
}
objectFrameBigIntDivModModCatch();

// A separately retained object edge stays live across both numeric results.
function objectFrameBigIntDivModRetained(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        },
        saved = target.held;
    var operand = choice ? -9n : 9n;
    var quotient = operand / 2n,
        remainder = operand % 2n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        saved: saved,
        quotient: quotient,
        remainder: remainder
    };
}
H.push(objectFrameBigIntDivModRetained(false));
H.push(objectFrameBigIntDivModRetained(true));

// --- INDEPENDENT BIGINT EXPONENTIATION CATEGORIES ---------------------------
// Both original operands retain BigInt categories after overwrite and deletion.
function objectFrameBigIntPowSaved(choice) {
    var child = {
        id: 1
    };
    var source = {
        held: child,
        base: choice ? -3n : 3n,
        exponent: 3n
    };
    var target = {
            ...source
        },
        base = target.base,
        exponent = source.exponent;
    target.base = 1;
    source.exponent = 0;
    delete source.base;
    delete target.base;
    delete source.exponent;
    delete target.exponent;
    var power = base ** exponent,
        squared = power ** 2n,
        unit = 0n ** 0n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        power: power,
        squared: squared,
        unit: unit
    };
}
H.push(objectFrameBigIntPowSaved(false));
H.push(objectFrameBigIntPowSaved(true));

// A shared producer has separate Number and BigInt path categories.
function objectFrameBigIntPowPaths(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? 3n : 3,
        power = operand ** operand;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        power: power
    };
}
H.push(objectFrameBigIntPowPaths(false));
H.push(objectFrameBigIntPowPaths(true));

// Observed BigInt actuals cannot prove the category of an opaque future operand.
function objectFrameBigIntPowOpaque(operand) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var power = operand ** 3n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        power: power
    };
}
H.push(objectFrameBigIntPowOpaque(3n));
H.push(objectFrameBigIntPowOpaque(-3n));

// Exact computed BigInts do not authorize mixed-category comparisons.
function objectFrameBigIntPowMixed(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var operand = choice ? -3n : 3n,
        power = operand ** 3n,
        equal = power == 27;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        power: power,
        equal: equal
    };
}
H.push(objectFrameBigIntPowMixed(false));
H.push(objectFrameBigIntPowMixed(true));

// Repeat the negative-exponent exit: each independent Error outlives the
// unpublished locals, and no result literal is allocated on the throwing path.
function objectFrameBigIntPowNegativeEarly(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var exponent = choice ? -1n : 2n,
        power = 9n ** exponent;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        power: power
    };
}
var objectFrameBigIntPowNegativeResult = objectFrameBigIntPowNegativeEarly(false);
H.push(objectFrameBigIntPowNegativeResult);

function objectFrameBigIntPowNegativeCatch() {
    try {
        return objectFrameBigIntPowNegativeEarly(true);
    } catch (error) {
        H.push(error);
        return error;
    }
}
var objectFrameBigIntPowNegativeFirst = objectFrameBigIntPowNegativeCatch();
var objectFrameBigIntPowNegativeSecond = objectFrameBigIntPowNegativeCatch();
if (!(objectFrameBigIntPowNegativeFirst instanceof RangeError) ||
    !(objectFrameBigIntPowNegativeSecond instanceof RangeError) ||
    objectFrameBigIntPowNegativeFirst === objectFrameBigIntPowNegativeSecond ||
    typeof objectFrameBigIntPowNegativeFirst.message !== "string" ||
    typeof objectFrameBigIntPowNegativeSecond.stack !== "string" ||
    objectFrameBigIntPowNegativeResult.power !== 81n) throw "BigInt Pow negative exit witness";

// The VM caps exponents even for base 1, while Node succeeds with the same tiny
// result. Keep that divergence explicit, and never attempt a huge allocation.
function objectFrameBigIntPowCapEarly(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var exponent = choice ? 4294967296n : 2n,
        power = 1n ** exponent;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        power: power
    };
}
var objectFrameBigIntPowCapResult = objectFrameBigIntPowCapEarly(false);
H.push(objectFrameBigIntPowCapResult);

function objectFrameBigIntPowCapCatch() {
    try {
        return objectFrameBigIntPowCapEarly(true);
    } catch (error) {
        H.push(error);
        return error;
    }
}
var objectFrameBigIntPowCapFirst = objectFrameBigIntPowCapCatch();
var objectFrameBigIntPowCapSecond = objectFrameBigIntPowCapCatch();
var objectFrameBigIntPowCapErrors = 0,
    objectFrameBigIntPowCapSuccess = 0;
if (objectFrameBigIntPowCapFirst instanceof RangeError) {
    if (!(objectFrameBigIntPowCapSecond instanceof RangeError) ||
        objectFrameBigIntPowCapFirst === objectFrameBigIntPowCapSecond ||
        objectFrameBigIntPowCapFirst === objectFrameBigIntPowNegativeFirst ||
        typeof objectFrameBigIntPowCapFirst.message !== "string" ||
        typeof objectFrameBigIntPowCapSecond.stack !== "string") throw "BigInt Pow cap exit witness";
    objectFrameBigIntPowCapErrors = 2;
} else {
    if (objectFrameBigIntPowCapFirst.power !== 1n ||
        objectFrameBigIntPowCapSecond.power !== 1n) throw "BigInt Pow small-base cap divergence";
    objectFrameBigIntPowCapSuccess = 2;
}
if (objectFrameBigIntPowCapResult.power !== 1n) throw "BigInt Pow small-base normal result";

// Returning a separately saved child preserves its identity through arithmetic.
function objectFrameBigIntPowRetained(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        },
        saved = target.held;
    var operand = choice ? -3n : 3n,
        power = operand ** 3n;
    delete source.held;
    delete target.held;
    return {
        source: source,
        target: target,
        saved: saved,
        power: power
    };
}
H.push(objectFrameBigIntPowRetained(false));
H.push(objectFrameBigIntPowRetained(true));
