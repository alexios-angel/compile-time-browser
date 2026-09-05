// A LOCAL BINDING NAMED FROM TWO FRAMES IN - part 24 Phase 59, slice 2 step 5,
// through the compilation-unit gate.
//
// Step 4 took `var f = function () { ... };` and made every call through the
// name a `ctjs.call_direct`, in this frame and one frame in. One frame in was
// as far as it went: a closure made INSIDE the function that captured the box
// can fill a slot of its OWN from that slot - the descriptor is not
// from_parent_local and the VM copies `enclosing->upvalues[up.index]`, which is
// `enclosing_indices` in the IR (slice 1b) - and step 4 refused that shape with
// "a function two frames in names the binding through its enclosing closure,
// and this step has no call site out here for it".
//
// BOTH HALVES OF THAT SENTENCE WERE WRONG, AND THE SECOND WAS WRONG BECAUSE OF
// STEP 4 ITSELF. Renumbering was never the obstacle - `removeCaptureSlots` has
// always renumbered a nested closure's `enclosing_indices` beside the target's
// own reads, and what a removal actually cannot do is give the REMOVED index an
// image, which is that function's `move()` fatal. And there IS a call site out
// here: `makeBoundCallDirect` writes a `ctjs.call_direct` in a frame the closure
// VALUE cannot reach, which is exactly what step 4's cross-frame calls are. So
// the binding is followed inward instead, and at every level the same three
// questions are asked that the owning frame already asks - one store, it
// dominates, every read is a call - with the slots removed DEEPEST FIRST so
// that no renumbering meets an index it is about to delete.
//
// THE ANSWERS, worked out by hand and NOT asserted anywhere - the interpreter is
// the reference, by definition. Seven globals, seven distinct values, so a wrong
// one cannot hide behind a right one:
//   both6606 = 6606    chain35 = 35    data25 = 25    pair26 = 26
//   recur21 = 21       three16 = 16    two10 = 10
//
// THE REFUSED SHAPES ARE NOT HERE, and cannot be: native-pipeline.cmake refuses
// to write a module while any function carries `ctnative.not_native`. An inner
// level that ASSIGNS the binding and an inner level that uses the name as a
// VALUE are one program apiece in CTNative/Lowering/closure-refusals.mlir, under
// split-file, and each pins the CHAINED sentence - the outer level refusing with
// the inner level's reason after it.

// TWO FRAMES IN, THE SMALLEST ONE - and the program that was the BOUNDDEEP
// refusal until this step. `base` is a box of this frame because `deep`
// mentions the name; `mid` holds the box at a slot of its own, and `deep` fills
// a slot from `mid`'s. Two slots go, `deep`'s first, and the `ctjs.load_upvalue`
// inside `deep` becomes a `ctjs.call_direct` of `base`'s target - written in a
// frame two removes from the one that holds the closure value.
function two_frames_in(n) {
    var base = function (k) { return k + 1; };
    var mid = function (k) {
        var deep = function (j) { return base(j) * 2; };
        return deep(k);
    };
    return mid(n);
}

// AND THREE, BECAUSE TWO COULD BE A SPECIAL CASE OF ONE. Neither `one` nor `two`
// reads `root` at all - they carry it - so the middle levels are slots with no
// reads to rewrite, which is the case a walk that only looked for reads would
// leave behind. Three slots go, in the order three, two, one.
function three_frames_in(n) {
    var root = function (k) { return k * 3; };
    var one = function (k) {
        var two = function (j) {
            var three = function (m) { return root(m) + 1; };
            return three(j);
        };
        return two(k);
    };
    return one(n);
}

// AN INNER FUNCTION THAT ALSO CAPTURES DATA. `inner` holds two captures: the
// binding, filled from `wrap`'s closure, and `offset` - a number of `wrap`'s own
// frame. Only the first is removed, so `inner` is left as a function of one
// capture and slice 1 prepends `offset` at the call inside `wrap`. A removal
// that took the whole capture list, or that renumbered `offset` wrongly, gives
// 12 or a verifier error rather than 25.
function deep_with_data(n) {
    var scale = function (k) { return k * 4; };
    var wrap = function (k) {
        var offset = k + 10;
        var inner = function (j) { return scale(j) + offset; };
        return inner(k);
    };
    return wrap(n);
}

// CALLED AT EVERY DEPTH IT REACHES, WHICH IS THREE DIFFERENT REWRITES OF ONE
// NAME. `step` is read here through a `ctjs.cell_get` - replaced by the closure
// VALUE, so slice 1's own rule takes that call - and through a
// `ctjs.load_upvalue` in `mid` and another in `deep`, where there is no value to
// name and this step writes the `ctjs.call_direct` itself. A rewrite that
// stopped at the first depth prints 606.
function both_depths(n) {
    var step = function (k) { return k - 1; };
    var mid = function (k) {
        var deep = function (j) { return step(j) * 10; };
        return step(k) + deep(k);
    };
    return step(n) + mid(n) * 100;
}

// RECURSION THROUGH TWO FRAMES. `down` names itself, so its closure captures the
// box it is written into - and the name is then carried two frames further in,
// through `hop` and into `again`, where the call of it is a `ctjs.call_direct`
// of the function three frames out. Condition 3 is not asked again inward and
// does not need to be: `hop` cannot run before `down` does, and `again` cannot
// run before `hop` does.
function deep_recursion(n) {
    var down = function (k) {
        if (k <= 0) { return 0; }
        var hop = function (j) {
            var again = function (m) { return down(m); };
            return again(j - 1) + j;
        };
        return hop(k);
    };
    return down(n);
}

// TWO NAMES TRAVELLING THROUGH THE SAME TWO CLOSURES, which is the case that
// renumbers at BOTH levels. `mid` holds both boxes, at slots 0 and 1, and `deep`
// fills both of its own slots from them; all four go. Removing `mid`'s slot 0
// before `deep`'s entry for it is the fatal this step's ordering exists to
// avoid, and removing `deep`'s slot 0 alone would leave `times` being read at an
// index that no longer means what it did.
function two_names_deep(n) {
    var plus = function (k) { return k + 2; };
    var times = function (k) { return k * 5; };
    var mid = function (k) {
        var deep = function (j) { return plus(j) + times(j); };
        return deep(k);
    };
    return mid(n);
}

// A DEEP NAME WHOSE FUNCTION CLOSES OVER ANOTHER BINDING - condition 5 and this
// step in one program. `outer_fn`'s closure holds `inner_fn`'s box, and
// `outer_fn` is the name `deep` reaches two frames in; condition 5 admits the
// capture because it is itself a binding this step erases, and both boxes are
// gone in one fixpoint. `inner_fn` never travels past one frame, so the two
// names are erased by different depths of the same rule.
function chained_deep(n) {
    var inner_fn = function (k) { return k + 3; };
    var outer_fn = function (k) { return inner_fn(k) * 7; };
    var mid = function (k) {
        var deep = function (j) { return outer_fn(j); };
        return deep(k);
    };
    return mid(n);
}

var both6606 = both_depths(7);
var chain35 = chained_deep(2);
var data25 = deep_with_data(3);
var pair26 = two_names_deep(4);
var recur21 = deep_recursion(6);
var three16 = three_frames_in(5);
var two10 = two_frames_in(4);
