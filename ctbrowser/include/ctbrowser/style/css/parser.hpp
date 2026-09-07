#pragma once
#include <cstdint>
#include <string_view>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/style/css/value.hpp>

// CSS Syntax Level 3 §5: the grammar, on top of the §4 tokenizer.
//
// LENIENT, like the thing it replaces, but lenient in the spec's way rather than
// by accident. §5 says exactly what to do with input it cannot understand -
// consume a component value, skip to the next `;` or `}`, drop the invalid rule -
// and every one of those recoveries keeps the REST of the sheet. There is no error
// channel here for the same reason there is none in the tokenizer: a stylesheet
// with a mistake in it is not a broken stylesheet, and a browser that refused one
// would be alone in doing so.
//
// The at-rules and the selector subset are deliberately what the previous front
// end supported and no more - this rung is a pure substitution, so that a
// difference in the rendered page can only be a regression. Widening the selector
// grammar, evaluating `@media` conditions and modelling values are the rungs
// after it; see docs/plans/bootstrap.md.

namespace ctbrowser::style::css {

// A whole stylesheet. `atoms` interns property names and selector parts, so the
// result is ready for the cascade with no further conversion.
[[nodiscard]] stylesheet parse_stylesheet(std::string_view css, atom_table & atoms);

// A `style="..."` attribute: a declaration list with no selector and no braces.
//
// Its own entry point because the old path wrapped the text in `*{...}` and ran
// the SHEET parser over it - which worked, but only because the alternative it was
// avoiding (ctcss's own declaration splitter) peeled `!important` off and threw
// the flag away. That flag is the entire question of what a style attribute beats,
// which docs/style-layout.md spells out. Here the flag survives either way, so the
// wrap is gone and a `}` inside an attribute value can no longer end the dummy
// rule early.
[[nodiscard]] stylesheet parse_declaration_list(std::string_view css, atom_table & atoms);

// A STANDALONE SELECTOR LIST - `querySelector`'s argument, and `matches`'s.
//
// Its own entry point for the same reason a style attribute has one: the sheet
// parser wants a `{`, and wrapping the text in `x{}` to get one would let a `{` or
// a `}` in an attribute value change what was parsed. The returned sheet holds one
// `compiled_selector` per comma-separated alternative in `selectors`, and owns the
// pool every view in them points into - so it must outlive the matching.
//
// `invalid` reports a SYNTAX error, which is a different thing from a selector this
// engine cannot match; `parse_selector_list`'s declaration says which is which and
// why `querySelector` must tell them apart.
[[nodiscard]] stylesheet parse_selector_text(std::string_view text, atom_table & atoms,
                                             bool & invalid);

} // namespace ctbrowser::style::css
