#pragma once

#include "ctcompile/CTNative/Runtime/String.hpp"

#include "ctbrowser/core/symbol.hpp"

#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace ctnative {

struct undefined_t;
struct symbol_constructor;

// GCC 13 misdiagnoses the inactive String arm through optional loop state.
// ponytail: remove this scoped suppression when GCC 13 support ends.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

// A primitive identity, not its description or a boxed object. Well-known
// values need no allocation, including during static initialization.
class js_symbol_t {
    std::variant<ctbrowser::well_known_symbol, ctbrowser::symbol_value> identity;

    explicit constexpr js_symbol_t(ctbrowser::well_known_symbol symbol) : identity(symbol) {}
    explicit js_symbol_t(ctbrowser::symbol_value symbol) : identity(std::move(symbol)) {}
    friend struct symbol_constructor;

public:
    // Symbols are primitive values: moving one must not consume its identity.
    js_symbol_t(const js_symbol_t &) = default;
    js_symbol_t & operator=(const js_symbol_t &) = default;
    explicit constexpr operator bool() const noexcept { return true; }
    std::optional<js_string> description() const {
        if (const auto * symbol = std::get_if<ctbrowser::symbol_value>(&identity)) {
            const auto text = symbol->description_value();
            if (!text) { return std::nullopt; }
            return js_string{*text};
        }
        return js_string{
            ctbrowser::make_well_known_symbol(std::get<ctbrowser::well_known_symbol>(identity))
                .description};
    }
    js_string toString() const {
        if (const auto * symbol = std::get_if<ctbrowser::symbol_value>(&identity)) {
            return js_string{symbol->to_string()};
        }
        return js_string{
            ctbrowser::make_well_known_symbol(std::get<ctbrowser::well_known_symbol>(identity))
                .to_string()};
    }
    js_symbol_t valueOf() const { return *this; }
    friend bool operator==(const js_symbol_t &, const js_symbol_t &) = default;
};

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13
#pragma GCC diagnostic pop
#endif

struct symbol_to_string_method {
    js_string call(const js_symbol_t & receiver) const { return receiver.toString(); }
};
struct symbol_value_of_method {
    js_symbol_t call(const js_symbol_t & receiver) const { return receiver.valueOf(); }
};
struct symbol_prototype {
    symbol_to_string_method toString;
    symbol_value_of_method valueOf;
};

struct symbol_constructor {
    const symbol_prototype prototype{};
#define CTBROWSER_WELL_KNOWN_SYMBOL(name)                                                          \
    const js_symbol_t name{ctbrowser::well_known_symbol::name};
#include "ctbrowser/core/well_known_symbols.def"
#undef CTBROWSER_WELL_KNOWN_SYMBOL

    js_symbol_t operator()() const { return make(std::nullopt); }
    js_symbol_t operator()(const undefined_t &) const { return (*this)(); }
    js_symbol_t operator()(const js_string & description) const {
        return make(description.value());
    }

private:
    static js_symbol_t make(std::optional<std::string_view> description) {
        // One serial source across translation units and factory copies. Never
        // wrap into an existing identity, even when concurrent calls exhaust it.
        static std::atomic<std::uint64_t> next{0};
        auto serial = next.load(std::memory_order_relaxed);
        do {
            if (serial == std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error("native Symbol identity space exhausted");
            }
        } while (!next.compare_exchange_weak(serial, serial + 1, std::memory_order_relaxed));
        return js_symbol_t{ctbrowser::make_symbol(serial, description)};
    }
};

inline constexpr symbol_constructor Symbol{};

} // namespace ctnative
