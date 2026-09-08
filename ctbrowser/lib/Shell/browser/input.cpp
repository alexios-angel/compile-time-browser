// browser - input: handle() and the mouse, keys, text input, the editing
// keys inside a field, and the wheel over a textarea.
//
// One of ten files carved out of a 2,871-line Shell/browser.cpp on 2026-09-08.
// All are member functions of one class declared in
// include/ctbrowser/shell/browser.hpp; internal.hpp beside this carries the
// includes browser.cpp had, so every file sees exactly what it saw. Nothing
// about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

bool browser::handle(const input_event & event) {
    // Remembered for anything that has to keep aiming at the pointer after the
    // events stop - the drag auto-scroll, which runs off tick() and is handed
    // no coordinates of its own.
    if (event.kind == input_kind::mouse_move || event.kind == input_kind::mouse_down ||
        event.kind == input_kind::mouse_up ||
        (event.kind == input_kind::wheel && event.has_pointer)) {
        pointer_ = point{event.x, event.y};
        have_pointer_ = true;
    }
    switch (event.kind) {
    case input_kind::wheel: {
        // THE PAGE GETS THE NOTCH FIRST, and may keep it. A `wheel` listener
        // that calls preventDefault means the document must NOT also scroll -
        // which is how a canvas zooms without the page sliding out from under
        // it, and what every 3D library on the web relies on. Nothing was
        // dispatched at all before this: the notch went straight to the
        // scroller, so a scene could be orbited and not zoomed.
        const node_id under = event.has_pointer ? hit_test(event.x, event.y) : body_node();
        if (bindings_->dispatch_wheel(under ? under : body_node(), event)) { return true; }
        // The field under the pointer takes the notch next, and only what it
        // cannot use falls through to the page - which is what makes a textarea
        // at its last line stop swallowing the wheel.
        if (scroll_field_under(event)) { return true; }
        scroll_by(-event.wheel_y * options_.wheel_step);
        return true;
    }
    case input_kind::mouse_move: {
        if (sb_dragging_) {
            // The grab offset is kept so the thumb does not jump to centre
            // itself under the pointer on the first pixel of movement.
            const float height = static_cast<float>(options_.height);
            const float thumb_height = scrollbar_thumb().height;
            const float travel = height - thumb_height;
            scroll_to(travel > 0 ? (event.y - sb_grab_) / travel * max_scroll() : 0);
            return true;
        }
        if (field_selecting_) {
            // Extending a selection INSIDE a control. The anchor is the
            // control's `selection`, so dragging back over the text shrinks
            // it rather than starting again.
            const auto txn = doc_->read();
            const control_kind kind = kind_of(txn, field_selecting_);
            control_state & state = forms_.state_of(txn, atoms_, field_selecting_);
            const std::size_t at = offset_at_point(field_selecting_, state, kind, event.x, event.y);
            if (at != state.caret) {
                state.caret = at;
                // Rule 1 while the pointer is still INSIDE the box: the caret
                // leads and the view follows it. Once the pointer leaves, rule
                // 3 takes over in tick() and the view leads instead - the two
                // must never both run, or they oscillate.
                reveal_caret(field_selecting_, state, kind);
                restart_caret_blink();
                mark(dirty::paint);
            }
            return true;
        }
        if (selecting_) {
            // Extending. The ANCHOR stays put, which is what makes dragging
            // back over the text shrink the selection rather than start a
            // new one.
            const text_position at = position_at(event.x, event.y);
            if (at && (at.node != selection_focus_.node ||
                       at.code_point != selection_focus_.code_point)) {
                selection_focus_ = at;
                mark(dirty::paint);
            }
            return true;
        }
        const node_id under = hit_test(event.x, event.y);
        // A page tracking the pointer - MDN's breakout moves its paddle
        // this way - needs the event whether or not the hover state moved.
        const bool dispatched = dispatch_mouse("mousemove", under, event);
        return set_hover(under) || dispatched;
    }
    case input_kind::mouse_down:
        // An OPEN POPUP takes the press before anything else - it is drawn
        // over the page, so it has to be hit-tested over the page too.
        if (select_open_ && handle_popup_press(event)) { return true; }
        // The RIGHT button opens the context menu instead of pressing
        // anything, and the page gets a cancelable `contextmenu` first -
        // which is how a page that wants its own menu suppresses ours.
        if (event.button == input_event::right_button) {
            const node_id target = hit_test(event.x, event.y);
            if (!bindings_->dispatch_mouse("contextmenu", target ? target : body_node(), event)) {
                menu_at_ = point{event.x, event.y};
                menu_open_ = true;
                mark(dirty::paint);
            }
            return true;
        }
        // ...and a LEFT press anywhere closes an open one.
        if (menu_open_) {
            const bool consumed = handle_menu_press(event);
            if (consumed) { return true; }
        }
        if (on_scrollbar(event.x)) {
            const rect thumb = scrollbar_thumb();
            if (event.y >= thumb.y && event.y < thumb.y + thumb.height) {
                sb_dragging_ = true;
                sb_grab_ = event.y - thumb.y;
            } else {
                // A click on the TRACK pages towards the pointer, which is
                // what every scrollbar does with one.
                scroll_by(event.y < thumb.y ? -static_cast<float>(options_.height) * 0.9f
                                            : static_cast<float>(options_.height) * 0.9f);
            }
            mark(dirty::paint);
            return true;
        }
        pressed_ = hit_test(event.x, event.y);
        (void)dispatch_mouse("mousedown", pressed_, event);
        // A press begins a SELECTION. Inside an editable control that is a
        // selection of ITS text, anchored where the click landed; outside
        // one it is a page selection. A control had neither: clicking in a
        // textarea put the caret wherever it already was and dragging did
        // nothing at all.
        {
            const auto txn = doc_->read();
            const node_id control = control_ancestor(pressed_);
            const control_kind kind = kind_of(txn, control);
            if (kind == control_kind::text || kind == control_kind::textarea) {
                clear_selection();
                (void)focus(control); // before placing the caret: focus clears it
                // A click that reached the field through its LABEL focuses it
                // and stops there. The pointer is over the label's text, not
                // over any glyph of the value, so mapping it through
                // offset_at_point would drop the caret at whichever end the
                // label sits on and begin a drag-selection from there.
                if (!via_label(pressed_)) {
                    control_state & state = forms_.state_of(txn, atoms_, control);
                    state.caret = offset_at_point(control, state, kind, event.x, event.y);
                    state.selection = state.caret;
                    field_selecting_ = control;
                    // Clicking near an edge of a scrolled field nudges the view
                    // so the caret you just placed is actually on screen.
                    reveal_caret(control, state, kind);
                }
                restart_caret_blink();
                mark(dirty::paint);
            } else if (kind == control_kind::none) {
                const text_position at = position_at(event.x, event.y);
                selection_anchor_ = at;
                selection_focus_ = at;
                selecting_ = static_cast<bool>(at);
                mark(dirty::paint);
            } else {
                clear_selection();
            }
        }
        return set_state(pressed_, state_active, true);
    case input_kind::mouse_up: {
        if (sb_dragging_) {
            sb_dragging_ = false;
            mark(dirty::paint);
            return true;
        }
        selecting_ = false;
        field_selecting_ = node_id{};
        bool changed = set_state(pressed_, state_active, false);
        (void)dispatch_mouse("mouseup", hit_test(event.x, event.y), event);
        // Focus follows the press, and moves even when the click lands on
        // nothing - which is how clicking the page background blurs a field.
        changed = focus(control_ancestor(pressed_)) || changed;
        // A click fires on RELEASE, at the element the press started on -
        // which is what makes dragging off a button cancel it, the way every
        // real browser behaves.
        const node_id released_on = hit_test(event.x, event.y);
        if (pressed_ && released_on == pressed_) {
            const bool prevented = bindings_->dispatch("click", pressed_);
            changed = true;
            // Default actions run AFTER the listeners and only if none of
            // them cancelled - which is what preventDefault is for.
            if (!prevented) { activate(pressed_); }
        }
        pressed_ = node_id{};
        return changed;
    }
    case input_kind::key_down: return handle_key(event);
    case input_kind::key_up: return dispatch_key("keyup", event);
    case input_kind::text_input: return text_input(event.key);
    case input_kind::resize:
        resize(static_cast<int>(event.x), static_cast<int>(event.y));
        return true;
    }
    return false;
}

