// Style resolution: the engine against the previous engine, on the same document and the same
// sheet.
//
// This is the first stage where the plan's "high-performance" claim can be
// checked rather than asserted, so it is measured against the thing being
// replaced rather than against nothing.
//
// The two engines are asked to do the SAME WORK, which needs care to be fair:
// layout does not want one property, it wants all of them, so the previous engine is charged
// for the query() calls it would really make (one per property per element)
// and the engine for resolving each element once. Charging the previous engine for a single
// property would flatter it and measure nothing anybody runs.

#include <ctbrowser/core/core.hpp>
import ctbrowser.dom;
import ctbrowser.style;

#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <ctcss.hpp>

using namespace ctbrowser;
using clock_type = std::chrono::steady_clock;

namespace {

// The properties a layout pass actually asks for. the previous engine pays query() per element
// per entry in this list; the engine resolves the element once and reads them out.
constexpr std::string_view wanted[] = {
    "display",          "width",     "height",      "margin",      "padding",    "color",
    "background-color", "font-size", "font-family", "font-weight", "text-align",
};

// A document with realistic shape: nested structure, classes, ids - not a flat
// list, because depth is what the ancestor filter exists for.
[[nodiscard]] std::string build_html(int sections, int rows) {
    std::string out = "<body class=page><div id=root class=wrap>";
    for (int s = 0; s < sections; ++s) {
        out += "<section class='panel s" + std::to_string(s % 8) +
               "'><header><h2>T</h2></header><ul class=list>";
        for (int r = 0; r < rows; ++r) {
            out += "<li class='row r" + std::to_string(r % 4) +
                   "'><span class=label>L</span><a class=link href=#>go</a></li>";
        }
        out += "</ul></section>";
    }
    out += "</div></body>";
    return out;
}

[[nodiscard]] std::string build_css(int extra_rules) {
    std::string out = "body { font-family: serif; color: black }"
                      ".wrap { display: block; padding: 8px }"
                      ".panel { display: block; margin: 4px }"
                      ".list { display: block }"
                      ".row { display: block; height: 20px }"
                      ".label { color: gray; font-size: 12px }"
                      ".link { color: blue; text-align: left }"
                      "#root .panel .row .link { font-weight: bold }"
                      "section header h2 { font-size: 18px }"
                      "ul li span { background-color: white }";
    // Bulk rules that match nothing. the previous engine rescans them for every lookup; the engine
    // files them in buckets it never consults. That difference IS the point.
    for (int i = 0; i < extra_rules; ++i) {
        out += ".unused" + std::to_string(i) + " { color: red }";
        out += "#none" + std::to_string(i) + " div { width: 1px }";
    }
    return out;
}

// Collect every element with its ancestor chain in the previous engine's element_ref form,
// root-first and self-last, which is the order the previous engine documents.
//
// element_ref holds STRING_VIEWS, so the strings behind them have to outlive
// the chain. The obvious version of this function - constructing element_ref
// from `std::string{...}` temporaries - dangles every view immediately, and
// the previous engine then matches against freed memory. It looked like it worked and produced
// a flatteringly wrong benchmark; the declaration-count cross-check below is
// what exposed it. Hence the owned strings.
struct owned_element {
    std::string tag, id, classes;
};
struct v1_element {
    std::vector<owned_element> owned; // root-first
    std::vector<ctcss::element_ref> chain;

    void build_chain() {
        chain.clear();
        chain.reserve(owned.size());
        for (const owned_element & o : owned) {
            chain.push_back(ctcss::element_ref{o.tag, o.id, o.classes, 0u});
        }
    }
};

void collect_v1(const read_txn & txn, atom_table & atoms, node_id at,
                std::vector<owned_element> & path, std::vector<v1_element> & out) {
    const bool is_element = txn.kind(at).value_or(node_kind::text) == node_kind::element;
    if (is_element) {
        path.push_back(owned_element{std::string{atoms.text(txn.tag(at).value_or(atom{}))},
                                     std::string{txn.attribute_value(at, atoms.intern("id"))},
                                     std::string{txn.attribute_value(at, atoms.intern("class"))}});
        out.push_back(v1_element{path, {}});
    }
    for (const node_id c : txn.children(at)) { collect_v1(txn, atoms, c, path, out); }
    if (is_element) { path.pop_back(); }
}

} // namespace

int main() {
    constexpr int sections = 60;
    constexpr int rows = 25;
    constexpr int filler = 300;

    atom_table atoms;
    document doc{atoms};
    const std::string html = build_html(sections, rows);
    const std::string css = build_css(filler);
    (void)parse_html(doc, html);

    style::engine styles{atoms};
    styles.add_sheet(css, 1);

    const auto txn = doc.read();
    std::vector<owned_element> path;
    std::vector<v1_element> v1_elements;
    collect_v1(txn, atoms, txn.root(), path, v1_elements);
    // views are built only once every owning vector has stopped growing
    for (v1_element & e : v1_elements) { e.build_chain(); }

    const ctcss::value_sheet v1_sheet = ctcss::parse_value(css);

    std::printf("style resolution: %zu elements, %zu rules, %zu properties each\n\n",
                v1_elements.size(), styles.rule_count(), std::size(wanted));

    // --- the previous engine: query() per property per element -----------------------------
    std::size_t query_hits = 0;
    const auto t0 = clock_type::now();
    for (const v1_element & e : v1_elements) {
        for (const std::string_view property : wanted) {
            const std::string_view v =
                ctcss::query(v1_sheet, e.chain.data(), e.chain.size(), property);
            if (!v.empty()) { ++query_hits; }
        }
    }
    const auto query_time = clock_type::now() - t0;

    // --- resolve each element once ------------------------------------
    std::size_t resolve_hits = 0;
    const auto t1 = clock_type::now();
    const style::style_map resolved = styles.resolve_all(txn);
    for (const auto & [key, style] : resolved) {
        for (const std::string_view property : wanted) {
            if (!style->get(atoms.intern_lower(property)).empty()) { ++resolve_hits; }
        }
    }
    const auto resolve_time = clock_type::now() - t1;

    const double query_ms = std::chrono::duration<double, std::milli>(query_time).count();
    const double resolve_ms = std::chrono::duration<double, std::milli>(resolve_time).count();

    std::printf("  ctcss query() per property  %8.2f ms\n", query_ms);
    std::printf("  resolve once + read        %8.2f ms   %.1fx faster\n", resolve_ms,
                query_ms / resolve_ms);
    std::printf("\n  declarations found: query=%zu resolve=%zu %s\n", query_hits, resolve_hits,
                query_hits == resolve_hits ? "(agree)"
                                           : "(DISAGREE - the comparison is not like-for-like)");
    std::printf("  distinct computed styles: %zu (for %zu elements)\n",
                styles.styles().distinct_styles(), resolved.size());
    return 0;
}
