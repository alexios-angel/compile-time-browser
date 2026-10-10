//===- pickers.cpp - showPicker() on input and select -----------*- C++ -*-===//
//
// `showPicker()` on <input> (HTML 4.10.5.4) and <select> (HTML 4.10.7): the
// checks, then consuming the window's user activation (window/activation.cpp).
// Nothing in the engine draws a picker, so "show the picker, if applicable"
// is the one step left out.
//
//===----------------------------------------------------------------------===//

#include "helpers.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

/// The input types `readonly` applies to (HTML 4.10.5.3.3); on the rest it is
/// ignored.
[[nodiscard]] bool readonly_applies(std::string_view type) {
    for (const std::string_view one : {"text", "search", "url", "tel", "email", "password", "date",
                                       "month", "week", "time", "datetime-local", "number"}) {
        if (type == one) { return true; }
    }
    return false;
}

} // namespace

void dom_bindings::install_control_pickers(context & cx) {
    const control_helpers helpers{this};
    for (const char * which : {"HTMLInputElement", "HTMLSelectElement"}) {
        const bool select = std::string_view{which} == "HTMLSelectElement";
        helpers.operation(
            cx, which, "showPicker", 0, [this, helpers, select](context & c, std::span<value>) {
                const auto [b, id] = helpers.at(c);
                if (!id) { return value::undefined(); }
                bool unrendered = false;
                {
                    const auto txn = b->doc_->read();
                    // Step 1: not mutable is an InvalidStateError.
                    const bool read_only = !select &&
                                           readonly_applies(helpers.input_type(txn, b, id)) &&
                                           helpers.has_attribute(txn, b, id, "readonly");
                    if (helpers.is_disabled(txn, b, id) || read_only) {
                        throw_dom_exception(c, "InvalidStateError",
                                            "showPicker: the control is not mutable");
                        return value::undefined();
                    }
                    unrendered = select && element_unrendered(txn, *b->atoms_, b->styles_, id);
                }
                // A <select> that is not being rendered has no picker to show.
                if (unrendered) {
                    throw_dom_exception(c, "NotSupportedError",
                                        "showPicker: the select is not rendered");
                    return value::undefined();
                }
                if (!b->has_transient_activation()) {
                    throw_dom_exception(c, "NotAllowedError", "showPicker requires a user gesture");
                    return value::undefined();
                }
                b->consume_activation();
                return value::undefined();
            });
    }
}

} // namespace ctbrowser::shell
