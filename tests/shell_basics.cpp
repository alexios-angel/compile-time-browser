// ctbrowser.shell: the engine assembled, and what a frame is allowed to skip.
//
// Every earlier test checked one subsystem. This checks that they compose into
// something that behaves like a browser - and the claims worth pinning are the
// ones about WORK NOT DONE, because those are the whole point of the
// architecture and the only ones a screenshot cannot show:
//
//   a scroll        re-composites, and does not re-layout or re-raster
//   an idle frame   does nothing at all
//   a resize        re-lays-out, and does not re-parse
//   a new document  starts clean, with no rules left over from the last one
//
// Plus the things a browser is simply expected to do: apply the UA stylesheet,
// hit-test through scroll, and render the same bytes twice.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/style/style.hpp>
import ctbrowser.shell;

#include "check.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
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

constexpr std::string_view demo_page = R"(
<!doctype html><html><head><title>A Title</title><style>
.card { background-color: #ffffff; padding: 8px }
#hero { background-color: #103070; color: #ffffff; padding: 12px }
</style></head><body>
<div id=hero><h1>Heading</h1></div>
<div class=card><p>Some text that is long enough to wrap onto more than one line at a narrow width.</p></div>
<div class=card><p>A second card, so the page is taller than a short viewport and can scroll.</p></div>
<div class=card><p>A third card for the same reason.</p></div>
<div class=card><p>And a fourth.</p></div>
</body></html>)";

// How many tiles the renderer has drawn. The evidence for "this frame did no
// raster" has to come from the backend, not from a stopwatch.
[[nodiscard]] std::size_t raster_calls(browser & page) {
    const raster::software_backend * backend =
        const_cast<raster::renderer &>(page.rendering_with()).get_if<raster::software_backend>();
    return backend == nullptr ? 0 : backend->raster_calls();
}

// --- the UA stylesheet ----------------------------------------------------

void test_ua_stylesheet_applies() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><body><h1>Big</h1><p>Small</p><script>var x = 1;</script></body></html>");
    check(page.frame().has_value(), "the page renders");

    // An <h1> is 32px and a <p> is 16px because the UA sheet says so, not
    // because anything in the document does. Without it every document is one
    // undifferentiated size, which is what "unstyled" actually looks like.
    const layout::fragment & root = page.fragments();
    float widest_glyph_row = 0;
    const auto walk = [&](auto && self, const layout::fragment & f) -> void {
        if (!f.text.empty()) { widest_glyph_row = std::max(widest_glyph_row, f.bounds.height); }
        for (const auto & c : f.children) { self(self, c); }
    };
    walk(walk, root);
    check(widest_glyph_row >= 32, "the h1 renders larger than body text (UA font-size)");
    check(page.content_height() > 40, "and the document has real height");
}

void test_script_and_style_render_nothing() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><head><style>p { color: red }</style></head>"
                   "<body><script>var secret = 'do not render me';</script>"
                   "<p>visible</p></body></html>");
    check(page.frame().has_value(), "the page renders");

    std::string all_text;
    const auto walk = [&](auto && self, const layout::fragment & f) -> void {
        all_text += f.text;
        for (const auto & c : f.children) { self(self, c); }
    };
    walk(walk, page.fragments());
    // The classic failure of a browser without a UA sheet: it renders its own
    // scripts and stylesheets as page text.
    check(all_text.find("secret") == std::string::npos, "script source is not rendered");
    check(all_text.find("color: red") == std::string::npos, "stylesheet text is not rendered");
    check(all_text.find("visible") != std::string::npos, "...but the page content is");
}

void test_title_is_extracted() {
    browser page{browser_options{200, 200}};
    page.load_html(demo_page);
    check(page.title() == "A Title", "the document title is read");
}

// --- what a frame skips ---------------------------------------------------

void test_an_idle_frame_does_nothing() {
    browser page{browser_options{400, 300}};
    page.load_html(demo_page);
    check(page.frame().has_value(), "the first frame renders");
    const std::size_t after_first = raster_calls(page);
    check(after_first > 0, "and it rastered something");

    check(page.frame().has_value(), "an idle frame renders");
    // A browser that repaints when nothing changed is a browser that never
    // idles. This is the cheapest possible check that it does.
    check(raster_calls(page) == after_first, "an idle frame rasters NOTHING");
}

