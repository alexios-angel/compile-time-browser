#include <ctbrowser/shell/page/forms.hpp>

// forms: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::shell {

namespace {

// An <input>'s type state, or the tag of any other control.
[[nodiscard]] std::string type_of(const read_txn & txn, atom_table & atoms, node_id id) {
    const std::string_view tag = atoms.text(txn.tag(id).value_or(atom{}));
    if (tag != "input") { return std::string{tag}; }
    return input_types::type_state_of(txn.attribute_value(id, atoms.intern("type")));
}

// The value sanitization algorithm of the control's type over `text` - and
// for a textarea the API value's newline normalisation, since the value the
// store keeps is the one every reader wants (4.10.11: CR LF and CR are LF).
[[nodiscard]] std::string sanitized(const read_txn & txn, atom_table & atoms, node_id id,
                                    std::string_view type, std::string text) {
    const std::string_view tag = atoms.text(txn.tag(id).value_or(atom{}));
    if (tag == "textarea") { return input_types::normalize_newlines(text); }
    if (tag != "input") { return text; }
    return input_types::sanitize_value(
        type, std::move(text), txn.attribute_value(id, atoms.intern("min")),
        txn.attribute_value(id, atoms.intern("max")), txn.attribute_value(id, atoms.intern("step")),
        txn.attribute_value(id, atoms.intern("value")),
        txn.has_attribute(id, atoms.intern("multiple")));
}

// A textarea's value as its children make it: the CHILD TEXT CONTENT - the
// data of its Text children, not of every descendant (a <span> inside one
// contributes nothing) - newline-normalised.
[[nodiscard]] std::string textarea_value(const read_txn & txn, node_id id) {
    std::string raw;
    for (const node_id child : txn.children(id)) {
        const node_kind kind = txn.kind(child).value_or(node_kind::element);
        if (kind == node_kind::text || kind == node_kind::cdata_section) { raw += txn.text(child); }
    }
    return input_types::normalize_newlines(raw);
}

// Does the selection API apply to a type state (4.10.5's table)? A textarea's
// always does.
[[nodiscard]] bool selection_applies(std::string_view type) {
    return type == "text" || type == "search" || type == "url" || type == "tel" ||
           type == "password" || type == "textarea";
}

// A value changed under the selection by something other than the `value`
// setter or typing: the selection stays where it was, clamped to the end.
void clamp_selection(control_state & held) {
    held.caret = std::min(held.caret, held.value.size());
    held.selection = std::min(held.selection, held.value.size());
}

} // namespace

