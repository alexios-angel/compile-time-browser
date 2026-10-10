//===- media.hpp - Media queries --------------------------------*- C++ -*-===//
//
// Media queries and container queries: parsed into an AST, then evaluated
// against an environment.
//
// Evaluation is paid once, not per match. A rule carries an index into a table
// of conditions and the engine keeps a parallel vector of their truth, so
// matching tests one bool. Re-evaluating on a resize walks that table and
// reports whether anything flipped, which lets a page with no `@media` skip
// the cascade entirely when the window changes.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/style/css/boolean.hpp>
#include <ctbrowser/style/css/calc.hpp>
#include <ctbrowser/style/css/media_fwd.hpp>
#include <ctbrowser/style/css/value.hpp>

namespace ctbrowser::style::css {

/// Parses a media query list from an at-rule prelude, or from its text. Never
/// fails: a query it cannot read is marked malformed, which Media Queries 4 §3
/// defines as `not all`, so it never matches. That is the safe direction; the
/// alternative is applying rules the author meant to gate.
[[nodiscard]] std::vector<media_query> parse_media_query_list(
    const stylesheet & sheet, std::span<const component_value> prelude);
[[nodiscard]] std::vector<media_query> parse_media_query_list(std::string_view text);

/// Whether the list matches. An empty list matches: `@media { }` is `all`.
[[nodiscard]] bool evaluate(std::span<const media_query> queries, const media_environment & env);

/// Evaluates one `<media-condition>`, or one bare `<media-feature>` without its
/// parentheses as `if(media(max-width: 1px): ...)` writes it. nullopt for text
/// that is not a condition; a condition this engine cannot decide is false, as
/// at the top of any `@media`.
[[nodiscard]] std::optional<bool> evaluate_media_condition(std::string_view text,
                                                           const media_environment & env);

/// The query container's content box, as the size features read it. An axis
/// the container does not contain has no size (an `inline-size` container has
/// no block size), and every feature reading it is unknown rather than false.
struct container_box {
    std::optional<float> width;
    std::optional<float> height;
    /// The directions it can still scroll, for `scroll-state(scrollable: ...)`:
    /// a mask of the scroll_* bits, all clear for a box that is not a scroll
    /// container or is scrolled as far as it goes every way.
    std::uint8_t scrollable = 0;
    /// True for a vertical writing mode on the container: its inline size is
    /// its height, and `inline-size`/`block-size` read the other axis.
    bool vertical = false;
    /// The container's font, for the relative units in the query: `(width:
    /// 10em)` is ten of the container's ems (CSS Conditional 5 §3.2). Its
    /// viewport fields are ignored; the window's come from `env`.
    length_context lengths;
};
inline constexpr std::uint8_t scroll_top = 1, scroll_right = 2, scroll_bottom = 4, scroll_left = 8;

/// Evaluates a `<container-condition>` (CSS Conditional 5 §3): size features
/// against `box`, each `style(...)` query answered by `query` against the
/// container's computed style, and viewport units such as `50vw` against
/// `env`. nullopt for text that is not a condition.
using style_query = std::function<truth(std::string_view query)>;
[[nodiscard]] std::optional<truth> evaluate_container_condition(std::string_view text,
                                                                const media_environment & env,
                                                                const container_box & box,
                                                                const style_query & query);

/// What a container must be to answer a `<container-condition>`, which decides
/// the query container (CSS Conditional 5 §2.2): `width` and `inline-size`
/// need a size container on the inline axis, `height` and `block-size` one on
/// the block axis, `aspect-ratio` and `orientation` both; `scroll-state()`
/// needs a scroll-state container. `style()` needs nothing, since every
/// element is a style container.
///
/// A feature no container has (`(foo: bar)`, `foo(bar)`, `style(unknown)`)
/// sets `unknown`; then no container is eligible and the query is false
/// whatever the rest of it would answer.
struct container_needs {
    bool inline_axis = false;
    bool block_axis = false;
    bool scroll_state = false;
    bool unknown = false;
};
[[nodiscard]] container_needs container_requirements(std::string_view condition);

/// Parses an `@container` prelude, `<container-condition>#` (CSS Conditional 5
/// §3), or returns nullopt when it is not one. Each condition is
/// `[<container-name>]? <container-query>?` with at least one part. A leading
/// identifier other than `not`/`and`/`or` is the name (a <custom-ident>, so no
/// CSS-wide keyword, `default` or `none`), and the rest must be a query: `not`
/// one parenthesised query, or several joined by all-`and` or all-`or`, each a
/// size feature, a nested query, a function (`style()`, `scroll-state()`) or a
/// `<general-enclosed>`. `screen`, `foo (width)` and `(width),` are invalid.
[[nodiscard]] std::optional<std::vector<container_query>> parse_container_conditions(
    std::string_view text);

/// Serialises container conditions as CSSOM's `conditionText` reads them: the
/// name as an identifier, the query with its whitespace collapsed, a size
/// feature's name lowercased, `: ` after a feature and one space either side
/// of a comparison, and the conditions joined by `, `. A function (`style()`,
/// `calc()`) is copied as written, its whitespace collapsed.
[[nodiscard]] std::string serialize_container_query(std::string_view condition);
[[nodiscard]] std::string serialize_container_conditions(std::span<const container_query> list);

} // namespace ctbrowser::style::css