node_id browser::hit_test(float x, float y) const {
    return layers_.hit_test(point{x, y});
}

std::string_view browser::cursor_at(float x, float y) {
    if (on_scrollbar(x)) { return "default"; }
    const node_id under = hit_test(x, y);
    if (!under) { return "default"; }
    const auto txn = doc_->read();
    // The nearest ancestor that says something, because `cursor` inherits
    // and the text inside a link is not itself the link.
    for (node_id at = under; at; at = txn.parent(at)) {
        const auto found = resolved_.find(ctbrowser::style::engine::key_of(at));
        if (found != resolved_.end() && found->second) {
            const std::string_view wanted = found->second->get(atoms_.intern("cursor"));
            if (!wanted.empty()) { return wanted; }
        }
        const control_kind kind = kind_of(txn, at);
        if (kind == control_kind::text || kind == control_kind::textarea) { return "text"; }
    }
    // Bare text is selectable, and an I-beam is how a page says so.
    return txn.kind(under).value_or(node_kind::element) == node_kind::text ? "text" : "default";
}

bool browser::text_input(std::string_view text) {
    control_state * control = editable_focus();
    if (control == nullptr || text.empty()) { return false; }
    // This path is for PRINTABLE text; a control character is a KEY, and one
    // arriving here would be inserted literally - a Tab dropped into the very
    // field Tab is supposed to leave. SDL never sends one, so this is a guard
    // on the headless path, which is exactly where a test driving Tab would
    // produce it by accident. Bytes >= 0x80 are left alone: they are UTF-8
    // continuation bytes, not controls.
    std::string printable;
    printable.reserve(text.size());
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        // '\n' survives: a textarea wants it, and insert_text is where a
        // newline in a pasted value has to land.
        if (byte == '\n' || byte >= 0x20) {
            if (byte != 0x7F) { printable.push_back(c); }
        }
    }
    if (printable.empty()) { return false; }
    forms_.insert_text(*control, printable);
    // Typing past the last visible row of a textarea scrolls it, or the caret
    // walks off a box that cannot grow to follow it.
    reveal_caret(focused_, *control, kind_of(doc_->read(), focused_));
    restart_caret_blink(); // a caret that blinks out under what you typed looks broken
    bindings_->dispatch("input", focused_);
    mark(dirty::paint);
    return true;
}

