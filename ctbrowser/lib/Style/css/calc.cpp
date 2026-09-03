#include <ctbrowser/style/css/calc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
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

    [[nodiscard]] math_answer run() {
        const std::optional<term> value = sum();
        skip_whitespace();
        // UNRESOLVED WINS OVER INVALID. A comparison that could not be decided
        // here stopped the parse the same way an error does, so the latch has to
        // be read before the missing value is: `min(10px, 5%)` is a valid
        // declaration and reporting it as a syntax error would delete it.
        if (unresolved_) {
            return math_answer{math_outcome::unresolved, {}};
        }
        // A trailing token means the expression did not consume its input -
        // `calc(1px 2px)` - which is an error and not a partial answer.
        if (!ok_ || !value || !at_end()) {
            return math_answer{math_outcome::invalid, {}};
        }
        calc_result out;
        // A NUMBER IS AN ANSWER. `calc()` of a bare number used to be reported as
        // no answer at all, which the cascade read as an invalid declaration and
        // threw away - so `opacity: calc(2 / 4)` and `rgb(calc(0), calc(255),
        // calc(0))` produced nothing. CSS Values 3 §8.1 says a math function may
        // resolve to a <number>; whether the PROPERTY accepts one is a separate
        // question, and math_context is where it is asked.
        out.is_number = value->is_number;
        out.px = value->is_number ? value->number : value->px;
        out.percent = value->percent;
        out.has_percent = value->has_percent;
        return math_answer{math_outcome::resolved, out};
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
        case token_type::function: {
            // A NESTED CALC, which Bootstrap writes eight times over
            // (`calc(1em + .5rem + calc(var(x) * 2))`), and the three comparison
            // functions. Any other function - one this does not evaluate - is an
            // error rather than a guess.
            const std::string_view name = t_.text_of(tok);
            if (ascii_iequals(name, "calc(")) { return nested(); }
            if (ascii_iequals(name, "min(")) { return comparison(compare::smallest); }
            if (ascii_iequals(name, "max(")) { return comparison(compare::largest); }
            if (ascii_iequals(name, "clamp(")) { return comparison(compare::clamped); }
            return fail();
        }
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

    enum class compare : std::uint8_t {
        smallest,
        largest,
        clamped
    };

    // min( sum [, sum]* ) | max( sum [, sum]* ) | clamp( sum, sum, sum )
    //
    // CSS Values 4 §10.3. Two things make this more than a fold over `sum()`:
    //
    // EVERY ARGUMENT MUST BE THE SAME TYPE. `min(1px, 2)` compares a length with
    // a number and has no meaning, exactly as `1px + 2` has none.
    //
    // A PERCENTAGE ANYWHERE MAKES THE COMPARISON UNDECIDABLE HERE. `min(10px,
    // 5%)` is 10px on a 200px containing block and 5% of it on a 100px one -
    // there is no answer until layout, and §10.11 says so: the computed value of
    // a math function whose percentages did not resolve is the function itself.
    // That is `unresolved`, NOT an error, and the difference is a declaration
    // kept versus a declaration deleted.
    [[nodiscard]] std::optional<term> comparison(compare kind) {
        ++at_; // the function token, `(` included
        std::vector<term> args;
        for (;;) {
            const std::optional<term> one = sum();
            if (!one) { return std::nullopt; }
            args.push_back(*one);
            skip_whitespace();
            if (peek().type == token_type::comma) {
                ++at_;
                continue;
            }
            break;
        }
        if (peek().type != token_type::close_paren) { return fail(); }
        ++at_;
        if (args.empty()) { return fail(); }
        // `clamp()` takes exactly three arguments and the others take at least
        // one. An arity error is a syntax error, not an undecidable comparison.
        if (kind == compare::clamped && args.size() != 3) { return fail(); }
        for (const term & one : args) {
            if (one.is_number != args.front().is_number) { return fail(); }
            if (one.has_percent) { return unresolvable(); }
        }
        const auto value_of = [](const term & one) { return one.is_number ? one.number : one.px; };
        if (kind == compare::clamped) {
            // clamp(low, value, high) is max(low, min(value, high)) - and the
            // spec's order matters when low > high: the LOW bound wins, because
            // the min is taken first.
            const float low = value_of(args[0]);
            const float mid = value_of(args[1]);
            const float high = value_of(args[2]);
            return with_value(args[1], std::max(low, std::min(mid, high)));
        }
        float best = value_of(args.front());
        for (const term & one : args) {
            const float v = value_of(one);
            best = kind == compare::smallest ? std::min(best, v) : std::max(best, v);
        }
        return with_value(args.front(), best);
    }

    // A term of the same TYPE as `like`, carrying `value`. The type is what the
    // caller has already checked is uniform across the argument list; only the
    // number changes.
    [[nodiscard]] static term with_value(const term & like, float value) {
        term out;
        out.is_number = like.is_number;
        if (like.is_number) {
            out.number = value;
        } else {
            out.px = value;
        }
        return out;
    }

    [[nodiscard]] std::optional<term> fail() {
        ok_ = false;
        return std::nullopt;
    }

    // Well formed, and without an answer here. It stops the parse like an error
    // does - there is nothing to carry upwards - but `run()` reads this latch
    // first, so the caller is told to keep the text rather than to drop it.
    [[nodiscard]] std::optional<term> unresolvable() {
        unresolved_ = true;
        return std::nullopt;
    }

    const token_stream & t_;
    const length_context & ctx_;
    std::size_t at_ = 0;
    bool ok_ = true;
    bool unresolved_ = false;
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
    if (ascii_iequals(unit, "ch") || ascii_iequals(unit, "ex")) {
        return value * ctx.font_size / 2;
    }
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

math_answer evaluate_math(std::string_view expression, const length_context & ctx) {
    const token_stream tokens = tokenize(expression);
    evaluator run{tokens, ctx};
    return run.run();
}

std::optional<calc_result> evaluate_calc(std::string_view expression, const length_context & ctx) {
    const math_answer answer = evaluate_math(expression, ctx);
    if (answer.outcome != math_outcome::resolved || answer.value.is_number) { return std::nullopt; }
    return answer.value;
}

// THE PROPERTIES WHOSE WHOLE VALUE IS LENGTHS. Sorted and searched linearly,
// because the list is short and this runs once per math function rather than
// once per declaration.
//
// The rule for being ON it is narrow on purpose: every component of the value
// must be a <length>, a <percentage> or a keyword, so that a <number> answer
// from a math function can only ever be a syntax error. Anything compound -
// `box-shadow`, `background-position`, `transform`, `border` - is deliberately
// absent, because math_context applies to EVERY math function in the value and
// one of them may legitimately be a number (`transform: scale(calc(1 / 2))`).
//
// `line-height` is the one that looks like it belongs here and does not: a
// unitless `line-height: 1.5` is a number and is the commonest spelling of it.
math_context math_context_of(std::string_view property) noexcept {
    static constexpr std::string_view lengths[] = {
        "block-size",
        "border-bottom-left-radius",
        "border-bottom-right-radius",
        "border-bottom-width",
        "border-end-end-radius",
        "border-end-start-radius",
        "border-left-width",
        "border-radius",
        "border-right-width",
        "border-spacing",
        "border-start-end-radius",
        "border-start-start-radius",
        "border-top-left-radius",
        "border-top-right-radius",
        "border-top-width",
        "border-width",
        "bottom",
        "column-gap",
        "column-rule-width",
        "column-width",
        "flex-basis",
        "font-size",
        "gap",
        "height",
        "inline-size",
        "inset",
        "inset-block",
        "inset-block-end",
        "inset-block-start",
        "inset-inline",
        "inset-inline-end",
        "inset-inline-start",
        "left",
        "letter-spacing",
        "margin",
        "margin-block",
        "margin-block-end",
        "margin-block-start",
        "margin-bottom",
        "margin-inline",
        "margin-inline-end",
        "margin-inline-start",
        "margin-left",
        "margin-right",
        "margin-top",
        "max-block-size",
        "max-height",
        "max-inline-size",
        "max-width",
        "min-block-size",
        "min-height",
        "min-inline-size",
        "min-width",
        "outline-offset",
        "outline-width",
        "padding",
        "padding-block",
        "padding-block-end",
        "padding-block-start",
        "padding-bottom",
        "padding-inline",
        "padding-inline-end",
        "padding-inline-start",
        "padding-left",
        "padding-right",
        "padding-top",
        "right",
        "row-gap",
        "text-indent",
        "top",
        "width",
        "word-spacing",
    };
    for (const std::string_view one : lengths) {
        if (ascii_iequals(one, property)) { return math_context::length; }
    }
    return math_context::any;
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
    // A NUMBER HAS NO UNIT. `opacity: calc(2 / 4)` is `0.5`, and appending `px`
    // to it would be a different kind of wrong from dropping it - a value layout
    // and the cascade would both happily misread.
    if (value.is_number) { return format_number(value.px); }
    if (!value.has_percent) { return format_number(value.px) + "px"; }
    if (value.px == 0.0f) { return format_number(value.percent) + "%"; }
    // The two-term canonical form, with the sign folded into the operator the way
    // Chrome prints it: `calc(100% - 12px)`, never `calc(100% + -12px)`.
    const bool negative = value.px < 0;
    return "calc(" + format_number(value.percent) + "% " + (negative ? "- " : "+ ") +
           format_number(negative ? -value.px : value.px) + "px)";
}

namespace {

// The four function names this folds, longest first so that a scan which stops
// at the first match cannot mistake `min(` for the start of `minmax(` - which it
// cannot anyway, because `minmax(` fails the identifier-boundary test below, but
// the ordering costs nothing and states the intent.
constexpr std::string_view math_names[] = {"clamp(", "calc(", "min(", "max("};

// A `(`-terminated function name AT `at`, or an empty view. The boundary test is
// the whole point: `-webkit-calc(` and a custom property called `--my-calc` both
// contain the five bytes of `calc(` and neither is one, and `minmax(100px, 1fr)`
// contains `max(` three bytes in.
[[nodiscard]] std::string_view math_name_at(std::string_view value, std::size_t at) noexcept {
    const auto name_char = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               c == '-' || c == '_';
    };
    if (at != 0 && name_char(value[at - 1])) { return {}; }
    for (const std::string_view name : math_names) {
        if (ascii_iequals(value.substr(at, name.size()), name)) { return name; }
    }
    return {};
}

} // namespace

