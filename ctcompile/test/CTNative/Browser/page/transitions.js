// Bootstrap's wait for the end of a transition (bootstrap.bundle.js `b`,
// executeAfterTransition): a transitionend listener that removes itself
// through its own binding, and a timer that dispatches transitionend itself
// when no transition ended in time. The test (transitions.cpp) moves the clock
// and asks transitions_report() what ran; transitions.test runs it on the
// interpreter and natively and compares the answers. The helpers live in a
// function that runs once, as the bundle's do; the event type is spelled out
// where the bundle reads it from a binding (`i`).
var log = '';
var ends = 0;

(function () {
    const triggerTransitionEnd = element => {
        element.dispatchEvent(new Event('transitionend'));
    };

    // An end that bubbles up from a child is not the element's own, so the
    // listener stays. The timer dispatches the end only when the listener has
    // not run. Each call makes its own listener, so two waits on one element
    // are two registrations, and each removes only itself.
    function waitForTransition(element, label, duration) {
        let called = false;
        const handler = ({
            target
        }) => {
            if (target !== element) {
                log = log + 'skip:' + label + ';';
                return;
            }
            called = true;
            element.removeEventListener('transitionend', handler);
            log = log + 'end:' + label + ';';
        };
        element.addEventListener('transitionend', handler);
        setTimeout(() => {
            if (!called) {
                triggerTransitionEnd(element);
            }
        }, duration);
    }

    // `box` waits twice; a bubbling end from its child `leaf` reaches both of
    // its listeners at 5 ms and both stay, then box's own end at 10 ms removes
    // both, so its second timer at 20 ms and the end at 40 ms find nothing to
    // run.
    function boxes(box) {
        waitForTransition(box, 'a', 10);
        waitForTransition(box, 'c', 20);
        const leaves = box.children;
        for (let m = 0; m < leaves.length; m++) {
            const leaf = leaves[m];
            setTimeout(() => {
                leaf.dispatchEvent(new Event('transitionend', {
                    bubbles: true
                }));
            }, 5);
        }
        setTimeout(() => {
            triggerTransitionEnd(box);
        }, 40);
    }

    // `other` ends early at 5 ms, so its timer at 30 ms does nothing, and the
    // end at 40 ms reaches only the counting listener: its waiting listener is
    // gone.
    function others(other) {
        waitForTransition(other, 'b', 30);
        other.addEventListener('transitionend', () => {
            ends = ends + 1;
        });
        setTimeout(() => {
            triggerTransitionEnd(other);
        }, 5);
        setTimeout(() => {
            triggerTransitionEnd(other);
        }, 40);
    }

    // Bootstrap's execute (`_`) and the callback executeAfterTransition runs
    // through it: from the listener once the transition ends, or at once when
    // there is none to wait for. Each callback is a closure of the frame, so
    // the listener that runs it later takes what the callback captures.
    const execute = (possibleCallback, args = [], defaultValue = possibleCallback) => {
        return typeof possibleCallback === 'function' ? possibleCallback.call(...args) :
            defaultValue;
    };

    function afterTransition(element, label, duration, waits) {
        const callback = () => {
            log = log + 'done:' + label + ';';
        };
        if (!waits) {
            execute(callback);
            return;
        }
        let called = false;
        const handler = ({
            target
        }) => {
            if (target !== element) {
                return;
            }
            called = true;
            element.removeEventListener('transitionend', handler);
            execute(callback);
        };
        element.addEventListener('transitionend', handler);
        setTimeout(() => {
            if (!called) {
                triggerTransitionEnd(element);
            }
        }, duration);
    }

    // Bootstrap's components hand their callbacks to _queueCallback, which
    // hands them to executeAfterTransition: there the callback arrives as a
    // parameter, and execute runs it at once or from the listener.
    function afterTransitionWith(callback, element, waits) {
        if (!waits) {
            execute(callback);
            return;
        }
        const handler = ({
            target
        }) => {
            if (target !== element) {
                return;
            }
            element.removeEventListener('transitionend', handler);
            execute(callback);
        };
        element.addEventListener('transitionend', handler);
    }

    function queue(callback, element, waits) {
        afterTransitionWith(callback, element, waits);
    }

    // `third` runs one callback at once and one when its timer ends the
    // transition at 15 ms; the end at 40 ms finds the listener gone. It also
    // queues one callback that runs at once and one that the end at 15 ms
    // runs from its listener.
    function thirds(third) {
        afterTransition(third, 'x', 15, true);
        afterTransition(third, 'y', 15, false);
        const label = 'q';
        queue(() => {
            log = log + 'done:' + label + ';';
        }, third, true);
        queue(() => {
            log = log + 'now:' + label + ';';
        }, third, false);
        setTimeout(() => {
            triggerTransitionEnd(third);
        }, 40);
    }

    // The body's children are `box`, `other` and `third`.
    const kids = document.documentElement.children;
    for (let k = 0; k < kids.length; k++) {
        const kid = kids[k];
        if (kid.tagName === 'BODY') {
            const parts = kid.children;
            for (let j = 0; j < parts.length; j++) {
                const part = parts[j];
                if (part.id === 'box') {
                    boxes(part);
                } else if (part.id === 'other') {
                    others(part);
                } else {
                    thirds(part);
                }
            }
        }
    }
})();

function transitions_report() {
    return 'log=' + log + ' ends=' + ends;
}
