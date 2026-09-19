#include "helpers.hpp"

namespace ctbrowser::shell {
using namespace detail;
using namespace input_types;

void dom_bindings::install_control_numbers_validation(context & cx) {
    const control_helpers helpers{this};
    // --- the input types' numbers, HTML 4.10.5.1 ----------------------------------
    //
    // number and range parse a floating-point number; date, month, week, time
    // and datetime-local parse their strings to a number of milliseconds (or
    // months, for month). `to_number` answers nullopt where the value is not
    // one, and `to_text` writes a number back in the type's own format.

    const auto text_of_number = [helpers](const read_txn & txn, dom_bindings * b, node_id id,
                                          double number) -> std::string {
        return type_number_to_text(helpers.input_type(txn, b, id), number);
    };
    // The allowed value step in the type's units, or nullopt for `any`; the
    // step base; and the min/max, each when parseable.

    // valueAsNumber / valueAsDate, HTML 4.10.5.3.
    const auto number_types = [](std::string_view type) {
        return type == "number" || type == "range" || type == "date" || type == "month" ||
               type == "week" || type == "time" || type == "datetime-local";
    };
    const auto date_types = [](std::string_view type) {
        return type == "date" || type == "month" || type == "week" || type == "time";
    };
    // Write a control's value into the store, as the `value` setter does.
    const auto store_value = [](const read_txn & txn, dom_bindings * b, node_id id,
                                std::string text) {
        control_state & held = b->forms_->state_of(txn, *b->atoms_, id);
        held.value = std::move(text);
        held.caret = held.value.size();
        held.selection = held.caret;
        held.value_edited = true;
        b->wrote_to_control_ = true;
    };
    helpers.accessor(
        cx, "HTMLInputElement", "valueAsNumber",
        [helpers, number_types](context & c, std::span<value>) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::number(std::nan("")); }
            const auto txn = b->doc_->read();
            const std::string type = helpers.input_type(txn, b, id);
            if (!number_types(type)) { return value::number(std::nan("")); }
            const std::optional<double> held =
                helpers.to_number(txn, b, id, helpers.value_of(txn, b, id));
            (void)c;
            return value::number(held.value_or(std::nan("")));
        },
        [this, helpers, number_types, text_of_number, store_value](context & c,
                                                                   std::span<value> a) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::undefined(); }
            const double number = context::to_number(arg(a, 0));
            if (std::isinf(number)) {
                c.throw_error("TypeError", "valueAsNumber: the value is not finite");
                return value::undefined();
            }
            const auto txn = b->doc_->read();
            if (!number_types(helpers.input_type(txn, b, id))) {
                throw_dom_exception(c, "InvalidStateError",
                                    "valueAsNumber does not apply to this input type");
                return value::undefined();
            }
            store_value(txn, b, id,
                        std::isnan(number) ? std::string{} : text_of_number(txn, b, id, number));
            b->mutated();
            return value::undefined();
        });
    helpers.accessor(
        cx, "HTMLInputElement", "valueAsDate",
        [helpers, date_types](context & c, std::span<value>) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::null(); }
            std::optional<double> ms;
            {
                const auto txn = b->doc_->read();
                const std::string type = helpers.input_type(txn, b, id);
                if (!date_types(type)) { return value::null(); }
                ms = helpers.to_number(txn, b, id, helpers.value_of(txn, b, id));
                if (ms && type == "month") { ms = month_index_to_ms(*ms); }
            }
            if (!ms) { return value::null(); }
            const value when = value::number(*ms);
            return c.construct(c.global("Date"), std::span<const value>{&when, 1});
        },
        [this, helpers, date_types, text_of_number, store_value](context & c, std::span<value> a) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::undefined(); }
            const value given = arg(a, 0);
            const auto txn = b->doc_->read();
            const std::string type = helpers.input_type(txn, b, id);
            if (!date_types(type)) {
                throw_dom_exception(c, "InvalidStateError",
                                    "valueAsDate does not apply to this input type");
                return value::undefined();
            }
            double ms = std::nan("");
            if (!given.is_null()) {
                const value get_time = given.is_object_like() ? c.lookup_property(given, "getTime")
                                                              : value::undefined();
                if (!get_time.is_callable()) {
                    c.throw_error("TypeError", "valueAsDate: the value is not a Date");
                    return value::undefined();
                }
                ms = context::to_number(c.call(get_time, {}, given));
            }
            if (!std::isnan(ms) && type == "month") { ms = ms_to_month_index(ms); }
            store_value(txn, b, id,
                        std::isnan(ms) ? std::string{} : text_of_number(txn, b, id, ms));
            b->mutated();
            return value::undefined();
        });
    // `value`, `checked` and `files` ON THE PROTOTYPE, for the inputs the
    // wrapper installs no own accessor on - a hidden input has none, so its
    // value read `undefined` - and `indeterminate`, a slot on the wrapper.
    // Where the wrapper's own accessor exists it shadows these, as it must.
    helpers.accessor(
        cx, "HTMLInputElement", "value",
        [helpers](context & c, std::span<value>) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return c.string(""); }
            const auto txn = b->doc_->read();
            return c.string(helpers.value_of(txn, b, id));
        },
        [helpers, store_value](context & c, std::span<value> a) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::undefined(); }
            {
                const auto txn = b->doc_->read();
                const value given = arg(a, 0);
                store_value(txn, b, id, given.is_null() ? std::string{} : c.to_string(given));
            }
            b->mutated();
            return value::undefined();
        });
    helpers.accessor(
        cx, "HTMLInputElement", "checked",
        [helpers](context & c, std::span<value>) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::boolean(false); }
            const auto txn = b->doc_->read();
            return value::boolean(b->forms_->state_of(txn, *b->atoms_, id).checked);
        },
        [helpers](context & c, std::span<value> a) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::undefined(); }
            {
                const auto txn = b->doc_->read();
                b->forms_->state_of(txn, *b->atoms_, id).checked = context::truthy(arg(a, 0));
            }
            b->wrote_to_control_ = true;
            b->mutated();
            return value::undefined();
        });
    helpers.accessor(cx, "HTMLInputElement", "files",
                     [](context &, std::span<value>) { return value::null(); });
    helpers.accessor(
        cx, "HTMLInputElement", "indeterminate",
        [helpers](context & c, std::span<value>) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::boolean(false); }
            return value::boolean(context::truthy(helpers.slot_of(c, b, id, "__indeterminate")));
        },
        [helpers](context & c, std::span<value> a) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (id) {
                helpers.set_slot(c, b, id, "__indeterminate",
                                 value::boolean(context::truthy(arg(a, 0))));
            }
            return value::undefined();
        });

    // stepUp / stepDown, HTML 4.10.5.3.
    const auto step_by = [this, helpers, number_types, text_of_number,
                          store_value](context & c, std::span<value> a, bool up) {
        const auto where = helpers.at(c);
        dom_bindings * b = where.first;
        const node_id id = where.second;
        if (!id) { return; }
        const auto txn = b->doc_->read();
        const std::string type = helpers.input_type(txn, b, id);
        if (!number_types(type)) {
            throw_dom_exception(c, "InvalidStateError", "stepUp/stepDown does not apply here");
            return;
        }
        const std::optional<double> step = helpers.step_of(txn, b, id);
        if (!step) {
            throw_dom_exception(c, "InvalidStateError", "the step is `any`");
            return;
        }
        const double base = helpers.step_base_of(txn, b, id);
        const std::optional<double> min = helpers.bound_of(txn, b, id, "min");
        const std::optional<double> max = helpers.bound_of(txn, b, id, "max");
        if (min && max && *min > *max) { return; }
        double number = helpers.to_number(txn, b, id, helpers.value_of(txn, b, id)).value_or(0);
        const double before = number;
        double n = a.empty() ? 1 : context::to_number(a[0]);
        if (std::isnan(n)) { n = 0; }
        if (!helpers.is_step_aligned(number, base, *step)) {
            const double quotient = (number - base) / *step;
            number = base + (up ? std::ceil(quotient) : std::floor(quotient)) * *step;
        } else {
            number += (up ? 1 : -1) * n * *step;
        }
        if (min && number < *min) { number = base + std::ceil((*min - base) / *step) * *step; }
        if (max && number > *max) { number = base + std::floor((*max - base) / *step) * *step; }
        if ((up && number < before) || (!up && number > before)) { return; }
        if (min && number < *min) { return; }
        if (max && number > *max) { return; }
        store_value(txn, b, id, text_of_number(txn, b, id, number));
        b->mutated();
    };
    helpers.operation(cx, "HTMLInputElement", "stepUp", 0,
                      [step_by](context & c, std::span<value> a) {
                          step_by(c, a, true);
                          return value::undefined();
                      });
    helpers.operation(cx, "HTMLInputElement", "stepDown", 0,
                      [step_by](context & c, std::span<value> a) {
                          step_by(c, a, false);
                          return value::undefined();
                      });

    // --- constraint validation, HTML 4.10.20 ------------------------------------------
    //
    // `willValidate` is "candidate for constraint validation": a submittable
    // element that is not barred - disabled, readonly (an input or textarea),
    // an input of type hidden/reset/button, inside a datalist - and not an
    // output, fieldset or object, which are never candidates. The validity
    // flags are computed on every read from the store's value and the
    // attributes; the custom message is a slot on the wrapper.
    //
    // ponytail: tooLong and tooShort are always false. HTML raises them only
    // for a value the USER edited, and the store cannot tell a user's edit
    // from a script's `value =` (both set value_edited); the manual tests
    // are the ones that would measure them.

    // The pattern attribute compiled as the specification says - anchored,
    // with the `v` flag - through a JS function so a bad pattern is caught
    // there and means "no constraint" rather than a throw.

    // Every flag of a control, as a bit set.

    // A ValidityState object: the flags frozen into a hidden slot, read by
    // the accessors on the prototype.
    if (!secondary_) {
        if (auto * on = helpers.proto("ValidityState")) {
            const auto flag_getter = [&](const char * name, unsigned bit) {
                on->define_accessor(
                    name,
                    native(cx, name,
                           [bit](context & c, std::span<value>) {
                               const value held = c.lookup_property(c.current_this(), "__flags");
                               const unsigned flags = context::to_uint32(held);
                               return value::boolean(bit == 0 ? flags == 0 : (flags & bit) != 0);
                           }),
                    value::undefined());
            };
            flag_getter("valueMissing", value_missing);
            flag_getter("typeMismatch", type_mismatch);
            flag_getter("patternMismatch", pattern_mismatch);
            flag_getter("tooLong", too_long);
            flag_getter("tooShort", too_short);
            flag_getter("rangeUnderflow", range_underflow);
            flag_getter("rangeOverflow", range_overflow);
            flag_getter("stepMismatch", step_mismatch);
            flag_getter("badInput", bad_input);
            flag_getter("customError", custom_error);
            flag_getter("valid", 0);
        }
    }
    const auto validity_object = [this](context & c, unsigned flags) {
        auto * made = c.allocate<script::object_object>();
        if (const value proto_value = interface_prototype("ValidityState");
            proto_value.is_object()) {
            made->prototype = proto_value;
        }
        made->define("__flags", value::number(static_cast<double>(flags)), script::attr_none);
        return value::object(made);
    };
    // The message for the first flag set, as a browser words it.
    const auto validation_message = [helpers](context & c, dom_bindings * b, node_id id,
                                              unsigned flags) -> std::string {
        if (flags == 0) { return {}; }
        if ((flags & custom_error) != 0) {
            return c.to_string(helpers.slot_of(c, b, id, custom_slot));
        }
        if ((flags & value_missing) != 0) { return "Please fill out this field."; }
        if ((flags & type_mismatch) != 0) { return "Please enter a valid value."; }
        if ((flags & pattern_mismatch) != 0) { return "Please match the requested format."; }
        if ((flags & range_underflow) != 0) {
            return "Value must be greater than or equal to the minimum.";
        }
        if ((flags & range_overflow) != 0) {
            return "Value must be less than or equal to the maximum.";
        }
        if ((flags & step_mismatch) != 0) { return "Please enter a valid value."; }
        if ((flags & bad_input) != 0) { return "Please enter a valid value."; }
        return "Please lengthen this text.";
    };
    // "Check validity" / "report validity" for one element: fires `invalid`
    // (cancelable, not bubbling) when a candidate is invalid.

    for (const char * which :
         {"HTMLButtonElement", "HTMLFieldSetElement", "HTMLInputElement", "HTMLObjectElement",
          "HTMLOutputElement", "HTMLSelectElement", "HTMLTextAreaElement"}) {
        helpers.accessor(cx, which, "willValidate", [helpers](context & c, std::span<value>) {
            const auto where = helpers.at(c);
            dom_bindings * b = where.first;
            const node_id id = where.second;
            if (!id) { return value::boolean(false); }
            const auto txn = b->doc_->read();
            return value::boolean(helpers.will_validate(txn, b, id));
        });
        helpers.accessor(cx, which, "validity",
                         [helpers, validity_object](context & c, std::span<value>) {
                             const auto where = helpers.at(c);
                             dom_bindings * b = where.first;
                             const node_id id = where.second;
                             return validity_object(c, id ? helpers.validity_flags(c, b, id) : 0);
                         });
        helpers.accessor(cx, which, "validationMessage",
                         [helpers, validation_message](context & c, std::span<value>) {
                             const auto where = helpers.at(c);
                             dom_bindings * b = where.first;
                             const node_id id = where.second;
                             if (!id) { return c.string(""); }
                             {
                                 const auto txn = b->doc_->read();
                                 if (!helpers.will_validate(txn, b, id)) { return c.string(""); }
                             }
                             return c.string(
                                 validation_message(c, b, id, helpers.validity_flags(c, b, id)));
                         });
        helpers.operation(
            cx, which, "setCustomValidity", 1, [this, helpers](context & c, std::span<value> a) {
                if (const node_id id = receiver(c)) {
                    helpers.set_slot(c, this, id, custom_slot, c.string(arg_string(c, a, 0)));
                }
                return value::undefined();
            });
        helpers.operation(cx, which, "checkValidity", 0,
                          [this, helpers](context & c, std::span<value>) {
                              const node_id id = receiver(c);
                              return value::boolean(!id || helpers.check_one(c, this, id));
                          });
        helpers.operation(cx, which, "reportValidity", 0,
                          [this, helpers](context & c, std::span<value>) {
                              const node_id id = receiver(c);
                              return value::boolean(!id || helpers.check_one(c, this, id));
                          });
    }
    // The form's: "statically validate the constraints" over its submittable
    // elements, every invalid one told.

    helpers.operation(cx, "HTMLFormElement", "checkValidity", 0,
                      [this, helpers](context & c, std::span<value>) {
                          const node_id id = receiver(c);
                          return value::boolean(!id || helpers.validate_form(c, this, id));
                      });
    helpers.operation(cx, "HTMLFormElement", "reportValidity", 0,
                      [this, helpers](context & c, std::span<value>) {
                          const node_id id = receiver(c);
                          return value::boolean(!id || helpers.validate_form(c, this, id));
                      });
}

} // namespace ctbrowser::shell
