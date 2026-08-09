// What a mouse move COSTS, on a real page.
//
// The question this exists to answer is "why does an idle-looking application
// use most of a core", and the answer is not in any of the other benches:
// nothing here is a big document or a hot loop. It is that moving the pointer
// from one element to another re-resolves the cascade, re-lays-out, re-records
// and re-rasterises the whole page - and an event loop capped at 60 fps will
// happily do that sixty times a second for as long as the mouse keeps moving.
//
// So the number to look at is `hover change` in milliseconds. Multiply it by
// the frame rate to get the CPU the application burns while the pointer is in
// motion: 10 ms is 60% of a core, which is what was reported.
//
// Measured against the same page from the widget gallery, headlessly, so the
// figure is about the ENGINE and not about a compositor or a driver.

#include <ctbrowser.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;
using clock_type = std::chrono::steady_clock;

namespace {

[[nodiscard]] std::string read_page(const char * path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

struct sample {
    double median = 0;
    double p95 = 0;
    double total = 0;
};

[[nodiscard]] sample summarise(std::vector<double> times) {
    sample out;
    if (times.empty()) { return out; }
    for (const double t : times) { out.total += t; }
    std::ranges::sort(times);
    out.median = times[times.size() / 2];
    out.p95 = times[static_cast<std::size_t>(0.95 * static_cast<double>(times.size() - 1))];
    return out;
}

// One measured operation: do the thing, then draw the frame it made necessary.
template <typename F> [[nodiscard]] sample measure(browser & page, int repeats, F && operation) {
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(repeats));
    for (int i = 0; i < repeats; ++i) {
        const auto began = clock_type::now();
        operation(i);
        (void)page.frame();
        times.push_back(
            std::chrono::duration<double, std::milli>(clock_type::now() - began).count());
    }
    return summarise(std::move(times));
}

} // namespace

int main() {
    const std::string html = read_page("examples/pages/widgets.html");
    if (html.empty()) {
        std::printf("bench_interaction: cannot read examples/pages/widgets.html\n");
        return 1;
    }

    browser_options options{640, 560};
    browser page{options};
    page.load_html(html);
    (void)page.frame();

    // Two points that are certainly in DIFFERENT elements, so every other move
    // is a real hover change - which is the case that costs.
    const float x1 = 40;
    const float y1 = 40;
    const float x2 = 60;
    const float y2 = 360;

    const sample idle = measure(page, 200, [&](int) {
        // The pointer moves but stays inside one element: the hover state does
        // not change and nothing should be redrawn at all.
        (void)page.handle(input_event::mouse_move_to(x1, y1 + 1));
        (void)page.handle(input_event::mouse_move_to(x1, y1));
    });
    const sample hover = measure(page, 200, [&](int i) {
        (void)page.handle(input_event::mouse_move_to(i % 2 == 0 ? x1 : x2, i % 2 == 0 ? y1 : y2));
    });
    const std::size_t layouts_before = page.layout_count();
    const sample scroll =
        measure(page, 200, [&](int i) { page.scroll_by(i % 2 == 0 ? 4.0f : -4.0f); });
    const std::size_t scroll_layouts = page.layout_count() - layouts_before;

    std::printf("\n  operation           median ms    p95 ms   implied CPU at 60fps\n");
    const auto row = [](const char * name, const sample & s) {
        std::printf("  %-18s %8.3f  %8.3f   %6.1f%% of one core\n", name, s.median, s.p95,
                    100.0 * s.median * 60.0 / 1000.0);
    };
    row("move, same element", idle);
    row("hover change", hover);
    row("scroll", scroll);
    std::printf("\n  layouts run by 200 scrolls: %zu (a scroll must not re-lay-out)\n",
                scroll_layouts);
    return 0;
}
