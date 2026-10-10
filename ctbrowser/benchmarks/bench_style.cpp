//===- ctbrowser/benchmarks/bench_style.cpp - Style resolution benchmark --===//
//
// Benchmark: style::engine (resolve each element once, then read every
// property) against ctcss::query (match the sheet once per property per
// element), on the same document and the same sheet.
//
// Both sides do the work a layout pass really needs: every property in `wanted`
// for every element. Charging ctcss::query for a single property would flatter
// it and measure nothing anybody runs.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/style/style.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <ctcss.hpp>

using namespace ctbrowser;
using clock_type = std::chrono::steady_clock;

namespace {

// The properties a layout pass actually asks for. ctcss pays one query() per
// element per entry; style::engine resolves each element once and reads them.
constexpr std::string_view wanted[] = {
    "display",          "width",     "height",      "margin",      "padding",    "color",
    "background-color", "font-size", "font-family", "font-weight", "text-align",
};

/// Builds a document with a realistic shape (nesting, classes, ids) rather than
/// a flat list, because depth is what the engine's ancestor filter speeds up.
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

/// Builds a sheet of ten matching rules plus 2 * `extra_rules` rules that match
/// nothing.
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
    // Bulk rules that match nothing. ctcss rescans them on every lookup;
    // style::engine files them in buckets it never consults, which is the
    // difference being measured.
    for (int i = 0; i < extra_rules; ++i) {
        out += ".unused" + std::to_string(i) + " { color: red }";
        out += "#none" + std::to_string(i) + " div { width: 1px }";
    }
    return out;
}

// Collect every element with its ancestor chain as ctcss::element_ref,
// root-first and self-last (the order ctcss::query expects).
//
// element_ref holds string_views, so the strings behind them must outlive the
// chain: they live in `owned`, and build_chain() is called only after every
// vector has stopped growing. Building element_ref from `std::string{...}`
// temporaries would dangle every view and silently produce wrong timings.

/// One element's tag, id and class attribute, owned.
struct owned_element {
    std::string tag, id, classes;
};
/// An element as ctcss sees it: its ancestor chain, owned, plus the views
/// into it that ctcss::query takes.
struct v1_element {
    std::vector<owned_element> owned; // Root-first.
    std::vector<ctcss::element_ref> chain;

    void build_chain() {
        chain.clear();
        chain.reserve(owned.size());
        for (const owned_element & o : owned) {
            chain.push_back(ctcss::element_ref{o.tag, o.id, o.classes, 0u});
        }
    }
};

/// Appends a v1_element for every element at or below `at`; `path` holds the
/// ancestors of `at` and is restored before returning.
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
    // Views are built only once every owning vector has stopped growing.
    for (v1_element & e : v1_elements) { e.build_chain(); }

    const ctcss::value_sheet v1_sheet = ctcss::parse_value(css);

    std::printf("style resolution: %zu elements, %zu rules, %zu properties each\n\n",
                v1_elements.size(), styles.rule_count(), std::size(wanted));

    // --- ctcss: query() per property per element -----------------------
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

    // --- style::engine: resolve each element once -----------------------
    std::size_t resolve_hits = 0;
    const auto t1 = clock_type::now();
    const style::style_map resolved = styles.resolve_all(txn);
    for (const auto & [key, style] : resolved) {
        for (const std::string_view property : wanted) {
            if (!style->get(atoms.intern_lower(property)).empty()) {
                ++resolve_hits;
                continue;
            }
            // Shorthands are gone by now, correctly: the engine expands
            // `margin: 4px` into four longhands at parse time so the cascade
            // orders them properly (style/engine.hpp says why). Asking for
            // `margin` therefore finds nothing, while ctcss::query, which never
            // expands, does find it. So count through the engine's own
            // expansion rather than a second copy of that knowledge here.
            for (const auto & [longhand, ignored] :
                 style::engine::expand_shorthand(property, "0")) {
                (void)ignored;
                if (!style->get(atoms.intern_lower(longhand)).empty()) {
                    ++resolve_hits;
                    break;
                }
            }
        }
    }
    const auto resolve_time = clock_type::now() - t1;

    const double query_ms = std::chrono::duration<double, std::milli>(query_time).count();
    const double resolve_ms = std::chrono::duration<double, std::milli>(resolve_time).count();

    std::printf("  ctcss query() per property  %8.2f ms\n", query_ms);
    std::printf("  resolve once + read        %8.2f ms   %.1fx faster\n", resolve_ms,
                query_ms / resolve_ms);
    // Both sides count the same declarations (shorthands counted through the
    // engine's own expansion), so DISAGREE means a real difference, such as the
    // dangling element_ref views described above collect_v1().
    std::printf("\n  declarations found: query=%zu resolve=%zu %s\n", query_hits, resolve_hits,
                query_hits == resolve_hits
                    ? "(agree)"
                    : "(DISAGREE - the two paths found different declarations)");
    std::printf("  distinct computed styles: %zu (for %zu elements)\n",
                styles.styles().distinct_styles(), resolved.size());
    return 0;
}
