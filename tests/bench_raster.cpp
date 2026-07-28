// Frame cost: what a first paint costs, and what a SCROLL costs.
//
// The second number is the one the whole pipeline exists for. A frame is split
// into three, and ONLY THE LAST runs on a scroll - an engine that re-runs
// layout and re-emits every paint command each frame pays a full layout to
// scroll one line:
//
//   record     fragment tree -> display list
//   raster     display list -> tiles, in CONTENT space, so a scroll cannot
//              invalidate them
//   composite  tiles -> the target, at the layer's current offset
//
// Everything here is measured with the software backend, which is the point of
// writing it first: the numbers are reproducible on a machine with no GPU.

import ctbrowser.core;
import ctbrowser.dom;
import ctbrowser.style;
import ctbrowser.layout;
import ctbrowser.paint;
import ctbrowser.raster;
import ctbrowser.shell; // metrics_for/font8x8_metrics, the layout<->raster adapter

#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using clock_type = std::chrono::steady_clock;
using namespace ctbrowser;

namespace {

[[nodiscard]] std::string build_html(int sections, int rows) {
    std::string out = "<body><div id=root>";
    for (int s = 0; s < sections; ++s) {
        out += "<section><h2>Section heading number " + std::to_string(s) + "</h2><ul>";
        for (int r = 0; r < rows; ++r) {
            out += "<li>Row " + std::to_string(r) +
                   " with enough words in it that the line breaker has real work to do</li>";
        }
        out += "</ul></section>";
    }
    out += "</div></body>";
    return out;
}

constexpr std::string_view sheet =
    "body { margin: 0; padding: 0; background-color: #ffffff }"
    "section { display: block; margin: 4px; padding: 2px; background-color: #f4f4f4 }"
    "h2 { display: block; font-size: 16px; margin: 2px; color: #103070 }"
    "ul { display: block; padding: 8px }"
    "li { display: block; font-size: 8px; margin: 1px; color: #202020 }";

template <typename F> [[nodiscard]] double time_ms(int reps, F && f) {
    const auto start = clock_type::now();
    for (int i = 0; i < reps; ++i) { f(); }
    const auto end = clock_type::now();
    return std::chrono::duration<double, std::milli>(end - start).count() / reps;
}

void run_case(int sections, int rows, int viewport_w, int viewport_h, scheduler & pool) {
    const std::string html = build_html(sections, rows);

    atom_table atoms;
    ::ctbrowser::document doc{atoms};
    (void)parse_html(doc, html);
    style::engine styles{atoms};
    styles.add_sheet(sheet, 1);
    const auto txn = doc.read();
    const style::style_map resolved = styles.resolve_all(txn);
    layout::box_builder builder{atoms, resolved};
    const layout::box_node boxes = builder.build(txn, txn.root());
    const layout::engine eng{shell::font8x8_metrics()};
    const layout::fragment placed = eng.run(boxes, static_cast<float>(viewport_w));

    const paint::recorder rec{atoms};
    const double record_ms = time_ms(10, [&] { (void)rec.record(placed); });
    paint::layer_tree layers = rec.record_layers(placed);

    // The first paint, viewport-culled: only tiles near the visible region get
    // drawn, plus one tile of prefetch margin.
    const rect viewport{0, 0, static_cast<float>(viewport_w), static_cast<float>(viewport_h)};
    const double first_ms = time_ms(10, [&] {
        raster::software_backend backend{viewport_w, viewport_h};
        (void)raster::draw(backend, layers, nullptr, raster::default_tile_extent, viewport);
    });
    const double first_par_ms = time_ms(10, [&] {
        raster::software_backend backend{viewport_w, viewport_h};
        (void)raster::draw(backend, layers, &pool, raster::default_tile_extent, viewport);
    });

    // A SCROLL FRAME, measured the way one actually happens: raster whatever
    // came into view, then composite. Tiles already drawn are kept, so the
    // steady-state cost is one row of tiles rather than a page.
    raster::software_backend scrolled{viewport_w, viewport_h};
    (void)raster::draw(scrolled, layers, &pool, raster::default_tile_extent, viewport);
    const std::size_t after_first = scrolled.raster_calls();
    float at = 0;
    const int scroll_steps = 40;
    const double scroll_ms = time_ms(scroll_steps, [&] {
        at += 60;
        layers.scroll_to(0, at);
        (void)raster::draw(scrolled, layers, &pool, raster::default_tile_extent, viewport);
    });
    const double tiles_per_scroll =
        static_cast<double>(scrolled.raster_calls() - after_first) / scroll_steps;

    const std::size_t all_tiles = raster::tiles_for(layers.layers[0].content_bounds(), 0).size();

    std::printf("%4d x %-4d %7zu %5zu/%-5zu  %8.3f %8.3f %8.3f %8.3f  %5.1f\n", sections, rows,
                placed.count(), after_first, all_tiles, record_ms, first_ms, first_par_ms,
                scroll_ms, tiles_per_scroll);
}

} // namespace

int main() {
    scheduler pool;
    std::printf("frame cost, 900x700 viewport, software backend, %zu pool workers\n\n",
                pool.worker_count());
    std::printf("%-12s %7s %11s  %8s %8s %8s %8s  %s\n", "  document", "frags", "tiles 1st/all",
                "record", "raster", "ras par", "scroll", "tiles/scroll");
    std::printf("%s\n", std::string(96, '-').c_str());
    for (const auto [sections, rows] :
         {std::pair{4, 5}, std::pair{10, 10}, std::pair{40, 25}, std::pair{120, 40}}) {
        run_case(sections, rows, 900, 700, pool);
    }
    std::printf("\nThe scroll column is a real scroll frame: raster whatever came into view,\n"
                "then composite. tiles/scroll is how many tiles that averaged, against the\n"
                "whole-page count in tiles 1st/all - the gap is what viewport culling and\n"
                "keeping already-drawn tiles buy. Both stay flat as the document grows,\n"
                "which is the property that matters: scrolling a long page costs the same\n"
                "as scrolling a short one.\n\n"
                "What is left in the scroll column is the software blit itself - roughly a\n"
                "megapixel of tile copied into a 900x700 target every frame. That is the\n"
                "cost the SDL3 GPU backend removes in stage 6, and it is why the software\n"
                "backend is the reference implementation rather than the fast one.\n");
    return 0;
}
