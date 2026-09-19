function objectFrameLooseEqualityRelational(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var input = choice ? "17." : 0;
    var less = input < 17,
        atMost = input <= 17,
        greater = input > 17,
        atLeast = input >= 17;
    var selected = less ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        less: less,
        atMost: atMost,
        greater: greater,
        atLeast: atLeast
    };
}
H.push(objectFrameLooseEqualityRelational(false));
H.push(objectFrameLooseEqualityRelational(true));

// The original Released function above is preserved as a global-lookup refusal:
// bare undefined imports as load_global. This exact repair uses literal void 0,
// which imports as constant Undefined. Both original runtime observations agree.
function objectFrameLooseEqualityLiteral(choice) {
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
        other = choice ? 18 : void 0;
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
H.push(objectFrameLooseEqualityLiteral(false));
H.push(objectFrameLooseEqualityLiteral(true));

// Relational operands retain their original primitive identity when their
// own fields are replaced with BigInt and then deleted. String/String order
// differs from numeric order; every result and selected container is observable.
function objectFrameRelationalSaved(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    source.input = choice ? "20" : "3";
    var saved = source.input;
    source.input = 1n;
    delete source.input;
    var less = saved < "3",
        atMost = saved <= "3";
    var greater = saved > "3",
        atLeast = saved >= "3",
        numeric = saved < 3;
    var reversed = "3" < saved,
        selected = less ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        less: less,
        atMost: atMost,
        greater: greater,
        atLeast: atLeast,
        numeric: numeric,
        reversed: reversed
    };
}
H.push(objectFrameRelationalSaved(false));
H.push(objectFrameRelationalSaved(true));

// Undefined and an invalid numeric String both produce unordered comparison,
// so <= and >= cannot be implemented as negated > and <. All structural arms
// still need a complete contents proof despite both observed selectors false.
function objectFrameRelationalUnordered(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var input = choice ? "not-a-number" : void 0,
        other = choice ? 1 : 0;
    var less = input < other,
        atMost = input <= other;
    var greater = input > other,
        atLeast = input >= other;
    var reversed = other <= input,
        selected = less ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        less: less,
        atMost: atMost,
        greater: greater,
        atLeast: atLeast,
        reversed: reversed
    };
}
H.push(objectFrameRelationalUnordered(false));
H.push(objectFrameRelationalUnordered(true));

// Executing primitive actuals cannot prove an opaque formal for future calls.
function objectFrameRelationalOpaque(input) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var less = input < 17,
        atMost = input <= 17;
    var greater = input > 17,
        atLeast = input >= 17;
    var selected = less ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        less: less,
        atMost: atMost,
        greater: greater,
        atLeast: atLeast
    };
}
H.push(objectFrameRelationalOpaque(0));
H.push(objectFrameRelationalOpaque("17."));

// Successful BigInt comparisons remain outside the non-BigInt origin proof.
function objectFrameRelationalBigInt(choice) {
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
    var less = input < 2n,
        atMost = input <= 2n;
    var greater = input > 2n,
        atLeast = input >= 2n;
    var selected = less ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        less: less,
        atMost: atMost,
        greater: greater,
        atLeast: atLeast
    };
}
H.push(objectFrameRelationalBigInt(false));
H.push(objectFrameRelationalBigInt(true));

// Deleting every container field does not release an independently saved
// child that is itself returned. Primitive selectors cannot erase that alias.
function objectFrameRelationalRetained(choice) {
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
    var input = choice ? null : false,
        less = input < 1;
    var selected = less ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        saved: saved,
        less: less
    };
}
H.push(objectFrameRelationalRetained(false));
H.push(objectFrameRelationalRetained(true));

// Saved String operands keep their original value through a BigInt field
// overwrite and deletion. Every arithmetic result and selected alias survives.
function objectFrameArithmeticBinarySaved(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    source.input = choice ? "20" : "-0";
    var saved = source.input;
    source.input = 1n;
    delete source.input;
    var difference = saved - 3,
        product = saved * -2,
        quotient = 1 / saved;
    var remainder = saved % 2,
        power = saved ** 2;
    var selected = difference < 0 ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        difference: difference,
        product: product,
        quotient: quotient,
        remainder: remainder,
        power: power
    };
}
H.push(objectFrameArithmeticBinarySaved(false));
H.push(objectFrameArithmeticBinarySaved(true));

// Number arithmetic stays primitive for NaN, infinity and signed zero. Pow's
// +/-1 to NaN differs from libm; neither NaN selector chooses its true arm.
function objectFrameArithmeticBinaryNumbers(choice) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var input = choice ? 1 : -1,
        nan = (void 0) - 1;
    var quotient = input / 0,
        remainder = input % 0,
        power = input ** nan;
    var product = (choice ? 0 : -0) * -2;
    var selected = power ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        nan: nan,
        product: product,
        quotient: quotient,
        remainder: remainder,
        power: power
    };
}
H.push(objectFrameArithmeticBinaryNumbers(false));
H.push(objectFrameArithmeticBinaryNumbers(true));

// Primitive actual observations cannot prove this formal's future conversions.
function objectFrameArithmeticBinaryOpaque(input) {
    var child = {
        id: 1
    };
    var source = {
            held: child
        },
        target = {
            ...source
        };
    var difference = input - 3,
        product = input * 2,
        quotient = input / 2;
    var remainder = input % 2,
        power = input ** 2;
    var selected = difference < 0 ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        difference: difference,
        product: product,
        quotient: quotient,
        remainder: remainder,
        power: power
    };
}
H.push(objectFrameArithmeticBinaryOpaque(2));
H.push(objectFrameArithmeticBinaryOpaque("5."));

// Successful BigInt results do not supply a primitive Number origin proof.
function objectFrameArithmeticBinaryBigInt(choice) {
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
    var difference = input - 3n,
        product = input * 2n,
        quotient = input / 2n;
    var remainder = input % 2n,
        power = input ** 2n;
    var selected = difference < 0n ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        difference: difference,
        product: product,
        quotient: quotient,
        remainder: remainder,
        power: power
    };
}
H.push(objectFrameArithmeticBinaryBigInt(false));
H.push(objectFrameArithmeticBinaryBigInt(true));

// Deleting both container edges cannot release the independently returned child.
function objectFrameArithmeticBinaryRetained(choice) {
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
        input = choice ? "5." : "2";
    var difference = input - 3,
        product = input * 2,
        quotient = input / 2;
    var remainder = input % 2,
        power = input ** 2;
    var selected = difference < 0 ? source : target;
    delete source.held;
    delete target.held;
    return {
        selected: selected,
        source: source,
        target: target,
        saved: saved,
        difference: difference,
        product: product,
        quotient: quotient,
        remainder: remainder,
        power: power
    };
}
H.push(objectFrameArithmeticBinaryRetained(false));
H.push(objectFrameArithmeticBinaryRetained(true));
