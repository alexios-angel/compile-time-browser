// Local function bindings called from closures two or more frames inside the
// function that owns them, compiled natively and checked end to end by the
// compilation-unit test.
//
// `var f = function () { ... };` whose box has one store that dominates every
// read, and whose every read is a call, has its box erased: each call through
// the name becomes a `ctjs.call_direct` of the target. When the name is used in
// a closure nested further in, that closure fills an upvalue slot of its own
// from its enclosing closure's slot (the descriptor is not from_parent_local;
// the VM copies `enclosing->upvalues[up.index]`, which is `enclosing_indices`
// in the IR).
//
// The pass follows the binding inward and asks the same three questions at
// every level that the owning frame asks: one store, the store dominates, and
// every read is a call. Two pieces of existing machinery make that work.
// `removeCaptureSlots` renumbers a nested closure's `enclosing_indices` along
// with the target's own reads; what it cannot do is give the removed index a
// new position (that is its `move()` fatal error), so slots are removed
// deepest first and no renumbering meets an index about to be deleted. And
// `makeBoundCallDirect` writes a `ctjs.call_direct` in a frame the closure
// value cannot reach. Both live in lib/CTNative/Lowering/ClosureLifting/
// Bindings.cpp.
//
// Expected values, worked out by hand and not asserted here: the interpreter
// is the reference. Every global has a distinct value, so a wrong one cannot
// hide behind a right one:
//   both6606 = 6606    chain35 = 35    data25 = 25    pair26 = 26
//   recur21 = 21       three16 = 16    two10 = 10     trap15 = 15
//   maxt17 = 17
//
// Refused shapes cannot live here: test/CTNative/Checks/pipeline.cmake fails
// if any function carries `ctnative.not_native`. An inner level that assigns
// the binding, and one that uses the name as a value, each have their own
// program in test/CTNative/Lowering/Closures/refusals.mlir (under
// split-file), which checks that the outer level's refusal message ends with
// the inner level's reason.

// Two frames in, the smallest case. `base` is boxed in this frame because
// `deep` names it; `mid` holds the box in an upvalue slot of its own, and
// `deep` fills a slot from `mid`'s. Both slots are removed, `deep`'s first,
// and the `ctjs.load_upvalue` inside `deep` becomes a `ctjs.call_direct` of
// `base`'s target, written two frames away from the frame that holds the
// closure value.
function two_frames_in(n) {
    var base = function (k) {
        return k + 1;
    };
    var mid = function (k) {
        var deep = function (j) {
            return base(j) * 2;
        };
        return deep(k);
    };
    return mid(n);
}

// Three frames in, because two could be a special case of one. Neither `one`
// nor `two` reads `root`; they only carry it, so the middle levels are slots
// with no reads to rewrite, which a walk that only looked for reads would
// miss. Three slots are removed, in the order three, two, one.
function three_frames_in(n) {
    var root = function (k) {
        return k * 3;
    };
    var one = function (k) {
        var two = function (j) {
            var three = function (m) {
                return root(m) + 1;
            };
            return three(j);
        };
        return two(k);
    };
    return one(n);
}

// An inner function that also captures data. `inner` holds two captures: the
// binding, filled from `wrap`'s closure, and `offset`, a number in `wrap`'s own
// frame. Only the binding's slot is removed, so `inner` keeps one capture, and
// closure lifting passes `offset` as an extra leading argument at the call
// inside `wrap`. Removing the whole capture list, or renumbering `offset`
// wrongly, gives 12 or a verifier error instead of 25.
function deep_with_data(n) {
    var scale = function (k) {
        return k * 4;
    };
    var wrap = function (k) {
        var offset = k + 10;
        var inner = function (j) {
            return scale(j) + offset;
        };
        return inner(k);
    };
    return wrap(n);
}

