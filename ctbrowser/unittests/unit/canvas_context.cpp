// THE CANVAS CONTEXTS THROUGH THE BINDINGS: the 2D transform and path family,
// text alignment, the fill rule, every composite operation, ImageData, `clip()`
// and a canvas as an image source - and that `getContext('webgl')` constructs
// and refuses cleanly. widgets_basics.cpp asks the same canvas what it DRAWS;
// this asks what a script GETS BACK.
//
// One of eight files carved out of unittests/unit/bindings_basics.cpp on
// 2026-09-07, when it had reached 3,365 lines. Every case is verbatim and in
// the order it had; the helpers more than one of the eight needs are in
// chrome_probe.hpp beside this, and `find_id` is test/support/dom_probe.hpp's.

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
#include "chrome_probe.hpp"
#include "dom_probe.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;
using ctbrowser_test::find_id;
using ctbrowser_test::log_of;

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

// Coordinates near +-FLT_MAX overflow float differences to inf/NaN; the scanline
// sort must not see a NaN crossing (QSORT-001).
void test_huge_coordinate_fill() {
    browser page{browser_options{200, 200}};
    page.load_html(R"(<html><body><canvas id=c width=50 height=50></canvas><script>
        const ctx = document.getElementById('c').getContext('2d');
        ctx.beginPath();
        for (let i = 0; i < 24; ++i) {
            ctx.moveTo(-3e38, -3e38 + i); ctx.lineTo(3e38, 3e38 - i); ctx.lineTo(-3e38, 3e38);
        }
        ctx.fill();
        ctx.fill('evenodd');
        console.log('done');
    </script></body></html>)");
    check(page.script_error().empty(), "huge-coordinate fill ran: " + page.script_error());
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

void test_image_data_numeric_bounds() {
    browser page{browser_options{80, 60}};
    page.load_html(R"(<canvas id=c width=4 height=2></canvas><script>
        const ctx = document.getElementById('c').getContext('2d');
        function error(f) {
          try { f(); console.log('no error'); } catch (e) { console.log(e.name); }
        }
        error(() => ctx.createImageData(NaN, 1));
        error(() => ctx.createImageData(Infinity, 1));
        error(() => ctx.createImageData(2147483648, 1));
        error(() => ctx.createImageData(0, 1));
        error(() => ctx.getImageData(0, 0, 1, 0));
        const blank = ctx.createImageData(-2.9, -1.9);
        console.log('negative=' + blank.width + 'x' + blank.height);
        const copied = ctx.createImageData(blank);
        console.log('copy=' + copied.data.length + ',' + copied.data[0]);
        ctx.fillStyle = '#ff0000'; ctx.fillRect(0, 0, 1, 1);
        ctx.fillStyle = '#00ff00'; ctx.fillRect(1, 0, 1, 1);
        const back = ctx.getImageData(2, 1, -2, -1);
        console.log('back=' + back.data[0] + ',' + back.data[5]);
        const far = ctx.getImageData(2147483647, 0, 2, 1);
        const left = ctx.getImageData(-2147483648, 0, -2, 1);
        console.log('outside=' + far.data[3] + ',' + far.data[7] + ',' + left.data[3]);
        ctx.putImageData(back, 2147483647, 0);
        ctx.putImageData(back, -2147483648, 0);
        console.log('unchanged=' + ctx.getImageData(0, 0, 1, 1).data[0]);
        ctx.putImageData(back, -1, 0);
        console.log('clipped=' + ctx.getImageData(0, 0, 1, 1).data[1]);
        error(() => ctx.putImageData(back, NaN, 0));
        error(() => ctx.getImageData(Infinity, 0, 1, 1));
        let order = '';
        ctx.getImageData({ valueOf() { order += 'x'; return 0.9; } },
                         { valueOf() { order += 'y'; return 0; } }, 1, 1);
        console.log('coercion=' + order);
        error(() => ctx.getImageData({ valueOf() { throw new Error('stop'); } },
                                     { valueOf() { order += 'wrong'; return 0; } }, 1, 1));
        console.log('stopped=' + order);
        console.log('wrap=' + new ImageData(4294967297, 1).width);
        error(() => new ImageData(NaN, 1));
        error(() => new ImageData(-1, -1));
        error(() => new ImageData(new Uint8ClampedArray(8), 2, 0));
        error(() => new ImageData(new Uint8ClampedArray(7), 2));
        const view = new Uint8ClampedArray(new ArrayBuffer(8), 4, 4);
        view[0] = 10; view[1] = 20; view[2] = 30; view[3] = 255;
        ctx.putImageData(new ImageData(view, 1), 0, 0);
        console.log('view=' + ctx.getImageData(0, 0, 1, 1).data[2]);
        // Something merely SHAPED like an ImageData is not one: WebIDL converts
        // putImageData's argument to the interface (2d.imageData.put.wrongtype).
        error(() => ctx.putImageData({ width: 1, height: 1, data: [NaN, Infinity, 2.5, 255] },
                                     0, 0));
    </script>)");
    check(page.script_error().empty(), "image-data boundary calls ran: " + page.script_error());
    const std::vector<std::string> expected{
        "TypeError",      "TypeError", "TypeError",      "IndexSizeError", "IndexSizeError",
        "negative=2x1",   "copy=8,0",  "back=255,255",   "outside=0,0,0",  "unchanged=255",
        "clipped=255",    "TypeError", "TypeError",      "coercion=xy",    "Error",
        "stopped=xy",     "wrap=1",    "IndexSizeError", "RangeError",     "IndexSizeError",
        "IndexSizeError", "view=30",   "TypeError"};
    const auto & log = log_of(page);
    check(log == expected, "ImageData conversions, clipping, buffer shapes and errors agree");
    if (log != expected) {
        for (const auto & line : log) { std::fprintf(stderr, "ImageData: %s\n", line.c_str()); }
    }
}

void test_raster_numeric_bounds() {
    constexpr float infinity = std::numeric_limits<float>::infinity();
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    check(raster::round_to_pixel(-27.5f) == -28 && raster::round_to_pixel(27.5f) == 28,
          "pixel ties still round away from zero");
    check(raster::round_to_pixel(infinity) == std::numeric_limits<int>::max() &&
              raster::round_to_pixel(-infinity) == std::numeric_limits<int>::min() &&
              raster::round_to_pixel(nan) == 0,
          "pixel rounding defines out-of-range and non-finite results");
    const auto clipped = raster::to_pixels(rect{-1e30f, -1e30f, 2e30f, 2e30f}, 4, 4);
    check(clipped.left == 0 && clipped.top == 0 && clipped.right == 4 && clipped.bottom == 4,
          "large finite bounds clip to the tiny surface before indexing");
    check(raster::to_pixels(rect{nan, 0, 4, 4}, 4, 4).empty() &&
              raster::to_pixels(rect{0, 0, infinity, 4}, 4, 4).empty(),
          "invalid rectangles do not draw");
    raster::surface into{4, 4};
    paint::paint_command text;
    text.text = "AA";
    text.font_size = 16;
    text.fill = color::rgba(255, 0, 0);
    const auto & fonts = raster::font8x8_fonts();
    fonts.draw_run(rect{1e30f, 0, 16, 16}, text, {0, 0, 4, 4}, into);
    fonts.draw_run(rect{0, 1e30f, 16, 16}, text, {0, 0, 4, 4}, into);
    check(std::ranges::all_of(into.pixels(), [](auto pixel) { return pixel == 0; }),
          "far-off glyphs leave the surface unchanged");
    text.font_size = 1e30f;
    fonts.draw_run(rect{0, 0, 16, 16}, text, {0, 0, 4, 4}, into);
    check(fonts.ascent(text.font_size, "", false, false) > 0,
          "large font scales remain defined and drawing visits only clipped pixels");
}

// bufferData(target, size) takes a page's number: NaN, negatives, infinities and 1e12
// must set a GL error rather than crash, and Int32Array([-1]) uploads modularly.
void test_webgl_buffer_data_sizes() {
    browser page{browser_options{100, 100}};
    page.load_html(R"(<html><body><script>
        var gl = document.createElement('canvas').getContext('webgl');
        var b = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, b);
        var out = [];
        [1e12, NaN, -1, Infinity, -Infinity].forEach(function (n) {
            gl.getError(); gl.bufferData(gl.ARRAY_BUFFER, n, gl.STATIC_DRAW);
            out.push(gl.getError());
        });
        gl.bufferData(gl.ARRAY_BUFFER, new Int32Array([-1]), gl.STATIC_DRAW);
        gl.bufferData(gl.ARRAY_BUFFER, 16, gl.STATIC_DRAW);
        console.log('sizes=' + out.join(',') + ',' + gl.getError());
    </script></body></html>)");
    check(page.script_error().empty(), "the page loaded: " + page.script_error());
    bool found = false;
    for (const std::string & line : log_of(page)) {
        if (line.starts_with("sizes=")) {
            found = true;
            // OUT_OF_MEMORY, none (NaN is 0), INVALID_VALUE, OUT_OF_MEMORY, INVALID_VALUE, none
            check(line == "sizes=1285,0,1281,1285,1281,0", "bufferData sizes: " + line);
        }
    }
    check(found, "the sizes line was logged");
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
    page.load_html(R"(<html><head><script>var IS_MINIFIED = true;</script>
        <script src="vendor/p5/p5.js"></script></head><body><script>
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

// A CLIP MASK IS SIZED TO THE BITMAP IT WAS BUILT FOR. When the surface is
// re-dimensioned under a live clip (WebGL's present() once did it), indexing
// the old mask with the new width read past its end. A stale mask now clips
// out everything, until a resize or restore() replaces it. 2D only, so the
// asan preset (no WebGL) can run it.
void test_clip_after_surface_resized() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body><canvas id=c width=100 height=40></canvas><script>
        var ctx = document.getElementById('c').getContext('2d');
        ctx.save();
        ctx.beginPath(); ctx.rect(10, 10, 20, 20); ctx.clip();
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    shell::canvas_context * canvas = page.canvases().find(find_id(page, "c"));
    check(canvas != nullptr, "the canvas has a context");
    if (canvas == nullptr) { return; }
    *canvas->surface() = paint::bitmap{400, 40};
    (void)page.run_script(R"(
        ctx.fillStyle = '#ff0000'; ctx.fillRect(0, 0, 400, 40);
        ctx.clearRect(0, 0, 400, 40);
        ctx.globalCompositeOperation = 'copy'; ctx.fillRect(0, 0, 400, 40);
        ctx.globalCompositeOperation = 'source-over';
        ctx.beginPath(); ctx.rect(0, 0, 400, 40); ctx.clip();
        ctx.fillRect(0, 0, 400, 40);
        ctx.restore();
        ctx.fillStyle = '#00ff00'; ctx.fillRect(300, 0, 10, 10);
    )");
    check(page.script_error().empty(), "the draws ran: " + page.script_error());
    const auto pixels = canvas->surface();
    check(color{pixels->at(350, 20)}.alpha() == 0, "a stale clip clips everything out");
    check(color{pixels->at(305, 5)} == color::rgba(0, 255, 0), "restore() drops the stale clip");
}

// ONE CONTEXT PER CANVAS. A canvas that handed out a 2D context answers null
// to `webgl`; sharing the bitmap let present() re-dimension it under the 2D
// clip mask (a heap overread). A canvas whose surface predates a width change
// gets a WebGL context over a surface of the new size.
void test_webgl_after_2d_is_null() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body><canvas id=c width=100 height=40></canvas>
        <canvas id=d width=100 height=40></canvas><script>
        const c = document.getElementById('c');
        const ctx = c.getContext('2d');
        ctx.beginPath(); ctx.rect(10, 10, 20, 20); ctx.clip();
        c.setAttribute('width', '400');
        console.log('gl=' + c.getContext('webgl'));
        ctx.fillStyle = '#ff0000'; ctx.fillRect(0, 0, 400, 40);
        ctx.clearRect(0, 0, 400, 40);
        const d = document.getElementById('d');
        d.toDataURL();
        d.setAttribute('width', '400');
        const gl = d.getContext('webgl');
        if (gl) {
            gl.clearColor(0, 0, 1, 1); gl.clear(gl.COLOR_BUFFER_BIT);
            const px = new Uint8Array(4);
            gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
        }
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    bool nulled = false;
    for (const std::string & line : log_of(page)) { nulled = nulled || line == "gl=null"; }
    check(nulled, "getContext('webgl') after '2d' is null");
    const auto pixels = page.canvases().pixels_of(find_id(page, "d"));
    check(pixels != nullptr && pixels->width == 400, "the WebGL surface takes the new width");
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

// THE CONTEXT THE WEBIDL WAY: attributes that refuse what they cannot hold and
// read back their own serialisation, the paints a fill can use, and the path
// verbs that were missing. Each line is one rule of HTML 4.12.5.1 that a page
// can observe; html/canvas/element in WPT has the full set.
void test_attributes_paints_and_verbs() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body><canvas id=c width=100 height=60></canvas><script>
        const canvas = document.getElementById('c');
        const ctx = canvas.getContext('2d');
        console.log('same=' + (canvas.getContext('2d') === ctx));
        ctx.lineWidth = 3; ctx.lineWidth = -1; ctx.lineWidth = NaN;
        console.log('lineWidth=' + ctx.lineWidth);
        ctx.fillStyle = '#fa0'; console.log('fill=' + ctx.fillStyle);
        ctx.fillStyle = 'not a colour'; console.log('kept=' + ctx.fillStyle);
        ctx.fillStyle = 'rgba(255, 255, 255, 0.5)'; console.log('alpha=' + ctx.fillStyle);
        ctx.lineCap = 'bogus'; console.log('cap=' + ctx.lineCap);
        ctx.font = '20PX   SERIF'; console.log('font=' + ctx.font);
        console.log('proto=' + (typeof CanvasRenderingContext2D.prototype.roundRect));
        let threw = 'no';
        try { ctx.scale(1); } catch (e) { threw = e.name; }
        console.log('missing=' + threw);
        // A gradient: red at the left edge, blue at the right.
        const g = ctx.createLinearGradient(0, 0, 100, 0);
        g.addColorStop(0, '#ff0000'); g.addColorStop(1, '#0000ff');
        ctx.fillStyle = g;
        console.log('gradient=' + (ctx.fillStyle === g));
        ctx.fillRect(0, 0, 100, 20);
        // A round rectangle, its corner cut away; and hit testing.
        ctx.fillStyle = '#00ff00';
        ctx.beginPath(); ctx.roundRect(0, 20, 40, 40, 15); ctx.fill();
        console.log('inside=' + ctx.isPointInPath(20, 40) + ',' + ctx.isPointInPath(1, 21));
        // A square cap reaches half the width past the end of the line.
        ctx.strokeStyle = '#0000ff'; ctx.lineWidth = 10; ctx.lineCap = 'square';
        ctx.beginPath(); ctx.moveTo(60, 40); ctx.lineTo(80, 40); ctx.stroke();
        ctx.save(); ctx.fillStyle = '#123456'; ctx.reset();
        console.log('reset=' + ctx.fillStyle + ',' + ctx.lineWidth + ',' +
                    ctx.getImageData(5, 5, 1, 1).data[3]);
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    const std::vector<std::string> expected{"same=true",
                                            "lineWidth=3",
                                            "fill=#ffaa00",
                                            "kept=#ffaa00",
                                            "alpha=rgba(255, 255, 255, 0.5)",
                                            "cap=butt",
                                            "font=20px serif",
                                            "proto=function",
                                            "missing=TypeError",
                                            "gradient=true",
                                            "inside=true,false",
                                            "reset=#000000,1,0"};
    const auto & log = log_of(page);
    check(log == expected, "attributes, paints and verbs behave as the spec says");
    if (log != expected) {
        for (const auto & line : log) { std::fprintf(stderr, "canvas: %s\n", line.c_str()); }
    }
}

