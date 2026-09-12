#pragma once
// Private to lib/Script/builtins/text/: what RegExp and String share.
//
// 22.1.3's match, matchAll, replace, replaceAll, search and split each end in
// "return Invoke(rx, @@method, ...)", and 22.2.6's @@methods are written over
// RegExpExec and the same GetSubstitution the string forms use. These are the
// operations both files spell, declared once so that neither carries a second
// copy that can disagree about a boundary.

#include "../internal.hpp"

namespace ctbrowser::script::builtins_detail {

// [[RegExpMatcher]]: is `v` an object made by RegExpAlloc? The slot is a
// private-keyed property (value.hpp says why that IS an internal slot here);
// `regexp_slots` reads the two it carries.
[[nodiscard]] bool has_regexp_matcher(value v);
[[nodiscard]] bool regexp_slots(value v, std::string & source, std::string & flags);

// IsRegExp, 7.2.8: `@@match` wins over the slot when present at all. FALSE
// with a throw in flight when the getter threw.
[[nodiscard]] bool is_regexp(context & cx, value v, bool & out);

// RegExpCreate, 22.2.3.1: a fresh RegExp over ToString(pattern) - undefined
// is the empty pattern - and the flags given. Undefined when the pattern or
// flags are refused (the SyntaxError is in flight).
[[nodiscard]] value regexp_create(context & cx, value pattern, value flags);

// RegExpExec, 22.2.7.1: the receiver's own `exec` when it has a callable one
// (its answer must be an object or null), the built-in matcher otherwise.
// Undefined means a throw is in flight; null means no match.
[[nodiscard]] value regexp_exec(context & cx, value rx, value subject);

// GetSubstitution, 22.1.3.19.1: the `$` template language of replace.
// `named_captures` is undefined when the pattern has no groups object, in
// which case `$<` is literal. FALSE with a throw in flight.
[[nodiscard]] bool get_substitution(context & cx, const std::string & matched,
                                    const std::string & subject, std::size_t position,
                                    std::span<const value> captures, value named_captures,
                                    const std::string & replacement_template, std::string & out);

// AdvanceStringIndex, 22.2.7.3. The subject is UTF-8 here and a "code unit" is
// a byte, so a full-Unicode advance steps over one UTF-8 sequence.
[[nodiscard]] std::size_t advance_string_index(const std::string & s, std::size_t index,
                                               bool full_unicode);

// ToLength(Get(rx, "lastIndex")) and Set(rx, "lastIndex", v, true). FALSE with
// a throw in flight - a frozen pattern refuses the write with a TypeError.
[[nodiscard]] bool get_last_index(context & cx, value rx, double & out);
[[nodiscard]] bool set_last_index(context & cx, value rx, double v);

} // namespace ctbrowser::script::builtins_detail
