#include <ctbrowser/style/css/token.hpp>

#include <charconv>
#include <cstdint>
#include <string>
#include <system_error>
#include <string_view>

// CSS Syntax Level 3 §4.3, the consume-a-token algorithms.
//
// THE POOL IS BUILT IN TWO HALVES AND CONCATENATED ONCE. The first half is the
// §3.3-preprocessed input, so every unescaped token's text is a slice of it and
// every offset is stable. The second half is decoded escapes: `\26` and `\:` have
// text that appears nowhere in the source, so it is appended to a `decoded`
// buffer and the token points at `input.size() + k`. Because the final pool is
// exactly `input + decoded`, that offset is already correct and no fix-up pass is
// needed - which matters because appending to the pool while holding
// string_views into it would invalidate them.
//
// Escapes are rare (Bootstrap has none; Tailwind is full of them), so `decoded`
// is usually empty and the pool is one copy of the input.

namespace ctbrowser::style::css {
namespace {

constexpr char32_t replacement_character = 0xFFFD;
// §4.3.7: a code point above the Unicode maximum, or a surrogate, becomes
// U+FFFD. The maximum is here rather than inline so the two places that check it
// cannot disagree.
constexpr char32_t max_code_point = 0x10FFFF;

[[nodiscard]] constexpr bool is_newline(char c) noexcept {
    return c == '\n'; // preprocessing has already folded \r\n, \r and \f
}
[[nodiscard]] constexpr bool is_whitespace(char c) noexcept {
    return c == '\n' || c == '\t' || c == ' ';
}
[[nodiscard]] constexpr bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}
[[nodiscard]] constexpr bool is_hex(char c) noexcept {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
[[nodiscard]] constexpr int hex_of(char c) noexcept {
    if (is_digit(c)) { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    return c - 'A' + 10;
}
[[nodiscard]] constexpr bool is_letter(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
// §4.2. Anything >= 0x80 counts: a UTF-8 lead or continuation byte is a name code
// point, which is what lets `.café` tokenize byte-wise with no decoding.
[[nodiscard]] constexpr bool is_name_start(char c) noexcept {
    return is_letter(c) || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}
[[nodiscard]] constexpr bool is_name(char c) noexcept {
    return is_name_start(c) || is_digit(c) || c == '-';
}
// §4.3.8, and NOT `\` followed by anything: a backslash at the end of a line does
// not escape it, which is what makes an unterminated string recoverable.
[[nodiscard]] constexpr bool is_valid_escape(std::string_view s, std::size_t at) noexcept {
    return at < s.size() && s[at] == '\\' && at + 1 < s.size() && !is_newline(s[at + 1]);
}

void append_utf8(std::string & out, char32_t cp) {
    if (cp == 0 || cp > max_code_point || (cp >= 0xD800 && cp <= 0xDFFF)) {
        cp = replacement_character;
    }
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// §3.3. Into a copy, because that is what makes every later offset trustworthy -
// and it replaces the whole-input comment strip this used to have, which changed
// the offsets of everything after every comment.
[[nodiscard]] std::string preprocess(std::string_view css) {
    std::string out;
    out.reserve(css.size());
    std::size_t at = 0;
    // A UTF-8 BOM is not a name code point but nothing removed it before, so it
    // became the leading characters of the first selector and killed exactly one
    // rule - silently, since an unmatchable selector is not an error.
    if (css.size() >= 3 && static_cast<unsigned char>(css[0]) == 0xEF &&
        static_cast<unsigned char>(css[1]) == 0xBB && static_cast<unsigned char>(css[2]) == 0xBF) {
        at = 3;
    }
    for (; at < css.size(); ++at) {
        const char c = css[at];
        if (c == '\r') {
            out += '\n';
            if (at + 1 < css.size() && css[at + 1] == '\n') { ++at; }
        } else if (c == '\f') {
            out += '\n';
        } else if (c == '\0') {
            append_utf8(out, replacement_character);
        } else {
            out += c;
        }
    }
    return out;
}

class lexer {
public:
    explicit lexer(std::string_view input) : s_(input) {}

    [[nodiscard]] token_stream run() {
        token_stream out;
        for (;;) {
            skip_comments();
            if (at_ >= s_.size()) { break; }
            out.tokens.push_back(next());
        }
        css_token end;
        end.text = static_cast<std::uint32_t>(s_.size());
        out.tokens.push_back(end);
        // input THEN decoded, which is what makes the offsets already written
        // into the escaped tokens correct.
        out.pool.reserve(s_.size() + decoded_.size());
        out.pool.assign(s_);
        out.pool += decoded_;
        out.source_length = static_cast<std::uint32_t>(s_.size());
        return out;
    }

private:
    [[nodiscard]] char peek(std::size_t ahead = 0) const noexcept {
        return at_ + ahead < s_.size() ? s_[at_ + ahead] : '\0';
    }

    // §4.3.2, and it runs between tokens rather than as a pre-pass: a comment is
    // not a token, but it IS a boundary - `a/**/b` is two idents, and stripping
    // comments up front made it one.
    void skip_comments() {
        while (at_ + 1 < s_.size() && s_[at_] == '/' && s_[at_ + 1] == '*') {
            const std::size_t close = s_.find("*/", at_ + 2);
            at_ = close == std::string_view::npos ? s_.size() : close + 2;
        }
    }

    [[nodiscard]] css_token simple(token_type type, std::size_t length) {
        css_token t;
        t.type = type;
        t.text = static_cast<std::uint32_t>(at_);
        t.length = static_cast<std::uint32_t>(length);
        at_ += length;
        return t;
    }

    // §4.3.9: would the next three code points start an identifier?
    [[nodiscard]] bool starts_ident(std::size_t from) const noexcept {
        if (from >= s_.size()) { return false; }
        if (is_name_start(s_[from])) { return true; }
        if (is_valid_escape(s_, from)) { return true; }
        if (s_[from] != '-') { return false; }
        if (from + 1 >= s_.size()) { return false; }
        return is_name_start(s_[from + 1]) || s_[from + 1] == '-' || is_valid_escape(s_, from + 1);
    }

    // §4.3.10: would the next three code points start a number?
    [[nodiscard]] bool starts_number(std::size_t from) const noexcept {
        if (from >= s_.size()) { return false; }
        const char c = s_[from];
        if (is_digit(c)) { return true; }
        if (c == '.') { return from + 1 < s_.size() && is_digit(s_[from + 1]); }
        if (c != '+' && c != '-') { return false; }
        if (from + 1 >= s_.size()) { return false; }
        if (is_digit(s_[from + 1])) { return true; }
        return s_[from + 1] == '.' && from + 2 < s_.size() && is_digit(s_[from + 2]);
    }

    [[nodiscard]] css_token next() {
        const char c = peek();
        if (is_whitespace(c)) {
            const std::size_t start = at_;
            while (at_ < s_.size() && is_whitespace(s_[at_])) { ++at_; }
            css_token t;
            t.type = token_type::whitespace;
            t.text = static_cast<std::uint32_t>(start);
            t.length = static_cast<std::uint32_t>(at_ - start);
            return t;
        }
        switch (c) {
        case '"':
        case '\'': return consume_string(c);
        case '(': return simple(token_type::open_paren, 1);
        case ')': return simple(token_type::close_paren, 1);
        case '[': return simple(token_type::open_square, 1);
        case ']': return simple(token_type::close_square, 1);
        case '{': return simple(token_type::open_curly, 1);
        case '}': return simple(token_type::close_curly, 1);
        case ',': return simple(token_type::comma, 1);
        case ':': return simple(token_type::colon, 1);
        case ';': return simple(token_type::semicolon, 1);
        case '#':
            if (is_name(peek(1)) || is_valid_escape(s_, at_ + 1)) {
                const std::size_t start = at_;
                ++at_; // the '#'
                // ID-LIKE OR NOT is decided BEFORE the name is consumed, from the
                // characters that follow: `#fff` is a colour and `#f0f0f0` is a
                // colour, but `#nav` is a selector, and only the first character
                // run can tell them apart.
                const bool id_like = starts_ident(at_);
                css_token t = consume_name_token(token_type::hash, start);
                if (id_like) { t.flags |= flag_id_hash; }
                return t;
            }
            return simple(token_type::delim, 1);
        case '+': return starts_number(at_) ? consume_numeric() : simple(token_type::delim, 1);
        case '.': return starts_number(at_) ? consume_numeric() : simple(token_type::delim, 1);
        case '-':
            if (starts_number(at_)) { return consume_numeric(); }
            if (peek(1) == '-' && peek(2) == '>') { return simple(token_type::cdc, 3); }
            if (starts_ident(at_)) { return consume_ident_like(); }
            return simple(token_type::delim, 1);
        case '<':
            if (peek(1) == '!' && peek(2) == '-' && peek(3) == '-') {
                return simple(token_type::cdo, 4);
            }
            return simple(token_type::delim, 1);
        case '@':
            if (starts_ident(at_ + 1)) {
                const std::size_t start = at_;
                ++at_; // the '@'
                return consume_name_token(token_type::at_keyword, start);
            }
            return simple(token_type::delim, 1);
        case '\\':
            if (is_valid_escape(s_, at_)) { return consume_ident_like(); }
            return simple(token_type::delim, 1); // a parse error, and a delim
        default: break;
        }
        if (is_digit(c)) { return consume_numeric(); }
        if (is_name_start(c)) { return consume_ident_like(); }
        return simple(token_type::delim, 1);
    }

    // §4.3.7. Returns the decoded code point; `at_` is left after the escape.
    [[nodiscard]] char32_t consume_escape() {
        ++at_; // the backslash
        if (at_ >= s_.size()) { return replacement_character; }
        const char c = s_[at_];
        if (!is_hex(c)) {
            // One code point, taken verbatim. This is the `\:` in `.foo\:bar`,
            // which is how a framework puts a colon in a class name.
            const std::size_t start = at_;
            std::size_t width = 1;
            // A non-ASCII escape is a whole UTF-8 sequence; copying its bytes
            // through is enough, since nothing here needs the scalar value.
            while (start + width < s_.size() &&
                   static_cast<unsigned char>(s_[start + width]) >= 0x80 &&
                   static_cast<unsigned char>(s_[start + width]) < 0xC0) {
                ++width;
            }
            at_ = start + width;
            verbatim_ = s_.substr(start, width);
            return 0; // signals "use verbatim_"
        }
        char32_t value = 0;
        int digits = 0;
        while (digits < 6 && at_ < s_.size() && is_hex(s_[at_])) {
            value = value * 16 + static_cast<char32_t>(hex_of(s_[at_]));
            ++at_;
            ++digits;
        }
        // ONE trailing whitespace is part of the escape, so `\26 B` is `&B` and
        // not `& B`.
        if (at_ < s_.size() && is_whitespace(s_[at_])) { ++at_; }
        return value == 0 ? replacement_character : value;
    }

    // §4.3.11, into `decoded_` when an escape appears and in place otherwise.
    [[nodiscard]] css_token consume_name_token(token_type type, std::size_t start) {
        // The fast path: no escapes, so the token is a slice of the input and
        // nothing is copied. This is every token in every stylesheet that does
        // not use escapes.
        std::size_t scan = at_;
        while (scan < s_.size() && (is_name(s_[scan]) || is_valid_escape(s_, scan))) {
            if (s_[scan] == '\\') { break; }
            ++scan;
        }
        if (scan >= s_.size() || s_[scan] != '\\') {
            at_ = scan;
            css_token t;
            t.type = type;
            t.text = static_cast<std::uint32_t>(start);
            t.length = static_cast<std::uint32_t>(at_ - start);
            return t;
        }
        // The slow path: rebuild the whole token - prefix included, so `#a\:b`
        // keeps its `#` - into the tail.
        const std::size_t out_start = decoded_.size();
        decoded_.append(s_.substr(start, at_ - start)); // the sigil, if any
        while (at_ < s_.size()) {
            if (is_valid_escape(s_, at_)) {
                verbatim_ = {};
                const char32_t cp = consume_escape();
                if (cp == 0) {
                    decoded_.append(verbatim_);
                } else {
                    append_utf8(decoded_, cp);
                }
                continue;
            }
            if (!is_name(s_[at_])) { break; }
            decoded_ += s_[at_];
            ++at_;
        }
        css_token t;
        t.type = type;
        t.text = static_cast<std::uint32_t>(s_.size() + out_start);
        t.length = static_cast<std::uint32_t>(decoded_.size() - out_start);
        return t;
    }

    // §4.3.4. A newline inside a string makes a bad_string, and the newline is
    // NOT consumed - it belongs to whatever follows, which is what lets the rest
    // of the sheet parse.
    [[nodiscard]] css_token consume_string(char quote) {
        const std::size_t start = at_;
        ++at_;
        bool escaped = false;
        std::size_t scan = at_;
        while (scan < s_.size() && s_[scan] != quote && !is_newline(s_[scan])) {
            if (s_[scan] == '\\') {
                escaped = true;
                break;
            }
            ++scan;
        }
        if (!escaped) {
            if (scan < s_.size() && is_newline(s_[scan])) {
                at_ = scan;
                css_token t;
                t.type = token_type::bad_string;
                t.text = static_cast<std::uint32_t>(start);
                t.length = static_cast<std::uint32_t>(at_ - start);
                return t;
            }
            // The closing quote, or EOF - which §4.3.4 says is a parse error and
            // a normal string token, not a bad one.
            at_ = scan < s_.size() ? scan + 1 : s_.size();
            css_token t;
            t.type = token_type::string;
            t.text = static_cast<std::uint32_t>(start);
            t.length = static_cast<std::uint32_t>(at_ - start);
            return t;
        }
        // Escapes present: rebuild into the tail, KEEPING the quotes so
        // value_of() can strip them uniformly.
        const std::size_t out_start = decoded_.size();
        decoded_ += quote;
        while (at_ < s_.size() && s_[at_] != quote && !is_newline(s_[at_])) {
            if (s_[at_] == '\\') {
                if (at_ + 1 < s_.size() && is_newline(s_[at_ + 1])) {
                    at_ += 2; // an escaped newline is a line continuation
                    continue;
                }
                verbatim_ = {};
                const char32_t cp = consume_escape();
                if (cp == 0) {
                    decoded_.append(verbatim_);
                } else {
                    append_utf8(decoded_, cp);
                }
                continue;
            }
            decoded_ += s_[at_];
            ++at_;
        }
        const bool unterminated = at_ >= s_.size() || is_newline(s_[at_]);
        if (!unterminated) { ++at_; }
        decoded_ += quote;
        css_token t;
        t.type = unterminated && at_ < s_.size() ? token_type::bad_string : token_type::string;
        t.text = static_cast<std::uint32_t>(s_.size() + out_start);
        t.length = static_cast<std::uint32_t>(decoded_.size() - out_start);
        return t;
    }

    // §4.3.3 and §4.3.12 together: the number, then what follows decides whether
    // it is a dimension, a percentage or a plain number.
    [[nodiscard]] css_token consume_numeric() {
        const std::size_t start = at_;
        bool integer = true;
        if (peek() == '+' || peek() == '-') { ++at_; }
        while (at_ < s_.size() && is_digit(s_[at_])) { ++at_; }
        if (peek() == '.' && is_digit(peek(1))) {
            integer = false;
            at_ += 2;
            while (at_ < s_.size() && is_digit(s_[at_])) { ++at_; }
        }
        if ((peek() == 'e' || peek() == 'E')) {
            const std::size_t save = at_;
            std::size_t look = at_ + 1;
            if (look < s_.size() && (s_[look] == '+' || s_[look] == '-')) { ++look; }
            if (look < s_.size() && is_digit(s_[look])) {
                integer = false;
                at_ = look;
                while (at_ < s_.size() && is_digit(s_[at_])) { ++at_; }
            } else {
                at_ = save;
            }
        }
        // from_chars, like layout/values.hpp - no allocation, no exception. It
        // rejects a leading `+`, which CSS allows, so the sign is peeled first.
        std::string_view digits = s_.substr(start, at_ - start);
        double sign = 1;
        if (!digits.empty() && (digits.front() == '+' || digits.front() == '-')) {
            if (digits.front() == '-') { sign = -1; }
            digits.remove_prefix(1);
        }
        double value = 0;
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
        // Out of range still produces a token, per §4.3.3 - the sheet is not
        // broken by a silly number, the declaration just will not resolve.
        if (parsed.ec != std::errc{}) { value = 0; }
        value *= sign;

        const std::size_t number_end = at_;
        if (starts_ident(at_)) {
            css_token t = consume_name_token(token_type::dimension, start);
            // A dimension built through the slow path has been REBUILT into the
            // tail, so the unit length is measured against the rebuilt text.
            t.number = value;
            if (integer) { t.flags |= flag_integer; }
            const std::size_t unit = t.length - (number_end - start);
            t.unit_length = static_cast<std::uint16_t>(unit);
            return t;
        }
        if (peek() == '%') {
            ++at_;
            css_token t;
            t.type = token_type::percentage;
            t.text = static_cast<std::uint32_t>(start);
            t.length = static_cast<std::uint32_t>(at_ - start);
            t.number = value;
            return t;
        }
        css_token t;
        t.type = token_type::number;
        t.text = static_cast<std::uint32_t>(start);
        t.length = static_cast<std::uint32_t>(at_ - start);
        t.number = value;
        if (integer) { t.flags |= flag_integer; }
        return t;
    }

    // §4.3.5. `url(` with an unquoted body is its own token, because the body is
    // not a string and not an ident - it is "everything to the `)`", which is why
    // `url(a b.png)` is a BAD url and `url("a b.png")` is a function and a string.
    [[nodiscard]] css_token consume_url(std::size_t start) {
        while (at_ < s_.size() && is_whitespace(s_[at_])) { ++at_; }
        const std::size_t body = at_;
        bool bad = false;
        bool escaped = false;
        while (at_ < s_.size()) {
            const char c = s_[at_];
            if (c == ')') { break; }
            if (is_whitespace(c)) {
                std::size_t look = at_;
                while (look < s_.size() && is_whitespace(s_[look])) { ++look; }
                if (look < s_.size() && s_[look] != ')') { bad = true; }
                at_ = look;
                break;
            }
            if (c == '"' || c == '\'' || c == '(') {
                bad = true;
                break;
            }
            if (c == '\\') {
                if (!is_valid_escape(s_, at_)) {
                    bad = true;
                    break;
                }
                escaped = true;
                at_ += 2;
                continue;
            }
            ++at_;
        }
        const std::size_t body_end = at_;
        if (bad) {
            // §4.3.14: consume to the `)` so the rest of the sheet survives.
            while (at_ < s_.size() && s_[at_] != ')') {
                if (is_valid_escape(s_, at_)) { ++at_; }
                ++at_;
            }
            if (at_ < s_.size()) { ++at_; }
            css_token t;
            t.type = token_type::bad_url;
            t.text = static_cast<std::uint32_t>(start);
            t.length = static_cast<std::uint32_t>(at_ - start);
            return t;
        }
        if (at_ < s_.size()) { ++at_; } // the ')'
        css_token t;
        t.type = token_type::url;
        if (!escaped) {
            t.text = static_cast<std::uint32_t>(body);
            t.length = static_cast<std::uint32_t>(body_end - body);
            return t;
        }
        const std::size_t out_start = decoded_.size();
        for (std::size_t i = body; i < body_end;) {
            if (s_[i] == '\\' && i + 1 < body_end) {
                const std::size_t save = at_;
                at_ = i;
                verbatim_ = {};
                const char32_t cp = consume_escape();
                if (cp == 0) {
                    decoded_.append(verbatim_);
                } else {
                    append_utf8(decoded_, cp);
                }
                i = at_;
                at_ = save;
                continue;
            }
            decoded_ += s_[i];
            ++i;
        }
        t.text = static_cast<std::uint32_t>(s_.size() + out_start);
        t.length = static_cast<std::uint32_t>(decoded_.size() - out_start);
        return t;
    }

    // §4.3.4 (ident-like). `url(` is special-cased before `function`, and the
    // distinction is not cosmetic: `url(data:...;base64,...)` has a `;` in it,
    // and treating it as a function would let the declaration splitter cut there.
    [[nodiscard]] css_token consume_ident_like() {
        const std::size_t start = at_;
        css_token name = consume_name_token(token_type::ident, start);
        if (peek() != '(') { return name; }
        // ASCII case-insensitive, and measured on the token's VALUE so an escaped
        // spelling like `u\rl(` is recognised too.
        std::string_view text = name.length == 0 ? std::string_view{}
                                : name.text >= s_.size()
                                    ? std::string_view{decoded_}.substr(name.text - s_.size(),
                                                                        name.length)
                                    : s_.substr(name.text, name.length);
        bool is_url = text.size() == 3;
        if (is_url) {
            const auto lower = [](char c) {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
            };
            is_url = lower(text[0]) == 'u' && lower(text[1]) == 'r' && lower(text[2]) == 'l';
        }
        ++at_; // the '('
        if (is_url) {
            std::size_t look = at_;
            while (look < s_.size() && is_whitespace(s_[look])) { ++look; }
            // A QUOTED body is a function plus a string, per §4.3.4, so
            // `url("a.png")` and `url(a.png)` come out as different token shapes
            // on purpose - the first has a real string in it.
            if (look >= s_.size() || (s_[look] != '"' && s_[look] != '\'')) {
                return consume_url(start);
            }
        }
        name.type = token_type::function;
        name.length = static_cast<std::uint32_t>(name.length + 1); // include the '('
        return name;
    }

    std::string_view s_;
    std::string decoded_;
    std::string_view verbatim_; // set by consume_escape for the non-hex case
    std::size_t at_ = 0;
};

} // namespace

token_stream tokenize(std::string_view css) {
    // The preprocessed input has to outlive the lexer's views into it, and the
    // stream owns the final copy - so it is built here and moved through.
    const std::string input = preprocess(css);
    lexer lex{input};
    return lex.run();
}

} // namespace ctbrowser::style::css
