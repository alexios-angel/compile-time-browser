// A KNOWN DEFECT, PINNED AS A FAILURE - ctcompile part 24 Phase 63 Step 5.
//
// THIS PROGRAM IS REGISTERED TO FAIL THE DIFFERENTIAL GATE, and the test
// passes only when it fails with `global 'idx_fractional' differs` in the
// output. Two things fall out of that, and both are the point:
//
//  1. It is the NEGATIVE PROOF for native-divergence-fixture.js beside it.
//     That fixture claims "every guard in the emitted half agrees with the
//     interpreter", and a claim like that is worth nothing unless the gate
//     can be shown to catch a case where a guard does NOT. This is that case,
//     on a module the native pipeline generated - not on the hand-written
//     one, which is the only module the driver's own -DMUTATE proof has ever
//     run against (see the note at the bottom).
//
//  2. It pins the defect itself. The day `ctnative::vec_at` truncates the way
//     the interpreter does, this test goes RED - because the gate will pass -
//     and whoever fixed it is made to come back and update ND-8 and delete
//     this file. A defect recorded only in prose is a defect that gets
//     re-found.
//
// THE DEFECT. `LowerToEmitC.cpp`'s `vec_at` answers `NaN` for any index that
// is not integral:
//
//     if (!(i >= 0.0) || i != std::trunc(i) || i >= (double)v.size()) return NAN;
//
// This interpreter does not. It TRUNCATES TOWARD ZERO and then bounds-checks,
// measured on the box on 2026-09-02 with ctcompile-test-native-reference:
//
//     [10,20,30][0.5]   -> 10      (vec_at: NaN)
//     [10,20,30][1.5]   -> 20      (vec_at: NaN)
//     [10,20,30][2.9]   -> 30      (vec_at: NaN)
//     [10,20,30][-0.5]  -> 10      (vec_at: NaN)  - trunc(-0.5) is -0
//     [10,20,30][3.1]   -> undefined  (agrees)
//     [10,20,30][-1.5]  -> undefined  (agrees)
//
// V8 answers `undefined` for every one of the first four: `a[0.5]` is a read
// of the property named "0.5", which a dense array does not have. So this is
// a divergence the ENGINE has from ECMA-262, and the native tier picked the
// standard's answer over the interpreter's. Part 24 §1.3 and this document's
// own table say which one wins: the ctbrowser VM, always. A difference from
// it is a DEFECT in the native backend, not a declared divergence.
//
// NOT FIXED HERE ON PURPOSE. `LowerToEmitC.cpp` belongs to another agent this
// week; this file is the report, in the form that cannot be ignored.
//
// A SECOND FINDING, RECORDED WHERE IT WAS FOUND. `check-native-unit.cmake`'s
// -DMUTATE anchors the off-by-one on the literal text `std::printf(`. The
// emitter writes `printf(` for a generated module and `std::printf(` only for
// the hand-written `native-fixture.emitc.mlir`, so that negative proof has
// never run against pipeline output - it aborts with "cannot mutate". That is
// why the negative proof for the divergence fixture is this file and not a
// `-DMUTATE=` line.
function element(i) {
    var a = [10, 20, 30];
    return a[i];
}
var idx_fractional = element(0.5) + 0;
var idx_fractional_high = element(2.9) + 0;
var idx_fractional_negative = element(0 - 0.5) + 0;
// AND THE TWO THE TIER ALREADY AGREES ON, so a reader can see that the
// disagreement is the fractional part and not the bounds check.
var idx_past_end = element(3.1) + 0;
var idx_below_start = element(0 - 1.5) + 0;
