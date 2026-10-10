// The first live call keeps the generic `seed` and its runtime type evidence.
// The calls in `initialize` create variants whose only callers disappear
// during partial evaluation, leaving their bodies with no references.
function seed(offset, input) {
    const map = new Map();
    map.set("base", offset * 10);
    return map.get("base") + input;
}

function runtime(input) {
    effect = input;
    return seed(1, input);
}

function initialize() {
    return seed(2, 3) + seed(4, 5);
}
var effect = 0;
var staged = initialize();
var observed = runtime(7);
var lifetime42 = 6 * 7;
