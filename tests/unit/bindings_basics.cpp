// The DOM bindings: script driving the page.
//
// the previous engine's bindings were safe because the document owned every node forever and
// nothing was concurrent. this engine's hold HANDLES, so the interesting tests are the
// ones the previous engine could not have failed:
//
//   * a wrapper for a removed element resolves to nothing, and its methods do
//     nothing, rather than writing through a dangling pointer
//   * a mutation from script invalidates the pipeline, so the NEXT frame shows
//     it - script and rendering are not two views that can disagree
//
// Plus the ordinary web-platform surface, checked through the browser rather
// than against the bindings in isolation: a binding that mutates the DOM but
// does not change what is drawn is not working, whatever a unit test says.

#include <ctbrowser/app/app.hpp>
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;

namespace {

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %s\n", std::string{what}.c_str());
        ++ctbrowser_test_failures;
    }
}

// Everything the page ends up drawing as text. The honest way to ask "did the
// page change", since that is what a user sees.
[[nodiscard]] node_id find_id(browser & page, std::string_view want) {
    const auto txn = page.doc().read();
    const atom key = page.atoms().intern("id");
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!found && txn.attribute_value(at, key) == want) { found = at; }
        for (const node_id c : txn.children(at)) { self(self, c); }
    };
    walk(walk, txn.root());
    return found;
}

[[nodiscard]] std::string rendered_text(browser & page) {
    std::string out;
    const auto walk = [&](auto && self, const layout::fragment & f) -> void {
        out += f.text;
        for (const auto & c : f.children) { self(self, c); }
    };
    walk(walk, page.fragments());
    return out;
}

[[nodiscard]] std::size_t count_fill(browser & page, color want) {
    std::size_t n = 0;
    for (const auto & layer : page.layers().layers) {
        if (!layer.contents) { continue; }
        for (const auto & c : layer.contents->commands()) {
            if (c.fill == want) { ++n; }
        }
    }
    return n;
}

// A file as bytes, for the asset registry. p5.js is 4.4 MB on disk and the
// registry is what a `<script src>` resolves against, so the test loads it the
// same way a page would rather than inlining it.
[[nodiscard]] std::vector<std::byte> bytes_of(std::string_view text) {
    return std::vector<std::byte>{reinterpret_cast<const std::byte *>(text.data()),
                                  reinterpret_cast<const std::byte *>(text.data() + text.size())};
}

[[nodiscard]] std::vector<std::byte> read_bytes(const std::string & path) {
    std::ifstream in{path, std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    return std::vector<std::byte>{reinterpret_cast<const std::byte *>(text.data()),
                                  reinterpret_cast<const std::byte *>(text.data() + text.size())};
}

[[nodiscard]] const std::vector<std::string> & log_of(browser & page) {
    return page.bindings().console_output();
}

// --- element.classList and element.style ----------------------------------

void test_class_list_edits_the_attribute() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><div id=d class="a b"></div><script>
        const d = document.getElementById('d');
        d.classList.add('c');
        d.classList.add('c');
        console.log('added=' + d.getAttribute('class'));
        console.log('has=' + d.classList.contains('b') + ',' + d.classList.contains('zz'));
        console.log('len=' + d.classList.length);
        d.classList.remove('b');
        console.log('removed=' + d.getAttribute('class'));
        console.log('forced=' + d.classList.toggle('a', true) + ',' + d.getAttribute('class'));
        d.classList.toggle('a');
        console.log('flipped=' + d.getAttribute('class'));
        console.log('item=' + d.classList.item(0) + ',' + d.classList.item(9));
    </script></body></html>)");
    check(page.script_error().empty(), "the class list script ran: " + page.script_error());
    const auto & log = log_of(page);
    // add is a SET, not an append - twice is once.
    check(log[0] == "added=a b c", "classList.add appends a token, once: " + log[0]);
    check(log[1] == "has=true,false", "classList.contains: " + log[1]);
    // The count is live, so it moves with the attribute rather than reporting
    // whatever it was when the element was first wrapped.
    check(log[2] == "len=3", "classList.length: " + log[2]);
    check(log[3] == "removed=a c", "classList.remove: " + log[3]);
    check(log[4] == "forced=true,a c", "toggle(name, true) forces rather than flips: " + log[4]);
    check(log[5] == "flipped=c", "toggle(name) flips: " + log[5]);
    check(log[6] == "item=c,null", "classList.item, past the end too: " + log[6]);
}

void test_class_list_reaches_the_cascade() {
    browser page{browser_options{400, 200}};
    // The point of writing the attribute rather than keeping a list beside it:
    // the style engine matches on what the DOM says.
    page.load_html(R"(<html><head><style>
    .on { background-color: #008000 }
    </style></head><body><div id=d>x</div>
    <script>document.getElementById('d').classList.add('on');</script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    check(page.frame().has_value(), "the page renders");
    check(count_fill(page, color::rgba(0, 128, 0)) == 1, "a class added from script cascades");
}

void test_style_writes_reach_the_document() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><div id=d></div><script>
        const d = document.getElementById('d');
        d.style.display = 'none';
        console.log('attr=' + d.getAttribute('style'));
        console.log('read=' + d.style.display);
        d.style.backgroundColor = 'red';
        console.log('camel=' + d.getAttribute('style'));
        d.style.display = '';
        console.log('cleared=' + d.getAttribute('style'));
        d.style.setProperty('--custom', '4px');
        console.log('custom=' + d.style.getPropertyValue('--custom'));
        d.style.removeProperty('--custom');
        console.log('gone=' + d.getAttribute('style'));
    </script></body></html>)");
    check(page.script_error().empty(), "the style script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "attr=display: none; ", "a style write serialises to the attribute: " + log[0]);
    // The proxy's target holds the declarations, so a read needs no trap.
    check(log[1] == "read=none", "a style property reads back: " + log[1]);
    // The IDL name and the CSS name are different spellings of one property.
    check(log[2].find("background-color: red") != std::string::npos,
          "backgroundColor is background-color: " + log[2]);
    // Assigning "" REMOVES a declaration. Emitting `display: ;` instead would
    // leave the old value standing as far as the parser is concerned.
    check(log[3].find("display") == std::string::npos,
          "assigning the empty string removes it: " + log[3]);
    check(log[4] == "custom=4px", "setProperty reaches a name no identifier can spell: " + log[4]);
    check(log[5].find("custom") == std::string::npos, "removeProperty: " + log[5]);
}

void test_style_writes_reach_the_pixels() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><div id=d>x</div><script>
        document.getElementById('d').style.backgroundColor = '#0000ff';
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    check(page.frame().has_value(), "the page renders");
    check(count_fill(page, color::rgba(0, 0, 255)) == 1, "a style write repaints");
}

void test_window_is_the_global_object() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><script>
        // A global reached through the window, which is how a library calls a
        // host function it did not have to look up by bare name.
        console.log('raf=' + (typeof window.requestAnimationFrame));
        console.log('in=' + ('setTimeout' in window) + ',' + ('nosuch' in window));
        // A top-level declaration IS a global, so the window can see it - this
        // is how p5.js decides a sketch is in global mode.
        console.log('decl=' + (typeof window.sketchSetup));
        function sketchSetup() {}
        // ...and a write through the window defines a global, which is how p5
        // installs its ~200 drawing functions for a sketch to call bare.
        window.installed = 7;
        console.log('bare=' + installed);
        // An own property of the window keeps its own storage rather than
        // shadowing itself in the globals.
        console.log('own=' + (window.innerWidth > 0));
        console.log('same=' + (window === globalThis));
    </script></body></html>)");
    check(page.script_error().empty(), "the window script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "raf=function", "a global is reachable through the window: " + log[0]);
    check(log[1] == "in=true,false", "`in` asks the globals too: " + log[1]);
    check(log[2] == "decl=function", "a hoisted declaration is on the window: " + log[2]);
    check(log[3] == "bare=7", "a write through the window defines a global: " + log[3]);
    check(log[4] == "own=true", "the window keeps its own properties: " + log[4]);
    check(log[5] == "same=true", "globalThis is the window: " + log[5]);
}

// The REFLECTED attributes: id, className, width, height.
//
// These are IDL attributes over content attributes - reading one reads the
// attribute, writing one writes it. As data properties they only went one way:
// a page's assignment changed the wrapper alone and the next refresh put the
// old value back. p5.js names its canvas and sizes it exactly that way, so both
// writes vanished and left a nameless 300x150 canvas.
void test_reflected_attributes() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><div id=d></div><canvas id=c></canvas><script>
        const d = document.getElementById('d');
        d.id = 'renamed';
        console.log('id=' + d.getAttribute('id') + ',' + (document.getElementById('renamed') !== null));
        d.className = 'a b';
        console.log('class=' + d.getAttribute('class') + ',' + d.classList.length);
        const c = document.getElementById('c');
        // The HTML defaults, which a page that omits the attributes relies on.
        console.log('default=' + c.width + 'x' + c.height);
        c.width = 640; c.height = 480;
        console.log('sized=' + c.width + 'x' + c.height +
                    ' attr=' + c.getAttribute('width') + 'x' + c.getAttribute('height'));
    </script></body></html>)");
    check(page.script_error().empty(), "the reflection script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "id=renamed,true", "a written id reaches the document: " + log[0]);
    check(log[1] == "class=a b,2", "className and classList agree: " + log[1]);
    check(log[2] == "default=300x150",
          "a canvas without attributes has the HTML defaults: " + log[2]);
    check(log[3] == "sized=640x480 attr=640x480",
          "a written size reaches the attribute: " + log[3]);
}

// parentNode / remove / insertBefore - WALKING the tree, not just editing it.
//
// appendChild and removeChild already worked; nothing could find a parent. That
// makes `this.elt.parentNode.removeChild(this.elt)` - the ordinary way to take
// an element out of a page - throw, which is how p5.js's discarded default
// canvas stayed in the document underneath the real one.
void test_tree_navigation() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><div id=box><span id=a></span><span id=b></span></div><script>
        const box = document.getElementById('box');
        const a = document.getElementById('a');
        console.log('parent=' + a.parentNode.id + ',' + a.parentElement.id);
        console.log('children=' + box.children.length);
        // The idiom that needs both halves.
        a.parentNode.removeChild(a);
        console.log('removed=' + box.children.length + ',' + (document.getElementById('a') === null));
        const c = document.createElement('span');
        c.id = 'c';
        box.insertBefore(c, document.getElementById('b'));
        console.log('inserted=' + box.children[0].id + ',' + box.children[1].id);
        document.getElementById('b').remove();
        console.log('self=' + box.children.length);
    </script></body></html>)");
    check(page.script_error().empty(), "the navigation script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "parent=box,box", "parentNode and parentElement: " + log[0]);
    check(log[1] == "children=2", "children lists the element children: " + log[1]);
    check(log[2] == "removed=1,true", "removeChild through parentNode: " + log[2]);
    check(log[3] == "inserted=c,b", "insertBefore puts it before the reference: " + log[3]);
    check(log[4] == "self=1", "remove() takes an element out itself: " + log[4]);
}

// The canvas additions p5.js draws through: the transform family, ellipse and
// Path2D. A Path2D is a RECORDING - built once and replayed by fill(path) or
// stroke(path), which is how p5 draws every 2D shape.
void test_canvas_transform_and_paths() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><canvas id=c width=100 height=100></canvas>
        <canvas id=d width=100 height=100></canvas><script>
        const ctx = document.getElementById('c').getContext('2d');
        ctx.setTransform(2, 0, 0, 3, 4, 5);
        let t = ctx.getTransform();
        console.log('set=' + [t.a, t.b, t.c, t.d, t.e, t.f].join(','));
        ctx.transform(1, 0, 0, 1, 10, 0);   // composes, rather than replacing
        t = ctx.getTransform();
        console.log('composed=' + t.a + ',' + t.d + ',' + t.e + ',' + t.f);
        ctx.setTransform();                 // no arguments is the identity
        t = ctx.getTransform();
        console.log('identity=' + [t.a, t.b, t.c, t.d, t.e, t.f].join(','));
        // A path built now, drawn later.
        const p = new Path2D();
        p.moveTo(10, 10); p.lineTo(60, 10); p.lineTo(60, 60); p.lineTo(10, 60); p.closePath();
        ctx.fillStyle = '#ff0000';
        ctx.fill(p);
        // A copy carries the original's verbs, which p5 relies on when it makes
        // separate fill and stroke paths - so it is drawn on its OWN canvas,
        // where the square it inherited cannot be mistaken for the first fill.
        const copy = new Path2D(p);
        copy.moveTo(70, 70); copy.lineTo(90, 70); copy.lineTo(90, 90); copy.closePath();
        const other = document.getElementById('d').getContext('2d');
        other.fillStyle = '#0000ff';
        other.fill(copy);
        ctx.beginPath();
        ctx.ellipse(50, 50, 20, 10, 0, 0, 6.2831853);
        ctx.fillStyle = '#00ff00';
        ctx.fill();
    </script></body></html>)");
    check(page.script_error().empty(), "the canvas script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "set=2,0,0,3,4,5", "setTransform REPLACES the matrix: " + log[0]);
    // translate(10, 0) under a 2x scale moves 20 device units, which is what
    // makes this compose rather than replace.
    check(log[1] == "composed=2,3,24,5", "transform composes with it: " + log[1]);
    check(log[2] == "identity=1,0,0,1,0,0", "setTransform with no arguments: " + log[2]);
    if (const auto pixels = page.canvases().pixels_of(find_id(page, "c"))) {
        const auto at = [&](int x, int y) { return color{pixels->at(x, y)}; };
        // The Path2D actually filled, and its copy filled somewhere else.
        check(at(20, 20) == color::rgba(255, 0, 0), "fill(path) replayed the recording");
        // ...and the ellipse is an ellipse: wide at the waist, empty above it.
        check(at(66, 50) == color::rgba(0, 255, 0), "the ellipse reaches its x radius");
        check(at(50, 34) != color::rgba(0, 255, 0), "and not past its y radius");
    } else {
        check(false, "the canvas has pixels");
    }
    if (const auto pixels = page.canvases().pixels_of(find_id(page, "d"))) {
        const auto at = [&](int x, int y) { return color{pixels->at(x, y)}; };
        check(at(20, 20) == color::rgba(0, 0, 255), "a copied Path2D carries the original verbs");
        check(at(80, 80) == color::rgba(0, 0, 255), "...and the ones added after the copy");
    } else {
        check(false, "the second canvas has pixels");
    }
}

