//===- symbol.hpp - Symbol and internal keys ------------------ -*- C++ -*-===//
//
// How JavaScript symbols and engine-internal slots are spelled as property
// keys, and symbol_value, a symbol held independently of the VM heap.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ctbrowser {

// An internal key starts with a byte no JavaScript string can hold. Symbols and
// internal slots are both stored as properties under string keys, and a key a
// page can spell is a key a page can forge (with a printable prefix such as
// `@@`, `o["@@iterator"] = f` would make any object iterable). A JS string is
// WTF-8 (script/utf16.hpp), where 0xFF never occurs: context::string and the
// compiler's source both turn a stray one into U+FFFD, as the Encoding
// Standard's decoder would. So only the engine can mint a key led by 0xFF.
// `@` then marks a symbol and `#` a private name or slot
// (script/value/base.hpp), keeping both marks two bytes long. These are macros
// because only literal concatenation keeps a spelled key a literal.
#define CTBROWSER_INTERNAL_KEY_LEAD "\xFF"
#define CTBROWSER_SYMBOL_KEY(text) CTBROWSER_INTERNAL_KEY_LEAD "@" text
inline constexpr char internal_key_lead = '\xFF';
/// Every symbol's key starts with this mark: a well-known symbol's key is the
/// mark and its name; every other symbol uses one of the two prefixes below.
inline constexpr std::string_view symbol_key_mark = CTBROWSER_SYMBOL_KEY("");
/// `Symbol()`: the prefix, a serial, and ":" plus the description if it has one.
inline constexpr std::string_view symbol_key_prefix = CTBROWSER_SYMBOL_KEY("sym:");
/// `Symbol.for(k)`: the prefix and k, which is the registry's identity.
inline constexpr std::string_view registered_symbol_key_prefix = CTBROWSER_SYMBOL_KEY("for:");

/// Replaces every 0xFF byte in externally supplied `text` with U+FFFD, as the
/// Encoding Standard's UTF-8 decoder would, so outside text can never contain
/// an internal key lead. Costs one memchr when there is none.
inline void replace_internal_key_lead(std::string & text) {
    for (std::size_t at = text.find(internal_key_lead); at != std::string::npos;
         at = text.find(internal_key_lead, at + 3)) {
        text.replace(at, 1, "\xEF\xBF\xBD");
    }
}

/// A symbol as a plain value, independent of the VM heap. `key` is its
/// identity and decides equality; `description` is only a label.
struct symbol_value {
    std::string description;
    std::string key;

    symbol_value(std::string description, std::string key)
        : description(std::move(description)), key(std::move(key)) {}

    /// Symbol.prototype.description: nullopt for a `Symbol()` created without
    /// one (its key has no ':' after the serial).
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

/// Makes a fresh `Symbol(description)`. `serial` must be unique; the caller
/// owns the counter. An absent description differs from "".
[[nodiscard]] inline symbol_value make_symbol(std::uint64_t serial,
                                              std::optional<std::string_view> description) {
    std::string key = std::string{symbol_key_prefix} + std::to_string(serial);
    if (description) {
        key += ':';
        key += *description;
    }
    return {description ? std::string{*description} : std::string{}, std::move(key)};
}

/// The well-known symbols (Symbol.iterator and so on), from
/// well_known_symbols.def.
enum class well_known_symbol {
#define CTBROWSER_WELL_KNOWN_SYMBOL(name) name,
#include "ctbrowser/core/well_known_symbols.def"
#undef CTBROWSER_WELL_KNOWN_SYMBOL
};

/// The symbol_value for `symbol`, described as "Symbol.<name>".
[[nodiscard]] inline symbol_value make_well_known_symbol(well_known_symbol symbol) {
    constexpr std::array names{
#define CTBROWSER_WELL_KNOWN_SYMBOL(name) std::string_view{#name},
#include "ctbrowser/core/well_known_symbols.def"
#undef CTBROWSER_WELL_KNOWN_SYMBOL
    };
    const auto name = names.at(static_cast<std::size_t>(symbol));
    return {"Symbol." + std::string{name}, std::string{symbol_key_mark} + std::string{name}};
}

} // namespace ctbrowser
