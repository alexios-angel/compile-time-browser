var startup42 = (function initialize(factory) {
    return factory();
})(function bootstrapFactory() { return 42; });

var branch21 = (function (mode, factory) {
    if (mode > 0) { return factory(10) + 1; }
    return factory(20) + 2;
})(1, function (value) { return value * 2; });

var other42 = (function (mode, factory) {
    if (mode > 0) { return factory(10) + 1; }
    return factory(20) + 2;
})(-1, function (value) { return value * 2; });

function repeated(seed) {
    var factory = function (value) { return value + 3; };
    var wrapper = function (callback, value) { return callback(value) * 2; };
    return wrapper(factory, seed) + wrapper(factory, seed + 1);
}
var repeated30 = repeated(4);

function ordered(seed) {
    var count = seed;
    var next = function () { count = count * 10 + 1; return count; };
    var wrapper = function (factory, left, right) { return factory(left, right); };
    var answer = wrapper(function (left, right) { return left * 1000 + right; }, next(), next());
    return answer + count;
}
var ordered11222 = ordered(1);

var padded73 = (function (factory) {
    return factory(7);
})(function (left, right) { return left * 10 + (right || 3); });

var two31 = (function (first, second) {
    return first() + second();
})(function () { return 10; }, function () { return 21; });

var loop18 = (function (factory, count) {
    var total = 0;
    for (var index = 0; index < count; index = index + 1) {
        total = total + factory(index);
    }
    return total;
})(function (value) { return value * 3; }, 4);
