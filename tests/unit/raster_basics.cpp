// ctbrowser.raster: tiles, the backend seam, and the scroll path.
//
// Four claims, and the last two are the ones the architecture stands on:
//
//   1. the software backend satisfies RasterBackend - checked at compile time
//      in the module itself, so a missing method is a build error, not a
//      surprise when the GPU backend lands beside it.
//   2. blending and clipping are right.
//   3. TILING IS INVISIBLE. The same page rastered as one tile and as sixteen
//      must be byte-identical, and so must sequential against parallel. A tile
//      seam is a visible bug that no unit test of a single fill would catch.
//   4. A SCROLL DOES NOT RASTER. This is the reason tiles are kept in content
//      space at all; the evidence is that the raster counter does not move.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/shell/shell.hpp> // shell::font8x8_metrics - see shell/metrics.cppm
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using namespace ctbrowser::raster;
using ctbrowser::paint::display_list;
using ctbrowser::paint::layer;
using ctbrowser::paint::layer_tree;
using ctbrowser::paint::recorder;

namespace {

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %s\n", std::string{what}.c_str());
        ++ctbrowser_test_failures;
    }
}

// A page, taken all the way from HTML to a layer tree.
struct page {
    atom_table atoms;
    document doc{atoms};
    style::engine styles{atoms};
    style::style_map resolved;
    layout::box_node boxes;
    layout::fragment placed;
    layer_tree layers;

    void load(std::string_view html, std::string_view css, float viewport) {
        (void)parse_html(doc, html);
        styles.add_sheet(css, 1);
        const auto txn = doc.read();
        resolved = styles.resolve_all(txn);
        layout::box_builder builder{atoms, resolved};
        boxes = builder.build(txn, txn.root());
        // font8x8_advance, not the default measure: the rasterizer draws 8x8
        // cells at an integer scale, so layout has to measure the same way or
        // text lands where nothing expects it.
        const layout::engine eng{shell::font8x8_metrics()};
        placed = eng.run(boxes, viewport);
        const recorder rec{atoms};
        layers = rec.record_layers(placed);
    }
};

[[nodiscard]] std::uint32_t pixel_at(const surface & s, int x, int y) {
    if (x < 0 || y < 0 || x >= s.width() || y >= s.height()) { return 0; }
    return s.row(y)[static_cast<std::size_t>(x)];
}

// --- blending and clipping ------------------------------------------------

void test_blend_over() {
    // A solid colour replaces; a fully transparent one does nothing; half
    // alpha lands halfway. Getting this wrong makes every translucent overlay
    // subtly the wrong shade, which is exactly the kind of bug a golden image
    // catches long after it was introduced.
    check(blend_over(0xFF000000u, color{0xFFFF0000u}) == 0xFFFF0000u, "an opaque source replaces");
    check(blend_over(0xFF00FF00u, color{0x00FF0000u}) == 0xFF00FF00u,
          "a transparent source does nothing");
    const std::uint32_t half = blend_over(0xFF000000u, color::rgba(255, 255, 255, 128));
    const std::uint32_t red = (half >> 16) & 0xFFu;
    check(red >= 126 && red <= 130, "half alpha over black lands near the midpoint");
}

void test_fill_and_clip() {
    auto list = std::make_shared<display_list>();
    list->push_clip(rect{0, 0, 10, 10});
    list->fill(rect{0, 0, 100, 100}, color::rgba(255, 0, 0));
    list->pop_clip();

    software_backend backend{64, 64, 64};
    layer_tree tree;
    tree.layers.push_back(layer{list, point{}, rect{}, true});
    check(draw(backend, tree).has_value(), "a clipped fill draws without error");

    check(pixel_at(backend.target(), 5, 5) == 0xFFFF0000u, "inside the clip is filled");
    check(pixel_at(backend.target(), 20, 20) != 0xFFFF0000u, "outside the clip is not");
}

void test_frame_bracketing_is_enforced() {
    software_backend backend{16, 16, 16};
    // Calling into a backend outside a frame is a programming error the
    // interface can report rather than a crash later - the GPU backend will
    // have a command buffer that genuinely does not exist yet.
    const auto bad = backend.raster(tile_id{0, 0, 0}, display_list{});
    check(!bad.has_value() && bad.error() == gpu_error::no_frame, "raster outside a frame fails");
    const auto opened = backend.begin_frame();
    check(opened.has_value(), "begin_frame opens one");
    check(!backend.begin_frame().has_value(), "and a second begin_frame is refused");
    check(backend.end_frame().has_value(), "end_frame closes it");
}

// --- tiling is invisible --------------------------------------------------

