function primitiveUShrEarly(choice) {
    var child = {},
        items = [child],
        value, shift;
    if (choice) {
        value = 8n;
        shift = 1n;
    } else {
        value = 8;
        shift = 1;
    }
    var result = value >>> shift;
    items[0] = result;
    return items;
}

function primitiveUShrRetained(choice) {
    var child = {},
        items = [child],
        saved = items[0],
        value, shift;
    if (choice) {
        value = 8n;
        shift = 1n;
    } else {
        value = 8;
        shift = 1;
    }
    var result = value >>> shift;
    items[0] = result;
    return saved;
}

function primitiveUShrOpaque(value, shift) {
    var child = {},
        items = [child];
    var result = value >>> shift;
    items[0] = result;
    return items;
}

function primitiveUShrCatch(fn, input, shift) {
    try {
        return fn(input, shift);
    } catch (error) {
        H.push(error);
        return error;
    }
}
var primitiveUShrNormal = primitiveUShrEarly(false);
var primitiveUShrSaved = primitiveUShrRetained(false);
var primitiveUShrUnknown = primitiveUShrOpaque(8, 1);
H.push(primitiveUShrNormal);
H.push(primitiveUShrSaved);
H.push(primitiveUShrUnknown);
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
    var child = {},
        items = [child],
        value, shift;
    if (choice) {
        value = 8n;
        shift = 1;
    } else {
        value = 8;
        shift = 1;
    }
    var result = value >> shift;
    items[0] = result;
    return items;
}

function primitiveMixedStaticRetained(choice) {
    var child = {},
        items = [child],
        saved = items[0],
        value, shift;
    if (choice) {
        value = 8;
        shift = 1n;
    } else {
        value = 8;
        shift = 1;
    }
    var result = value >> shift;
    items[0] = result;
    return saved;
}

function primitiveMixedStaticOpaque(value, shift) {
    var child = {},
        items = [child];
    var result = value >> shift;
    items[0] = result;
    return items;
}
var primitiveMixedStaticNormal = primitiveMixedStaticEarly(false);
var primitiveMixedStaticSaved = primitiveMixedStaticRetained(false);
var primitiveMixedStaticUnknown = primitiveMixedStaticOpaque(8, 1);
H.push(primitiveMixedStaticNormal);
H.push(primitiveMixedStaticSaved);
H.push(primitiveMixedStaticUnknown);
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
    var child = {},
        items = [child],
        value, shift;
    if (choice) {
        value = 8n;
        shift = 1;
    } else {
        value = 8;
        shift = 1;
    }
    var result = value + shift;
    items[0] = result;
    return items;
}

function primitiveMixedAddRetained(choice) {
    var child = {},
        items = [child],
        saved = items[0],
        value, shift;
    if (choice) {
        value = 8;
        shift = 1n;
    } else {
        value = 8;
        shift = 1;
    }
    var result = value + shift;
    items[0] = result;
    return saved;
}

function primitiveMixedAddOpaque(value, shift) {
    var child = {},
        items = [child];
    var result = value + shift;
    items[0] = result;
    return items;
}
var primitiveMixedAddNormal = primitiveMixedAddEarly(false);
var primitiveMixedAddSaved = primitiveMixedAddRetained(false);
var primitiveMixedAddUnknown = primitiveMixedAddOpaque(8, 1);
H.push(primitiveMixedAddNormal);
H.push(primitiveMixedAddSaved);
H.push(primitiveMixedAddUnknown);
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
    var child = {},
        items = [child];
    items["0"] = 9;
    return items;
}

function canonicalStringSaved() {
    var child = {},
        items = [child],
        saved = items["0"];
    items["0"] = 9;
    return saved;
}

function canonicalStringLookalike() {
    var child = {},
        items = [child];
    items["00"] = 9;
    return items;
}

function canonicalStringLoaded() {
    var child = {},
        items = [child],
        keys = ["0"];
    var key = keys[0],
        saved = items[key];
    items[key] = 9;
    return [items, saved === child];
}
var canonicalStringNormal = canonicalStringReleased();
var canonicalStringRetained = canonicalStringSaved();
var canonicalStringNamed = canonicalStringLookalike();
var canonicalStringForwarded = canonicalStringLoaded();
H.push(canonicalStringNormal);
H.push(canonicalStringRetained);
H.push(canonicalStringNamed);
H.push(canonicalStringForwarded);
if (canonicalStringNormal[0] !== 9 || typeof canonicalStringNormal[0] !== "number" ||
    typeof canonicalStringRetained !== "object" || canonicalStringRetained === null ||
    Array.isArray(canonicalStringRetained) ||
    typeof canonicalStringNamed[0] !== "object" || canonicalStringNamed[0] === null ||
    canonicalStringNamed["00"] !== 9 || canonicalStringNamed.length !== 1 ||
    canonicalStringForwarded[0][0] !== 9 || typeof canonicalStringForwarded[0][0] !== "number" ||
    canonicalStringForwarded[1] !== true) throw "canonical String index and saved identity witness";

