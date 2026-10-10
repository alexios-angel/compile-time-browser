//===- ctbrowser/lib/Style/engine/cascade_values.cpp - Value folding ------===//
//
// Computed-value helpers for the cascade: shorthand splitting through the CSSOM
// grammar, per-property length folding and clamping (fold_value), and viewport
// and container-query unit handling. Declared in cascade_values.hpp.
//
//===----------------------------------------------------------------------===//

#include "cascade_values.hpp"

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/calc.hpp>
#include <ctbrowser/style/css/token.hpp>

#include <cmath>

namespace ctbrowser::style::cascade_detail {

// Every shorthand that expand_shorthand does not split goes through the
// CSSOM's declaration block, which knows each one's grammar
// (properties/shorthands.cpp): `font`, `white-space`, `animation`, the
// flow-relative margins, insets and borders, `place-*`, `columns` and so on.
// What comes back is validated and canonical; a value the grammar refuses, or
// a shape the block keeps whole, gives an empty block and the declaration
// stays as it was.
css::declaration_block split_via_block(std::string_view name, std::string_view text) {
    css::declaration_block block;
    if (css::longhands_of(name).empty()) { return block; } // A longhand.
    (void)css::set_declaration(block, name, text, false);
    if (block.size() == 1 && ascii_iequals(block[0].name, name)) { block.clear(); }
    return block;
}

bool has_viewport_unit(std::string_view text) {
    if (text.find_first_of("vV") == std::string_view::npos) { return false; }
    const css::token_stream ts = css::tokenize(text);
    for (const css::css_token & t : ts.tokens) {
        if (t.type == css::token_type::eof) { break; }
        if (t.type != css::token_type::dimension) { continue; }
        std::string_view unit = ts.unit_of(t);
        if (unit.size() > 2 && ascii_iequals_any(unit.substr(0, 1), {"s", "l", "d"})) {
            unit.remove_prefix(1);
        }
        if (ascii_iequals_any(unit, {"vw", "vh", "vi", "vb", "vmin", "vmax"})) { return true; }
    }
    return false;
}

std::string resolve_container_units(std::string text, const css::length_context & lengths) {
    if (text.find("cq") == std::string::npos && text.find("CQ") == std::string::npos) {
        return text;
    }
    const css::token_stream ts = css::tokenize(text);
    std::string out;
    bool changed = false;
    for (const css::css_token & t : ts.tokens) {
        if (t.type == css::token_type::eof) { break; }
        const std::string_view piece = ts.text_of(t);
        if (t.type == css::token_type::dimension && ascii_istarts_with(ts.unit_of(t), "cq")) {
            // Evaluated in double precision, as a folded calc() is: `10cqh` of
            // 768 is 76.8px, not the float's 76.800003px.
            const css::math_answer answer = css::evaluate_math(piece, lengths);
            if (answer.outcome == css::math_outcome::resolved && !answer.value.is_number &&
                !answer.value.has_percent) {
                out += css::serialize_number(answer.value.px) + "px";
                changed = true;
                continue;
            }
        }
        out += piece;
    }
    return changed ? out : text;
}

std::string fold_value(std::string text, std::string_view name,
                       const css::length_context & lengths) {
    if (name == "background-size" || name == "mask-size") {
        // Each layer resolves and clamps before inheritance; anything
        // computed_background_list cannot handle falls through to the generic
        // folding below.
        const css::color_context ctx{{}, &lengths};
        if (std::string computed = css::computed_background_list(name, text, ctx);
            !computed.empty()) {
            return computed;
        }
    }
    const css::property_syntax * known = css::find_property(name);
    const auto component = [&](std::string_view part) {
        std::string value =
            css::canonical_dimension_text(part, lengths).value_or(std::string{part});
        return known != nullptr && known->nonnegative ? css::non_negative(value) : value;
    };
    if (name == "background-position" || name == "background-position-x" ||
        name == "background-position-y" || name == "mask-position") {
        // Lengths compute before inheritance, including offsets and comma lists.
        // Keep logical keywords for the resolver that knows the writing mode.
        const css::token_stream ts = css::tokenize(text);
        std::string result;
        for (const css::css_token & t : ts.tokens) {
            if (t.type == css::token_type::eof) { break; }
            const std::string_view part = ts.text_of(t);
            result += t.type == css::token_type::dimension ? component(part) : std::string{part};
        }
        return result;
    }
    const bool radius = name.starts_with("border-") && name.ends_with("-radius");
    if (radius || name == "border-image-width" || name == "border-image-outset" ||
        name == "border-image-slice" || name == "text-decoration-inset" || name == "text-indent" ||
        name == "overflow-clip-margin" || name == "mask-border-width" ||
        name == "mask-border-outset" || name == "mask-border-slice") {
        // Every component computes before inheritance; scalar folding
        // cannot resolve or clamp a list as a single length.
        std::vector<std::string> parts;
        for (const std::string_view part : split_top_level(text, html_whitespace)) {
            parts.push_back(component(part));
        }
        if (radius && parts.size() == 2 && parts[0] == parts[1]) { parts.pop_back(); }
        std::string result;
        for (const std::string & part : parts) {
            if (!result.empty()) { result += ' '; }
            result += part;
        }
        if (!radius) {
            const css::value_check checked = css::check_declaration(name, result, false);
            if (checked.valid) { return checked.serialized; }
        }
        return result;
    }
    if (name == "hyphenate-limit-chars" || name == "counter-increment" || name == "counter-reset" ||
        name == "counter-set") {
        // <integer>s among other words: a math function computes to its
        // value, rounded (CSS Values 4 §10.9), and the whole is written
        // canonically.
        std::string result;
        for (const std::string_view raw : split_top_level(text, html_whitespace)) {
            const std::string_view part = trim(raw, html_whitespace);
            if (part.empty()) { continue; }
            std::string one{part};
            if (css::may_have_math(part)) {
                const css::math_answer n = css::evaluate_math(part, lengths);
                if (n.outcome == css::math_outcome::resolved) {
                    one = css::serialize_number(std::floor(n.value.px + 0.5));
                }
            }
            result += (result.empty() ? "" : " ") + one;
        }
        const css::value_check checked = css::check_declaration(name, result, false);
        return checked.valid ? checked.serialized : result;
    }
    // A comma list computes item by item: `transition-delay: -500ms, 6s` is
    // `-0.5s, 6s`, as each item alone would be.
    if (const auto items = split_top_level(text, ","); items.size() > 1) {
        std::string result;
        bool changed = false;
        for (const std::string_view raw : items) {
            const std::string_view item = trim(raw, html_whitespace);
            const std::string one = component(item);
            changed = changed || one != item;
            result += (result.empty() ? "" : ", ") + one;
        }
        return changed ? result : text;
    }
    // Shorthands have no range of their own, so a value is clamped only here,
    // after expansion, including components whose math was already folded.
    return component(text);
}

} // namespace ctbrowser::style::cascade_detail