const control_state * browser::control_state_of(node_id id) {
    if (!id) { return nullptr; }
    const auto txn = doc_->read();
    if (!txn.contains(id) || kind_of(txn, id) == control_kind::none) { return nullptr; }
    return &forms_.state_of(txn, atoms_, id);
}

bool browser::handle_key(const input_event & event) {
    // Escape closes an open popup before the page sees the key, which is
    // what every select does.
    if (select_open_ && event.key == "Escape") {
        select_open_ = node_id{};
        mark(dirty::paint);
        return true;
    }
    if (dispatch_key("keydown", event)) { return true; }

    // The CLIPBOARD SHORTCUTS come before the editing keys, and before the
    // editable check: Ctrl+C is not a C, and copying the PAGE selection has
    // to work when nothing is focused at all - which is the usual case for
    // someone reading a page.
    if (event.ctrl && (event.key == "KeyC" || event.key == "KeyX" || event.key == "KeyV" ||
                       event.key == "KeyA")) {
        run_clipboard_verb(event.key == "KeyC"   ? "Copy"
                           : event.key == "KeyX" ? "Cut"
                           : event.key == "KeyV" ? "Paste"
                                                 : "Select All");
        return true;
    }

    // TAB moves focus, and it is a DEFAULT ACTION like every other key here -
    // the page already saw this keydown above, and a preventDefault returned
    // before we got here.
    //
    // Before edit_key deliberately: edit_key has no "Tab" arm today, but one
    // added later would silently eat the key in the very control Tab exists to
    // LEAVE. It returns true whatever focus_next decides, so Tab can never fall
    // through to the page-scrolling keys below.
    if (event.key == "Tab") {
        (void)focus_next(event.shift);
        return true;
    }

    if (control_state * control = editable_focus(); control != nullptr) {
        if (edit_key(*control, event)) {
            // One place rather than an arm-by-arm sprinkle: every editing key
            // either moves the caret or changes the value, and both can put it
            // outside a textarea's visible rows.
            reveal_caret(focused_, *control, kind_of(doc_->read(), focused_));
            return true;
        }
    }
    const float page = static_cast<float>(options_.height) * 0.9f;
    if (event.key == "ArrowDown") {
        scroll_by(options_.wheel_step);
        return true;
    }
    if (event.key == "ArrowUp") {
        scroll_by(-options_.wheel_step);
        return true;
    }
    if (event.key == "PageDown" || event.key == "Space") {
        scroll_by(page);
        return true;
    }
    if (event.key == "PageUp") {
        scroll_by(-page);
        return true;
    }
    if (event.key == "Home") {
        scroll_to(0);
        return true;
    }
    if (event.key == "End") {
        scroll_to(max_scroll());
        return true;
    }
    return false;
}

