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
