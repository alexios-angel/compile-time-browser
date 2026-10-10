//===- ctbrowser/lib/Shell/browser/selection.cpp - Selection --------------===//
//
// The page text selection: the text runs it is measured over, where a pointer
// lands in them, the highlight, and the clipboard verbs (Copy, Cut, Paste,
// Select All).
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

bool browser::has_selection() const noexcept {
    return selection_anchor_.node && selection_focus_.node &&
           !(selection_anchor_.node == selection_focus_.node &&
             selection_anchor_.code_point == selection_focus_.code_point);
}

void browser::clear_selection() {
    if (!selection_anchor_.node && !selection_focus_.node) { return; }
    selection_anchor_ = {};
    selection_focus_ = {};
    mark(dirty::paint);
}

std::string browser::selected_text() {
    // Extracted from each node's own text over the whole selected range, not
    // by concatenating the runs. A wrap drops the space it broke at, so that
    // space is in no run at all, and joining the runs would delete a space per
    // line from anything copied off a wrapped paragraph.
    std::string out;
    node_id current;
    std::size_t from = 0;
    std::size_t to = 0;
    const ctbrowser::layout::fragment * owner = nullptr;
    const auto flush = [&] {
        if (owner != nullptr && owner->box != nullptr && from < to) {
            const std::string_view full{owner->box->text};
            if (from < full.size()) {
                if (!out.empty()) { out += ' '; }
                out += full.substr(from, std::min(to, full.size()) - from);
            }
        }
    };
    for (const text_run & run : text_runs()) {
        const auto [run_from, run_to] = selected_range(run);
        if (run_from >= run_to) { continue; }
        if (run.source != current) {
            flush();
            current = run.source;
            owner = run.fragment;
            from = run_from;
            to = run_to;
        } else {
            to = std::max(to, run_to);
        }
    }
    flush();
    return out;
}

std::vector<browser::text_run> browser::text_runs() const {
    std::vector<text_run> out;
    flat_map<std::uint64_t, std::size_t> consumed; // Text offset reached, per source node.
    const auto walk = [&](auto && self, const ctbrowser::layout::fragment & f, float dx,
                          float dy) -> void {
        const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
        if (!f.text.empty() && f.source) {
            // Where this line begins in the node's text, found by searching
            // rather than by summing lengths: a wrap drops the space it broke
            // at, so the fragments do not partition the text and summing
            // their lengths would drift by one character per line.
            std::size_t & at = consumed[f.source.key()];
            const std::string_view full =
                f.box != nullptr ? std::string_view{f.box->text} : std::string_view{f.text};
            const std::size_t found = full.find(f.text, at);
            const std::size_t offset = found == std::string_view::npos ? at : found;
            out.push_back(text_run{&f, f.source, offset, f.text, box, out.size()});
            at = offset + f.text.size();
        }
        for (const auto & child : f.children) { self(self, child, box.x, box.y); }
    };
    walk(walk, fragments_, 0, 0);
    return out;
}

browser::text_position browser::position_at(float x, float y) {
    const std::vector<text_run> runs = text_runs();
    if (runs.empty()) { return {}; }
    const float content_y = y + scroll_y_;
    const text_run * best = nullptr;
    float best_distance = 0;
    for (const text_run & run : runs) {
        const float distance = content_y < run.box.y          ? run.box.y - content_y
                               : content_y > run.box.bottom() ? content_y - run.box.bottom()
                                                              : 0.0f;
        if (best == nullptr || distance < best_distance) {
            best = &run;
            best_distance = distance;
        }
    }
    if (best == nullptr) { return {}; }
    return text_position{best->source, best->offset + code_point_at(*best, x)};
}

