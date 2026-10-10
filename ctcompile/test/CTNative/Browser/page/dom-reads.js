// The DOM reads of an ordinary native script: document.documentElement, an
// element's tagName, id, className and children, the root's clientWidth and
// clientHeight, window.devicePixelRatio, getBoundingClientRect guarded as
// css-dump.js guards it, and getComputedStyle(el).getPropertyValue(name). dom-reads.test runs it on the interpreter and
// natively over dom-reads.html and compares the answers.
function dom_reads() {
    var root = document.documentElement;
    var out = root.tagName + ' ' + root.id + ' ' + root.className;
    var kids = root.children;
    if (!kids) {
        return out;
    }
    for (var i = 0; i < kids.length; i++) {
        var kid = kids[i];
        out += '|' + kid.tagName + '#' + kid.id + '.' + kid.className;
        var grand = kid.children;
        for (var j = 0; j < grand.length; j++) {
            var el = grand[j];
            var r = el.getBoundingClientRect ? el.getBoundingClientRect() : null;
            out += '>' + el.tagName + '@' + (r ? r.left : '') + ',' + (r ? r.top : '') + ' ' +
                (r ? r.width : '') + 'x' + (r ? r.height : '');
            var cs = getComputedStyle(el);
            out += ' ' + cs.getPropertyValue('display') + '/' + cs.getPropertyValue('margin-left');
        }
    }
    return out + '|' + root.clientWidth + 'x' + root.clientHeight + '@' +
        (window.devicePixelRatio || 1);
}
