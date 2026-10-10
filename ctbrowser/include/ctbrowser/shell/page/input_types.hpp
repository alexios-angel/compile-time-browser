//===- input_types.hpp - Input type value model -----------------*- C++ -*-===//
//
// The input types' value model (HTML 4.10.5.1 and 2.3.5): what a number,
// range, date, month, week, time or datetime-local control's value string
// means as a number, how a number is written back in the type's own format,
// the step units, the e-mail and URL validity checks, and the value
// sanitization algorithm each type state runs over a value it is given.
//
// It lives in the shell's page layer rather than in the form bindings because
// the form store also sanitises a control's value, when its type changes or
// its attribute is re-read.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ctbrowser::shell::input_types {

/// An <input>'s type state (HTML 4.10.5): the attribute's keyword, else text.
[[nodiscard]] std::string type_state_of(std::string_view type_attribute);

/// Which mode the `value` IDL attribute is in for a type state (HTML 4.10.5.1):
/// "value", "default", "default/on" or "filename".
[[nodiscard]] std::string_view value_mode_of(std::string_view type);

/// The "value sanitization algorithm" of a type state over `value`; `min`,
/// `max`, `step` and `value_attribute` are the content attributes' text (range
/// needs them, the last for its step base), `multiple` the attribute's
/// presence (email), and `display_p3` and `alpha` a color control's
/// `colorspace` state and `alpha` attribute. The result is what the control's
/// value becomes.
[[nodiscard]] std::string sanitize_value(std::string_view type, std::string value,
                                         std::string_view min = {}, std::string_view max = {},
                                         std::string_view step = {},
                                         std::string_view value_attribute = {},
                                         bool multiple = false, bool display_p3 = false,
                                         bool alpha = false);

/// A number control's value from the text the user typed into it (HTML
/// 4.10.5.1.12): the text when it is a valid floating-point number, the number
/// itself when the text is one with a trailing decimal point ("1." is 1), else
/// the empty string, with the typed text left as it was ("bad input").
[[nodiscard]] std::string number_value_of_input(std::string_view typed);

/// "Normalize newlines" (Infra): every CR LF and then every lone CR becomes
/// LF. Used for a textarea's API value and a custom validity message.
[[nodiscard]] std::string normalize_newlines(std::string_view text);

/// Parses a "valid floating-point number" (HTML 2.3.5.2) when `lenient` is
/// false, or applies the "rules for parsing floating-point number values"
/// when true. Returns false when `text` does not parse.
[[nodiscard]] bool parse_float(std::string_view text, double & out, bool lenient);

/// A type's value as a number (nullopt when it is not one): milliseconds for
/// the dates and times, months for month, the number for number and range.
/// type_number_to_text converts back, in the type's own format.
[[nodiscard]] std::optional<double> type_value_to_number(std::string_view type,
                                                         std::string_view text);
[[nodiscard]] std::string type_number_to_text(std::string_view type, double number);
[[nodiscard]] double month_index_to_ms(double months);
[[nodiscard]] double ms_to_month_index(double ms);
/// The step scale factor and the default step (HTML 4.10.5.1), in the type's
/// unit (ms, months, or the number).
[[nodiscard]] double step_scale_of(std::string_view type);
[[nodiscard]] double default_step_of(std::string_view type);

/// The IDL-exposed autofill value of an `autocomplete` attribute (HTML
/// 4.10.18.7.1 "autofill detail tokens"): the recognised tokens, lowercased
/// and in canonical order (`section-*`, `shipping`/`billing`, a contact
/// type, the field, `webauthn`), or "" for anything the algorithm sends to
/// its "default" step. `anchor_mantle` is true for a hidden input, on which
/// `on` and `off` are not accepted. `has_attribute` false means the attribute
/// is missing.
[[nodiscard]] std::string autocomplete_idl_value(std::string_view attribute, bool has_attribute,
                                                 bool anchor_mantle);

/// A "valid e-mail address" (HTML 4.10.5.1.5), and a "valid URL potentially
/// surrounded by spaces" that is absolute.
[[nodiscard]] bool is_valid_email(std::string_view text);
[[nodiscard]] bool is_valid_url(std::string_view text);

} // namespace ctbrowser::shell::input_types
