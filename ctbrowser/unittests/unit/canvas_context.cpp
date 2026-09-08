// THE CANVAS CONTEXTS THROUGH THE BINDINGS: the 2D transform and path family,
// text alignment, the fill rule, every composite operation, ImageData, `clip()`
// and a canvas as an image source - and that `getContext('webgl')` constructs
// and refuses cleanly. widgets_basics.cpp asks the same canvas what it DRAWS;
// this asks what a script GETS BACK.
//
// One of eight files carved out of unittests/unit/bindings_basics.cpp on
// 2026-09-07, when it had reached 3,365 lines. Every case is verbatim and in
// the order it had; the helpers more than one of the eight needs are in
// page_probe.hpp beside this, and `find_id` is test/support/dom_probe.hpp's.

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
#include "dom_probe.hpp"
#include "page_probe.hpp"
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
using ctbrowser_test::check;
using ctbrowser_test::find_id;
using ctbrowser_test::log_of;
using ctbrowser_test::read_bytes;

namespace {

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

// `clip()` CONFINES what is drawn after it, and `restore()` is the only way
// back. The region is an INTERSECTION of paths, so two clips leave what they
// have in common - which is what makes nesting them work.
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

} // namespace

int main() {
    test_canvas_as_image_source();
    test_clip();
    test_webgl_is_constructible_and_refuses();
    test_composite_operations();
    test_composite_clears_untouched_pixels();
    test_image_data();
    test_fill_rule();
    test_text_alignment();
    test_canvas_transform_and_paths();
    REPORT("canvas_context");
}
