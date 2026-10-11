// The DOM reads of an ordinary native script: document.documentElement, an
// element's tagName, id, className and children, the root's clientWidth and
// clientHeight, window.devicePixelRatio, getBoundingClientRect guarded as
// css-dump.js guards it, getComputedStyle(el).getPropertyValue(name), and
// rows holding elements (dom_paths). dom-reads.test runs it on the interpreter and
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
        (window.devicePixelRatio || 1) + '|' + dom_paths();
}

// Rows of a path and its element, made by a named function expression that
// calls itself, as css-dump.js walks the page: the element is passed to the
// recursive call and kept in the row, and the row read back by position.
function dom_paths() {
    var els = [];
    (function walk(el, path) {
        els.push([path, el]);
        var kids = el.children;
        if (!kids) {
            return;
        }
        for (var i = 0; i < kids.length; i++) {
            walk(kids[i], path + '>' + i);
        }
    })(document.documentElement, 'html');
    var out = '';
    for (var j = 0; j < els.length; j++) {
        out += ' ' + els[j][0] + '=' + els[j][1].tagName;
    }
    return out;
}
