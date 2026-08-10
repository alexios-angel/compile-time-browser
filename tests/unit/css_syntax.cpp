// The CSS tokenizer, against the cases the thing it replaced got wrong.
//
// The submodule this supersedes had no tokenizer: a whole-input comment strip,
// then a scan for `{`, `}`, `;` and the first `:`. Nothing knew that a quote opens
// a string. Bootstrap survived it by luck - percent-encoded data URIs, no braces
// in strings - so a test suite built from Bootstrap alone would not notice.
// Every case below is either a documented §4.3 behaviour or a specific way the
// old path produced a wrong answer, and the second kind is labelled.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using ctbrowser::style::css::flag_id_hash;
using ctbrowser::style::css::flag_integer;
using ctbrowser::style::css::token_stream;
using ctbrowser::style::css::token_type;
using ctbrowser::style::css::tokenize;

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

// The token types, whitespace dropped, as one string - so an expectation reads
// like the shape of the input rather than like a list of assertions.
[[nodiscard]] std::string shape(const token_stream & s) {
    std::string out;
    for (const auto & t : s.tokens) {
        if (t.type == token_type::whitespace || t.type == token_type::eof) { continue; }
        if (!out.empty()) { out += ' '; }
        out += name_of(t.type);
    }
    return out;
}

// Every non-whitespace token's TEXT, pipe-separated. This is what catches a
// token whose type is right and whose extent is wrong.
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
    const token_stream got = tokenize(css);
    if (shape(got) != want) {
        std::printf("FAIL shape of [%s]\n  got  %s\n  want %s\n", std::string{css}.c_str(),
                    shape(got).c_str(), std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}

void expect_texts(std::string_view css, std::string_view want) {
    const token_stream got = tokenize(css);
    if (texts(got) != want) {
        std::printf("FAIL texts of [%s]\n  got  %s\n  want %s\n", std::string{css}.c_str(),
                    texts(got).c_str(), std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}

void expect_number(std::string_view css, double want, bool integer) {
    const token_stream got = tokenize(css);
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
    // `12 px` is NOT a dimension, and a grammar that cannot tell it from `12px`
    // will accept nonsense.
    expect_shape("12 px", "number ident");
    // An `e` that is not an exponent must not be swallowed: `3em` is 3 + `em`.
    expect_shape("3em", "dimension");
    expect_texts("3em", "3em");

    expect_number("12px", 12, true);
    expect_number("1.5", 1.5, false);
    expect_number("-3", -3, true);
    expect_number("+4", 4, true);
    // A percentage carries a value but NO type flag - §4 defines one for
    // number-token and dimension-token only, and nothing needs a percentage's
    // integer-ness.
    expect_number("50%", 50, false);
    expect_number("1e3", 1000, false);
    expect_number(".5", 0.5, false);

    const token_stream d = tokenize("12.5px");
    if (d.unit_of(d.tokens.front()) != "px") {
        std::printf("FAIL unit of 12.5px: [%s]\n", std::string{d.unit_of(d.tokens.front())}.c_str());
        ++ctbrowser_test_failures;
    }
}

void test_hash_is_id_like_when_its_body_could_be_an_identifier() {
    // NOT "colour versus selector" - that was the first guess and it is wrong.
    // `#fff` is id-like, and `#fff { }` really does select id="fff". What the flag
    // answers is whether the body COULD be an identifier at all.
    const token_stream id = tokenize("#nav");
    CHECK((id.tokens.front().flags & flag_id_hash) != 0);
    const token_stream three = tokenize("#fff");
    CHECK((three.tokens.front().flags & flag_id_hash) != 0);
    const token_stream letters_first = tokenize("#f0a1b2");
    CHECK((letters_first.tokens.front().flags & flag_id_hash) != 0);
    // A DIGIT first cannot start an identifier, so this one is not id-like - which
    // is what lets a selector parser reject `#0d6efd` as an id.
    const token_stream digit_first = tokenize("#0d6efd");
    CHECK((digit_first.tokens.front().flags & flag_id_hash) == 0);
    const token_stream nine = tokenize("#999");
    CHECK((nine.tokens.front().flags & flag_id_hash) == 0);
    // A leading `-` is allowed in an identifier, so `#-a` is id-like.
    const token_stream dash = tokenize("#-a");
    CHECK((dash.tokens.front().flags & flag_id_hash) != 0);
    // A bare `#` is a delim, not an empty hash.
    expect_shape("# ", "delim");
}

// --- the ones the old path got wrong ---------------------------------------

void test_a_semicolon_inside_a_string_is_not_a_declaration_end() {
    // THE OLD FAILURE: `;` was structural everywhere, so this declaration was cut
    // in half and the tail became a garbage property.
    expect_shape("content:\"a;b\"", "ident colon string");
    expect_texts("content:\"a;b\"", "content|:|\"a;b\"");
}

void test_a_brace_inside_a_string_does_not_desynchronise() {
    // THE OLD FAILURE: the brace scanner counted this `}` and every rule after it
    // in the file was mis-parsed - a whole-sheet corruption from one string.
    expect_shape("a{content:\"}\"}", "ident { ident colon string }");
    const token_stream s = tokenize("a{content:\"}\"} b{c:d}");
    CHECK(shape(s) == "ident { ident colon string } ident { ident colon ident }");
}

void test_a_comment_opener_inside_a_string_is_just_text() {
    // THE OLD FAILURE: comments were stripped by a pre-pass with no idea what a
    // string was, so this ate the rest of the file to the next `*/`.
    expect_shape("content:\"/*\"", "ident colon string");
    expect_texts("a:\"/*\";b:c", "a|:|\"/*\"|;|b|:|c");
}

void test_a_semicolon_inside_url_is_not_a_declaration_end() {
    // Bootstrap's data URIs are percent-encoded, which is the only reason the old
    // path survived them. An un-encoded one cut the declaration at the `;`.
    expect_shape("background:url(data:image/svg+xml;base64,AAA)", "ident colon url");
    const token_stream s = tokenize("background:url(data:image/png;base64,iVBOR)");
    CHECK(std::string{s.value_of(s.tokens.back() /*eof*/)}.empty());
    for (const auto & t : s.tokens) {
        if (t.type == token_type::url) { CHECK(s.text_of(t) == "data:image/png;base64,iVBOR"); }
    }
}

void test_a_comment_is_a_token_boundary() {
    // Stripping comments up front joined the two idents into one.
    expect_shape("a/**/b", "ident ident");
    expect_texts("a/**/b", "a|b");
    expect_shape("/* leading */a", "ident");
    expect_shape("a/* unterminated", "ident");
}

void test_a_bom_does_not_become_part_of_the_first_selector() {
    // THE OLD FAILURE: the three BOM bytes were name code points as far as
    // anything knew, so the first rule's selector was `\xEF\xBB\xBFbody` and
    // silently matched nothing.
    const std::string with_bom = std::string{"\xEF\xBB\xBF"} + "body{a:b}";
    expect_texts(with_bom, "body|{|a|:|b|}");
}

// --- strings, urls and escapes ---------------------------------------------

void test_strings_and_their_failures() {
    expect_shape("\"abc\"", "string");
    expect_shape("'abc'", "string");
    // A NEWLINE inside a string is a bad-string, and the newline is left for
    // whatever follows - that is what makes the rest of the sheet parse.
    expect_shape("\"ab\ncd\"", "bad-string ident string");
    // EOF inside a string is a parse error but still a normal string, per §4.3.4.
    expect_shape("\"abc", "string");
    const token_stream s = tokenize("\"a\\\"b\"");
    CHECK(s.tokens.front().type == token_type::string);
    CHECK(s.value_of(s.tokens.front()) == "a\"b");
}

void test_urls_quoted_unquoted_and_bad() {
    // Unquoted is its own token; QUOTED is a function plus a string, per §4.3.4.
    expect_shape("url(a.png)", "url");
    expect_shape("url(\"a.png\")", "function string )");
    expect_shape("url('a.png')", "function string )");
    // Whitespace is allowed around an unquoted body but not INSIDE it.
    expect_shape("url(  a.png  )", "url");
    expect_shape("url(a b.png)", "bad-url");
    // A bad url consumes to the `)` so the next rule survives.
    const token_stream s = tokenize("a{b:url(x y)}c{d:e}");
    CHECK(shape(s) == "ident { ident colon bad-url } ident { ident colon ident }");
    const token_stream body = tokenize("url(a.png)");
    CHECK(body.text_of(body.tokens.front()) == "a.png");
}

void test_escapes_decode_into_the_token() {
    // `\26` is `&`. One trailing space belongs to the escape, so this is one
    // ident `&B` and not `&` followed by `B`.
    const token_stream amp = tokenize("\\26 B");
    CHECK(amp.tokens.front().type == token_type::ident);
    CHECK(amp.text_of(amp.tokens.front()) == "&B");
    // A colon in a class name - how a framework writes `sm:block`. The old path
    // split on `:` and turned this into an unknown pseudo, killing the rule.
    const token_stream cls = tokenize(".foo\\:bar");
    CHECK(shape(cls) == "delim ident");
    for (const auto & t : cls.tokens) {
        if (t.type == token_type::ident) { CHECK(cls.text_of(t) == "foo:bar"); }
    }
    // An escaped sigil keeps the sigil: `#a\:b` is one hash token `#a:b`.
    const token_stream h = tokenize("#a\\:b");
    CHECK(h.tokens.front().type == token_type::hash);
    CHECK(h.text_of(h.tokens.front()) == "#a:b");
    // A hex escape with no trailing space still ends at a non-hex character.
    const token_stream two = tokenize("\\41\\42");
    CHECK(two.text_of(two.tokens.front()) == "AB");
    // Zero and out-of-range become U+FFFD rather than a null byte in the pool.
    const token_stream zero = tokenize("\\0");
    CHECK(zero.text_of(zero.tokens.front()) == "\xEF\xBF\xBD");
}

void test_at_keywords_and_the_charset_line() {
    expect_shape("@media", "at");
    expect_shape("@import url(a.css);", "at url semicolon");
    // Bootstrap's line 1. It must be one at-keyword, a string and a semicolon -
    // not something that swallows the rule after it.
    expect_texts("@charset \"UTF-8\";", "@charset|\"UTF-8\"|;");
    // `@` followed by something that cannot start an identifier is a delim.
    expect_shape("@ media", "delim ident");
    expect_shape("@1x", "delim dimension");
    const token_stream s = tokenize("@media");
    CHECK(s.value_of(s.tokens.front()) == "media");
}

void test_important_and_the_delims_around_it() {
    expect_shape("color:red!important", "ident colon ident delim ident");
    expect_shape("color:red ! important", "ident colon ident delim ident");
    // A `!` in the middle of a value is a delim too - nothing here decides what
    // it MEANS, which is the parser's job and is why rfind('!') was fragile.
    expect_shape("a:b!c", "ident colon ident delim ident");
}

void test_unbalanced_input_terminates() {
    // None of these may hang or read out of bounds; the shapes are secondary.
    for (const std::string_view css : {"a{", "a{b:c", "}", "((((", "url(", "\"", "\\",
                                       "/*", "a{b:url(", "@", "#", "-", "+", ".", "<!--"}) {
        const token_stream s = tokenize(css);
        CHECK(!s.tokens.empty());
        CHECK(s.tokens.back().type == token_type::eof);
    }
}

void test_preprocessing_folds_line_endings() {
    // CRLF, CR and FF all become one LF, so a token's extent does not depend on
    // which editor wrote the file.
    for (const std::string_view css : {"a\r\nb", "a\rb", "a\fb"}) {
        const token_stream s = tokenize(css);
        CHECK(shape(s) == "ident ident");
    }
    const token_stream crlf = tokenize("a\r\nb");
    CHECK(crlf.pool.find('\r') == std::string::npos);
}

void test_a_real_bootstrap_shaped_rule() {
    const std::string_view css =
        ".btn{--bs-btn-padding-x:0.75rem;color:var(--bs-btn-color);"
        "border:var(--bs-btn-border-width) solid transparent}";
    const token_stream s = tokenize(css);
    CHECK(s.tokens.back().type == token_type::eof);
    // The custom property is ONE ident: `--bs-btn-padding-x`. A tokenizer that
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
    REPORT("css_syntax");
}
