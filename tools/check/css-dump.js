// The parity dump: one element per line, box geometry plus a fixed property set.
// Run through `compare.py eval` on ctbrowser AND Chrome; css-parity.py prepends
// PROPS/PROPS_VERSION/FROM/COUNT and diffs the two answers. The bottom of this
// file runs the dump when those globals are there.
//
// ctbrowser/unittests/unit/bootstrap_layout.cpp is the second caller: it loads
// this file into a fixture and calls css_dump_table() with its inputs as
// strings, so it binds no globals and nothing runs at load.
//
// WRITTEN AGAINST FOUR CONSTRAINTS, each of which has already been got wrong
// somewhere in this repo's history:
//
//  1. EVERYTHING GOES THROUGH console.log. ctdrive's `eval` can return nothing
//     else - it answers with what the page LOGGED, because run_script reports
//     whether a script ran, not what it produced (ctbrowser/tools/ctdrive/ctdrive.cpp). The
//     Playwright side patches console.log into an array to match
//     (tools/check/compare.py), so this is the one channel both engines share.
//     css_dump_table() RETURNS the table instead: its caller's engine seam
//     (ctbrowser/test/support/engine.hpp) carries a call's answer back itself.
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

// The table, one string per line, for both ways in. The parameters are named
// after the globals css-parity.py prepends.
function css_dump_lines(PROPS, PROPS_VERSION, FROM, COUNT, COMPACT) {
    var lines = [];
    // Document order, with a structural path as the identity. The path survives
    // a class or text change, which is what makes the ratchet comparable across
    // rungs; the tag/id/classes after it are for the human reading the report.
    var els = [];
    (function walk(el, path) {
        els.push([path, el]);
        var kids = el.children;
        if (!kids) {
            return;
        }
        for (var i = 0; i < kids.length; i++) {
            var kid = kids[i];
            var tag = (kid.tagName || '?').toLowerCase();
            walk(kid, path + '>' + tag + ':' + i);
        }
    })(document.documentElement, 'html');

    function key(el) {
        var out = (el.tagName || '?').toLowerCase();
        if (el.id) {
            out += '#' + el.id;
        }
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
        lines.push('#head ' + JSON.stringify({
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
        if (COMPACT) {
            // ONE LINE PER ELEMENT, EMPTIES OMITTED - the form
            // ctbrowser/unittests/unit/bootstrap_layout.cpp byte-compares against
            // ctbrowser/test/baseline/. Most values are empty today (the engine answers
            // for well under half of them), so JSON would make the baselines
            // several times larger and a `git diff` correspondingly harder to
            // read - and reading that diff is the entire reason the text baseline
            // exists beside the image golden. A property APPEARING is exactly the
            // signal wanted when something starts being modelled.
            var parts = [];
            for (var q = 0; q < pairs.length; q++) {
                var v = pairs[q][1];
                if (v !== '' && v !== null && v !== undefined) {
                    parts.push(pairs[q][0] + '=' + v);
                }
            }
            line = els[i][0] + '|' + key(el) + '|' + parts.join(' ');
        } else {
            // JSON for css-parity.py, which wants a dict and does not care about
            // key order.
            var out = {};
            for (var j = 0; j < pairs.length; j++) {
                out[pairs[j][0]] = pairs[j][1];
            }
            line = els[i][0] + '|' + key(el) + '|' + JSON.stringify(out);
        }
        lines.push(line);
    }
    return lines;
}

// bootstrap_layout.cpp's way in. Every argument is a STRING, because that is
// all the engine seam passes: the text of tools/check/css-parity-props.txt, then
// PROPS_VERSION, FROM, COUNT and COMPACT in decimal. Number() them before use -
// `FROM === 0` is false for "0", and "0" + "1000000" is not a count. The answer
// is the table with every line ended by a newline, which is the baseline file
// byte for byte.
function css_dump_table(propsText, version, from, count, compact) {
    // The geometry columns are filtered out. They are in the props file because
    // the REPORT has a column for each, but they come from getBoundingClientRect
    // rather than from getComputedStyle - asking the cascade for `@x` would just
    // add four empty answers. A trailing CR or space goes too, so a CRLF checkout
    // reads the same names.
    var props = [];
    var rows = propsText.split('\n');
    for (var i = 0; i < rows.length; i++) {
        var name = rows[i].replace(/[\r ]+$/, '');
        if (name !== '' && name[0] !== '#' && name[0] !== '@') {
            props.push(name);
        }
    }
    var lines = css_dump_lines(props, Number(version), Number(from), Number(count),
        Number(compact));
    return lines.join('\n') + '\n';
}

// css-parity.py's way in: the globals it prepends, and one console.log per line
// (constraint 1). COMPACT is optional, for a chunk pasted into a console by hand.
if (typeof PROPS !== 'undefined') {
    css_dump_lines(PROPS, PROPS_VERSION, FROM, COUNT,
        typeof COMPACT !== 'undefined' && COMPACT).forEach(function (line) {
        console.log(line);
    });
}