control_state & form_store::state_of(const read_txn & txn, atom_table & atoms, node_id id) {
    const auto it = states_.find(id.key());
    if (it != states_.end()) {
        control_state & held = it->second;
        const std::string type = type_of(txn, atoms, id);
        if (held.type != type) {
            // HTML 4.10.5, "when the type attribute changes": a value carried
            // into a default mode goes to the content attribute; a default mode
            // becoming the value mode re-reads it, undirtied; the file state
            // starts empty; and the new state sanitises what is left.
            const std::string_view was = input_types::value_mode_of(held.type);
            const std::string_view now = input_types::value_mode_of(type);
            if (was == "value" && !held.value.empty() &&
                (now == "default" || now == "default/on")) {
                held.pending_attribute = held.value;
            } else if ((was == "default" || was == "default/on") && now == "value") {
                held.value = held.pending_attribute
                                 ? *held.pending_attribute
                                 : std::string{txn.attribute_value(id, atoms.intern("value"))};
                held.value_edited = false;
            } else if (was != "filename" && now == "filename") {
                held.value.clear();
                held.value_edited = false;
            }
            // Steps 9-10: a state the selection API did not apply to becoming
            // one it does puts the cursor at the BEGINNING (an email's value
            // turned text starts at 0); otherwise the selection is clamped.
            const bool previously_selectable = selection_applies(held.type);
            held.type = type;
            held.value = sanitized(txn, atoms, id, type, std::move(held.value));
            if (!previously_selectable && selection_applies(type)) {
                held.caret = 0;
                held.selection = 0;
            } else {
                clamp_selection(held);
            }
        }
        // THE ATTRIBUTE IS STILL THE ANSWER UNTIL SOMETHING EDITS THE CONTROL.
        //
        // The state used to be seeded once, when the control was first asked
        // about, and never looked at the attribute again. A control created by
        // script and given its `value` afterwards therefore kept the empty seed
        // - and `createInput('hello')`, which p5 writes as createElement then
        // setAttribute('value', ...), came back empty.
        //
        // `value_edited` is the spec's rule stated as a flag: once the user has
        // typed or a script has assigned `.value`, the content attribute stops
        // being the answer and re-reading it would undo their work. A textarea
        // takes its value from its children and a select from its options, so
        // neither has an attribute to re-read.
        // THE DEFAULT MODES ARE THE ATTRIBUTE, dirty or not (a submit button's
        // `.value = x` writes the attribute); "on" for a checkbox or radio
        // with none (the default/on mode's answer).
        const std::string_view mode = input_types::value_mode_of(type);
        if (mode == "default" || mode == "default/on") {
            if (held.pending_attribute) {
                held.value = *held.pending_attribute;
            } else {
                const atom value_attr = atoms.intern("value");
                held.value = std::string{txn.attribute_value(id, value_attr)};
                if (mode == "default/on" && !txn.has_attribute(id, value_attr)) {
                    held.value = "on";
                }
            }
        } else if (!held.value_edited) {
            // A CLEAN control follows its default: an input its `value`
            // attribute, a textarea its child text content (4.10.11's
            // children changed steps). Neither moves the selection - that is
            // the `value` setter's and typing's - it only clamps.
            const std::string_view tag = atoms.text(txn.tag(id).value_or(atom{}));
            if (tag == "textarea" || (tag != "select" && mode != "filename")) {
                std::string fresh =
                    tag == "textarea"
                        ? textarea_value(txn, id)
                        : sanitized(txn, atoms, id, type,
                                    std::string{txn.attribute_value(id, atoms.intern("value"))});
                if (fresh != held.value) {
                    held.value = std::move(fresh);
                    clamp_selection(held);
                }
            }
        } else if (type == "email") {
            // A dirty email value is still re-sanitised when `multiple` comes
            // or goes (4.10.5.1.5) - the sanitisation is idempotent, so a read
            // that changed nothing changes nothing.
            if (std::string fresh = sanitized(txn, atoms, id, type, held.value);
                fresh != held.value) {
                held.value = std::move(fresh);
                clamp_selection(held);
            }
        }
        return held;
    }
    control_state seeded;
    const std::string_view tag = atoms.text(txn.tag(id).value_or(atom{}));
    if (tag == "textarea") {
        seeded.value = textarea_value(txn, id);
    } else if (tag == "select") {
        // A <select> has no `value` attribute: its value is the SELECTED
        // OPTION'S. Seeded from the attribute rather than from the option's
        // text, because those differ - `<option value=g>green</option>` is
        // worth "g" to a form and shows "green" to a reader, and reading
        // the label back as the value is a silent wrong answer.
        seeded.value = std::string{selected_option_value(txn, atoms, id)};
    } else {
        seeded.value = txn.attribute_value(id, atoms.intern("value"));
    }
    seeded.type = type_of(txn, atoms, id);
    const std::string_view mode = input_types::value_mode_of(seeded.type);
    if (mode == "filename") {
        seeded.value.clear();
    } else if (mode == "default/on" && !txn.has_attribute(id, atoms.intern("value"))) {
        seeded.value = "on";
    } else if (mode == "value") {
        seeded.value = sanitized(txn, atoms, id, seeded.type, std::move(seeded.value));
    }
    seeded.checked = txn.has_attribute(id, atoms.intern("checked"));
    // THE SELECTION STARTS AT 0, 0 (defaultSelection.html): only the `value`
    // setter and typing move it to the end.
    seeded.caret = 0;
    seeded.selection = 0;
    return states_.emplace(id.key(), std::move(seeded)).first->second;
}

std::vector<std::pair<node_id, std::string>> form_store::settle_types(const read_txn & txn,
                                                                      atom_table & atoms) {
    std::vector<std::pair<node_id, std::string>> writes;
    const atom input_tag = atoms.intern_lower("input");
    for (auto & [key, held] : states_) {
        const node_id id{static_cast<std::uint32_t>(key >> 32), static_cast<std::uint32_t>(key)};
        if (txn.tag(id).value_or(atom{}) != input_tag) { continue; }
        control_state & settled = state_of(txn, atoms, id);
        if (settled.pending_attribute) {
            writes.emplace_back(id, *settled.pending_attribute);
            settled.pending_attribute.reset();
        }
    }
    return writes;
}

bool form_store::assign_value(const read_txn & txn, atom_table & atoms, node_id id,
                              std::string text) {
    control_state & control = state_of(txn, atoms, id);
    return set_value(control, sanitized(txn, atoms, id, control.type, std::move(text)));
}

const control_state * form_store::find(node_id id) const {
    const auto it = states_.find(id.key());
    return it == states_.end() ? nullptr : &it->second;
}

bool form_store::set_value(control_state & control, std::string text) {
    control.value_edited = true;
    if (control.value == text) { return false; }
    control.value = std::move(text);
    control.caret = control.value.size();
    control.selection = control.caret;
    return true;
}