// textAlign and textBaseline - where text sits relative to the point it was
// given. The most-used canvas properties this engine did not have: p5.js sets
// textAlign 229 times, and without them every label started at x on the
// alphabetic baseline, so a right-aligned one ran off the edge it was aligned
// to and a centred one was centred nowhere.
void test_text_alignment() {
    browser page{browser_options{400, 400}};
    page.load_html(R"(<html><body><canvas id=c width=300 height=360></canvas><script>
        const ctx = document.getElementById('c').getContext('2d');
        ctx.fillStyle = '#ffffff'; ctx.fillRect(0, 0, 300, 360);
        ctx.fillStyle = '#000000';
        ctx.font = '20px sans-serif';
        // Three rows at the same x, one per alignment.
        ctx.textBaseline = 'alphabetic';
        ctx.textAlign = 'left';   ctx.fillText('MM', 150, 30);
        ctx.textAlign = 'center'; ctx.fillText('MM', 150, 70);
        ctx.textAlign = 'right';  ctx.fillText('MM', 150, 110);
        // ...and three well-separated rows, one per baseline.
        ctx.textAlign = 'left';
        ctx.textBaseline = 'top';        ctx.fillText('MM', 10, 170);
        ctx.textBaseline = 'alphabetic'; ctx.fillText('MM', 10, 250);
        ctx.textBaseline = 'bottom';     ctx.fillText('MM', 10, 330);
        // The metrics a library positions text from - p5 measures a line as
        // left + right and got NaN + NaN while only `width` existed.
        const m = ctx.measureText('MM');
        console.log('box=' + (m.actualBoundingBoxLeft + m.actualBoundingBoxRight === m.width) +
                    ',' + (m.actualBoundingBoxAscent > 0) +
                    // The fallback bitmap font has no descent at all, so this
                    // asks that the number EXISTS rather than that it is
                    // positive - `undefined >= 0` is false, which is the case
                    // this is here to catch.
                    ',' + (m.fontBoundingBoxDescent >= 0));
    </script></body></html>)");
    check(page.script_error().empty(), "the text script ran: " + page.script_error());
    check(log_of(page)[0] == "box=true,true,true",
          "measureText reports a bounding box, not just a width: " + log_of(page)[0]);

    const auto pixels = page.canvases().pixels_of(find_id(page, "c"));
    check(pixels != nullptr, "the canvas has pixels");
    if (pixels == nullptr) { return; }
    // The horizontal extent of the ink in a band of rows.
    const auto ink_x = [&](int from_y, int to_y) {
        int lo = pixels->width, hi = -1;
        for (int y = from_y; y < to_y; ++y) {
            for (int x = 0; x < pixels->width; ++x) {
                if (color{pixels->at(x, y)}.red() < 128) {
                    lo = std::min(lo, x);
                    hi = std::max(hi, x);
                }
            }
        }
        return std::pair{lo, hi};
    };
    const auto ink_y = [&](int from_y, int to_y) {
        int lo = pixels->height, hi = -1;
        for (int y = from_y; y < to_y; ++y) {
            for (int x = 0; x < pixels->width; ++x) {
                if (color{pixels->at(x, y)}.red() < 128) {
                    lo = std::min(lo, y);
                    hi = std::max(hi, y);
                }
            }
        }
        return std::pair{lo, hi};
    };
    const auto [left_lo, left_hi] = ink_x(10, 35);
    const auto [mid_lo, mid_hi] = ink_x(50, 75);
    const auto [right_lo, right_hi] = ink_x(90, 115);
    check(left_hi > left_lo, "the left-aligned run drew something");
    // Left STARTS at the anchor, right ENDS at it, centre straddles it. Stated
    // as inequalities against the anchor rather than exact columns, because the
    // glyphs' own side bearings are the font's business and not this test's.
    check(left_lo >= 149 && left_hi > 150, "textAlign left starts at the anchor");
    check(right_hi <= 151 && right_lo < 150, "textAlign right ends at the anchor");
    check(mid_lo < 150 && mid_hi > 150 && std::abs((mid_lo + mid_hi) / 2 - 150) <= 2,
          "textAlign center straddles the anchor");

    const auto [top_lo, top_hi] = ink_y(140, 210);
    const auto [base_lo, base_hi] = ink_y(220, 290);
    const auto [bottom_lo, bottom_hi] = ink_y(300, 360);
    check(top_hi > top_lo, "the top-baseline run drew something");
    // `top` hangs BELOW its anchor, `bottom` sits entirely above it, and
    // `alphabetic` sits on it with only descenders below.
    check(top_lo >= 170, "textBaseline top puts the text below the anchor");
    check(bottom_hi <= 331, "textBaseline bottom puts it above the anchor");
    check(base_lo < 250 && base_hi <= 256, "textBaseline alphabetic sits on the anchor");
}

// WHICH POINTS ARE INSIDE a path that crosses itself.
//
// The spec's default is nonzero winding and this filled even-odd, so a star -
// or anything else drawn as one continuous self-crossing path, which is most
// of what beginShape/vertex is used for - came out with a hole in the middle
// and nothing said so.
void test_fill_rule() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><canvas id=c width=200 height=100></canvas><script>
        const ctx = document.getElementById('c').getContext('2d');
        ctx.fillStyle = '#ffffff'; ctx.fillRect(0, 0, 200, 100);
        function star(cx) {
          ctx.beginPath();
          for (let i = 0; i < 5; i++) {
            const a = -Math.PI / 2 + i * 4 * Math.PI / 5;
            const x = cx + 40 * Math.cos(a), y = 50 + 40 * Math.sin(a);
            if (i === 0) { ctx.moveTo(x, y); } else { ctx.lineTo(x, y); }
          }
          ctx.closePath();
        }
        ctx.fillStyle = '#ff0000'; star(50);  ctx.fill();            // the default
        ctx.fillStyle = '#0000ff'; star(150); ctx.fill('evenodd');
    </script></body></html>)");
    check(page.script_error().empty(), "the fill-rule script ran: " + page.script_error());
    const auto pixels = page.canvases().pixels_of(find_id(page, "c"));
    check(pixels != nullptr, "the canvas has pixels");
    if (pixels == nullptr) { return; }
    const auto at = [&](int x, int y) { return color{pixels->at(x, y)}; };
    // A star's arms are inside under BOTH rules; only its middle differs.
    check(at(50, 20) == color::rgba(255, 0, 0), "the default fills the star's arm");
    check(at(150, 20) == color::rgba(0, 0, 255), "so does even-odd");
    // The centre is the whole difference: nonzero solid, even-odd hollow.
    check(at(50, 50) == color::rgba(255, 0, 0), "nonzero fills the middle of a star");
    check(at(150, 50) == color::rgba(255, 255, 255), "even-odd leaves it hollow");
}

// `globalCompositeOperation` - EVERY MODE, because ignoring it was a silent
// wrong answer and half-implementing it would be another.
//
// The maths is the W3C Compositing and Blending Level 1 formula (see
// shell/page/composite.hpp), so the expectations here are computed from the spec by
// hand rather than recorded from a run - a test that records what the code did
// cannot tell you the code is right.
void test_composite_operations() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><canvas id=c width=80 height=40></canvas><script>
        const ctx = document.getElementById('c').getContext('2d');
        // Each cell: an opaque backdrop, then one source over it in some mode.
        // Backdrop #804020 (128,64,32), source #40c060 (64,192,96), both opaque.
        function cell(i, mode) {
          ctx.globalCompositeOperation = 'source-over';
          ctx.fillStyle = '#804020';
          ctx.fillRect(i * 8, 0, 8, 8);
          ctx.globalCompositeOperation = mode;
          ctx.fillStyle = '#40c060';
          ctx.fillRect(i * 8, 0, 8, 8);
        }
        cell(0, 'source-over');
        cell(1, 'multiply');
        cell(2, 'screen');
        cell(3, 'darken');
        cell(4, 'lighten');
        cell(5, 'difference');
        cell(6, 'exclusion');
        cell(7, 'destination-over');
        // The operator has to come back with restore(), and read back as the
        // string that was set.
        ctx.globalCompositeOperation = 'source-over';
        ctx.save();
        ctx.globalCompositeOperation = 'multiply';
        console.log('set=' + ctx.globalCompositeOperation);
        ctx.restore();
        console.log('restored=' + ctx.globalCompositeOperation);
        // An unknown name behaves as source-over rather than throwing.
        ctx.globalCompositeOperation = 'nonsense';
        ctx.fillStyle = '#ff0000';
        ctx.fillRect(0, 30, 8, 8);
    </script></body></html>)");
    check(page.script_error().empty(), "the composite script ran: " + page.script_error());
    const auto pixels = page.canvases().pixels_of(find_id(page, "c"));
    check(pixels != nullptr, "the canvas has pixels");
    if (pixels == nullptr) { return; }
    const auto at = [&](int cell) { return color{pixels->at(cell * 8 + 4, 4)}; };

    // Backdrop b = (128, 64, 32), source s = (64, 192, 96), both opaque - so
    // ao = 1 and the result is B(b, s) with no alpha weighting at all.
    check(at(0) == color::rgba(64, 192, 96), "source-over is the source");
    // multiply: b*s/255 = (32, 48, 12)
    check(at(1) == color::rgba(32, 48, 12), "multiply");
    // screen: b + s - b*s/255 = (160, 208, 116)
    check(at(2) == color::rgba(160, 208, 116), "screen");
    check(at(3) == color::rgba(64, 64, 32), "darken takes the min per channel");
    check(at(4) == color::rgba(128, 192, 96), "lighten takes the max per channel");
    check(at(5) == color::rgba(64, 128, 64), "difference");
    // exclusion: b + s - 2*b*s/255 = (128, 160, 104)
    check(at(6) == color::rgba(128, 160, 104), "exclusion");
    check(at(7) == color::rgba(128, 64, 32), "destination-over keeps the backdrop");

    const auto & log = log_of(page);
    check(log.size() >= 2, "the property reported itself");
    if (log.size() >= 2) {
        check(log[0] == "set=multiply", "the operator reads back what was set: " + log[0]);
        check(log[1] == "restored=source-over", "and restore() puts it back: " + log[1]);
    }
    check(color{pixels->at(4, 34)} == color::rgba(255, 0, 0),
          "an unknown operator draws as source-over rather than not at all");
}

// THE FIVE OPERATORS THAT CLEAR WHAT THE SOURCE NEVER TOUCHED.
//
// Put as = 0 in the formula and ao comes out 0 for source-in, source-out,
// destination-in, destination-atop and copy. So `destination-in` with a small
// shape does not mask that shape - it throws away everything outside it, which
// is what a page uses it for. Compositing only the pixels the source covered
// would leave the rest of the canvas untouched and look almost right.
//
// This is the half of globalCompositeOperation that is easy to skip, and p5's
// tint() depends on it: the destination-in pass is what restores the alpha
// channel the multiply destroyed.
void test_composite_clears_untouched_pixels() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><canvas id=c width=40 height=40></canvas><script>
        const ctx = document.getElementById('c').getContext('2d');
        ctx.fillStyle = '#ff0000';
        ctx.fillRect(0, 0, 40, 40);          // the whole canvas red
        ctx.globalCompositeOperation = 'destination-in';
        ctx.fillStyle = '#000000';
        ctx.fillRect(0, 0, 10, 10);          // a small opaque shape
    </script></body></html>)");
    check(page.script_error().empty(), "the destination-in script ran: " + page.script_error());
    const auto pixels = page.canvases().pixels_of(find_id(page, "c"));
    if (pixels == nullptr) { return; }
    // Inside the shape: the BACKDROP survives - destination-in keeps the
    // destination's colour and takes the source's alpha.
    check(color{pixels->at(5, 5)} == color::rgba(255, 0, 0),
          "destination-in keeps the backdrop where the source covered it");
    // Outside it: gone. Not red, and not the source's black either.
    check(pixels->at(30, 30) == 0, "and clears everything the source did not touch");
}