void test_tiling_does_not_change_the_image() {
    page p;
    p.load("<html><body><div class=a>alpha beta gamma delta</div>"
           "<div class=b>epsilon zeta eta theta iota kappa</div>"
           "<div class=a>lambda mu nu xi omicron pi rho</div></body></html>",
           "body { margin: 0; padding: 0 } "
           ".a { background-color: #cc2020; color: white; font-size: 16px } "
           ".b { background-color: #2040cc; color: yellow; font-size: 16px }",
           400);

    // One tile covering everything, versus a 32px grid: 13 tiles across and
    // several down, so almost every glyph and every fill straddles a seam.
    software_backend whole{400, 300, 1024};
    software_backend tiled{400, 300, 32};
    check(draw(whole, p.layers, nullptr, 1024).has_value(), "untiled draw succeeds");
    check(draw(tiled, p.layers, nullptr, 32).has_value(), "tiled draw succeeds");
    check(whole.target() == tiled.target(), "tiling is invisible: the images are byte-identical");
    check(tiled.raster_calls() > whole.raster_calls(), "...and it really did use more tiles");
}

void test_parallel_raster_matches_sequential() {
    page p;
    p.load("<html><body><div class=a>alpha beta gamma delta epsilon zeta</div>"
           "<div class=b>eta theta iota kappa lambda mu nu xi</div>"
           "<div class=a>omicron pi rho sigma tau upsilon phi chi</div>"
           "<div class=b>psi omega and then some more words again</div></body></html>",
           "body { margin: 0 } .a { background-color: #207020; color: white; font-size: 16px } "
           ".b { background-color: #702070; color: #ffff00; font-size: 16px }",
           640);

    scheduler pool;
    software_backend sequential{640, 480, 64};
    check(draw(sequential, p.layers, nullptr, 64).has_value(), "sequential draw succeeds");

    // Several runs: a tile race that only shows on some interleaving is still
    // a race, and one clean image proves very little.
    for (int attempt = 0; attempt < 8; ++attempt) {
        software_backend parallel{640, 480, 64};
        check(draw(parallel, p.layers, &pool, 64).has_value(), "parallel draw succeeds");
        if (!(sequential.target() == parallel.target())) {
            std::printf("FAIL parallel raster diverged from sequential on attempt %d\n", attempt);
            ++ctbrowser_test_failures;
            return;
        }
    }
    check(true, "parallel raster is byte-identical to sequential, 8 runs");
    check(sequential.raster_calls() >= 20, "the page really is spread over many tiles");
}

// --- a scroll does not raster ---------------------------------------------

void test_scrolling_recomposites_without_rastering() {
    page p;
    p.load("<html><body><div id=a>first line of the page</div>"
           "<div id=b>second line of the page</div></body></html>",
           "body { margin: 0; padding: 0 } "
           "#a { height: 40px; background-color: #ff0000 } "
           "#b { height: 40px; background-color: #00ff00 }",
           200);

    software_backend backend{200, 200, 64};
    check(draw(backend, p.layers, nullptr, 64).has_value(), "the first frame draws");
    const std::size_t after_first = backend.raster_calls();
    check(after_first > 0, "the first frame rastered something");

    std::vector<std::uint32_t> before;
    for (int y = 0; y < 200; ++y) { before.push_back(pixel_at(backend.target(), 10, y)); }

    p.layers.scroll_to(0, 20);
    check(recomposite(backend, p.layers).has_value(), "the scrolled frame composites");

    // THE CLAIM. A scroll is a composite. Tiles were rastered in content space
    // and are still valid; the previous engine re-ran layout and re-emitted every command.
    check(backend.raster_calls() == after_first, "scrolling rastered NOTHING new");

    // And it actually moved: the pixel at y is now what used to be at y+20.
    bool shifted = true;
    for (int y = 0; y + 20 < 160; ++y) {
        if (pixel_at(backend.target(), 10, y) != before[static_cast<std::size_t>(y + 20)]) {
            shifted = false;
            std::printf("     first mismatch at y=%d: %08X vs %08X\n", y,
                        pixel_at(backend.target(), 10, y),
                        before[static_cast<std::size_t>(y + 20)]);
            break;
        }
    }
    check(shifted, "and the composited image is the old one, moved up by the scroll");
}

