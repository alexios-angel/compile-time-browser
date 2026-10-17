// Objects used as string-keyed tables, as ctnative::dictionary<V>: Bootstrap's
// EventHandler keeps its listener registry in tables of tables written and
// read under computed keys, deleted from and tested with `in`, and css-dump.js fills one
// from rows of name and value and stringifies it. Keys are Strings and Numbers, which are the
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
// css-dump.js's non-compact line: rows of name and value pushed into an
// array, a table filled from them, and JSON.stringify of the table, whose
// keys come out indices first, then in the order they were made.
function dumped(text) {
    var names = text.split(',');
    var pairs = [];
    for (var i = 0; i < names.length; i++) {
        pairs.push([names[i], 'v' + i]);
    }
    var out = {};
    for (var j = 0; j < pairs.length; j++) {
        out[pairs[j][0]] = pairs[j][1];
    }
    return JSON.stringify(out);
}
// EventHandler's registry shape: tables of tables of tables, made on first
// use with `E[uid] = E[uid] || {}` and `l[type] || (l[type] = {})`, written
// under `type::n` keys, deleted from and read back through each level. Its
// keys are uids (Numbers) and namespaced types (`click.bs`).
function registry(uid, name) {
    var E = {};
    var type = name + '.bs';
    var count = 0;
    for (var round = 0; round < 3; round++) {
        E[uid] = E[uid] || {};
        const l = E[uid];
        const c = l[type] || (l[type] = {});
        c[type + '::' + round] = round + 10;
        count = count + 1;
    }
    delete E[uid][type][type + '::1'];
    var left = E[uid][type];
    var missing = E[uid + 1];
    return count * 1000 + left[type + '::2'] * 10 + (type + '::1' in left ? 1 : 0) +
        (missing === undefined ? 100 : 0);
}
var presence = present('k');
var numbers = numbered(7);
var style = filled('color,width,color');
var json = dumped('color,width,10,color,a"b');
var nested = registry(1, 'click');
