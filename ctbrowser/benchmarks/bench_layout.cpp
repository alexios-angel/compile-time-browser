//===- ctbrowser/benchmarks/bench_layout.cpp - Layout benchmark -----------===//
//
// Measures building the box tree and running the layout pass, on generated
// documents of increasing size at a fixed viewport.
//
// Style resolution is excluded: bench_style measures it, and including it here
// would make this a second, noisier copy of that benchmark. Styles are resolved
// once, before timing starts.
//
// Building the box tree is reported separately from laying it out because they
// are paid at different times: a restyle pays build plus layout, while a resize
// pays layout alone.
//
//===----------------------------------------------------------------------===//

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

/// Counts element fragments under `f`, not all fragments. A wrapped paragraph
/// produces one fragment per visual line, all with the same source node, so a
/// fragment count would vary with the viewport. Skipping text boxes keeps the
/// number comparable between runs.
std::size_t count_elements(const ctbrowser::layout::fragment & f) {
    using ctbrowser::layout::box_kind;
    std::size_t total = (f.source && f.box != nullptr && f.box->kind != box_kind::text) ? 1u : 0u;
    for (const auto & c : f.children) { total += count_elements(c); }
    return total;
}

/// count_elements() minus the outermost box, which stands for the document
/// rather than an element.
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

    // Print the element count so a timing can be checked against the whole
    // document having been laid out.
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
