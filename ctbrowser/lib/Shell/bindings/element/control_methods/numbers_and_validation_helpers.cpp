//===- numbers_and_validation_helpers.cpp - Constraint validation helpers -===//
//
// The control_helpers members behind numeric inputs and constraint
// validation (HTML 4.10.20): value-to-number conversion, step, min and max,
// the `pattern` matcher, the ValidityState flags and form validation.
//
//===----------------------------------------------------------------------===//

#include "helpers.hpp"

#include <ctbrowser/script/regex.hpp>

namespace ctbrowser::shell::detail {
using namespace input_types;

std::optional<double> control_helpers::to_number(const read_txn & txn, dom_bindings * b, node_id id,
                                                 std::string_view text) const {
    const auto helpers = *this;
    return type_value_to_number(helpers.input_type(txn, b, id), text);
}

std::optional<double> control_helpers::step_of(const read_txn & txn, dom_bindings * b,
                                               node_id id) const {
    const auto helpers = *this;
    const std::string type = helpers.input_type(txn, b, id);
    const double scale = step_scale_of(type);
    const std::string text = helpers.attribute_of(txn, b, id, "step");
    if (!text.empty()) {
        if (ascii_lower_copy(text) == "any") { return std::nullopt; }
        double parsed = 0;
        if (parse_float(text, parsed, true) && parsed > 0) { return parsed * scale; }
    }
    return default_step_of(type) * scale;
}

double control_helpers::step_base_of(const read_txn & txn, dom_bindings * b, node_id id) const {
    const auto helpers = *this;
    for (const char * name : {"min", "value"}) {
        if (const auto held =
                helpers.to_number(txn, b, id, helpers.attribute_of(txn, b, id, name))) {
            return *held;
        }
    }
    // The type's default step base (HTML 4.10.5.4): only the week state has
    // one, -259,200,000 ms - the Monday that starts 1970-W01, so a week value is a
    // whole number of steps from it and `1970-W01` is not a mismatch.
    return helpers.input_type(txn, b, id) == "week" ? -259'200'000.0 : 0;
}

std::optional<double> control_helpers::bound_of(const read_txn & txn, dom_bindings * b, node_id id,
                                                const char * which) const {
    const auto helpers = *this;
    return helpers.to_number(txn, b, id, helpers.attribute_of(txn, b, id, which));
}

bool control_helpers::is_step_aligned(double number, double base, double step) const {
    // "An integral multiple of the allowed value step", decided the way the
    // browsers decide it: past step * 2^53 (9007199254740992) the value's own
    // precision is coarser than the step and no remainder means anything, so
    // it is aligned; otherwise the remainder against the nearest multiple (in
    // extended precision, where 17 against 3e-15 is not lost) counts only
    // beyond step / 2^24 (16777216), the tolerance a decimal step like 0.003
    // needs.
    const long double value = std::fabs(static_cast<long double>(number) - base);
    const long double unit = step;
    if (value / 9007199254740992.0L > unit) { return true; }
    const long double remainder = std::fabs(value - unit * std::round(value / unit));
    const long double acceptable = unit / 16777216.0L;
    return !(acceptable < remainder && remainder < unit - acceptable);
}

bool control_helpers::will_validate(const read_txn & txn, dom_bindings * b, node_id id) const {
    const auto helpers = *this;
    if (!helpers.is_submittable(txn, id) || txn.local_name(id) == "object") { return false; }
    if (helpers.is_disabled(txn, b, id)) { return false; }
    const std::string_view local = txn.local_name(id);
    if (local == "input") {
        const std::string type = helpers.input_type(txn, b, id);
        if (type == "hidden" || type == "reset" || type == "button") { return false; }
        if (helpers.has_attribute(txn, b, id, "readonly")) { return false; }
    }
    if (local == "textarea" && helpers.has_attribute(txn, b, id, "readonly")) { return false; }
    // A <button> that is not a submit button (HTML 4.10.6: type=reset or
    // type=button; a missing or invalid type is the submit state).
    if (local == "button") {
        const std::string type = ascii_lower_copy(helpers.attribute_of(txn, b, id, "type"));
        if (type == "reset" || type == "button") { return false; }
    }
    for (node_id up = txn.parent(id); up; up = txn.parent(up)) {
        if (helpers.is(txn, up, "datalist")) { return false; }
    }
    return true;
}

// HTML 4.10.5.3.6's "compiled pattern regular expression": the attribute
// alone must compile with the v flag (no u fallback; a pattern such as
// `a)(b` that only parses once wrapped is as invalid as `[(]`), and then so
// must the anchored `^(?:` pattern `)$`. A pattern that fails either is no
// constraint. This uses the engine's own matcher with RegExp's own checks
// (the early-error scan, then the compile) rather than a RegExp made in
// script, because the selector engine asks `:invalid` outside any script,
// where there is no frame for a call to return into.
bool control_helpers::matches_pattern(const std::string & pattern, const std::string & text) const {
    namespace rx = script::rx;
    const auto compiled = [](const std::string & source) -> std::optional<rx::rx_prog> {
        if (rx::rx_pattern_error(source, "v")) { return std::nullopt; }
        rx::rx_prog program = rx::rx_compile(source, "v");
        if (!program.ok) { return std::nullopt; }
        return program;
    };
    if (!compiled(pattern)) { return true; }
    const std::optional<rx::rx_prog> anchored = compiled("^(?:" + pattern + ")$");
    if (!anchored) { return true; }
    rx::rx_match match;
    return rx::rx_search(*anchored, text, 0, match);
}

unsigned control_helpers::validity_flags(context & c, dom_bindings * b, node_id id) const {
    const auto helpers = *this;
    unsigned flags = 0;
    const auto txn = b->doc_->read();
    if (!txn.contains(id)) { return flags; }
    const value custom = helpers.slot_of(c, b, id, custom_slot);
    if (custom.is_string() && !c.to_string(custom).empty()) { flags |= custom_error; }
    // Being barred from constraint validation (disabled, readonly) is not a
    // reason to clear the flags: validity reports what the control suffers
    // from, and only willValidate, validationMessage and checkValidity ask
    // whether it is a candidate (validator.js clones every control disabled
    // and readonly and expects the same flags). What mutability does change
    // is "suffering from being missing" for a text-ish input and a textarea,
    // whose constraint says "and the element is mutable" (HTML 4.10.5.3.4,
    // 4.10.11); a checkbox, a radio group, a file or a select has no such
    // clause.
    const std::string_view local = txn.local_name(id);
    const bool required = helpers.has_attribute(txn, b, id, "required");
    const bool is_mutable =
        !helpers.is_disabled(txn, b, id) && !helpers.has_attribute(txn, b, id, "readonly");
    if (local == "select") {
        if (required) {
            std::vector<node_id> options;
            const std::vector<bool> selected = helpers.selected_flags(c, txn, b, id, options);
            std::size_t first = options.size();
            for (std::size_t i = 0; i < options.size(); ++i) {
                if (selected[i]) {
                    first = i;
                    break;
                }
            }
            bool missing = first == options.size();
            // The placeholder label option (HTML 4.10.7): in a
            // display-size-1 single select, the first option in the list of
            // options (one inside an optgroup included), when its value is
            // empty and its parent is the select itself.
            if (!missing && helpers.display_size(txn, b, id) == 1 &&
                !helpers.has_attribute(txn, b, id, "multiple") && first == 0 &&
                txn.parent(options[0]) == id && helpers.option_value(txn, b, options[0]).empty()) {
                missing = true;
            }
            if (missing) { flags |= value_missing; }
        }
        return flags;
    }
    if (local == "textarea") {
        if (required && is_mutable && helpers.value_of(txn, b, id).empty()) {
            flags |= value_missing;
        }
        return flags;
    }
    if (local != "input") { return flags; }
    const std::string type = helpers.input_type(txn, b, id);
    const std::string text = helpers.value_of(txn, b, id);
    const bool checked = b->forms_->state_of(txn, *b->atoms_, id).checked;
    if (type == "checkbox") {
        if (required && !checked) { flags |= value_missing; }
        return flags;
    }
    if (type == "radio") {
        // The radio button group: same name, same form owner, same tree.
        const std::string name = helpers.attribute_of(txn, b, id, "name");
        bool any_required = required;
        bool any_checked = checked;
        // A radio with no name is in no group, and the requirement is the
        // group's (HTML 4.10.5.1.16), so it is never missing.
        if (name.empty()) { return flags; }
        helpers.walk_tree(txn, helpers.tree_top(txn, b, id), [&](node_id other) {
            if (other == id || !helpers.in_radio_group(txn, b, id, other)) { return; }
            if (helpers.has_attribute(txn, b, other, "required")) { any_required = true; }
            if (b->forms_->state_of(txn, *b->atoms_, other).checked) { any_checked = true; }
        });
        if (any_required && !any_checked) { flags |= value_missing; }
        return flags;
    }
    if (type == "hidden" || type == "submit" || type == "image" || type == "reset" ||
        type == "button") {
        return flags;
    }
    if (type == "file") {
        if (required) { flags |= value_missing; }
        return flags;
    }
    if (required && is_mutable && text.empty()) { flags |= value_missing; }
    if (type == "email" && !text.empty()) {
        bool ok = true;
        if (helpers.has_attribute(txn, b, id, "multiple")) {
            std::size_t start = 0;
            while (ok) {
                const std::size_t comma = text.find(',', start);
                const std::string_view one = std::string_view{text}.substr(
                    start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!is_valid_email(trim(one, html_whitespace))) { ok = false; }
                if (comma == std::string::npos) { break; }
                start = comma + 1;
            }
        } else {
            ok = is_valid_email(text);
        }
        if (!ok) { flags |= type_mismatch; }
    }
    if (type == "url" && !text.empty() && !is_valid_url(text)) { flags |= type_mismatch; }
    const std::string pattern = helpers.attribute_of(txn, b, id, "pattern");
    if (!pattern.empty() && !text.empty() &&
        (type == "text" || type == "search" || type == "url" || type == "tel" || type == "email" ||
         type == "password")) {
        bool ok = true;
        if (type == "email" && helpers.has_attribute(txn, b, id, "multiple")) {
            std::size_t start = 0;
            while (ok) {
                const std::size_t comma = text.find(',', start);
                ok = helpers.matches_pattern(pattern, text.substr(start, comma == std::string::npos
                                                                             ? std::string::npos
                                                                             : comma - start));
                if (comma == std::string::npos) { break; }
                start = comma + 1;
            }
        } else {
            ok = helpers.matches_pattern(pattern, text);
        }
        if (!ok) { flags |= pattern_mismatch; }
    }
    if (type == "number" || type == "date" || type == "month" || type == "week" || type == "time" ||
        type == "datetime-local" || type == "range") {
        const std::optional<double> number = helpers.to_number(txn, b, id, text);
        // Bad input is text the user agent could not make a value of: for a
        // number control, typed text behind an empty value.
        const std::string & shown = b->forms_->state_of(txn, *b->atoms_, id).value;
        if ((!text.empty() && !number) || (type == "number" && text.empty() && !shown.empty())) {
            flags |= bad_input;
        }
        if (number && type != "range") {
            const auto min = helpers.bound_of(txn, b, id, "min");
            const auto max = helpers.bound_of(txn, b, id, "max");
            if (type == "time" && min && max && *max < *min) {
                // A reversed range (HTML 4.10.5.3.7: time has a periodic domain):
                // only a value in the gap between max and min is out of range,
                // and it underflows and overflows at once.
                if (*number > *max && *number < *min) { flags |= range_underflow | range_overflow; }
            } else {
                if (min && *number < *min) { flags |= range_underflow; }
                if (max && *number > *max) { flags |= range_overflow; }
            }
        }
        if (number) {
            if (const auto step = helpers.step_of(txn, b, id);
                step &&
                !helpers.is_step_aligned(*number, helpers.step_base_of(txn, b, id), *step)) {
                flags |= step_mismatch;
            }
        }
    }
    return flags;
}

bool control_helpers::check_one(context & c, dom_bindings * b, node_id id) const {
    const auto helpers = *this;
    bool candidate = false;
    {
        const auto txn = b->doc_->read();
        candidate = txn.contains(id) && helpers.will_validate(txn, b, id);
    }
    if (!candidate || helpers.validity_flags(c, b, id) == 0) { return true; }
    (void)helpers.fire(c, b, id, "invalid", false, true, helpers.plain);
    return false;
}

bool control_helpers::validate_form(context & c, dom_bindings * b, node_id form) const {
    const auto helpers = *this;
    bool ok = true;
    for (const node_id one : helpers.elements_of_form(b, form)) {
        bool submittable = false;
        bool face_candidate = false;
        {
            const auto txn = b->doc_->read();
            submittable = txn.contains(one) && helpers.is_submittable(txn, one);
            // A form-associated custom element is a submittable candidate
            // too, barred by the same three things ElementInternals'
            // willValidate is - and invalid by the flags setValidity left.
            if (txn.contains(one) && helpers.is_face(b, one)) {
                bool in_datalist = false;
                for (node_id up = txn.parent(one); up; up = txn.parent(up)) {
                    if (helpers.is(txn, up, "datalist")) { in_datalist = true; }
                }
                face_candidate = !helpers.is_disabled(txn, b, one) &&
                                 !helpers.has_attribute(txn, b, one, "readonly") && !in_datalist;
            }
        }
        if (face_candidate) {
            if (helpers.face_invalid(b, one)) {
                (void)helpers.fire(c, b, one, "invalid", false, true, helpers.plain);
                ok = false;
            }
            continue;
        }
        if (submittable && !helpers.check_one(c, b, one)) { ok = false; }
    }
    return ok;
}

} // namespace ctbrowser::shell::detail
