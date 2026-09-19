#include "helpers.hpp"

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
    return 0;
}

std::optional<double> control_helpers::bound_of(const read_txn & txn, dom_bindings * b, node_id id,
                                                const char * which) const {
    const auto helpers = *this;
    return helpers.to_number(txn, b, id, helpers.attribute_of(txn, b, id, which));
}

bool control_helpers::is_step_aligned(double number, double base, double step) const {
    const double quotient = (number - base) / step;
    return std::fabs(quotient - std::round(quotient)) < 1e-9 * std::max(1.0, std::fabs(quotient));
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
    for (node_id up = txn.parent(id); up; up = txn.parent(up)) {
        if (helpers.is(txn, up, "datalist")) { return false; }
    }
    return true;
}

value control_helpers::compile_pattern(context & c, const std::string & pattern) const {
    const auto helpers = *this;
    auto * on = helpers.proto("ValidityState");
    if (on == nullptr) { return value::null(); }
    value compiler = value::undefined();
    if (const value * held = on->find("__compile"); held != nullptr) { compiler = *held; }
    if (!compiler.is_callable()) {
        script::program compiled = script::compiler::compile(
            "return (function (p) {\n"
            "    try { return new RegExp('^(?:' + p + ')$', 'v'); } catch (e) {}\n"
            "    try { return new RegExp('^(?:' + p + ')$', 'u'); } catch (e) {}\n"
            "    return null;\n"
            "});\n");
        if (!compiled.ok) { return value::null(); }
        compiler = c.run_nested(c.own_program(std::move(compiled)));
        if (!compiler.is_callable()) { return value::null(); }
        on->define("__compile", compiler, script::attr_none);
    }
    const value text = c.string(pattern);
    return c.call(compiler, std::span<const value>{&text, 1});
}

bool control_helpers::matches_pattern(context & c, const std::string & pattern,
                                      const std::string & text) const {
    const auto helpers = *this;
    const value re = helpers.compile_pattern(c, pattern);
    if (!re.is_object_like()) { return true; }
    const value test = c.lookup_property(re, "test");
    if (!test.is_callable()) { return true; }
    const value subject = c.string(text);
    return context::truthy(c.call(test, std::span<const value>{&subject, 1}, re));
}

unsigned control_helpers::validity_flags(context & c, dom_bindings * b, node_id id) const {
    const auto helpers = *this;
    unsigned flags = 0;
    const auto txn = b->doc_->read();
    if (!txn.contains(id)) { return flags; }
    const value custom = helpers.slot_of(c, b, id, custom_slot);
    if (custom.is_string() && !c.to_string(custom).empty()) { flags |= custom_error; }
    // BARRED FROM CONSTRAINT VALIDATION - disabled, readonly, hidden -
    // leaves every flag but customError false: the browsers agree and
    // form-validation-validity-*.html assert it for each state.
    if (!helpers.will_validate(txn, b, id)) { return flags; }
    const std::string_view local = txn.local_name(id);
    const bool required = helpers.has_attribute(txn, b, id, "required");
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
            // The placeholder label option: a display-size-1 single
            // select's first option child with an empty value.
            if (!missing && helpers.display_size(txn, b, id) == 1 &&
                !helpers.has_attribute(txn, b, id, "multiple")) {
                node_id first_child;
                for (const node_id child : txn.children(id)) {
                    if (helpers.is(txn, child, "option")) {
                        first_child = child;
                        break;
                    }
                }
                if (first_child && options[first] == first_child &&
                    helpers.option_value(txn, b, first_child).empty()) {
                    missing = true;
                }
            }
            if (missing) { flags |= value_missing; }
        }
        return flags;
    }
    if (local == "textarea") {
        if (required && helpers.value_of(txn, b, id).empty()) { flags |= value_missing; }
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
        // A radio with no name is in no group, and the requirement is
        // the group's (4.10.5.1.16) - so it is never missing.
        if (name.empty()) { return flags; }
        {
            const node_id owner = helpers.form_owner(txn, b, id);
            helpers.walk_tree(txn, helpers.tree_top(txn, b, id), [&](node_id other) {
                if (other == id || !helpers.is(txn, other, "input") ||
                    helpers.input_type(txn, b, other) != "radio" ||
                    helpers.attribute_of(txn, b, other, "name") != name ||
                    helpers.form_owner(txn, b, other) != owner) {
                    return;
                }
                if (helpers.has_attribute(txn, b, other, "required")) { any_required = true; }
                if (b->forms_->state_of(txn, *b->atoms_, other).checked) { any_checked = true; }
            });
        }
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
    if (required && text.empty()) { flags |= value_missing; }
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
                ok = helpers.matches_pattern(c, pattern,
                                             text.substr(start, comma == std::string::npos
                                                                    ? std::string::npos
                                                                    : comma - start));
                if (comma == std::string::npos) { break; }
                start = comma + 1;
            }
        } else {
            ok = helpers.matches_pattern(c, pattern, text);
        }
        if (!ok) { flags |= pattern_mismatch; }
    }
    if (type == "number" || type == "date" || type == "month" || type == "week" || type == "time" ||
        type == "datetime-local" || type == "range") {
        const std::optional<double> number = helpers.to_number(txn, b, id, text);
        if (!text.empty() && !number) { flags |= bad_input; }
        if (number && type != "range") {
            if (const auto min = helpers.bound_of(txn, b, id, "min"); min && *number < *min) {
                flags |= range_underflow;
            }
            if (const auto max = helpers.bound_of(txn, b, id, "max"); max && *number > *max) {
                flags |= range_overflow;
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
        {
            const auto txn = b->doc_->read();
            submittable = txn.contains(one) && helpers.is_submittable(txn, one);
        }
        if (submittable && !helpers.check_one(c, b, one)) { ok = false; }
    }
    return ok;
}

} // namespace ctbrowser::shell::detail
