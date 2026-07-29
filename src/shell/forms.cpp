#include <ctbrowser/shell/forms.hpp>

// forms: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::shell {

control_state & form_store::state_of(const read_txn & txn, atom_table & atoms, node_id id) {
    auto it = states_.find(id.key());
    if (it != states_.end()) {
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
        control_state & held = it->second;
        if (!held.value_edited) {
            const std::string_view tag = atoms.text(txn.tag(id).value_or(atom{}));
            if (tag != "textarea" && tag != "select") {
                const std::string_view attribute = txn.attribute_value(id, atoms.intern("value"));
                if (attribute != held.value) {
                    held.value = std::string{attribute};
                    held.caret = held.value.size();
                    held.selection = held.caret;
                }
            }
        }
        return held;
    }
    control_state seeded;
    const std::string_view tag = atoms.text(txn.tag(id).value_or(atom{}));
    if (tag == "textarea") {
        for (const node_id child : txn.children(id)) { seeded.value += txn.text(child); }
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
    seeded.checked = txn.has_attribute(id, atoms.intern("checked"));
    seeded.caret = seeded.value.size();
    seeded.selection = seeded.caret;
    return states_.emplace(id.key(), std::move(seeded)).first->second;
}

const control_state * form_store::find(node_id id) const {
    const auto it = states_.find(id.key());
    return it == states_.end() ? nullptr : &it->second;
}

void form_store::set_value(control_state & control, std::string text) {
    control.value = std::move(text);
    control.caret = control.value.size();
    control.selection = control.caret;
    control.value_edited = true;
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

std::size_t form_store::previous_code_point(const std::string & text, std::size_t at) {
    if (at == 0) { return 0; }
    --at;
    while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0u) == 0x80u) { --at; }
    return at;
}

std::size_t form_store::next_code_point(const std::string & text, std::size_t at) {
    if (at >= text.size()) { return text.size(); }
    ++at;
    while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0u) == 0x80u) { ++at; }
    return at;
}

} // namespace ctbrowser::shell