// getImageData / putImageData / createImageData - reading back what was
// drawn and writing back what was computed. Every filter, every colour pick
// and every `pixels[]` loop goes through them.
void test_image_data() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><canvas id=c width=40 height=20></canvas><script>
        const ctx = document.getElementById('c').getContext('2d');
        ctx.fillStyle = '#204080'; ctx.fillRect(0, 0, 40, 20);
        const d = ctx.getImageData(0, 0, 40, 20);
        console.log('size=' + d.width + 'x' + d.height + ',' + d.data.length);
        console.log('rgba=' + d.data[0] + ',' + d.data[1] + ',' + d.data[2] + ',' + d.data[3]);
        for (let i = 0; i < d.data.length; i += 4) {
          d.data[i] = 255 - d.data[i];
          d.data[i + 1] = 255 - d.data[i + 1];
          d.data[i + 2] = 255 - d.data[i + 2];
        }
        ctx.putImageData(d, 0, 0);
        const back = ctx.getImageData(0, 0, 1, 1);
        console.log('inverted=' + back.data[0] + ',' + back.data[1] + ',' + back.data[2]);
        const blank = ctx.createImageData(4, 4);
        console.log('blank=' + blank.width + ',' + blank.data.length + ',' + blank.data[0]);
        const made = new ImageData(new Uint8ClampedArray(3 * 2 * 4), 3, 2);
        console.log('made=' + made.width + 'x' + made.height + ',' + made.data.length);
        // A real Uint8ClampedArray, so a page's out-of-range write clamps.
        d.data[0] = 400;
        console.log('clamps=' + d.data[0]);
        // Only part of the canvas, at an offset.
        ctx.fillStyle = '#00ff00'; ctx.fillRect(10, 5, 4, 4);
        const patch = ctx.getImageData(10, 5, 2, 2);
        console.log('patch=' + patch.data[0] + ',' + patch.data[1] + ',' + patch.data[2]);
    </script></body></html>)");
    check(page.script_error().empty(), "the image-data script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "size=40x20,3200", "the buffer is four bytes a pixel: " + log[0]);
    // RGBA in that order, which is NOT the engine's packed ARGB - getting it
    // wrong swaps red and blue and looks almost right.
    check(log[1] == "rgba=32,64,128,255", "getImageData reads RGBA in order: " + log[1]);
    check(log[2] == "inverted=223,191,127", "putImageData writes them back: " + log[2]);
    check(log[3] == "blank=4,64,0", "createImageData is blank and the right size: " + log[3]);
    check(log[4] == "made=3x2,24", "new ImageData(data, w, h): " + log[4]);
    check(log[5] == "clamps=255", "the buffer is a real clamped array: " + log[5]);
    check(log[6] == "patch=0,255,0", "getImageData reads from the offset given: " + log[6]);
}

// p5.js INPUT: the bundle's own event system, driven by real events.
//
// The other p5 pages here draw; this one asks whether p5 hears the browser.
// p5 registers its listeners on the window with `{passive, signal}` options
// and reads `event.clientX`, `event.key` and the rest off the event object, so
// a gap anywhere along that path leaves a sketch that renders correctly and
// never responds - which is exactly how it looks to a user, with no error.
//
// Instance mode, so the assertions name what they mean rather than relying on
// p5 having installed 200 globals.
void test_p5_receives_input() {
    browser page{browser_options{300, 300}};
    page.assets().add("p5.js", read_bytes("vendor/p5/p5.js"));
    page.load_html(R"(<html><head><script>var IS_MINIFIED = true;</script>
        <script src="p5.js"></script></head><body style="margin:0"><script>
        var log = '';
        var sketch = null;
        new p5(function (s) {
          sketch = s;
          s.setup = function () { s.createCanvas(200, 200); s.noLoop(); };
          s.draw = function () {};
          s.mousePressed = function () { log += 'down@' + s.mouseX + ',' + s.mouseY + ';'; };
          s.mouseReleased = function () { log += 'up;'; };
          s.mouseMoved = function () { log += 'move@' + s.mouseX + ',' + s.mouseY + ';'; };
          s.keyPressed = function () { log += 'key(' + s.key + ');'; };
        });
        // p5 computes mouseX by subtracting the canvas's own box from the
        // event's viewport coordinates, so the check is that the two AGREE -
        // not that the canvas sits at any particular place on the page.
        function report() {
          const box = sketch._renderer.canvas.getBoundingClientRect();
          console.log(log + ' | offset=' + (40 - box.left) + ',' + (60 - box.top) +
                      ' | pressed=' + sketch.mouseIsPressed);
        }
    </script></body></html>)");
    check(page.script_error().empty(), "p5 loaded: " + page.script_error());
    // A frame first, so p5 has finished starting up and attached its listeners.
    (void)page.frame();
    page.tick(16);

    (void)page.handle(input_event::mouse_move_to(40, 60));
    (void)page.handle(input_event::mouse_down_at(40, 60));
    (void)page.handle(input_event::mouse_up_at(40, 60));
    (void)page.handle(input_event::key_press("KeyQ"));
    page.tick(16);
    (void)page.run_script("report();");

    const auto & log = log_of(page);
    check(!log.empty(), "the sketch reported");
    if (log.empty()) { return; }
    const std::string & line = log.back();
    // The COORDINATES matter as much as the event: p5 computes mouseX from the
    // event's clientX against the canvas's box, so a listener that fires with
    // no position leaves every sketch drawing at 0,0.
    // The COORDINATES matter as much as the event: p5 turns the event's
    // clientX into mouseX by subtracting the canvas's box, which is what
    // getBoundingClientRect is for. Compared against that box rather than
    // against a fixed number, so the test says "p5 and the DOM agree" instead
    // of pinning where p5 happens to put its canvas.
    const std::string offset = line.substr(line.find("offset=") + 7);
    const std::string want = offset.substr(0, offset.find(' '));
    check(line.find("move@" + want + ";") != std::string::npos,
          "mouseMoved at the canvas-relative position: " + line);
    check(line.find("down@" + want + ";") != std::string::npos,
          "mousePressed at the canvas-relative position: " + line);
    check(line.find("up;") != std::string::npos, "mouseReleased: " + line);
    check(line.find("key(q);") != std::string::npos, "keyPressed with the key: " + line);
    // ...and released, so the flag is not simply stuck on.
    check(line.find("pressed=false") != std::string::npos,
          "mouseIsPressed went back down: " + line);
}

// WEBGL COMPILES AND CONSTRUCTS, AND REFUSES CLEANLY.
//
// The scope for p5.js here is 2D. That is not the same as WebGL being absent:
// `class RendererGL extends Renderer3D` has to produce a working constructor
// at load or the bundle does not finish defining itself, and p5 registers it
// in its renderer table whether or not a sketch asks for it. What a sketch
// that DOES ask for it gets is a catchable Error naming WebGL - the same shape
// of refusal `new Function` gives, and the opposite of a getContext('webgl')
// that hands back an object with no drawing on it.
void test_webgl_is_constructible_and_refuses() {
    browser page{browser_options{300, 300}};
    page.assets().add("p5.js", read_bytes("vendor/p5/p5.js"));
    page.load_html(R"(<html><head><script>var IS_MINIFIED = true;</script>
        <script src="p5.js"></script></head><body><script>
        console.log('registered=' + (typeof p5.renderers['webgl']));
        var outcome = 'no error';
        try {
          new p5(function (s) {
            s.setup = function () { s.createCanvas(100, 100, s.WEBGL); };
            s.draw = function () {};
          });
        } catch (e) { outcome = e.name + ': ' + e.message; }
        console.log('asking=' + outcome);
    </script></body></html>)");
    check(page.script_error().empty(), "the page loaded: " + page.script_error());
    // FOUND, not indexed: p5 logs its own diagnostics too, and a test that
    // counts console lines breaks whenever the library says something new -
    // which is a fact about p5, not about the thing under test.
    const auto said = [&](std::string_view prefix) {
        for (const std::string & line : log_of(page)) {
            if (line.starts_with(prefix)) { return line; }
        }
        return std::string{"<not logged>"};
    };
    const std::string registered = said("registered=");
    check(registered == "registered=function",
          "the WebGL renderer is still a constructible function: " + registered);
    // THE ENGINE'S OWN GUARANTEE, tested directly rather than through p5: a
    // canvas hands out a real webgl context, and answers NULL for `webgl2`
    // because this is a WebGL 1 implementation (docs/history/webgl.md). Null is
    // what an unsupported context id returns, and what feature detection - p5's
    // included - is built on; this asserted a throw until that was measured.
    (void)page.run_script(R"(
        var one = document.createElement('canvas').getContext('webgl');
        console.log('direct=' + (one !== null && typeof one.drawArrays === 'function'));
        console.log('two=' + (document.createElement('canvas').getContext('webgl2') !== null));
    )");
    check(said("direct=") == "direct=true", "a webgl context exists: " + said("direct="));
    // WAS `=== null`; `webgl2` returns a context since 2026-08-02 - see
    // docs/history/webgl2.md stage 4. Replaced rather than removed, so the shape
    // of the answer is still pinned.
    check(said("two=") == "two=true", "and webgl2 hands back a context: " + said("two="));

    // WHAT p5 DOES WITH IT IS MEASURED BY THE p5 PROBE, not asserted here.
    // createCanvas(w, h, WEBGL) now reaches p5's RendererGL; it used to land on
    // Renderer2D, and the two engine bugs behind that - a throwing `webgl2` and
    // a missing `Float32Array.from` - were both invisible from this file.
    const std::string asking = said("asking=");
    check(asking != "<not logged>", "the sketch reported what happened: " + asking);
}

// `location`'s parts, and `document.cookie`.
//
// Neither is exotic and both are read WITHOUT a guard: the idiom is
// `location.search.substring(1)` and `document.cookie.split(';')`, so an absent
// one is not a missing feature but a TypeError on the first line of whatever
// library reached for it.
void test_location_parts_and_cookies() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body><script>
        console.log('parts=' + [typeof location.protocol, typeof location.host,
                                typeof location.hostname, typeof location.port,
                                typeof location.pathname, typeof location.search,
                                typeof location.origin].join(','));
        // Reading gives every pair; writing sets ONE of them, so a page that
        // stores two things has both.
        console.log('empty=[' + document.cookie + ']');
        document.cookie = 'a=1';
        document.cookie = 'b=2; path=/; SameSite=Lax';
        console.log('two=' + document.cookie);
        document.cookie = 'a=9';
        console.log('replaced=' + document.cookie);
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "parts=string,string,string,string,string,string,string",
          "location reports every part of its URL: " + log[0]);
    check(log[1] == "empty=[]", "no cookies is the empty string, not undefined: " + log[1]);
    // The attributes after the first `;` are not part of the value.
    check(log[2] == "two=a=1; b=2", "a write ADDS a cookie rather than replacing them: " + log[2]);
    check(log[3] == "replaced=a=9; b=2", "...and writing the same name replaces it: " + log[3]);
}

