//===- atom.hpp - Interned strings ---------------------------- -*- C++ -*-===//
//
// Interned strings. An atom is a 32-bit id; equality is an integer compare, and
// the id can key a hash map directly.
//
// Interning is write-rare and read-hot, so the table takes a shared_mutex and
// readers almost never block each other. Storage is a deque because atoms hand
// out string_views into it: those must stay valid as the table grows, which a
// vector cannot promise.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <shared_mutex>
#include <string>
#include <string_view>

#include <ctbrowser/core/containers.hpp>

namespace ctbrowser {

/// An interned string's id within one atom_table. Converts to false when empty.
struct atom {
    std::uint32_t id = 0; ///< 0 is the empty atom.

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return id != 0; }
    [[nodiscard]] friend constexpr bool operator==(atom, atom) noexcept = default;
    [[nodiscard]] friend constexpr auto operator<=>(atom, atom) noexcept = default;
};

/// Owns the interned strings and maps text to atoms and back. Thread-safe.
class atom_table {
public:
    atom_table();

    atom_table(const atom_table &) = delete;
    atom_table & operator=(const atom_table &) = delete;

    /// The atom for `text`, adding it if it is new.
    [[nodiscard]] atom intern(std::string_view text);

    /// intern() of `text` ASCII-lowercased, for HTML tag and attribute names.
    [[nodiscard]] atom intern_lower(std::string_view text);

    /// The text of `a`; valid for the table's lifetime.
    [[nodiscard]] std::string_view text(atom a) const;

    [[nodiscard]] std::size_t size() const;

private:
    mutable std::shared_mutex mutex_;
    std::deque<std::string> storage_; // Stable addresses; string_views point here.
    flat_map<std::string_view, std::uint32_t> index_;
};

} // namespace ctbrowser

template <> struct std::hash<ctbrowser::atom> {
    [[nodiscard]] std::size_t operator()(ctbrowser::atom a) const noexcept {
        return std::hash<std::uint32_t>{}(a.id);
    }
};
