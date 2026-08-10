#include <ctbrowser/style/css/calc.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/token.hpp>

namespace ctbrowser::style::css {
namespace {

// One operand mid-expression. CSS Values 4 §10 gives calc a type system, and the
// three states here are the whole of it for lengths: a plain number, a length,
// and a length that carries an unresolved percentage. Keeping `is_number`
// separate from "px happens to be zero" is what makes `1px + 2` an error rather
// than 3px.
struct term {
    bool is_number = true;
    float number = 0.0f;
    float px = 0.0f;
    float percent = 0.0f;
    bool has_percent = false;
};

[[nodiscard]] std::optional<term> add(const term & a, const term & b, bool subtract) {
    const float sign = subtract ? -1.0f : 1.0f;
    // A number and a length cannot be added. This is the check that makes
    // `calc(100% - 12)` invalid, which is what Chrome does with it.
    if (a.is_number != b.is_number) { return std::nullopt; }
    term out;
    out.is_number = a.is_number;
    out.number = a.number + sign * b.number;
    out.px = a.px + sign * b.px;
    out.percent = a.percent + sign * b.percent;
    out.has_percent = a.has_percent || b.has_percent;
    return out;
}

[[nodiscard]] std::optional<term> multiply(const term & a, const term & b) {
    // At most one operand may be a length: `2px * 3px` is an area, and calc has
    // no type for one.
    if (!a.is_number && !b.is_number) { return std::nullopt; }
    if (a.is_number && b.is_number) {
        term out;
        out.number = a.number * b.number;
        return out;
    }
    const term & len = a.is_number ? b : a;
    const float scale = a.is_number ? a.number : b.number;
    term out = len;
    out.px *= scale;
    out.percent *= scale;
    return out;
}

[[nodiscard]] std::optional<term> divide(const term & a, const term & b) {
    // The divisor must be a number, and a nonzero one. `calc(1px / 0)` is invalid
    // rather than infinite.
    if (!b.is_number || b.number == 0.0f) { return std::nullopt; }
    term out = a;
    out.number = a.number / b.number;
    out.px = a.px / b.number;
    out.percent = a.percent / b.number;
    return out;
}

// A recursive-descent parser over the token stream, one instance per expression.
// `ok_` latches false on the first error so every level can stop checking.
class evaluator {
public:
    evaluator(const token_stream & tokens, const length_context & ctx) : t_(tokens), ctx_(ctx) {}

    [[nodiscard]] std::optional<calc_result> run() {
        const std::optional<term> value = sum();
        skip_whitespace();
        // A trailing token means the expression did not consume its input -
        // `calc(1px 2px)` - which is an error and not a partial answer.
        if (!ok_ || !value || !at_end()) { return std::nullopt; }
        if (value->is_number) { return std::nullopt; } // calc() of a bare number is not a length
        calc_result out;
        out.px = value->px;
        out.percent = value->percent;
        out.has_percent = value->has_percent;
        return out;
    }

private:
    [[nodiscard]] const css_token & peek() const noexcept { return t_.tokens[at_]; }
    [[nodiscard]] bool at_end() const noexcept { return peek().type == token_type::eof; }
    void skip_whitespace() noexcept {
        while (peek().type == token_type::whitespace) { ++at_; }
    }
    [[nodiscard]] bool is_delim(char c) const noexcept {
        return peek().type == token_type::delim && t_.text_of(peek()) == std::string_view{&c, 1};
    }

    // sum := product (('+' | '-') product)*
    //
    // CSS requires whitespace around `+` and `-` inside a calc, and this gets it
    // for free by not looking for a sign: the tokenizer folds `-12px` into one
    // dimension token, so `calc(100% -12px)` presents two lengths with no
    // operator between them and fails at the trailing-token check. That is
    // Chrome's answer for it too.
    [[nodiscard]] std::optional<term> sum() {
        std::optional<term> left = product();
        if (!left) { return std::nullopt; }
        for (;;) {
            skip_whitespace();
            const bool plus = is_delim('+');
            const bool minus = is_delim('-');
            if (!plus && !minus) { return left; }
            ++at_;
            const std::optional<term> right = product();
            if (!right) { return fail(); }
            left = add(*left, *right, minus);
            if (!left) { return fail(); }
        }
    }

