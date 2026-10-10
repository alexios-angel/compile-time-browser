//===- internal.hpp - RegExp/String helpers ---------------------*- C++ -*-===//
//
// Private to lib/Script/builtins/text/: what RegExp and String share.
//
// ECMA-262 22.1.3's match, matchAll, replace, replaceAll, search and split
// each end in "return Invoke(rx, @@method, ...)" over a pattern RegExpCreate
// made, and replace's `$` template is the same GetSubstitution both forms use.
// The helpers below are what string.cpp reaches; RegExpExec and its
// neighbours are regexp.cpp's alone.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../internal.hpp"

namespace ctbrowser::script::builtins_detail {

/// IsRegExp (7.2.8): sets `out` to whether `v` is a RegExp. A present
/// `@@match` decides over the internal slot. Returns false, with the exception
/// in flight, only when the `@@match` getter threw.
[[nodiscard]] bool is_regexp(context & cx, value v, bool & out);

/// RegExpCreate (22.2.3.1): a fresh RegExp over ToString(pattern), where
/// undefined is the empty pattern, and the flags given. Returns undefined when
/// the pattern or flags are refused (the SyntaxError is in flight).
[[nodiscard]] value regexp_create(context & cx, value pattern, value flags);

/// GetSubstitution (22.1.3.19.1): the `$` template language of replace.
/// `named_captures` is undefined when the pattern has no groups object, in
/// which case `$<` is literal. Numbered captures are strings or undefined,
/// already coerced by RegExp @@replace. Returns false with a throw in flight.
[[nodiscard]] bool get_substitution(context & cx, const std::string & matched,
                                    const std::string & subject, std::size_t position,
                                    std::span<const value> captures, value named_captures,
                                    const std::string & replacement_template, std::string & out);

/// The default case conversion of 22.1.3.28/22.1.3.30 over WTF-8 text:
/// Unicode's full lower- (`upper` false) or uppercase mapping, Final_Sigma
/// included. Defined in case_mapping.cpp over the generated case_mapping.inc.
[[nodiscard]] std::string unicode_case(std::string_view s, bool upper);

/// Checks that appending `addition` bytes to a string of `length` bytes stays
/// within max_string_length, the same ceiling repeat and pad use, before
/// replacement expansion or concatenation can request an unbounded native
/// allocation. Throws a RangeError and returns false when it would not.
[[nodiscard]] inline bool check_string_growth(context & cx, std::size_t length,
                                              std::size_t addition) {
    constexpr auto limit = static_cast<std::size_t>(max_string_length);
    if (length <= limit && addition <= limit - length) { return true; }
    cx.throw_error("RangeError", "Invalid string length");
    return false;
}

} // namespace ctbrowser::script::builtins_detail