// `await` ON A PENDING PROMISE SUSPENDS THE FRAME.
//
// There is one stack and the event loop is above it, so await cannot block: the
// frame is lifted out of the register stack, the caller is handed a promise,
// and the frame goes back when the awaited promise settles. It used to read
// `__value` off a promise that had none - so await on anything genuinely
// asynchronous evaluated to UNDEFINED and ran the rest of the function
// immediately, which is the largest silent wrong answer this engine had left.
//
// Here rather than in vm_basics because it needs an event loop: nothing
// suspends without something to resume it.
void test_await_suspends_and_resumes() {
    browser page{browser_options{200, 200}};
    page.load_html(R"(<html><body><script>
        var log = '';
        function gate() {
          const g = {};
          g.p = new Promise(function (ok, no) { g.go = ok; g.fail = no; });
          return g;
        }

        // Several awaits in one function, with LOCALS that have to survive each
        // suspension - they live in the register window, which is copied out and
        // back.
        const a = gate(), b = gate();
        async function many() {
          let total = 100;
          const first = await a.p;
          total += first;
          const second = await b.p;
          return total + second;
        }
        many().then(function (v) { log += 'many(' + v + ');'; });

        // A rejection across a suspension reaches a catch INSIDE the function.
        const c = gate();
        async function caught() {
          try { await c.p; return 'no throw'; } catch (e) { return 'caught:' + e; }
        }
        caught().then(function (v) { log += v + ';'; });

        // ...and an uncaught one rejects the function's own promise rather than
        // ending the run.
        const d = gate();
        async function uncaught() { await d.p; return 'unreached'; }
        uncaught().then(function () { log += 'wrong;'; },
                       function (e) { log += 'rejected:' + e + ';'; });

        // An async function awaiting another that suspends - the outer one
        // suspends too, and both come back in order.
        const e = gate();
        async function inner() { return await e.p; }
        async function outer() { return 'outer(' + (await inner()) + ')'; }
        outer().then(function (v) { log += v + ';'; });

        // Nothing has run past its first await yet: that is the point.
        console.log('atLoad=[' + log + ']');
        setTimeout(function () { a.go(1); b.go(2); c.fail('boom'); d.fail('bang'); e.go(9); }, 0);
        function report() { console.log('after=[' + log + ']'); }
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    // Enough ticks that the timer fires and every resumption drains. Each await
    // costs a turn, and the nested pair costs two.
    for (int frame = 0; frame < 12; ++frame) { page.tick(16); }
    (void)page.run_script("report();");

    const auto & log = log_of(page);
    check(log[0] == "atLoad=[]", "no function ran past its first await at load: " + log[0]);
    const std::string & after = log.back();
    // 100 + 1 + 2: the locals were still there after two suspensions.
    check(after.find("many(103);") != std::string::npos,
          "locals survive several suspensions: " + after);
    check(after.find("caught:boom;") != std::string::npos,
          "a rejection throws AT the await, so try/catch spans it: " + after);
    check(after.find("rejected:bang;") != std::string::npos,
          "an uncaught rejection rejects the function's own promise: " + after);
    check(after.find("outer(9);") != std::string::npos,
          "an async function awaiting one that suspends suspends too: " + after);
}

// A `value` CAPTURED BY A NATIVE LAMBDA IS NOT A ROOT.
//
// `new Promise(fn)` handed its executor a resolve function that held the promise
// in a C++ lambda capture. The collector walks a native's properties, not its
// captures, so a promise nothing else referenced was freed while the page was
// still holding the resolve that would settle it - and settling a recycled cell
// does nothing, silently, because settle() checks is_object() first.
//
// The symptom was an async function that could suspend EXACTLY ONCE. The first
// await's promise was still in a live frame's registers; a promise created
// during the resumption existed only in those captures and in its own handler
// list - a cycle with no root - so the second await never came back.
//
// test_await_suspends_and_resumes has two awaits and passed throughout: its
// gates are top-level consts, so they were rooted. Only a promise created DURING
// the resumption shows it, which is what every real loader does - `await
// fetch(u)` and then `await response.bytes()`.
void test_a_promise_made_during_a_resumption_survives() {
    browser page{browser_options{200, 200}};
    page.load_html(R"(<html><body><script>
        var log = '';
        // No reference kept anywhere: the promise exists only inside the
        // executor's resolve and, once awaited, in its own handler list.
        function later(v) {
          return new Promise(function (ok) { setTimeout(function () { ok(v); }, 0); });
        }
        async function three() {
          const a = await later(1);
          // Garbage between the suspensions, so a collection actually happens
          // while the next promise is the only thing holding itself up.
          for (var i = 0; i < 20000; i = i + 1) { var junk = { n: i }; }
          const b = await later(2);
          for (var j = 0; j < 20000; j = j + 1) { var more = { n: j }; }
          const c = await later(3);
          return a + b + c;
        }
        three().then(function (v) { log += 'sum=' + v + ';'; },
                     function (e) { log += 'rejected:' + e + ';'; });
        function report() { console.log('log=[' + log + ']'); }
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    // Three suspensions, each waiting on a timer registered by the previous
    // resumption - so this needs a turn per await plus the drains between them.
    for (int frame = 0; frame < 20; ++frame) { page.tick(16); }
    (void)page.run_script("report();");
    check(log_of(page).back() == "log=[sum=6;]",
          "all three suspensions resumed: " + log_of(page).back());
}

// A SUSPENDED FRAME IS A GC ROOT. Its register window is copied out of the
// register stack, which is what the collector normally walks - so without
// tracing it, everything a waiting function was holding is freed and comes back
// as garbage. The churn here is what makes the test mean something: a
// collection has to actually happen while the frame is away.
void test_a_suspended_frame_survives_collection() {
    browser page{browser_options{200, 200}};
    page.load_html(R"(<html><body><script>
        var log = '';
        const g = {};
        const p = new Promise(function (ok) { g.go = ok; });
        async function holds() {
          const mine = { tag: 'kept', list: [1, 2, 3], deep: { s: 'still here' } };
          const v = await p;
          return mine.tag + '/' + mine.list.join('') + '/' + mine.deep.s + '/' + v;
        }
        holds().then(function (r) { log += r; });
        setTimeout(function () {
          for (let i = 0; i < 60000; i++) { const junk = { i: i, s: 'x' + i, a: [i, i, i] }; }
          g.go('resumed');
        }, 0);
        function report() { console.log(log); }
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    for (int frame = 0; frame < 12; ++frame) { page.tick(16); }
    (void)page.run_script("report();");
    check(log_of(page).back() == "kept/123/still here/resumed",
          "a suspended frame's locals survive a collection: " + log_of(page).back());
}

// `querySelector` ON AN ELEMENT, searching its own subtree rather than the
// document. The document had both and an element had neither, so "find
// something inside this" - what a library does with a container it owns -
// threw. p5.js's describe() builds an offscreen tree and queries it.
void test_element_query_selector() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body>
        <div id=box><span class=hit>a</span><span class=hit>b</span></div>
        <span class=hit>outside</span>
        <script>
          const box = document.getElementById('box');
          console.log('one=' + box.querySelector('.hit').getText());
          console.log('all=' + box.querySelectorAll('.hit').length);
          // The document's own search still sees everything, including the one
          // outside the box - that is what makes the scoping meaningful.
          console.log('doc=' + document.querySelectorAll('.hit').length);
          console.log('miss=' + (box.querySelector('.nothing') === null));
        </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "one=a", "an element finds the first match inside itself: " + log[0]);
    check(log[1] == "all=2", "...and only the ones inside it: " + log[1]);
    check(log[2] == "doc=3", "while the document still sees them all: " + log[2]);
    check(log[3] == "miss=true", "no match is null: " + log[3]);
}

// `once` AND `capture` ON A LISTENER.
//
// Both were accepted and ignored. `once` meant a listener a page registered to
// run exactly once ran on every event - a one-shot "has the user interacted
// yet" handler kept firing. `capture` meant a listener that asked to see an
// event BEFORE its target saw it ran after instead, which is the entire reason
// to pass the flag.
void test_listener_options() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body style="margin:0">
        <div id=outer style="width:100px;height:100px">
          <div id=inner style="width:50px;height:50px"></div>
        </div>
        <script>
          var log = '';
          const outer = document.getElementById('outer');
          const inner = document.getElementById('inner');
          outer.addEventListener('click', function () { log += 'outer-capture;'; }, { capture: true });
          outer.addEventListener('click', function () { log += 'outer-bubble;'; });
          inner.addEventListener('click', function () { log += 'inner;'; });
          // The old spelling: a bare boolean means capture.
          document.addEventListener('click', function () { log += 'doc-capture;'; }, true);
          var counted = 0;
          inner.addEventListener('click', function () { counted++; }, { once: true });
          function report() { console.log(log + ' counted=' + counted); }
        </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    (void)page.frame();
    for (int i = 0; i < 3; ++i) {
        (void)page.handle(input_event::mouse_down_at(10, 10));
        (void)page.handle(input_event::mouse_up_at(10, 10));
    }
    (void)page.run_script("report();");
    const std::string & line = log_of(page).back();
    // Down to the target first, then back up: the document's capturing
    // listener is outermost and runs before the outer element's, which runs
    // before the target's own.
    const std::size_t doc = line.find("doc-capture;");
    const std::size_t cap = line.find("outer-capture;");
    const std::size_t at = line.find("inner;");
    const std::size_t bubble = line.find("outer-bubble;");
    check(doc != std::string::npos && cap != std::string::npos && at != std::string::npos &&
              bubble != std::string::npos,
          "every phase fired: " + line);
    check(doc < cap && cap < at && at < bubble,
          "capture runs outermost-first and before the target, bubble after: " + line);
    // Three clicks, one call.
    check(line.find("counted=1") != std::string::npos,
          "a `once` listener fires exactly once: " + line);
}

// `innerHTML` PARSES, and `textContent` does not.
//
// innerHTML was a plain property on the wrapper: assigning markup stored a
// string, built no nodes, rendered nothing and reported nothing - and reading
// it back gave whatever the page last wrote rather than what the DOM holds. It
// goes through the same WHATWG tokenizer and tree builder the page did, because
// the alternative is a second and worse parser for the commonest way a page
// builds content.
void test_inner_html() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><head><style>.k { background-color: #008000 }</style></head>
        <body><div id=d></div><script>
        const d = document.getElementById('d');
        d.innerHTML = '<b id="ib" class="k">hi</b><span>there</span>';
        console.log('nodes=' + d.children.length + ',' + (document.getElementById('ib') !== null));
        console.log('read=' + d.innerHTML);
        console.log('text=' + d.textContent);
        // Replacing wipes what was there rather than appending.
        d.innerHTML = '<i>only</i>';
        console.log('replaced=' + d.children.length + ',' + d.textContent);
        // textContent is TEXT, never markup - that is why a page reaches for it.
        d.textContent = '<not markup>';
        console.log('asText=' + d.children.length + ',' + d.textContent);
        </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "nodes=2,true", "the markup became real nodes, findable by id: " + log[0]);
    // Serialised from the tree rather than echoed back, so a node appended
    // afterwards would show up too.
    check(log[1] == R"(read=<b id="ib" class="k">hi</b><span>there</span>)",
          "reading serialises the children: " + log[1]);
    check(log[2] == "text=hithere", "textContent is every text node under it: " + log[2]);
    check(log[3] == "replaced=1,only", "assigning again replaces: " + log[3]);
    check(log[4] == "asText=0,<not markup>", "textContent stores text, not markup: " + log[4]);
    // And the parsed nodes are in the CASCADE, which is what says they are
    // really in the document rather than in a side table.
    check(page.frame().has_value(), "the page renders");

    // A <script>'s textContent is its SOURCE, unmangled. p5's error system
    // reads it back and parses it, so anything lost here becomes a syntax
    // error in a file the page never wrote.
    browser scripts{browser_options{200, 200}};
    scripts.load_html(R"(<html><body>
<script id=t>
var a = 1;
if (a < 2 && a > 0) { a++; }
</script>
<script>
console.log('src=' + document.getElementById('t').textContent.split('\n').join('|'));
</script></body></html>)");
    check(scripts.script_error().empty(), "the script ran: " + scripts.script_error());
    check(!log_of(scripts).empty(), "the script reported");
    if (!log_of(scripts).empty()) {
        check(log_of(scripts).back() == "src=|var a = 1;|if (a < 2 && a > 0) { a++; }|",
              "a script's text survives the round trip: " + log_of(scripts).back());
    }
}

// `clip()` CONFINES what is drawn after it, and `restore()` is the only way
// back. The region is an INTERSECTION of paths, so two clips leave what they
// have in common - which is what makes nesting them work.
// A CONTROL'S `value` IS LIVE, and a canvas is an image source.
//
// Both were the same shape of bug: a property written on whatever tick a sync
// next ran, read by a page in the statement that created it.
void test_control_value_is_live() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body><input id=static value="s"><script>
        console.log('static=' + document.getElementById('static').value);
        // The order p5's createInput uses: create, set the ATTRIBUTE, append.
        // The value has to be visible immediately, not after the next refresh.
        const made = document.createElement('input');
        made.setAttribute('type', 'text');
        made.setAttribute('value', 'from-attribute');
        document.body.appendChild(made);
        console.log('created=' + made.value);
        // An assignment wins over the attribute from then on - including an
        // assignment of the empty string, which is how a page clears a field.
        made.value = 'assigned';
        console.log('assigned=' + made.value);
        made.value = '';
        console.log('cleared=[' + made.value + ']');
        const box = document.createElement('input');
        box.setAttribute('type', 'checkbox');
        document.body.appendChild(box);
        box.checked = true;
        console.log('checked=' + box.checked);
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "static=s", "a control in the markup reads its attribute: " + log[0]);
    check(log[1] == "created=from-attribute",
          "a control created by script reads it immediately: " + log[1]);
    check(log[2] == "assigned=assigned", "an assignment is read back: " + log[2]);
    // The attribute must NOT come back and undo it.
    check(log[3] == "cleared=[]", "assigning the empty string clears the field: " + log[3]);
    check(log[4] == "checked=true", "checked round-trips: " + log[4]);
}

// A CANVAS IS AN IMAGE SOURCE. `drawImage(otherCanvas, ...)` is how a page
// composites one surface onto another - and it is what p5's `image(g, ...)` does
// with a createGraphics, so an offscreen buffer drew nothing at all.
void test_canvas_as_image_source() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body>
        <canvas id=src width=20 height=20></canvas>
        <canvas id=dst width=40 height=40></canvas><script>
        const a = document.getElementById('src').getContext('2d');
        a.fillStyle = '#ff0000'; a.fillRect(0, 0, 20, 20);
        const b = document.getElementById('dst').getContext('2d');
        b.fillStyle = '#000000'; b.fillRect(0, 0, 40, 40);
        b.drawImage(document.getElementById('src'), 10, 10);
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    if (const auto pixels = page.canvases().pixels_of(find_id(page, "dst"))) {
        const auto at = [&](int x, int y) { return color{pixels->at(x, y)}; };
        check(at(15, 15) == color::rgba(255, 0, 0), "the source canvas was drawn");
        check(at(5, 5) == color::rgba(0, 0, 0), "and only where it was placed");
    } else {
        check(false, "the destination canvas has pixels");
    }
}

// `insertAdjacentHTML` - a fragment parse at one of four places relative to the
// element. Same parser and same copy as innerHTML; only where the nodes land
// differs.
void test_insert_adjacent_html() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body><div id=box><span id=mid>mid</span></div><script>
        const mid = document.getElementById('mid');
        mid.insertAdjacentHTML('beforebegin', '<i id=before>b</i>');
        mid.insertAdjacentHTML('afterend', '<i id=after>a</i>');
        mid.insertAdjacentHTML('afterbegin', '<b id=in-first>1</b>');
        mid.insertAdjacentHTML('beforeend', '<b id=in-last>2</b>');
        const box = document.getElementById('box');
        let order = '';
        for (const kid of box.children) { order += kid.id + ' '; }
        let inner = '';
        for (const kid of mid.children) { inner += kid.id + ' '; }
        console.log('outer=' + order.trim());
        console.log('inner=' + inner.trim());
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "outer=before mid after", "beforebegin and afterend place siblings: " + log[0]);
    check(log[1] == "inner=in-first in-last", "afterbegin and beforeend place children: " + log[1]);
}

