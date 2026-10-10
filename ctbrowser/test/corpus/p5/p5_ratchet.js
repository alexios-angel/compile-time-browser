// The questions test/corpus/p5/p5_ratchet.cpp asks inside the page, one
// function each. p5_ratchet-min.html and p5_ratchet-full.html load this, and
// the test calls the functions through test/support/engine.hpp.

// --- and is there a p5? -----------------------------------------------------
//
// Asked from inside the page, because that is the only place its globals live.
function p5_ratchet_typeof_p5() {
    return typeof p5;
}

// --- and does a SKETCH run? -------------------------------------------------
//
// Instance mode rather than global: a sketch function gets its own p5, so
// the test says exactly what it is asking for instead of relying on p5
// having installed 200 names as globals.
//
// Progress is recorded in a global string rather than returned, because setup
// and draw are called BY p5, from inside its own machinery - there is
// nothing for the test to take a return value from.
var __ran = '';
var __err = '';

function p5_ratchet_construct() {
    try {
        new p5(function (s) {
            s.setup = function () {
                s.createCanvas(100, 100);
                __ran += 'setup;';
            };
            s.draw = function () {
                // Two colours nothing else in the pipeline produces, so finding
                // them in the buffer cannot be a coincidence.
                s.background(10, 20, 30);
                s.noStroke();
                s.fill(200, 100, 50);
                s.rect(20, 20, 40, 40);
                __ran += 'draw;';
            };
        });
    } catch (e) {
        __err = '' + (e && e.message ? e.message : e);
    }
}

// 'ok', or what `new p5(sketch)` threw.
function p5_ratchet_construct_error() {
    return __err || 'ok';
}

// What setup and draw have recorded so far: 'setup;draw;draw;...'.
function p5_ratchet_progress() {
    return __ran;
}
