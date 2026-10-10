//===- handle.hpp - Generation-tagged handles ----------------- -*- C++ -*-===//
//
// A generation-tagged reference into a slab (slab.hpp).
//
//===----------------------------------------------------------------------===//

#pragma once
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace ctbrowser {

/// A generation-tagged reference into a slab. The engine passes these instead
/// of pointers: a slot can be freed and reused while something still refers to
/// it, and a handle carries the generation the slot had when the handle was
/// made, so a lookup against a reused slot fails rather than returning another
/// object.
///
/// `Tag` makes handles of different subsystems distinct types, so passing a
/// node_id where a layer_id is expected is a compile error.
template <typename Tag> struct handle {
    std::uint32_t slot = 0;
    std::uint32_t generation = 0; ///< 0 is reserved: a zeroed handle is null.

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return generation != 0; }
    [[nodiscard]] friend constexpr auto operator<=>(handle, handle) noexcept = default;
    [[nodiscard]] friend constexpr bool operator==(handle, handle) noexcept = default;

    /// Slot and generation packed into one integer, unique per handle; used to
    /// key hash maps.
    [[nodiscard]] constexpr std::uint64_t key() const noexcept {
        return (static_cast<std::uint64_t>(slot) << 32) | generation;
    }
};

} // namespace ctbrowser

// Hashable, so handles can key the unordered_flat_maps in style and script.
template <typename Tag> struct std::hash<ctbrowser::handle<Tag>> {
    [[nodiscard]] std::size_t operator()(ctbrowser::handle<Tag> h) const noexcept {
        return std::hash<std::uint64_t>{}(h.key());
    }
};
