// The GC-roots fixture, driven by GC/Roots.cpp.
//
// Roots.cpp embeds this file and the build compiles the same file, so the
// compiler and the interpreter run the same bytes. That matters: a compiled
// body bakes in the function index of every closure it builds (`held` builds
// one), and an index means nothing outside the program it was compiled in.
// Two diverging copies would make the compiled body close over a different
// function while every check still passed.
//
// The build compiles `f`, `held` and `built`; Roots.cpp installs each one in
// turn and runs its driver (`run`, `runHeld`, `runBuilt`) after `setup` has
// built fresh 32-character strings in the globals `A` and `B`. Each driver
// stores its answer in the global `R`, and the interpreter's answer for the
// same driver is the expected value. A correct answer is 65 characters:
// thirty-two A's, thirty-two B's and the "Z" that `c.valueOf` returns; a
// collected intermediate shows up as a short or garbled string.
//
// Two safepoints with a live value between them, which is the shape that
// matters. `a + b` allocates a string; `k(...)` is a call that runs user
// JavaScript and can collect. The intermediate is live across the call, and it
// also has to be in the call's contiguous argument window - so this exercises
// both kinds of rooting the backend does: one slot per produced value, and a
// reserved run per call site.
function f(a, b, c, k) {
    return k(a + b, c);
}

// A closure built in compiled code, held across a collection.
//
// ct_aot_make_closure allocates and is a safepoint, and so is the call below
// it. Three things have to survive: the string `a + b` builds, the cell holding
// it, and the closure_object itself - none of which is reachable from anything
// but this frame's slots while `k` runs user JavaScript.
function held(a, b, c, k) {
    var s = a + b;
    var keep = function () {
        return s;
    };
    return k(keep(), c);
}

function built(a, b, c, k) {
    var o = new Box(a + b);
    return k(o.s, c);
}

function cat(x, y) {
    return x + y;
}

// A constructor, so ct_aot_construct's own argument window is covered.
//
// It is a separate window from ct_aot_call's - a second park loop and a second
// window_pointer in the backend - and nothing above reaches it. The string
// `a + b` builds is handed to `new Box(...)` and then lives only in that
// window while make_instance allocates and the body runs.
function Box(s) {
    this.s = s;
}

function repeat(ch) {
    var s = '';
    for (var i = 0; i < 32; i++) {
        s = s + ch;
    }
    return s;
}

var A = '',
    B = '',
    R = '';

// Its valueOf allocates before it answers, so the collection happens while the
// caller is holding a value it has nowhere rooted.
var c = {
    valueOf: function () {
        var j = '';
        for (var i = 0; i < 8; i++) {
            j = j + 'q';
        }
        return 'Z';
    }
};

function setup() {
    A = repeat('A');
    B = repeat('B');
}

function run() {
    R = f(A, B, c, cat);
}

function runHeld() {
    R = held(A, B, c, cat);
}

function runBuilt() {
    R = built(A, B, c, cat);
}
