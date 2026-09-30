#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
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
    // The type is an enumerated attribute, so ASCII case-insensitive:
    // `type=CHECKBOX` is a checkbox.
    if (ascii_iequals(type, "checkbox")) { return control_kind::checkbox; }
    if (ascii_iequals(type, "radio")) { return control_kind::radio; }
    if (ascii_iequals_any(type, {"button", "submit", "reset"})) { return control_kind::button; }
    if (ascii_iequals(type, "hidden")) { return control_kind::none; }
    return control_kind::text;
}

struct control_state {
    std::string value;
    std::size_t caret = 0;     // in CODE UNITS of `value`
    std::size_t selection = 0; // anchor; equal to caret means no selection
    bool checked = false;
    bool indeterminate = false;  // the IDL-only flag; cloned with checkedness (4.10.5.1.18)
    bool checked_edited = false; // dirty checkedness: until set, `checked` follows the attribute
    bool value_edited = false;   // once true, the `value` attribute stops being the answer
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

    // A SUBMIT BUTTON (HTML 4.10.2): an <input> whose type is submit or image,
    // or a <button> in the Submit Button state - `type=submit`, or the Auto
    // state (no type, or one that is not a keyword) without a `command` or a
    // `commandfor`, which make it a plain button (4.10.6).
    [[nodiscard]] static bool is_submit_button(const read_txn & txn, atom_table & atoms,
                                               node_id id) {
        if (txn.element_ns(id) != node_ns::html) { return false; }
        const std::string_view type = txn.attribute_value(id, atoms.intern("type"));
        if (txn.local_name(id) == "input") { return ascii_iequals_any(type, {"submit", "image"}); }
        if (txn.local_name(id) != "button") { return false; }
        if (ascii_iequals_any(type, {"submit", "reset", "button"})) {
            return ascii_iequals(type, "submit");
        }
        return !txn.has_attribute(id, atoms.intern("command")) &&
               !txn.has_attribute(id, atoms.intern("commandfor"));
    }

    // A control's FORM OWNER (HTML 4.10.18.3): connected with a `form`
    // attribute, the first element in its tree whose id is the value - when that is an HTML
    // <form>, and none at all otherwise - else the form the parser associated
    // it with, else its nearest ancestor HTML <form>. A button outside its
    // form that names it submits it. THE ONE
    // ALGORITHM: the browser's default actions and every binding that asks
    // for a form owner (control_helpers::form_owner) come here.
    [[nodiscard]] static node_id owning_form(const read_txn & txn, atom_table & atoms,
                                             node_id from) {
        const atom form_tag = atoms.intern_lower("form");
        const auto is_form = [&](node_id at) {
            return txn.element_ns(at) == node_ns::html && txn.tag(at).value_or(atom{}) == form_tag;
        };
        // ONLY A CONNECTED control's `form` attribute names its owner (step 4);
        // a disconnected one falls through to its nearest form ancestor, which
        // is how the one inside a removed form keeps it and the one beside a
        // removed form loses its (form_attribute.html, form_owner_and_table).
        const node_id top = txn.root_of_tree(from, true);
        const bool connected =
            top == txn.root() || txn.kind(top).value_or(node_kind::element) == node_kind::document;
        if (const atom named = atoms.intern("form"); connected && txn.has_attribute(from, named)) {
            const std::string_view id = txn.attribute_value(from, named);
            if (id.empty()) { return node_id{}; }
            const atom id_attribute = atoms.intern("id");
            std::vector<node_id> pending{txn.root_of_tree(from)};
            while (!pending.empty()) {
                const node_id at = pending.back();
                pending.pop_back();
                if (txn.attribute_value(at, id_attribute) == id) {
                    return is_form(at) ? at : node_id{};
                }
                const std::span<const node_id> children = txn.children(at);
                for (std::size_t i = children.size(); i-- > 0;) { pending.push_back(children[i]); }
            }
            return node_id{};
        }
        // THE FORM ELEMENT POINTER'S ASSOCIATION, which outlives the parse:
        // `<table><form><tr><td><input>` pops the form at once and the input
        // is still its control, with the form nowhere above it. The parser
        // inserted flag keeps that until a "reset the form owner" - a removal
        // that parts the two trees, or a `form` attribute - and the document
        // drops the pair at exactly those two (document::parser_form).
        if (const node_id parsed = txn.parser_form(from)) { return parsed; }
        for (node_id at = txn.parent(from); at; at = txn.parent(at)) {
            if (is_form(at)) { return at; }
        }
        return node_id{};
    }

    // HTML 4.10.18.5 "actually disabled": the element's own `disabled`, or a
    // disabled <fieldset> ancestor - unless the element is inside that
    // fieldset's first <legend> child, which the fieldset leaves enabled.
    [[nodiscard]] static bool is_actually_disabled(const read_txn & txn, atom_table & atoms,
                                                   node_id id) {
        const atom disabled = atoms.intern("disabled");
        if (txn.has_attribute(id, disabled)) { return true; }
        const atom fieldset = atoms.intern_lower("fieldset");
        const atom legend = atoms.intern_lower("legend");
        for (node_id child = id, at = txn.parent(id); at; child = at, at = txn.parent(at)) {
            if (txn.tag(at).value_or(atom{}) != fieldset || !txn.has_attribute(at, disabled)) {
                continue;
            }
            node_id first_legend;
            for (const node_id one : txn.children(at)) {
                if (txn.tag(one).value_or(atom{}) == legend) {
                    first_legend = one;
                    break;
                }
            }
            if (child != first_legend) { return true; }
        }
        return false;
    }

