//===- forms.hpp - Form control state and submission ------------*- C++ -*-===//
//
// Form control state, kept in a store keyed by node_id rather than on the
// node: value, caret, selection and scroll offset are fields only the shell
// cares about, and a document snapshot must not include whatever the user had
// half-typed.
//
// A control's value is document state once the user edits it (the `value`
// attribute stops being the answer), while its caret and selection are view
// state. Both live here, but only the value is what script reads back through
// `.value`. The file also declares the form submission encoders and the
// request handler an embedder may install.
//
//===----------------------------------------------------------------------===//

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

namespace ctbrowser::shell {

/// Which editing behaviour an element gets; `none` for non-controls.
enum class control_kind : std::uint8_t {
    none,
    text, // <input type=text|search|url|email|tel|password> and friends
    checkbox,
    radio,
    button, // <button>, <input type=button|submit|reset>
    textarea,
    select,
};

/// Classifies an element by tag name and `type` attribute.
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

/// The live state of one form control.
struct control_state {
    std::string value;
    std::size_t caret = 0;     // in code units of `value`
    std::size_t selection = 0; // anchor; equal to caret means no selection
    bool checked = false;
    bool indeterminate = false;  // the IDL-only flag; cloned with checkedness (4.10.5.1.18)
    bool checked_edited = false; // dirty checkedness: until set, `checked` follows the attribute
    bool value_edited = false;   // once true, the `value` attribute stops being the answer
    /// The input type state `value` was last sanitised for (HTML 4.10.5.1),
    /// so state_of notices a type change on the next read and runs the "type
    /// attribute changes" steps and the new state's sanitization. Empty until
    /// first seeded; the tag name for a control that is not an <input>.
    std::string type;
    /// A `value` content attribute those steps want written (a value carried
    /// into the default modes). The store reads under a read transaction and
    /// cannot write; the bindings' `value` accessor writes it, and until then
    /// this is the value the default mode reads.
    std::optional<std::string> pending_attribute;
    // Scroll position: a control's value can be bigger than its box in either
    // direction, and these say which part is on screen. View state, so it
    // lives here beside the caret rather than on the node.

    /// The first visual line a textarea shows. A textarea is sized by its
    /// `rows` and does not grow, so once its value wraps past the bottom the
    /// view must scroll to keep the caret visible.
    std::size_t scroll_line = 0;
    /// How far right the view has moved, in pixels rather than characters:
    /// the painter and the click mapping both work in pixels against
    /// measure(), and a character offset would quantise the scroll so a caret
    /// at the right edge could never sit flush against it. Applies to a
    /// textarea too: words_that_fit returns an unbreakable word longer than
    /// the line whole, so a wrapped field can still overflow sideways.
    float scroll_x = 0;
};

/// Every form control's state in a document, plus the HTML form algorithms
/// (form owner, submit buttons, disabled state) that read it.
class form_store {
public:
    /// The live state of a control, seeded from its attributes the first time
    /// it is asked for, so the store never walks the document up front.
    [[nodiscard]] control_state & state_of(const read_txn & txn, atom_table & atoms, node_id id);

    [[nodiscard]] const control_state * find(node_id id) const;

    /// HTML 4.10.5 / 4.10.11's cloning steps: the copy takes the source's
    /// value, dirty value flag, checkedness and dirty checkedness, but not its
    /// selection, which starts collapsed at 0 as in a fresh control
    /// (select-event.html reads the clone's selectionEnd as 0 and expects a
    /// `select()` on it to be a change). A source nobody has touched has no
    /// state, and the copy seeds from its attributes, which gives the same
    /// answer.
    void clone_state(node_id source, node_id made) {
        if (const auto it = states_.find(source.key()); it != states_.end()) {
            control_state copy = it->second;
            copy.pending_attribute.reset();
            copy.caret = 0;
            copy.selection = 0;
            states_.insert_or_assign(made.key(), std::move(copy));
        }
    }

    /// Called after a DOM write: every seeded <input> whose type attribute
    /// changed runs its type-change steps now (through state_of), and the
    /// `value` attributes those steps left pending are returned. The store
    /// cannot write under its read transaction, so the caller writes them.
    // ponytail: O(seeded controls) per mutation; index by type write if a
    // page with thousands of controls ever mutates in a storm.
    [[nodiscard]] std::vector<std::pair<node_id, std::string>> settle_types(const read_txn & txn,
                                                                            atom_table & atoms);

    void clear() { states_.clear(); }

    // --- editing ----------------------------------------------------------

    /// An <option>'s value: the attribute if it has one, else its text (the
    /// HTML rule).
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