bool browser::dispatch_key(std::string_view type, const input_event & event) {
    if (!bindings_) { return false; }
    // At the focused element, so a keystroke in a text field is that
    // field's event; at the body otherwise, which is where a game listens.
    const node_id target = focused_ ? focused_ : body_node();
    return bindings_->dispatch_key(type, target, event);
}

bool browser::dispatch_mouse(std::string_view type, node_id target, const input_event & event) {
    if (!bindings_) { return false; }
    return bindings_->dispatch_mouse(type, target ? target : body_node(), event);
}

node_id browser::body_node() {
    const auto txn = doc_->read();
    const atom body = atoms_.intern_lower("body");
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!found && txn.tag(at).value_or(atom{}) == body) { found = at; }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found ? found : txn.root();
}

bool browser::scroll_field_under(const input_event & event) {
    if (!event.has_pointer || event.wheel_y == 0) { return false; }
    const node_id under = control_ancestor(hit_test(event.x, event.y));
    if (!under) { return false; }
    const auto txn = doc_->read();
    const control_kind kind = kind_of(txn, under);
    // Only a textarea scrolls vertically. A single-line field's overflow is
    // sideways, and a wheel is not how anyone asks for that.
    if (kind != control_kind::textarea) { return false; }

    control_state & state = forms_.state_of(txn, atoms_, under);
    const rect box = viewport_box_of(under);
    if (box.empty()) { return false; }
    const field_layout geometry = layout_of_field(box, under, state, kind);
    const std::size_t most = geometry.lines.size() > geometry.visible_lines
                                 ? geometry.lines.size() - geometry.visible_lines
                                 : 0;
    if (most == 0) { return false; } // nothing to scroll: the page takes it

    // Negative wheel_y is towards the user, which is down the document.
    const auto step = static_cast<std::ptrdiff_t>(options_.wheel_lines);
    const auto delta = event.wheel_y > 0 ? -step : step;
    const auto want = static_cast<std::ptrdiff_t>(geometry.scroll_line) + delta;
    const auto next = static_cast<std::size_t>(
        std::clamp<std::ptrdiff_t>(want, 0, static_cast<std::ptrdiff_t>(most)));
    // ALREADY AT THAT END: the notch is not ours, so the page gets it. Without
    // this a textarea scrolled to its bottom swallows every further notch and
    // the page appears stuck.
    if (next == geometry.scroll_line) { return false; }

    state.scroll_line = next;
    mark(dirty::paint);
    return true;
}

