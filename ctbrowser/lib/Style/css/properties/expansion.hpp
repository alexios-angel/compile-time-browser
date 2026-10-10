//===- expansion.hpp - Shorthand expansion -------------------- -*- C++ -*-===//
//
// Positional (margin-style) and `flex` shorthand expansion, templated on the
// value type so CSSOM and the cascade share one implementation.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ctbrowser/core/algorithms.hpp>

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::style::css::detail {

/// Expands 1-2 or 1-4 positional values (margin-style) to `count` values, or
/// returns an empty vector for a bad count. Templated so CSSOM's owning
/// strings and the cascade's borrowed tokens share one implementation.
template <typename T>
[[nodiscard]] std::vector<T> expand_positional(std::span<const T> parts, std::size_t count) {
    if (parts.empty() || parts.size() > count || (count != 2 && count != 4)) { return {}; }
    std::vector<T> out(count, parts[0]);
    if (parts.size() > 1) { out[1] = parts[1]; }
    if (count == 4) {
        if (parts.size() > 2) { out[2] = parts[2]; }
        out[3] = parts.size() > 3 ? parts[3] : out[1];
    }
    return out;
}

/// Expands the `flex` shorthand into grow, shrink and basis. Callers validate
/// numbers (`number`) and bases (`basis`), and pass the basis used when it is
/// omitted (`0%` for both current callers).
template <typename T, typename Number, typename Basis>
[[nodiscard]] std::optional<std::array<T, 3>> expand_flex(std::span<const std::string_view> parts,
                                                          std::string_view omitted_basis,
                                                          Number number, Basis basis) {
    if (parts.empty() || parts.size() > 3) { return std::nullopt; }
    if (parts.size() == 1) {
        if (ascii_iequals(parts[0], "none")) { return std::array<T, 3>{T{"0"}, T{"0"}, T{"auto"}}; }
        if (ascii_iequals(parts[0], "auto")) { return std::array<T, 3>{T{"1"}, T{"1"}, T{"auto"}}; }
    }
    std::array<T, 3> out{T{"1"}, T{"1"}, T{omitted_basis}};
    std::size_t i = 0;
    if (auto grow = number(parts[0])) {
        out[0] = std::move(*grow);
        i = 1;
        if (i < parts.size()) {
            if (auto shrink = number(parts[i])) {
                out[1] = std::move(*shrink);
                ++i;
            }
        }
    }
    if (i < parts.size()) {
        auto parsed = basis(parts[i++]);
        if (!parsed) { return std::nullopt; }
        out[2] = std::move(*parsed);
        // `||`: the basis may come first, the grow and shrink after it
        // (Flexbox 1 §7.1), so `flex: 7% 8` is a basis of 7% and a grow of 8.
        if (i == 1 && i < parts.size()) {
            auto grow = number(parts[i]);
            if (!grow) { return std::nullopt; }
            out[0] = std::move(*grow);
            if (++i < parts.size()) {
                auto shrink = number(parts[i]);
                if (!shrink) { return std::nullopt; }
                out[1] = std::move(*shrink);
                ++i;
            }
        }
    }
    return i == parts.size() ? std::optional{std::move(out)} : std::nullopt;
}

} // namespace ctbrowser::style::css::detail