// The pixels of the same calls, before reset() cleared them.
void test_gradients_caps_and_round_corners() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body><canvas id=c width=100 height=60></canvas><script>
        const ctx = document.getElementById('c').getContext('2d');
        const g = ctx.createLinearGradient(0, 0, 100, 0);
        g.addColorStop(0, '#ff0000'); g.addColorStop(1, '#0000ff');
        ctx.fillStyle = g;
        ctx.fillRect(0, 0, 100, 20);
        ctx.fillStyle = '#00ff00';
        ctx.beginPath(); ctx.roundRect(0, 20, 40, 40, 15); ctx.fill();
        ctx.strokeStyle = '#0000ff'; ctx.lineWidth = 10; ctx.lineCap = 'square';
        ctx.beginPath(); ctx.moveTo(60, 40); ctx.lineTo(80, 40); ctx.stroke();
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    const auto pixels = page.canvases().pixels_of(find_id(page, "c"));
    check(pixels != nullptr, "the canvas has pixels");
    if (pixels == nullptr) { return; }
    const auto at = [&](int x, int y) { return color{pixels->at(x, y)}; };
    check(at(1, 10).red() > 240 && at(1, 10).blue() < 15, "the gradient starts red");
    check(at(98, 10).blue() > 240 && at(98, 10).red() < 15, "and ends blue");
    check(at(50, 10).red() > 110 && at(50, 10).red() < 145, "and is half way in the middle");
    check(at(20, 40) == color::rgba(0, 255, 0), "the round rectangle filled");
    check(at(1, 21).alpha() == 0, "with its corner cut away");
    check(at(83, 40) == color::rgba(0, 0, 255), "a square cap reaches past the end");
    check(at(87, 40).alpha() == 0, "by half the width and no further");
}

