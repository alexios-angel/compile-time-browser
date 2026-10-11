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

function events_report() {
    return 'clicks=' + clicks + ' order=' + order + ' seen=' + seen + '| phases=' + phases + '| notes=' + notes;
}