// The same name called at three depths, needing different rewrites. In this
// frame `step` is read through `ctjs.cell_get`, which is replaced by the
// closure value, so the ordinary same-frame rewrite turns the call into a
// direct call. In `mid` and `deep` it is read through `ctjs.load_upvalue`;
// there is no closure value to substitute, so the pass writes the
// `ctjs.call_direct` itself. A rewrite that stopped at the first depth gives 606.
function both_depths(n) {
    var step = function (k) {
        return k - 1;
    };
    var mid = function (k) {
        var deep = function (j) {
            return step(j) * 10;
        };
        return step(k) + deep(k);
    };
    return step(n) + mid(n) * 100;
}

// Recursion through two frames. `down` names itself, so its closure captures
// the box it is stored into, and the name is carried two frames further in,
// through `hop` into `again`, where the call becomes a `ctjs.call_direct` of
// the function three frames out. The "store dominates every read" check is not
// repeated for the inner levels and does not need to be: `hop` cannot run
// before `down` does, and `again` cannot run before `hop` does.
function deep_recursion(n) {
    var down = function (k) {
        if (k <= 0) {
            return 0;
        }
        var hop = function (j) {
            var again = function (m) {
                return down(m);
            };
            return again(j - 1) + j;
        };
        return hop(k);
    };
    return down(n);
}

// Two names travelling through the same two closures, which renumbers at both
// levels. `mid` holds both boxes, at slots 0 and 1, and `deep` fills both of
// its own slots from them; all four slots are removed. Removing `mid`'s slot 0
// before `deep`'s entry for it is the fatal error the deepest-first ordering
// avoids, and removing `deep`'s slot 0 alone would leave `times` read at an
// index that no longer means what it did.
function two_names_deep(n) {
    var plus = function (k) {
        return k + 2;
    };
    var times = function (k) {
        return k * 5;
    };
    var mid = function (k) {
        var deep = function (j) {
            return plus(j) + times(j);
        };
        return deep(k);
    };
    return mid(n);
}

// A deeply used name whose function closes over another binding. `outer_fn`'s
// closure holds `inner_fn`'s box, and `deep` reaches `outer_fn` two frames in.
// That capture is allowed because `inner_fn` is itself a binding being erased
// (the fixpoint in bindLocalFunctions), so both boxes go in one fixpoint.
// `inner_fn` is only used one frame in, so the two names are erased at
// different depths by the same rule.
function chained_deep(n) {
    var inner_fn = function (k) {
        return k + 3;
    };
    var outer_fn = function (k) {
        return inner_fn(k) * 7;
    };
    var mid = function (k) {
        var deep = function (j) {
            return outer_fn(j);
        };
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

// Ordering trap: why slot removal is ordered by each closure's nesting depth.
//
// `mid` holds a binding that stops at depth 0 (`w`) and one that travels on
// (`q`); `inner` holds the travelled `q` at depth 1 and `p`, owned by `mid`'s
// frame, at depth 0. Ordered by the minimum slot depth, both closures key on
// 0, stable_sort keeps `mid` first, and removeCaptureSlots' `move()` finds no
// new index for the slot `inner` still names, so the compiler aborts on seven
// lines of ordinary JavaScript.
function order_trap(n) {
    var w = function (k) {
        return k + 3;
    };
    var q = function (k) {
        return k + 4;
    };
    var mid = function (k) {
        var p = function (j) {
            return j + 5;
        };
        var inner = function (m) {
            return q(m) + p(m);
        };
        return w(k) + inner(k);
    };
    return mid(n);
}

// The maximum slot depth does not work either, and is the obvious next try.
// `a` holds `near` at depth 0 and `far` at depth 1; `b`, made inside `a`'s
// target, holds only `near`, at depth 1. Ordered by the maximum, both key on 1
// and the tie aborts the other way round, so no function of the slot depths
// orders both programs. A slot's depth measures how far a binding travelled;
// the rewrite needs how deep the closure itself sits.
function maxtrap(n) {
    var far = function (k) {
        return k + 7;
    };
    var outer = function (k) {
        var near = function (j) {
            return j + 8;
        };
        var a = function (j) {
            var b = function (m) {
                return near(m);
            };
            return b(j) + far(j);
        };
        return a(k);
    };
    return outer(n);
}

var trap15 = order_trap(1);
var maxt17 = maxtrap(1);
