// Array callback methods on arrays of Strings and of rows, each callback
// specialised for its one call and passed to the runtime as
// std::bind_front(callback, captures...): filter, map with the index, some,
// every with the array, find, reduce with an initial value, forEach, and
// callbacks that capture a Number, a String and another array. Arrays stay
// inside functions; every global is a scalar compared with the interpreter.
function words() {
    return ['btn', 'card', 'nav', 'dropdown'];
}
function longCount(least) {
    return words().filter(function (s) {
        return s.length > least;
    }).length;
}
function tagged(prefix) {
    return words()
        .map(function (s, i) {
            return prefix + s + i;
        })
        .join(',');
}
function anyNamed(name) {
    return words().some(function (s) {
        return s === name;
    });
}
function allShort() {
    return words().every(function (s, i, all) {
        return s.length < 9 && i < all.length;
    });
}
function firstLong() {
    return words().find(function (s) {
        return s.length > 4;
    });
}
function noneLonger() {
    return words().find(function (s) {
        return s.length > 40;
    });
}
function totalLength() {
    return words().reduce(function (sum, s) {
        return sum + s.length;
    }, 0);
}
function initials() {
    return words().reduce(function (text, s) {
        return text + s;
    }, '');
}
var visited = 0;
function visit(s, i) {
    visited = visited + s.length * i;
}
function visitAll() {
    words().forEach(function (s, i) {
        visit(s, i);
    });
    return visited;
}
function without() {
    var excluded = ['nav', 'card'];
    return words()
        .filter(function (s) {
            return excluded.length > 1 && s.length > 3;
        })
        .join('+');
}
function rowNames() {
    var rows = [];
    rows.push(['a', 1]);
    rows.push(['bb', 2]);
    return rows
        .filter(function (row) {
            return row[1] > 1;
        })
        .length;
}
var longWords = longCount(3);
var taggedWords = tagged('#');
var hasNav = anyNamed('nav') ? 1 : 0;
var short = allShort() ? 1 : 0;
var first = firstLong();
var missing = noneLonger();
var total = totalLength();
var letters = initials();
var weighted = visitAll();
var kept = without();
var rowsKept = rowNames();
