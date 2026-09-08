// browser - focus: the editable field, what is focusable, labels, Tab order,
// and the drag auto-scroll that runs off tick().
//
// One of ten files carved out of a 2,871-line Shell/browser.cpp on 2026-09-08.
// All are member functions of one class declared in
// include/ctbrowser/shell/browser.hpp; internal.hpp beside this carries the
// includes browser.cpp had, so every file sees exactly what it saw. Nothing
// about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

bool browser::has_editable_focus() {
    if (!focused_) { return false; }
    const auto txn = doc_->read();
    if (!txn.contains(focused_)) { return false; }
    const control_kind kind = kind_of(txn, focused_);
    return kind == control_kind::text || kind == control_kind::textarea;
}

control_state * browser::editable_focus() {
    if (!focused_) { return nullptr; }
    const auto txn = doc_->read();
    if (!txn.contains(focused_)) { return nullptr; }
    const control_kind kind = kind_of(txn, focused_);
    if (kind != control_kind::text && kind != control_kind::textarea) { return nullptr; }
    return &forms_.state_of(txn, atoms_, focused_);
}

control_kind browser::kind_of(const read_txn & txn, node_id id) {
    return control_kind_of(atoms_.text(txn.tag(id).value_or(atom{})),
                           txn.attribute_value(id, atoms_.intern("type")));
}

