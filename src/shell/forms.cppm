module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/style/style.hpp>

export module ctbrowser.shell:forms;

// Form control state.
//
// Kept in a store keyed by node_id, NOT on the node. the previous engine put value, caret,
// selection and scroll offset on `node`, which is what made its node struct
// carry thirty-odd fields that only the shell cared about - and what made a
// document snapshot mean "including whatever the user had half-typed".
//
// The distinction is real: a control's VALUE is document state after the user
// edits it (the `value` attribute stops being the answer), while its caret and
// selection are interaction state that belongs to the view. Both live here
// because both are the shell's business, but only the first is what a script
// reads back through `.value`.

export namespace ctbrowser::shell {

enum class control_kind : std::uint8_t {
    none,
    text, // <input type=text|search|url|email|tel|password> and friends
    checkbox,
    radio,
    button, // <button>, <input type=button|submit|reset>
    textarea,
    select,
};

[[nodiscard]] inline control_kind control_kind_of(std::string_view tag, std::string_view type) {
    if (tag == "textarea") { return control_kind::textarea; }
    if (tag == "select") { return control_kind::select; }
    if (tag == "button") { return control_kind::button; }
    if (tag != "input") { return control_kind::none; }
    if (type == "checkbox") { return control_kind::checkbox; }
    if (type == "radio") { return control_kind::radio; }
    if (type == "button" || type == "submit" || type == "reset") { return control_kind::button; }
    if (type == "hidden") { return control_kind::none; }
    return control_kind::text;
}

struct control_state {
    std::string value;
    std::size_t caret = 0;     // in CODE UNITS of `value`
    std::size_t selection = 0; // anchor; equal to caret means no selection
    bool checked = false;
    bool value_edited = false; // once true, the `value` attribute stops being the answer
};

class form_store {
public:
    // The live state of a control, seeded from its attributes the first time it
    // is asked for. Seeding lazily is what makes `<input value=hi>` show "hi"
    // without the store having to walk the document up front.
    [[nodiscard]] control_state & state_of(const read_txn & txn, atom_table & atoms, node_id id) {
        auto it = states_.find(id.key());
        if (it != states_.end()) { return it->second; }
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

    [[nodiscard]] const control_state * find(node_id id) const {
        const auto it = states_.find(id.key());
        return it == states_.end() ? nullptr : &it->second;
    }

    void clear() { states_.clear(); }

    // --- editing ----------------------------------------------------------

    // An <option>'s value: the attribute if it has one, else its text. That
    // fallback is the HTML rule, not a convenience.
    [[nodiscard]] static std::string option_value(const read_txn & txn, atom_table & atoms,
                                                  node_id option) {
        const atom value_attr = atoms.intern("value");
        if (txn.has_attribute(option, value_attr)) {
            return std::string{txn.attribute_value(option, value_attr)};
        }
        std::string text;
        for (const node_id child : txn.children(option)) { text += txn.text(child); }
        return text;
    }

    // The value a <select> starts with: the `selected` option's, else the
    // first one's, which is what a browser shows in a select nobody has
    // touched.
    [[nodiscard]] static std::string selected_option_value(const read_txn & txn, atom_table & atoms,
                                                           node_id select) {
        const atom option_tag = atoms.intern_lower("option");
        const atom selected = atoms.intern("selected");
        std::string first;
        bool have_first = false;
        for (const node_id child : txn.children(select)) {
            if (txn.tag(child).value_or(atom{}) != option_tag) { continue; }
            if (!have_first) {
                first = option_value(txn, atoms, child);
                have_first = true;
            }
            if (txn.has_attribute(child, selected)) { return option_value(txn, atoms, child); }
        }
        return first;
    }

    // Replace the whole value - what `input.value = "x"` does. The caret goes
    // to the end, which is where a browser puts it, and the control counts as
    // edited so the `value` attribute stops being the answer.
    static void set_value(control_state & control, std::string text) {
        control.value = std::move(text);
        control.caret = control.value.size();
        control.selection = control.caret;
        control.value_edited = true;
    }

    // Insert typed text at the caret, replacing any selection.
    void insert_text(control_state & control, std::string_view text) {
        erase_selection(control);
        control.value.insert(control.caret, text);
        control.caret += text.size();
        control.selection = control.caret;
        control.value_edited = true;
    }

    // Returns whether anything changed, so the caller can skip a repaint.
    bool backspace(control_state & control) {
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

    bool delete_forward(control_state & control) {
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

    bool move_caret(control_state & control, int by, bool extend) {
        const std::size_t before = control.caret;
        if (by < 0 && control.caret > 0) {
            control.caret = previous_code_point(control.value, control.caret);
        } else if (by > 0 && control.caret < control.value.size()) {
            control.caret = next_code_point(control.value, control.caret);
        }
        if (!extend) { control.selection = control.caret; }
        return control.caret != before;
    }

    bool move_to_edge(control_state & control, bool to_end, bool extend) {
        const std::size_t before = control.caret;
        control.caret = to_end ? control.value.size() : 0;
        if (!extend) { control.selection = control.caret; }
        return control.caret != before;
    }

    void select_all(control_state & control) {
        control.selection = 0;
        control.caret = control.value.size();
    }

    // What is selected, as text. Empty when the caret and the anchor are in the
    // same place, which is what "nothing is selected" is.
    [[nodiscard]] static std::string selected_text(const control_state & control) {
        const std::size_t from = std::min(control.caret, control.selection);
        const std::size_t to = std::max(control.caret, control.selection);
        if (from >= to || to > control.value.size()) { return {}; }
        return control.value.substr(from, to - from);
    }

    // Remove it, leaving the caret where the selection started - which is where
    // typing over a selection has to continue from.
    static bool delete_selection(control_state & control) {
        const std::size_t from = std::min(control.caret, control.selection);
        const std::size_t to = std::max(control.caret, control.selection);
        if (from >= to || to > control.value.size()) { return false; }
        control.value.erase(from, to - from);
        control.caret = from;
        control.selection = from;
        return true;
    }

    // --- checkboxes and radios --------------------------------------------

    // A radio deselects every other radio with the same name in the document -
    // which is why this needs the document and a checkbox toggle does not.
    void toggle(const read_txn & txn, atom_table & atoms, node_id id, control_kind kind) {
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

    // --- forms ------------------------------------------------------------

    // Reset every control in the form back to its markup. `value_edited` is
    // what makes this different from doing nothing: it is the flag that said
    // the attribute had stopped being the answer.
    void reset_form(const read_txn & txn, node_id form) {
        const auto walk = [&](auto && self, node_id at) -> void {
            states_.erase(at.key());
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        walk(walk, form);
    }

    // The successful controls of a form, as name/value pairs, in document
    // order. This is what a submission would send - there is no network yet, so
    // producing the data and stopping there is the honest half.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> form_data(const read_txn & txn,
                                                                             atom_table & atoms,
                                                                             node_id form) {
        std::vector<std::pair<std::string, std::string>> out;
        const atom name_attr = atoms.intern("name");
        const atom type_attr = atoms.intern("type");
        const auto walk = [&](auto && self, node_id at) -> void {
            const std::string_view name = txn.attribute_value(at, name_attr);
            if (!name.empty()) {
                const std::string_view tag = atoms.text(txn.tag(at).value_or(atom{}));
                const control_kind kind = control_kind_of(tag, txn.attribute_value(at, type_attr));
                if (kind != control_kind::none && kind != control_kind::button) {
                    control_state & control = state_of(txn, atoms, at);
                    // Unchecked boxes are NOT successful controls and are omitted
                    // entirely - a server that sees the name knows it was ticked.
                    const bool toggleable =
                        kind == control_kind::checkbox || kind == control_kind::radio;
                    if (!toggleable || control.checked) {
                        std::string value = control.value;
                        if (toggleable && value.empty()) { value = "on"; }
                        out.emplace_back(std::string{name}, std::move(value));
                    }
                }
            }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        walk(walk, form);
        return out;
    }

    // The <form> an element is inside, if any.
    [[nodiscard]] static node_id owning_form(const read_txn & txn, atom_table & atoms,
                                             node_id from) {
        const atom form_tag = atoms.intern_lower("form");
        for (node_id at = from; at; at = txn.parent(at)) {
            if (txn.tag(at).value_or(atom{}) == form_tag) { return at; }
        }
        return node_id{};
    }

private:
    void erase_selection(control_state & control) {
        if (control.caret == control.selection) { return; }
        const std::size_t from = std::min(control.caret, control.selection);
        const std::size_t to = std::max(control.caret, control.selection);
        control.value.erase(from, to - from);
        control.caret = from;
        control.selection = from;
    }

    [[nodiscard]] static std::size_t previous_code_point(const std::string & text, std::size_t at) {
        if (at == 0) { return 0; }
        --at;
        while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0u) == 0x80u) { --at; }
        return at;
    }
    [[nodiscard]] static std::size_t next_code_point(const std::string & text, std::size_t at) {
        if (at >= text.size()) { return text.size(); }
        ++at;
        while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0u) == 0x80u) {
            ++at;
        }
        return at;
    }

    flat_map<std::uint64_t, control_state> states_;
};

} // namespace ctbrowser::shell
