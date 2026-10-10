//===- unittests/unit/page/static_parity.cpp - static_page vs browser -----===//
//
// page::static_page loads and lays out a document with no script engine, so a
// test or a native program can ask it what getComputedStyle and
// getBoundingClientRect would answer. That is only useful if the answers are
// the browser's. This test loads each Bootstrap fixture both ways, the way
// bootstrap_layout.cpp loads it, and compares every element: its computed
// style entries, its bounding client rect, and the viewport's client size.
//
// Both sides run the same query functions (ctbrowser/page/), so what this
// compares is the trees they run over: the parse, the author sheets, the
// cascade and the layout.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>
#include <ctbrowser/page/page.hpp>

#include "check.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;

namespace {

/// bootstrap_layout.cpp's viewport and fixtures.
constexpr int viewport_width = 1024;
constexpr int viewport_height = 768;
constexpr std::string_view fixtures[] = {"box",        "type",     "grid",
                                         "components", "position", "kitchen"};

/// Every element of `doc` in tree order.
[[nodiscard]] std::vector<node_id> elements_of(const document & doc) {
    std::vector<node_id> out;
    const auto txn = doc.read();
    const auto walk = [&](auto && self, node_id at) -> void {
        if (txn.kind(at).value_or(node_kind::text) == node_kind::element) { out.push_back(at); }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return out;
}

[[nodiscard]] rect bounding_of(const page::page_view & view, node_id id) {
    std::vector<rect> rects;
    for (const page::box_geometry & box : page::client_boxes(view, id, "border")) {
        rects.push_back(box.bounding_rect());
    }
    return page::bounding_client_rect(rects);
}

[[nodiscard]] std::string text_of(const rect & r) {
    return std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.width) + "x" +
           std::to_string(r.height);
}

void compare_fixture(std::string_view name) {
    const std::string label = "bootstrap-" + std::string{name};
    const std::string html = read_file(std::filesystem::path{"examples/pages"} / (label + ".html"));
    check(!html.empty(), label + ": the fixture reads");
    if (html.empty()) { return; }

    // The browser, set up as bootstrap_layout.cpp sets it up.
    shell::browser vm{shell::browser_options{viewport_width, viewport_height}};
    vm.assets().set_base_path("examples/pages");
    vm.assets().set_confine(false);
    vm.load_html(html);
    vm.frame();

    page::static_page native{page::static_page_options{viewport_width, viewport_height}};
    native.assets().set_base_path("examples/pages");
    native.assets().set_confine(false);
    native.load_html(html);
    native.frame();

    check(vm.style_error().empty(), label + ": the browser loads its stylesheet");
    check(native.style_error() == vm.style_error(), label + ": the same style error");

    const page::page_view theirs = vm.bindings().view_of_page();
    const page::page_view ours = native.view();
    check(ours.viewport_width == theirs.viewport_width,
          label + ": clientWidth " + std::to_string(ours.viewport_width) + " against " +
              std::to_string(theirs.viewport_width));
    check(native.viewport_client_width() == theirs.viewport_width, label + ": viewport width");
    check(native.viewport_client_height() == theirs.viewport_height, label + ": viewport height");

    const std::vector<node_id> a = elements_of(vm.doc());
    const std::vector<node_id> b = elements_of(native.doc());
    check(a.size() == b.size(), label + ": the same number of elements (" +
                                    std::to_string(b.size()) + " against " +
                                    std::to_string(a.size()) + ")");
    if (a.size() != b.size()) { return; }

    // Name the first difference only: one moved box moves everything after it,
    // and the first is the one to read.
    for (std::size_t i = 0; i < a.size(); ++i) {
        const std::string where = label + " element " + std::to_string(i) + " <" +
                                  std::string{native.doc().read().local_name(b[i])} + ">";
        const rect want = bounding_of(theirs, a[i]);
        const rect got = bounding_of(ours, b[i]);
        if (!(want == got)) {
            check(false, where + ": rect " + text_of(got) + " against " + text_of(want));
            return;
        }
        const auto want_style = page::computed_style_entries(theirs, a[i]);
        const auto got_style = native.computed_style(b[i]);
        if (want_style != got_style) {
            std::string detail = "the entry lists differ in size";
            for (std::size_t p = 0; p < std::min(want_style.size(), got_style.size()); ++p) {
                if (want_style[p] != got_style[p]) {
                    detail = got_style[p].first + ": " + got_style[p].second + " against " +
                             want_style[p].first + ": " + want_style[p].second;
                    break;
                }
            }
            check(false, where + ": computed style, " + detail);
            return;
        }
    }
}

} // namespace

int main() {
    for (const std::string_view name : fixtures) { compare_fixture(name); }
    REPORT("page_static_parity");
}
