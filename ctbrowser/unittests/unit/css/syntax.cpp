//===- ctbrowser/unittests/unit/css/syntax.cpp - CSS tokenizer tests ------===//
//
// Tests for the CSS Syntax Level 3 tokenizer. Most cases come from the spec's
// tokenization rules (CSS Syntax 3 §4.3; bare § numbers below are that spec);
// cases labelled "Regression" cover inputs that the earlier ctcss-based parser
// mishandled because it had no tokenizer: it stripped comments over the whole
// input, then scanned for `{`, `}`, `;` and the first `:`, so a `;`, `}` or
// `/*` inside a string broke it. Bootstrap happens to avoid all of these, so a
// suite built from Bootstrap alone would not notice.
//
// Every input goes through test/support/css_inputs.hpp and is listed in
// syntax.inputs.json. This build tokenizes and parses at run time; the native
// build (css_syntax.native) checks the same expectations against what
// ctcss-translate compiled from that list, with no CSS parsed at run time.
//
//===----------------------------------------------------------------------===//

#include "check.hpp"
#include "css_inputs.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using ctbrowser::style::css::flag_id_hash;
using ctbrowser::style::css::flag_integer;
using ctbrowser::style::css::token_stream;
using ctbrowser::style::css::token_type;
using ctbrowser_test::css_declarations;
using ctbrowser_test::css_sheet;
using ctbrowser_test::css_sheet_file;
using ctbrowser_test::css_supports_selector;
using ctbrowser_test::css_tokens;