// --- DECIMAL BIGINT INDICES: exact dense slots, saved identity, literal origins ---
function decimalBigIntReleased() {
    var child = {},
        items = [child];
    items[0n] = 9;
    return items;
}

function decimalBigIntSaved() {
    var child = {},
        items = [child],
        saved = items[0n];
    items[0n] = 9;
    return saved;
}

function decimalBigIntSecond() {
    var child = {},
        items = [0, child];
    items[1n] = 9;
    return items;
}

function decimalBigIntLoaded() {
    var child = {},
        items = [child],
        keys = [0n];
    var key = keys[0],
        saved = items[key];
    items[key] = 9;
    return [items, saved === child];
}

function decimalBigIntNegative() {
    var child = {},
        items = [child];
    items[-1n] = 9;
    return items;
}
var decimalBigIntNormal = decimalBigIntReleased();
var decimalBigIntRetained = decimalBigIntSaved();
var decimalBigIntOne = decimalBigIntSecond();
var decimalBigIntForwarded = decimalBigIntLoaded();
var decimalBigIntNamed = decimalBigIntNegative();
H.push(decimalBigIntNormal);
H.push(decimalBigIntRetained);
H.push(decimalBigIntOne);
H.push(decimalBigIntForwarded);
H.push(decimalBigIntNamed);
if (decimalBigIntNormal[0] !== 9 || typeof decimalBigIntNormal[0] !== "number" ||
    typeof decimalBigIntRetained !== "object" || decimalBigIntRetained === null ||
    Array.isArray(decimalBigIntRetained) ||
    decimalBigIntOne[0] !== 0 || decimalBigIntOne[1] !== 9 || decimalBigIntOne.length !== 2 ||
    decimalBigIntForwarded[0][0] !== 9 || typeof decimalBigIntForwarded[0][0] !== "number" ||
    decimalBigIntForwarded[1] !== true ||
    typeof decimalBigIntNamed[0] !== "object" || decimalBigIntNamed[0] === null ||
    decimalBigIntNamed["-1"] !== 9 || decimalBigIntNamed.length !== 1)
    throw "decimal BigInt index and saved identity witness";

// --- DENSE ARRAY LENGTH: independent Numbers, saved children and exact keys ---
function denseLengthReleased() {
    var child = {},
        items = [child];
    var before = items.length;
    items[0] = 9;
    return [items, before];
}

function denseLengthSaved() {
    var child = {},
        items = [child],
        saved = items[0];
    var before = items.length;
    items[0] = 9;
    return [items, saved, before];
}

function denseLengthLoaded() {
    var child = {},
        items = [child],
        keys = ["length"];
    var key = keys[0],
        before = items[key];
    keys[0] = "0";
    items[0] = 9;
    return [items, before, keys[0]];
}

function denseLengthEmpty() {
    var child = {},
        empty = [],
        items = [child];
    var before = empty.length;
    items[0] = 9;
    return [items, before];
}

function denseLengthIndexed() {
    var child = {},
        items = [child];
    var before = items.length;
    items[before - 1] = 9;
    return [items, before];
}

function denseLengthChanged() {
    var child = {},
        items = [child];
    var before = items.length;
    items.length = 0;
    return [items, before];
}
var denseLengthNormal = denseLengthReleased();
var denseLengthRetained = denseLengthSaved();
var denseLengthForwarded = denseLengthLoaded();
var denseLengthZero = denseLengthEmpty();
var denseLengthComputed = denseLengthIndexed();
var denseLengthWritten = denseLengthChanged();
H.push(denseLengthNormal);
H.push(denseLengthRetained);
H.push(denseLengthForwarded);
H.push(denseLengthZero);
H.push(denseLengthComputed);
H.push(denseLengthWritten);
if (denseLengthNormal[0][0] !== 9 || denseLengthNormal[1] !== 1 ||
    typeof denseLengthNormal[1] !== "number" ||
    denseLengthRetained[0][0] !== 9 || denseLengthRetained[2] !== 1 ||
    typeof denseLengthRetained[1] !== "object" || denseLengthRetained[1] === null ||
    Array.isArray(denseLengthRetained[1]) ||
    denseLengthForwarded[0][0] !== 9 || denseLengthForwarded[1] !== 1 ||
    denseLengthForwarded[2] !== "0" ||
    denseLengthZero[0][0] !== 9 || denseLengthZero[1] !== 0 ||
    denseLengthComputed[0][0] !== 9 || denseLengthComputed[1] !== 1 ||
    denseLengthWritten[0].length !== 0 || denseLengthWritten[1] !== 1)
    throw "dense array length and saved child witness";

// --- NONDECIMAL BIGINT INDICES: exact slots, saved origins, String separation ---
function radixBigIntReleased() {
    var child = {},
        items = [child, child, child];
    items[0x0n] = 7;
    items[0O1n] = 8;
    items[0b10n] = 9;
    return items;
}

