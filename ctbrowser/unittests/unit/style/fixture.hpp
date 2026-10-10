//===- fixture.hpp - Shared style test fixture ------------------*- C++ -*-===//
//
// The fixture the tests in unittests/unit/style/ share: build a document,
// resolve its styles, and ask for one element's computed value. The
// using-directives are at file scope on purpose, so every including test sees
// the ctbrowser and ctbrowser::style names unqualified.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"

#include <cstdio>
#include <string>
#include <string_view>

using namespace ctbrowser;
using namespace ctbrowser::style;

/// Builds a document, resolves its styles, and lets a test ask for one
/// element's computed value.
struct fixture {
    atom_table atoms;
    document doc{atoms};
    engine styles{atoms};
    style_map resolved;

    /// Parses \p html into the document, adds \p css as the author sheet (and
    /// \p ua, if given, as the user-agent sheet), and resolves every element.
    void load(std::string_view html, std::string_view css, std::string_view ua = {}) {
        (void)parse_html(doc, html);
        if (!ua.empty()) { styles.add_sheet(ua, 0); } // User agent origin.
        styles.add_sheet(css, 1);                     // Author origin.
        const auto txn = doc.read();
        resolved = styles.resolve_all(txn);
    }

    /// Returns the first element in tree order whose tag is \p tag_name
    /// (case-insensitive), or a null node_id.
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
    /// Returns the first node whose `id` attribute is \p want_id, or a null
    /// node_id.
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

    /// Returns \p property's computed value on \p n, or "<unresolved>" if the
    /// last load() produced no style for it.
    [[nodiscard]] std::string value_of(node_id n, std::string_view property) {
        const auto it = resolved.find(engine::key_of(n));
        if (it == resolved.end() || !it->second) { return "<unresolved>"; }
        return std::string{it->second->get(atoms.intern_lower(property))};
    }
    /// Returns \p n's computed style, or null if it has none.
    [[nodiscard]] computed_style_ptr style_of(node_id n) {
        const auto it = resolved.find(engine::key_of(n));
        return it == resolved.end() ? computed_style_ptr{} : it->second;
    }
};

/// Records a test failure, naming \p what, unless \p property's computed
/// value on \p n is exactly \p want.
inline void expect_value(fixture & f, node_id n, std::string_view property, std::string_view want,
                         std::string_view what) {
    const std::string got = f.value_of(n, property);
    if (got != want) {
        std::printf("FAIL %-38s %s => '%s' (want '%s')\n", std::string{what}.c_str(),
                    std::string{property}.c_str(), got.c_str(), std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}
