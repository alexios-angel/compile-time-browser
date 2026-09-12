#pragma once
// Private to lib/Script/builtins/text/: what RegExp and String share.
//
// 22.1.3's match, matchAll, replace, replaceAll, search and split each end in
// "return Invoke(rx, @@method, ...)" over a pattern RegExpCreate made, and
// replace's `$` template is the same GetSubstitution both forms use. These
// three are what string.cpp reaches; RegExpExec and its neighbours are
// regexp.cpp's alone.

#include "../internal.hpp"

namespace ctbrowser::script::builtins_detail {

// IsRegExp, 7.2.8: `@@match` wins over the slot when present at all. FALSE
// with a throw in flight when the getter threw.
[[nodiscard]] bool is_regexp(context & cx, value v, bool & out);

// RegExpCreate, 22.2.3.1: a fresh RegExp over ToString(pattern) - undefined
// is the empty pattern - and the flags given. Undefined when the pattern or
// flags are refused (the SyntaxError is in flight).
[[nodiscard]] value regexp_create(context & cx, value pattern, value flags);

// GetSubstitution, 22.1.3.19.1: the `$` template language of replace.
// `named_captures` is undefined when the pattern has no groups object, in
// which case `$<` is literal. FALSE with a throw in flight.
[[nodiscard]] bool get_substitution(context & cx, const std::string & matched,
                                    const std::string & subject, std::size_t position,
                                    std::span<const value> captures, value named_captures,
                                    const std::string & replacement_template, std::string & out);

} // namespace ctbrowser::script::builtins_detail
