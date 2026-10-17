// for-of over arrays this tier holds, as index loops: an array of Strings
// from split, an array of Numbers from map, a loop left by `break`, and
// `[name, value]` rows of a pair vector destructured in the loop head.
// Every global is a scalar compared with the interpreter.
function total(text) {
    var sum = 0;
    for (const p of text.split(',')) {
        sum = sum + Number.parseInt(p, 10);
    }
    return sum;
}
function longest(text) {
    var best = 0;
    for (const n of text.split(',').map(function (t) {
        return t.length;
    })) {
        if (n > best) {
            best = n;
        }
    }
    return best;
}
function firstBig(text) {
    var found = -1;
    for (const p of text.split(',')) {
        if (Number.parseInt(p, 10) > 5) {
            found = Number.parseInt(p, 10);
            break;
        }
    }
    return found;
}
function rows() {
    var lines = [];
    var pairs = [];
    pairs.push(['@x', 1.5]);
    pairs.push(['color', 'red']);
    for (const [name, value] of pairs) {
        lines.push(name + '=' + value);
    }
    return lines.join(' ');
}
var a = total('1,2,3');
var b = longest('a,bbb,cc');
var c = firstBig('1,7,9');
var d = firstBig('1,2');
var e = rows();
