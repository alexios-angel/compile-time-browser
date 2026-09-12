#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/style/css/calc.hpp>

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

// A REGISTERED CUSTOM PROPERTY, CSS Properties and Values API 1 §2: the
// `@property` rule or the `CSS.registerProperty()` call that gave `--x` a
// syntax, an inheritance flag and an initial value. A registered property is
// no longer a token stream that means whatever reads it: its value is parsed
// against the syntax at computed-value time, computed like the type it names
// (`1em` becomes `30px`), and falls back to the initial value when it does not
// parse.
struct property_registration {
    std::string syntax = "*";
    bool inherits = true;
    std::string initial;
};

// The computed value of `text` for a registration, or nothing when the text
// does not parse against the syntax. `*` takes anything; a `<length>` or
// another dimension is folded against `ctx` into its canonical unit; a
// `calc()` is folded wherever it sits.
[[nodiscard]] std::optional<std::string> compute_registered(std::string_view text,
                                                            std::string_view syntax,
                                                            const length_context & ctx);

// The lookup a style query needs: is this custom property registered, and how?
using registration_lookup = std::function<const property_registration *(std::string_view property)>;

// WHAT `if()` MAY ASK, CSS Values 5 §if-notation. The third arbitrary
// substitution function: `if( <if-condition> : <declaration-value>? ; ... )`,
// replaced by the value of the first branch whose condition holds, or `else`.
// A condition is a boolean expression over three tests, and each needs a fact
// the substituter does not have on its own:
//
//   style( <style-query> )     the element's computed values - the custom ones
//                              through `custom_lookup`, the parent's for
//                              `inherit` through `inherited`, and any other
//                              property's through `computed`
//   media( <media-condition> ) the window, through `media`
//   supports( ... )            the property table, asked directly
//
// An engine without one of these leaves that test false rather than guessing;
// with no environment at all every `if()` is left as written, like an `attr()`
// without an element.
struct condition_environment {
    // A property's computed value on the PARENT, or nothing if it has none:
    // what `style(--x: inherit)` compares against.
    std::function<std::optional<std::string>(std::string_view property)> inherited;
    // A NON-custom property's computed value on the element, for
    // `style(color: green)`.
    std::function<std::optional<std::string>(std::string_view property)> computed;
    // A media condition's truth against the current environment, or nothing for
    // one that does not parse.
    std::function<std::optional<bool>(std::string_view condition)> media;
    // Whether a custom property is registered: a query against one compares
    // computed values of its type, and `initial` names its initial value.
    registration_lookup registered;
    // A colour in its canonical form, when the caller has a colour parser: a
    // `<color>` registration compares `green` with `rgb(0, 128, 0)` through
    // it. The style engine has none of its own - colours are paint's - and
    // without one the two are the different texts they are.
    std::function<std::string(std::string_view)> canonical_color;
    // Told the name of every custom property a var() reads, so the cascade can
    // see a dependency it has to refuse: `font-size: var(--x)` where the
    // registered `--x` is in `em` (CSS Properties and Values API 1 §2.4).
    std::function<void(std::string_view property)> on_read;
    // The bases a range query resolves its dimensions against: `style(10em >
    // 3px)` needs a font size.
    length_context lengths;
    // THE PROPERTY THE VALUE BELONGS TO, when the caller knows it. A custom
    // property whose `if()` queries itself - `--x: if(style(--x): a; else: b)`
    // - is a cycle, and only the name tells that from a query about some
    // other property.
    std::string property;
};

// The value with every `var()`, `attr()` and `if()` replaced, or nothing if the result is
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
[[nodiscard]] std::optional<std::string> substitute_var(
    std::string_view value, const custom_lookup & lookup, atom_table & atoms,
    const attribute_lookup & attributes = {}, const condition_environment * conditions = nullptr);

// Whether a value contains a `var()`, an `attr()` or an `if()` at all - a cheap guard so
// the substitution machinery only runs on the values that need it.
//
// A plain `find` would be wrong for `content: "var(--x)"`, where the text is inside a
// string; this is only ever used to SKIP work, so a false positive costs a tokenize
// and a false negative is impossible.
[[nodiscard]] bool may_have_var(std::string_view value) noexcept;

} // namespace ctbrowser::style::css
