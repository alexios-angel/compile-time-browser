// The application the launcher test (Application.cpp) compiles and runs.
//
// Every other fixture here is a set of bodies a driver calls one at a time.
// This one is a program: its top level does the work, and the answer is the
// transcript it leaves in `OUT`. That difference is the point of the test -
// nothing installs an entry by hand, nothing calls a body by name, and the only
// thing that decides whether a compiled body runs is whether it was installed
// on the right `function_proto` before the program did.
//
// Every function here must lower, including the top level, because the strict
// build of the test refuses to start when any `function_proto` is left without
// a compiled entry. That makes this file a constraint rather than a sample:
// adding a construct the backend refuses makes the strict build fail, and the
// message names the function.
//
// It is deliberately not a correctness test. Runtime/Differential separates
// the lowerings and this does not try to - what it asks is whether a whole
// program, entered once from C++ and running compiled bodies from there down,
// produces the same bytes as the same program interpreted.
var OUT = "";

function emit(line) {
    OUT = OUT + line + "\n";
}

// A constructor and its instances, so `new` and property writes are on the
// path rather than only calls.
function Ship(x, y) {
    this.x = x;
    this.y = y;
    this.hits = 0;
}

function clampTo(v, lo, hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

// A call chain three deep, which is what makes the AOT -> AOT transition
// counter mean something: a program whose every compiled body is entered from
// the interpreter never crosses it.
function stepShip(s, dx, wide) {
    s.x = clampTo(s.x + dx, 0, wide);
    return s.x;
}

function makeWave(count) {
    var wave = [];
    var i = 0;
    while (i < count) {
        wave[i] = new Ship(i * 3, 1);
        i = i + 1;
    }
    return wave;
}

function advance(wave, dx, wide) {
    var moved = 0;
    var i = 0;
    while (i < wave.length) {
        moved = moved + stepShip(wave[i], dx, wide);
        i = i + 1;
    }
    return moved;
}

// A closure over a captured cell. The score has to survive between frames, so a
// body that copied the binding instead of capturing it answers the same number
// six times and this transcript says so.
function scorer(base) {
    var total = base;
    return function add(points) {
        total = total + points;
        return total;
    };
}

function frame(wave, tick, wide, score) {
    var drift = tick % 3;
    return score(advance(wave, drift - 1, wide));
}

function main() {
    var wave = makeWave(4);
    var score = scorer(100);
    var tick = 0;
    while (tick < 6) {
        emit("tick " + tick + " score " + frame(wave, tick, 20, score));
        tick = tick + 1;
    }
    emit("ships " + wave.length + " first " + wave[0].x + " kind " + typeof wave[0].x);
    return OUT;
}

main();
