// Object spread whose copied keys are known at compile time. `{...a}` copies
// whatever own keys `a` has when it runs; when `a` is a literal whose keys are
// all written in plain sight before the spread, --ctjs-lift-to-scf writes the
// copy out key by key, and the result is an ordinary closed-shape literal that
// lowers to a C++ struct. The interpreter's output is the reference for every
// printed global.

// Later sources win, key by key: the merge a component's configuration does
// with its defaults and the caller's options.
function merged() {
    var defaults = {
        interval: 5000,
        wrap: true
    };
    var options = {
        interval: 200
    };
    var config = {
        ...defaults,
        ...options
    };
    return config.interval + (config.wrap ? 1 : 0);
}

// A field written before a spread is overwritten by it, and one written after
// it wins.
function ordered() {
    var base = {
        a: 1,
        b: 2
    };
    var o = {
        b: 9,
        ...base,
        a: 30
    };
    return o.a * 100 + o.b;
}

// The spread copies the value the source holds when the spread runs: a write
// to the source before it is seen, a write after it is not.
function snapshot() {
    var source = {
        n: 1
    };
    source.n = 4;
    var copy = {
        ...source
    };
    source.n = 100;
    return copy.n * 1000 + source.n;
}

// Null, undefined and an empty literal copy nothing.
function nothing() {
    var o = {
        x: 3,
        ...null,
        ...undefined,
        ...{}
    };
    return o.x;
}

// The copy is independent of its source.
function independent() {
    var a = {
        v: 1
    };
    var b = {
        ...a
    };
    b.v = b.v + 10;
    return a.v * 100 + b.v;
}

var merged_answer = merged();
var ordered_answer = ordered();
var snapshot_answer = snapshot();
var nothing_answer = nothing();
var independent_answer = independent();
