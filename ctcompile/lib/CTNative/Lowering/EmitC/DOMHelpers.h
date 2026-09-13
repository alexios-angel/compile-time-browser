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
inline bool toggle_class(ctbrowser::element_ref element, std::string_view token) {
    auto result = ctbrowser::toggle_token(*element.owner, element.id,
        element.owner->atoms().intern("class"), token).value();
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

} // namespace ctcompile::ctnative::lowering_detail
