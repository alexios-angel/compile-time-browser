// Objects that the native compiler keeps on the stack. Every object literal
// here has a closed shape: it is used only through constant keys, so nothing
// can add or remove a field, and it never leaves the function that made it
// (not returned, stored or passed), so it lowers to a C++ struct held by value
// in that function's frame. The interpreter's output is the reference for
// every printed global.
function area() {
    var p = {
        x: 1,
        y: 2
    };
    p.x = p.x + 3;
    return p.x * p.y;
}

function swap_sum() {
    var a = {
        v: 10
    };
    var b = {
        v: 32
    };
    var t = {
        hold: 0
    };
    t.hold = a.v;
    a.v = b.v;
    b.v = t.hold;
    return a.v * 100 + b.v;
}
// An object updated in a loop must still be one struct slot, however many SSA
// values reach it. The importer threads every register live across a loop
// through the loop's block arguments, which would make `acc` reach the loop op
// itself and its shape read as open; --ctjs-lift-to-scf drops that argument
// (it is fed only by the literal and by itself: a trivial phi) before
// structuring, so every access is on the one literal, updated in place.
// CTNative/Lowering/Objects/struct.mlir checks the lowered shape.
function accumulate(n) {
    var acc = {
        total: 0,
        count: 0
    };
    var i = 0;
    while (i < n) {
        acc.total = acc.total + i * 0.5;
        acc.count = acc.count + 1;
        i = i + 1;
    }
    return acc.total / acc.count;
}

function read_before_write() {
    var o = {
        seen: 1
    };
    var before = o.later; // undefined, which is NaN as a number
    o.later = 5;
    return before + o.later; // NaN + 5 is NaN
}

function boolean_field(x) {
    var f = {
        big: false
    };
    if (x > 100) {
        f.big = true;
    }
    return f.big ? 1 : 0;
}

// One shape is one definition. `{x, y}` is written at three sites in this file
// (once in `area` above and twice here), and all three lower to one class
// named for the shape, not for the site. That is what lets a generated struct
// be passed between functions.
function dot() {
    var a = {
        x: 3,
        y: 4
    };
    var b = {
        x: 5,
        y: 6
    };
    return a.x * b.x + a.y * b.y;
}

// Where the field names match and a type differs, one template. These two
// agree that the fields are `at` and `hit` and disagree about what `hit` is, so
// they are one family at two instantiations, `ctn_at_hit<bool>` and
// `ctn_at_hit<double>`, of a template parametrised on the one position they
// disagree about. Not a variant field: each site is monomorphic, and only the
// union of the two is not, so a variant would put a std::visit in front of
// every read at both sites for a polymorphism neither has.
//
// The disagreement is `bool` against `double` to keep this fixture to scalar
// fields; string fields are tested in
// CTNative/Lowering/Objects/string-fields.mlir.
function hit_flag(x) {
    var h = {
        hit: false,
        at: 0
    };
    h.at = x * 2;
    if (x > 10) {
        h.hit = true;
    }
    return h.hit ? h.at : 0;
}

function hit_count(x) {
    var h = {
        hit: 0,
        at: 0
    };
    h.at = x;
    h.hit = h.hit + 1;
    return h.hit * h.at;
}
var area_answer = area();
var swap_answer = swap_sum();
var nan_from_undefined = read_before_write();
var flags = boolean_field(1000) * 10 + boolean_field(5);
var mean = accumulate(10);
var dotted = dot();
var flagged = hit_flag(20) + hit_flag(3);
var counted = hit_count(7);


// A global written only inside a helper and never mentioned at the top level.
// The generated `main` must still print it, so the set of printed globals is
// collected from every function before any function is lowered. Collected
// lazily instead, `main` (lowered first) would miss `hidden`, and the
// differential test would report a missing line rather than the cause.
function stash(x) {
    hidden = x * 2;
    return x;
}
var stashed = stash(21);
