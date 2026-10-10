//===- parser.hpp - CSS parser ----------------------------------*- C++ -*-===//
//
// CSS Syntax Level 3 §5: the grammar, on top of the §4 tokenizer.
//
// Lenient, as the spec requires. §5 says exactly what to do with input it
// cannot understand (consume a component value, skip to the next `;` or `}`,
// drop the invalid rule), and every recovery keeps the rest of the sheet.
// There is no error channel, as in the tokenizer: a stylesheet with a mistake
// in it is not a broken stylesheet, and no browser refuses one.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/style/css/value.hpp>

namespace ctbrowser::style::css {

/// Parses a whole stylesheet. `atoms` interns property names and selector
/// parts, so the result is ready for the cascade with no further conversion.
[[nodiscard]] stylesheet parse_stylesheet(std::string_view css, atom_table & atoms);

/// Decodes a fetched stylesheet's bytes to text (CSS Syntax 3 §3.2). A byte
/// order mark wins; otherwise the fallback encoding is the protocol's label,
/// else an `@charset "...";` the bytes begin with (a UTF-16 one meaning
/// UTF-8), else the referring document's `environment` encoding, else UTF-8.
/// A `<link charset>` plays no part.
[[nodiscard]] std::string decode_stylesheet(std::string_view bytes, std::string_view protocol,
                                            std::string_view environment);

/// The `@scope` prelude that `@import ... scope(<argument>)` stands for (CSS
/// Cascade 6 §2.2). The argument is `<scope-start> | <scope-boundaries>`, so a
/// bare start selector gets parenthesised (`scope(.a)` is `@scope (.a)`),
/// while `(.a) to (.b)` and `to (.b)` are already boundaries, and an empty
/// argument is the implicit scope. It is read as tokens, so `scope(toolbar)`
/// names an element rather than starting with `to`.
[[nodiscard]] std::string import_scope_prelude(std::string_view argument);

/// Parses a `style="..."` attribute: a declaration list with no selector and no
/// braces.
///
/// It has its own entry point rather than wrapping the text in `*{...}` for
/// the sheet parser, so `!important` survives (docs/style-layout.md says what
/// a style attribute beats) and a `}` inside the attribute cannot end the
/// dummy rule early.
[[nodiscard]] stylesheet parse_declaration_list(std::string_view css, atom_table & atoms);

/// Parses a standalone selector list, such as the argument of `querySelector`
/// or `matches`.
///
/// It has its own entry point for the same reason a style attribute does:
/// wrapping the text in `x{}` for the sheet parser would let a `{` or `}` in
/// an attribute value change what was parsed. The returned sheet holds one
/// `compiled_selector` per comma-separated alternative in `selectors`, and owns
/// the pool every view in them points into, so it must outlive the matching.
///
/// `invalid` reports a syntax error, which differs from a selector this engine
/// cannot match; parse_selector_list() (css/selector.hpp) explains the
/// difference and why `querySelector` must tell them apart.
///
/// `namespaces` is the `@namespace` rules in force: a CSSOM sheet's, or an
/// empty list for `querySelector`, which has no namespace resolver and so
/// throws on `ns|div`. Null means the caller has none to offer, and a prefix
/// is accepted on trust rather than refused.
///
/// `nesting` is the context of a nested style rule (css/selector.hpp): with
/// one, `&` and a leading combinator are anchored on it, and a selector naming
/// no `&` is prefixed with one, as the sheet parser does for a nested rule.
struct nesting_context;
[[nodiscard]] stylesheet parse_selector_text(
    std::string_view text, atom_table & atoms, bool & invalid,
    const std::vector<namespace_declaration> * namespaces = nullptr,
    const nesting_context * nesting = nullptr);

/// `selector(<complex-selector>)` in @supports and CSS.supports() (CSS
/// Conditional 4 §6.2): true for exactly one complex selector that parses,
/// read unforgivingly. `:is(:foo)`, `:where()` and `:has(:is(:has(a)))` fail,
/// where a style rule would merely drop the bad argument.
[[nodiscard]] bool supports_selector(std::string_view text);

} // namespace ctbrowser::style::css
