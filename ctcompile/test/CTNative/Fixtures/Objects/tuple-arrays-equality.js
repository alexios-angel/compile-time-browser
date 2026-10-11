// css-dump.js's filter on a row's Number|String value:
// `v !== '' && v !== null && v !== undefined`. A Number|String is never null
// or undefined, and the global `undefined` is the undefined value (it is a
// non-writable property of the global object).
function kept() {
    var pairs = [];
    pairs.push(['a', 1]);
    pairs.push(['b', '']);
    pairs.push(['c', 'x']);
    pairs.push(['d', 0]);
    var parts = [];
    for (var q = 0; q < pairs.length; q++) {
        var v = pairs[q][1];
        if (v !== '' && v !== null && v !== undefined) {
            parts.push(pairs[q][0] + '=' + v);
        }
    }
    return parts.join(' ');
}
var line = kept();
var width = kept().length;
