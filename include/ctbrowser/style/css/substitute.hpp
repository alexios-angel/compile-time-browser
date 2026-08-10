#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <ctbrowser/core/core.hpp>

// `var()` substitution: CSS Custom Properties Level 1, §3.
//
// This is the single highest-leverage thing missing from the engine. Bootstrap 5.3's
// component layer is built entirely on custom properties - `.btn` declares thirty
// `--bs-btn-*` and then reads every one of them back through `var()` - so buttons,
// cards, forms, navs, modals, tables, alerts and badges all render unstyled while
// 1,370 `var()` calls sit in the cascade as literal text that no consumer can parse.
//
// IT IS A TOKEN-STREAM OPERATION, not a value one, and that is why it happens before
// anything is interpreted: `var(--x)` may expand to a whole comma list, to a
// fragment that only makes sense joined to its neighbours, or to nothing at all.
// `rgba(var(--bs-body-color-rgb), .5)` is the case that settles it - the var()
// expands to `33, 37, 41`, three arguments where the source had one.
//
// SUBSTITUTION HAPPENS AT COMPUTED-VALUE TIME, on text, which is what lets it run
// before shorthand expansion and before any property grammar exists. `border:
// var(--w) solid var(--c)` cannot be expanded into longhands until it has been
// substituted, because until then there is no way to know how many components it has.

namespace ctbrowser::style::css {

// What a custom property holds, or nothing if it is not in scope.
//
// An EMPTY string is a real answer and not the same as absent: `--x: ;` is an empty
// but VALID custom property, and it substitutes to nothing rather than making the
// declaration invalid.
using custom_lookup = std::function<std::optional<std::string_view>(atom)>;

// The value with every `var()` replaced, or nothing if the result is INVALID AT
// COMPUTED-VALUE TIME - a missing custom property with no fallback, a cycle, or a
// substitution that introduced a top-level `!important` or `;`.
//
// WHAT THE CALLER MUST DO WITH nothing IS `unset`, not "drop the declaration". The
// difference is observable and it is the classic wrong implementation:
//
//     color: red;
//     color: var(--missing);
//
// renders as the INHERITED colour in Chrome, not red. Dropping the second
// declaration would let the first one win.
[[nodiscard]] std::optional<std::string> substitute_var(std::string_view value,
                                                        const custom_lookup & lookup,
                                                        atom_table & atoms);

// Whether a value contains a `var()` at all - a cheap guard so the substitution
// machinery only runs on the values that need it. Bootstrap: 834 of 5,365.
//
// A plain `find` would be wrong for `content: "var(--x)"`, where the text is inside a
// string; this is only ever used to SKIP work, so a false positive costs a tokenize
// and a false negative is impossible.
[[nodiscard]] bool may_have_var(std::string_view value) noexcept;

} // namespace ctbrowser::style::css
