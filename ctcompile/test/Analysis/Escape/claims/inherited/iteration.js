var retained; // Spreading/iterating reads inherited "0", whose getter saves `this`.
var seed = {
    length: 1
};
Object.defineProperty(Object.prototype, "0", {
    get: function () {
        retained = this;
        return 42;
    }
});

function iterateInherited(input) {
    var local = {
        ...input
    };
    for (var item of local) {}
    return 0;
}
iterateInherited(seed);
