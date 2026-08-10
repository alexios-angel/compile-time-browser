#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/style/css/media_fwd.hpp>
#include <ctbrowser/style/css/value.hpp>

// Media queries: parsed into an AST, then evaluated against an environment.
//
// WHY THIS IS WORTH ITS OWN FILE. The front end this replaced substring-matched the
// prelude for `portrait` and `print` and flattened every other block in
// unconditionally, so all of Bootstrap's breakpoints applied at once and the last in
// source order won. That is not a small inaccuracy: `.container` took the xxl
// breakpoint's `max-width: 1320px` at every viewport, which does not clamp a 1009px
// page, so nothing clamped, nothing centred, and every child was measured against the
// wrong basis. One missing feature accounted for the largest single cluster of
// differences from Chrome.
//
// THE COST OF EVALUATION IS PAID ONCE, NOT PER MATCH. A rule carries an index into a
// table of conditions and the engine keeps a parallel vector of their truth; matching
// tests one bool. Re-evaluating on a resize is a walk of that table, and it reports
// whether anything actually FLIPPED - which is what lets a page with no `@media` skip
// the cascade entirely when the window changes.

namespace ctbrowser::style::css {

// Parse a media query list from an at-rule prelude. Never fails: a query it cannot
// read is marked malformed, which per §3 means `not all` - it never matches. That is
// the safe direction, because the alternative is applying rules the author gated.
[[nodiscard]] std::vector<media_query> parse_media_query_list(
    const stylesheet & sheet, std::span<const component_value> prelude);

// Does this list match? An empty list matches - `@media { }` is `all`.
[[nodiscard]] bool evaluate(std::span<const media_query> queries, const media_environment & env);

} // namespace ctbrowser::style::css