// THE REVIEW'S CASES (round 14): what a page can observe of each - an explicit
// undefined maxWidth, a style object across save()/restore(), createPattern's
// order of checks, the ImageData brand and DOMMatrix2DInit's conversion.
void test_argument_conversions() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body><canvas id=c width=100 height=60></canvas><script>
        const ctx = document.getElementById('c').getContext('2d');
        // fillText(text, x, y, undefined) is fillText(text, x, y).
        ctx.font = '20px sans-serif';
        ctx.fillStyle = '#000';
        ctx.fillText('WW', 5, 25, undefined);
        let inked = 0;
        const d = ctx.getImageData(0, 0, 50, 30).data;
        for (let i = 3; i < d.length; i += 4) { if (d[i] !== 0) { ++inked; } }
        console.log('fillText=' + (inked > 0));
        ctx.clearRect(0, 0, 100, 60);
        ctx.strokeText('WW', 5, 25, undefined);
        inked = 0;
        const s = ctx.getImageData(0, 0, 50, 30).data;
        for (let i = 3; i < s.length; i += 4) { if (s[i] !== 0) { ++inked; } }
        console.log('strokeText=' + (inked > 0));
        ctx.clearRect(0, 0, 100, 60);
        // THE OBJECT the state holds, after a restore() brought it back.
        const g1 = ctx.createLinearGradient(0, 0, 10, 0);
        const g2 = ctx.createLinearGradient(0, 0, 20, 0);
        ctx.fillStyle = g1;
        ctx.save();
        ctx.fillStyle = g2;
        ctx.save();
        ctx.fillStyle = 'red';
        ctx.restore();
        const inner = ctx.fillStyle === g2;
        ctx.restore();
        console.log('restored=' + inner + ',' + (ctx.fillStyle === g1) + ',' +
                    (ctx.fillStyle === ctx.fillStyle));
        const other = document.createElement('canvas');
        const p = ctx.createPattern(other, 'repeat');
        ctx.strokeStyle = p;
        ctx.save(); ctx.strokeStyle = '#123456'; ctx.restore();
        console.log('pattern=' + (ctx.strokeStyle === p));
        ctx.save(); ctx.reset();
        console.log('reset=' + ctx.fillStyle + ',' + ctx.strokeStyle);
        // createPattern: an image that is not ready is null BEFORE the
        // repetition is validated; a ready one with a bad one still throws.
        console.log('unready=' + ctx.createPattern(new Image(), 'bogus'));
        let bad = 'no';
        try { ctx.createPattern(other, 'bogus'); } catch (e) { bad = e.name; }
        console.log('bogus=' + bad);
        // putImageData wants an ImageData by its brand: a lookalike with the
        // right prototype is not one, and replacing the global breaks nothing.
        const fake = { width: 1, height: 1, data: new Uint8ClampedArray(4) };
        Object.setPrototypeOf(fake, ImageData.prototype);
        let forged = 'no';
        try { ctx.putImageData(fake, 0, 0); } catch (e) { forged = e.name; }
        console.log('forged=' + forged);
        const real = ctx.createImageData(1, 1);
        window.ImageData = function () {};
        let replaced = 'no';
        try { ctx.putImageData(real, 0, 0); } catch (e) { replaced = e.name; }
        console.log('replaced=' + replaced);
        // DOMMatrix2DInit: members in lexicographic order, validate and fixup
        // before the canvas ignores what is not finite.
        const order = [];
        const init = {};
        for (const name of ['m42', 'f', 'm11', 'a', 'c', 'm21', 'b', 'm12', 'd', 'm22', 'e', 'm41']) {
            Object.defineProperty(init, name, { get() { order.push(name); return undefined; } });
        }
        ctx.setTransform(init);
        console.log('order=' + order.join(','));
        let clash = 'no';
        try { ctx.setTransform({ a: Infinity, m11: 1 }); } catch (e) { clash = e.name; }
        console.log('clash=' + clash);
        ctx.setTransform(2, 0, 0, 2, 0, 0);
        ctx.setTransform({ a: Infinity });
        ctx.setTransform({ a: NaN, m11: NaN });
        console.log('ignored=' + ctx.getTransform().a);
        ctx.setTransform({ m11: 3, a: 3 });
        console.log('agree=' + ctx.getTransform().a);
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    const std::vector<std::string> expected{"fillText=true",
                                            "strokeText=true",
                                            "restored=true,true,true",
                                            "pattern=true",
                                            "reset=#000000,#000000",
                                            "unready=null",
                                            "bogus=SyntaxError",
                                            "forged=TypeError",
                                            "replaced=no",
                                            "order=a,b,c,d,e,f,m11,m12,m21,m22,m41,m42",
                                            "clash=TypeError",
                                            "ignored=2",
                                            "agree=3"};
    const auto & log = log_of(page);
    check(log == expected, "the review's argument conversions behave as the spec says");
    if (log != expected) {
        for (const auto & line : log) { std::fprintf(stderr, "canvas: %s\n", line.c_str()); }
    }
}

