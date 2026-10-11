// Listeners an ordinary native script registers with addEventListener, called
// back when the host dispatches an event. The top level registers them; the
// test (events.cpp) clicks elements of events.html and asks events_report()
// what ran. events.test runs it on the interpreter and natively and compares
// the answers.
var clicks = 0;
var order = '';

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

function events_report() {
    return 'clicks=' + clicks + ' order=' + order;
}
