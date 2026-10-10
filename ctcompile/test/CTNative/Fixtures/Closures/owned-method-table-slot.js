// Six functions, the script entry included. The local container `ns` dies
// before the caller uses the method table it held in its `exports` field.
function makeData(seed) {
    const state = new Map();
    state.set("value", seed);
    return {
        get() {
            return state.get("value") + 0;
        },
        set(value) {
            state.set("value", value);
            return 0;
        }
    };
}

function publish(seed) {
    const ns = {
        exports: makeData(seed)
    };
    return ns.exports;
}

function run() {
    const first = publish(40),
        second = publish(10);
    first.set(42);
    second.set(11);
    return first.get() * 100 + second.get();
}
var result = run();
