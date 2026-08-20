// Layout: the box tree, the sequential pass and the parallel pass, on the same
// document at the same viewport.
//
// STYLE IS EXCLUDED. Resolving a style per property per element is what
// bench_style measures; leaving it in would make this a second, noisier copy
// of that benchmark. The styles here come out of a precomputed table.
//
// BUILDING THE BOX TREE IS REPORTED SEPARATELY from laying it out, because
// they are paid at different times: a restyle pays build + layout, while a
// resize or a scroll pays layout alone - and real frames are overwhelmingly
// the second.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/style/style.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using clock_type = std::chrono::steady_clock;

namespace {

// Realistic shape: nested containers with text, not a flat list. Depth and
// text are what layout actually spends its time on.
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

constexpr std::string_view sheet = "body { margin: 0; padding: 0 }"
                                   "section { display: block; margin: 4px; padding: 2px }"
                                   "h2 { display: block; font-size: 18px; margin: 2px }"
                                   "ul { display: block; padding: 8px }"
                                   "li { display: block; font-size: 14px; margin: 1px }";

template <typename F> [[nodiscard]] double time_ms(int reps, F && f) {
    const auto start = clock_type::now();
    for (int i = 0; i < reps; ++i) { f(); }
    const auto end = clock_type::now();
    return std::chrono::duration<double, std::milli>(end - start).count() / reps;
}

// Count ELEMENTS, not fragments. A wrapped paragraph produces one fragment per
// visual line, all carrying the same source node, so counting fragments counts
// line breaks rather than content and moves with the viewport. Filtering to
// non-text boxes is what makes the number comparable between runs.
std::size_t count_elements(const ctbrowser::layout::fragment & f) {
    using ctbrowser::layout::box_kind;
    std::size_t total = (f.source && f.box != nullptr && f.box->kind != box_kind::text) ? 1u : 0u;
    for (const auto & c : f.children) { total += count_elements(c); }
    return total;
}

// ...minus the outermost box, which stands for the document rather than for an
// element, so that this counts what the page has rather than what layout
// wraps it in.
std::size_t count_document_elements(const ctbrowser::layout::fragment & root) {
    const std::size_t all = count_elements(root);
    return all == 0 ? 0 : all - 1;
}

void run_case(int sections, int rows, std::int32_t viewport, ctbrowser::scheduler & pool) {
    using namespace ctbrowser;
    const std::string html = build_html(sections, rows);

    atom_table atoms;
    ::ctbrowser::document doc{atoms};
    (void)parse_html(doc, html);
    style::engine styles{atoms};
    styles.add_sheet(sheet, 1);
    const auto txn = doc.read();
    const style::style_map resolved = styles.resolve_all(txn);

    const double build_ms = time_ms(20, [&] {
        layout::box_builder b{atoms, resolved};
        const layout::box_node t = b.build(txn, txn.root());
        if (t.children.empty()) { std::printf("(empty)"); } // keep the build honest
    });
    layout::box_builder builder{atoms, resolved};
    const layout::box_node tree = builder.build(txn, txn.root());

    // One square glyph per code point at the font size: a deterministic
    // stand-in, so the line-breaking work is identical from run to run.
    layout::engine eng{layout::monospace_measure(1.0f)};
    eng.parallel_min_boxes = 0; // measure the parallel path itself, not the threshold
    const double seq_ms = time_ms(20, [&] { (void)eng.run(tree, static_cast<float>(viewport)); });
    const double par_ms =
        time_ms(20, [&] { (void)eng.run_parallel(tree, static_cast<float>(viewport), pool); });

    // The style benchmark taught this the hard way: a timing is only worth
    // reading once the run is shown to have processed the whole document.
    const layout::fragment out = eng.run(tree, static_cast<float>(viewport));
    const std::size_t placed = count_document_elements(out);
    const layout::box_node * split = layout::engine::split_point(&tree);

    // `boxes` is what the parallel threshold is expressed in, so it is the
    // column to read when tuning engine::parallel_min_boxes.
    std::printf("%4d x %-4d %6zu %6zu %7zu %6zu  %8.3f %8.3f %8.3f  %6.2fx\n", sections, rows,
                placed, tree.descendant_count(), out.count(),
                split == nullptr ? 0u : split->children.size(), build_ms, seq_ms, par_ms,
                seq_ms / par_ms);
}

} // namespace

int main() {
    ctbrowser::scheduler pool;
    std::printf("layout, viewport 900px, %zu pool workers + the calling thread\n\n",
                pool.worker_count());
    std::printf("%-12s %6s %6s %7s %6s  %8s %8s %8s  %7s\n", "  document", "placed", "boxes",
                "frags", "split", "build", "seq", "par", "par/seq");
    std::printf("%s\n", std::string(86, '-').c_str());
    for (const auto [sections, rows] :
         {std::pair{4, 5}, std::pair{10, 10}, std::pair{16, 14}, std::pair{24, 18},
          std::pair{40, 25}, std::pair{70, 32}, std::pair{120, 40}, std::pair{300, 60}}) {
        run_case(sections, rows, 900, pool);
    }
    std::printf("\nbuild is the box tree: a resize pays par alone, a restyle pays build + par.\n\n"
                "Parallel tops out near 1.8x on 21 workers, which is not good scaling. The\n"
                "likely reason is that the pass is allocation-bound, not compute-bound: the\n"
                "largest case produces 55k fragments in 4.4 ms - about 80 ns each - and every\n"
                "fragment is a std::string plus a std::vector, so a pass is ~110k malloc/free\n"
                "pairs contending across threads. That is a hypothesis the numbers are\n"
                "consistent with, not something measured directly. Arena-allocating the\n"
                "fragment tree is the obvious next lever and belongs with the paint stage\n"
                "that consumes it.\n");
    return 0;
}
