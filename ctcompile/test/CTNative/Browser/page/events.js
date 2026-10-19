// Listeners an ordinary native script registers with addEventListener, called
// back when the host dispatches an event. The top level registers them; the
// test (events.cpp) clicks elements of events.html and asks events_report()
// what ran. events.test runs it on the interpreter and natively and compares
// the answers.
var clicks = 0;
var order = '';
var seen = '';
var phases = '';
var notes = '';
var counted = '';
var named = '';

// Named top-level functions as listeners: onNamed is added twice to the root
// (the second ignored), and onLeaf, on <body>, removes it when a click on the
// leaf passes, so the root's step of that click no longer runs it.
function onNamed(e) {
    named = named + (e.target.id === 'top' ? 'n' : e.target.id);
}
function onLeaf(e) {
    if (e.target.id === 'leaf') {
        document.documentElement.removeEventListener('click', onNamed);
    }
}
document.documentElement.addEventListener('click', onNamed);
document.documentElement.addEventListener('click', onNamed);

// A listener with no captures, on the root element.
document.documentElement.addEventListener('click', function () {
    clicks = clicks + 1;
    order = order + 'r';
});

// A listener that copies its captured parameter: `name` is never written
// after the listener captures it. It goes on <body>, found among the root's
// children.
function listen(name) {
    var kids = document.documentElement.children;
    for (var i = 0; i < kids.length; i++) {
        var kid = kids[i];
        if (kid.tagName === 'BODY') {
            kid.addEventListener('click', function () {
                order = order + name;
            });
            kid.addEventListener('click', onLeaf);
            // A listener that takes the event: it reads the event's type,
            // target and current target, cancels it, and stops a click on the
            // leaf from reaching the root.
            kid.addEventListener('click', function (e) {
                e.preventDefault();
                seen = seen + e.type + ':' + e.target.id + '@' + e.currentTarget.id +
                    (e.defaultPrevented ? '!' : '') + ' ';
                if (e.target.id === 'leaf') {
                    e.stopPropagation();
                }
            });
        }
    }
}
listen('b');

// A listener that writes a binding of the frame that registered it: the frame
// and the listener share `on` through a cell, so each click flips the one
// binding and the frame's own write after registering is what the first
// click sees.
function toggler() {
    var on = true;
    document.documentElement.addEventListener('click', function () {
        on = !on;
        order = order + (on ? '+' : '-');
    });
    on = false;
}
toggler();

// The capture phase, from the window down: a capturing listener on the
// document and one on the root, and a bubbling one on the window, which a
// click stopped at <body> never reaches.
document.addEventListener('click', function (e) {
    phases = phases + 'D' + e.target.id + ' ';
}, true);
document.documentElement.addEventListener('click', function () {
    phases = phases + 'c ';
}, true);
window.addEventListener('click', function () {
    phases = phases + 'W ';
});

// Events the script makes and dispatches itself: the root's listener cancels
// each one, which only a cancelable event takes, and the script reads the
// answer and defaultPrevented back afterwards.
document.documentElement.addEventListener('note', function (e) {
    e.preventDefault();
    notes = notes + e.type + ':';
});
function announce() {
    var root = document.documentElement;
    var plain = new Event('note');
    var heard = root.dispatchEvent(plain);
    notes = notes + (heard ? 'heard' : 'canceled') + (plain.defaultPrevented ? '!' : '') + ' ';
    var loud = new Event('note', {
        bubbles: true,
        cancelable: true
    });
    heard = root.dispatchEvent(loud);
    notes = notes + (heard ? 'heard' : 'canceled') + (loud.defaultPrevented ? '!' : '') + ' ';
}
announce();

// Duplicate EventInit keys keep the last value. Cancellation and dispatch to
// an ancestor expose the flags without reading unsupported Event members.
var init = '';
var bubbled = false;
document.documentElement.addEventListener('duplicate', function () {
    bubbled = true;
});

function duplicateInit() {
    var cancelableTrue = new Event('duplicate', {
        cancelable: false,
        cancelable: true
    });
    cancelableTrue.preventDefault();
    var cancelableFalse = new Event('duplicate', {
        cancelable: true,
        cancelable: false
    });
    cancelableFalse.preventDefault();
    init = 'cancelable:' + cancelableTrue.defaultPrevented + ',' + cancelableFalse.defaultPrevented;

    var kids = document.documentElement.children;
    for (var i = 0; i < kids.length; i++) {
        var kid = kids[i];
        if (kid.tagName === 'BODY') {
            var bubblesTrue = new Event('duplicate', {
                bubbles: false,
                bubbles: true
            });
            kid.dispatchEvent(bubblesTrue);
            init = init + ' bubbles:' + bubbled;
            bubbled = false;
            var bubblesFalse = new Event('duplicate', {
                bubbles: true,
                bubbles: false
            });
            kid.dispatchEvent(bubblesFalse);
            init = init + ',' + bubbled;
        }
    }
}
duplicateInit();

// Listeners named by a variable, so the same function reaches addEventListener
// twice (the second is ignored) and removeEventListener once: removal matches
// the capture flag, so the capturing registration of `g` stays.
function wire(mark) {
    var root = document.documentElement;
    var h = function () {
        counted = counted + mark;
    };
    var g = function () {
        counted = counted + 'g';
    };
    root.addEventListener('click', h);
    root.addEventListener('click', h);
    root.addEventListener('click', g);
    root.addEventListener('click', g, true);
    root.removeEventListener('click', g);
}
wire('h');

// Promise jobs a listener queues run at the next microtask checkpoint: when a
// host click returns, but only after the rest of the script when the script
// dispatches the event itself.
var jobs = '';
document.documentElement.addEventListener('ping', function () {
    Promise.resolve(0).then(function () {
        jobs = jobs + 'j';
    });
});
function ping() {
    document.documentElement.dispatchEvent(new Event('ping'));
    jobs = jobs + 's';
}
ping();
document.documentElement.addEventListener('click', function () {
    Promise.resolve(0).then(function () {
        jobs = jobs + 'c';
    });
});

function events_report() {
    return 'clicks=' + clicks + ' order=' + order + ' seen=' + seen + '| phases=' + phases + '| notes=' + notes + '| counted=' + counted + '| named=' + named + '| jobs=' + jobs + '| init=' + init;
}