    /// HTML 4.10.7 "list of options": the option descendants in tree order,
    /// excluding those inside another select, a datalist, an option, an hr,
    /// or an optgroup nested in an optgroup (select-selectedOptions-nesting.html).
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

    /// The value a <select> starts with: the `selected` option's, else the
    /// first one's.
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

    /// Replaces the whole value, as `input.value = "x"` does, and marks the
    /// control edited so the `value` attribute stops being the answer.
    /// Returns whether the value changed: HTML 4.10.5.3 moves the caret to the
    /// end (and resets the selection direction) only when the new value
    /// differs from the old (selection-after-content-change.html).
    static bool set_value(control_state & control, std::string text);
    /// The same through the input's type state: the value sanitization
    /// algorithm runs over `text` first (HTML 4.10.5.1: `input.value = "a\nb"`
    /// is "ab" on a text input and "" on a number one).
    bool assign_value(const read_txn & txn, atom_table & atoms, node_id id, std::string text);

    /// Inserts typed text at the caret, replacing any selection.
    void insert_text(control_state & control, std::string_view text);

    /// The editing commands below return whether anything changed, so the
    /// caller can skip a repaint.
    bool backspace(control_state & control);

    bool delete_forward(control_state & control);

    bool move_caret(control_state & control, int by, bool extend);

    bool move_to_edge(control_state & control, bool to_end, bool extend);

    void select_all(control_state & control);

    /// What is selected, as text. Empty when the caret and the anchor are in
    /// the same place, which means nothing is selected.
    [[nodiscard]] static std::string selected_text(const control_state & control);

    /// Removes the selection, leaving the caret where it started, which is
    /// where typing over a selection continues from.
    static bool delete_selection(control_state & control);

    // --- checkboxes and radios --------------------------------------------

    /// Toggles a checkbox or checks a radio. A radio deselects every other
    /// radio with the same name in the document, which is why this needs the
    /// document.
    void toggle(const read_txn & txn, atom_table & atoms, node_id id, control_kind kind);

    // --- forms ------------------------------------------------------------

    /// Resets every control in the form back to its markup, which includes
    /// clearing `value_edited` so the attributes become the answer again.
    void reset_form(const read_txn & txn, node_id form);

    /// The successful controls of a form as name/value pairs in document order.
    /// A simplified record that browser::last_submission() exposes after a
    /// submit; the real request body is built from a form_entry list by
    /// urlencoded_form_body and friends below.
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
                    // Unchecked boxes are not successful controls and are omitted
                    // entirely: a server that sees the name knows it was ticked.
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

    /// Whether `id` is a submit button (HTML 4.10.2): an <input> whose type is
    /// submit or image, or a <button> in the Submit Button state, meaning
    /// `type=submit`, or the Auto state (no type, or one that is not a
    /// keyword) without a `command` or `commandfor`, which would make it a
    /// plain button (HTML 4.10.6).
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

    /// A control's form owner (HTML 4.10.18.3): if connected and it has a
    /// `form` attribute, the first element in its tree with that id when it is
    /// an HTML <form> (none otherwise); else the form the parser associated it
    /// with; else its nearest ancestor HTML <form>. Every caller that needs a
    /// form owner, including control_helpers::form_owner, uses this function.
    [[nodiscard]] static node_id owning_form(const read_txn & txn, atom_table & atoms,
                                             node_id from) {
        const atom form_tag = atoms.intern_lower("form");
        const auto is_form = [&](node_id at) {
            return txn.element_ns(at) == node_ns::html && txn.tag(at).value_or(atom{}) == form_tag;
        };
        // Only a connected control's `form` attribute names its owner (step
        // 4); a disconnected one falls through to its nearest form ancestor.
        // So a control inside a removed form keeps it, and one beside a
        // removed form loses it (form_attribute.html, form_owner_and_table).
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
        // The parser's form element pointer association outlives the parse:
        // `<table><form><tr><td><input>` pops the form at once, yet the input
        // is still its control with the form nowhere above it. The association
        // lasts until "reset the form owner" runs (a removal that separates
        // the two trees, or a `form` attribute), and the document drops the
        // pair at exactly those two points (document::parser_form).
        if (const node_id parsed = txn.parser_form(from)) { return parsed; }
        for (node_id at = txn.parent(from); at; at = txn.parent(at)) {
            if (is_form(at)) { return at; }
        }
        return node_id{};
    }

    /// HTML 4.10.18.5 "actually disabled": the element's own `disabled`, or a
    /// disabled <fieldset> ancestor, unless the element is inside that
    /// fieldset's first <legend> child, which the fieldset leaves enabled.
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

