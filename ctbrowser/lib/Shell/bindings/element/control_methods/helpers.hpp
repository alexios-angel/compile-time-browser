#pragma once

#include "../internal.hpp"
#include <ctbrowser/shell/page/input_types.hpp>

namespace ctbrowser::shell::detail {

// The hidden slots on a wrapper the forms keep their non-attribute state in:
// an option's selectedness and its dirtiness, a control's custom validity
// message, a FormData's entries.
inline constexpr std::string_view selected_slot = "__selected";
inline constexpr std::string_view dirty_slot = "__selectedDirty";
inline constexpr std::string_view custom_slot = "__customValidity";
inline constexpr std::string_view entries_slot = "__entries";

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

std::size_t units_before(std::string_view text, std::size_t bytes);
std::size_t units_length_of(std::string_view text);
std::size_t bytes_before(std::string_view text, std::size_t units);

// Shared callback state is only the original installation's bindings pointer.
// Capturing this value never retains an installer stack frame or helper reference.
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
    bool is_listed(const read_txn & txn, dom_bindings * b, node_id id) const;
    bool is_submittable(const read_txn & txn, node_id id) const;
    bool is_labelable(const read_txn & txn, dom_bindings * b, node_id id) const;
    std::vector<node_id> elements_of_form(dom_bindings * b, node_id form) const;
    bool is_disabled(const read_txn & txn, dom_bindings * b, node_id id) const;
    static void plain(script::object_object &);
    std::vector<node_id> options_of(const read_txn & txn, dom_bindings * b, node_id select) const;
    std::string option_value(const read_txn & txn, dom_bindings * b, node_id option) const;
    bool option_selected(context & c, const read_txn & txn, dom_bindings * b, node_id option) const;
    bool is_multiple(const read_txn & txn, dom_bindings * b, node_id select) const;
    long long display_size(const read_txn & txn, dom_bindings * b, node_id select) const;
    std::vector<bool> selected_flags(context & c, const read_txn & txn, dom_bindings * b,
                                     node_id select, std::vector<node_id> & options) const;
    std::optional<double> to_number(const read_txn & txn, dom_bindings * b, node_id id,
                                    std::string_view text) const;
    std::optional<double> step_of(const read_txn & txn, dom_bindings * b, node_id id) const;
    double step_base_of(const read_txn & txn, dom_bindings * b, node_id id) const;
    std::optional<double> bound_of(const read_txn & txn, dom_bindings * b, node_id id,
                                   const char * which) const;
    bool is_step_aligned(double number, double base, double step) const;
    bool will_validate(const read_txn & txn, dom_bindings * b, node_id id) const;
    value compile_pattern(context & c, const std::string & pattern) const;
    bool matches_pattern(context & c, const std::string & pattern, const std::string & text) const;
    unsigned validity_flags(context & c, dom_bindings * b, node_id id) const;
    bool check_one(context & c, dom_bindings * b, node_id id) const;
    bool validate_form(context & c, dom_bindings * b, node_id form) const;

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
