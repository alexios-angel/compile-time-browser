var retained;
Object.defineProperty(Object.prototype, "nd3retain", {
    get: function () {
        retained = this;
        return 42;
    }
});

function readInherited() {
    var local = {};
    return local.nd3retain;
}
readInherited();
