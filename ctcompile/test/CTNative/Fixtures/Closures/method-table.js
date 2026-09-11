// Method fields own their callable and captured state after factory return.
function makeData(seed) {
    const state = new Map();
    state.set("value", seed);
    return {
        get(delta) { return state.get("value") + delta; },
        set(value) { state.set("value", value); return state.get("value") + 0; },
        remove() { return state.delete("value") ? 1 : 0; }
    };
}
function forwardTable(table) { return table; }
function readTable(table, delta) { return table.get(delta); }
function tableLifetime() {
    const first = makeData(40);
    const alias = forwardTable(first);
    const second = makeData(10);
    const before = readTable(first, 2);
    alias.set(41);
    const shared = first.get(1);
    const independent = second.get(2);
    const removed = alias.remove();
    const removedAgain = first.remove();
    return before + shared * 100 + independent * 10000 + removed * 1000000 + removedAgain;
}
function retainedTable() {
    const retained = makeData(40);
    for (var i = 0; i < 100; ++i) {
        const temporary = makeData(i);
        temporary.set(i + 1);
        temporary.get(1);
        temporary.remove();
    }
    return retained.get(2);
}
function makeLabeled(seed, label) {
    const state = new Map();
    state.set("value", seed);
    const text = label + "-a-string-long-enough-to-need-owned-storage";
    return {
        get(delta) { return state.get("value") + delta + (label === "aa" ? 2 : 4); },
        set(value) { state.set("value", value); return state.get("value") + 0; },
        name(suffix) { return text + suffix; }
    };
}
function readLabeled(table, delta) { return table.get(delta); }
function labeledInstances() {
    const first = makeLabeled(10, "aa");
    const alias = first;
    const second = makeLabeled(20, "bbbb");
    const before = readLabeled(first, 1) * 100 + readLabeled(second, 1);
    alias.set(40);
    const text = first.name("!") === "aa-a-string-long-enough-to-need-owned-storage!" ? 1 : 0;
    const otherText = second.name("?") === "bbbb-a-string-long-enough-to-need-owned-storage?" ? 1 : 0;
    return before * 10000 + readLabeled(first, 1) * 100 + readLabeled(second, 1) + text + otherText;
}
function makeScalar(seed) { return { get(delta) { return seed + delta; } }; }
function liftedTableCall() {
    const extra = 20;
    return (function(value) {
        const table = makeScalar(10);
        return table.get(value) + extra;
    })(12);
}
function makeSignatures(seed) {
    const state = new Map();
    return {
        store(value) { state.set("key", value); return true; },
        read(delta) { return state.get("key") + seed + delta; },
        name(value) { return "x" + value; }
    };
}
function differentSignatures() {
    const table = makeSignatures(10);
    const ok = table.store(30);
    return (ok ? table.read(0) : 0) + (table.name("y") === "xy" ? 2 : 0);
}
function makeEmptyTable() { return { twice(value) { return value * 2; } }; }
function emptyEnvironment() {
    const marker = {table_0: 1};
    const environment = {env_fn_2: 1};
    const table = makeEmptyTable();
    return table.twice(20) + marker.table_0 + environment.env_fn_2;
}
function missingArgument() { const table = makeScalar(10); return table.get(); }
var sharedTables = tableLifetime();
var lifetime42 = retainedTable();
var labels = labeledInstances();
var lifted = liftedTableCall();
var signatures = differentSignatures();
var empty = emptyEnvironment();
var missing = missingArgument();
