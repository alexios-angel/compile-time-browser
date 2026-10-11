// The element calls of Bootstrap's SelectorEngine (bootstrap.bundle.js's `R`)
// and Manipulator (`H`) in an ordinary script, in a table of the same shape
// held by a function that runs once: SelectorEngine's find and findOne
// (Element.prototype.querySelectorAll and querySelector through `call`, the
// document element as the default root, and the `[].concat(...nodes)` copy),
// prev and next (the element-sibling walks with matches, returning `[i]` or
// `[]`), parentElement, closest, querySelector, and
// getAttribute, setAttribute and removeAttribute.
// selectors.test runs selectors_report() on the interpreter and natively over
// selectors.html and compares the answers.
var selectorsAnswer = (function () {
    const R = {
        find: (t, e = document.documentElement) => [].concat(...Element.prototype.querySelectorAll.call(e, t)),
        findOne: (t, e = document.documentElement) => Element.prototype.querySelector.call(e, t),
        prev(t, e) {
            let i = t.previousElementSibling;
            for (; i;) {
                if (i.matches(e)) return [i];
                i = i.previousElementSibling;
            }
            return [];
        },
        next(t, e) {
            let i = t.nextElementSibling;
            for (; i;) {
                if (i.matches(e)) return [i];
                i = i.nextElementSibling;
            }
            return [];
        }
    };

    function ids(elements) {
        let out = '';
        for (let k = 0; k < elements.length; k++) {
            out += elements[k].id + ',';
        }
        return out;
    }

    function run() {
        const root = document.documentElement;
        const kids = root.children;
        let out = '';
        for (let k = 0; k < kids.length; k++) {
            const kid = kids[k];
            out += ids(R.next(kid, 'body')) + ids(R.prev(kid, 'head')) + ids(R.next(kid, 'p')) + '|';
            const up = kid.parentElement;
            out += (up ? up.id : 'none') + '/' + kid.getAttribute('id') + '/' + kid.getAttribute('lang');
            kid.setAttribute('data-bs-seen', 'yes');
            out += '/' + kid.getAttribute('DATA-BS-SEEN') + root.getAttribute('data-bs-seen');
            kid.removeAttribute('data-bs-seen');
            out += '/' + kid.getAttribute('data-bs-seen') + ';';
        }
        const top = root.parentElement;
        const b2 = R.findOne('#b2');
        if (b2) {
            out += ids(R.prev(b2, '.item')) + '/' + ids(R.next(b2, '.item')) + '/' + ids(R.next(b2, 'p')) + '|';
        }
        const found = R.findOne('#list');
        out += '|' + (found ? ids(R.find('.item', found)) : 'none') + '|' + ids(R.find('.item')) + '|' +
            (R.findOne('#missing') ? 'found' : 'none') + '|';
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
