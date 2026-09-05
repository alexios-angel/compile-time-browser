// The first live call keeps the generic helper's runtime type evidence.
// The later initializer calls create variants whose only callers disappear
// during PE; their bodies have no remaining symbolic or numeric references.
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
