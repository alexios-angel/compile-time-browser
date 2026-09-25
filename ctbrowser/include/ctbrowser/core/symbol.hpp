#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ctbrowser {

inline constexpr std::string_view symbol_key_prefix = "@@sym:";

// Own the existing symbol identity encoding independently of VM allocation.
// Descriptions are labels; equality compares the complete identity key.
struct symbol_value {
    std::string description;
    std::string key;

    symbol_value(std::string description, std::string key)
        : description(std::move(description)), key(std::move(key)) {}

    [[nodiscard]] std::optional<std::string_view> description_value() const noexcept {
        if (key.starts_with(symbol_key_prefix) &&
            key.find(':', symbol_key_prefix.size()) == std::string::npos) {
            return std::nullopt;
        }
        return description;
    }
    [[nodiscard]] std::string to_string() const { return "Symbol(" + description + ")"; }
    [[nodiscard]] friend bool operator==(const symbol_value & left,
                                         const symbol_value & right) noexcept {
        return left.key == right.key;
    }
};

// The caller owns the serial source. An absent description differs from "".
[[nodiscard]] inline symbol_value make_symbol(std::uint64_t serial,
                                              std::optional<std::string_view> description) {
    std::string key = std::string{symbol_key_prefix} + std::to_string(serial);
    if (description) {
        key += ':';
        key += *description;
    }
    return {description ? std::string{*description} : std::string{}, std::move(key)};
}

enum class well_known_symbol {
#define CTBROWSER_WELL_KNOWN_SYMBOL(name) name,
#include "ctbrowser/core/well_known_symbols.def"
#undef CTBROWSER_WELL_KNOWN_SYMBOL
};

[[nodiscard]] inline symbol_value make_well_known_symbol(well_known_symbol symbol) {
    constexpr std::array names{
#define CTBROWSER_WELL_KNOWN_SYMBOL(name) std::string_view{#name},
#include "ctbrowser/core/well_known_symbols.def"
#undef CTBROWSER_WELL_KNOWN_SYMBOL
    };
    const auto name = names.at(static_cast<std::size_t>(symbol));
    return {"Symbol." + std::string{name}, "@@" + std::string{name}};
}

} // namespace ctbrowser
