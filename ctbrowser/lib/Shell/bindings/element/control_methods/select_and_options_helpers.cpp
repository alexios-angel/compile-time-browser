#include "helpers.hpp"

namespace ctbrowser::shell::detail {
using namespace input_types;

std::vector<node_id> control_helpers::options_of(const read_txn & txn, dom_bindings * b,
                                                 node_id select) const {
    return form_store::list_of_options(txn, *b->atoms_, select);
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
    std::vector<bool> flags;
    flags.reserve(options.size());
    bool any_slot = false;
    for (const node_id option : options) {
        flags.push_back(helpers.option_selected(c, txn, b, option));
        if (helpers.slot_of(c, b, option, selected_slot).is_boolean()) { any_slot = true; }
    }
    if (helpers.is_multiple(txn, b, select)) { return flags; }
    // A select nothing has touched: the parser's insertions ran "ask for
    // a reset" - the last of several selected wins, and a display-size-1
    // select with none selects its first enabled option.
    if (!any_slot) {
        std::size_t count = 0;
        std::size_t last = options.size();
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (flags[i]) {
                ++count;
                last = i;
            }
        }
        if (count > 1) {
            for (std::size_t i = 0; i < options.size(); ++i) { flags[i] = i == last; }
        } else if (count == 0 && helpers.display_size(txn, b, select) == 1) {
            for (std::size_t i = 0; i < options.size(); ++i) {
                if (!helpers.is_disabled(txn, b, options[i])) {
                    flags[i] = true;
                    break;
                }
            }
        }
    }
    const control_state * held = b->forms_->find(select);
    if (held == nullptr) { return flags; }
    const value synced = helpers.slot_of(c, b, select, "__syncedValue");
    const std::string reference = synced.is_string()
                                      ? c.to_string(synced)
                                      : form_store::selected_option_value(txn, *b->atoms_, select);
    if (held->value == reference) { return flags; }
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

} // namespace ctbrowser::shell::detail