void test_clip() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body><canvas id=c width=100 height=100></canvas><script>
        const ctx = document.getElementById('c').getContext('2d');
        ctx.fillStyle = '#ffffff'; ctx.fillRect(0, 0, 100, 100);
        ctx.save();
        ctx.beginPath(); ctx.rect(20, 20, 40, 40); ctx.clip();
        // Asks for the whole canvas; gets the clip.
        ctx.fillStyle = '#ff0000'; ctx.fillRect(0, 0, 100, 100);
        // A second clip intersects rather than replacing.
        ctx.beginPath(); ctx.rect(40, 20, 40, 40); ctx.clip();
        ctx.fillStyle = '#00ff00'; ctx.fillRect(0, 0, 100, 100);
        ctx.restore();
        // Outside every clip again: restore is the only way back.
        ctx.fillStyle = '#0000ff'; ctx.fillRect(70, 70, 20, 20);
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    const auto pixels = page.canvases().pixels_of(find_id(page, "c"));
    check(pixels != nullptr, "the canvas has pixels");
    if (pixels == nullptr) { return; }
    const auto at = [&](int x, int y) { return color{pixels->at(x, y)}; };
    check(at(5, 5) == color::rgba(255, 255, 255), "a fill of the whole canvas is confined");
    check(at(25, 30) == color::rgba(255, 0, 0), "...to the clip region");
    // The intersection of x[20,60) and x[40,80) is x[40,60).
    check(at(50, 30) == color::rgba(0, 255, 0), "a second clip intersects the first");
    check(at(25, 30) == color::rgba(255, 0, 0), "and does not extend it leftwards");
    check(at(70, 30) == color::rgba(255, 255, 255), "nor rightwards past the first");
    check(at(75, 75) == color::rgba(0, 0, 255), "restore() puts the old region back");

    // `addPath(other, matrix)` APPLIES the matrix. The verbs are copied with
    // their coordinates already transformed, which is what makes a path built
    // once reusable at several places.
    browser moved{browser_options{300, 200}};
    moved.load_html(R"(<html><body><canvas id=c width=100 height=100></canvas><script>
        const ctx = document.getElementById('c').getContext('2d');
        ctx.fillStyle = '#ffffff'; ctx.fillRect(0, 0, 100, 100);
        const unit = new Path2D();
        unit.rect(0, 0, 10, 10);
        const placed = new Path2D();
        placed.addPath(unit);                                  // no matrix: as written
        placed.addPath(unit, { a: 1, b: 0, c: 0, d: 1, e: 50, f: 0 });   // translated
        placed.addPath(unit, { a: 2, b: 0, c: 0, d: 2, e: 0, f: 50 });   // scaled and moved
        ctx.fillStyle = '#ff0000';
        ctx.fill(placed);
    </script></body></html>)");
    check(moved.script_error().empty(), "the addPath script ran: " + moved.script_error());
    if (const auto out = moved.canvases().pixels_of(find_id(moved, "c"))) {
        const auto hit = [&](int x, int y) {
            return color{out->at(x, y)} == color::rgba(255, 0, 0);
        };
        check(hit(5, 5), "the untransformed copy is where it was written");
        check(hit(55, 5), "a translated copy moves");
        check(!hit(30, 5), "and does not stay behind");
        // Scaled 2x from the origin and moved down 50: a 20x20 square at y 50.
        check(hit(15, 65), "a scaled copy takes the matrix's scale");
        check(!hit(25, 65), "and stops where the scaled edge is");
    } else {
        check(false, "the second canvas has pixels");
    }
}

// `fetch` IS ASYNCHRONOUS.
//
// It used to do the work and hand back an already-settled promise - the only
// option while `await` could not suspend, since a pending one would have
// evaluated to undefined and the rest of the function would have run with it.
// `await` suspends now, so a fetch can be what it is: work that finishes on a
// later turn.
//
// That is not pedantry. `await fetch(url)` used to return before any other timer
// or listener could run, so nothing a page does to stay responsive while loading
// was observable - and an AbortController had nothing to abort, because the
// request was over before the object existed.
void test_fetch_is_async() {
    browser page{browser_options{300, 200}};
    page.assets().add("data.json", bytes_of(R"({"name":"ctbrowser","n":3})"));
    page.load_html(R"(<html><body><script>
        var log = '';
        fetch('data.json').then(function (r) {
          log += 'ok=' + r.ok + ',' + r.status + ';';
          log += 'ct=' + r.headers.get('content-type') + ';';
          log += 'absent=' + r.headers.get('x-nothing') + ';';
          return r.json();
        }).then(function (j) { log += 'name=' + j.name + ',' + j.n + ';'; });
        // The request is OUTSTANDING here: nothing it produces may have
        // happened yet, which is the whole difference.
        log += 'sync;';
        function report() { console.log(log); }
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    (void)page.run_script("report();");
    check(log_of(page).back() == "sync;",
          "nothing resolved before the turn ended: " + log_of(page).back());

    for (int frame = 0; frame < 4; ++frame) { page.tick(16); }
    (void)page.run_script("report();");
    const std::string & after = log_of(page).back();
    check(after.find("ok=true,200;") != std::string::npos, "the response arrived: " + after);
    // `headers` is an OBJECT with get(), not a content-type string - a page
    // doing the ordinary thing used to throw on it.
    check(after.find("absent=null;") != std::string::npos,
          "an unknown header is null rather than an error: " + after);
    check(after.find("name=ctbrowser,3;") != std::string::npos, "json() parsed it: " + after);
}

// `await fetch(...)` across a real suspension, and the bytes three ways.
void test_fetch_await_and_bytes() {
    browser page{browser_options{300, 200}};
    page.assets().add("blob.bin",
                      std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{255}});
    page.load_html(R"(<html><body><script>
        var log = '';
        (async function () {
          const r = await fetch('blob.bin');
          const buf = await r.arrayBuffer();
          // A view over the WHOLE buffer shares its storage, so this is the
          // response's bytes rather than a copy of them.
          const view = new Uint8Array(buf);
          log += 'len=' + buf.byteLength + ',' + view.length + ';';
          log += 'bytes=' + view[0] + ',' + view[1] + ',' + view[2] + ';';
          const again = await (await fetch('blob.bin')).bytes();
          log += 'bytes2=' + again.length + ';';
          const blob = await (await fetch('blob.bin')).blob();
          log += 'blob=' + blob.size + ';';
        })();
        log += 'sync;';
        function report() { console.log(log); }
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    // Several awaits, each one a turn: the loop has to run enough times.
    for (int frame = 0; frame < 20; ++frame) { page.tick(16); }
    (void)page.run_script("report();");
    const std::string & out = log_of(page).back();
    check(out.find("sync;") == 0, "the async function suspended at its first await: " + out);
    check(out.find("len=3,3;") != std::string::npos, "arrayBuffer and its view agree: " + out);
    check(out.find("bytes=1,2,255;") != std::string::npos, "the bytes are the file's: " + out);
    check(out.find("bytes2=3;") != std::string::npos, "bytes() too: " + out);
    check(out.find("blob=3;") != std::string::npos, "and blob(): " + out);
}

// An ABORT now has something to abort, because the request is outstanding for at
// least one turn.
void test_fetch_abort() {
    browser page{browser_options{300, 200}};
    page.assets().add("data.json", bytes_of("{}"));
    page.load_html(R"(<html><body><script>
        var log = '';
        const control = new AbortController();
        fetch('data.json', { signal: control.signal })
          .then(function () { log += 'resolved;'; },
                function (e) { log += 'rejected=' + e.name + ';'; });
        control.abort();
        function report() { console.log(log); }
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    for (int frame = 0; frame < 4; ++frame) { page.tick(16); }
    (void)page.run_script("report();");
    check(log_of(page).back() == "rejected=AbortError;",
          "an aborted fetch rejects: " + log_of(page).back());
}

// --- the document API -----------------------------------------------------

void test_script_mutates_what_is_drawn() {
    browser page{browser_options{400, 200}};
    page.load_html(
        "<html><body><div id=a>original</div>"
        "<script>document.getElementById('a').setText('replaced');</script></body></html>");
    check(page.frame().has_value(), "the page renders");
    check(page.script_error().empty(), "the script ran without error");
    // The whole point: the mutation reached the pixels, not just the DOM.
    check(rendered_text(page).find("replaced") != std::string::npos, "setText changed the page");
    check(rendered_text(page).find("original") == std::string::npos, "and removed the old text");
}

void test_attributes_and_classes() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><head><style>
    .hot { background-color: #ff0000 }
    </style></head><body><div id=a>x</div><script>
    var el = document.getElementById('a');
    el.setAttribute('data-role', 'banner');
    el.addClass('hot');
    console.log('role=' + el.getAttribute('data-role'));
    console.log('hot=' + el.hasClass('hot'));
    console.log('cold=' + el.hasClass('cold'));
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(page.script_error().empty(), "the script ran without error");

    const auto & log = log_of(page);
    check(log.size() == 3, "three console lines");
    if (log.size() == 3) {
        check(log[0] == "role=banner", "setAttribute then getAttribute round-trips");
        check(log[1] == "hot=true", "hasClass sees the class it added");
        check(log[2] == "cold=false", "and does not see one it did not");
    }
    // And the class actually restyled the element, which is the part that
    // matters: adding a class that changes nothing on screen is not a binding
    // that works.
    check(count_fill(page, color::rgba(255, 0, 0)) == 1, "addClass restyled the element");
}

// --- the document's own properties ------------------------------------------

// `document.title` and `getElementsByTagName` simply were not there. A page
// asking for either got `undefined` and, in the second case, died on calling
// it - which is how the comparison rig found them.
void test_document_title_and_tag_lookup() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><head><title>a page</title></head><body>
    <p>one</p><p>two</p><div><p>three</p></div>
    <script>
    console.log('title=' + document.title);
    console.log('paragraphs=' + document.getElementsByTagName('p').length);
    console.log('second=' + document.getElementsByTagName('p')[1].getText());
    console.log('star=' + (document.getElementsByTagName('*').length > 5));
    console.log('none=' + document.getElementsByTagName('blink').length);
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(page.script_error().empty(), "the script ran without error");

    const auto & log = log_of(page);
    check(log.size() == 5, "five console lines");
    if (log.size() != 5) { return; }
    check(log[0] == "title=a page", "document.title is the <title>'s text");
    // Nested ones too: the walk is the whole tree, not the body's children.
    check(log[1] == "paragraphs=3", "getElementsByTagName finds every match");
    check(log[2] == "second=two", "in document order, and they are real elements");
    check(log[3] == "star=true", "'*' matches every element");
    check(log[4] == "none=0", "and a tag nothing uses is an empty list");

    // ...and the title is LIVE, not a snapshot taken when the document object
    // was built. Rewriting the <title> has to be visible, which is the same bug
    // class location.href had: set once at install and wrong ever after.
    (void)page.run_script("document.getElementsByTagName('title')[0].setText('renamed');");
    check(page.frame().has_value(), "the page redraws");
    (void)page.run_script("console.log('after=' + document.title);");
    check(log.size() == 6 && log[5] == "after=renamed", "document.title follows the element");
}

// `document.activeElement` is the script-visible mirror of browser::focused().
// The bindings could SET focus - element.focus() - but nothing came back, so
// the property could not exist at all.
void test_document_active_element_follows_focus() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body>
    <input id=a><input id=b>
    <script>
    function active() { return document.activeElement ? document.activeElement.id : 'none'; }
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");

    const auto ask = [&page](const char * label) {
        (void)page.run_script(std::string{"console.log('"} + label + "=' + active())");
    };
    ask("start");

    // Focus by SCRIPT...
    (void)page.run_script("document.getElementById('b').focus();");
    ask("scripted");
    // ...by the KEYBOARD...
    (void)page.handle(input_event::key_press("Tab"));
    ask("tabbed");
    // ...and blurred.
    (void)page.run_script("document.getElementById('b').blur();");
    ask("blurred");

    const auto & log = log_of(page);
    check(log.size() == 4, "four answers");
    if (log.size() != 4) { return; }
    check(log[0] == "start=none", "nothing is focused to begin with");
    check(log[1] == "scripted=b", "element.focus() is visible as activeElement");
    check(log[2] == "tabbed=a", "and Tab moves it, wrapping past the last control");
    check(log[3] == "blurred=none", "and blur clears it");
}

// A page that errors ONCE used to report that error for ever: run_script only
// ever assigned script_error_, never cleared it, so every later success still
// carried the old message. The rig hit this immediately - a perfectly good eval
// came back with a failure from three calls earlier.
void test_a_script_error_does_not_outlive_the_script() {
    browser page{browser_options{400, 300}};
    page.load_html("<body><p id=p>x</p></body>");
    check(page.frame().has_value(), "the page renders");
    check(page.script_error().empty(), "no error to begin with");

    check(!page.run_script("nosuchfunction();"), "a broken script fails");
    check(!page.script_error().empty(), "and says so");

    check(page.run_script("console.log('fine');"), "a good script runs");
    check(page.script_error().empty(), "and the old error is gone");
}

