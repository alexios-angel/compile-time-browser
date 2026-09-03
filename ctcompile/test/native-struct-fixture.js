// THE FIRST OBJECT ON THE STACK - part 24 Phase 56, stages A and B, through
// the gate. Every object literal here has a CLOSED shape: it is used only
// through constant keys, so nothing can add or remove a field, and it never
// leaves the function that made it - not returned, not stored, not passed -
// so it lives in that function's frame as a C++ struct by value. The
// interpreter has the last word on every printed global, as always.
function area() {
    var p = { x: 1, y: 2 };
    p.x = p.x + 3;
    return p.x * p.y;
}
function swap_sum() {
    var a = { v: 10 };
    var b = { v: 32 };
    var t = { hold: 0 };
    t.hold = a.v;
    a.v = b.v;
    b.v = t.hold;
    return a.v * 100 + b.v;
}
// AN OBJECT UPDATED IN A LOOP - obligation O-3, one slot however many SSA
// values reach it. The importer threads every register live across a loop
// through the loop's block arguments, so `acc` used to reach the loop op
// itself and its shape read as open; --ctjs-lift-to-scf now drops that
// argument (it is fed by the literal and by itself - a trivial phi) before
// structuring, so every access is on the one literal and it is one struct
// on the stack, updated in place. native-struct.mlir pins the shape of it.
function accumulate(n) {
    var acc = { total: 0, count: 0 };
    var i = 0;
    while (i < n) {
        acc.total = acc.total + i * 0.5;
        acc.count = acc.count + 1;
        i = i + 1;
    }
    return acc.total / acc.count;
}
function read_before_write() {
    var o = { seen: 1 };
    var before = o.later;      // undefined, which is NaN as a number
    o.later = 5;
    return before + o.later;   // NaN + 5 is NaN
}
function boolean_field(x) {
    var f = { big: false };
    if (x > 100) { f.big = true; }
    return f.big ? 1 : 0;
}
// PHASE 56C: ONE SHAPE IS ONE DEFINITION. `{x, y}` is written at THREE sites
// in this file - once in `area` above and twice here - and they are one class,
// named for the shape and not for the site. That is what makes a generated
// struct passable between functions at all; 56B named them ctn_shape_0,
// ctn_shape_1 and ctn_shape_2 and no two of them were the same type.
function dot() {
    var a = { x: 3, y: 4 };
    var b = { x: 5, y: 6 };
    return a.x * b.x + a.y * b.y;
}
// AND WHERE THE NAMES MATCH AND A TYPE DIFFERS, ONE TEMPLATE. These two agree
// that the fields are `at` and `hit` and disagree about what `hit` is, so they
// are one family at two instantiations - `ctn_at_hit<bool>` and
// `ctn_at_hit<double>` - over a template parametrised on the ONE position they
// disagree about. Not a variant field: each site is monomorphic here, and only
// the union of the two is not, so a variant would put a std::visit in front of
// every read at both sites to pay for a polymorphism neither has.
//
// A BOOLEAN, NOT A STRING. Phase 56C's written example is "two numeric and one
// string"; a string has no carrier in this tier yet and the function would be
// refused before a shape was formed, so the disagreement that IS expressible
// today is `double` against `bool`.
function hit_flag(x) {
    var h = { hit: false, at: 0 };
    h.at = x * 2;
    if (x > 10) { h.hit = true; }
    return h.hit ? h.at : 0;
}
function hit_count(x) {
    var h = { hit: 0, at: 0 };
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

// A GLOBAL WRITTEN ONLY INSIDE A HELPER, never mentioned at the top level.
// `main` prints the globals from a set that used to be filled lazily as each
// was first touched, and main is lowered first - so this one was declared and
// never printed, and the differential failed by naming a missing line instead
// of the ordering that caused it. The census now runs over the whole accepted
// set before anything is lowered.
function stash(x) {
    hidden = x * 2;
    return x;
}
var stashed = stash(21);