void test_a_scroll_only_recomposites() {
    browser page{browser_options{400, 200}};
    page.load_html(demo_page);
    check(page.frame().has_value(), "the first frame renders");
    const std::size_t after_first = raster_calls(page);
    check(page.max_scroll() > 0, "the page is taller than the viewport");

    page.scroll_by(20);
    check(page.scroll_y() == 20, "the scroll took effect");
    check(page.frame().has_value(), "the scrolled frame renders");
    // THE claim of the whole architecture: the PAGE's tiles are in content
    // space and survive a scroll. the previous engine re-ran layout here.
    //
    // The scrollbar's own tile does not survive, and must not: its thumb is a
    // function of where the page now is, and a tile is identified by (layer,
    // column, row) - so leaving it cached serves the old thumb forever, which
    // is what "the scrollbar does not update" looked like. ONE tile, the
    // chrome's, is the price.
    const std::size_t scrolled = raster_calls(page);
    check(scrolled > after_first, "the scrollbar's tile is redrawn");
    check(scrolled - after_first <= 2, "and only the scrollbar's - not the page's");

    // With no scrollbar there is nothing to redraw at all, which is the
    // original claim with the chrome taken out of it.
    browser_options no_chrome;
    no_chrome.width = 400;
    no_chrome.height = 200;
    no_chrome.scrollbar_width = 0;
    browser bare{no_chrome};
    bare.load_html(demo_page);
    check(bare.frame().has_value(), "the bare page renders");
    const std::size_t bare_first = raster_calls(bare);
    bare.scroll_by(20);
    check(bare.frame().has_value(), "the bare scrolled frame renders");
    check(raster_calls(bare) == bare_first, "a scroll with no scrollbar rasters NOTHING");
}

void test_scroll_clamps_to_the_document() {
    browser page{browser_options{400, 200}};
    page.load_html(demo_page);
    check(page.frame().has_value(), "the frame renders");
    page.scroll_to(-100);
    check(page.scroll_y() == 0, "scrolling above the top clamps");
    page.scroll_to(1e9f);
    check(page.scroll_y() == page.max_scroll(), "scrolling past the end clamps to max_scroll");
    check(page.max_scroll() == page.content_height() - 200,
          "which is the content minus a viewport");
}

void test_a_resize_relayouts() {
    browser page{browser_options{600, 300}};
    page.load_html(demo_page);
    check(page.frame().has_value(), "the wide frame renders");
    const float wide_height = page.content_height();

    page.resize(200, 300);
    check(page.frame().has_value(), "the narrow frame renders");
    // Narrower means more wrapping means taller. If the resize had not
    // re-laid-out, this would be unchanged - which is the bug a resize test
    // exists to catch.
    check(page.content_height() > wide_height, "a narrower viewport makes the document taller");
    check(page.width() == 200, "and the viewport followed");
}

void test_a_new_document_starts_clean() {
    browser page{browser_options{300, 300}};
    page.load_html("<html><head><style>p { background-color: #ff0000 }</style></head>"
                   "<body><p>first</p></body></html>");
    check(page.frame().has_value(), "the first document renders");

    page.load_html("<html><body><p>second</p></body></html>");
    check(page.frame().has_value(), "the second document renders");
    check(page.title().empty(), "the old title is gone");

    // The first page's author rules must not survive. They would, if the style
    // engine were reused - which is exactly the bug this checks for, and it
    // shows up as one page bleeding into the next.
    std::size_t red_fills = 0;
    for (const auto & layer : page.layers().layers) {
        if (!layer.contents) { continue; }
        for (const auto & c : layer.contents->commands()) {
            if (c.fill == color::rgba(255, 0, 0)) { ++red_fills; }
        }
    }
    check(red_fills == 0, "the previous document's stylesheet does not apply to this one");
}

// --- input ----------------------------------------------------------------

void test_hit_testing_follows_the_scroll() {
    browser page{browser_options{400, 200}};
    page.load_html(demo_page);
    check(page.frame().has_value(), "the frame renders");

    const node_id top = page.hit_test(10, 10);
    check(static_cast<bool>(top), "something is under the top-left corner");

    // The property, stated so it does not depend on the page's exact geometry:
    // scrolling by S and looking at y is the same as not scrolling and looking
    // at y + S. Fragments are in CONTENT space; the scroll is applied at
    // hit-test time.
    const float step = 120;
    const node_id deeper_unscrolled = page.hit_test(10, 10 + step);
    page.scroll_by(step);
    check(page.scroll_y() == step, "the scroll took effect");
    check(page.hit_test(10, 10) == deeper_unscrolled, "hit testing follows the scroll exactly");

    // And it really did move to a different element - otherwise the check above
    // would pass on a page where scrolling changes nothing.
    check(deeper_unscrolled != top, "...onto a different element than before");
}