// WORK BOUNDED BY WHAT LANDS: a dash pattern along a line a billion pixels
// long, a stroke of text two thousand pixels wide and a shadow thrown 1e20
// pixels each finish at once - and still draw what reaches the canvas.
void test_bounded_work() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><canvas id=c width=100 height=60></canvas>
        <canvas id=t width=100 height=60></canvas><canvas id=s width=100 height=60></canvas><script>
        const ctx = document.getElementById('c').getContext('2d');
        ctx.strokeStyle = '#0000ff';
        ctx.setLineDash([2, 2]);
        ctx.beginPath(); ctx.moveTo(0, 5.5); ctx.lineTo(1e9, 5.5); ctx.stroke();
        // The phase far along the line is arithmetic, not walked: the dash
        // [100000, 100002) is on and [100002, 100004) off.
        console.log('hit=' + ctx.isPointInStroke(100001, 5.5) + ',' +
                    ctx.isPointInStroke(100003, 5.5));
        const text = document.getElementById('t').getContext('2d');
        text.lineWidth = 2000;
        text.font = '10px sans-serif';
        text.strokeStyle = '#00ff00';
        text.strokeText('W', 10, 58);
        const shade = document.getElementById('s').getContext('2d');
        shade.shadowColor = '#ff0000';
        shade.shadowOffsetX = 1e20;
        shade.fillStyle = 'rgba(0, 0, 0, 0.5)';
        shade.fillRect(40, 20, 10, 10);
        shade.shadowOffsetX = 30;
        shade.fillRect(-25, 40, 10, 10);   // off the canvas, its shadow on it
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    const std::vector<std::string> expected{"hit=true,false"};
    check(log_of(page) == expected, "isPointInStroke finds the dash phase far along a line");
    const auto dashes = page.canvases().pixels_of(find_id(page, "c"));
    const auto text = page.canvases().pixels_of(find_id(page, "t"));
    const auto shade = page.canvases().pixels_of(find_id(page, "s"));
    check(dashes != nullptr && text != nullptr && shade != nullptr, "the canvases have pixels");
    if (dashes == nullptr || text == nullptr || shade == nullptr) { return; }
    const auto at = [](const auto & pixels, int x, int y) { return color{pixels->at(x, y)}; };
    check(at(dashes, 0, 5) == color::rgba(0, 0, 255) && at(dashes, 1, 5) == color::rgba(0, 0, 255),
          "the first dash of a billion-pixel line is drawn");
    check(at(dashes, 2, 5).alpha() == 0 && at(dashes, 3, 5).alpha() == 0, "and the gap after it");
    check(at(dashes, 98, 5).alpha() == 0 && at(dashes, 97, 5) == color::rgba(0, 0, 255),
          "and the pattern holds to the edge");
    check(at(text, 60, 50) == color::rgba(0, 255, 0), "a 2000-wide stroke of text still paints");
    check(at(shade, 45, 25).alpha() != 0 && at(shade, 45, 25).red() == 0,
          "a shadow thrown off the canvas is not drawn under its shape");
    check(at(shade, 75, 25).alpha() == 0, "nor anywhere else");
    check(at(shade, 8, 45).red() > 200 && at(shade, 8, 45).alpha() != 0,
          "an off-canvas shape's shadow lands");
}

} // namespace

int main() {
    test_attributes_paints_and_verbs();
    test_argument_conversions();
    test_bounded_work();
    test_gradients_caps_and_round_corners();
    test_canvas_as_image_source();
    test_clip();
    test_clip_after_surface_resized();
    test_webgl_after_2d_is_null();
    test_webgl_is_constructible_and_refuses();
    test_webgl_buffer_data_sizes();
    test_composite_operations();
    test_composite_clears_untouched_pixels();
    test_image_data();
    test_image_data_numeric_bounds();
    test_raster_numeric_bounds();
    test_fill_rule();
    test_text_alignment();
    test_canvas_transform_and_paths();
    test_huge_coordinate_fill();
    REPORT("canvas_context");
}
