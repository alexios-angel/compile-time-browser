//===- helpers.hpp - Helpers shared by form control bindings ----*- C++ -*-===//
//
// control_helpers: the shared machinery of the form, select, validation and
// DataTransfer bindings in this directory (receiver lookup, hidden slots,
// form owners, the select model and constraint validation), plus the
// hidden-slot keys and validity flags they share.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../internal.hpp"
#include <ctbrowser/script/utf16.hpp>
#include <ctbrowser/shell/page/input_types.hpp>

namespace ctbrowser::shell::detail {

// The hidden slots on a wrapper the forms keep their non-attribute state in:
// an option's selectedness and its dirtiness, a control's custom validity
// message, a FormData's entries. They are private keys
// (script/value/base.hpp), so a page can neither read nor forge them.
inline constexpr std::string_view selected_slot = CTBROWSER_PRIVATE_KEY("OptionSelectedness");
inline constexpr std::string_view dirty_slot = CTBROWSER_PRIVATE_KEY("OptionDirtiness");
inline constexpr std::string_view custom_slot = CTBROWSER_PRIVATE_KEY("CustomValidity");
inline constexpr std::string_view entries_slot = CTBROWSER_PRIVATE_KEY("FormDataEntries");

/// The ValidityState flags as a bit set; validity_flags()
/// returns a combination of them.
enum flag : unsigned {
    value_missing = 1u << 0,
    type_mismatch = 1u << 1,
    pattern_mismatch = 1u << 2,
    too_long = 1u << 3,
    too_short = 1u << 4,
    range_underflow = 1u << 5,
    range_overflow = 1u << 6,
    step_mismatch = 1u << 7,
    bad_input = 1u << 8,
    custom_error = 1u << 9,
};

/// Makes a FileList over \p files (defined in data_transfer.cpp).
[[nodiscard]] value make_file_list(context & c, const std::vector<value> & files);
/// Whether \p v is a FileList made by make_file_list.
[[nodiscard]] bool is_file_list(value v);
/// The Files a FileList holds, in order; empty for anything else.
[[nodiscard]] std::vector<value> files_in(value list);

/// The helpers the form control bindings share. Its only state is the
/// installing bindings' pointer, so a callback can capture it by value without
/// keeping an installer stack frame or a helper reference alive.
struct control_helpers {
    dom_bindings * bindings;

