#include "helpers.hpp"

namespace ctbrowser::shell::detail {
using namespace input_types;

std::vector<node_id> control_helpers::options_of(const read_txn & txn, dom_bindings * b,
                                                 node_id select) const {
    return form_store::list_of_options(txn, *b->atoms_, select);
}

// An option's "nearest ancestor select" (HTML 4.10.10): up from its parent,
// and none through a datalist, an hr or another option, nor through a second
// optgroup - the nestings the list of options leaves out, so an option outside
// the list has no select to be indexed in or to take a form from.
node_id control_helpers::select_of(const read_txn & txn, node_id option) const {
    bool in_optgroup = false;
    for (node_id up = txn.parent(option); up; up = txn.parent(up)) {
        if (is(txn, up, "datalist") || is(txn, up, "hr") || is(txn, up, "option")) {
            return node_id{};
        }
        if (is(txn, up, "optgroup")) {
            if (in_optgroup) { return node_id{}; }
            in_optgroup = true;
        }
        if (is(txn, up, "select")) { return up; }
    }
    return node_id{};
}

std::string control_helpers::option_value(const read_txn & txn, dom_bindings * b,
                                          node_id option) const {
    return form_store::option_value(txn, *b->atoms_, option);
}

bool control_helpers::option_selected(context & c, const read_txn & txn, dom_bindings * b,
                                      node_id option) const {
    const auto helpers = *this;
    const value held = helpers.slot_of(c, b, option, selected_slot);
    if (held.is_boolean()) { return context::truthy(held); }
    return txn.has_attribute(option, b->atoms_->intern("selected"));
}

bool control_helpers::is_multiple(const read_txn & txn, dom_bindings * b, node_id select) const {
    return txn.has_attribute(select, b->atoms_->intern("multiple"));
}

long long control_helpers::display_size(const read_txn & txn, dom_bindings * b,
                                        node_id select) const {
    const auto helpers = *this;
    const std::string_view text = txn.attribute_value(select, b->atoms_->intern("size"));
    long long parsed = 0;
    if (parse_html_integer(text, parsed) && parsed > 0) { return parsed; }
    return helpers.is_multiple(txn, b, select) ? 4LL : 1LL;
}

std::vector<bool> control_helpers::selected_flags(context & c, const read_txn & txn,
                                                  dom_bindings * b, node_id select,
                                                  std::vector<node_id> & options) const {
    const auto helpers = *this;
    options = helpers.options_of(txn, b, select);
    // THE LIST OF OPTIONS AS IT WAS the last time it was asked about: an
    // option inserted or removed since is a change to the list, and HTML
    // 4.10.7 runs the selectedness setting algorithm on one - which a model
    // read lazily can only see here, by comparing.
    std::string signature = ",";
    for (const node_id option : options) { signature += std::to_string(option.key()) + ","; }
    const value seen = helpers.slot_of(c, b, select, "__optionList");
    const bool changed = seen.is_string() && c.to_string(seen) != signature;
    if (!seen.is_string() || changed) {
        helpers.set_slot(c, b, select, "__optionList", c.string(signature));
    }
    std::vector<bool> flags;
    flags.reserve(options.size());
    bool any_slot = false;
    for (const node_id option : options) {
        flags.push_back(helpers.option_selected(c, txn, b, option));
        if (helpers.slot_of(c, b, option, selected_slot).is_boolean()) { any_slot = true; }
    }
    if (helpers.is_multiple(txn, b, select)) { return flags; }
    // "Ask for a reset": the last of several selected wins - a single select
    // never has two, however it got them (a `multiple` removed afterwards
    // included) - and a select nothing has touched, whose insertions the
    // parser made, selects its first enabled option at display size 1. So
    // does one whose list CHANGED with nothing selected - the selectedness
    // setting algorithm runs on every insertion and removal - so removing
    // the selected option selects the first that is left. The options'
    // own selectedness and dirtiness are left as they are: an option put
    // back selected is selected again by its insertion (form_writes.cpp).
    std::size_t count = 0;
    std::size_t last = options.size();
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (flags[i]) {
            ++count;
            last = i;
        }
    }
    // WHAT THE ALGORITHM CHOSE IS THE OPTIONS' SELECTEDNESS NOW, written to
    // their slots rather than only answered: it is state the options keep, so
    // one moved out of the select afterwards is still selected
    // (moveBefore/select-option-optgroup.html) and an option inserted
    // selected is compared against it (form_writes.cpp).
    if (count > 1) {
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (flags[i] && i != last) {
                helpers.set_slot(c, b, options[i], selected_slot, value::boolean(false));
            }
            flags[i] = i == last;
        }
    } else if (count == 0 && (!any_slot || changed) && helpers.display_size(txn, b, select) == 1) {
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (!helpers.is_disabled(txn, b, options[i])) {
                flags[i] = true;
                helpers.set_slot(c, b, options[i], selected_slot, value::boolean(true));
                break;
            }
        }
    }
    const control_state * held = b->forms_->find(select);
    if (held == nullptr) { return flags; }
    const value synced = helpers.slot_of(c, b, select, "__syncedValue");
    if (changed && (!synced.is_string() || held->value == c.to_string(synced))) {
        // The store FOLLOWS the model when the list moved and nothing wrote
        // the store since it was last synced: its value is stale then, not a
        // choice the chrome or the `value` setter made.
        std::string first;
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (flags[i]) {
                first = helpers.option_value(txn, b, options[i]);
                break;
            }
        }
        b->forms_->state_of(txn, *b->atoms_, select).value = first;
        helpers.set_slot(c, b, select, "__syncedValue", c.string(first));
        return flags;
    }
    const std::string reference = synced.is_string()
                                      ? c.to_string(synced)
                                      : form_store::selected_option_value(txn, *b->atoms_, select);
    if (held->value == reference) {
        // The baseline a later write to the store is told apart from.
        if (!synced.is_string()) {
            helpers.set_slot(c, b, select, "__syncedValue", c.string(held->value));
        }
        return flags;
    }
    helpers.set_slot(c, b, select, "__syncedValue", c.string(held->value));
    std::size_t match = options.size();
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (helpers.option_value(txn, b, options[i]) == held->value) {
            match = i;
            break;
        }
    }
    for (std::size_t j = 0; j < options.size(); ++j) {
        flags[j] = j == match;
        helpers.set_slot(c, b, options[j], selected_slot, value::boolean(j == match));
    }
    return flags;
}

