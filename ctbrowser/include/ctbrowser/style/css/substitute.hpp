#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <ctbrowser/core/core.hpp>

// `var()` substitution: CSS Custom Properties Level 1, §3 - and `attr()`, CSS
// Values 5 §attr, which is the same operation over a different source.
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
//
// `attr()` IS THE SAME OPERATION and lives here for that reason: CSS Values 5
// calls both ARBITRARY SUBSTITUTION FUNCTIONS, they are replaced in one pass,
// and each may appear inside the other's arguments - `attr(var(--name))` names
// the attribute a custom property holds, and `attr(data-x type(*))` may hold a
// `var()`. The difference is the source: an attribute rather than a custom
// property, and a TYPE the attribute's text must parse as before it counts.

namespace ctbrowser::style::css {

// What a custom property holds, or nothing if it is not in scope.
//
// An EMPTY string is a real answer and not the same as absent: `--x: ;` is an empty
// but VALID custom property, and it substitutes to nothing rather than making the
// declaration invalid.
using custom_lookup = std::function<std::optional<std::string_view>(atom)>;

// What an ATTRIBUTE of the element holds, or nothing if it has none by that
// name. The same absent/empty distinction as above, and for the same reason:
// `attr(data-x)` on an element without the attribute takes the fallback, while
// one with `data-x=""` is the empty string. An engine without an element to ask
// leaves this empty, and every `attr()` is then left as written.
using attribute_lookup = std::function<std::optional<std::string>(std::string_view name)>;

// The value with every `var()` and `attr()` replaced, or nothing if the result is
// INVALID AT COMPUTED-VALUE TIME - a missing custom property with no fallback, a
// cycle, an attribute whose text does not parse as the type `attr()` asked for
// and no fallback to take instead, or a substitution that introduced a
// top-level `!important` or `;`.
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
                                                        atom_table & atoms,
                                                        const attribute_lookup & attributes = {});

// Whether a value contains a `var()` or an `attr()` at all - a cheap guard so
// the substitution machinery only runs on the values that need it.
//
// A plain `find` would be wrong for `content: "var(--x)"`, where the text is inside a
// string; this is only ever used to SKIP work, so a false positive costs a tokenize
// and a false negative is impossible.
[[nodiscard]] bool may_have_var(std::string_view value) noexcept;

} // namespace ctbrowser::style::css