void test_wheel_and_keys_scroll() {
    browser page{browser_options{400, 200}};
    page.load_html(demo_page);
    check(page.frame().has_value(), "the frame renders");

    check(page.handle(input_event::wheel_by(-1)), "a wheel notch is handled");
    check(page.scroll_y() > 0, "and scrolls down");

    page.scroll_to(0);
    check(page.handle(input_event::key_press("End")), "End is handled");
    check(page.scroll_y() == page.max_scroll(), "and goes to the bottom");
    check(page.handle(input_event::key_press("Home")), "Home is handled");
    check(page.scroll_y() == 0, "and comes back to the top");

    check(!page.handle(input_event::key_press("F13")),
          "an unhandled key reports that it did nothing");
}

void test_hover_restyles() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><head><style>#a { background-color: #00ff00; padding: 20px } "
                   "#a:hover { background-color: #ff0000 } </style></head>"
                   "<body><div id=a>hover me</div></body></html>");
    check(page.frame().has_value(), "the frame renders");

    const auto count_fill = [&](color want) {
        std::size_t n = 0;
        for (const auto & layer : page.layers().layers) {
            if (!layer.contents) { continue; }
            for (const auto & c : layer.contents->commands()) {
                if (c.fill == want) { ++n; }
            }
        }
        return n;
    };
    check(count_fill(color::rgba(0, 255, 0)) == 1, "the element paints its normal background");

    check(page.handle(input_event::mouse_move_to(10, 30)), "moving onto it is a change");
    check(page.frame().has_value(), "the hovered frame renders");
    check(count_fill(color::rgba(255, 0, 0)) == 1, ":hover applied");
    check(count_fill(color::rgba(0, 255, 0)) == 0, "and the normal background is gone");

    check(page.handle(input_event::mouse_move_to(390, 290)), "moving off it is a change");
    check(page.frame().has_value(), "the unhovered frame renders");
    check(count_fill(color::rgba(0, 255, 0)) == 1, ":hover unapplied when the pointer leaves");
}

void test_resize_keeps_the_renderer() {
    browser page{browser_options{400, 300}};
    page.load_html(demo_page);
    check(page.frame().has_value(), "the page renders");

    // Stand in for "the app chose the GPU": adopt a *named* renderer and check
    // the name survives. resize() used to build a fresh software backend, so an
    // app that picked hardware dropped to software on its first window resize
    // and never came back.
    page.use_renderer(raster::renderer::software(400, 300));
    const std::string before{page.rendering_with().name()};
    page.resize(700, 500);
    check(page.frame().has_value(), "the resized frame renders");
    check(page.rendering_with().name() == before, "resize keeps the renderer it was given");
    check(page.width() == 700 && page.height() == 500, "and the viewport followed");

    const auto image = page.read_pixels();
    check(image.has_value() && image->width() == 700 && image->height() == 500,
          "and the target really is the new size");
}

void test_background_is_honoured() {
    browser_options options{80, 60};
    options.background = color::rgba(0, 0, 255);
    browser page{options};
    // A page with no body background shows the canvas colour. browser_options
    // carried this field and nothing read it.
    page.load_html("<html><body></body></html>");
    check(page.frame().has_value(), "the page renders");
    const auto image = page.read_pixels();
    check(image.has_value(), "the frame reads back");
    if (!image) { return; }
    check(image->row(30)[40] == 0xFF0000FFu, "browser_options::background is the page canvas");
}

// A browser holds three `this`-capturing lambdas; moving one would leave them
// pointing at the old address. The implicit move was available until it was
// deleted, so this is worth stating as a compile-time fact.
static_assert(!std::is_move_constructible_v<browser>, "browser must not be movable");
static_assert(!std::is_copy_constructible_v<browser>, "browser must not be copyable");

// --- determinism ----------------------------------------------------------

void test_rendering_is_reproducible() {
    const auto render = [](std::vector<std::uint32_t> & into) {
        browser page{browser_options{320, 240}};
        page.load_html(demo_page);
        (void)page.frame();
        const auto image = page.read_pixels();
        if (!image) { return false; }
        into.assign(image->pixels().begin(), image->pixels().end());
        return true;
    };
    std::vector<std::uint32_t> first, second;
    check(render(first) && render(second), "two independent renders succeed");
    // A browser whose output depends on allocation addresses or map iteration
    // order cannot have a golden image, and cannot be reviewed by diffing one.
    check(first == second, "two renders of the same page are byte-identical");
}

} // namespace

int main() {
    test_ua_stylesheet_applies();
    test_script_and_style_render_nothing();
    test_title_is_extracted();

    test_an_idle_frame_does_nothing();
    test_a_scroll_only_recomposites();
    test_scroll_clamps_to_the_document();
    test_a_resize_relayouts();
    test_a_new_document_starts_clean();
    test_resize_keeps_the_renderer();
    test_background_is_honoured();

    test_hit_testing_follows_the_scroll();
    test_wheel_and_keys_scroll();
    test_hover_restyles();

    test_rendering_is_reproducible();

    REPORT("shell_basics");
}
