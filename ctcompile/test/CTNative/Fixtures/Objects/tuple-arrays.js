// Arrays of two-element rows lower to std::vector<std::pair<...>>: css-dump.js's
// `pairs`, a [name, value] row per property with a Number or a String value,
// read back in a loop bounded by the array's own length, so `pairs[q]` is a
// row and not possibly undefined.
function rows() {
    var lines = [];
    var pairs = [];
    pairs.push(['@x', 1.5]);
    pairs.push(['color', 'red']);
    pairs.push(['empty', '']);
    for (var q = 0; q < pairs.length; q++) {
        if (pairs[q][0] !== 'empty') {
            lines.push(pairs[q][0] + '=' + pairs[q][1]);
        }
    }
    return lines.join(' ');
}
function names() {
    var words = ['b', 'a', 'c'];
    var out = '';
    for (var i = 0; i < words.length; i++) {
        out = out + words[i];
    }
    return out;
}
var table = rows();
var width = names().length;
var joined = names();
