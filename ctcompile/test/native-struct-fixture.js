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
// AN OBJECT UPDATED IN A LOOP IS NOT HERE YET - see native-struct.mlir. The
// lift threads every register live across a loop through the loop's block
// arguments, so an object used inside a `while` reaches the loop op itself
// and its shape reads as open. Obligation O-3 (one slot, however many SSA
// values reach it) is what admits it, and that is Phase 56's next step.
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
var area_answer = area();
var swap_answer = swap_sum();
var nan_from_undefined = read_before_write();
var flags = boolean_field(1000) * 10 + boolean_field(5);
