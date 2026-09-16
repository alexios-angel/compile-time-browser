#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/style/css/value.hpp>

// CSS Syntax Level 3 §5: the grammar, on top of the §4 tokenizer.
//
// LENIENT, in the spec's way. §5 says exactly what to do with input it cannot
// understand - consume a component value, skip to the next `;` or `}`, drop the
// invalid rule - and every one of those recoveries keeps the REST of the sheet.
// There is no error channel here for the same reason there is none in the
// tokenizer: a stylesheet with a mistake in it is not a broken stylesheet, and a
// browser that refused one would be alone in doing so.

namespace ctbrowser::style::css {

// A whole stylesheet. `atoms` interns property names and selector parts, so the
// result is ready for the cascade with no further conversion.
[[nodiscard]] stylesheet parse_stylesheet(std::string_view css, atom_table & atoms);

// A `style="..."` attribute: a declaration list with no selector and no braces.
//
// Its own entry point rather than wrapping the text in `*{...}` for the sheet
// parser: `!important` survives (docs/style-layout.md says what a style attribute
// beats), and a `}` inside an attribute value cannot end the dummy rule early.
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
//
// `namespaces` is the `@namespace` rules in force - a CSSOM sheet's, or an EMPTY
// list for `querySelector`, which has no namespace resolver and so throws on
// `ns|div`. Null means the caller has none to offer and a prefix is taken on
// trust rather than refused.
[[nodiscard]] stylesheet parse_selector_text(
    std::string_view text, atom_table & atoms, bool & invalid,
    const std::vector<namespace_declaration> * namespaces = nullptr);

} // namespace ctbrowser::style::css
