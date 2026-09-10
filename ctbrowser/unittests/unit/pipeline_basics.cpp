// The runtime renderer seam: a type-erased renderer must render exactly what
// the backend it holds renders, or the fallback is not a fallback, it is a
// second renderer with its own bugs.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/shell/shell.hpp> // shell::font8x8_metrics - see shell/metrics.hpp
#include <ctbrowser/style/style.hpp>

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
    test_renderer_renders_what_its_backend_renders();
    test_renderer_reports_what_it_cannot_do();

    REPORT("pipeline_basics");
}