bool may_have_math(std::string_view value) noexcept {
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (!math_name_at(value, i).empty()) { return true; }
    }
    return false;
}

folded_value fold_math(std::string_view value, const length_context & ctx, math_context accepts) {
    std::string out;
    bool ok = true;
    std::size_t at = 0;
    while (at < value.size()) {
        const std::string_view name = math_name_at(value, at);
        if (name.empty()) {
            out.push_back(value[at]);
            ++at;
            continue;
        }
        // Match the parenthesis, skipping quoted runs so a `)` inside a string
        // cannot end the expression early.
        std::size_t scan = at + name.size();
        int depth = 1;
        char quote = 0;
        while (scan < value.size() && depth > 0) {
            const char c = value[scan];
            if (quote != 0) {
                if (c == '\\' && scan + 1 < value.size()) {
                    ++scan;
                } else if (c == quote) {
                    quote = 0;
                }
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')') {
                --depth;
            }
            ++scan;
        }
        const std::string_view whole = value.substr(at, scan - at);
        if (depth != 0) { // unterminated: leave the rest of the value alone
            out.append(value.substr(at));
            ok = false;
            break;
        }
        // The WHOLE function, name included, for everything but `calc(`: the
        // evaluator reads `min(...)` as a term, and handing it only the argument
        // list would turn `min(1px, 2px)` into the comma-separated nonsense
        // `1px, 2px`.
        const bool is_calc = ascii_iequals(name, "calc(");
        const std::string_view body = is_calc ? value.substr(at + 5, scan - at - 6) : whole;
        const math_answer answer = evaluate_math(body, ctx);
        // A NUMBER WHERE THE PROPERTY WANTS A LENGTH IS A SYNTAX ERROR. This is
        // the guard that makes it safe for the evaluator to answer with numbers
        // at all: `width: calc(2 * 3)` stays invalid, as CSS says and as this
        // engine already behaved, while `opacity: calc(2 * 3)` becomes `6`.
        const bool wrong_kind = answer.outcome == math_outcome::resolved &&
                                answer.value.is_number && accepts == math_context::length;
        if (answer.outcome == math_outcome::resolved && !wrong_kind) {
            out.append(serialize_calc(answer.value));
            at = scan;
            continue;
        }
        // AN INVALID CALC KEEPS ITS TEXT AND SAYS SO. Keeping the text is what
        // lets a caller with no better answer carry on; saying so is what lets the
        // cascade do the right thing instead, which is to treat the declaration as
        // invalid. Layout reading the text was the wrong outcome: parse_length
        // cannot read `calc(-1 * 0)` and answers `auto`, where CSS says the
        // property takes its initial value.
        //
        // A COMPARISON FUNCTION NEVER SAYS SO. `min()`, `max()` and `clamp()`
        // were kept verbatim by every version of this file before they could be
        // parsed, so "keep the text, do not condemn the declaration" is the one
        // answer that cannot regress a page - and for `min(10px, 5%)` it is also
        // the answer CSS Values 4 §10.11 gives.
        if (!is_calc || answer.outcome == math_outcome::unresolved) {
            out.append(whole);
            at = scan;
            continue;
        }
        ok = false;
        out.append(whole);
        at = scan;
    }
    return folded_value{std::move(out), ok};
}

} // namespace ctbrowser::style::css