    // product := term (('*' | '/') term)*
    [[nodiscard]] std::optional<term> product() {
        std::optional<term> left = single();
        if (!left) { return std::nullopt; }
        for (;;) {
            skip_whitespace();
            const bool times = is_delim('*');
            const bool over = is_delim('/');
            if (!times && !over) { return left; }
            ++at_;
            const std::optional<term> right = single();
            if (!right) { return fail(); }
            left = times ? multiply(*left, *right) : divide(*left, *right);
            if (!left) { return fail(); }
        }
    }

    // term := <number> | <dimension> | <percentage> | '(' sum ')' | calc( sum )
    [[nodiscard]] std::optional<term> single() {
        skip_whitespace();
        const css_token & tok = peek();
        switch (tok.type) {
        case token_type::number: {
            ++at_;
            term out;
            out.number = static_cast<float>(tok.number);
            return out;
        }
        case token_type::percentage: {
            ++at_;
            term out;
            out.is_number = false;
            out.percent = static_cast<float>(tok.number);
            out.has_percent = true;
            return out;
        }
        case token_type::dimension: {
            const std::optional<float> px =
                unit_to_px(static_cast<float>(tok.number), t_.unit_of(tok), ctx_);
            if (!px) { return fail(); } // an unmodelled unit, not a zero
            ++at_;
            term out;
            out.is_number = false;
            out.px = *px;
            return out;
        }
        case token_type::open_paren: return nested();
        case token_type::function:
            // A NESTED CALC, which Bootstrap writes eight times over
            // (`calc(1em + .5rem + calc(var(x) * 2))`). Any other function - one
            // this does not evaluate - is an error rather than a guess.
            if (!ascii_iequals(t_.text_of(tok), "calc(")) { return fail(); }
            return nested();
        default: return fail();
        }
    }

    // The body of a `(` or `calc(`, up to its matching close paren.
    [[nodiscard]] std::optional<term> nested() {
        ++at_; // the opener
        const std::optional<term> inner = sum();
        if (!inner) { return std::nullopt; }
        skip_whitespace();
        if (peek().type != token_type::close_paren) { return fail(); }
        ++at_;
        return inner;
    }

    [[nodiscard]] std::optional<term> fail() {
        ok_ = false;
        return std::nullopt;
    }

