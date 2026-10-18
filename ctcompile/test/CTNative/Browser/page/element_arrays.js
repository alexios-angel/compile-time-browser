// for-of and the Array callbacks over an array of elements, as Bootstrap
// walks SelectorEngine.find's result (bootstrap.bundle.js: `for (const t of
// R.find(...))`, `R.find(...).filter(t => ...)`, `.forEach(t => {...})`).
// A folded for-of's element after the last one is the runtime's unbound view
// (Runtime/Browser.hpp). element_arrays.test runs element_arrays_report() on
// the interpreter and natively over element_arrays.html and compares the
// answers.
var elementArraysAnswer = (function () {
    function find(selector) {
        return [].concat(...Element.prototype.querySelectorAll.call(document.documentElement, selector));
    }

    function run() {
        let out = '';
        let n = 0;
        for (const el of find('.item')) {
            out += el.id + ',';
            n = n + 1;
        }
        out += n + '|';
        out += find('li').map(el => el.id).join(',') + '|';
        out += find('li').filter(el => el.matches('.item')).length + '|';
        out += (find('li').some(el => el.matches('#b2')) ? 'some' : 'none') + '|';
        out += (find('li').every(el => el.matches('li')) ? 'every' : 'not') + '|';
        const hit = find('li').find(el => el.matches('.item'));
        out += (hit ? hit.id : 'none') + '|';
        find('li').forEach(el => {
            el.setAttribute('data-seen', 'yes');
        });
        out += find('[data-seen]').length;
        return out;
    }

    return run();
})();

function element_arrays_report() {
    return elementArraysAnswer + '';
}
