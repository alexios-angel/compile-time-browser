var added = new Function('a', 'b', 'return a + b')(20, 22);
var called = Function('return 7')();
var nested = Function('return function (n) { return n + 1; }')()(8);
var indirect = (0, eval)('6 * 7');
var globalValue = 100;
var isolated = (function () {
    var globalValue = 1;
    return Function('return globalValue')();
})();
var sideEffects = 0;
var completion = (0, eval)('40 + 2', sideEffects = sideEffects + 1);

function addRuntime(n) {
    return Function('x', 'return x + 2')(n);
}
var runtime = addRuntime(globalValue);