    const token_stream & t_;
    const length_context & ctx_;
    std::size_t at_ = 0;
    bool ok_ = true;
};

// Trailing zeros off a float, so a folded `12px` is not `12.000000px`. CSS
// serialisation drops them and so does every engine's getComputedStyle.
[[nodiscard]] std::string format_number(float value) {
    if (value == std::floor(value) && std::fabs(value) < 1e9f) {
        return std::to_string(static_cast<long long>(value));
    }
    std::string text = std::to_string(value);
    while (text.size() > 1 && text.back() == '0') { text.pop_back(); }
    if (!text.empty() && text.back() == '.') { text.pop_back(); }
    return text;
}

} // namespace

std::optional<float> unit_to_px(float value, std::string_view unit, const length_context & ctx) {
    if (unit.empty()) { return value; } // a plain number in a calc term
    if (ascii_iequals(unit, "px")) { return value; }
    if (ascii_iequals(unit, "em")) { return value * ctx.font_size; }
    if (ascii_iequals(unit, "rem")) { return value * ctx.root_font_size; }
    // ch and ex need font metrics the style engine deliberately cannot see -
    // layout/values.hpp is explicit that measurement is injected - so both take
    // CSS's own fallback of half an em. Bootstrap uses neither.
    if (ascii_iequals(unit, "ch") || ascii_iequals(unit, "ex")) { return value * ctx.font_size / 2; }
    if (ascii_iequals(unit, "vw")) { return value * ctx.viewport_width / 100.0f; }
    if (ascii_iequals(unit, "vh")) { return value * ctx.viewport_height / 100.0f; }
    if (ascii_iequals(unit, "vmin")) {
        return value * std::min(ctx.viewport_width, ctx.viewport_height) / 100.0f;
    }
    if (ascii_iequals(unit, "vmax")) {
        return value * std::max(ctx.viewport_width, ctx.viewport_height) / 100.0f;
    }
    // The absolute units, all defined against the CSS inch of 96px.
    if (ascii_iequals(unit, "in")) { return value * 96.0f; }
    if (ascii_iequals(unit, "cm")) { return value * 96.0f / 2.54f; }
    if (ascii_iequals(unit, "mm")) { return value * 96.0f / 25.4f; }
    if (ascii_iequals(unit, "q")) { return value * 96.0f / 101.6f; }
    if (ascii_iequals(unit, "pt")) { return value * 96.0f / 72.0f; }
    if (ascii_iequals(unit, "pc")) { return value * 16.0f; }
    return std::nullopt;
}

std::optional<calc_result> evaluate_calc(std::string_view expression, const length_context & ctx) {
    const token_stream tokens = tokenize(expression);
    evaluator run{tokens, ctx};
    return run.run();
}

namespace {

// The one token of a single-value length, or nullptr if the text is not exactly
// one value. `12px 4px` is a shorthand and answering with its first component
// would be a quiet wrong answer.
[[nodiscard]] const css_token * lone_value(const token_stream & tokens) {
    std::size_t at = 0;
    while (tokens.tokens[at].type == token_type::whitespace) { ++at; }
    std::size_t after = at + 1;
    while (tokens.tokens[after].type == token_type::whitespace) { ++after; }
    if (tokens.tokens[after].type != token_type::eof) { return nullptr; }
    return &tokens.tokens[at];
}

} // namespace

std::optional<float> dimension_text_to_px(std::string_view text, const length_context & ctx) {
    const token_stream tokens = tokenize(text);
    const css_token * tok = lone_value(tokens);
    if (tok == nullptr || tok->type != token_type::dimension) { return std::nullopt; }
    return unit_to_px(static_cast<float>(tok->number), tokens.unit_of(*tok), ctx);
}

std::optional<float> length_text_to_px(std::string_view text, const length_context & ctx) {
    // Tokenized rather than scanned, so `1.5e1px` and an escaped unit behave the
    // same here as they do everywhere else in the front end.
    const token_stream tokens = tokenize(text);
    const css_token * tok = lone_value(tokens);
    if (tok == nullptr) { return std::nullopt; }
    if (tok->type == token_type::number) { return static_cast<float>(tok->number); }
    if (tok->type != token_type::dimension) { return std::nullopt; }
    return unit_to_px(static_cast<float>(tok->number), tokens.unit_of(*tok), ctx);
}

std::string serialize_calc(const calc_result & value) {
    if (!value.has_percent) { return format_number(value.px) + "px"; }
    if (value.px == 0.0f) { return format_number(value.percent) + "%"; }
    // The two-term canonical form, with the sign folded into the operator the way
    // Chrome prints it: `calc(100% - 12px)`, never `calc(100% + -12px)`.
    const bool negative = value.px < 0;
    return "calc(" + format_number(value.percent) + "% " + (negative ? "- " : "+ ") +
           format_number(negative ? -value.px : value.px) + "px)";
}

bool may_have_calc(std::string_view value) noexcept {
    for (std::size_t i = 0; i + 5 <= value.size(); ++i) {
        if (ascii_iequals(value.substr(i, 5), "calc(")) { return true; }
    }
    return false;
}

std::string fold_calc(std::string_view value, const length_context & ctx) {
    std::string out;
    std::size_t at = 0;
    while (at < value.size()) {
        // A `calc(` that starts a FUNCTION, which means it is not preceded by an
        // identifier character - `-webkit-calc(` and a custom property called
        // `--my-calc` both contain the five bytes and neither is a calc.
        const auto name_char = [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   c == '-' || c == '_';
        };
        const bool boundary = at == 0 || !name_char(value[at - 1]);
        if (!boundary || !ascii_iequals(value.substr(at, 5), "calc(")) {
            out.push_back(value[at]);
            ++at;
            continue;
        }
        // Match the parenthesis, skipping quoted runs so a `)` inside a string
        // cannot end the expression early.
        std::size_t scan = at + 5;
        int depth = 1;
        char quote = 0;
        while (scan < value.size() && depth > 0) {
            const char c = value[scan];
            if (quote != 0) {
                if (c == '\\' && scan + 1 < value.size()) { ++scan; }
                else if (c == quote) { quote = 0; }
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')') {
                --depth;
            }
            ++scan;
        }
        if (depth != 0) { // unterminated: leave the rest of the value alone
            out.append(value.substr(at));
            break;
        }
        const std::string_view body = value.substr(at + 5, scan - at - 6);
        const std::optional<calc_result> answer = evaluate_calc(body, ctx);
        // AN INVALID CALC KEEPS ITS TEXT. Whoever reads the value next cannot
        // parse it and drops the declaration, which is what happened before this
        // function existed - the failure mode is a missing property, never a
        // wrong number.
        out.append(answer ? serialize_calc(*answer) : std::string{value.substr(at, scan - at)});
        at = scan;
    }
    return out;
}

} // namespace ctbrowser::style::css