namespace {

[[nodiscard]] const char * name_of(token_type t) {
    switch (t) {
    case token_type::eof: return "eof";
    case token_type::whitespace: return "ws";
    case token_type::ident: return "ident";
    case token_type::function: return "function";
    case token_type::at_keyword: return "at";
    case token_type::hash: return "hash";
    case token_type::string: return "string";
    case token_type::bad_string: return "bad-string";
    case token_type::url: return "url";
    case token_type::bad_url: return "bad-url";
    case token_type::number: return "number";
    case token_type::percentage: return "percentage";
    case token_type::dimension: return "dimension";
    case token_type::delim: return "delim";
    case token_type::cdo: return "cdo";
    case token_type::cdc: return "cdc";
    case token_type::colon: return "colon";
    case token_type::semicolon: return "semicolon";
    case token_type::comma: return "comma";
    case token_type::open_square: return "[";
    case token_type::close_square: return "]";
    case token_type::open_paren: return "(";
    case token_type::close_paren: return ")";
    case token_type::open_curly: return "{";
    case token_type::close_curly: return "}";
    }
    return "?";
}

/// The token types, whitespace dropped, as one string, so an expectation reads
/// like the shape of the input rather than like a list of assertions.
[[nodiscard]] std::string shape(const token_stream & s) {
    std::string out;
    for (const auto & t : s.tokens) {
        if (t.type == token_type::whitespace || t.type == token_type::eof) { continue; }
        if (!out.empty()) { out += ' '; }
        out += name_of(t.type);
    }
    return out;
}

/// Every non-whitespace token's text, pipe-separated. This catches a token
/// whose type is right and whose extent is wrong.
[[nodiscard]] std::string texts(const token_stream & s) {
    std::string out;
    for (const auto & t : s.tokens) {
        if (t.type == token_type::whitespace || t.type == token_type::eof) { continue; }
        if (!out.empty()) { out += '|'; }
        out += std::string{s.text_of(t)};
    }
    return out;
}

void expect_shape(std::string_view css, std::string_view want) {
    const token_stream got = css_tokens(css);
    if (shape(got) != want) {
        std::printf("FAIL shape of [%s]\n  got  %s\n  want %s\n", std::string{css}.c_str(),
                    shape(got).c_str(), std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}

void expect_texts(std::string_view css, std::string_view want) {
    const token_stream got = css_tokens(css);
    if (texts(got) != want) {
        std::printf("FAIL texts of [%s]\n  got  %s\n  want %s\n", std::string{css}.c_str(),
                    texts(got).c_str(), std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}

void expect_number(std::string_view css, double want, bool integer) {
    const token_stream got = css_tokens(css);
    const auto & t = got.tokens.front();
    const bool ok = t.number > want - 1e-9 && t.number < want + 1e-9 &&
                    ((t.flags & flag_integer) != 0) == integer;
    if (!ok) {
        std::printf("FAIL number of [%s]: value %g (want %g), integer %d (want %d)\n",
                    std::string{css}.c_str(), t.number, want,
                    static_cast<int>((t.flags & flag_integer) != 0), static_cast<int>(integer));
        ++ctbrowser_test_failures;
    }
}

// --- the shapes -------------------------------------------------------------

void test_the_basic_token_types() {
    expect_shape("div", "ident");
    expect_shape("#nav", "hash");
    expect_shape(".card", "delim ident");
    expect_shape("a b", "ident ident");
    expect_shape("a>b", "ident delim ident");
    expect_shape("a,b", "ident comma ident");
    expect_shape("a{b:c}", "ident { ident colon ident }");
    expect_shape("@media screen", "at ident");
    expect_shape("f(1)", "function number )");
    expect_shape("[type=x]", "[ ident delim ident ]");
    expect_shape("<!-- -->", "cdo cdc");
    expect_shape("", "");
}

void test_numbers_dimensions_and_percentages() {
    expect_shape("12px", "dimension");
    expect_shape("50%", "percentage");
    expect_shape("1.5", "number");
    expect_shape("-3", "number");
    expect_shape("+4", "number");
    expect_shape(".5em", "dimension");
    expect_shape("1e3", "number");
    expect_shape("2e-2", "number");
    // `12 px` is not a dimension, and a grammar that cannot tell it from `12px`
    // will accept nonsense.
    expect_shape("12 px", "number ident");
    // An `e` that is not an exponent must not be swallowed: `3em` is 3 + `em`.
    expect_shape("3em", "dimension");
    expect_texts("3em", "3em");

    expect_number("12px", 12, true);
    expect_number("1.5", 1.5, false);
    expect_number("-3", -3, true);
    expect_number("+4", 4, true);
    // A percentage carries a value but no type flag: §4 defines one for
    // number-token and dimension-token only, and nothing needs a percentage's
    // integer-ness.
    expect_number("50%", 50, false);
    expect_number("1e3", 1000, false);
    expect_number(".5", 0.5, false);

    const token_stream d = css_tokens("12.5px");
    if (d.unit_of(d.tokens.front()) != "px") {
        std::printf("FAIL unit of 12.5px: [%s]\n",
                    std::string{d.unit_of(d.tokens.front())}.c_str());
        ++ctbrowser_test_failures;
    }
}

void test_hash_is_id_like_when_its_body_could_be_an_identifier() {
    // The id-like flag says whether the hash body could be an identifier, not
    // whether it is a colour: `#fff` is id-like, and `#fff { }` selects id="fff".
    const token_stream id = css_tokens("#nav");
    CHECK((id.tokens.front().flags & flag_id_hash) != 0);
    const token_stream three = css_tokens("#fff");
    CHECK((three.tokens.front().flags & flag_id_hash) != 0);
    const token_stream letters_first = css_tokens("#f0a1b2");
    CHECK((letters_first.tokens.front().flags & flag_id_hash) != 0);
    // A digit cannot start an identifier, so this one is not id-like, which is
    // what lets a selector parser reject `#0d6efd` as an id.
    const token_stream digit_first = css_tokens("#0d6efd");
    CHECK((digit_first.tokens.front().flags & flag_id_hash) == 0);
    const token_stream nine = css_tokens("#999");
    CHECK((nine.tokens.front().flags & flag_id_hash) == 0);
    // A leading `-` is allowed in an identifier, so `#-a` is id-like.
    const token_stream dash = css_tokens("#-a");
    CHECK((dash.tokens.front().flags & flag_id_hash) != 0);
    // A bare `#` is a delim, not an empty hash.
    expect_shape("# ", "delim");
}

// --- regressions from the tokenizer-less parser ------------------------------

void test_a_semicolon_inside_a_string_is_not_a_declaration_end() {
    // Regression: a `;` inside a string must not end the declaration (the old
    // parser cut it in half and made the tail a garbage property).
    expect_shape("content:\"a;b\"", "ident colon string");
    expect_texts("content:\"a;b\"", "content|:|\"a;b\"");
}

void test_a_brace_inside_a_string_does_not_desynchronise() {
    // Regression: a `}` inside a string must not close the block (the old brace
    // scanner counted it and mis-parsed every later rule in the file).
    expect_shape("a{content:\"}\"}", "ident { ident colon string }");
    const token_stream s = css_tokens("a{content:\"}\"} b{c:d}");
    CHECK(shape(s) == "ident { ident colon string } ident { ident colon ident }");
}

void test_a_comment_opener_inside_a_string_is_just_text() {
    // Regression: a `/*` inside a string must not open a comment (the old
    // comment-stripping pre-pass ate the rest of the file to the next `*/`).
    expect_shape("content:\"/*\"", "ident colon string");
    expect_texts("a:\"/*\";b:c", "a|:|\"/*\"|;|b|:|c");
}

void test_a_semicolon_inside_url_is_not_a_declaration_end() {
    // Regression: an un-encoded data URI must keep its `;` (Bootstrap's are
    // percent-encoded, which hid the bug).
    expect_shape("background:url(data:image/svg+xml;base64,AAA)", "ident colon url");
    const token_stream s = css_tokens("background:url(data:image/png;base64,iVBOR)");
    CHECK(std::string{s.value_of(s.tokens.back() /*eof*/)}.empty());
    for (const auto & t : s.tokens) {
        if (t.type == token_type::url) { CHECK(s.text_of(t) == "data:image/png;base64,iVBOR"); }
    }
}

void test_a_comment_is_a_token_boundary() {
    // Regression: a comment separates two idents (stripping comments up front
    // joined them into one).
    expect_shape("a/**/b", "ident ident");
    expect_texts("a/**/b", "a|b");
    expect_shape("/* leading */a", "ident");
    expect_shape("a/* unterminated", "ident");
}

void test_a_bom_does_not_become_part_of_the_first_selector() {
    // Regression: a leading BOM is dropped (the old parser treated its three
    // bytes as name code points, so the first rule's selector was
    // `\xEF\xBB\xBFbody` and silently matched nothing).
    const std::string with_bom = std::string{"\xEF\xBB\xBF"} + "body{a:b}";
    expect_texts(with_bom, "body|{|a|:|b|}");
}

// --- strings, urls and escapes ---------------------------------------------

void test_strings_and_their_failures() {
    expect_shape("\"abc\"", "string");
    expect_shape("'abc'", "string");
    // A newline inside a string is a bad-string, and the newline is left for
    // whatever follows; that is what makes the rest of the sheet parse.
    expect_shape("\"ab\ncd\"", "bad-string ident string");
    // EOF inside a string is a parse error but still a normal string, per §4.3.4.
    expect_shape("\"abc", "string");
    const token_stream s = css_tokens("\"a\\\"b\"");
    CHECK(s.tokens.front().type == token_type::string);
    CHECK(s.value_of(s.tokens.front()) == "a\"b");
}

void test_urls_quoted_unquoted_and_bad() {
    // Unquoted is its own token; quoted is a function plus a string, per §4.3.4.
    expect_shape("url(a.png)", "url");
    expect_shape("url(\"a.png\")", "function string )");
    expect_shape("url('a.png')", "function string )");
    // Whitespace is allowed around an unquoted body but not inside it.
    expect_shape("url(  a.png  )", "url");
    expect_shape("url(a b.png)", "bad-url");
    // A bad url consumes to the `)` so the next rule survives.
    const token_stream s = css_tokens("a{b:url(x y)}c{d:e}");
    CHECK(shape(s) == "ident { ident colon bad-url } ident { ident colon ident }");
    const token_stream body = css_tokens("url(a.png)");
    CHECK(body.text_of(body.tokens.front()) == "a.png");
    for (const std::string_view text :
         {R"(url(a\2e png))", R"(url(a\00002epng))", R"(url(a\.png))"}) {
        const token_stream escaped = css_tokens(text);
        CHECK(shape(escaped) == "url");
        CHECK(escaped.text_of(escaped.tokens.front()) == "a.png");
    }
    expect_shape(R"(url(a\2e  png))", "bad-url");
}

void test_escapes_decode_into_the_token() {
    // `\26` is `&`. One trailing space belongs to the escape, so this is one
    // ident `&B` and not `&` followed by `B`.
    const token_stream amp = css_tokens("\\26 B");
    CHECK(amp.tokens.front().type == token_type::ident);
    CHECK(amp.text_of(amp.tokens.front()) == "&B");
    // Regression: an escaped colon in a class name, as a framework writes
    // `sm:block` (the old parser split on `:` and made it an unknown pseudo).
    const token_stream cls = css_tokens(".foo\\:bar");
    CHECK(shape(cls) == "delim ident");
    for (const auto & t : cls.tokens) {
        if (t.type == token_type::ident) { CHECK(cls.text_of(t) == "foo:bar"); }
    }
    // An escaped sigil keeps the sigil: `#a\:b` is one hash token `#a:b`.
    const token_stream h = css_tokens("#a\\:b");
    CHECK(h.tokens.front().type == token_type::hash);
    CHECK(h.text_of(h.tokens.front()) == "#a:b");
    // A hex escape with no trailing space still ends at a non-hex character.
    const token_stream two = css_tokens("\\41\\42");
    CHECK(two.text_of(two.tokens.front()) == "AB");
    // Zero and out-of-range become U+FFFD rather than a null byte in the pool.
    const token_stream zero = css_tokens("\\0");
    CHECK(zero.text_of(zero.tokens.front()) == "\xEF\xBF\xBD");
}

void test_at_keywords_and_the_charset_line() {
    expect_shape("@media", "at");
    expect_shape("@import url(a.css);", "at url semicolon");
    // Bootstrap's line 1. It must be one at-keyword, a string and a semicolon,
    // not something that swallows the rule after it.
    expect_texts("@charset \"UTF-8\";", "@charset|\"UTF-8\"|;");
    // `@` followed by something that cannot start an identifier is a delim.
    expect_shape("@ media", "delim ident");
    expect_shape("@1x", "delim dimension");
    const token_stream s = css_tokens("@media");
    CHECK(s.value_of(s.tokens.front()) == "media");
}

void test_important_and_the_delims_around_it() {
    expect_shape("color:red!important", "ident colon ident delim ident");
    expect_shape("color:red ! important", "ident colon ident delim ident");
    // A `!` in the middle of a value is a delim too; deciding what it means
    // (such as `!important`) is the parser's job, not the tokenizer's.
    expect_shape("a:b!c", "ident colon ident delim ident");
}

void test_unbalanced_input_terminates() {
    // None of these may hang or read out of bounds; the shapes are secondary.
    for (const std::string_view css : {"a{", "a{b:c", "}", "((((", "url(", "\"", "\\", "/*",
                                       "a{b:url(", "@", "#", "-", "+", ".", "<!--"}) {
        const token_stream s = css_tokens(css);
        CHECK(!s.tokens.empty());
        CHECK(s.tokens.back().type == token_type::eof);
    }
}

void test_preprocessing_folds_line_endings() {
    // CRLF, CR and FF all become one LF, so a token's extent does not depend on
    // which editor wrote the file.
    for (const std::string_view css : {"a\r\nb", "a\rb", "a\fb"}) {
        const token_stream s = css_tokens(css);
        CHECK(shape(s) == "ident ident");
    }
    const token_stream crlf = css_tokens("a\r\nb");
    CHECK(crlf.pool.find('\r') == std::string::npos);
}

void test_a_real_bootstrap_shaped_rule() {
    const std::string_view css = ".btn{--bs-btn-padding-x:0.75rem;color:var(--bs-btn-color);"
                                 "border:var(--bs-btn-border-width) solid transparent}";
    const token_stream s = css_tokens(css);
    CHECK(s.tokens.back().type == token_type::eof);
    // The custom property is one ident: `--bs-btn-padding-x`. A tokenizer that
    // treated `-` as a delim would make it five tokens and nothing downstream
    // could put it back together.
    bool found = false;
    for (const auto & t : s.tokens) {
        if (t.type == token_type::ident && s.text_of(t) == "--bs-btn-padding-x") { found = true; }
    }
    CHECK(found);
    // `var(` is a function, not an ident followed by a paren.
    int functions = 0;
    for (const auto & t : s.tokens) {
        if (t.type == token_type::function) { ++functions; }
    }
    CHECK(functions == 2);
}

/// A declaration's stored text is the source slice, and where an escape forced
/// a rebuild it is written back as CSS: `\33 myident` decodes to `3myident`,
/// which would read as a dimension the second time round
/// (WPT ident-function-computed).
void test_an_escaped_identifier_is_stored_as_one() {
    ctbrowser::atom_table atoms;
    const auto sheet = css_declarations("view-transition-name: \\33 myident", atoms);
    CHECK(sheet.declarations.size() == 1);
    if (sheet.declarations.size() == 1) {
        CHECK(sheet.text_of(sheet.declarations.front()) == "\\33 myident");
    }
}

// --- Bootstrap selector census -----------------------------------------------

/// Counts how many of Bootstrap's selectors parse into something the matcher
/// can consider, and fails if the count drops below a floor. The Chrome parity
/// harness compares only 48 layout/paint properties, so selector-parsing
/// improvements (which mostly unlock custom properties) do not show up there.
/// The floor may only be raised; a change that widens the selector grammar
/// should raise it.
void test_how_much_of_bootstrap_can_match() {
    ctbrowser::atom_table atoms;
    const auto parsed = css_sheet_file("vendor/bootstrap/bootstrap.css", atoms);
    if (!parsed) {
        std::printf("SKIP bootstrap census: vendor/bootstrap/bootstrap.css not readable\n");
        return;
    }
    const auto & sheet = *parsed;

    std::size_t live = 0;
    for (const auto & sel : sheet.selectors) {
        if (!sel.parts.empty() && !sel.parts.front().never_matches) { ++live; }
    }
    const std::size_t total = sheet.selectors.size();
    std::printf("  bootstrap: %zu/%zu selectors can match (%.1f%%), %zu rules, %zu declarations\n",
                live, total, 100.0 * static_cast<double>(live) / static_cast<double>(total),
                sheet.rules.size(), sheet.declarations.size());

    // Minimum matchable selectors. Raise it when the selector grammar widens;
    // never lower it. 2778 covers type/id/class, descendant and child, attribute
    // selectors, :root, sibling combinators, :not/:is/:where, structural pseudos
    // and the rules inside @media print (parsed and kept, but inactive on
    // screen). A selector that parses may still never match (a state pseudo
    // nothing sets), so this measures parsing, not correctness.
    constexpr std::size_t floor = 2778;
    if (live < floor) {
        std::printf("FAIL bootstrap census: %zu selectors can match, was at least %zu\n", live,
                    floor);
        ++ctbrowser_test_failures;
    }
    // The rule and selector totals are pinned too, so a parser change that drops
    // rules cannot hide behind a selector count that still meets the floor. The
    // totals include the @media print rules and exclude the one rule whose
    // selector list does not parse (`.form-select:-moz-focusring`, a
    // Firefox-only pseudo-class), which is dropped whole as Selectors 4 §3.1
    // requires, as Chrome also does.
    CHECK_EQ(sheet.rules.size(), std::size_t{2549});
    CHECK_EQ(total, std::size_t{2960});
}

/// Nesting is capped, and past the cap the declaration or selector holding the
/// nesting is invalid: never a stack overflow, never a partial parse. Without
/// the cap, thirty thousand `(` would recurse once per level through
/// consume_block.
void test_deep_nesting_invalidates_only_what_holds_it() {
    const auto nest = [](std::string_view open, std::string_view inner, std::string_view close,
                         int levels) {
        std::string out;
        for (int i = 0; i < levels; ++i) { out += open; }
        out += inner;
        for (int i = 0; i < levels; ++i) { out += close; }
        return out;
    };
    {
        ctbrowser::atom_table atoms;
        const std::string css = "a { color: " + nest("(", "", ")", 30000) +
                                "; width: 1px } b { --kept: " + nest("(", "", ")", 100) +
                                " } c { color: red }";
        const auto sheet = css_sheet(css, atoms);
        std::string names;
        for (const auto & d : sheet.declarations) {
            names += std::string{atoms.text(d.property)} + ",";
        }
        CHECK_EQ(names, std::string{"width,--kept,color,"});
    }
    {
        ctbrowser::atom_table atoms;
        const auto list =
            css_declarations("color: " + nest("f(", "", ")", 30000) + "; width: 1px", atoms);
        CHECK_EQ(list.declarations.size(), std::size_t{1});
    }
    {
        // Rules thirty thousand deep: the too-deep one styles nothing, the
        // rule after it is untouched.
        ctbrowser::atom_table atoms;
        const auto sheet =
            css_sheet(nest("a{", "color:red", "}", 30000) + " b { color: blue }", atoms);
        CHECK(!sheet.declarations.empty());
        CHECK_EQ(std::string{atoms.text(sheet.declarations.back().property)}, std::string{"color"});
    }
    // Functional pseudo-classes share one count: :not/:is/:where nested past
    // it are a syntax error, and a selector the component-value cap cut short
    // is too.
    CHECK(css_supports_selector(nest(":not(", "a", ")", 10)));
    CHECK(css_supports_selector(nest(":is(:where(", "a", "))", 8)));
    CHECK(!css_supports_selector(nest(":not(", "a", ")", 40)));
    CHECK(!css_supports_selector(nest(":is(:where(", "a", "))", 20)));
    CHECK(!css_supports_selector(nest(":not(", "a", ")", 20000)));
}

} // namespace

int main() {
    test_the_basic_token_types();
    test_numbers_dimensions_and_percentages();
    test_hash_is_id_like_when_its_body_could_be_an_identifier();
    test_a_semicolon_inside_a_string_is_not_a_declaration_end();
    test_a_brace_inside_a_string_does_not_desynchronise();
    test_a_comment_opener_inside_a_string_is_just_text();
    test_a_semicolon_inside_url_is_not_a_declaration_end();
    test_a_comment_is_a_token_boundary();
    test_a_bom_does_not_become_part_of_the_first_selector();
    test_strings_and_their_failures();
    test_urls_quoted_unquoted_and_bad();
    test_escapes_decode_into_the_token();
    test_at_keywords_and_the_charset_line();
    test_important_and_the_delims_around_it();
    test_unbalanced_input_terminates();
    test_preprocessing_folds_line_endings();
    test_a_real_bootstrap_shaped_rule();
    test_an_escaped_identifier_is_stored_as_one();
    test_how_much_of_bootstrap_can_match();
    test_deep_nesting_invalidates_only_what_holds_it();
    // Every input above is listed for the native build, which compiles them
    // ahead of time rather than parsing them here.
    ctbrowser_test::check_css_inputs("unittests/unit/css/syntax.inputs.json");
    REPORT("css_syntax");
}
