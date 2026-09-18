// style::engine - filing a sheet's `@keyframes` rules (CSS Animations 1 §4),
// and looking one up by name for the shell's CSSAnimation.
//
// WHAT A KEYFRAME'S VALUE IS HERE. The same text a rule's declaration would
// have become: a shorthand split into its longhands, each through the
// property table's `check_declaration`, a custom property kept verbatim, an
// unknown property dropped. The shell's Web Animations overlay reads these
// as if a rule had declared them (bindings/animations.cpp), so what applies
// from a keyframe and what applies from a rule are one grammar.

#include <ctbrowser/style/easing.hpp>
#include <ctbrowser/style/engine.hpp>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::style {

void engine::file_keyframes(const css::stylesheet & sheet, std::uint8_t origin,
                            std::size_t condition_base) {
    for (const css::keyframes_block & block : sheet.keyframes) {
        keyframes_rule made;
        made.name = block.name;
        made.origin = origin;
        made.condition = block.condition == 0
                             ? 0u
                             : static_cast<std::uint32_t>(condition_base + block.condition - 1);
        for (const css::keyframe_block & frame : block.frames) {
            // The block's declarations, once, then the same list at every offset
            // its selector named: `0%, 100% { ... }` is two keyframes.
            keyframes_rule::keyframe one;
            const auto put = [&one](std::string_view name, std::string_view text) {
                const auto seen = std::ranges::find_if(
                    one.values, [name](const auto & entry) { return entry.first == name; });
                if (seen == one.values.end()) {
                    one.values.emplace_back(std::string{name}, std::string{text});
                } else {
                    seen->second = std::string{text};
                }
            };
            const std::span<const css::raw_declaration> declarations =
                frame.declaration_count == 0
                    ? std::span<const css::raw_declaration>{}
                    : std::span<const css::raw_declaration>{sheet.declarations}.subspan(
                          frame.first_declaration, frame.declaration_count);
            for (const css::raw_declaration & d : declarations) {
                // `!important` is not allowed in a keyframe (§4.1): the
                // declaration is ignored.
                if (d.important) { continue; }
                const std::string_view property = atoms_->text(d.property);
                const std::string_view text = trim(sheet.text_of(d), html_whitespace);
                if (d.custom) {
                    put(property, text);
                    continue;
                }
                // The two animation properties a keyframe may carry (§4.3);
                // every other `animation-*` in one is ignored.
                if (property == "animation-timing-function") {
                    if (parse_easing(text)) { one.easing = std::string{text}; }
                    continue;
                }
                if (property == "animation-composition") {
                    if (text == "replace" || text == "add" || text == "accumulate") {
                        one.composite = std::string{text};
                    }
                    continue;
                }
                if (property.starts_with("animation")) { continue; }
                const css::property_syntax * known = css::find_property(property);
                if (known == nullptr) { continue; }
                if (known->shorthand) {
                    css::declaration_block expanded;
                    (void)css::set_declaration(expanded, property, text, false);
                    // Some CSSOM shorthands still retain their authored text.
                    if (expanded.size() == 1 && expanded[0].name == property) {
                        expanded.clear();
                        for (const auto & [name, value] :
                             css::expand_cascaded_shorthand(property, text)) {
                            const css::value_check checked = css::check_declaration(name, value);
                            if (checked.valid) {
                                expanded.push_back({std::string{name}, checked.serialized, false});
                            }
                        }
                    }
                    for (const css::declaration & d : expanded) { put(d.name, d.value); }
                    continue;
                }
                const css::value_check checked = css::check_declaration(property, text);
                if (checked.valid) { put(property, checked.serialized); }
            }
            for (const double offset : frame.offsets) {
                // TWO KEYFRAMES AT ONE OFFSET MERGE, the later winning per
                // property (§4.1), and a later easing likewise.
                const auto same = std::ranges::find_if(
                    made.keyframes, [offset](const auto & k) { return k.offset == offset; });
                if (same == made.keyframes.end()) {
                    keyframes_rule::keyframe copy = one;
                    copy.offset = offset;
                    made.keyframes.push_back(std::move(copy));
                    continue;
                }
                for (const auto & [name, text] : one.values) {
                    const auto seen = std::ranges::find_if(
                        same->values, [&name](const auto & e) { return e.first == name; });
                    if (seen == same->values.end()) {
                        same->values.emplace_back(name, text);
                    } else {
                        seen->second = text;
                    }
                }
                if (!one.easing.empty()) { same->easing = one.easing; }
                if (!one.composite.empty()) { same->composite = one.composite; }
            }
        }
        std::ranges::stable_sort(
            made.keyframes, [](const auto & a, const auto & b) { return a.offset < b.offset; });
        keyframes_.push_back(std::move(made));
    }
}

const keyframes_rule * engine::keyframes_of(std::string_view name) const {
    for (auto it = keyframes_.rbegin(); it != keyframes_.rend(); ++it) {
        if (it->name == name && condition_holds(it->condition)) { return &*it; }
    }
    return nullptr;
}

} // namespace ctbrowser::style
