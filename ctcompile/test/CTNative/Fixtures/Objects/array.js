// Dense numeric array literals, lowered to a `std::vector<double>` in the
// function's own frame. Every array literal here is dense: it is built by the
// literal's own appends and then only read, through an index or through
// `length`, so nothing can put a hole in it and nothing can change what
// `length` means. It never leaves the function that made it (not returned,
// stored or passed), so it lives in that function's frame by value. The
// interpreter's output is the reference for every printed global.
//
// No out-of-bounds read here, on purpose: `a[7]` is `undefined`, which native
// code carries as NaN, and the differential check (Checks/compilation-unit.py)
// prints such a global differently from the interpreter, so the test would
// fail for a reason unrelated to arrays. Type inference still starts the
// element join at `undefined` for that read;
// test/CTNative/Lowering/Objects/array.mlir checks the resulting type.


function sum_of_three() {
    var a = [1, 2, 3];
    return a[0] + a[1] + a[2];
}
// `length` is `size()` only because the array is proved dense. Assigning
// `length`, storing through an index, or deleting an element is admitted only
// with such a proof; CTNative/Lowering/Objects/array.mlir tests the refusals.
function counted() {
    var a = [4, 5];
    return a.length;
}

// Two number widths make one type, not a union: type normalisation merges
// `num<i32>` and `num<f64>` into the wider number rather than forming a variant
// of the two, so this array is a `vector<double>` like the others.
function widened() {
    var a = [1, 2.5];
    return a[0] + a[1];
}
var sum = sum_of_three();
var count = counted();
var mixed_widths = widened();