    script::object_object * proto(const char * which) const;
    void operation(context & cx, const char * which, const char * name, unsigned length,
                   script::native_fn fn) const;
    void getter(context & cx, const char * which, const char * name, script::native_fn get,
                script::native_fn set = nullptr) const;
    bool is(const read_txn & txn, node_id id, std::string_view name) const;
    node_id first_child(node_id parent, std::string_view name) const;
    std::pair<dom_bindings *, node_id> at(context & c) const;
    void accessor(context & cx, const char * which, const char * name, script::native_fn get,
                  script::native_fn set = nullptr) const;
    value slot_of(context & c, dom_bindings * b, node_id id, std::string_view key) const;
    void set_slot(context & c, dom_bindings * b, node_id id, std::string_view key, value v) const;
    void erase_slot(context & c, dom_bindings * b, node_id id, std::string_view key) const;
    std::string attribute_of(const read_txn & txn, dom_bindings * b, node_id id,
                             std::string_view name) const;
    bool has_attribute(const read_txn & txn, dom_bindings * b, node_id id,
                       std::string_view name) const;
    std::string input_type(const read_txn & txn, dom_bindings * b, node_id id) const;
    std::string value_of(const read_txn & txn, dom_bindings * b, node_id id) const;
    node_id tree_top(const read_txn & txn, dom_bindings * b, node_id id) const;
    node_id form_owner(const read_txn & txn, dom_bindings * b, node_id id) const;
    bool is_face(dom_bindings * b, node_id id) const;
    bool face_invalid(dom_bindings * b, node_id id) const;
    bool is_listed(const read_txn & txn, dom_bindings * b, node_id id) const;
    bool is_submittable(const read_txn & txn, node_id id) const;
    bool is_labelable(const read_txn & txn, dom_bindings * b, node_id id) const;
    node_id labeled_control(const read_txn & txn, dom_bindings * b, node_id label) const;
    std::vector<node_id> elements_of_form(dom_bindings * b, node_id form) const;
    /// The submittable elements \p form owns, in tree order. Unlike
    /// elements_of_form (the listed elements), this includes image buttons.
    std::vector<node_id> submittable_of_form(dom_bindings * b, node_id form) const;
    bool is_disabled(const read_txn & txn, dom_bindings * b, node_id id) const;
    bool in_radio_group(const read_txn & txn, dom_bindings * b, node_id a, node_id other) const;
    void uncheck_radio_group(const read_txn & txn, dom_bindings * b, node_id id) const;
    void children_replaced(dom_bindings * b, node_id id) const;
    static void plain(script::object_object &);
    /// The select's "list of options" (HTML 4.10.7), the same walk the form
    /// store uses, so the value the chrome paints and the model agree.
    std::vector<node_id> options_of(const read_txn & txn, dom_bindings * b, node_id select) const;
    /// An option's "nearest ancestor select" (HTML 4.10.10), or none.
    node_id select_of(const read_txn & txn, node_id option) const;
    std::string option_value(const read_txn & txn, dom_bindings * b, node_id option) const;
    bool option_selected(context & c, const read_txn & txn, dom_bindings * b, node_id option) const;
    bool is_multiple(const read_txn & txn, dom_bindings * b, node_id select) const;
    /// The display size: the `size` attribute, else 4 for multiple and 1
    /// otherwise.
    long long display_size(const read_txn & txn, dom_bindings * b, node_id select) const;
    /// Every option's selectedness, in list order, with \p options filled in
    /// and the store reconciled (see the definition for `__syncedValue`).
    std::vector<bool> selected_flags(context & c, const read_txn & txn, dom_bindings * b,
                                     node_id select, std::vector<node_id> & options) const;
    /// Writes the selected option's value to the form store, so the paint and
    /// the submission agree with the model.
    void sync_store(context & c, const read_txn & txn, dom_bindings * b, node_id select,
                    const std::vector<node_id> & options, const std::vector<bool> & flags) const;
    /// "Ask for a reset" (HTML 4.10.7): runs the selectedness setting
    /// algorithm on \p select, then updates the store.
    void ask_for_reset(context & c, dom_bindings * b, node_id select) const;
    std::optional<double> to_number(const read_txn & txn, dom_bindings * b, node_id id,
                                    std::string_view text) const;
    /// The allowed value step in the type's units, or nullopt for `step=any`.
    std::optional<double> step_of(const read_txn & txn, dom_bindings * b, node_id id) const;
    /// The step base: `min`, else `value`, else the type's default step base
    /// (non-zero only for week).
    double step_base_of(const read_txn & txn, dom_bindings * b, node_id id) const;
    /// The `min` or `max` attribute (\p which) as a number, when it parses.
    std::optional<double> bound_of(const read_txn & txn, dom_bindings * b, node_id id,
                                   const char * which) const;
    bool is_step_aligned(double number, double base, double step) const;
    bool will_validate(const read_txn & txn, dom_bindings * b, node_id id) const;
    bool matches_pattern(const std::string & pattern, const std::string & text) const;
    unsigned validity_flags(context & c, dom_bindings * b, node_id id) const;
    /// "Check validity" for one element: when it is a candidate for validation
    /// and invalid, fires a cancelable, non-bubbling `invalid` event and
    /// returns false.
    bool check_one(context & c, dom_bindings * b, node_id id) const;
    bool validate_form(context & c, dom_bindings * b, node_id form) const;
    /// HTMLFormElement and HTMLSelectElement as legacy platform objects
    /// (legacy_objects.cpp): the proxy wrap() hands out over \p target, or
    /// undefined for an element that is neither.
    value legacy_object(context & c, script::object_object & target, node_id id) const;
    /// An img's form owner: the form's named getter also finds images.
    node_id image_form_owner(const read_txn & txn, node_id img) const;
    void install_data_transfer(context & cx) const;

    void walk_tree(const read_txn & txn, node_id from, auto && visit) const {
        const auto walk = [&](auto && self, node_id at2) -> void {
            visit(at2);
            for (const node_id child : txn.children(at2)) { self(self, child); }
        };
        walk(walk, from);
    }

    bool fire(context & c, dom_bindings * b, node_id target, std::string_view type, bool bubbles,
              bool cancelable, auto && decorate) const {
        const value event = b->make_event_object(c, type, bubbles, cancelable);
        auto * object = static_cast<script::object_object *>(event.as_heap());
        object->set("__isTrusted", value::boolean(true));
        object->set("__initialised", value::boolean(true));
        object->set("target", b->wrap(c, target));
        object->set("srcElement", b->wrap(c, target));
        decorate(*object);
        return b->dispatch_event(type, target, event);
    }
};

} // namespace ctbrowser::shell::detail
