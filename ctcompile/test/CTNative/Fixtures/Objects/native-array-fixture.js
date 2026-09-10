// THE FIRST ARRAY ON THE STACK - part 24 Phase 57, stage A, through the gate.
// Every array literal here is DENSE: it is built by the literal's own appends
// and then only read, through an index or through `length`, so nothing can put
// a hole in it and nothing can change what `length` means. It never leaves the
// function that made it either - not returned, not stored, not passed - so it
// lives in that function's frame as a `std::vector<double>` by value. The
// interpreter has the last word on every printed global, as always.
//
// NO OUT-OF-BOUNDS READ HERE, deliberately, and the omission is a fact about
// the GATE rather than about the tier: `a[7]` is `undefined`, the tier carries
// undefined as NaN, and a global holding it is not a Number - so the
// reference would skip it, count it under "other globals skipped", and
// check-native-unit.cmake would fail for a reason that has nothing to do with
// arrays. The type inference still starts the element join at `undefined` for
// exactly that read, and ctcompile/test/CTNative/Lowering/native-array.mlir
// pins the type it produces.
function sum_of_three() {
    var a = [1, 2, 3];
    return a[0] + a[1] + a[2];
}
// `length` IS `size()` ONLY BECAUSE OF THE DENSITY PROOF. An array whose
// `length` is assigned, or which is written through an index, or which has an
// element deleted, is refused by name - the lit pins all three.
function counted() {
    var a = [4, 5];
    return a.length;
}
// TWO WIDTHS ARE ONE TYPE, NOT A UNION. Stage 53G's normalisation merges
// `num<i32>` and `num<f64>` into the wider number rather than forming a
// variant of the two, so this array is a `vector<double>` like the others and
// not a vector of a discriminated union.
function widened() {
    var a = [1, 2.5];
    return a[0] + a[1];
}
var sum = sum_of_three();
var count = counted();
var mixed_widths = widened();
