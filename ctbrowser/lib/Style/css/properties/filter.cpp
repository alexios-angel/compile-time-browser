// `<filter-value-list>`, Filter Effects 1 §11: `none | [ <filter-function> |
// <url> ]+`, for `filter` and `backdrop-filter`.
//
//   blur( <length>? )                 brightness( <number-percentage>? )
//   contrast( ... )  grayscale( ... )  invert( ... )  opacity( ... )
//   saturate( ... )  sepia( ... )      hue-rotate( <angle>? )
//   drop-shadow( <color>? && <length>{2,3} )
//
// The SPECIFIED value keeps the author's unit and percentage, fills in an
// omitted argument, clamps the four whose range is [0, 1] at their ceiling
// and refuses a negative literal; the COMPUTED value writes numbers for the
// percentages, pixels and degrees, a shadow's colour first with its blur
// filled in, and clamps a folded calc() into the function's range.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

// The functions whose argument is a `<number-percentage>`, and whether the
// range is capped at one (`grayscale(300%)` is `grayscale(100%)`) or open
// (`brightness(300%)`).
[[nodiscard]] std::optional<bool> capped_amount(std::string_view fn) {
    if (ascii_iequals_any(fn, {"grayscale", "invert", "opacity", "sepia"})) { return true; }
    if (ascii_iequals_any(fn, {"brightness", "contrast", "saturate"})) { return false; }
    return std::nullopt;
}

struct filter_context {
    const length_context * lengths = nullptr;
    std::string_view current_color;
};

[[nodiscard]] std::string_view slice(const token_stream & ts, std::size_t begin, std::size_t end) {
    const css_token & first = ts.tokens[begin];
    const css_token & last = ts.tokens[end - 1];
    return std::string_view{ts.pool}.substr(first.text, last.text + last.length - first.text);
}

// One `<number-percentage>` argument, or an omitted one, as the function's
// text. `capped` clamps to one; a negative literal is a syntax error and a
// negative calc() clamps to nought.
[[nodiscard]] std::optional<std::string> amount(std::string_view text, bool capped,
                                                const filter_context & ctx) {
    const token_stream ts = tokenize(text);
    const bool computed = ctx.lengths != nullptr;
    if (ts.tokens.size() == 2 && (ts.tokens.front().type == token_type::number ||
                                  ts.tokens.front().type == token_type::percentage)) {
        const css_token & t = ts.tokens.front();
        const bool percent = t.type == token_type::percentage;
        if (t.number < 0) { return std::nullopt; }
        double v = t.number;
        if (capped) { v = std::min(v, percent ? 100.0 : 1.0); }
        if (computed) { return serialize_number(percent ? v / 100.0 : v); }
        return serialize_number(v) + (percent ? "%" : "");
    }
    if (ts.tokens.size() < 2 || ts.tokens.front().type != token_type::function ||
        !may_have_math(text)) {
        return std::nullopt;
    }
    const length_context none;
    const math_answer answer = evaluate_math(text, computed ? *ctx.lengths : none);
    if (answer.outcome == math_outcome::invalid) { return std::nullopt; }
    if (answer.outcome == math_outcome::resolved) {
        const bool percent_only = answer.value.has_percent && answer.value.px == 0.0;
        if (!answer.value.is_number && !percent_only) { return std::nullopt; }
        if (computed) {
            double v = percent_only ? answer.value.percent / 100.0 : answer.value.px;
            if (std::isnan(v)) { v = 0; }
            v = std::max(0.0, v);
            if (capped) { v = std::min(v, 1.0); }
            return serialize_number(v);
        }
    }
    return simplify_math(text);
}

