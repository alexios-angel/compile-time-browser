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

} // namespace ctcompile::ctnative::lowering_detail
