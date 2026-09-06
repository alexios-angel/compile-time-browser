// Source names must survive heap staging, joins, loops and closure lifting.
function makeCatalog() {
    const item = new Map();
    item.set("price", 10);
    const catalog = new Map();
    catalog.set("featured", item);
    catalog.set("backup", item);
    return catalog;
}
function observePrice(catalog) {
    if (!catalog.has("featured")) { return -1; }
    return +catalog.get("featured").get("price");
}
function observeSharing(first, second) {
    var score = -1;
    if (first.has("featured")) {
        if (first.has("backup")) {
            if (second.has("backup")) {
                first.get("featured").set("price", 25);
                score = first.get("backup").get("price") * 100 + second.get("backup").get("price");
            }
        }
    }
    return score;
}
function collision(double, v1, NAN, _private, score$1, score_1) {
    const score = double + v1 + NAN + _private + score$1 + score_1;
    return score;
}
function accumulate(limit) {
    var score = 0;
    for (var i = 0; i < limit; ++i) { score = score + i; }
    return score;
}
function makeReader(catalog) { return offset => catalog + offset; }
function scopeReuse(input) {
    var score = input;
    { let entry = 5; score = score + entry; }
    { let entry = 7; score = score + entry; }
    return score;
}
function macroTokens(name, site) {
    const is_same_v = name + site;
    return is_same_v;
}
function observeClosure() {
    const read = makeReader(10);
    return read(3);
}
var aliasScore = observeSharing(makeCatalog(), makeCatalog());
var initialPrice = observePrice(makeCatalog());
var collisionScore = collision(1, 2, 3, 4, 5, 6);
var loopScore = accumulate(5);
var closureScore = observeClosure();
var reusedScore = scopeReuse(1);
var macroScore = macroTokens(5, 7);
