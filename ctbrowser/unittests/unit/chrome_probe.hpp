#pragma once
// A page's paint list and control state, asked about from a test - the helpers
// more than one of the five files carved out of chrome_basics.cpp on 2026-09-07
// needs. Verbatim from that file but for `inline` and the namespace, which is
// why the using-directive below is here: the bodies name `rect`, `node_id` and
// `paint::` the way a file with `using namespace ctbrowser` at its top does.
// Private to unittests/unit/, like the tests themselves.

#include <ctbrowser.hpp>

#include "check.hpp"
#include "dom_probe.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser_test {

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::input_event;

inline void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %s\n", std::string{what}.c_str());
        ++ctbrowser_test_failures;
    }
}

[[nodiscard]] inline std::vector<paint::paint_command> commands(browser & page) {
    std::vector<paint::paint_command> out;
    for (const auto & layer : page.layers().layers) {
        if (!layer.contents) { continue; }
        for (const auto & c : layer.contents->commands()) { out.push_back(c); }
    }
    return out;
}

[[nodiscard]] inline bool draws_text(browser & page, std::string_view want) {
    for (const auto & c : commands(page)) {
        if (c.op == paint::paint_op::text_run && c.text.find(want) != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline const std::vector<std::string> & log_of(browser & page) {
    return page.bindings().console_output();
}

// The caret bar for `id`, if one is drawn: a 1px-wide fill.
[[nodiscard]] inline std::vector<rect> caret_bars(browser & page, std::string_view id) {
    const node_id want = find_id(page, id);
    const rect box = box_of(page, id);
    std::vector<rect> out;
    for (const auto & c : commands(page)) {
        // Width 1 alone is not enough: the field's OUTLINE has two 1px-wide
        // vertical edges, and they are the same colour and the same source.
        // The caret is the one strictly INSIDE the box.
        if (c.op == paint::paint_op::fill_rect && c.source == want && c.bounds.width == 1 &&
            c.bounds.x > box.x + 1 && c.bounds.right() < box.right() - 1) {
            out.push_back(c.bounds);
        }
    }
    return out;
}

[[nodiscard]] inline std::size_t caret_of(browser & page, std::string_view id) {
    const auto * state = page.control_state_of(find_id(page, id));
    return state == nullptr ? 0 : state->caret;
}

[[nodiscard]] inline std::string value_of(browser & page, std::string_view id) {
    const auto * state = page.control_state_of(find_id(page, id));
    return state == nullptr ? std::string{} : state->value;
}

[[nodiscard]] inline std::pair<std::size_t, std::size_t> selection_of(browser & page,
                                                                      std::string_view id) {
    const auto * state = page.control_state_of(find_id(page, id));
    if (state == nullptr) { return {0, 0}; }
    return {std::min(state->caret, state->selection), std::max(state->caret, state->selection)};
}

inline void click(browser & page, float x, float y) {
    (void)page.handle(input_event::mouse_down_at(x, y));
    (void)page.handle(input_event::mouse_up_at(x, y));
}

} // namespace ctbrowser_test
