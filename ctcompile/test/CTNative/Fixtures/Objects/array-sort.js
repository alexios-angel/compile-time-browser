// Array.prototype.sort with a comparator, specialised for its one call and
// passed to the runtime as std::bind_front(comparator, captures...): Strings
// by length, where equal lengths keep their order, the sorted array used as
// the call's answer, Numbers with a Boolean comparator, Numbers by distance
// to a captured Number, rows by their Number, and an array a callee sorts in
// place, which it borrows by address. Every global is a scalar compared with
// the interpreter.
function words() {
    return ['card', 'btn', 'dropdown', 'nav', 'modal', 'tab'];
}
function lengths() {
    return words().map(function (s) {
        return s.length;
    });
}
function listed(numbers) {
    return numbers.reduce(function (text, n) {
        return text + n + ',';
    }, '');
}
function shortestFirst() {
    return words()
        .sort(function (a, b) {
            return a.length - b.length;
        })
        .join(',');
}
function descending() {
    var n = lengths();
    n.sort(function (a, b) {
        return a < b;
    });
    return listed(n);
}
function nearest(target) {
    var n = lengths();
    n.sort(function (a, b) {
        return (a - target) * (a - target) - (b - target) * (b - target);
    });
    return listed(n);
}
function ranked() {
    var rows = [];
    rows.push(['c', 3]);
    rows.push(['a', 1]);
    rows.push(['b', 2]);
    rows.sort(function (x, y) {
        return x[1] - y[1];
    });
    return rows.reduce(function (text, row) {
        return text + row[0];
    }, '');
}
function longestFirstIn(list) {
    list.sort(function (a, b) {
        return b.length - a.length;
    });
}
function longestFirst() {
    var list = words();
    longestFirstIn(list);
    return list.join(' ');
}
var ascending = shortestFirst();
var down = descending();
var near = nearest(4);
var order = ranked();
var longest = longestFirst();
