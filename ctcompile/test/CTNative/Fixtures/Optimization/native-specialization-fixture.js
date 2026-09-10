// Calls with identical static tuples share a variant. Each function retains
// one generic call; source-visible effects and argument evaluation stay ordered.
function formula(scale, offset, input) {
    return (scale * 10 + offset) * input;
}
function mixed(input) {
    return formula(2, 3, input) + formula(4, 5, input) + formula(4, 5, input);
}
function seed(seed, input) {
    const map = new Map();
    map.set("value", 0);
    map.set("value", seed * 10 + 2);
    const saved = map.get("value");
    map.set("value", input);
    return saved * 100 + map.get("value");
}
function heaps(input) {
    // Keep heap consumers at runtime while their initialization specializes.
    heapEffect = input;
    return seed(1, input) + seed(4, input) + seed(4, input);
}
function publish(factor, input) {
    published = factor * 10 + input;
    return factor * 100 + input;
}
function effects(input) {
    return publish(1, input) + publish(2, input) + publish(2, input);
}
function remember(map, value) {
    // Keep the argument producer at runtime even when its input is static.
    // The Map trace below also observes evaluation order and duplicate calls.
    argumentEffect = value;
    map.set("trace", map.get("trace") * 10 + value);
    return value;
}
function ordered(input) {
    const map = new Map();
    map.set("trace", 0);
    const result = formula(2, 3, remember(map, input)) +
        formula(4, 5, remember(map, input + 1));
    return result * 100 + map.get("trace");
}
function sign(negative, input) {
    if (negative) { return -input; }
    return input;
}
function coercion(value, input) {
    return +value + input;
}
function recurse(count) {
    if (count < 1) { return 1; }
    return count * recurse(count - 1);
}
var published = 0;
var argumentEffect = 0;
var heapEffect = 0;
var mixedFirst = mixed(1);
var mixedSecond = mixed(2);
var heapFirst = heaps(3);
var heapSecond = heaps(7);
var effectsFirst = effects(3);
var effectsSecond = effects(7);
var orderedResult = ordered(2);
var signFirst = sign(false, 3);
var signSecond = sign(true, 4);
var signThird = sign(true, 5);
var nullFirst = coercion(null, 1);
var nullSecond = coercion(null, 2);
var boolFirst = coercion(true, 3);
var boolSecond = coercion(true, 4);
var recursionFirst = recurse(4);
var recursionSecond = recurse(5);
var lifetime42 = formula(4, 2, 1);
