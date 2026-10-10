//===- substitute.hpp - var() substitution ----------------------*- C++ -*-===//
//
// Arbitrary substitution functions: `var()` (CSS Custom Properties Level 1
// §3), `attr()` (CSS Values 5 §attr), `if()` and custom `--name()` functions.
//
// Substitution works on the token stream, not on values, which is why it
// happens before anything is interpreted: `var(--x)` may expand to a whole
// comma list, to a fragment that only makes sense joined to its neighbours, or
// to nothing at all. In `rgba(var(--bs-body-color-rgb), .5)` the var()
// expands to `33, 37, 41`, three arguments where the source had one.
//
// It happens at computed-value time, on text, so it runs before shorthand
// expansion and before any property grammar applies: `border: var(--w) solid
// var(--c)` cannot be split into longhands until it is substituted, because
// until then the number of components is unknown.
//
// `attr()` lives here because CSS Values 5 classes it with `var()` as an
// arbitrary substitution function: both are replaced in one pass, and each may
// appear inside the other's arguments (`attr(var(--name))` names the attribute
// a custom property holds, and `attr(data-x type(*))` may hold a `var()`). The
// difference is the source, an attribute rather than a custom property, and a
// type the attribute's text must parse as before it counts.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/style/css/calc.hpp>

namespace ctbrowser::style::css {

/// What a custom property holds, or nullopt if it is not in scope.
///
/// An empty string is a real answer, not the same as absent: `--x: ;` is an
/// empty but valid custom property, and it substitutes to nothing rather than
/// making the declaration invalid.
using custom_lookup = std::function<std::optional<std::string_view>(atom)>;

/// What an attribute of the element holds, or nullopt if it has none by that
/// name. The same absent/empty distinction applies: `attr(data-x)` on an
/// element without the attribute takes the fallback, while one with
/// `data-x=""` is the empty string. A caller with no element leaves this
/// empty, and every `attr()` is then left as written.
using attribute_lookup = std::function<std::optional<std::string>(std::string_view name)>;

/// A registered custom property (CSS Properties and Values API 1 §2): the
/// `@property` rule or `CSS.registerProperty()` call that gave `--x` a syntax,
/// an inheritance flag and an initial value. A registered property's value is
/// parsed against the syntax at computed-value time, computed like the type it
/// names (`1em` becomes `30px`), and falls back to the initial value when it
/// does not parse.
struct property_registration {
    std::string syntax = "*";
    bool inherits = true;
    std::string initial;
};

/// A custom function (CSS Functions and Mixins 1 §2): `@function
/// --name(<params>) [returns <type>]? { <locals>; result: <value> }`.
///
/// A call `--name(args)` is an arbitrary substitution function like `var()`.
/// Its arguments are substituted in the caller's scope and bound to the
/// parameters (a typed one computed like its type, a missing one taking its
/// default). The body's locals and `result` are substituted in the function's
/// own scope, which sees the parameters and locals first and the calling
/// element's custom properties after. A typed `result` is computed like its
/// type on the way out; an untyped one is left as tokens.
struct custom_function {
    struct parameter {
        std::string name;    // `--x`.
        std::string syntax;  // `<number>`, or `*` when untyped.
        std::string initial; // The default, when there is one.
        bool has_default = false;
    };
    std::string name;
    std::vector<parameter> parameters;
    std::string returns = "*"; // The result's syntax, `*` when untyped.
    /// The body's declarations in source order: the locals (`--x: ...`) and
    /// `result`, told apart by name. A later declaration of a name wins.
    std::vector<std::pair<std::string, std::string>> body;
};
using function_lookup = std::function<const custom_function *(std::string_view name)>;

/// The computed value of `text` for a registered `syntax`, or nullopt when the
/// text does not parse against it. `*` takes anything; a `<length>` or other
/// dimension is folded against `ctx` into its canonical unit; a `calc()` is
/// folded wherever it sits.
[[nodiscard]] std::optional<std::string> compute_registered(std::string_view text,
                                                            std::string_view syntax,
                                                            const length_context & ctx);

/// The lookup a style query needs: whether a custom property is registered,
/// and how.
using registration_lookup = std::function<const property_registration *(std::string_view property)>;

/// The facts `if()` conditions and custom functions need (CSS Values 5
/// §if-notation). `if( <if-condition> : <declaration-value>? ; ... )` is
/// replaced by the value of the first branch whose condition holds, or `else`.
/// A condition is a boolean expression over three tests, each needing a fact
/// the substituter does not have on its own:
///
///   style( <style-query> )     the element's computed values: custom ones
///                              through `custom_lookup`, the parent's for
///                              `inherit` through `inherited`, and any other
///                              property's through `computed`
///   media( <media-condition> ) the window, through `media`
///   supports( ... )            the property table, queried directly
///
/// A missing callback makes that test false rather than guessing; with no
/// environment at all every `if()` is left as written, like an `attr()` without
/// an element.
struct condition_environment {
    /// A property's computed value on the parent, or nullopt if it has none:
    /// what `style(--x: inherit)` compares against.
    std::function<std::optional<std::string>(std::string_view property)> inherited;
    /// A non-custom property's computed value on the element, for
    /// `style(color: green)`.
    std::function<std::optional<std::string>(std::string_view property)> computed;
    /// A media condition's truth against the current environment, or nullopt
    /// for one that does not parse.
    std::function<std::optional<bool>(std::string_view condition)> media;
    /// Whether a custom property is registered: a query against one compares
    /// computed values of its type, and `initial` names its initial value.
    registration_lookup registered;
    /// The custom functions a `--name()` call may reach; without it every
    /// dashed function is left as written.
    function_lookup functions;
    /// Canonicalises a colour, when the caller has a colour parser, so a
    /// `<color>` registration can find `green` equal to `rgb(0, 128, 0)`.
    /// Without it the two compare as different text.
    std::function<std::string(std::string_view)> canonical_color;
    /// Called with the name of every custom property a var() reads, so the
    /// cascade can detect a dependency it must reject: `font-size: var(--x)`
    /// where the registered `--x` is in `em` (CSS Properties and Values API 1
    /// §2.4).
    std::function<void(std::string_view property)> on_read;
    /// The bases a range query resolves its dimensions against: `style(10em >
    /// 3px)` needs a font size.
    length_context lengths;
    /// The property the value belongs to, when the caller knows it. A custom
    /// property whose `if()` queries itself (`--x: if(style(--x): a; else: b)`)
    /// is a cycle, and only the name distinguishes that from a query about
    /// another property.
    std::string property;
};

/// The value with every `var()`, `attr()` and `if()` replaced, or nullopt if
/// the result is invalid at computed-value time: a missing custom property with
/// no fallback, a cycle, an attribute whose text does not parse as the type
/// `attr()` asked for with no fallback, or a substitution that introduced a
/// top-level `!important` or `;`.
///
/// On nullopt the caller must treat the property as `unset`, not drop the
/// declaration. The difference is observable:
///
///     color: red;
///     color: var(--missing);
///
/// renders in the inherited colour in Chrome, not red. Dropping the second
/// declaration would let the first one win.
[[nodiscard]] std::optional<std::string> substitute_var(
    std::string_view value, const custom_lookup & lookup, atom_table & atoms,
    const attribute_lookup & attributes = {}, const condition_environment * conditions = nullptr);

/// Whether a value may contain a `var()`, `attr()` or `if()`: a cheap guard so
/// substitution only runs on the values that need it. It is only used to skip
/// work, so a false positive (such as `content: "var(--x)"`, inside a string)
/// costs one tokenize, and a false negative is impossible.
[[nodiscard]] bool may_have_var(std::string_view value) noexcept;

} // namespace ctbrowser::style::css