// A `<length>` (blur, a shadow offset) or an `<angle>` (hue-rotate), kept
// with its unit when specified and canonical when computed.
[[nodiscard]] std::optional<std::string> dimension(std::string_view text, bool angle,
                                                   bool non_negative, const filter_context & ctx) {
    const token_stream ts = tokenize(text);
    const bool computed = ctx.lengths != nullptr;
    const length_context none;
    const length_context & bases = computed ? *ctx.lengths : none;
    if (ts.tokens.size() == 2) {
        const css_token & t = ts.tokens.front();
        if (t.type == token_type::number && t.number == 0) { return angle ? "0deg" : "0px"; }
        if (t.type != token_type::dimension) { return std::nullopt; }
        const math_answer typed = evaluate_math(text, bases);
        if (typed.outcome == math_outcome::invalid) { return std::nullopt; }
        if (typed.outcome == math_outcome::resolved) {
            const bool is_angle = typed.value.type == numeric_type::angle;
            if (is_angle != angle || (!angle && typed.value.type != numeric_type::length)) {
                return std::nullopt;
            }
            if (non_negative && t.number < 0) { return std::nullopt; }
            if (computed) { return serialize_calc(typed.value); }
        }
        return serialize_number(t.number) + ascii_lower_copy(ts.unit_of(t));
    }
    if (ts.tokens.size() < 2 || ts.tokens.front().type != token_type::function ||
        !may_have_math(text)) {
        return std::nullopt;
    }
    const math_answer answer = evaluate_math(text, bases);
    if (answer.outcome == math_outcome::invalid) { return std::nullopt; }
    if (answer.outcome == math_outcome::resolved) {
        const bool is_angle = answer.value.type == numeric_type::angle;
        if (is_angle != angle || answer.value.is_number || answer.value.has_percent) {
            return std::nullopt;
        }
        if (computed) {
            calc_result v = answer.value;
            if (non_negative && v.px < 0) { v.px = 0; }
            return serialize_calc(v);
        }
    }
    return simplify_math(text);
}

