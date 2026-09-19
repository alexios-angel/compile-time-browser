#ifndef CTBROWSER_V2_DOM_ENTITIES_HPP
#define CTBROWSER_V2_DOM_ENTITIES_HPP

#include <algorithm>
#include <string_view>

// Carried forward from the compile-time-html repository (include/cthtml/entities.hpp)
// as a COPY, the same way include/ctbrowser/raster/text/font8x8.hpp is: the engine must not include the previous
// engine's headers, and this is generated data that does not change.
//
// The WHATWG named character reference table (2125 names, the
// semicolon-terminated forms), generated in the cthtml repository from
// CPython's html.entities module - do not edit by hand. Entries are sorted by
// name (byte order, names are
// case-SENSITIVE: &Uuml; and &uuml; differ) so lookup is a binary
// search; a reference may decode to one or two code points (second ==
// 0 means one).

namespace ctbrowser::html_entities {

struct entity_ref {
    std::string_view name;
    char32_t first;
    char32_t second;
};

inline constexpr entity_ref entity_table[] = {
#include "entities/entity_table-01.inc"
#include "entities/entity_table-02.inc"
#include "entities/entity_table-03.inc"
};

constexpr const entity_ref * find_entity(std::string_view name) noexcept {
    const auto it = std::ranges::lower_bound(entity_table, name, {}, &entity_ref::name);
    return it != std::ranges::end(entity_table) && it->name == name ? it : nullptr;
}

} // namespace ctbrowser::html_entities

#endif
