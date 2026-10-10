function shrink() {
    var values = [4, 9, 16];
    var before = values.length;
    values.length = 1;
    return before * 10 + values.length + values[0];
}

function clear() {
    var values = [7, 8];
    var saved = values[1];
    values.length = -0;
    return saved + values.length;
}

function unchanged() {
    var values = [-0, 2];
    values.length = 2;
    return 1 / values[0];
}

var shortened = shrink();
var cleared = clear();
var preserved = unchanged();