// One filter function from its argument tokens (significant indices).
[[nodiscard]] std::optional<std::string> one_filter(const token_stream & ts, std::string_view name,
                                                    const std::vector<std::size_t> & args,
                                                    const filter_context & ctx) {
    const std::string fn = ascii_lower_copy(name);
    const bool computed = ctx.lengths != nullptr;
    const auto whole = [&]() -> std::optional<std::string_view> {
        if (args.empty()) { return std::string_view{}; }
        for (const std::size_t i : args) {
            if (ts.tokens[i].text >= ts.source_length) { return std::nullopt; }
        }
        return slice(ts, args.front(), args.back() + 1);
    };
    if (const std::optional<bool> capped = capped_amount(fn)) {
        if (args.empty()) { return fn + (computed ? "(1)" : "()"); }
        const std::optional<std::string_view> text = whole();
        if (!text) { return std::nullopt; }
        const std::optional<std::string> a = amount(*text, *capped, ctx);
        if (!a) { return std::nullopt; }
        return fn + "(" + *a + ")";
    }
    if (fn == "blur" || fn == "hue-rotate") {
        const bool angle = fn == "hue-rotate";
        if (args.empty()) { return fn + (computed ? (angle ? "(0deg)" : "(0px)") : "()"); }
        const std::optional<std::string_view> text = whole();
        if (!text) { return std::nullopt; }
        const std::optional<std::string> d = dimension(*text, angle, !angle, ctx);
        if (!d) { return std::nullopt; }
        return fn + "(" + *d + ")";
    }
    if (fn == "drop-shadow") {
        // `<color>? && <length>{2,3}`: the colour wherever it sits, first when written.
        std::string colour;
        std::vector<std::string> lengths;
        for (std::size_t k = 0; k < args.size();) {
            const css_token & t = ts.tokens[args[k]];
            std::size_t end = k + 1;
            if (t.type == token_type::function) {
                int depth = 0;
                for (std::size_t j = args[k]; j < ts.tokens.size(); ++j) {
                    const token_type type = ts.tokens[j].type;
                    if (type == token_type::function || type == token_type::open_paren) { ++depth; }
                    if (type == token_type::close_paren && --depth == 0) {
                        end = k;
                        while (end < args.size() && args[end] <= j) { ++end; }
                        break;
                    }
                }
            }
            if (ts.tokens[args[k]].text >= ts.source_length) { return std::nullopt; }
            const std::string_view text = slice(ts, args[k], args[end - 1] + 1);
            const bool colour_like = t.type == token_type::ident || t.type == token_type::hash ||
                                     (t.type == token_type::function && !may_have_math(text));
            if (colour_like) {
                if (!colour.empty()) { return std::nullopt; }
                colour = computed
                             ? computed_color(text, color_context{ctx.current_color, ctx.lengths})
                             : serialize_color(text);
                if (colour.empty()) { return std::nullopt; }
            } else {
                if (lengths.size() == 3) { return std::nullopt; }
                const std::optional<std::string> d =
                    dimension(text, false, lengths.size() == 2, ctx);
                if (!d) { return std::nullopt; }
                lengths.push_back(*d);
            }
            k = end;
        }
        if (lengths.size() < 2) { return std::nullopt; }
        std::string out = "drop-shadow(";
        if (computed && colour.empty()) {
            colour = ctx.current_color.empty() ? std::string{"currentcolor"}
                                               : std::string{ctx.current_color};
        }
        if (!colour.empty()) { out += colour + " "; }
        if (computed && lengths.size() == 2) { lengths.emplace_back("0px"); }
        for (std::size_t i = 0; i < lengths.size(); ++i) {
            out += (i == 0 ? "" : " ") + lengths[i];
        }
        return out + ")";
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> filter_list(const token_stream & ts,
                                                     const filter_context & ctx) {
    std::string out;
    std::size_t i = 0;
    bool any = false;
    while (i + 1 < ts.tokens.size()) {
        const css_token & t = ts.tokens[i];
        if (t.type == token_type::whitespace) {
            ++i;
            continue;
        }
        if (t.type == token_type::ident && ascii_iequals(ts.text_of(t), "none")) {
            if (any) { return std::nullopt; }
            ++i;
            while (i + 1 < ts.tokens.size() && ts.tokens[i].type == token_type::whitespace) { ++i; }
            if (i + 1 != ts.tokens.size()) { return std::nullopt; }
            return "none";
        }
        if (t.type == token_type::url) {
            out += (any ? " " : "") + normalize_value_tokens(ts, ts.text_of(t), nullptr, i, i + 1);
            any = true;
            ++i;
            continue;
        }
        if (t.type != token_type::function) { return std::nullopt; }
        const std::string_view raw = ts.text_of(t);
        const std::string_view name = raw.substr(0, raw.size() - 1);
        // To the matching paren, collecting the significant argument tokens.
        std::vector<std::size_t> args;
        int depth = 1;
        std::size_t j = i + 1;
        for (; j + 1 < ts.tokens.size(); ++j) {
            const token_type type = ts.tokens[j].type;
            if (type == token_type::function || type == token_type::open_paren) { ++depth; }
            if (type == token_type::close_paren && --depth == 0) { break; }
            if (type != token_type::whitespace) { args.push_back(j); }
        }
        if (ascii_iequals(name, "url")) {
            out += (any ? " " : "") + normalize_value_tokens(ts, raw, nullptr, i, j + 1);
        } else {
            const std::optional<std::string> one = one_filter(ts, name, args, ctx);
            if (!one) { return std::nullopt; }
            out += (any ? " " : "") + *one;
        }
        any = true;
        i = j + 1;
    }
    if (!any) { return std::nullopt; }
    return out;
}

} // namespace

namespace detail {

bool match_filter_list(const token_stream & ts, const scan & found, std::string & out) {
    (void)found;
    const std::optional<std::string> answer = filter_list(ts, filter_context{});
    if (!answer) { return false; }
    out = *answer;
    return true;
}

} // namespace detail

std::string computed_filter(std::string_view specified, const color_context & ctx) {
    const length_context fallback;
    filter_context fc;
    fc.lengths = ctx.lengths != nullptr ? ctx.lengths : &fallback;
    fc.current_color = ctx.current_color;
    const token_stream ts = tokenize(trim(specified, html_whitespace));
    return filter_list(ts, fc).value_or(std::string{});
}

} // namespace ctbrowser::style::css
