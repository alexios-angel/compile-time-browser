#pragma once
// Asking a live page where something ENDED UP. Shared because it was copied.
//
// `find_id` and `box_of` were byte-identical in tests/unit/chrome_basics.cpp and
// tests/unit/widgets_basics.cpp, and tests/unit/bootstrap_layout.cpp wanted a
// third copy. That is the trigger CLAUDE.md names: "small shared algorithms live
// in core/algorithms.hpp - everything there had at least three copies before it
// moved". This is the tests/ equivalent of that shelf.
//
// It stays in tests/support rather than becoming engine API on purpose. A page
// finds an element with `document.getElementById`; only a TEST wants to walk the
// document for an id without a script context, and only a test wants the raw
// fragment rectangle rather than `getBoundingClientRect`.

#include <string>
#include <string_view>

#include <ctbrowser.hpp>

namespace ctbrowser_test {

// The first element whose `id` attribute is `want`, in document order.
[[nodiscard]] inline ctbrowser::node_id find_id(ctbrowser::browser & page, std::string_view want) {
    const auto txn = page.doc().read();
    const ctbrowser::atom key = page.atoms().intern("id");
    ctbrowser::node_id found{};
    const auto walk = [&](auto && self, ctbrowser::node_id at) -> void {
        if (!found && txn.attribute_value(at, key) == want) { found = at; }
        for (const ctbrowser::node_id c : txn.children(at)) { self(self, c); }
    };
    walk(walk, txn.root());
    return found;
}

// The absolute box of the first fragment for `id`, so a test can click a control
// rather than guessing where it is.
//
// FIRST NON-EMPTY fragment, not first fragment: an element that breaks across
// lines has several, and an empty leading one is not where a click should go.
[[nodiscard]] inline ctbrowser::rect box_of(ctbrowser::browser & page, std::string_view id) {
    const ctbrowser::node_id want = find_id(page, id);
    const auto walk = [&](auto && self, const ctbrowser::layout::fragment & f, float dx,
                          float dy) -> ctbrowser::rect {
        const ctbrowser::rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width,
                                  f.bounds.height};
        if (f.source == want && !box.empty()) { return box; }
        for (const auto & child : f.children) {
            if (const ctbrowser::rect hit = self(self, child, box.x, box.y); !hit.empty()) {
                return hit;
            }
        }
        return ctbrowser::rect{};
    };
    return walk(walk, page.fragments(), 0, 0);
}

} // namespace ctbrowser_test
