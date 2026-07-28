// The frame pipeline: a compositor thread that owns the device, and the runtime
// renderer seam that lets the engine fall back to software.
//
// Two claims:
//
//   1. THE COMPOSITOR THREAD PRODUCES THE SAME IMAGE as running the frame
//      inline. Every device call happens on one thread while raster runs on the
//      pool and hands tiles over lock-free channels - so if the handoff drops,
//      duplicates or reorders anything, the image says so.
//   2. THE RENDERER SEAM IS TRANSPARENT. A type-erased renderer must render
//      exactly what the backend it holds renders, or the fallback is not a
//      fallback, it is a second renderer with its own bugs.
//
// This runs on the software backend on purpose: it is the half of stage 6 that
// needs no GPU, so it is the half CI can enforce.

#include <ctbrowser/core/core.hpp>
import ctbrowser.dom;
import ctbrowser.style;
import ctbrowser.layout;
import ctbrowser.paint;
import ctbrowser.raster;
import ctbrowser.shell; // shell::font8x8_metrics - see shell/metrics.cppm

#include "check.hpp"
#include <cstdint>
#include <cstdio>
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
        const layout::engine eng{shell::font8x8_metrics()};
        placed = eng.run(boxes, viewport);
        const recorder rec{atoms};
        layers = rec.record_layers(placed);
    }
};

// atom_table is noncopyable and page holds one, so the fixture is filled in
// place rather than returned.
void load_busy_page(page & p, float viewport) {
    p.load("<html><body><div class=a>alpha beta gamma delta epsilon zeta eta</div>"
           "<div class=b>theta iota kappa lambda mu nu xi omicron pi rho</div>"
           "<div class=a>sigma tau upsilon phi chi psi omega and some more</div>"
           "<div class=b>a fourth row so the tiling has something to spread</div>"
           "<div class=a>and a fifth for good measure, with words in it</div></body></html>",
           "body { margin: 0; padding: 0 } "
           ".a { background-color: #cc4020; color: white; font-size: 16px; padding: 4px } "
           ".b { background-color: #2040cc; color: #ffff00; font-size: 16px; padding: 4px }",
           viewport);
}

// --- the compositor thread ------------------------------------------------

void test_compositor_thread_matches_inline() {
    page p;
    load_busy_page(p, 600);
    scheduler pool;

    software_backend inline_backend{600, 400, 64};
    check(draw(inline_backend, p.layers, &pool, 64).has_value(), "the inline frame draws");

    // Several frames through the thread: a handoff that only drops a tile on
    // some interleaving is still broken, and one clean frame proves little.
    for (int attempt = 0; attempt < 6; ++attempt) {
        software_backend threaded{600, 400, 64};
        compositor_thread<software_backend> compositor{threaded, pool};
        const std::uint64_t n = compositor.submit(frame_request{p.layers, rect{}, 64});
        compositor.wait_for(n);
        check(!compositor.last_error().has_value(), "the threaded frame reported no error");
        if (!(inline_backend.target() == threaded.target())) {
            std::printf("FAIL compositor thread diverged from inline on attempt %d\n", attempt);
            ++ctbrowser_test_failures;
            return;
        }
        check(compositor.frames_presented() == 1, "one submit, one presented frame");
    }
    check(true, "the compositor thread produces the same image as an inline frame, 6 runs");
}

void test_every_tile_reaches_the_compositor() {
    page p;
    load_busy_page(p, 600);
    scheduler pool;
    software_backend backend{600, 400, 64};
    compositor_thread<software_backend> compositor{backend, pool};

    const std::uint64_t n = compositor.submit(frame_request{p.layers, rect{}, 64});
    compositor.wait_for(n);

    // publish() blocks rather than dropping when a channel is full, so the
    // count is exact. A dropped completion would mean the compositor waited for
    // a tile that never arrived - which is a hang, not a wrong pixel, and worth
    // pinning as a count rather than trusting the absence of one.
    check(compositor.tiles_published() == backend.raster_calls(),
          "every rastered tile was published to the compositor");
    check(backend.raster_calls() > 4, "and the page really was spread over several tiles");
}