void test_a_fixed_layer_does_not_move() {
    auto scrolling = std::make_shared<display_list>();
    scrolling->fill(rect{0, 0, 50, 50}, color::rgba(255, 0, 0));
    auto pinned = std::make_shared<display_list>();
    pinned->fill(rect{60, 0, 30, 30}, color::rgba(0, 0, 255));

    layer_tree tree;
    tree.layers.push_back(layer{scrolling, point{}, rect{}, true});
    tree.layers.push_back(layer{pinned, point{}, rect{}, false});

    software_backend backend{128, 128, 128};
    check(draw(backend, tree, nullptr, 128).has_value(), "two layers draw");
    tree.scroll_to(0, 20);
    check(recomposite(backend, tree).has_value(), "and re-composite after a scroll");

    // position:fixed needs no per-command flag here - it is simply a layer the
    // scroll does not move. the previous engine carried a `fixed` bool on every paint command.
    check(pixel_at(backend.target(), 70, 5) == 0xFF0000FFu, "the pinned layer stayed put");
    check(pixel_at(backend.target(), 10, 5) == 0xFFFF0000u, "the scrolling layer moved under it");
    check(pixel_at(backend.target(), 10, 35) != 0xFFFF0000u, "and its bottom edge came up");
}

// --- viewport culling and incremental raster ------------------------------

void test_culling_does_not_change_what_you_see() {
    page p;
    p.load("<html><body><div class=a>alpha beta gamma delta epsilon</div>"
           "<div class=b>zeta eta theta iota kappa lambda mu</div>"
           "<div class=a>nu xi omicron pi rho sigma tau upsilon</div>"
           "<div class=b>phi chi psi omega and then more words</div>"
           "<div class=a>a second screenful that is entirely offscreen</div>"
           "<div class=b>and a third one after that for good measure</div></body></html>",
           "body { margin: 0 } .a { background-color: #305030; color: white; font-size: 16px; "
           "height: 90px } .b { background-color: #503050; color: #ffff00; font-size: 16px; "
           "height: 90px }",
           300);

    const rect viewport{0, 0, 300, 200};
    software_backend everything{300, 200, 64};
    software_backend culled{300, 200, 64};
    check(draw(everything, p.layers, nullptr, 64).has_value(), "the uncelled frame draws");
    check(draw(culled, p.layers, nullptr, 64, viewport).has_value(), "the culled frame draws");

    // Culling is an optimisation, so it has to be invisible. If it is not, the
    // page has holes in it and no amount of speed makes up for that.
    check(everything.target() == culled.target(),
          "culling changes nothing you can see inside the viewport");
    check(culled.raster_calls() < everything.raster_calls(),
          "...and it really did skip offscreen tiles");
    std::printf("     culled %zu tiles vs %zu for the whole page\n", culled.raster_calls(),
                everything.raster_calls());
}

void test_a_repeated_frame_rasters_nothing() {
    page p;
    p.load("<html><body><div id=a>content</div></body></html>",
           "#a { height: 300px; background-color: #204080; color: white; font-size: 16px }", 300);

    software_backend backend{300, 200, 64};
    const rect viewport{0, 0, 300, 200};
    check(draw(backend, p.layers, nullptr, 64, viewport).has_value(), "the first frame draws");
    const std::size_t after_first = backend.raster_calls();
    check(after_first > 0, "and it rastered something");

    check(draw(backend, p.layers, nullptr, 64, viewport).has_value(), "the second frame draws");
    // Nothing changed, so nothing needs redrawing. Without this a caret blink
    // or a hover repaints the page.
    check(backend.raster_calls() == after_first, "an unchanged frame rasters NOTHING again");
}

void test_scrolling_rasters_only_what_came_into_view() {
    page p;
    p.load("<html><body><div id=a>a tall page</div></body></html>",
           "body { margin: 0 } #a { height: 2000px; background-color: #802020 }", 300);

    software_backend backend{300, 200, 64};
    const rect viewport{0, 0, 300, 200};
    check(draw(backend, p.layers, nullptr, 64, viewport).has_value(), "the first frame draws");
    const std::size_t first = backend.raster_calls();

    p.layers.scroll_to(0, 64);
    check(draw(backend, p.layers, nullptr, 64, viewport).has_value(), "the scrolled frame draws");
    const std::size_t after_scroll = backend.raster_calls() - first;

    // A one-tile scroll exposes one row of tiles. Paying for the whole page
    // again - which is what the uncelled path did - is the thing being fixed.
    check(after_scroll > 0, "scrolling into new content does raster it");
    check(after_scroll < first, "but only the newly exposed tiles, not the page");
    std::printf("     first frame %zu tiles, one-tile scroll %zu more\n", first, after_scroll);

    // And scrolling BACK is free, because those tiles were kept.
    const std::size_t before_back = backend.raster_calls();
    p.layers.scroll_to(0, 0);
    check(draw(backend, p.layers, nullptr, 64, viewport).has_value(), "scrolling back draws");
    check(backend.raster_calls() == before_back, "scrolling back over kept tiles rasters nothing");
}

