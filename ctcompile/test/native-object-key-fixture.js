// Empty object keys carry owning identity across calls and factory lifetimes.
function makeKey() { return {}; }
function forwardKey(key) { return key; }
function readKey(map, key) { return map.get(key) + 0; }
function keyAliases() {
    const first = makeKey();
    const second = makeKey();
    const alias = forwardKey(first);
    const map = new Map();
    map.set(first, 20);
    map.set(second, 21);
    map.set(alias, 22);
    return map.size * 10000 + readKey(map, first) * 100 + readKey(map, second);
}
function keyOperations() {
    const first = {};
    const second = {};
    const absent = {};
    const map = new Map();
    map.set(first, 40);
    map.set(second, 2);
    const before = (map.has(first) ? 1 : 0) + (map.has(absent) ? 10 : 0);
    const missing = map.delete(absent) ? 100 : 0;
    const removed = map.delete(first) ? 1000 : 0;
    const left = readKey(map, second);
    map.clear();
    return before + missing + removed + left + map.size;
}
function makeKeyedMap() {
    const map = new Map();
    map.set(makeKey(), 42);
    return map;
}
function retainedKeys() {
    const map = makeKeyedMap();
    for (var i = 0; i < 100; ++i) { map.set(makeKey(), i); }
    return map.size * 1000 + map.values()[0];
}
function retainedAlias() {
    const key = forwardKey(makeKey());
    const map = new Map();
    map.set(key, 42);
    for (var i = 0; i < 100; ++i) { makeKeyedMap(); }
    return readKey(map, key);
}
function makeData() {
    const entries = new Map();
    return {
        set(key, value) {
            if (!entries.has(key)) { entries.set(key, new Map()); }
            const data = entries.get(key);
            data.set("value", value);
            return value;
        },
        get(key) {
            if (entries.has(key)) { return entries.get(key).get("value") + 0; }
            return 0;
        },
        remove(key) {
            if (entries.has(key)) {
                const data = entries.get(key);
                data.delete("value");
                if (data.size === 0) { entries.delete(key); }
                return 1;
            }
            return 0;
        }
    };
}
function dataLifetime() {
    const first = makeKey();
    const other = makeKey();
    const missing = makeKey();
    const data = makeData();
    data.set(first, 40);
    data.set(other, 2);
    const before = data.get(first) + data.get(other);
    const absent = data.get(missing);
    data.set(first, 41);
    const replaced = data.get(first);
    const removed = data.remove(first);
    const repeated = data.remove(first);
    const remaining = data.get(other);
    data.set(first, 42);
    return before * 1000000 + replaced * 10000 + removed * 1000 +
        remaining * 100 + data.get(first) + absent + repeated;
}
var aliases = keyAliases();
var operations = keyOperations();
var retained = retainedKeys();
var lifetime42 = retainedAlias();
var data = dataLifetime();
