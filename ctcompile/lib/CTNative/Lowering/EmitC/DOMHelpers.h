#pragma once

#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative::lowering_detail {

inline constexpr llvm::StringLiteral kDOMEntryHelpers = R"cpp(
namespace ctnative {
inline void require_element(ctbrowser::element_ref element) {
    ctbrowser::validate_element(element).value();
}
} // namespace ctnative
)cpp";

inline constexpr llvm::StringLiteral kDOMToggleHelpers = R"cpp(
namespace ctnative {
inline bool toggle_class(ctbrowser::element_ref element, std::string_view token,
                         std::optional<bool> force = std::nullopt) {
    auto result = ctbrowser::toggle_token(*element.owner, element.id,
        element.owner->atoms().intern("class"), token, force).value();
    result.update.value();
    return result.present;
}
} // namespace ctnative
)cpp";

inline constexpr llvm::StringLiteral kDOMAttributeReadHelpers = R"cpp(
namespace ctnative {
inline std::optional<std::string> get_attribute(ctbrowser::element_ref element,
                                               std::string_view name) {
    return ctbrowser::get_element_attribute(*element.owner, element.id, name);
}
} // namespace ctnative
)cpp";

inline constexpr llvm::StringLiteral kDOMAttributeHelpers = R"cpp(
namespace ctnative {
inline void set_attribute(ctbrowser::element_ref element, std::string_view name,
                          std::string_view text) {
    ctbrowser::set_element_attribute(*element.owner, element.id, name, text).value();
}
inline void set_attribute(ctbrowser::element_ref element, std::string_view name, bool value) {
    set_attribute(element, name, value ? std::string_view("true") : std::string_view("false"));
}
} // namespace ctnative
)cpp";

inline constexpr llvm::StringLiteral kDOMAttributeToggleHelpers = R"cpp(
namespace ctnative {
inline bool toggle_attribute(ctbrowser::element_ref element, std::string_view name,
                             std::optional<bool> force = std::nullopt) {
    auto result = ctbrowser::toggle_element_attribute(*element.owner, element.id, name, force).value();
    result.update.value();
    return result.present;
}
} // namespace ctnative
)cpp";

inline constexpr llvm::StringLiteral kDOMAttributePresenceHelpers = R"cpp(
namespace ctnative {
inline bool has_attribute(ctbrowser::element_ref element, std::string_view name) {
    return element.owner->read().has_attribute(element.id,
        ctbrowser::attribute_key(*element.owner, element.id, name));
}
} // namespace ctnative
)cpp";

inline constexpr llvm::StringLiteral kDOMAttributeRemovalHelpers = R"cpp(
namespace ctnative {
inline void remove_attribute(ctbrowser::element_ref element, std::string_view name) {
    element.owner->remove_attribute(element.id,
        ctbrowser::attribute_key(*element.owner, element.id, name)).value();
}
} // namespace ctnative
)cpp";

inline constexpr llvm::StringLiteral kDOMContainsHelpers = R"cpp(
namespace ctnative {
inline bool contains(ctbrowser::element_ref element, ctbrowser::element_ref other) {
    return element.owner == other.owner &&
        element.owner->read().is_ancestor_of(element.id, other.id);
}
} // namespace ctnative
)cpp";

inline constexpr llvm::StringLiteral kDOMSelectorHelpers = R"cpp(
namespace ctnative {
inline void require_style(ctbrowser::element_ref element, ctbrowser::style::engine & style) {
    if (&style.atoms() != &element.owner->atoms()) {
        throw std::invalid_argument("DOM selector engine uses another atom table");
    }
}
inline ctbrowser::style::css::stylesheet parse_selector(ctbrowser::element_ref element,
                                                       std::string_view selector) {
    bool bad = false;
    auto parsed = ctbrowser::style::css::parse_selector_text(selector, element.owner->atoms(), bad);
    if (bad) { throw std::invalid_argument("DOM selector is invalid"); }
    return parsed;
}
} // namespace ctnative
)cpp";

inline constexpr llvm::StringLiteral kDOMMatchesHelpers = R"cpp(
namespace ctnative {
inline bool matches(ctbrowser::element_ref element, ctbrowser::style::engine & style,
                    std::string_view selector) {
    const auto parsed = parse_selector(element, selector);
    return style.element_matches(element.owner->read(), element.id, parsed.selectors);
}
} // namespace ctnative
)cpp";

inline constexpr llvm::StringLiteral kDOMClosestHelpers = R"cpp(
namespace ctnative {
inline ctbrowser::element_ref closest(ctbrowser::element_ref element,
                                     ctbrowser::style::engine & style, std::string_view selector) {
    const auto parsed = parse_selector(element, selector);
    const auto found = style.closest(element.owner->read(), element.id, parsed.selectors);
    return found ? ctbrowser::element_ref{element.owner, found} : ctbrowser::element_ref{};
}
} // namespace ctnative
)cpp";

} // namespace ctcompile::ctnative::lowering_detail
