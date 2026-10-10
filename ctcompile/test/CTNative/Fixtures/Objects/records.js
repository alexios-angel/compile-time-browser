// Data records: object literals a function builds and returns, whose caller
// only reads and writes their fields through constant keys. The function
// returns its shape's class by value and the caller keeps the result in a
// local of that class. Nothing else holds the object once the callee returns,
// so the copy is unobservable. The interpreter's output is the reference for
// every printed global.
//
//   viewport12 = 12    client20 = 20    mutated25 = 25    twocalls103 = 103
//   optional10 = 10    chained9 = 9

// The rectangle Popper's getViewportRect returns: four Number fields.
function viewport(width, height) {
    return {
        width: width,
        height: height,
        x: 0,
        y: 0
    };
}

function viewport_area() {
    var r = viewport(3, 4);
    return r.width * r.height;
}

// The literal is a local first, written field by field, then returned.
function client_rect(x, y, width, height) {
    var rect = {
        x: 0,
        y: 0
    };
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    return rect;
}

function client_sum() {
    var t = client_rect(1, 2, 7, 10);
    return t.x + t.y + t.width + t.height;
}

// The caller owns its copy and may write it.
function mutated() {
    var r = viewport(2, 5);
    r.width = r.width * 10;
    return r.width + r.height;
}

// Two calls make two objects: writing one leaves the other alone.
function two_calls() {
    var a = viewport(1, 2);
    var b = viewport(3, 4);
    a.width = 100;
    return a.width + b.width;
}

// A field written on one path only is optional: absent, it reads undefined.
function maybe(flag) {
    var o = {
        n: 1
    };
    if (flag) {
        o.m = 5;
    }
    return o;
}

function optional_field() {
    var p = maybe(true);
    return p.m * 2;
}

// A record's field passed on to another record function.
function chained() {
    var a = viewport(4, 5);
    var b = viewport(a.width, a.height);
    return b.width + b.height;
}

var viewport12 = viewport_area();
var client20 = client_sum();
var mutated25 = mutated();
var twocalls103 = two_calls();
var optional10 = optional_field();
var chained9 = chained();
