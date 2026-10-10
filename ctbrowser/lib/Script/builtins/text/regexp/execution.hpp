//===- execution.hpp - RegExp internals -------------------------*- C++ -*-===//
//
// RegExp internals shared by regexp.cpp and regexp/execution.cpp: the
// private-key slot names an instance and %RegExp.prototype% carry, the
// compiled-pattern cache, and RegExpExec with its neighbours. Each function is
// documented at its definition in execution.cpp.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../internal.hpp"
#include <map>

namespace ctbrowser::script::builtins_detail::regexp_detail {

/// Compiled patterns keyed by `source\0flags` (see compiled()).
using regex_cache = std::map<std::string, std::shared_ptr<rx::rx_prog>>;

/// [[OriginalSource]]: the pattern text of a RegExp instance.
inline constexpr std::string_view slot_source = CTBROWSER_PRIVATE_KEY("RegExpSource");

/// [[OriginalFlags]]: the flags text of a RegExp instance.
inline constexpr std::string_view slot_flags = CTBROWSER_PRIVATE_KEY("RegExpFlags");

/// [[LegacyFeaturesEnabled]] (the legacy RegExp features, Annex B): present on
/// an instance RegExpAlloc made with %RegExp% itself as new.target (a literal,
/// `RegExp(p)`, `new RegExp(p)`) and absent on a subclass's.
inline constexpr std::string_view slot_legacy = CTBROWSER_PRIVATE_KEY("RegExpLegacyFeatures");
/// %RegExp%'s legacy static slots ([[RegExpInput]], [[RegExpLastMatch]], ...)
/// as one record on the intrinsic %RegExp.prototype%, which a page can neither
/// replace nor spell. slot_intrinsic stores %RegExp% itself beside it, which
/// the accessors compare the receiver against. The record is an array laid out
/// as legacy_field says; undefined in its place is the "empty" an
/// invalidation leaves.
inline constexpr std::string_view slot_legacy_record = CTBROWSER_PRIVATE_KEY("RegExpLegacyStatics");
inline constexpr std::string_view slot_intrinsic = CTBROWSER_PRIVATE_KEY("RegExpIntrinsic");
/// Indices into the slot_legacy_record array.
enum legacy_field : std::size_t {
    legacy_input,
    legacy_last_match,
    legacy_last_paren,
    legacy_subject, // The input as matched: `RegExp.input = x` does not move the contexts.
    legacy_begin,
    legacy_end,
    legacy_paren1, // $1; $2 through $9 follow.
    legacy_fields = legacy_paren1 + 9
};

[[nodiscard]] std::shared_ptr<rx::rx_prog> compiled(const std::shared_ptr<regex_cache> & cache,
                                                    const std::string & source,
                                                    const std::string & flags);
[[nodiscard]] object_object * regexp_table(value v);
[[nodiscard]] object_object * this_regexp(context & cx, const char * method);
[[nodiscard]] std::string escape_pattern(const std::string & source);
/// Builds exec's result array. `subject` is a string value; the result's
/// positions are its unit indices. `matched` is the text the matcher walked
/// when that was not the subject's own (its pairs split, for a RegExp without
/// `u`) and cuts the strings.
[[nodiscard]] value exec_result(context & cx, const rx::rx_prog & program, value subject,
                                const rx::rx_match & m, const string_object * matched = nullptr);
bool has_regexp_matcher(value v);
bool regexp_slots(value v, std::string & source, std::string & flags);
std::size_t advance_string_index(const string_object & s, std::size_t index, bool full_unicode);
bool get_last_index(context & cx, value rx, double & out);
bool set_last_index(context & cx, value rx, double v);
value regexp_exec(context & cx, value rx, value subject);
[[nodiscard]] value builtin_exec(context & cx, const std::shared_ptr<regex_cache> & cache, value rx,
                                 value subject);
/// UpdateLegacyRegExpStaticProperties from an exec result, or, for undefined,
/// InvalidateLegacyRegExpStaticProperties, as RegExpBuiltinExec runs them.
void set_legacy_statics(context & cx, value result);
[[nodiscard]] value species_constructor(context & cx, value o, value fallback);
[[nodiscard]] bool read_flags(context & cx, value rx, std::string & out);
[[nodiscard]] bool object_this(context & cx, value self, const char * method);
[[nodiscard]] bool matched_text(context & cx, value result, std::string & out);
value regexp_string_iterator_next(context & cx, std::span<value>);

} // namespace ctbrowser::script::builtins_detail::regexp_detail