std::size_t browser::code_point_at(const text_run & run, float x) const {
    if (x <= run.box.x) { return 0; }
    if (x >= run.box.right()) { return run.text.size(); }
    const auto metrics = measure();
    const ctbrowser::layout::text_face face =
        run.fragment->box != nullptr ? run.fragment->box->face : ctbrowser::layout::text_face{};
    const float size = run.fragment->box != nullptr ? run.fragment->box->font_size : 16.0f;
    std::size_t best = 0;
    float best_distance = std::abs(x - run.box.x);
    for (std::size_t i = 1; i <= run.text.size(); ++i) {
        // UTF-8: a boundary is not inside a continuation byte.
        if (i < run.text.size() && (static_cast<unsigned char>(run.text[i]) & 0xC0u) == 0x80u) {
            continue;
        }
        const float edge = run.box.x + metrics(run.text.substr(0, i), size, face);
        const float distance = std::abs(x - edge);
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

std::pair<std::size_t, std::size_t> browser::selected_range(const text_run & run) {
    if (!has_selection()) { return {0, 0}; }
    const std::vector<text_run> runs = text_runs();
    // Which end comes first in document order, so a drag upward selects the
    // same text as the same drag downward.
    const auto locate = [&runs](const text_position & p) -> std::size_t {
        std::size_t best = runs.size();
        for (const text_run & r : runs) {
            if (r.source != p.node) { continue; }
            if (p.code_point >= r.offset && p.code_point <= r.offset + r.text.size()) {
                return r.order;
            }
            best = std::min(best, r.order);
        }
        return best;
    };
    const std::size_t a_order = locate(selection_anchor_);
    const std::size_t b_order = locate(selection_focus_);
    text_position first = selection_anchor_;
    text_position last = selection_focus_;
    if (b_order < a_order ||
        (b_order == a_order && selection_focus_.code_point < selection_anchor_.code_point)) {
        std::swap(first, last);
    }
    const std::size_t first_order = std::min(a_order, b_order);
    const std::size_t last_order = std::max(a_order, b_order);
    if (run.order < first_order || run.order > last_order) { return {0, 0}; }

    const std::size_t run_start = run.offset;
    const std::size_t run_end = run.offset + run.text.size();
    const std::size_t from =
        run.order == first_order ? std::max(run_start, first.code_point) : run_start;
    const std::size_t to = run.order == last_order ? std::min(run_end, last.code_point) : run_end;
    return {std::min(from, to), to};
}

rect browser::highlight_for(const ctbrowser::layout::fragment & f) {
    if (!has_selection() || f.text.empty() || !f.source) { return rect{}; }
    for (const text_run & run : text_runs()) {
        if (run.fragment != &f) { continue; }
        const auto [from, to] = selected_range(run);
        if (from >= to) { return rect{}; }
        const auto metrics = measure();
        const ctbrowser::layout::text_face face =
            f.box != nullptr ? f.box->face : ctbrowser::layout::text_face{};
        const float size = f.box != nullptr ? f.box->font_size : 16.0f;
        const float left = metrics(run.text.substr(0, from - run.offset), size, face);
        const float right = metrics(run.text.substr(0, to - run.offset), size, face);
        return rect{left, 0, right - left, f.bounds.height};
    }
    return rect{};
}

void browser::run_clipboard_verb(std::string_view verb) {
    clipboard_verb(verb);
    // Every verb that edits a field moves its caret, so scroll it into view
    // here, once, rather than in each verb's return path.
    if (control_state * control = editable_focus(); control != nullptr) {
        reveal_caret(focused_, *control, kind_of(doc_->read(), focused_));
    }
}

void browser::clipboard_verb(std::string_view verb) {
    if (verb == "Select All") {
        // The one verb that does not dispatch, so this arm may hold a pointer
        // into the form store: no script runs between fetching and using it.
        if (control_state * control = editable_focus(); control != nullptr) {
            forms_.select_all(*control);
            mark(dirty::paint);
            return;
        }
        // Nothing editable focused: select the whole page.
        const std::vector<text_run> runs = text_runs();
        if (!runs.empty()) {
            selection_anchor_ = text_position{runs.front().source, runs.front().offset};
            selection_focus_ =
                text_position{runs.back().source, runs.back().offset + runs.back().text.size()};
            mark(dirty::paint);
        }
        return;
    }
    const std::string type = verb == "Copy" ? "copy" : verb == "Cut" ? "cut" : "paste";
    if (focused_ && bindings_->dispatch(type, focused_)) { return; } // Cancelled.

    // Fetch the focused control only after dispatching the event. A `cut`/`copy`
    // listener runs page JavaScript that may seed new form state (rehashing
    // form_store::states_, a boost::unordered_flat_map with no reference
    // stability), blur or remove the field, focus another, or navigate
    // (loading a page clears forms_). Asking again re-validates that something
    // is still focused and editable; a node-based map would fix only the
    // rehash.
    control_state * control = editable_focus();
    if (control == nullptr) {
        // No editable focused: Copy takes the page selection. Cut and Paste
        // have nowhere to act, because a page is not editable.
        if (verb == "Copy" && has_selection()) {
            clipboard_ = selected_text();
            if (clipboard_write_) { clipboard_write_(clipboard_); }
        }
        return;
    }
    if (verb == "Paste") {
        const std::string text = clipboard_read_ ? clipboard_read_() : clipboard_;
        // `beforeinput` first, as for typing, and the control is fetched
        // again after it, for the reason above.
        if (!text.empty() && !before_edit("insertFromPaste", text)) {
            if (control_state * live = editable_focus(); live != nullptr) {
                forms_.insert_text(*live, text);
                (void)edited(true, "insertFromPaste", text);
            }
        }
        return;
    }
    const std::string selected = form_store::selected_text(*control);
    if (selected.empty()) { return; }
    clipboard_ = selected;
    if (clipboard_write_) { clipboard_write_(selected); }
    if (verb == "Cut" && !before_edit("deleteByCut", std::nullopt)) {
        if (control_state * live = editable_focus(); live != nullptr) {
            (void)form_store::delete_selection(*live);
            (void)edited(true, "deleteByCut", std::nullopt);
        }
    }
}

} // namespace ctbrowser::shell
