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
