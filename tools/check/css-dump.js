// The parity dump: one element per line, box geometry plus a fixed property set.
// Run through `compare.py eval` on ctbrowser AND Chrome; css-parity.py prepends
// PROPS/PROPS_VERSION/FROM/COUNT and diffs the two answers.
//
// WRITTEN AGAINST FOUR CONSTRAINTS, each of which has already been got wrong
// somewhere in this repo's history:
//
//  1. EVERYTHING GOES THROUGH console.log. ctdrive's `eval` can return nothing
//     else - it answers with what the page LOGGED, because run_script reports
//     whether a script ran, not what it produced (examples/cli/ctdrive.cpp). The
//     Playwright side patches console.log into an array to match
//     (tools/check/compare.py), so this is the one channel both engines share.
//
//  2. NO COMPLEX SELECTORS. ctbrowser's selector engine is the thing under test:
//     using `querySelectorAll('.card > .btn')` to find elements would make the
//     dump disagree with itself exactly where the engine is weakest. The walk
//     uses documentElement and .children and nothing else.
//
//  3. NO STATE BETWEEN CHUNKS. Playwright wraps each eval in a fresh
//     page.evaluate, so a top-level function does NOT persist between calls -
//     while in ctbrowser globals do. Anything that installed itself on the first
//     call and was called on the second would work in one engine only. So every
//     chunk is self-contained and re-sends the whole script; any chunk can also
//     be pasted into a console by hand, which is how you debug one element.
//
//  4. RAW VALUES, NORMALISED IN PYTHON. Two normalisers running in two engines
//     is two chances to normalise differently. This emits what the engine said.
(function (FROM, COUNT) {
    // Document order, with a structural path as the identity. The path survives
    // a class or text change, which is what makes the ratchet comparable across
    // rungs; the tag/id/classes after it are for the human reading the report.
    var els = [];
    (function walk(el, path) {
        els.push([path, el]);
        var kids = el.children;
        if (!kids) { return; }
        for (var i = 0; i < kids.length; i++) {
            var kid = kids[i];
            var tag = (kid.tagName || '?').toLowerCase();
            walk(kid, path + '>' + tag + ':' + i);
        }
    })(document.documentElement, 'html');

    function key(el) {
        var out = (el.tagName || '?').toLowerCase();
        if (el.id) { out += '#' + el.id; }
        var cls = (el.className || '').trim();
        if (cls) {
            // SORTED, so a difference in class ORDER is not a difference in
            // identity - the two engines are free to report className however
            // they like as long as the set matches.
            out += '.' + cls.split(/\s+/).sort().join('.');
        }
        return out;
    }

    if (FROM === 0) {
        // The header, checked BEFORE any element. clientWidth is the trap:
        // ctbrowser re-runs layout at width-15 when a page overflows
        // (browser::run_layout) and Chrome's classic scrollbar also takes width
        // from the initial containing block. A 15px disagreement moves every
        // element's @x and @w, and the report becomes eight thousand lines about
        // one number. css-parity.py exits on a mismatch here instead.
        console.log('#head ' + JSON.stringify({
            v: PROPS_VERSION,
            n: els.length,
            cw: document.documentElement.clientWidth,
            ch: document.documentElement.clientHeight,
            dpr: window.devicePixelRatio || 1
        }));
    }

    var last = Math.min(FROM + COUNT, els.length);
    for (var i = FROM; i < last; i++) {
        var el = els[i][1];
        // AN ORDERED LIST of pairs, not an object, because the compact form below
        // has to be byte-stable: `for (var k in obj)` is insertion-ordered for
        // string keys by spec but relying on two engines agreeing about that to
        // keep a golden stable is a bet with no upside.
        var pairs = [];
        // getBoundingClientRect is the BORDER box in both engines and is defined
        // identically by the spec, which is why the geometry channel is these
        // four numbers rather than the `width` property - `width` is the content
        // box and depends on box-sizing, borders and padding all being right.
        var r = el.getBoundingClientRect ? el.getBoundingClientRect() : null;
        pairs.push(['@x', r ? r.left : '']);
        pairs.push(['@y', r ? r.top : '']);
        pairs.push(['@w', r ? r.width : '']);
        pairs.push(['@h', r ? r.height : '']);
        var cs = getComputedStyle(el);
        for (var p = 0; p < PROPS.length; p++) {
            // getPropertyValue, never the camelCase attribute: it is the one
            // spelling both engines agree on, and in ctbrowser it is also the
            // only one that answers for an INHERITED property the element did
            // not declare itself.
            pairs.push([PROPS[p], cs.getPropertyValue(PROPS[p])]);
        }

        var line;
        if (typeof COMPACT !== 'undefined' && COMPACT) {
            // ONE LINE PER ELEMENT, EMPTIES OMITTED - the form
            // tests/unit/bootstrap_layout.cpp byte-compares against
            // tests/baseline/. Most values are empty today (the engine answers
            // for well under half of them), so JSON would make the baselines
            // several times larger and a `git diff` correspondingly harder to
            // read - and reading that diff is the entire reason the text baseline
            // exists beside the image golden. A property APPEARING is exactly the
            // signal wanted when something starts being modelled.
            var parts = [];
            for (var q = 0; q < pairs.length; q++) {
                var v = pairs[q][1];
                if (v !== '' && v !== null && v !== undefined) { parts.push(pairs[q][0] + '=' + v); }
            }
            line = els[i][0] + '|' + key(el) + '|' + parts.join(' ');
        } else {
            // JSON for css-parity.py, which wants a dict and does not care about
            // key order.
            var out = {};
            for (var j = 0; j < pairs.length; j++) { out[pairs[j][0]] = pairs[j][1]; }
            line = els[i][0] + '|' + key(el) + '|' + JSON.stringify(out);
        }
        console.log(line);
    }
})(FROM, COUNT);
