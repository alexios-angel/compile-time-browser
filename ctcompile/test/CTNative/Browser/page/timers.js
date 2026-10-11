// Handlers an ordinary native script schedules with setTimeout, called back
// when the host's clock reaches them. The top level schedules them; the test
// (timers.cpp) moves the clock and asks timers_report() what ran. timers.test
// runs it on the interpreter and natively and compares the answers.
var log = '';

// Handlers with no captures: one due later, one at once, and one whose
// negative timeout counts as 0.
setTimeout(function () {
    log = log + 'a';
}, 10);
setTimeout(function () {
    log = log + 'z';
}, 0);
setTimeout(function () {
    log = log + 'n';
}, -1);

// A handler that copies its captured parameter and schedules another with
// it, which waits for the next turn of the clock.
function later(label, ms) {
    setTimeout(function () {
        log = log + label;
        setTimeout(function () {
            log = log + label + '2';
        }, 0);
    }, ms);
}
later('m', 5);

// Two handlers that write a binding of the frame that scheduled them: the
// frame and both handlers share `ticks` through a cell, so each handler sees
// the other's write and the frame's write after scheduling.
function counter() {
    var ticks = 0;
    setTimeout(function () {
        ticks = ticks + 1;
        log = log + 't' + ticks;
    }, 0);
    setTimeout(function () {
        ticks = ticks + 1;
        log = log + 't' + ticks;
    }, 5);
    ticks = 10;
}
counter();

function timers_report() {
    return 'log=' + log;
}
