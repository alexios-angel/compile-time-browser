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

// Media queries: parsed into an AST, then evaluated against an environment.
//
// THE COST OF EVALUATION IS PAID ONCE, NOT PER MATCH. A rule carries an index into a
// table of conditions and the engine keeps a parallel vector of their truth; matching
// tests one bool. Re-evaluating on a resize is a walk of that table, and it reports
// whether anything actually FLIPPED - which is what lets a page with no `@media` skip
// the cascade entirely when the window changes.

namespace ctbrowser::style::css {

// Parse a media query list from an at-rule prelude, or from its text. Never
// fails: a query it cannot read is marked malformed, which per §3 means `not
// all` - it never matches. That is the safe direction, because the alternative
// is applying rules the author gated.
[[nodiscard]] std::vector<media_query> parse_media_query_list(
    const stylesheet & sheet, std::span<const component_value> prelude);
[[nodiscard]] std::vector<media_query> parse_media_query_list(std::string_view text);

// Does this list match? An empty list matches - `@media { }` is `all`.
[[nodiscard]] bool evaluate(std::span<const media_query> queries, const media_environment & env);

// ONE `<media-condition>` - or one bare `<media-feature>` without its
// parentheses, which is what `if(media(max-width: 1px): ...)` writes - against
// the environment. nullopt for text that is not a condition at all; a
// condition this engine cannot decide is false, as at the top of any `@media`.
[[nodiscard]] std::optional<bool> evaluate_media_condition(std::string_view text,
                                                           const media_environment & env);

// The query container's CONTENT BOX, as the size features read it. An axis
// the container does not contain has no size - an `inline-size` container
// has no block size to ask - and every feature reading it is unknown rather
// than false.
struct container_box {
    std::optional<float> width;
    std::optional<float> height;
    // WHERE IT CAN STILL SCROLL, for `scroll-state(scrollable: ...)`: the
    // scroll_* bits, all clear for a box that is no scroll container or is
    // scrolled as far as it goes every way.
    std::uint8_t scrollable = 0;
    // The CONTAINER'S font, for the relative units in the query: `(width:
    // 10em)` is ten of the container's ems (CSS Conditional 5 §3.2). The
    // viewport in it is ignored - the window's is `env`'s.
    length_context lengths;
};
inline constexpr std::uint8_t scroll_top = 1, scroll_right = 2, scroll_bottom = 4, scroll_left = 8;

// A `<container-condition>` (CSS Conditional 5 §3): the size features
// against `box`, each `style(...)`'s query text answered by `query` against
// the container's computed style, and `env` the window the units in it -
// `50vw` - are relative to. nullopt for text that is not a condition.
using style_query = std::function<truth(std::string_view query)>;
[[nodiscard]] std::optional<truth> evaluate_container_condition(std::string_view text,
                                                                const media_environment & env,
                                                                const container_box & box,
                                                                const style_query & query);

// WHAT A CONTAINER MUST BE to answer a `<container-condition>`, which is what
// picks the query container (CSS Conditional 5 §2.2): `width` and
// `inline-size` want a size container on the inline axis, `height` and
// `block-size` one on the block axis, `aspect-ratio` and `orientation` both;
// `scroll-state()` wants a scroll-state container. `style()` wants nothing -
// every element is a style container.
struct container_needs {
    bool inline_axis = false;
    bool block_axis = false;
    bool scroll_state = false;
};
[[nodiscard]] container_needs container_requirements(std::string_view condition);

// An `@container` PRELUDE, `<container-condition>#` (CSS Conditional 5 §3),
// or nullopt when it is not one. Each condition is `[<container-name>]?
// <container-query>?` and not neither: a leading identifier other than
// `not`/`and`/`or` is the name - a <custom-ident>, so no CSS-wide keyword,
// `default` or `none` - and the rest must be a query: `not` one in
// parentheses, or several joined by all-`and` or all-`or`, each a size
// feature, a nested query, a function (`style()`, `scroll-state()`) or a
// `<general-enclosed>`. `screen`, `foo (width)` and `(width),` are not.
[[nodiscard]] std::optional<std::vector<container_query>> parse_container_conditions(
    std::string_view text);

// ...and back, as CSSOM's `conditionText` reads it: the name as an
// identifier, the query with its whitespace collapsed, a size feature's name
// lowercased, `: ` after a feature and one space either side of a comparison,
// and the conditions joined by `, `. A function - `style()`, `calc()` - is
// copied as written, its whitespace collapsed.
[[nodiscard]] std::string serialize_container_query(std::string_view condition);
[[nodiscard]] std::string serialize_container_conditions(std::span<const container_query> list);

} // namespace ctbrowser::style::css
