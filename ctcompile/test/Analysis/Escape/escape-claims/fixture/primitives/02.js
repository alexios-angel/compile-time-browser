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