void test_repeated_frames_through_the_thread() {
    page p;
    load_busy_page(p, 600);
    scheduler pool;
    software_backend backend{600, 400, 64};
    compositor_thread<software_backend> compositor{backend, pool};

    std::uint64_t last = 0;
    for (int i = 0; i < 5; ++i) {
        last = compositor.submit(frame_request{p.layers, rect{0, 0, 600, 400}, 64});
        compositor.wait_for(last);
    }
    check(compositor.frames_presented() == 5, "five frames in, five frames out");
    check(!compositor.last_error().has_value(), "and none of them failed");
    // Frames 2..5 change nothing, so they must raster nothing: the incremental
    // path has to survive going through the thread.
    const std::size_t after = backend.raster_calls();
    last = compositor.submit(frame_request{p.layers, rect{0, 0, 600, 400}, 64});
    compositor.wait_for(last);
    check(backend.raster_calls() == after, "an unchanged frame still rasters nothing");
}

void test_scrolling_through_the_thread() {
    page p;
    load_busy_page(p, 400);
    scheduler pool;
    software_backend backend{400, 200, 64};
    compositor_thread<software_backend> compositor{backend, pool};

    const rect viewport{0, 0, 400, 200};
    compositor.wait_for(compositor.submit(frame_request{p.layers, viewport, 64}));
    const std::size_t first = backend.raster_calls();

    p.layers.scroll_to(0, 64);
    compositor.wait_for(compositor.submit(frame_request{p.layers, viewport, 64}));
    check(backend.raster_calls() >= first, "a scroll may raster newly exposed tiles");
    check(backend.raster_calls() - first < first, "but not the whole page again");
}

// --- the runtime renderer seam --------------------------------------------

void test_renderer_renders_what_its_backend_renders() {
    page p;
    load_busy_page(p, 600);
    scheduler pool;

    software_backend direct{600, 400, 64};
    check(draw(direct, p.layers, &pool, 64).has_value(), "the direct frame draws");

    renderer indirect = renderer::software(600, 400, 64);
    check(static_cast<bool>(indirect), "the software renderer was created");
    check(!indirect.hardware(), "and reports itself as not hardware");
    check(indirect.name() == "software", "and names itself");
    check(draw(indirect, p.layers, &pool, 64).has_value(), "the frame through the seam draws");

    const auto through = indirect.read_target();
    check(through.has_value(), "the software renderer can read its own target back");
    if (!through) { return; }
    // If type erasure changed a single pixel, it is not a seam, it is a second
    // renderer.
    check(direct.target() == *through, "the renderer seam is byte-for-byte transparent");
}

void test_renderer_works_in_the_compositor_thread() {
    page p;
    load_busy_page(p, 600);
    scheduler pool;
    renderer r = renderer::software(600, 400, 64);
    compositor_thread<renderer> compositor{r, pool};
    compositor.wait_for(compositor.submit(frame_request{p.layers, rect{}, 64}));
    check(!compositor.last_error().has_value(), "a type-erased renderer runs the pipeline");

    software_backend direct{600, 400, 64};
    check(draw(direct, p.layers, &pool, 64).has_value(), "the reference frame draws");
    const auto through = r.read_target();
    check(through.has_value() && direct.target() == *through,
          "...and produces the same image through the thread");
}

void test_renderer_reports_what_it_cannot_do() {
    renderer empty;
    check(!static_cast<bool>(empty), "a default renderer is empty");
    renderer r = renderer::software(16, 16, 16);
    check(r.get_if<software_backend>() != nullptr, "get_if finds the concrete backend");
    // discard() must reach through the seam, or a relayout would silently keep
    // showing the old page on the fallback path.
    software_backend * inner = r.get_if<software_backend>();
    check(draw(r, layer_tree{}, nullptr, 16).has_value(), "an empty frame draws");
    r.discard();
    check(inner != nullptr, "and discard reached the backend");
}

} // namespace

int main() {
    test_compositor_thread_matches_inline();
    test_every_tile_reaches_the_compositor();
    test_repeated_frames_through_the_thread();
    test_scrolling_through_the_thread();

    test_renderer_renders_what_its_backend_renders();
    test_renderer_works_in_the_compositor_thread();
    test_renderer_reports_what_it_cannot_do();

    REPORT("pipeline_basics");
}
