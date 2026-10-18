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

    // The body's children are `box` and `other`.
    const kids = document.documentElement.children;
    for (let k = 0; k < kids.length; k++) {
        const kid = kids[k];
        if (kid.tagName === 'BODY') {
            const parts = kid.children;
            for (let j = 0; j < parts.length; j++) {
                const part = parts[j];
                if (part.id === 'box') {
                    boxes(part);
                } else {
                    others(part);
                }
            }
        }
    }
})();

function transitions_report() {
    return 'log=' + log + ' ends=' + ends;
}
