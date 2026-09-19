#include "cascade_values.hpp"

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/calc.hpp>

namespace ctbrowser::style::cascade_detail {

// EVERY SHORTHAND expand_shorthand DOES NOT SPLIT goes through the CSSOM's
// declaration block, which knows each one's grammar
// (properties/shorthands.cpp): `font`, `white-space`, `animation`, the
// flow-relative margins, insets and borders, `place-*`, `columns` ... What
// comes back is validated and canonical; a value the grammar refuses, or a
// shape the block keeps whole, is an empty block and the declaration
// stays as it was.
css::declaration_block split_via_block(std::string_view name, std::string_view text) {
    css::declaration_block block;
    if (css::longhands_of(name).empty()) { return block; } // a longhand
    (void)css::set_declaration(block, name, text, false);
    if (block.size() == 1 && ascii_iequals(block[0].name, name)) { block.clear(); }
    return block;
}

std::string fold_value(std::string text, std::string_view name,
                       const css::length_context & lengths) {
    if (name == "background-size" || name == "mask-size") {
        // Each layer resolves and clamps before inheritance. Keep the
        // shared math fold above for random ordinals and finite bounds.
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
    const bool radius = name.starts_with("border-") && name.ends_with("-radius");
    if (radius || name == "border-image-width" || name == "border-image-outset" ||
        name == "border-image-slice") {
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
    // Shorthands have no range of their own. Clamp after expansion,
    // including components whose math was already folded above.
    return component(text);
}

} // namespace ctbrowser::style::cascade_detail