void test_relayout_invalidates_every_tile() {
    page p;
    p.load("<html><body><div id=a>x</div></body></html>",
           "#a { height: 100px; background-color: #204080 }", 300);
    software_backend backend{300, 200, 64};
    check(draw(backend, p.layers, nullptr, 64).has_value(), "the first frame draws");
    const std::size_t first = backend.raster_calls();

    // A relayout means the tiles hold pixels for content that no longer exists.
    // Keeping them would show the old page.
    backend.discard();
    check(draw(backend, p.layers, nullptr, 64).has_value(), "the frame after a discard draws");
    check(backend.raster_calls() > first, "discard() forces every tile to be rastered again");
}

// --- the golden -----------------------------------------------------------

constexpr std::string_view golden_path = "tests/golden/page.ppm";

[[nodiscard]] std::string to_ppm(const surface & s) {
    std::string out =
        "P6\n" + std::to_string(s.width()) + " " + std::to_string(s.height()) + "\n255\n";
    out.reserve(out.size() + static_cast<std::size_t>(s.width() * s.height()) * 3);
    for (int y = 0; y < s.height(); ++y) {
        for (int x = 0; x < s.width(); ++x) {
            const std::uint32_t p = s.row(y)[static_cast<std::size_t>(x)];
            out.push_back(static_cast<char>((p >> 16) & 0xFFu));
            out.push_back(static_cast<char>((p >> 8) & 0xFFu));
            out.push_back(static_cast<char>(p & 0xFFu));
        }
    }
    return out;
}

void test_golden_page() {
    page p;
    p.load("<html><body>"
           "<h1>ctbrowser</h1>"
           "<div class=note>The software backend renders this without a font "
           "installed, which is what makes the image reproducible.</div>"
           "<div class=box><div class=inner>nested</div></div>"
           "</body></html>",
           "body { margin: 0; padding: 8px; background-color: #f0f0f0; color: #202020 } "
           "h1 { font-size: 24px; margin: 4px; color: #103070; background-color: #ffffff } "
           ".note { font-size: 16px; margin: 4px; padding: 4px; background-color: #ffffe0 } "
           ".box { margin: 4px; padding: 6px; background-color: #d0e0ff; border-color: #3050a0; "
           "border-width: 2px } "
           ".inner { font-size: 16px; background-color: rgba(255, 0, 0, 0.5) } ",
           320);

    software_backend backend{320, 240, 64};
    check(draw(backend, p.layers, nullptr, 64).has_value(), "the golden page draws");
    const std::string got = to_ppm(backend.target());

    if (const char * regolden = std::getenv("REGOLDEN"); regolden != nullptr && *regolden != '0') {
        std::FILE * f = std::fopen(std::string{golden_path}.c_str(), "wb");
        if (f == nullptr) {
            std::printf("FAIL cannot write %s\n", std::string{golden_path}.c_str());
            ++ctbrowser_test_failures;
            return;
        }
        std::fwrite(got.data(), 1, got.size(), f);
        std::fclose(f);
        std::printf("     regenerated %s (%zu bytes)\n", std::string{golden_path}.c_str(),
                    got.size());
        return;
    }

    std::FILE * f = std::fopen(std::string{golden_path}.c_str(), "rb");
    if (f == nullptr) {
        std::printf("FAIL %s missing - run with REGOLDEN=1 from the source root\n",
                    std::string{golden_path}.c_str());
        ++ctbrowser_test_failures;
        return;
    }
    std::string want;
    char buffer[4096];
    while (const std::size_t n = std::fread(buffer, 1, sizeof buffer, f)) {
        want.append(buffer, n);
    }
    std::fclose(f);

    if (got == want) {
        check(true, "the rendered page matches the golden byte for byte");
        return;
    }
    std::size_t differing = 0;
    for (std::size_t i = 0; i < got.size() && i < want.size(); ++i) {
        if (got[i] != want[i]) { ++differing; }
    }
    std::printf("FAIL golden mismatch: %zu bytes differ (got %zu, want %zu). "
                "REGOLDEN=1 to accept.\n",
                differing, got.size(), want.size());
    ++ctbrowser_test_failures;
}

} // namespace

int main() {
    test_blend_over();
    test_fill_and_clip();
    test_frame_bracketing_is_enforced();

    test_tiling_does_not_change_the_image();
    test_parallel_raster_matches_sequential();

    test_scrolling_recomposites_without_rastering();
    test_a_fixed_layer_does_not_move();

    test_culling_does_not_change_what_you_see();
    test_a_repeated_frame_rasters_nothing();
    test_scrolling_rasters_only_what_came_into_view();
    test_relayout_invalidates_every_tile();

    test_golden_page();

    REPORT("raster_basics");
}
