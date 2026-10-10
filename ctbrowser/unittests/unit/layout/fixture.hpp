#pragma once
// The fixture the layout_*.cpp, flex_basics.cpp and position_basics.cpp files
// share - a document, its styles and its box tree - and the prose assertions
// every one of them is written with. They were unit/layout_basics.cpp's until
// that file was split on 2026-09-08; the includes and using-directives are the
// ones that file had at file scope, so every file here sees exactly what that
// one saw.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

using namespace ctbrowser;
using namespace ctbrowser::layout;

// A document, its styles and its box tree - the whole front half of the
// pipeline, which is what layout consumes.
struct fixture {
    atom_table atoms;
    document doc{atoms};
    style::engine styles{atoms};
    style::style_map resolved;
    box_node root;

    void load(std::string_view html, std::string_view css) {
        (void)parse_html(doc, html);
        styles.add_sheet(css, 1);
        const auto txn = doc.read();
        resolved = styles.resolve_all(txn);
        box_builder builder{atoms, resolved};
        root = builder.build(txn, txn.root());
    }

    [[nodiscard]] node_id find_id(std::string_view want) {
        const auto txn = doc.read();
        const atom key = atoms.intern("id");
        node_id found{};
        const auto walk = [&](auto && self, node_id at) -> void {
            if (!found && txn.attribute_value(at, key) == want) { found = at; }
            for (const node_id c : txn.children(at)) { self(self, c); }
        };
        walk(walk, txn.root());
        return found;
    }
};

// Find a box by the element it came from.
inline const box_node * box_for(const box_node & at, node_id id) {
    if (at.source == id) { return &at; }
    for (const box_node & c : at.children) {
        if (const box_node * hit = box_for(c, id)) { return hit; }
    }
    return nullptr;
}

inline std::size_t count_kind(const box_node & at, box_kind kind) {
    std::size_t n = at.kind == kind ? 1u : 0u;
    for (const box_node & c : at.children) { n += count_kind(c, kind); }
    return n;
}

inline bool near(float a, float b) {
    return std::fabs(a - b) < 0.01f;
}

inline void expect_near(float got, float want, std::string_view what) {
    if (!near(got, want)) {
        std::printf("FAIL %-44s got %.3f want %.3f\n", std::string{what}.c_str(),
                    static_cast<double>(got), static_cast<double>(want));
        ++ctbrowser_test_failures;
    }
}
