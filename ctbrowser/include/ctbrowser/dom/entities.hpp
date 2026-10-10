//===- entities.hpp - HTML named character references --------- -*- C++ -*-===//
//
// The WHATWG named character reference table (2125 names, the
// semicolon-terminated forms) and its lookup.
//
// A copy of include/cthtml/entities.hpp from the compile-time-html repository
// (copied, like raster/text/font8x8.hpp, so the engine does not depend on that
// repository's headers). The table in entities/*.inc was generated there from
// CPython's html.entities module; do not edit it by hand.
//
//===----------------------------------------------------------------------===//

#ifndef CTBROWSER_V2_DOM_ENTITIES_HPP
#define CTBROWSER_V2_DOM_ENTITIES_HPP

#include <algorithm>
#include <string_view>

namespace ctbrowser::html_entities {

/// One named reference: `name` (without `&`) decodes to one or two code
/// points; `second` is 0 when there is only one.
struct entity_ref {
    std::string_view name;
    char32_t first;
    char32_t second;
};

/// Sorted by name in byte order (names are case-sensitive: &Uuml; and &uuml;
/// differ), so find_entity can binary-search.

inline constexpr entity_ref entity_table[] = {
#include "entities/entity_table-01.inc"
#include "entities/entity_table-02.inc"
#include "entities/entity_table-03.inc"
};

/// The entry for `name`, or nullptr.
constexpr const entity_ref * find_entity(std::string_view name) noexcept {
    const auto it = std::ranges::lower_bound(entity_table, name, {}, &entity_ref::name);
    return it != std::ranges::end(entity_table) && it->name == name ? it : nullptr;
}

} // namespace ctbrowser::html_entities

#endif
