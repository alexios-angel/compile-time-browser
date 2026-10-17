// Arrays of Strings across calls: a parameter is a read-only borrow
// (std::span<const ctnative::js_string>), or a borrow by address when the
// callee pushes to it, directly or through a callee of its own, a returned
// array moves out to its caller, and a caller owns what a call returns.
function joinAll(parts, separator) {
    return parts.join(separator);
}
function longest(words) {
    var best = '';
    for (var i = 0; i < words.length; i++) {
        if (words[i].length > best.length) {
            best = words[i];
        }
    }
    return best;
}
function build(n) {
    var out = [];
    for (var i = 0; i < n; i++) {
        out.push('w' + i);
    }
    return out;
}
function addTo(list, word) {
    list.push(word);
    return list.length;
}
function addTwice(list) {
    addTo(list, 'one');
    return addTo(list, 'two');
}
function demo() {
    var made = build(3);
    made.push('extra');
    var grown = addTwice(made);
    return joinAll(made, '+') + '|' + longest(made) + '|' + grown;
}
var result = demo();
var size = build(4).length;
