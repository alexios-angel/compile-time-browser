#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// CSS Syntax Level 3, §4: the tokenizer.
//
// WHY THERE IS ONE NOW. The submodule this replaced had no tokenizer at all -
// `strip_css_comments` over the whole input, then a scan for `{`, `}`, `;` and
// the first `:`. Nothing was aware that a quote opens a string, so a `;` inside
// `content: "a;b"` ended the declaration, a `}` inside a string desynchronised
// the brace scanner for the rest of the file, and `content: "/*"` ate everything
// to the next `*/`. Bootstrap survives all three by luck - its data URIs are
// percent-encoded and it has no braces in strings - which is exactly the kind of
// luck that stops holding on the next stylesheet.
//
// ONE POOL PER STREAM. Every token's text is an offset into `token_stream::pool`,
// which is the §3.3-preprocessed input: CRLF/CR/FF -> LF, NUL -> U+FFFD. Doing
// that filtering into a copy up front is what makes every offset trustworthy.
// Escapes decode into a TAIL region appended to the same pool, so a token's text
// is always one contiguous view and no consumer needs an "is escaped" branch -
// `.foo\:bar` comes back as the class `foo:bar` with no further work.
//
// The token vector is DROPPED after parsing (see css/value.hpp): only the pool,
// the component values and the rules are retained.

namespace ctbrowser::style::css {

enum class token_type : std::uint8_t {
    eof,
    whitespace,
    ident,
    function,   // an ident immediately followed by `(`
    at_keyword, // `@media`
    hash,       // `#id` or `#fff`; see flag_id_hash
    string,
    bad_string, // a newline inside a string. §4.3.1 says it is its own token
    url,        // `url(` with an UNQUOTED body; a quoted one is function+string
    bad_url,
    number,
    percentage,
    dimension, // a number with a unit; see unit_length
    delim,     // any other single code point
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

// A hash whose body would start an IDENT SEQUENCE - `#nav`, `#fff`, `#f0a1b2` -
// as opposed to one that could not, like `#0d6efd`.
//
// It does NOT distinguish a colour from an id selector, and expecting it to was a
// misreading worth recording: `#fff` is id-like, and `#fff { }` really does select
// `id="fff"`. Whether a hash MEANS a colour or an id is decided by where it
// appears, and only the parser knows that. What the flag is actually for is the
// other direction - `#0d6efd` cannot be an id selector, because an identifier may
// not begin with a digit, so a selector parser can reject it without re-examining
// the characters.
inline constexpr std::uint8_t flag_id_hash = 1u << 0;
// A number with no `.` and no exponent. `nth-child(2n+1)` needs it, and so does
// telling `font-weight: 700` from `line-height: 1.5`.
inline constexpr std::uint8_t flag_integer = 1u << 1;

struct css_token {
    token_type type = token_type::eof;
    std::uint8_t flags = 0;
    // A dimension's unit is the LAST `unit_length` bytes of [text, text+length):
    // `12px` is one token whose value is 12 and whose unit is `px`. Keeping them
    // in one token rather than two is what stops `12 px` and `12px` looking the
    // same to the grammar, which they are not.
    std::uint16_t unit_length = 0;
    std::uint32_t text = 0; // offset into token_stream::pool
    std::uint32_t length = 0;
    double number = 0; // number, percentage and dimension only
};

struct token_stream {
    std::string pool;
    std::vector<css_token> tokens;
    // Where the input half of the pool ends and decoded escape text begins. A
    // token whose `text` is below this is a slice of the source; one at or above
    // it was rebuilt, and a RUN containing any such token is not a contiguous
    // source substring.
    std::uint32_t source_length = 0;

    [[nodiscard]] std::string_view text_of(const css_token & t) const noexcept {
        return std::string_view{pool}.substr(t.text, t.length);
    }
    // A dimension's unit, lowercased by the caller if it cares. CSS units are
    // ASCII case-insensitive but the tokenizer preserves what was written -
    // folding here would lose the author's bytes for no gain, and
    // core/algorithms.hpp has the fold for whoever needs it.
    [[nodiscard]] std::string_view unit_of(const css_token & t) const noexcept {
        if (t.type != token_type::dimension || t.unit_length == 0) { return {}; }
        return std::string_view{pool}.substr(t.text + t.length - t.unit_length, t.unit_length);
    }
    // The value of an ident/string/url token with its delimiters removed. For a
    // string that is the body; for a url it is already the body.
    [[nodiscard]] std::string_view value_of(const css_token & t) const noexcept {
        const std::string_view raw = text_of(t);
        if (t.type == token_type::string && raw.size() >= 2) { return raw.substr(1, raw.size() - 2); }
        if (t.type == token_type::hash && !raw.empty()) { return raw.substr(1); }
        if (t.type == token_type::at_keyword && !raw.empty()) { return raw.substr(1); }
        return raw;
    }
};

// §4.3.1. Never fails: every input is a sequence of tokens, and the error cases
// are tokens too (bad_string, bad_url). A trailing `eof` token is always present
// so a parser can look ahead one without a bounds check on every read.
[[nodiscard]] token_stream tokenize(std::string_view css);

} // namespace ctbrowser::style::css
