//===- token.hpp - CSS tokenizer --------------------------------*- C++ -*-===//
//
// CSS Syntax Level 3 §4: the tokenizer. A real one, so that a `;` inside
// `content: "a;b"` does not end the declaration and a `}` inside a string does
// not desynchronise the rest of the file.
//
// Each stream has one text pool. Every token's text is an offset into
// `token_stream::pool`, which holds the §3.3-preprocessed input (CRLF/CR/FF ->
// LF, NUL -> U+FFFD); filtering into a copy up front keeps every offset valid.
// Escapes decode into a tail region appended to the same pool, so a token's
// text is always one contiguous view and no consumer needs an "is escaped"
// branch: `.foo\:bar` comes back as the class `foo:bar` with no further work.
//
// After parsing, the pool and the tokens move into the css::stylesheet
// (css/value.hpp), alongside the component values and rules built from them.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ctbrowser::style::css {

/// The token types of CSS Syntax 3 §4.
enum class token_type : std::uint8_t {
    eof,
    whitespace,
    ident,
    function,   // An ident immediately followed by `(`.
    at_keyword, // `@media`
    hash,       // `#id` or `#fff`; see flag_id_hash.
    string,
    bad_string, // A string cut short by a newline (§4.3.1).
    url,        // `url(` with an unquoted body; a quoted one is function+string.
    bad_url,
    number,
    percentage,
    dimension, // A number with a unit; see unit_length.
    delim,     // Any other single code point.
    cdo,       // `<!--`
    cdc,       // `-->`
    colon,
    semicolon,
    comma,
    open_square,
    close_square,
    open_paren,
    close_paren,
    open_curly,
    close_curly,
};

/// Set on a hash whose body would start an ident sequence (`#nav`, `#fff`,
/// `#f0a1b2`), as opposed to one that could not, like `#0d6efd`.
///
/// It does not distinguish a colour from an id selector: `#fff { }` really
/// does select `id="fff"`, and only the parser knows from context which a hash
/// means. The flag serves the other direction: `#0d6efd` cannot be an id
/// selector, because an identifier may not begin with a digit, so a selector
/// parser can reject it without re-examining the characters.
inline constexpr std::uint8_t flag_id_hash = 1u << 0;
/// Set on a number with no `.` and no exponent. `nth-child(2n+1)` needs it, and
/// so does telling `font-weight: 700` from `line-height: 1.5`.
inline constexpr std::uint8_t flag_integer = 1u << 1;

/// One token. Its text is [text, text + length) in token_stream::pool.
struct css_token {
    token_type type = token_type::eof;
    std::uint8_t flags = 0; // flag_id_hash, flag_integer.
    /// A dimension's unit is the last `unit_length` bytes of its text: `12px`
    /// is one token whose value is 12 and whose unit is `px`. One token rather
    /// than two keeps `12px` and `12 px` distinct to the grammar.
    std::uint16_t unit_length = 0;
    std::uint32_t text = 0; // Offset into token_stream::pool.
    std::uint32_t length = 0;
    double number = 0; // Number, percentage and dimension only.
    [[nodiscard]] friend bool operator==(const css_token &, const css_token &) = default;
};

/// The output of tokenize(): the text pool and the tokens that index it.
struct token_stream {
    std::string pool;
    std::vector<css_token> tokens;
    /// Where the source half of the pool ends and decoded escape text begins. A
    /// token whose `text` is below this is a slice of the source; one at or
    /// above it was rebuilt, and a run containing any such token is not a
    /// contiguous source substring.
    std::uint32_t source_length = 0;

    [[nodiscard]] std::string_view text_of(const css_token & t) const noexcept {
        return std::string_view{pool}.substr(t.text, t.length);
    }
    /// A dimension's unit as written; empty for any other token. CSS units are
    /// ASCII case-insensitive, but the tokenizer preserves the author's bytes,
    /// so callers that care fold with core/algorithms.hpp.
    [[nodiscard]] std::string_view unit_of(const css_token & t) const noexcept {
        if (t.type != token_type::dimension || t.unit_length == 0) { return {}; }
        return std::string_view{pool}.substr(t.text + t.length - t.unit_length, t.unit_length);
    }
    /// A token's text without its delimiters: a string's body without quotes,
    /// a hash or at-keyword without its leading `#`/`@`; a url token's text is
    /// already its body.
    [[nodiscard]] std::string_view value_of(const css_token & t) const noexcept {
        const std::string_view raw = text_of(t);
        if (t.type == token_type::string && raw.size() >= 2) {
            return raw.substr(1, raw.size() - 2);
        }
        if (t.type == token_type::hash && !raw.empty()) { return raw.substr(1); }
        if (t.type == token_type::at_keyword && !raw.empty()) { return raw.substr(1); }
        return raw;
    }
    [[nodiscard]] friend bool operator==(const token_stream &, const token_stream &) = default;
};

/// §4.2's name-start and name code points. Any byte >= 0x80 counts: a UTF-8
/// lead or continuation byte is a name code point, which lets `.café` tokenize
/// byte-wise with no decoding. These are the one definition for every "is this
/// still the identifier" test in the front end (a function-name boundary,
/// `var(` at a boundary, a family name that needs no quotes).
[[nodiscard]] constexpr bool is_name_start(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           static_cast<unsigned char>(c) >= 0x80;
}
[[nodiscard]] constexpr bool is_name(char c) noexcept {
    return is_name_start(c) || (c >= '0' && c <= '9') || c == '-';
}

/// Tokenizes `css` (§4.3.1). Never fails: every input is a sequence of tokens,
/// and the error cases are tokens too (bad_string, bad_url). A trailing `eof`
/// token is always present, so a parser can look ahead one token without a
/// bounds check on every read.
[[nodiscard]] token_stream tokenize(std::string_view css);

/// The source text of tokens [first, last), widened over the `url(` and `)` of
/// a <url-token> at either end (whose own text is just its body, so a run
/// ending in `url(a.png)` sliced by offsets alone would lose its `)`). Empty
/// when the run is not one slice of the source because an escape rebuilt one
/// of its tokens in the pool's tail.
[[nodiscard]] std::string_view source_run(const token_stream & s, std::size_t first,
                                          std::size_t last);

} // namespace ctbrowser::style::css
