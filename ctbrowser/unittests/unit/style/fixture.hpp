#pragma once
// The fixture the style_*.cpp files share: build a document, resolve it, and
// ask for one element's value. It was unit/style_basics.cpp's until that file
// was split on 2026-09-08; the includes and using-directives are the ones that
// file had at file scope, so every file here sees exactly what that one saw.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"

#include <cstdio>
#include <string>
#include <string_view>

using namespace ctbrowser;
using namespace ctbrowser::style;

// Build a document, resolve it, and let a test ask for one element's value.
struct fixture {
    atom_table atoms;
    document doc{atoms};
    engine styles{atoms};
    style_map resolved;

    void load(std::string_view html, std::string_view css, std::string_view ua = {}) {
        (void)parse_html(doc, html);
        if (!ua.empty()) { styles.add_sheet(ua, 0); } // user agent origin
        styles.add_sheet(css, 1);                     // author origin
        const auto txn = doc.read();
        resolved = styles.resolve_all(txn);
    }

    [[nodiscard]] node_id find(std::string_view tag_name) {
        const auto txn = doc.read();
        const atom want = atoms.intern_lower(tag_name);
        node_id found{};
        const auto walk = [&](auto && self, node_id at) -> void {
            if (txn.kind(at).value_or(node_kind::text) == node_kind::element &&
                txn.tag(at).value_or(atom{}) == want && !found) {
                found = at;
            }
            for (const node_id c : txn.children(at)) { self(self, c); }
        };
        walk(walk, txn.root());
        return found;
    }
    [[nodiscard]] node_id find_id(std::string_view want_id) {
        const auto txn = doc.read();
        const atom key = atoms.intern("id");
        node_id found{};
        const auto walk = [&](auto && self, node_id at) -> void {
            if (txn.attribute_value(at, key) == want_id && !found) { found = at; }
            for (const node_id c : txn.children(at)) { self(self, c); }
        };
        walk(walk, txn.root());
        return found;
    }

    [[nodiscard]] std::string value_of(node_id n, std::string_view property) {
        const auto it = resolved.find(engine::key_of(n));
        if (it == resolved.end() || !it->second) { return "<unresolved>"; }
        return std::string{it->second->get(atoms.intern_lower(property))};
    }
    [[nodiscard]] computed_style_ptr style_of(node_id n) {
        const auto it = resolved.find(engine::key_of(n));
        return it == resolved.end() ? computed_style_ptr{} : it->second;
    }
};

inline void expect_value(fixture & f, node_id n, std::string_view property, std::string_view want,
                         std::string_view what) {
    const std::string got = f.value_of(n, property);
    if (got != want) {
        std::printf("FAIL %-38s %s => '%s' (want '%s')\n", std::string{what}.c_str(),
                    std::string{property}.c_str(), got.c_str(), std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}