void test_removeclass_undoes_it() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><head><style>.hot { background-color: #ff0000 }</style></head>
    <body><div id=a class=hot>x</div><script>
    document.getElementById('a').removeClass('hot');
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(count_fill(page, color::rgba(255, 0, 0)) == 0, "removeClass unstyled the element");
}

void test_create_and_append() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><div id=host></div><script>
    var el = document.createElement('p');
    el.setText('made by script');
    document.getElementById('host').appendChild(el);
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(page.script_error().empty(), "the script ran without error");
    check(rendered_text(page).find("made by script") != std::string::npos,
          "a created element appears once appended");
}

void test_remove_child() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><div id=host><p id=gone>remove me</p></div><script>
    var host = document.getElementById('host');
    host.removeChild(document.getElementById('gone'));
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(rendered_text(page).find("remove me") == std::string::npos,
          "a removed element stops being drawn");
}

void test_a_stale_handle_is_inert() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><div id=host><p id=doomed>text</p></div><script>
    var doomed = document.getElementById('doomed');
    document.getElementById('host').removeChild(doomed);
    doomed.setText('written to a dead node');
    doomed.addClass('whatever');
    console.log('survived');
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    // the previous engine held a raw node* here. The handle turns a use-after-free into a
    // lookup that finds nothing, so the writes go nowhere and the page is
    // unharmed - which is the entire argument for handles.
    check(page.script_error().empty(), "writing through a stale handle does not fail the script");
    check(log_of(page).size() == 1 && log_of(page)[0] == "survived",
          "and execution continues past it");
    check(rendered_text(page).find("written to a dead node") == std::string::npos,
          "nothing was written to the removed node");
}

void test_layout_is_visible_to_script() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><head><style>#a { width: 123px; height: 45px }</style></head>
    <body><div id=a>x</div><script>
    var el = document.getElementById('a');
    console.log('w=' + el.offsetWidth + ' h=' + el.offsetHeight);
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");

    // The script ran BEFORE the first layout, so it must report 0 rather than
    // a guess. Reporting a plausible-looking wrong number is worse: a page that
    // sizes itself from it would be silently wrong.
    check(log_of(page).size() == 1, "one console line");
    if (!log_of(page).empty()) {
        check(log_of(page)[0] == "w=0 h=0", "before the first layout, geometry reads as zero");
    }

    // After a layout, a freshly-obtained wrapper sees real numbers.
    auto & bindings = page.bindings();
    (void)bindings;
    page.load_html(R"(<html><head><style>#a { width: 123px; height: 45px }</style></head>
    <body><div id=a>x</div><script>
    function report() { var el = document.getElementById('a');
      console.log('w=' + el.offsetWidth + ' h=' + el.offsetHeight); }
    setTimeout(report, 0);
    </script></body></html>)");
    check(page.frame().has_value(), "the second page renders");
    check(page.tick(1) == 1, "the timer ran after layout");
    check(page.frame().has_value(), "and the frame after it renders");
    check(!log_of(page).empty() && log_of(page).back() == "w=123 h=45",
          "after layout, offsetWidth/Height report the real box");
}

// --- events ---------------------------------------------------------------

// --- alert, location, and <a href> -------------------------------------
//
// The last three things the previous engine's script surface had and this engine's did not. MDN's
// breakout calls alert() and document.location.reload() the moment the game
// ends, so a page could win and then die on an undefined identifier.

void test_alert_is_recorded() {
    browser page{browser_options{200, 100}};
    page.load_html("<body><script>alert('hello'); alert('again');</script></body>");
    check(page.script_error().empty(), "the script ran");
    check(page.alerts().size() == 2, "both alerts were recorded");
    if (page.alerts().size() == 2) {
        check(page.alerts()[0] == "hello" && page.alerts()[1] == "again", "in order, with text");
    }
}

void test_alert_reaches_the_hook() {
    browser page{browser_options{200, 100}};
    std::vector<std::string> seen;
    page.set_alert_hook([&seen](const std::string & message) { seen.push_back(message); });
    page.load_html("<body><script>alert('modal');</script></body>");
    check(seen.size() == 1 && !seen.empty() && seen[0] == "modal", "the hook saw it");
}

void test_location_reload_reruns_the_page() {
    browser page{browser_options{200, 100}};
    // The script appends a paragraph, so a reload is visible as a page that
    // has ONE again rather than two - a reload re-parses the source, it does
    // not re-run the script over the mutated document.
    page.load_html(R"(<body><div id=host></div><script>
    var p = document.createElement('p');
    p.setAttribute('id', 'added');
    document.getElementById('host').appendChild(p);
    </script></body>)");
    check(page.script_error().empty(), "the script ran");

    // A page that reloads itself from a timer: the request is recorded and
    // drained BETWEEN callbacks, because a reload inside one would destroy the
    // context the callback is running in.
    page.load_html(R"(<body><p>page</p><script>
    setTimeout(function () { document.location.reload(); }, 5);
    var runs = 0;
    </script></body>)");
    check(page.script_error().empty(), "the reloading page ran");
    (void)page.frame();
    check(page.tick(10) == 1, "the timer fired");
    // After the reload the page is fresh: the same timer is armed again.
    check(page.script_error().empty(), "the reloaded page ran too");
    check(page.tick(10) == 1, "and its timer fired, so the script really re-ran");
}

void test_window_and_document_share_one_location() {
    browser page{browser_options{200, 100}};
    page.load_html(R"(<body><script>
    console.log(String(document.location === window.location));
    console.log(String(location === window.location));
    </script></body>)");
    check(log_of(page).size() == 2, "both comparisons logged");
    if (log_of(page).size() == 2) {
        check(log_of(page)[0] == "true" && log_of(page)[1] == "true",
              "document.location, window.location and location are ONE object");
    }
}

void test_a_link_is_handed_to_the_embedder() {
    browser page{browser_options{400, 200}};
    std::vector<std::string> visited;
    page.set_navigate_hook([&visited](const std::string & url) { visited.push_back(url); });
    page.load_html("<body><a href='https://example.com/x'>a link</a></body>");
    check(page.frame().has_value(), "the page renders");

    // Clicked on the link's TEXT, which is a different node from the <a>.
    (void)page.handle(input_event::mouse_down_at(8, 8));
    (void)page.handle(input_event::mouse_up_at(8, 8));
    check(visited.size() == 1, "the link was followed");
    if (!visited.empty()) { check(visited[0] == "https://example.com/x", "with its href"); }
    check(page.location_href() == "https://example.com/x", "and location.href records it");
}

void test_a_fragment_scrolls_instead_of_navigating() {
    browser page{browser_options{300, 200}};
    std::vector<std::string> visited;
    page.set_navigate_hook([&visited](const std::string & url) { visited.push_back(url); });
    page.load_html(R"(<body><a href='#far'>jump</a>
    <div style='height:1200px'>tall</div>
    <p id=far>the target</p></body>)");
    check(page.frame().has_value(), "the page renders");
    check(page.scroll_y() == 0, "starts at the top");

    (void)page.handle(input_event::mouse_down_at(8, 8));
    (void)page.handle(input_event::mouse_up_at(8, 8));
    check(visited.empty(), "a fragment is NOT handed to the embedder");
    check(page.scroll_y() > 1000, "it scrolled to the target instead");
    check(page.location_hash() == "#far", "and location.hash says where");
}

void test_a_page_can_read_where_a_link_went() {
    browser page{browser_options{300, 200}};
    page.set_navigate_hook([](const std::string &) {});
    page.load_html(R"(<body><a href='/first'>go</a><script>
    document.addEventListener('click', function () { console.log(location.href); });
    </script></body>)");
    check(page.frame().has_value(), "the page renders");

    // A listener runs BEFORE the default action, so the first click logs the
    // href from before it - empty - and the second logs the first link's.
    // That second value is the point: `href` was written once when the object
    // was built, so a page could never see a link it had already followed.
    (void)page.handle(input_event::mouse_down_at(12, 12));
    (void)page.handle(input_event::mouse_up_at(12, 12));
    check(page.location_href() == "/first", "the browser recorded the href");
    (void)page.handle(input_event::mouse_down_at(12, 12));
    (void)page.handle(input_event::mouse_up_at(12, 12));
    check(page.script_error().empty(), "reading location.href from script works");
    check(log_of(page).size() == 2, "the listener fired twice");
    if (log_of(page).size() == 2) {
        check(log_of(page)[1] == "/first", "and location.href is LIVE, not a page-load snapshot");
    }
}

// --- garbage collection ---------------------------------------------------
//
// Collection never ran. Not "ran rarely" - the VM had no automatic trigger at
// all, so a long-running page accumulated every object it ever made. The
// reason it could not simply be switched on is that the DOM bindings hold
// every listener, every timer callback and every element wrapper in C++
// containers the collector cannot see, so a sweep would have freed a page's
// own listeners while the page was still using them.
void test_collection_keeps_what_the_page_still_uses() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<body><div id=a>click me</div><script>
    var count = 0;
    document.getElementById('a').addEventListener('click', function () {
      count = count + 1;
      console.log('fired ' + count);
    });
    setInterval(function () { console.log('tick'); }, 1000);
    var kept = document.getElementById('a');
    </script></body>)");
    check(page.frame().has_value(), "the page renders");
    check(page.script_error().empty(), "the script ran");

    // Make a lot of garbage, then collect. Without external roots this frees
    // the listener, the interval's callback and the wrapper `kept` refers to.
    check(page.run_script("for (var i = 0; i < 20000; i = i + 1) { var junk = { n: i }; }"),
          "made some garbage");
    const std::size_t freed = page.collect_garbage();
    check(freed > 0, "and collecting freed some of it");

    // The listener still fires.
    (void)page.handle(input_event::mouse_down_at(20, 20));
    (void)page.handle(input_event::mouse_up_at(20, 20));
    check(!log_of(page).empty() && log_of(page).back() == "fired 1",
          "the listener survived the collection");

    // The interval still fires.
    check(page.tick(1100) >= 1, "the interval survived too");
    check(log_of(page).back() == "tick", "and ran its callback");

    // And the wrapper the page is holding is still the live element.
    check(page.run_script("console.log(kept.tagName);"), "reading the kept wrapper works");
    check(log_of(page).back() == "DIV", "it is still the element it was");
}

void test_collection_happens_on_its_own() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<body><p>x</p><script>
    function churn() { for (var i = 0; i < 3000; i = i + 1) { var junk = { n: i }; } }
    setInterval(churn, 16);
    </script></body>)");
    check(page.script_error().empty(), "the script ran");

    // A page that makes garbage on a timer must not grow without bound. Before
    // this, nothing was ever freed for the life of the document.
    std::size_t peak = 0;
    for (int i = 0; i < 60; ++i) {
        (void)page.tick(20);
        peak = std::max(peak, page.live_script_objects());
    }
    const std::size_t settled = page.live_script_objects();
    check(peak > 3000, "the page really did allocate");
    check(settled < peak, "and the heap came back down on its own");
}

// A LINK LEAVES THE PAGE THROUGH run_app, and lands in the SYSTEM BROWSER.
//
// Driven through run_app rather than the browser directly, because the wiring
// is what was wrong: `ctbrowse` set browser::set_navigate_hook itself, which
// REPLACES run_app's hook rather than chaining with it, so every http:// link
// it was handed was silently swallowed. The application gets first refusal
// now and anything it does not claim goes to SDL_OpenURL - the system default
// browser, whatever that is.
void test_a_link_reaches_the_application_through_run_app() {
    std::vector<std::string> asked;
    ctbrowser::app_options options;
    options.width = 300;
    options.height = 200;
    options.max_frames = 3;
    options.real_fonts = false;
    options.network = false;
    // Claimed, so the run does not actually open a browser mid-test. Returning
    // FALSE is what sends it to the system one.
    options.on_navigate = [&asked](const std::string & url) {
        asked.push_back(url);
        return true;
    };
    options.on_ready = [](shell::browser & page) {
        // The click has to happen inside the run: run_app owns the browser.
        (void)page.frame();
        (void)page.handle(input_event::mouse_down_at(12, 12));
        (void)page.handle(input_event::mouse_up_at(12, 12));
    };
    const int code =
        ctbrowser::run_app("<body><a href='https://example.com/here'>a link</a></body>", options);
    check(code == 0, "the application ran");
    check(asked.size() == 1, "the link reached the application");
    if (!asked.empty()) { check(asked[0] == "https://example.com/here", "with its href"); }
}

