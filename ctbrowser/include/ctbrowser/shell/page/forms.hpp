#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/shell/page/input_types.hpp>
#include <ctbrowser/style/style.hpp>

// Form control state.
//
// Kept in a store keyed by node_id, NOT on the node: value, caret, selection
// and scroll offset are fields only the shell cares about, and a document
// snapshot must not mean "including whatever the user had half-typed".
//
// The distinction is real: a control's VALUE is document state after the user
// edits it (the `value` attribute stops being the answer), while its caret and
// selection are interaction state that belongs to the view. Both live here
// because both are the shell's business, but only the first is what a script
// reads back through `.value`.

namespace ctbrowser::shell {

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
    // THE INPUT TYPE STATE `value` WAS LAST SANITISED FOR (HTML 4.10.5.1), so
    // state_of notices a type change on the next read and runs the "type
    // attribute changes" steps and the new state's sanitization. Empty until
    // first seeded; the tag for a control that is not an <input>.
    std::string type;
    // A `value` content attribute those steps want written (step 1: a value
    // carried into the default modes). The store reads under a read
    // transaction and cannot write; the bindings' `value` accessor does, and
    // until then this IS the value the default mode reads.
    std::optional<std::string> pending_attribute;
    // WHERE THE FIELD IS LOOKING. A control's value can be bigger than the box
    // it is drawn in, in either direction, and these say which part of it is on
    // screen. View state, so it belongs here beside the caret rather than on
    // the node - the node is document content.
    //
    // The first VISUAL line a textarea shows. A textarea is sized by its `rows`
    // and does not grow, so once its value wraps past the bottom the caret
    // would type off the end of a box that cannot show it.
    std::size_t scroll_line = 0;
    // How far right the view has moved, in PIXELS rather than characters: the
    // painter and the click mapping both work in pixels against measure(), and
    // a character offset would quantise the scroll so a caret at the right edge
    // could never sit flush against it. Applies to a textarea too - an
    // unbreakable word longer than the line is returned whole by
    // words_that_fit, so a wrapped field can still overflow sideways.
    float scroll_x = 0;
};

class form_store {
public:
    // The live state of a control, seeded from its attributes the first time it
    // is asked for. Seeding lazily is what makes `<input value=hi>` show "hi"
    // without the store having to walk the document up front.
    [[nodiscard]] control_state & state_of(const read_txn & txn, atom_table & atoms, node_id id);

    [[nodiscard]] const control_state * find(node_id id) const;

    // HTML 4.10.5 / 4.10.11's cloning steps: the copy takes the source's
    // value, dirty value flag, checkedness and dirty checkedness - and NOT
    // its selection, which is a fresh control's, collapsed at 0
    // (select-event.html reads the clone's selectionEnd as 0 and expects a
    // `select()` on it to be a change). A source nobody has touched has no
    // state and the copy seeds from its attributes, which is the same answer.
    void clone_state(node_id source, node_id made) {
        if (const auto it = states_.find(source.key()); it != states_.end()) {
            control_state copy = it->second;
            copy.pending_attribute.reset();
            copy.caret = 0;
            copy.selection = 0;
            states_.insert_or_assign(made.key(), std::move(copy));
        }
    }

    // After a DOM write: every seeded <input> whose type attribute moved runs
    // its type-change steps now (state_of does), and the `value` attributes
    // those steps left pending are handed to `write` - the store cannot write
    // under its read transaction, so the caller does, outside one.
    // ponytail: O(seeded controls) per mutation; index by type write if a
    // page with thousands of controls ever mutates in a storm.
    [[nodiscard]] std::vector<std::pair<node_id, std::string>> settle_types(const read_txn & txn,
                                                                            atom_table & atoms);

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

    // HTML 4.10.7 "list of options": the option descendants in tree order,
    // excluding those inside another select, an option, an hr, or an
    // optgroup nested in an optgroup (select-selectedOptions-nesting.html).
    [[nodiscard]] static std::vector<node_id> list_of_options(const read_txn & txn,
                                                              atom_table & atoms, node_id select) {
        const atom option_tag = atoms.intern_lower("option");
        const atom optgroup_tag = atoms.intern_lower("optgroup");
        const atom select_tag = atoms.intern_lower("select");
        const atom datalist_tag = atoms.intern_lower("datalist");
        const atom hr_tag = atoms.intern_lower("hr");
        std::vector<node_id> out;
        const auto walk = [&](auto && self, node_id at, bool in_group) -> void {
            for (const node_id child : txn.children(at)) {
                const atom tag = txn.tag(child).value_or(atom{});
                if (tag == option_tag) {
                    out.push_back(child);
                    continue;
                }
                if (tag == select_tag || tag == datalist_tag || tag == hr_tag) { continue; }
                const bool group = tag == optgroup_tag;
                if (group && in_group) { continue; }
                self(self, child, in_group || group);
            }
        };
        walk(walk, select, false);
        return out;
    }

    // The value a <select> starts with: the `selected` option's, else the
    // first one's, which is what a browser shows in a select nobody has
    // touched.
    [[nodiscard]] static std::string selected_option_value(const read_txn & txn, atom_table & atoms,
                                                           node_id select) {
        const atom selected = atoms.intern("selected");
        std::string first;
        bool have_first = false;
        for (const node_id child : list_of_options(txn, atoms, select)) {
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
    // Answers whether the value CHANGED: HTML 4.10.5.3 moves the caret to the
    // end (and resets the selection direction) only when the new value
    // differs from the old (selection-after-content-change.html).
    static bool set_value(control_state & control, std::string text);
    // The same through the input's type state: the value sanitization
    // algorithm runs over `text` first (HTML 4.10.5.1 - `input.value = "a\nb"`
    // is "ab" on a text input and "" on a number one).
    bool assign_value(const read_txn & txn, atom_table & atoms, node_id id, std::string text);

    // Insert typed text at the caret, replacing any selection.
    void insert_text(control_state & control, std::string_view text);

    // Returns whether anything changed, so the caller can skip a repaint.
    bool backspace(control_state & control);

    bool delete_forward(control_state & control);

    bool move_caret(control_state & control, int by, bool extend);

    bool move_to_edge(control_state & control, bool to_end, bool extend);

    void select_all(control_state & control);

    // What is selected, as text. Empty when the caret and the anchor are in the
    // same place, which is what "nothing is selected" is.
    [[nodiscard]] static std::string selected_text(const control_state & control);

    // Remove it, leaving the caret where the selection started - which is where
    // typing over a selection has to continue from.
    static bool delete_selection(control_state & control);

    // --- checkboxes and radios --------------------------------------------

    // A radio deselects every other radio with the same name in the document -
    // which is why this needs the document and a checkbox toggle does not.
    void toggle(const read_txn & txn, atom_table & atoms, node_id id, control_kind kind);

    // --- forms ------------------------------------------------------------

    // Reset every control in the form back to its markup. `value_edited` is
    // what makes this different from doing nothing: it is the flag that said
    // the attribute had stopped being the answer.
    void reset_form(const read_txn & txn, node_id form);

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

    // The UTF-8 code point boundary on either side of a byte offset - a caret
    // moves by these, never by bytes. Clamped: past the end is the end. The
    // browser's caret and password masking step with the same two.
    [[nodiscard]] static std::size_t previous_code_point(std::string_view text, std::size_t at);
    [[nodiscard]] static std::size_t next_code_point(std::string_view text, std::size_t at);

private:
    void erase_selection(control_state & control);

    flat_map<std::uint64_t, control_state> states_;
};

} // namespace ctbrowser::shell