bool browser::edit_key(control_state & control, const input_event & event) {
    const std::string & key = event.key;
    const control_kind kind = kind_of(doc_->read(), focused_);
    const bool multiline = kind == control_kind::textarea;
    if (key == "Backspace") { return edited(forms_.backspace(control)); }
    if (key == "Delete") { return edited(forms_.delete_forward(control)); }
    if (key == "ArrowLeft") { return moved(forms_.move_caret(control, -1, event.shift)); }
    if (key == "ArrowRight") { return moved(forms_.move_caret(control, 1, event.shift)); }
    // UP and DOWN are visual LINES, and Home/End are the ends of one. In a
    // single-line field up and down are the whole value's ends, which is
    // what a browser does with them there.
    if (key == "ArrowUp") {
        return moved(multiline ? move_caret_by_line(focused_, control, kind, -1, event.shift)
                               : forms_.move_to_edge(control, false, event.shift));
    }
    if (key == "ArrowDown") {
        return moved(multiline ? move_caret_by_line(focused_, control, kind, 1, event.shift)
                               : forms_.move_to_edge(control, true, event.shift));
    }
    // PAGE UP AND DOWN BELONG TO THE FIELD when a multi-line one has focus.
    // They fell through to the page-scrolling keys, so paging inside a textarea
    // scrolled the document out from under it.
    if (multiline && (key == "PageUp" || key == "PageDown")) {
        const rect box = viewport_box_of(focused_);
        int lines = 1;
        if (!box.empty()) {
            const field_layout geometry = layout_of_field(box, focused_, control, kind);
            lines = std::max(1, static_cast<int>(geometry.visible_lines) - 1);
        }
        const int direction = key == "PageDown" ? 1 : -1;
        bool any = false;
        for (int i = 0; i < lines; ++i) {
            if (!move_caret_by_line(focused_, control, kind, direction, event.shift)) { break; }
            any = true;
        }
        // Consumed either way: a caret already at the top or bottom of the
        // value must not hand the key to the page and scroll the document.
        (void)moved(any);
        return true;
    }
    if (key == "Home") {
        return moved(multiline ? move_to_line_edge(focused_, control, kind, false, event.shift)
                               : forms_.move_to_edge(control, false, event.shift));
    }
    if (key == "End") {
        return moved(multiline ? move_to_line_edge(focused_, control, kind, true, event.shift)
                               : forms_.move_to_edge(control, true, event.shift));
    }
    // ESCAPE drops the selection and keeps the caret, which is what every
    // browser does with it in a field. Ctrl+A then Escape left the whole
    // value highlighted forever.
    if (key == "Escape") {
        if (control.selection == control.caret) { return false; }
        control.selection = control.caret;
        restart_caret_blink();
        mark(dirty::paint);
        return true;
    }
    if (event.ctrl && key == "KeyA") {
        forms_.select_all(control);
        mark(dirty::paint);
        return true;
    }
    if (key == "Enter") {
        // In a textarea this is a newline; in a single-line field it submits
        // the form, which is the implicit-submission rule every login page
        // depends on.
        const auto txn = doc_->read();
        if (kind_of(txn, focused_) == control_kind::textarea) {
            forms_.insert_text(control, "\n");
            return edited(true);
        }
        submit(form_store::owning_form(txn, atoms_, focused_));
        return true;
    }
    return false;
}

bool browser::edited(bool changed) {
    if (!changed) { return false; }
    restart_caret_blink();
    bindings_->dispatch("input", focused_);
    mark(dirty::paint);
    return true;
}

bool browser::moved(bool changed) {
    restart_caret_blink();
    if (changed) { mark(dirty::paint); }
    return true; // the key was consumed either way - it must not scroll the page
}

} // namespace ctbrowser::shell