    /// What a button's activation does to its form (HTML 4.10.6,
    /// 4.10.5.1.18-19): a submit button submits its form owner and a reset
    /// button resets it. `form` is empty when the element is neither, is
    /// actually disabled or has no form owner. browser::activate and a frame
    /// document's activation (bindings/frames.cpp) both use this.
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

    /// The UTF-8 code point boundary on either side of a byte offset; a caret
    /// moves by these, never by bytes. Clamped: past the end is the end. The
    /// browser's caret and password masking use the same two.
    [[nodiscard]] static std::size_t previous_code_point(std::string_view text, std::size_t at);
    [[nodiscard]] static std::size_t next_code_point(std::string_view text, std::size_t at);

private:
    void erase_selection(control_state & control);

    flat_map<std::uint64_t, control_state> states_;
};

// --- a request an embedder may answer --------------------------------------

// This engine opens no socket for a page loaded from disk: every byte a frame,
// a fetch() or an XHR gets comes out of the asset registry, which answers a GET
// for a file and nothing else. A form POSTed to a script, or a GET whose answer
// depends on its query, needs something else to answer it: a request handler
// (browser_options::on_request). Given the method, the absolute URL, the
// request's Content-Type and its body, it returns a response, or nullopt to
// leave the request to the registry.
//
// The only caller in this tree that installs one is ctdrive, under a document
// root, with emulations of the web-platform-tests' Python handlers
// (tools/ctdrive/wpt_handlers.cpp): the server half of a form submission test.
// An application has no reason to set it.

/// A request passed to a request_handler.
struct resource_request {
    std::string method = "GET";
    std::string url;          // absolute, fragment removed
    std::string content_type; // the request's Content-Type; empty for none
    std::string body;         // the entity body's bytes
};

/// A request_handler's response.
struct resource_reply {
    int status = 200;
    std::string content_type; // the response's Content-Type, parameters and all
    std::string body;         // its bytes
};

/// Answers a request, or returns nullopt to leave it to the asset registry.
using request_handler = std::function<std::optional<resource_reply>(const resource_request &)>;

// --- form submission, HTML 4.10.21.3-9 ---------------------------------------

/// One entry of a form's entry list as the three encodings read it: a name
/// and a string, or a File, whose name, type and bytes multipart/form-data
/// sends and the other two encodings reduce to the name. Text is UTF-8 of
/// scalar values.
struct form_entry {
    std::string name;
    std::string value; // a string entry's text; a File's bytes
    bool file = false;
    std::string filename;
    std::string type; // a File's type; "" is application/octet-stream
};

/// The three submission bodies over an entry list, each encoded in
/// `encoding` (the one "picking an encoding for the form" chose) with the
/// Encoding Standard's "html" error mode, so a character the encoding lacks
/// becomes `&#N;`: application/x-www-form-urlencoded (URL 5.2, also a GET's
/// query), text/plain (HTML 4.10.21.9) and multipart/form-data (HTML
/// 4.10.21.8). Names and string values have their line breaks normalised to
/// CR LF first (HTML 4.10.21.6 and .8). A legacy multi-byte encoding has no
/// encoder here and is sent as UTF-8.
[[nodiscard]] std::string urlencoded_form_body(const std::vector<form_entry> & entries,
                                               std::string_view encoding);
[[nodiscard]] std::string text_plain_form_body(const std::vector<form_entry> & entries,
                                               std::string_view encoding);
/// A multipart/form-data body and its boundary: `----ctbrowser` and 24
/// random alphanumerics from std::random_device (not the page's seeded
/// Math.random, which a page can predict), drawn again if any name, value or
/// file contains it. A fixed public boundary would let a value end its part
/// early and forge fields after it.
struct multipart_body {
    std::string boundary;
    std::string body;
};
[[nodiscard]] multipart_body multipart_form_body(const std::vector<form_entry> & entries,
                                                 std::string_view encoding);

class dom_bindings;

/// A submission's navigation, as build_submission makes it: the target it
/// names, the URL as the page wrote it (the name the asset registry is asked
/// for) and the request (method, absolute URL, Content-Type and body).
struct form_navigation {
    std::string target;
    std::string src;
    resource_request request;
};

/// A form_navigation once a navigable is chosen (bindings/frames.cpp): the
/// form's document and the form (a later planned navigation for the same form
/// replaces this one; both are empty for a script's navigation such as a
/// Location setter or window.open), and the <iframe> it navigates, given by
/// the bindings of the document it is in.
struct planned_navigation {
    dom_bindings * form_document = nullptr;
    node_id form;
    dom_bindings * navigable = nullptr;
    node_id element;
    form_navigation navigation;
};

} // namespace ctbrowser::shell
