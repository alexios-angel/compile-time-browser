#pragma once

#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative::deforestation_detail {

// The numeric snapshot indexing contract is vec_at's: check the tag, truncate,
// then bounds-check. In particular -0.5 indexes zero in the current interpreter.
inline constexpr llvm::StringLiteral kProjectionHelpers = R"cpp(
namespace ctnative {
// ctcompile: scalar consumer of a proved unmodified Map snapshot
template <bool Keys, class K, class V> nullable_scalar map_snapshot_at(
    const std::shared_ptr<map_storage<K, V>> & map, nullable_scalar key) {
    if (key.tag != nullable_scalar::kind::number) { return {}; }
    const double index = std::trunc(key.value);
    if (!(index >= 0.0) || index >= static_cast<double>(map->entries.size())) { return {}; }
    const auto & entry = map->entries[static_cast<std::size_t>(index)];
    if constexpr (Keys) { return entry.first; }
    else { return entry.second; }
}
} // namespace ctnative
)cpp";

} // namespace ctcompile::ctnative::deforestation_detail
