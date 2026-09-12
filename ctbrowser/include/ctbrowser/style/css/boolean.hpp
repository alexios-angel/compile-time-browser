#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/token.hpp>

// `<boolean-expr[ test ]>`, CSS Values 5 §boolean-expr: the one grammar behind
// `@media`'s conditions, `@supports`, `@container` and `if()`. Groups joined by
// all-`and` or all-`or` - mixing the two without parentheses is a syntax error
// - or `not` one group, where a group is a test, a parenthesised expression,
// or a `<general-enclosed>` nothing here can read.
//
// THREE-VALUED. A test this engine cannot decide - an unmodelled media feature,
// a range over two different types - is UNKNOWN, which `not` leaves unknown,
// `and` and `or` propagate the obvious way, and which counts as false only once
// the whole condition is asked. Reading unknown as false one level too early
// turns `not (unknown)` true, which is exactly the mistake the spec's three
// values exist to prevent.

namespace ctbrowser::style::css {

enum class truth : std::uint8_t {
    no,
    yes,
    unknown
};

[[nodiscard]] constexpr truth negate(truth t) noexcept {
    return t == truth::unknown ? t : (t == truth::yes ? truth::no : truth::yes);
}
[[nodiscard]] constexpr truth both(truth a, truth b) noexcept {
    if (a == truth::no || b == truth::no) { return truth::no; }
    if (a == truth::yes && b == truth::yes) { return truth::yes; }
    return truth::unknown;
}
[[nodiscard]] constexpr truth either(truth a, truth b) noexcept {
    if (a == truth::yes || b == truth::yes) { return truth::yes; }
    if (a == truth::no && b == truth::no) { return truth::no; }
    return truth::unknown;
}

// The index one past the block that opens at `open`, or the eof token if it
// never closes - EOF closes every open block, CSS Syntax 3 §5.4.9.
[[nodiscard]] inline std::size_t end_of_block(const token_stream & s, std::size_t open) {
    int depth = 0;
    for (std::size_t i = open; i < s.tokens.size(); ++i) {
        const css_token & t = s.tokens[i];
        if (t.type == token_type::eof) { return i; }
        if (t.type == token_type::function || t.type == token_type::open_paren ||
            t.type == token_type::open_square || t.type == token_type::open_curly) {
            ++depth;
        } else if (t.type == token_type::close_paren || t.type == token_type::close_square ||
                   t.type == token_type::close_curly) {
            --depth;
            if (depth == 0) { return i + 1; }
        }
    }
    return s.tokens.size() - 1;
}

[[nodiscard]] inline bool is_delim_text(const token_stream & s, std::size_t i, char c) {
    return i < s.tokens.size() && s.tokens[i].type == token_type::delim &&
           s.text_of(s.tokens[i]) == std::string_view{&c, 1};
}

[[nodiscard]] inline bool is_ident_named(const token_stream & s, std::size_t i,
                                         std::string_view want) {
    return i < s.tokens.size() && s.tokens[i].type == token_type::ident &&
           ascii_iequals(s.text_of(s.tokens[i]), want);
}

// `test` is asked with a function token's name and the token range of its
// argument list, and answers unknown for a function it does not know;
// `enclosed` is asked about a parenthesised group that is not itself an
// expression, which for an if() condition is a `<general-enclosed>` and for a
// style or media query is where the features are.
template <typename Test, typename Enclosed> class boolean_parser {
public:
    boolean_parser(const token_stream & s, const Test & test, const Enclosed & enclosed)
        : s_(s), test_(test), enclosed_(enclosed) {}

    // The tokens [i, end) as one expression, which must consume all of them.
    // nullopt is a PARSE failure, which the caller decides the meaning of.
    [[nodiscard]] std::optional<truth> expression(std::size_t i, std::size_t end) {
        std::optional<truth> result;
        skip_ws(i, end);
        if (is_ident_named(s_, i, "not")) {
            ++i;
            const std::optional<truth> g = group(i, end);
            if (!g) { return std::nullopt; }
            result = negate(*g);
        } else {
            result = group(i, end);
            enum class join : std::uint8_t {
                none,
                all_and,
                all_or
            } joined = join::none;
            while (result) {
                skip_ws(i, end);
                if (i >= end) { break; }
                const bool is_and = is_ident_named(s_, i, "and");
                const bool is_or = !is_and && is_ident_named(s_, i, "or");
                if (!is_and && !is_or) { return std::nullopt; }
                const join want = is_and ? join::all_and : join::all_or;
                if (joined != join::none && joined != want) { return std::nullopt; }
                joined = want;
                ++i;
                const std::optional<truth> g = group(i, end);
                if (!g) { return std::nullopt; }
                result = is_and ? both(*result, *g) : either(*result, *g);
            }
        }
        skip_ws(i, end);
        if (i < end) { return std::nullopt; } // trailing tokens
        return result;
    }

private:
    void skip_ws(std::size_t & i, std::size_t end) const {
        while (i < end && s_.tokens[i].type == token_type::whitespace) { ++i; }
    }

    // One group, `i` left one past it.
    [[nodiscard]] std::optional<truth> group(std::size_t & i, std::size_t end) {
        skip_ws(i, end);
        if (i >= end) { return std::nullopt; }
        const css_token & t = s_.tokens[i];
        const bool function = t.type == token_type::function;
        if (!function && t.type != token_type::open_paren) { return std::nullopt; }
        const std::size_t close = std::min(end_of_block(s_, i), end);
        // The argument list: past the opener, and short of the `)` when it is
        // there.
        const std::size_t last =
            close > i + 1 && s_.tokens[close - 1].type == token_type::close_paren ? close - 1
                                                                                  : close;
        std::optional<truth> answer;
        if (function) {
            std::string_view name = s_.text_of(t);
            name.remove_suffix(1);
            answer = test_(name, i + 1, last);
        } else {
            answer = expression(i + 1, last);
            if (!answer) { answer = enclosed_(i + 1, last); }
        }
        i = close;
        return answer;
    }

    const token_stream & s_;
    const Test & test_;
    const Enclosed & enclosed_;
};

template <typename Test, typename Enclosed>
[[nodiscard]] std::optional<truth> boolean_expression(const token_stream & s, std::size_t from,
                                                      std::size_t to, const Test & test,
                                                      const Enclosed & enclosed) {
    boolean_parser<Test, Enclosed> parser{s, test, enclosed};
    return parser.expression(from, to);
}

} // namespace ctbrowser::style::css