void form_store::insert_text(control_state & control, std::string_view text) {
    erase_selection(control);
    control.value.insert(control.caret, text);
    control.caret += text.size();
    control.selection = control.caret;
    control.value_edited = true;
}

bool form_store::backspace(control_state & control) {
    if (control.caret != control.selection) {
        erase_selection(control);
        control.value_edited = true;
        return true;
    }
    if (control.caret == 0) { return false; }
    // One CODE POINT, not one byte. Deleting a byte out of a multi-byte
    // character leaves invalid UTF-8 in the value, which then renders as
    // replacement characters for the rest of the field.
    const std::size_t start = previous_code_point(control.value, control.caret);
    control.value.erase(start, control.caret - start);
    control.caret = start;
    control.selection = start;
    control.value_edited = true;
    return true;
}

bool form_store::delete_forward(control_state & control) {
    if (control.caret != control.selection) {
        erase_selection(control);
        control.value_edited = true;
        return true;
    }
    if (control.caret >= control.value.size()) { return false; }
    const std::size_t end = next_code_point(control.value, control.caret);
    control.value.erase(control.caret, end - control.caret);
    control.value_edited = true;
    return true;
}

bool form_store::move_caret(control_state & control, int by, bool extend) {
    const std::size_t before = control.caret;
    if (by < 0 && control.caret > 0) {
        control.caret = previous_code_point(control.value, control.caret);
    } else if (by > 0 && control.caret < control.value.size()) {
        control.caret = next_code_point(control.value, control.caret);
    }
    if (!extend) { control.selection = control.caret; }
    return control.caret != before;
}

bool form_store::move_to_edge(control_state & control, bool to_end, bool extend) {
    const std::size_t before = control.caret;
    control.caret = to_end ? control.value.size() : 0;
    if (!extend) { control.selection = control.caret; }
    return control.caret != before;
}

void form_store::select_all(control_state & control) {
    control.selection = 0;
    control.caret = control.value.size();
}

std::string form_store::selected_text(const control_state & control) {
    const std::size_t from = std::min(control.caret, control.selection);
    const std::size_t to = std::max(control.caret, control.selection);
    if (from >= to || to > control.value.size()) { return {}; }
    return control.value.substr(from, to - from);
}

bool form_store::delete_selection(control_state & control) {
    const std::size_t from = std::min(control.caret, control.selection);
    const std::size_t to = std::max(control.caret, control.selection);
    if (from >= to || to > control.value.size()) { return false; }
    control.value.erase(from, to - from);
    control.caret = from;
    control.selection = from;
    // EDITED, like every other mutator here. It was the one that did not say
    // so, which did not matter while the `value` attribute was read once and
    // never again - and the moment the attribute became live, a Cut put the
    // original text straight back.
    control.value_edited = true;
    return true;
}

void form_store::toggle(const read_txn & txn, atom_table & atoms, node_id id, control_kind kind) {
    control_state & control = state_of(txn, atoms, id);
    if (kind == control_kind::checkbox) {
        control.checked = !control.checked;
        return;
    }
    if (kind != control_kind::radio) { return; }
    const atom name_attr = atoms.intern("name");
    const std::string_view group = txn.attribute_value(id, name_attr);
    const auto walk = [&](auto && self, node_id at) -> void {
        if (txn.attribute_value(at, name_attr) == group && at != id) {
            const std::string_view type = txn.attribute_value(at, atoms.intern("type"));
            if (type == "radio") { state_of(txn, atoms, at).checked = false; }
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    // An unnamed radio is its own group, per spec - so do not clear anything.
    if (!group.empty()) { walk(walk, txn.root()); }
    control.checked = true;
}

void form_store::reset_form(const read_txn & txn, node_id form) {
    const auto walk = [&](auto && self, node_id at) -> void {
        states_.erase(at.key());
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, form);
}

void form_store::erase_selection(control_state & control) {
    if (control.caret == control.selection) { return; }
    const std::size_t from = std::min(control.caret, control.selection);
    const std::size_t to = std::max(control.caret, control.selection);
    control.value.erase(from, to - from);
    control.caret = from;
    control.selection = from;
}

std::size_t form_store::previous_code_point(std::string_view text, std::size_t at) {
    if (at == 0) { return 0; }
    --at;
    while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0u) == 0x80u) { --at; }
    return at;
}

std::size_t form_store::next_code_point(std::string_view text, std::size_t at) {
    if (at >= text.size()) { return text.size(); }
    ++at;
    while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0u) == 0x80u) { ++at; }
    return at;
}

} // namespace ctbrowser::shell
