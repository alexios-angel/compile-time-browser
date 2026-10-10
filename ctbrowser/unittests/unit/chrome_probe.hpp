//===- unittests/unit/chrome_probe.hpp - Page probes ------------*- C++ -*-===//
//
// Test helpers that read a page's paint commands and form-control state
// (caret, value, selection). They are shared by several unittests/unit/ tests
// and are private to that directory. The using-directive lets the bodies name
// `rect`, `node_id` and `paint::` unqualified.
//
//===----------------------------------------------------------------------===//
#pragma once

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

/// Every paint command of every layer of `page`, in layer order.
[[nodiscard]] inline std::vector<paint::paint_command> commands(browser & page) {
    std::vector<paint::paint_command> out;
    for (const auto & layer : page.layers().layers) {
        if (!layer.contents) { continue; }
        for (const auto & c : layer.contents->commands()) { out.push_back(c); }
    }
    return out;
}

/// Whether any text run painted on `page` contains `want`.
[[nodiscard]] inline bool draws_text(browser & page, std::string_view want) {
    for (const auto & c : commands(page)) {
        if (c.op == paint::paint_op::text_run && c.text.find(want) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/// The page's console.log output so far, one entry per call.
[[nodiscard]] inline const std::vector<std::string> & log_of(browser & page) {
    return page.bindings().console_output();
}

/// The caret bar for `id`, if one is drawn: a 1px-wide fill.
[[nodiscard]] inline std::vector<rect> caret_bars(browser & page, std::string_view id) {
    const node_id want = find_id(page, id);
    const rect box = box_of(page, id);
    std::vector<rect> out;
    for (const auto & c : commands(page)) {
        // Width 1 alone is not enough: the field's outline has two 1px-wide
        // vertical edges, and they are the same colour and the same source.
        // The caret is the one strictly inside the box.
        if (c.op == paint::paint_op::fill_rect && c.source == want && c.bounds.width == 1 &&
            c.bounds.x > box.x + 1 && c.bounds.right() < box.right() - 1) {
            out.push_back(c.bounds);
        }
    }
    return out;
}

/// The caret offset of control `id`, or 0 if it has no control state.
[[nodiscard]] inline std::size_t caret_of(browser & page, std::string_view id) {
    const auto * state = page.control_state_of(find_id(page, id));
    return state == nullptr ? 0 : state->caret;
}

/// The current value of control `id`, or "" if it has no control state.
[[nodiscard]] inline std::string value_of(browser & page, std::string_view id) {
    const auto * state = page.control_state_of(find_id(page, id));
    return state == nullptr ? std::string{} : state->value;
}

/// The selection of control `id` as an ordered [start, end) pair.
[[nodiscard]] inline std::pair<std::size_t, std::size_t> selection_of(browser & page,
                                                                      std::string_view id) {
    const auto * state = page.control_state_of(find_id(page, id));
    if (state == nullptr) { return {0, 0}; }
    return {std::min(state->caret, state->selection), std::max(state->caret, state->selection)};
}

/// A mouse down and up at (x, y).
inline void click(browser & page, float x, float y) {
    (void)page.handle(input_event::mouse_down_at(x, y));
    (void)page.handle(input_event::mouse_up_at(x, y));
}

} // namespace ctbrowser_test
