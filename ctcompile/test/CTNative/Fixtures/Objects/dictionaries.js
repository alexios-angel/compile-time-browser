// Objects used as string-keyed tables, as ctnative::dictionary<V>: Bootstrap's
// EventHandler keeps its listener registry in `{}` written and read under
// computed keys, deleted from and tested with `in`, and css-dump.js fills one
// from rows of name and value. Keys are Strings and Numbers, which are the
// same key once ToPropertyKey makes text of them. Every computed key a table
// is read under is a Number or has a `::` or `!` in it, so it is never a name
// Object.prototype answers for. Every global is a scalar compared with the
// interpreter.
function present(key) {
    var table = {};
    var id = key + '::' + 1;
    table[id] = 'here';
    var found = table[id];
    var missing = table[id + '!'];
    return (id in table ? 1 : 0) + (id + '!' in table ? 10 : 0) +
        (missing === undefined ? 100 : 0) + (found === 'here' ? 1000 : 0);
}
function numbered(n) {
    var table = {};
    table[n] = 1;
    table['a'] = 2;
    table['' + n] = table[n] + 40;
    table['a'] = 5;
    delete table['a'];
    delete table['never'];
    var a = table['a'];
    return table[n] * 10 + (a === undefined ? 1 : 0) + ('a' in table ? 100 : 0) +
        ('toString' in table ? 1000 : 0);
}
function filled(text) {
    var rows = text.split(',');
    var out = {};
    for (var i = 0; i < rows.length; i++) {
        out[rows[i]] = 'v' + i;
    }
    return out['width'] + '|' + out['color'] + '|' + ('constructor' in out);
}
var presence = present('k');
var numbers = numbered(7);
var style = filled('color,width,color');
