var retained, receiver;
Object.defineProperty(Object.prototype, "nd3retain", {
    set: function (value) {
        retained = value;
        receiver = this;
    }
});

function writeInherited() {
    var child = {};
    var local = {};
    local.nd3retain = child;
    delete local.nd3retain;
    return 0;
}
writeInherited();
