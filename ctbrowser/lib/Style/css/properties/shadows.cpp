// `box-shadow` and `text-shadow`, CSS Backgrounds 3 §7.2 and Text Decoration
// 3 §4: `none | <shadow>#` where a shadow is `<color>? && <length>{2,4} &&
// inset?` (three lengths and no `inset` for text). The lengths are one run -
// `4px inset -4px` is not a shadow - and the third may not be a negative
// literal. Serialised colour first, then the lengths, then `inset`.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

[[nodiscard]] std::optional<std::string> one_shadow(const token_stream & ts,
                                                    const std::vector<std::size_t> & at, bool box) {
    std::string colour;
    std::vector<std::string> lengths;
    bool inset = false;
    bool lengths_done = false;
    for (std::size_t k = 0; k < at.size();) {
        const css_token & t = ts.tokens[at[k]];
        if (t.text >= ts.source_length) { return std::nullopt; }
        std::size_t end = k + 1;
        if (t.type == token_type::function) {
            int depth = 0;
            for (std::size_t j = at[k]; j < ts.tokens.size(); ++j) {
                const token_type type = ts.tokens[j].type;
                if (type == token_type::function || type == token_type::open_paren) { ++depth; }
                if (type == token_type::close_paren && --depth == 0) {
                    end = k;
                    while (end < at.size() && at[end] <= j) { ++end; }
                    break;
                }
            }
        }
        const css_token & last = ts.tokens[at[end - 1]];
        if (last.text >= ts.source_length) { return std::nullopt; }
        const std::string_view text =
            std::string_view{ts.pool}.substr(t.text, last.text + last.length - t.text);
        const bool is_length = t.type == token_type::dimension ||
                               (t.type == token_type::number && t.number == 0) ||
                               (t.type == token_type::function && may_have_math(text));
        if (is_length) {
            if (lengths_done || lengths.size() == (box ? 4u : 3u)) { return std::nullopt; }
            std::string canonical;
            if (t.type == token_type::number) {
                canonical = "0px";
            } else if (t.type == token_type::dimension) {
                const math_answer typed = evaluate_math(text, length_context{});
                if (typed.outcome == math_outcome::resolved &&
                    typed.value.type != numeric_type::length) {
                    return std::nullopt;
                }
                if (typed.outcome == math_outcome::invalid) { return std::nullopt; }
                if (lengths.size() == 2 && t.number < 0) { return std::nullopt; }
                canonical = serialize_number(t.number) + ascii_lower_copy(ts.unit_of(t));
            } else {
                const math_answer answer = evaluate_math(text, length_context{});
                if (answer.outcome == math_outcome::invalid) { return std::nullopt; }
                if (answer.outcome == math_outcome::resolved &&
                    (answer.value.is_number || answer.value.has_percent ||
                     answer.value.type != numeric_type::length)) {
                    return std::nullopt;
                }
                if (answer.outcome == math_outcome::unresolved) {
                    if (const std::optional<calc_result> kind = math_type_of(text);
                        kind && (kind->is_number || kind->has_percent ||
                                 kind->type != numeric_type::length)) {
                        return std::nullopt;
                    }
                }
                canonical = simplify_math(text);
            }
            lengths.push_back(canonical);
            k = end;
            continue;
        }
        if (!lengths.empty()) { lengths_done = true; }
        if (t.type == token_type::ident && ascii_iequals(text, "inset")) {
            if (!box || inset) { return std::nullopt; }
            inset = true;
            k = end;
            continue;
        }
        if (!colour.empty()) { return std::nullopt; }
        colour = serialize_color(text);
        if (colour.empty()) { return std::nullopt; }
        k = end;
    }
    if (lengths.size() < 2) { return std::nullopt; }
    std::string out = colour;
    for (const std::string & one : lengths) { out += (out.empty() ? "" : " ") + one; }
    if (inset) { out += " inset"; }
    return out;
}

} // namespace

namespace detail {

bool match_shadow_list(std::string_view property, const token_stream & ts, const scan & found,
                       std::string & out) {
    const bool box = ascii_iequals(property, "box-shadow");
    if (found.significant.size() == 1) {
        const css_token & only = ts.tokens[found.significant.front()];
        if (only.type == token_type::ident && ascii_iequals(ts.text_of(only), "none")) {
            out = "none";
            return true;
        }
    }
    std::vector<std::vector<std::size_t>> shadows(1);
    int depth = 0;
    for (const std::size_t i : found.significant) {
        const token_type type = ts.tokens[i].type;
        if (type == token_type::function || type == token_type::open_paren) { ++depth; }
        if (type == token_type::close_paren) { --depth; }
        if (depth == 0 && type == token_type::comma) {
            shadows.emplace_back();
            continue;
        }
        shadows.back().push_back(i);
    }
    out.clear();
    for (const std::vector<std::size_t> & one : shadows) {
        const std::optional<std::string> text = one_shadow(ts, one, box);
        if (!text) {
            out.clear();
            return true; // modelled, and invalid
        }
        out += (out.empty() ? "" : ", ") + *text;
    }
    return true;
}

} // namespace detail

} // namespace ctbrowser::style::css
