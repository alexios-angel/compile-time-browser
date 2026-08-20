#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <shared_mutex>
#include <string>
#include <string_view>

#include <ctbrowser/core/containers.hpp>

// Interned strings.
//
// the previous engine compared tag, class and attribute names as std::string, by value, on every
// selector match and every attribute lookup - so the hottest inner loop in the
// style system was strcmp. An atom is a 32-bit id; equality is an integer
// compare, and the id can key a hash map directly.
//
// Interning is write-rare and read-hot, so the table takes a shared_mutex and
// readers almost never block each other. Storage is a deque because atoms hand
// out string_views into it: those must stay valid as the table grows, which a
// vector cannot promise.

namespace ctbrowser {

struct atom {
    std::uint32_t id = 0; // 0 is the empty atom

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return id != 0; }
    [[nodiscard]] friend constexpr bool operator==(atom, atom) noexcept = default;
    [[nodiscard]] friend constexpr auto operator<=>(atom, atom) noexcept = default;
};

class atom_table {
public:
    atom_table();

    atom_table(const atom_table &) = delete;
    atom_table & operator=(const atom_table &) = delete;

    [[nodiscard]] atom intern(std::string_view text);

    // ASCII-lowercasing intern, for HTML tag and attribute names
    [[nodiscard]] atom intern_lower(std::string_view text);

    [[nodiscard]] std::string_view text(atom a) const;

    [[nodiscard]] std::size_t size() const;

private:
    mutable std::shared_mutex mutex_;
    std::deque<std::string> storage_; // stable addresses; string_views point here
    flat_map<std::string_view, std::uint32_t> index_;
};

} // namespace ctbrowser

template <> struct std::hash<ctbrowser::atom> {
    [[nodiscard]] std::size_t operator()(ctbrowser::atom a) const noexcept {
        return std::hash<std::uint32_t>{}(a.id);
    }
};