// A PAGE THAT DIES MUST SAY SO. `run_app` ticked the clock and never looked at
// what came back, so a page whose callbacks throw kept rendering whatever it
// last drew - it looked FROZEN and reported nothing at all. That is how the
// Phaser invaders page hid an undefined method for an afternoon: update() threw
// on every frame and the window showed create()'s output forever.
//
// The message arrives ONCE PER DISTINCT FAULT, not once per frame. A callback
// that faults every frame at 60 Hz would otherwise bury the first and only
// useful line under thousands of copies of itself, which is the same as saying
// nothing.
// `keyCode` AND `which` - deprecated, and universally used.
//
// The engine had `code` and `key`, the modern pair, and stopped there. The
// event looked complete and was unusable to a large amount of real code:
// PHASER'S ENTIRE KEYBOARD SYSTEM MATCHES ON keyCode - `KeyCodes.LEFT` is 37 -
// so every arrow key in a Phaser game did nothing. The listener fired, the
// event arrived, `code` was correct, and no key ever matched. A game that
// renders and cannot be played.
void test_key_events_carry_the_legacy_codes() {
    browser page{browser_options{200, 150}};
    page.load_html(R"(<html><body><script>
        window.__log = [];
        window.addEventListener('keydown', function (e) {
          window.__log.push(e.code + ' ' + e.keyCode + ' ' + e.which + ' ' + e.key);
        });
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");

    // The arrows first, because they are the ones that were broken. The numbers
    // are the well-known ones every browser reports rather than anything
    // derived, which is the whole reason they are worth having.
    for (const char * code : {"ArrowLeft", "ArrowUp", "ArrowRight", "ArrowDown", "Space", "Enter",
                              "Escape", "KeyA", "KeyZ", "Digit0", "Digit9", "F1", "F12"}) {
        (void)page.handle(input_event::key_press(code));
    }

    const std::size_t before = page.bindings().console_output().size();
    (void)page.run_script("console.log('=' + window.__log.join('|'));");
    std::string answer;
    const auto & said = page.bindings().console_output();
    for (std::size_t i = said.size(); i-- > before;) {
        if (said[i].starts_with("=")) {
            answer = said[i].substr(1);
            break;
        }
    }
    const std::string want =
        "ArrowLeft 37 37 ArrowLeft|ArrowUp 38 38 ArrowUp|ArrowRight 39 39 ArrowRight|"
        "ArrowDown 40 40 ArrowDown|Space 32 32  |Enter 13 13 Enter|Escape 27 27 Escape|"
        "KeyA 65 65 a|KeyZ 90 90 z|Digit0 48 48 0|Digit9 57 57 9|F1 112 112 F1|"
        "F12 123 123 F12";
    check(answer == want,
          "keyCode and which are the legacy numbers:\n  got  " + answer + "\n  want " + want);
}

void test_run_app_reports_a_script_error() {
    std::vector<std::string> reported;
    ctbrowser::app_options options;
    options.width = 200;
    options.height = 150;
    // Several frames, so a fault that repeats is given every chance to repeat.
    options.max_frames = 8;
    options.real_fonts = false;
    options.network = false;
    options.on_script_error = [&reported](const std::string & message) {
        reported.push_back(message);
    };
    // The fault is in an ANIMATION FRAME rather than in the page's top level:
    // a top-level throw is reported by load_html and was never the gap. What
    // was missing is the one that happens later, on a turn nobody is watching.
    const int code = ctbrowser::run_app(R"(<body><script>
        function spin() { window.requestAnimationFrame(spin); missingFunction(); }
        window.requestAnimationFrame(spin);
    </script></body>)",
                                        options);
    check(code == 0, "a page that throws does not take the application down");
    check(reported.size() == 1,
          "reported once, not once per frame: " + std::to_string(reported.size()));
    if (!reported.empty()) {
        check(reported[0].find("missingFunction") != std::string::npos,
              "and it names what was missing: " + reported[0]);
        check(reported[0].find("requestAnimationFrame") != std::string::npos,
              "and which kind of callback it was in: " + reported[0]);
    }
}

// AND STAYS SILENT WHEN NOTHING IS WRONG, or the hook is noise a caller learns
// to ignore - which is the same as not having it.
void test_run_app_is_silent_for_a_healthy_page() {
    std::vector<std::string> reported;
    ctbrowser::app_options options;
    options.width = 200;
    options.height = 150;
    options.max_frames = 8;
    options.real_fonts = false;
    options.network = false;
    options.on_script_error = [&reported](const std::string & message) {
        reported.push_back(message);
    };
    const int code = ctbrowser::run_app(R"(<body><script>
        var n = 0;
        function spin() { n++; window.requestAnimationFrame(spin); }
        window.requestAnimationFrame(spin);
    </script></body>)",
                                        options);
    check(code == 0, "the application ran");
    check(reported.empty(), "a healthy page reports nothing");
}

void test_click_dispatch() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><head><style>#a { width: 200px; height: 100px }</style></head>
    <body><div id=a>click me</div><script>
    document.getElementById('a').addEventListener('click', function (e) {
      console.log('clicked ' + e.type);
    });
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(log_of(page).empty(), "nothing fired yet");

    // A click is a press and a release on the same element - which is what
    // makes dragging off a button cancel it.
    (void)page.handle(input_event::mouse_down_at(20, 20));
    (void)page.handle(input_event::mouse_up_at(20, 20));
    check(log_of(page).size() == 1, "the listener fired once");
    if (!log_of(page).empty()) { check(log_of(page)[0] == "clicked click", "with the event type"); }

    // Released somewhere else: no click.
    (void)page.handle(input_event::mouse_down_at(20, 20));
    (void)page.handle(input_event::mouse_up_at(380, 290));
    check(log_of(page).size() == 1, "releasing off the element does not click it");
}

// `el.onclick = fn` - THE OTHER HALF OF THE EVENT API, and it did nothing.
//
// Assigning a handler stored a function on the wrapper that nothing ever looked
// at, so a page written the older way fired no callbacks and reported no
// problem. p5.js needs it on its own load path: loadImage sets img.onload and
// img.onerror and awaits a promise those two settle.
void test_handler_properties() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><head><style>#a { width: 200px; height: 100px }</style></head>
    <body><div id=a>click me</div><script>
    var a = document.getElementById('a');
    a.onclick = function (e) { console.log('handler ' + e.type + ' ' + (this === a)); };
    a.addEventListener('click', function () { console.log('listener'); });
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");

    (void)page.handle(input_event::mouse_down_at(20, 20));
    (void)page.handle(input_event::mouse_up_at(20, 20));
    const auto & log = log_of(page);
    check(log.size() == 2, "both the listener and the handler property fired");
    if (log.size() == 2) {
        // `this` is the element, which is what a handler written this way reads.
        check(log[0] == "listener" && log[1] == "handler click true",
              "the handler runs with the event and the element as `this`");
    }

    // ASSIGNING OVER ONE REPLACES IT - that is the whole difference from
    // addEventListener, and a page that reassigns in a loop relies on it.
    (void)page.run_script("a.onclick = function () { console.log('replaced'); };");
    (void)page.handle(input_event::mouse_down_at(20, 20));
    (void)page.handle(input_event::mouse_up_at(20, 20));
    check(log_of(page).size() == 4 && log_of(page)[3] == "replaced",
          "the second assignment replaced the first rather than adding to it");
    (void)page.run_script("a.onclick = null;");
    (void)page.handle(input_event::mouse_down_at(20, 20));
    (void)page.handle(input_event::mouse_up_at(20, 20));
    check(log_of(page).size() == 5, "and null removes it, leaving the listener");
}

void test_events_bubble_and_can_be_prevented() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><head><style>#outer { width: 300px; height: 200px }</style></head>
    <body><div id=outer><div id=inner>x</div></div><script>
    document.getElementById('inner').addEventListener('click', function (e) {
      console.log('inner'); e.preventDefault();
    });
    document.getElementById('outer').addEventListener('click', function () { console.log('outer'); });
    document.addEventListener('click', function () { console.log('document'); });
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");

    (void)page.handle(input_event::mouse_down_at(10, 10));
    (void)page.handle(input_event::mouse_up_at(10, 10));
    const auto & log = log_of(page);
    check(log.size() == 3, "the event reached all three listeners");
    if (log.size() == 3) {
        // Order matters: a listener on the target must see the event before one
        // on its parent, or preventDefault from the inner one is pointless.
        check(log[0] == "inner" && log[1] == "outer" && log[2] == "document",
              "and bubbled outwards in order");
    }
}

// --- timers and frames ----------------------------------------------------

void test_timers() {
    browser page{browser_options{200, 200}};
    page.load_html(R"(<html><body><script>
    setTimeout(function () { console.log('late'); }, 100);
    setTimeout(function () { console.log('soon'); }, 5);
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(page.bindings().pending_timers() == 2, "two timers are armed");

    check(page.tick(1) == 0, "nothing is due after 1ms");
    check(page.tick(10) == 1, "the 5ms timer fires by 11ms");
    check(log_of(page).size() == 1 && log_of(page)[0] == "soon", "and it is the right one");
    check(page.tick(200) == 1, "the 100ms timer fires later");
    check(log_of(page).back() == "late", "in the right order");
    check(page.tick(1000) == 0, "a one-shot timer does not fire twice");
}

void test_interval_repeats_and_can_be_cleared() {
    browser page{browser_options{200, 200}};
    page.load_html(R"(<html><body><script>
    var n = 0;
    var id = setInterval(function () { n = n + 1; console.log('tick ' + n);
      if (n == 3) { clearInterval(id); } }, 10);
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    for (int i = 0; i < 6; ++i) { (void)page.tick(11); }
    // Three ticks then cleared. An interval that keeps firing after
    // clearInterval is the classic leak, and it only shows up over time.
    check(log_of(page).size() == 3, "the interval fired three times and then stopped");
    check(page.bindings().pending_timers() == 0, "and is no longer armed");
}

void test_request_animation_frame() {
    browser page{browser_options{200, 200}};
    page.load_html(R"(<html><body><script>
    var frames = 0;
    function loop() { frames = frames + 1; console.log('frame ' + frames);
      if (frames < 3) { requestAnimationFrame(loop); } }
    requestAnimationFrame(loop);
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(page.bindings().pending_animation_frames() == 1, "one frame callback is queued");
    for (int i = 0; i < 5; ++i) { (void)page.tick(16); }
    // A rAF callback that re-registers itself is the commonest animation
    // idiom, and running the queue in place would loop forever inside one tick.
    check(log_of(page).size() == 3, "a self-re-registering rAF runs once per tick");
    check(page.bindings().pending_animation_frames() == 0, "and stops when it stops asking");
}

// --- robustness -----------------------------------------------------------

void test_a_broken_script_still_renders() {
    browser page{browser_options{300, 200}};
    page.load_html(
        "<html><body><p>content</p><script>this is not javascript(((</script></body></html>");
    check(page.frame().has_value(), "the page still renders");
    check(!page.script_error().empty(), "and the error is recorded");
    // A page whose script fails must still show its markup. Anything else
    // turns one bad script into a blank window.
    check(rendered_text(page).find("content") != std::string::npos, "the markup is unaffected");
}

void test_window_and_performance() {
    browser page{browser_options{321, 234}};
    page.load_html(R"(<html><body><script>
    console.log('size ' + window.innerWidth + 'x' + window.innerHeight);
    console.log('t0 ' + performance.now());
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    const auto & log = log_of(page);
    check(log.size() == 2, "two console lines");
    if (log.size() == 2) {
        check(log[0] == "size 321x234", "window reports the viewport");
        check(log[1] == "t0 0", "and the page clock starts at zero");
    }
}

// --- input reaches script -------------------------------------------------
//
// This is the gap that made every game unplayable: the browser handled keys
// itself - scrolling, caret movement - and never told the page. A page could
// register a keydown listener and receive nothing, forever, with no error.

void test_keyboard_reaches_script() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<body><script>
      document.addEventListener('keydown', function (e) {
        console.log('down ' + e.code + ' key=' + e.key + ' shift=' + e.shiftKey);
      });
      document.addEventListener('keyup', function (e) { console.log('up ' + e.code); });
    </script></body>)");
    check(page.script_error().empty(), "the script ran");

    (void)page.handle(input_event::key_press("ArrowRight"));
    (void)page.handle(input_event::key_release("ArrowRight"));
    (void)page.handle(input_event::key_press("KeyA"));
    (void)page.handle(input_event::key_press("KeyA", true));
    (void)page.handle(input_event::key_press("Space"));

    const auto log = page.bindings().console_output();
    check(log.size() == 5, "five key events reached the page");
    if (log.size() == 5) {
        check(log[0] == "down ArrowRight key=ArrowRight shift=false", "an arrow key");
        // A RELEASE, which did not exist at all before: without it a game that
        // tracks held keys never stops moving.
        check(log[1] == "up ArrowRight", "the release");
        // `code` is the physical key, `key` is what it means - and shift is
        // what makes them differ.
        check(log[2] == "down KeyA key=a shift=false", "a letter");
        check(log[3] == "down KeyA key=A shift=true", "the same letter shifted");
        check(log[4] == "down Space key=  shift=false", "space's key is a space");
    }
}

void test_preventDefault_stops_the_browser_acting() {
    const char * tall = "<body><div style='height:2000px'>tall</div>";

    browser page{browser_options{200, 100}};
    page.load_html(std::string{tall} + R"(<script>
      document.addEventListener('keydown', function (e) { e.preventDefault(); });
    </script></body>)");
    check(page.frame().has_value(), "the page renders");
    check(page.max_scroll() > 0, "the page is taller than the viewport");
    (void)page.handle(input_event::key_press("Space"));
    check(page.scroll_y() == 0, "a cancelled keydown does not scroll the page");

    // ...and without the listener it does, which is what makes the test above
    // about preventDefault rather than about Space doing nothing.
    browser plain{browser_options{200, 100}};
    plain.load_html(std::string{tall} + "</body>");
    check(plain.frame().has_value(), "the plain page renders");
    (void)plain.handle(input_event::key_press("Space"));
    check(plain.scroll_y() > 0, "an uncancelled Space still scrolls");
}

void test_mouse_reaches_script() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<body><script>
      document.addEventListener('mousemove', function (e) {
        console.log('move ' + e.clientX + ',' + e.clientY);
      });
      document.addEventListener('mousedown', function (e) { console.log('down ' + e.button); });
      document.addEventListener('mouseup', function () { console.log('up'); });
    </script></body>)");
    check(page.script_error().empty(), "the script ran");

    (void)page.handle(input_event::mouse_move_to(40, 12));
    (void)page.handle(input_event::mouse_down_at(40, 12));
    (void)page.handle(input_event::mouse_up_at(40, 12));

    const auto log = page.bindings().console_output();
    check(log.size() == 3, "three mouse events reached the page");
    if (log.size() == 3) {
        // MDN's breakout moves its paddle from clientX alone, so the
        // coordinates are the whole content of the event.
        check(log[0] == "move 40,12", "the pointer position");
        check(log[1] == "down 0", "the left button is 0 in the DOM, not 1");
        check(log[2] == "up", "and the release");
    }
}