node_id browser::node_by_id(const read_txn & txn, std::string_view want) {
    if (want.empty()) { return node_id{}; }
    const atom key = atoms_.intern("id");
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (found) { return; }
        if (txn.attribute_value(at, key) == want) {
            found = at;
            return;
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

node_id browser::labelled_control(const read_txn & txn, node_id from) {
    if (!from) { return node_id{}; }
    const atom label_tag = atoms_.intern_lower("label");
    node_id label;
    for (node_id at = from; at; at = txn.parent(at)) {
        if (txn.tag(at).value_or(atom{}) == label_tag) {
            label = at;
            break;
        }
    }
    if (!label) { return node_id{}; }

    // `for` wins when it is there, even if it names nothing - an explicit
    // reference that does not resolve labels NOTHING, rather than quietly
    // falling back to whatever the label happens to contain.
    const std::string_view target = txn.attribute_value(label, atoms_.intern("for"));
    if (!target.empty()) {
        const node_id named = node_by_id(txn, target);
        return kind_of(txn, named) != control_kind::none ? named : node_id{};
    }

    // Otherwise the first labelable DESCENDANT, in document order. The label
    // itself cannot be the answer - a <label> is not a control - so the walk
    // starts at its children.
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (found) { return; }
        if (at != label && kind_of(txn, at) != control_kind::none) {
            found = at;
            return;
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, label);
    return found;
}

node_id browser::control_ancestor(node_id from) {
    if (!from) { return node_id{}; }
    const auto txn = doc_->read();
    for (node_id at = from; at; at = txn.parent(at)) {
        if (kind_of(txn, at) != control_kind::none) { return at; }
    }
    // Nothing up the tree IS a control, so the click may still be on a label
    // FOR one. Second, not first: a press on the control inside a label finds
    // it on the walk above and never reaches here, so a click can never resolve
    // twice and toggle a checkbox back off again.
    return labelled_control(txn, from);
}

bool browser::via_label(node_id from) {
    if (!from) { return false; }
    const auto txn = doc_->read();
    for (node_id at = from; at; at = txn.parent(at)) {
        if (kind_of(txn, at) != control_kind::none) { return false; }
    }
    return static_cast<bool>(labelled_control(txn, from));
}

bool browser::focus(node_id id) {
    if (is_disabled(id)) { return false; } // a disabled control cannot take focus
    if (id == focused_) { return false; }
    if (focused_) {
        // `change` fires on BLUR, not on every keystroke - that is the
        // difference between it and `input`, and pages rely on it.
        bindings_->dispatch("change", focused_);
        (void)set_state(focused_, state_focus, false);
        // And the outgoing field DROPS ITS SELECTION. A highlight left
        // behind in a field nobody is typing in reads as still selected,
        // and Ctrl+A followed by a click somewhere else did exactly that.
        const auto txn = doc_->read();
        if (txn.contains(focused_)) {
            const control_kind kind = kind_of(txn, focused_);
            if (kind == control_kind::text || kind == control_kind::textarea) {
                control_state & state = forms_.state_of(txn, atoms_, focused_);
                state.selection = state.caret;
            }
        }
    }
    focused_ = id;
    // Told to the bindings BEFORE the event fires, so a `focus` listener asking
    // document.activeElement gets the element it was just handed rather than
    // the one that had focus a moment ago.
    bindings_->observe_focus(focused_);
    restart_caret_blink(); // a field you just clicked into shows its caret at once
    if (focused_) {
        (void)set_state(focused_, state_focus, true);
        bindings_->dispatch("focus", focused_);
    }
    mark(dirty::paint);
    return true;
}

bool browser::is_focusable(const read_txn & txn, node_id id) {
    // Every focusable kind is exactly "is a control", so there is no per-kind
    // list to keep in step with control_kind_of - and adding one would be the
    // obvious wrong edit here.
    if (kind_of(txn, id) == control_kind::none) { return false; }
    if (is_disabled(id)) { return false; }
    return !viewport_box_of(id).empty();
}

std::vector<node_id> browser::focusable_controls() {
    const auto txn = doc_->read();
    std::vector<node_id> out;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (is_focusable(txn, at)) { out.push_back(at); }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return out;
}

bool browser::focus_next(bool backwards) {
    const std::vector<node_id> order = focusable_controls();
    if (order.empty()) { return false; }
    const auto at = std::find(order.begin(), order.end(), focused_);
    std::size_t next = 0;
    if (at == order.end()) {
        // Nothing focused, or focus is on something that is not a control:
        // Tab starts at the top of the document and Shift+Tab at the bottom.
        next = backwards ? order.size() - 1 : 0;
    } else {
        const auto here = static_cast<std::size_t>(at - order.begin());
        next = backwards ? (here + order.size() - 1) % order.size() : (here + 1) % order.size();
    }
    // Discarded: focus() reports "nothing changed" on a page with one control,
    // and that must not read as "Tab did nothing" - the key was still handled.
    (void)focus(order[next]);
    return true;
}

browser::autoscroll_state browser::autoscroll_now() {
    autoscroll_state out;
    if (!field_selecting_ || !have_pointer_ || options_.autoscroll_ms <= 0) { return out; }
    const rect box = viewport_box_of(field_selecting_);
    if (box.empty()) { return out; }
    const auto txn = doc_->read();
    if (!txn.contains(field_selecting_)) { return out; }
    const control_kind kind = kind_of(txn, field_selecting_);
    control_state & state = forms_.state_of(txn, atoms_, field_selecting_);
    const field_layout geometry = layout_of_field(box, field_selecting_, state, kind);

    // How far outside, per axis. Inside on an axis means no motion on it, so a
    // drag straight down does not also creep sideways.
    const float below = pointer_.y > box.bottom() ? pointer_.y - box.bottom()
                        : pointer_.y < box.y      ? pointer_.y - box.y
                                                  : 0;
    const float beside = pointer_.x > box.right() ? pointer_.x - box.right()
                         : pointer_.x < box.x     ? pointer_.x - box.x
                                                  : 0;

    // ...and only where the view can still MOVE that way. Without this the
    // wakeup below is scheduled forever and an idle loop with the pointer
    // parked below a fully-scrolled field spins at the step interval.
    const std::size_t most = geometry.lines.size() > geometry.visible_lines
                                 ? geometry.lines.size() - geometry.visible_lines
                                 : 0;
    if ((below > 0 && geometry.scroll_line < most) || (below < 0 && geometry.scroll_line > 0)) {
        out.below = below;
    }
    const float widest = std::max(0.0f, geometry.content_width + 1 - geometry.inner.width);
    if ((beside > 0 && geometry.scroll_x < widest) || (beside < 0 && geometry.scroll_x > 0)) {
        out.beside = beside;
    }
    if (out.below != 0 || out.beside != 0) { out.field = field_selecting_; }
    return out;
}

double browser::autoscroll_interval_ms(float distance) const {
    const float d = std::fabs(distance);
    const double ramp = options_.autoscroll_ramp_px > 0
                            ? 1.0 + static_cast<double>(d) / options_.autoscroll_ramp_px
                            : 1.0;
    return std::max(options_.autoscroll_min_ms, options_.autoscroll_ms / ramp);
}

void browser::autoscroll_step(const autoscroll_state & at) {
    const rect box = viewport_box_of(at.field);
    if (box.empty()) { return; }
    const auto txn = doc_->read();
    const control_kind kind = kind_of(txn, at.field);
    control_state & state = forms_.state_of(txn, atoms_, at.field);
    {
        const field_layout geometry = layout_of_field(box, at.field, state, kind);
        // THE VIEW MOVES FIRST. One line, one character - the smallest step
        // there is, so the rate alone decides how fast it goes.
        if (at.below > 0) {
            state.scroll_line = geometry.scroll_line + 1;
        } else if (at.below < 0 && geometry.scroll_line > 0) {
            state.scroll_line = geometry.scroll_line - 1;
        }
        if (at.beside != 0) {
            // A character's worth, measured rather than assumed: "one column"
            // means nothing in a proportional face.
            const float step = measure()("n", geometry.size, geometry.metrics_face);
            state.scroll_x = geometry.scroll_x + (at.beside > 0 ? step : -step);
        }
    }
    // ...and THEN the caret follows it, derived from where the pointer actually
    // is against the view we just moved. Not reveal_caret, which is the inverse
    // and would drag the view back to the caret it is trying to lead.
    const std::size_t caret = offset_at_point(at.field, state, kind, pointer_.x, pointer_.y);
    if (caret != state.caret) { state.caret = caret; }
    restart_caret_blink();
    mark(dirty::paint);
}

} // namespace ctbrowser::shell
