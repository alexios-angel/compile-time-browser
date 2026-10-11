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

function events_report() {
    return 'clicks=' + clicks + ' order=' + order;
}
