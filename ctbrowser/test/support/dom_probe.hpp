#pragma once
// Asking a live page where something ENDED UP. Shared because it was copied.
//
// `find_id` and `box_of` were byte-identical in unittests/unit/chrome_basics.cpp (now
// control_editing.cpp and its siblings) and
// unittests/unit/widgets_basics.cpp, and unittests/unit/bootstrap_layout.cpp wanted a
// third copy. That is the trigger CLAUDE.md names: "small shared algorithms live
// in core/algorithms.hpp - everything there had at least three copies before it
// moved". This is the test suite's equivalent of that shelf.
//
// It stays in test/support rather than becoming engine API on purpose. A page
// finds an element with `document.getElementById`; only a TEST wants to walk the
// document for an id without a script context, and only a test wants the raw
// fragment rectangle rather than `getBoundingClientRect`.

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser.hpp>

#include "check.hpp"

namespace ctbrowser_test {

// ONE EXPRESSION against a FRESH page, and whatever it logged. Fresh because
// most DOM cases mutate the document, and a case that changed the tree for
// the next one would report a failure in the wrong place. The `try` is not
// politeness: a thrown exception answers as "threw:<name>", which is what half
// the DOM tests assert on, and without it a fence regression would look like
// the answer. Eleven tests carried this before it moved here.
[[nodiscard]] inline std::string answer_in(std::string html, const std::string & expression) {
    ctbrowser::browser page{ctbrowser::browser_options{400, 300}};
    const std::string tail = "<script>try { console.log(String(" + expression +
                             ")); } catch (e) { console.log('threw:' + e.name); }</script>";
    html.insert(html.find("</body>"), tail);
    page.load_html(html);
    const std::vector<std::string> & logged = page.bindings().console_output();
    if (logged.empty()) { return "<nothing logged: " + page.script_error() + ">"; }
    return logged.back();
}

inline void is_in(const std::string & html, const std::string & expression,
                  const std::string & expected) {
    const std::string got = answer_in(html, expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

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