function radixBigIntSaved() {
    var child = {},
        items = [0, child],
        saved = items[0B1n];
    items[0o1n] = 9;
    return [items, saved, saved === child];
}

function radixBigIntLoaded() {
    var child = {},
        items = [child],
        keys = [0X0n];
    var key = keys[0];
    keys[0] = 1n;
    items[key] = 9;
    return [items, key === 0n];
}

function radixBigIntString() {
    var child = {},
        items = [child];
    items["0x0"] = 9;
    return items;
}

function radixBigIntComputed() {
    var child = {},
        items = [child];
    var key = 0x1n - 0b1n;
    items[key] = 9;
    return items;
}
var radixBigIntNormal = radixBigIntReleased();
var radixBigIntRetained = radixBigIntSaved();
var radixBigIntForwarded = radixBigIntLoaded();
var radixBigIntNamed = radixBigIntString();
var radixBigIntCalculated = radixBigIntComputed();
H.push(radixBigIntNormal);
H.push(radixBigIntRetained);
H.push(radixBigIntForwarded);
H.push(radixBigIntNamed);
H.push(radixBigIntCalculated);
if (radixBigIntNormal[0] !== 7 || radixBigIntNormal[1] !== 8 ||
    radixBigIntNormal[2] !== 9 || radixBigIntNormal.length !== 3 ||
    radixBigIntRetained[0][0] !== 0 || radixBigIntRetained[0][1] !== 9 ||
    typeof radixBigIntRetained[1] !== "object" || radixBigIntRetained[1] === null ||
    Array.isArray(radixBigIntRetained[1]) || radixBigIntRetained[2] !== true ||
    radixBigIntForwarded[0][0] !== 9 || radixBigIntForwarded[1] !== true ||
    typeof radixBigIntNamed[0] !== "object" || radixBigIntNamed[0] === null ||
    radixBigIntNamed["0x0"] !== 9 || radixBigIntNamed.length !== 1 ||
    radixBigIntCalculated[0] !== 9 || radixBigIntCalculated.length !== 1)
    throw "nondecimal BigInt index and saved origin witness";

// --- DENSE LENGTH INDICES: saved Numbers, exact offsets and structural paths ---
function denseIndexSaved() {
    var child = {},
        items = [child],
        before = items.length;
    var index = before - 1,
        saved = items[index];
    items[index] = 9;
    return [items, saved, saved === child];
}

function denseIndexLoaded() {
    var child = {},
        items = [child],
        before = items.length;
    var keys = [before - 1],
        index = keys[0];
    keys[0] = child;
    items[index] = 9;
    return [items, index];
}

function denseIndexPaths(choice) {
    var child = {},
        items;
    if (choice) items = [child];
    else items = [child, child];
    var before = items.length;
    items[before - 1] = 9;
    return [items, before];
}

function denseIndexStringOffset() {
    var child = {},
        items = [child],
        before = items.length;
    items[before - "1"] = 9;
    return items;
}

function denseIndexChain() {
    var child = {},
        items = [child, 0],
        before = items.length;
    var last = before - 1;
    items[last - 1] = 9;
    return [items, last];
}
var denseIndexRetained = denseIndexSaved();
var denseIndexForwarded = denseIndexLoaded();
var denseIndexSingle = denseIndexPaths(true);
var denseIndexDouble = denseIndexPaths(false);
var denseIndexCoerced = denseIndexStringOffset();
var denseIndexChained = denseIndexChain();
H.push(denseIndexRetained);
H.push(denseIndexForwarded);
H.push(denseIndexSingle);
H.push(denseIndexDouble);
H.push(denseIndexCoerced);
H.push(denseIndexChained);
if (denseIndexRetained[0][0] !== 9 ||
    typeof denseIndexRetained[1] !== "object" || denseIndexRetained[1] === null ||
    Array.isArray(denseIndexRetained[1]) || denseIndexRetained[2] !== true ||
    denseIndexForwarded[0][0] !== 9 || denseIndexForwarded[1] !== 0 ||
    typeof denseIndexForwarded[1] !== "number" ||
    denseIndexSingle[0].length !== 1 || denseIndexSingle[0][0] !== 9 ||
    denseIndexSingle[1] !== 1 || denseIndexDouble[0].length !== 2 ||
    typeof denseIndexDouble[0][0] !== "object" || denseIndexDouble[0][0] === null ||
    Array.isArray(denseIndexDouble[0][0]) || denseIndexDouble[0][1] !== 9 ||
    denseIndexDouble[1] !== 2 || denseIndexCoerced[0] !== 9 ||
    denseIndexChained[0][0] !== 9 || denseIndexChained[0][1] !== 0 ||
    denseIndexChained[1] !== 1)
    throw "dense length index and saved Number witness";