// The end-to-end version of the three tests above, against a page nobody wrote
// for this engine: MDN's breakout reads e.code and e.clientX, and if input does
// not reach it the paddle simply never moves. Comparing frames with and without
// input is the assertion, because "the paddle moved" is JS state this test
// cannot see - but it can see the pixels.
// The reason any of this exists: MDN's breakout ENDS by calling alert("GAME
// OVER") and then document.location.reload(). Both were undefined identifiers,
// so the one page in the suite that proves web compatibility died on its own
// game-over - after the point every other test stops looking.
void test_the_breakout_page_survives_its_own_game_over() {
    browser page{browser_options{480, 320}};
    std::ifstream in{"examples/pages/pong.html", std::ios::binary};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    page.load_html(buffer.str());
    check(page.script_error().empty(), "the page loaded");

    // Left alone the paddle never moves, so the ball is missed and the game
    // ends. Bounded so a page that never ends fails the check below instead of
    // hanging.
    for (int frame = 0; frame < 2000 && page.alerts().empty(); ++frame) {
        (void)page.tick(1000.0 / 60.0);
        (void)page.frame();
    }
    check(!page.alerts().empty(), "the game ended and alerted");
    if (!page.alerts().empty()) { check(page.alerts()[0] == "GAME OVER", "with GAME OVER"); }
    check(page.script_error().empty(), "and reloading itself did not break the script");

    // The reload really re-ran the page: it is playing again, so it can end
    // AGAIN rather than sitting on a dead context.
    const std::size_t after_first = page.alerts().size();
    for (int frame = 0; frame < 2000 && page.alerts().size() == after_first; ++frame) {
        (void)page.tick(1000.0 / 60.0);
        (void)page.frame();
    }
    check(page.alerts().size() > after_first, "and the reloaded game runs and ends too");
}

// The paddle stays ON the canvas however the mouse is moved.
//
// MDN's mouseMoveHandler gates on the CURSOR being inside the canvas and then
// centres the paddle on it WITHOUT clamping the resulting rect, so the paddle
// hangs up to half its width off either edge - in Chrome and Firefox too. The
// page carries one deviation from the tutorial to fix that (see the comment in
// examples/demos/pong.cpp); this is what says the deviation is still there.
void test_the_breakout_paddle_stays_on_the_canvas() {
    browser page{browser_options{480, 320}};
    std::ifstream in{"examples/pages/pong.html", std::ios::binary};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    page.load_html(buffer.str());
    check(page.script_error().empty(), "the page loaded");
    // The page computes relativeX as clientX - canvas.offsetLeft, and offsetLeft
    // is 0 only because the canvas is exactly the viewport width and nothing
    // scrolls. Assert it rather than assume it: if either stops holding, the
    // arithmetic below is measuring something else and the test goes vacuous.
    check(page.max_scroll() <= 0, "the page does not scroll, so the canvas is at x=0");

    // Ask the page a question in its own context. `alert` is recorded on the
    // browser, which is what makes a JS-side value readable from a test at all.
    const auto ask = [&page](const char * expression) {
        const std::size_t before = page.alerts().size();
        (void)page.run_script(std::string{"alert("} + expression + ");");
        return page.alerts().size() > before ? page.alerts().back() : std::string{"<no answer>"};
    };

    // Hard against the left wall. relativeX = 2 puts MDN's unclamped paddle at
    // -35.5 - most of a 75px paddle off the canvas.
    (void)page.handle(input_event::mouse_move_to(2, 300));
    check(ask("paddleX >= 0 ? 'in' : 'out'") == "in", "the paddle does not cross the left wall");

    // And the right, where the bound is canvas.width - paddleWidth = 405;
    // relativeX = 478 puts the unclamped paddle at 440.5.
    (void)page.handle(input_event::mouse_move_to(478, 300));
    check(ask("paddleX <= 480 - 75 ? 'in' : 'out'") == "in",
          "the paddle does not cross the right wall");

    // The control: the clamp must not have pinned the paddle to a wall for
    // every input. A cursor mid-canvas still centres the paddle on it.
    (void)page.handle(input_event::mouse_move_to(240, 300));
    check(ask("paddleX > 0 && paddleX < 405 ? 'free' : 'stuck'") == "free",
          "and it still tracks the mouse in between");
}

void test_a_real_page_responds_to_input() {
    const auto render = [](std::string_view held) {
        browser page{browser_options{480, 320}};
        std::ifstream in{"examples/pages/pong.html", std::ios::binary};
        std::ostringstream buffer;
        buffer << in.rdbuf();
        page.load_html(buffer.str());
        if (!held.empty()) { (void)page.handle(input_event::key_press(std::string{held})); }
        for (int frame = 0; frame < 20; ++frame) {
            (void)page.tick(1000.0 / 60.0);
            (void)page.frame();
        }
        const auto image = page.read_pixels();
        std::vector<std::uint32_t> pixels;
        if (image) {
            for (int y = 0; y < image->height(); ++y) {
                const auto row = image->row(y);
                pixels.insert(pixels.end(), row.begin(), row.end());
            }
        }
        return pixels;
    };

    const std::vector<std::uint32_t> idle = render("");
    const std::vector<std::uint32_t> pressed = render("ArrowRight");
    check(!idle.empty(), "the page rendered");
    check(idle.size() == pressed.size(), "both runs are the same size");
    check(idle != pressed, "holding a key changes what MDN's breakout draws");

    // The control: a key the page does not read must change NOTHING. Without
    // it, "the frames differ" could just mean the run is not reproducible, and
    // the test above would pass whether or not input worked.
    check(render("KeyQ") == idle, "a key the page ignores changes nothing");
}

// The same question asked of the ported example page, because it is the one
// whose key names I had to change: e.key was "Left", which nothing produces.
void test_the_invaders_page_responds_to_input() {
    const auto ship_row = [](std::string_view held) {
        browser page{browser_options{320, 240}};
        std::ifstream in{"examples/pages/invaders.html", std::ios::binary};
        std::ostringstream buffer;
        buffer << in.rdbuf();
        page.load_html(buffer.str());
        if (!held.empty()) { (void)page.handle(input_event::key_press(std::string{held})); }
        for (int frame = 0; frame < 30; ++frame) {
            (void)page.tick(1000.0 / 60.0);
            (void)page.frame();
        }
        // The ship's row of the canvas: moving left or right changes it, and
        // the drifting aliens above do not touch it.
        std::vector<std::uint32_t> row;
        if (const auto pixels = page.canvases().pixels_of(find_id(page, "game"))) {
            for (int x = 0; x < pixels->width; ++x) { row.push_back(pixels->at(x, 224)); }
        }
        return row;
    };

    const std::vector<std::uint32_t> still = ship_row("");
    check(!still.empty(), "the game drew a ship");
    check(ship_row("ArrowLeft") != still, "holding left moves the ship");
    check(ship_row("KeyQ") == still, "a key the page ignores does not");
}

// Space fires. It did not, and the reason was not input at all: the page tests
// `e.code === "Space"`, and `===` compared string ALLOCATIONS, so it was false
// for every event. Counting the bullet's own colour is what distinguishes
// "the key arrived" from "the page acted on it".
void test_the_invaders_page_shoots() {
    browser page{browser_options{320, 240}};
    // run_app installs playSound; a bare browser does not, and the page must
    // not depend on that to fire.
    page.define_native("playSound", [](script::context &, std::span<script::value>) {
        return script::value::boolean(true);
    });
    std::ifstream in{"examples/pages/invaders.html", std::ios::binary};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    page.load_html(buffer.str());

    const auto bullet_pixels = [&] {
        std::size_t found = 0;
        if (const auto pixels = page.canvases().pixels_of(find_id(page, "game"))) {
            for (int y = 0; y < pixels->height; ++y) {
                for (int x = 0; x < pixels->width; ++x) {
                    if (pixels->at(x, y) == 0xFFFFFF00U) { ++found; } // the bullet's yellow
                }
            }
        }
        return found;
    };
    const auto run = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            (void)page.tick(1000.0 / 60.0);
            (void)page.frame();
        }
    };

    run(5);
    check(bullet_pixels() == 0, "nothing is firing yet");
    (void)page.handle(input_event::key_press("Space"));
    run(5);
    check(bullet_pixels() > 0, "Space fires a bullet");
}

// A letterboxed page is authored at its LOGICAL size and the window only
// decides how big that gets drawn. SDL announces the window's pixel size on the
// first frame, and taking that as a page resize left the canvas - 320x240 by
// its own attributes - occupying a ninth of the viewport.
void test_a_letterboxed_page_keeps_its_size() {
    browser page{browser_options{320, 240}};
    std::ifstream in{"examples/pages/invaders.html", std::ios::binary};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    page.load_html(buffer.str());
    check(page.frame().has_value(), "the page renders");

    const auto canvas_box = [&] {
        const node_id want = find_id(page, "game");
        const auto walk = [&](auto && self, const layout::fragment & f, float dx,
                              float dy) -> rect {
            const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
            if (f.source == want) { return box; }
            for (const auto & child : f.children) {
                if (const rect hit = self(self, child, box.x, box.y); !hit.empty()) { return hit; }
            }
            return rect{};
        };
        return walk(walk, page.fragments(), 0, 0);
    };
    // The canvas fills the logical viewport exactly, which is the whole point
    // of authoring at 320x240 and letting SDL scale it.
    check(canvas_box().width == 320.0f, "the canvas is as wide as the page");
    check(canvas_box().height == 240.0f, "and as tall");
}

} // namespace

int main() {
    test_script_mutates_what_is_drawn();
    test_attributes_and_classes();
    test_document_title_and_tag_lookup();
    test_document_active_element_follows_focus();
    test_a_script_error_does_not_outlive_the_script();
    test_removeclass_undoes_it();
    test_create_and_append();
    test_remove_child();
    test_a_stale_handle_is_inert();
    test_layout_is_visible_to_script();

    test_alert_is_recorded();
    test_alert_reaches_the_hook();
    test_location_reload_reruns_the_page();
    test_window_and_document_share_one_location();
    test_a_link_is_handed_to_the_embedder();
    test_a_fragment_scrolls_instead_of_navigating();
    test_a_page_can_read_where_a_link_went();
    test_collection_keeps_what_the_page_still_uses();
    test_collection_happens_on_its_own();
    test_a_link_reaches_the_application_through_run_app();
    test_key_events_carry_the_legacy_codes();
    test_run_app_reports_a_script_error();
    test_run_app_is_silent_for_a_healthy_page();
    test_click_dispatch();
    test_handler_properties();
    test_events_bubble_and_can_be_prevented();

    test_timers();
    test_interval_repeats_and_can_be_cleared();
    test_request_animation_frame();

    test_a_broken_script_still_renders();
    test_window_and_performance();
    test_keyboard_reaches_script();
    test_preventDefault_stops_the_browser_acting();
    test_mouse_reaches_script();
    test_a_real_page_responds_to_input();
    test_the_breakout_page_survives_its_own_game_over();
    test_the_breakout_paddle_stays_on_the_canvas();
    test_the_invaders_page_responds_to_input();
    test_the_invaders_page_shoots();
    test_a_letterboxed_page_keeps_its_size();

    test_fetch_is_async();
    test_fetch_await_and_bytes();
    test_fetch_abort();
    test_control_value_is_live();
    test_canvas_as_image_source();
    test_insert_adjacent_html();
    test_clip();
    test_inner_html();
    test_listener_options();
    test_element_query_selector();
    test_await_suspends_and_resumes();
    test_a_promise_made_during_a_resumption_survives();
    test_a_suspended_frame_survives_collection();
    test_location_parts_and_cookies();
    test_p5_receives_input();
    test_webgl_is_constructible_and_refuses();
    test_composite_operations();
    test_composite_clears_untouched_pixels();
    test_image_data();
    test_fill_rule();
    test_text_alignment();
    test_reflected_attributes();
    test_tree_navigation();
    test_canvas_transform_and_paths();
    test_window_is_the_global_object();
    test_class_list_edits_the_attribute();
    test_class_list_reaches_the_cascade();
    test_style_writes_reach_the_document();
    test_style_writes_reach_the_pixels();

    REPORT("bindings_basics");
}
