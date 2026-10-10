//===- ctbrowser/test/support/dom_probe.hpp - Live-page query helpers -----===//
//
// Test helpers that query a live page: run one expression on a fresh page,
// collect alerts, find an element by id, and get its laid-out box.
//
// They stay in test/support rather than engine API: a page uses
// document.getElementById and getBoundingClientRect; only a test walks the
// document without a script context or wants the raw fragment rectangle.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser.hpp>

#include "check.hpp"

namespace ctbrowser_test {

/// Evaluates one expression on a fresh page built from `html` and returns what
/// it logged, as a string. Fresh, because most DOM cases mutate the document
/// and must not affect the next case. A thrown exception logs as
/// "threw:<name>", which many DOM tests assert on.
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

/// Checks that `expression` evaluated in `html` answers `expected`
/// (see answer_in()), printing the expression on failure.
inline void is_in(const std::string & html, const std::string & expression,
                  const std::string & expected) {
    const std::string got = answer_in(html, expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

/// Returns the alerts of a fresh page with `html` loaded, joined with `;`. A
/// script error is appended as `|error:<message>` rather than swallowed, so a
/// page that threw halfway does not just look like a short list.
[[nodiscard]] inline std::string said(std::string_view html) {
    ctbrowser::browser page{ctbrowser::browser_options{200, 100}};
    page.load_html(html);
    std::string out;
    for (const std::string & one : page.alerts()) {
        if (!out.empty()) { out += ';'; }
        out += one;
    }
    if (!page.script_error().empty()) { out += "|error:" + page.script_error(); }
    return out;
}

/// Returns the first element whose `id` attribute is `want`, in document order.
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

/// Returns the absolute box of the first non-empty fragment for `id`, so a test
/// can click a control rather than guess where it is. An element that breaks
/// across lines has several fragments, and an empty leading one is not where a
/// click should go.
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
