#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ctbrowser {

// AN INTERNAL KEY STARTS WITH A BYTE NO JAVASCRIPT STRING CAN HOLD. A symbol
// and an internal slot are both properties under a string key here, and a key
// a page can spell is a key a page can forge: `o["@@iterator"] = f` made any
// object iterable while the spelling was `@@`, and `it["@#IteratedString"]`
// rewrote a RegExp String Iterator's slot. A JS string is WTF-8
// (script/utf16.hpp), where 0xFF never occurs - context::string and the
// compiler's source both turn a stray one into U+FFFD, as the Encoding
// Standard's decoder would - so a key led by it is one only the engine mints.
// `@` then marks a symbol, `#` a private name or slot (script/value/base.hpp),
// which keeps both marks two bytes long. Macros, because only literal
// concatenation keeps a spelled key a literal.
#define CTBROWSER_INTERNAL_KEY_LEAD "\xFF"
#define CTBROWSER_SYMBOL_KEY(text) CTBROWSER_INTERNAL_KEY_LEAD "@" text
inline constexpr char internal_key_lead = '\xFF';
// Every symbol's key starts with the mark: a well-known one is the mark and
// its name, the rest carry one of the two prefixes below.
inline constexpr std::string_view symbol_key_mark = CTBROWSER_SYMBOL_KEY("");
// `Symbol()`: the prefix, a serial, and ":" plus the description if it has one.
inline constexpr std::string_view symbol_key_prefix = CTBROWSER_SYMBOL_KEY("sym:");
// `Symbol.for(k)`: the prefix and k - the registry's identity.
inline constexpr std::string_view registered_symbol_key_prefix = CTBROWSER_SYMBOL_KEY("for:");

// TEXT THAT CAME FROM OUTSIDE, WITH EVERY STRAY LEAD BYTE AS U+FFFD - what
// the Encoding Standard's UTF-8 decoder makes of an 0xFF, and so what a
// producer that handed over bytes undecoded would have had to give. One memchr
// when there is none, which is every time the text really was decoded.
inline void replace_internal_key_lead(std::string & text) {
    for (std::size_t at = text.find(internal_key_lead); at != std::string::npos;
         at = text.find(internal_key_lead, at + 3)) {
        text.replace(at, 1, "\xEF\xBF\xBD");
    }
}

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
    return {"Symbol." + std::string{name}, std::string{symbol_key_mark} + std::string{name}};
}

} // namespace ctbrowser