// Write the selected option's value to the store, so the paint and the
// submission agree with the model.
void control_helpers::sync_store(context & c, const read_txn & txn, dom_bindings * b,
                                 node_id select, const std::vector<node_id> & options,
                                 const std::vector<bool> & flags) const {
    const auto helpers = *this;
    std::string first;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (flags[i]) {
            first = helpers.option_value(txn, b, options[i]);
            break;
        }
    }
    control_state & held = b->forms_->state_of(txn, *b->atoms_, select);
    held.value = first;
    helpers.set_slot(c, b, select, "__syncedValue", c.string(first));
    b->wrote_to_control_ = true;
}

// "Ask for a reset", HTML 4.10.7: a single select with several selected
// keeps the last; a display-size-1 single select with none selects the
// first option that is not disabled.
void control_helpers::ask_for_reset(context & c, dom_bindings * b, node_id select) const {
    const auto helpers = *this;
    const auto txn = b->doc_->read();
    if (!txn.contains(select)) { return; }
    std::vector<node_id> options;
    std::vector<bool> flags = helpers.selected_flags(c, txn, b, select, options);
    if (helpers.is_multiple(txn, b, select)) {
        helpers.sync_store(c, txn, b, select, options, flags);
        return;
    }
    std::size_t count = 0;
    std::size_t last = options.size();
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (flags[i]) {
            ++count;
            last = i;
        }
    }
    if (count > 1) {
        for (std::size_t i = 0; i < options.size(); ++i) {
            flags[i] = i == last;
            helpers.set_slot(c, b, options[i], selected_slot, value::boolean(flags[i]));
        }
    } else if (count == 0 && helpers.display_size(txn, b, select) == 1) {
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (!helpers.is_disabled(txn, b, options[i])) {
                flags[i] = true;
                helpers.set_slot(c, b, options[i], selected_slot, value::boolean(true));
                break;
            }
        }
    }
    helpers.sync_store(c, txn, b, select, options, flags);
}

} // namespace ctbrowser::shell::detail
