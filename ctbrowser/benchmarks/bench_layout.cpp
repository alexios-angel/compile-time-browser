// Layout: the box tree and the layout pass, on the same document at the same
// viewport.
//
// STYLE IS EXCLUDED. Resolving a style per property per element is what
// bench_style measures; leaving it in would make this a second, noisier copy
// of that benchmark. The styles here come out of a precomputed table.
//
// BUILDING THE BOX TREE IS REPORTED SEPARATELY from laying it out, because
// they are paid at different times: a restyle pays build + layout, while a
// resize or a scroll pays layout alone - and real frames are overwhelmingly
// the second.

#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/style/style.hpp>

#include "bench_fixture.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view sheet = "body { margin: 0; padding: 0 }"
                                   "section { display: block; margin: 4px; padding: 2px }"
                                   "h2 { display: block; font-size: 18px; margin: 2px }"
                                   "ul { display: block; padding: 8px }"
                                   "li { display: block; font-size: 14px; margin: 1px }";

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

void run_case(int sections, int rows, std::int32_t viewport) {
    using namespace ctbrowser;
    const std::string html = bench::build_html(sections, rows);

    atom_table atoms;
    ::ctbrowser::document doc{atoms};
    (void)parse_html(doc, html);
    style::engine styles{atoms};
    styles.add_sheet(sheet, 1);
    const auto txn = doc.read();
    const style::style_map resolved = styles.resolve_all(txn);

    const double build_ms = bench::time_ms(20, [&] {
        layout::box_builder b{atoms, resolved};
        const layout::box_node t = b.build(txn, txn.root());
        if (t.children.empty()) { std::printf("(empty)"); } // keep the build honest
    });
    layout::box_builder builder{atoms, resolved};
    const layout::box_node tree = builder.build(txn, txn.root());

    // One square glyph per code point at the font size: a deterministic
    // stand-in, so the line-breaking work is identical from run to run.
    const layout::engine eng{layout::monospace_measure(1.0f)};
    const double seq_ms =
        bench::time_ms(20, [&] { (void)eng.run(tree, static_cast<float>(viewport)); });

    // The style benchmark taught this the hard way: a timing is only worth
    // reading once the run is shown to have processed the whole document.
    const layout::fragment out = eng.run(tree, static_cast<float>(viewport));
    const std::size_t placed = count_document_elements(out);
    std::printf("%4d x %-4d %6zu %7zu  %8.3f %8.3f\n", sections, rows, placed, out.count(),
                build_ms, seq_ms);
}

} // namespace

int main() {
    std::printf("layout, viewport 900px\n\n");
    std::printf("%-12s %6s %7s  %8s %8s\n", "  document", "placed", "frags", "build", "layout");
    std::printf("%s\n", std::string(48, '-').c_str());
    for (const auto [sections, rows] :
         {std::pair{4, 5}, std::pair{10, 10}, std::pair{16, 14}, std::pair{24, 18},
          std::pair{40, 25}, std::pair{70, 32}, std::pair{120, 40}, std::pair{300, 60}}) {
        run_case(sections, rows, 900);
    }
    std::printf("\nbuild is the box tree: a resize pays layout alone, a restyle pays both.\n");
    return 0;
}
