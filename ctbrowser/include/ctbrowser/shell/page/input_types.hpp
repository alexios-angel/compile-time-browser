#pragma once
// The input types' VALUE MODEL, HTML 4.10.5.1 and 2.3.5: what a number,
// range, date, month, week, time or datetime-local control's value string
// means as a number, how a number is written back in the type's own format,
// the step units, the e-mail and URL validity checks - and the value
// sanitization algorithm each type state runs over a value it is given.
//
// Here, in the page half, rather than in the form bindings that used to hold
// them file-local: the form store sanitises a control's value when its type
// changes or its attribute is re-read, and a binding is not the only reader.

#include <optional>
#include <string>
#include <string_view>

namespace ctbrowser::shell::input_types {

// An <input>'s type STATE, HTML 4.10.5: the attribute's keyword, else text.
[[nodiscard]] std::string type_state_of(std::string_view type_attribute);

// Which mode the `value` IDL attribute is in for a type state (HTML 4.10.5.1):
// "value", "default", "default/on" or "filename".
[[nodiscard]] std::string_view value_mode_of(std::string_view type);

// The "value sanitization algorithm" of a type state over `value`; `min`,
// `max` and `step` are the attributes' text (range needs them). The result is
// what the control's value becomes.
[[nodiscard]] std::string sanitize_value(std::string_view type, std::string value,
                                         std::string_view min = {}, std::string_view max = {},
                                         std::string_view step = {});

// A "valid floating-point number" (HTML 2.3.5.2) when `lenient` is false; the
// "rules for parsing floating-point number values" when true.
[[nodiscard]] bool parse_float(std::string_view text, double & out, bool lenient);

// A type's value as a number (nullopt when it is not one) - milliseconds for
// the dates and times, months for month, the number for number and range -
// and back, in the type's own format.
[[nodiscard]] std::optional<double> type_value_to_number(std::string_view type,
                                                         std::string_view text);
[[nodiscard]] std::string type_number_to_text(std::string_view type, double number);
[[nodiscard]] double month_index_to_ms(double months);
[[nodiscard]] double ms_to_month_index(double ms);
// The step scale factor and the default step, HTML 4.10.5.1, in the type's
// unit (ms, months, or the number).
[[nodiscard]] double step_scale_of(std::string_view type);
[[nodiscard]] double default_step_of(std::string_view type);

// The IDL-exposed autofill value of an `autocomplete` attribute (HTML
// 4.10.18.7.1 "autofill detail tokens"): the recognised tokens, lowercased
// and in canonical order - `section-*`, `shipping`/`billing`, a contact
// type, the field, `webauthn` - or "" for anything the algorithm sends to
// its "default" step. `anchor_mantle` is a hidden input, on which `on` and
// `off` are not accepted. `has_attribute` false is the missing attribute.
[[nodiscard]] std::string autocomplete_idl_value(std::string_view attribute, bool has_attribute,
                                                 bool anchor_mantle);

// A "valid e-mail address" (HTML 4.10.5.1.5) and a "valid URL potentially
// surrounded by spaces" that is absolute.
[[nodiscard]] bool is_valid_email(std::string_view text);
[[nodiscard]] bool is_valid_url(std::string_view text);

} // namespace ctbrowser::shell::input_types
