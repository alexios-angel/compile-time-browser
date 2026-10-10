// Arrays of Strings across calls: a parameter is a read-only borrow
// (std::span<const ctnative::js_string>), a returned array moves out to its
// caller, and a caller owns what a call returns.
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
function demo() {
    var made = build(3);
    made.push('extra');
    return joinAll(made, '+') + '|' + longest(made);
}
var result = demo();
var size = build(4).length;
