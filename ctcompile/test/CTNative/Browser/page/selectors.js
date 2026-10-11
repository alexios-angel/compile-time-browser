// The element calls of Bootstrap's SelectorEngine (bootstrap.bundle.js's `R`)
// and Manipulator (`H`) in an ordinary script, in a table of the same shape
// held by a function that runs once: the element-sibling and parent links,
// matches, closest, querySelector, and getAttribute, setAttribute and
// removeAttribute.
// selectors.test runs selectors_report() on the interpreter and natively over
// selectors.html and compares the answers.
var selectorsAnswer = (function () {
    const R = {
        count(t, e) {
            let n = 0;
            let i = t.nextElementSibling;
            for (; i;) {
                if (i.matches(e)) n++;
                i = i.nextElementSibling;
            }
            return n;
        },
        before(t, e) {
            const i = t.previousElementSibling;
            return i ? i.id + ':' + i.matches(e) : 'none';
        }
    };

    function run() {
        const root = document.documentElement;
        const kids = root.children;
        let out = '';
        for (let k = 0; k < kids.length; k++) {
            const kid = kids[k];
            out += R.count(kid, 'body') + R.before(kid, 'head') + '|';
            const up = kid.parentElement;
            out += (up ? up.id : 'none') + '/' + kid.getAttribute('id') + '/' + kid.getAttribute('lang');
            kid.setAttribute('data-bs-seen', 'yes');
            out += '/' + kid.getAttribute('DATA-BS-SEEN') + root.getAttribute('data-bs-seen');
            kid.removeAttribute('data-bs-seen');
            out += '/' + kid.getAttribute('data-bs-seen') + ';';
        }
        const top = root.parentElement;
        const list = root.querySelector('#list');
        const inside = list ? list.closest('body') : null;
        return out + (top ? 'parent' : 'no parent') + '|' + (list ? list.id : 'none') + '|' +
            (inside ? inside.id : 'none') + '|' + (root.querySelector('#missing') ? 'found' : 'none');
    }

    return run();
})();

function selectors_report() {
    return selectorsAnswer + '';
}