    // WHAT A BUTTON'S ACTIVATION DOES TO ITS FORM (4.10.6, 4.10.5.1.18-19): a
    // submit button submits its form owner and a reset button resets it.
    // `form` is empty when the element is neither, is actually disabled or
    // has no form owner. browser::activate and a frame document's activation
    // (bindings/frames.cpp) both ask this.
    struct form_action {
        node_id form;
        bool submits = false;
        bool resets = false;
    };
    [[nodiscard]] static form_action form_action_of(const read_txn & txn, atom_table & atoms,
                                                    node_id id) {
        if (!txn.contains(id) || txn.element_ns(id) != node_ns::html ||
            is_actually_disabled(txn, atoms, id)) {
            return {};
        }
        const std::string_view local = txn.local_name(id);
        form_action out;
        out.submits = is_submit_button(txn, atoms, id);
        out.resets = (local == "button" || local == "input") &&
                     ascii_iequals(txn.attribute_value(id, atoms.intern("type")), "reset");
        if (out.submits || out.resets) { out.form = owning_form(txn, atoms, id); }
        return out;
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

// --- a request an embedder may answer --------------------------------------

// This engine opens no socket for a page loaded from disk: every byte a frame,
// a fetch() or an XHR gets comes out of the asset registry, which answers a GET
// for a FILE and nothing else. A form POSTed to a script, or a GET whose answer
// depends on its query, has nobody to answer it - and a request handler
// (browser_options::on_request) is that somebody: given the method, the
// absolute URL, the request's Content-Type and its body, it returns a
// response, or nullopt to leave the request to the registry as before.
//
// ONE CALLER FILLS IT IN THIS TREE: ctdrive, under a document root, with
// emulations of the web-platform-tests' Python handlers
// (tools/ctdrive/wpt_handlers.cpp) - the server half of a form submission
// test. An application has no reason to set it.
struct resource_request {
    std::string method = "GET";
    std::string url;          // absolute, fragment removed
    std::string content_type; // the request's Content-Type; empty for none
    std::string body;         // the entity body's bytes
};

struct resource_reply {
    int status = 200;
    std::string content_type; // the response's Content-Type, parameters and all
    std::string body;         // its bytes
};

using request_handler = std::function<std::optional<resource_reply>(const resource_request &)>;

// --- form submission, HTML 4.10.21.3-9 ---------------------------------------

// ONE ENTRY OF A FORM'S ENTRY LIST as the three encodings read it: a name and
// a string - or a File, whose name, type and bytes multipart/form-data sends
// and the other two reduce to the name. Text is UTF-8 of scalar values.
struct form_entry {
    std::string name;
    std::string value; // a string entry's text; a File's bytes
    bool file = false;
    std::string filename;
    std::string type; // a File's type; "" is application/octet-stream
};

// The three submission bodies over an entry list, each ENCODED in `encoding`
// - the one "picking an encoding for the form" chose - with the Encoding
// Standard's "html" error mode, so a character the encoding lacks is `&#N;`:
// application/x-www-form-urlencoded (URL 5.2, also a GET's query),
// text/plain (4.10.21.9) and multipart/form-data (4.10.21.8). Names and
// string values have their line breaks normalised to CR LF first (4.10.21.6
// and .8). A legacy multi-byte encoding has no encoder here and is sent as
// UTF-8.
[[nodiscard]] std::string urlencoded_form_body(const std::vector<form_entry> & entries,
                                               std::string_view encoding);
[[nodiscard]] std::string text_plain_form_body(const std::vector<form_entry> & entries,
                                               std::string_view encoding);
// multipart/form-data under a boundary of its OWN: `----ctbrowser` and 24
// random alphanumerics from std::random_device - not the page's seeded
// Math.random, which a page can predict - drawn again should any name, value
// or file contain it. A fixed public boundary let a value end its part early
// and forge fields after it.
struct multipart_body {
    std::string boundary;
    std::string body;
};
[[nodiscard]] multipart_body multipart_form_body(const std::vector<form_entry> & entries,
                                                 std::string_view encoding);

class dom_bindings;

// A SUBMISSION'S NAVIGATION, as build_submission makes it: the target it
// names, the URL as the page wrote it - the name the asset registry is asked
// for - and the request (method, absolute URL, Content-Type and body).
struct form_navigation {
    std::string target;
    std::string src;
    resource_request request;
};

// ...PLANNED, once a navigable is chosen (bindings/frames.cpp): the form's
// document and the form - whose one planned navigation a later one replaces;
// both empty for a script's navigation (a Location setter, window.open) - and
// the <iframe> it navigates, by the bindings of the document it is in.
struct planned_navigation {
    dom_bindings * form_document = nullptr;
    node_id form;
    dom_bindings * navigable = nullptr;
    node_id element;
    form_navigation navigation;
};

} // namespace ctbrowser::shell
