//===- StringNumber.h - Literal string to Number ----------------*- C++ -*-===//
//
// Converts string literals to Numbers at compile time, only where the result
// provably matches JavaScript's ToNumber.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "ctbrowser/core/algorithms.hpp"
#include "ctbrowser/core/number_format.hpp"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace ctcompile::ctnative {

/// Converts a string literal to a Number when the result is provably the same
/// as JavaScript's ToNumber; returns nullopt for anything it cannot prove,
/// including radix overflow and unrecognised spellings. Linear in the text
/// length: callers must bound or charge for the bytes first.
inline std::optional<double> literalStringNumber(llvm::StringRef text) {
    // ctbrowser::string_to_number counts mantissa digits in an int when
    // resolving overflow and underflow.
    if (text.size() > static_cast<unsigned>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    llvm::StringRef digits = ctbrowser::trim_js_space({text.data(), text.size()});
    // Only these exact tokens may produce a non-finite result. Other spellings
    // that string_to_number turns into NaN must pass the grammar check below.
    if (digits == "Infinity" || digits == "+Infinity" || digits == "-Infinity" || digits == "NaN") {
        return ctbrowser::string_to_number({text.data(), text.size()});
    }
    int radix = 10;
    if (digits.consume_front_insensitive("0x")) {
        radix = 16;
    } else if (digits.consume_front_insensitive("0o")) {
        radix = 8;
    } else if (digits.consume_front_insensitive("0b")) {
        radix = 2;
    } else if ((digits.consume_front("+") || digits.consume_front("-")) && digits.empty()) {
        return std::nullopt;
    }
    if (radix == 10 && !digits.empty()) {
        const auto decimal = [](char c) { return c >= '0' && c <= '9'; };
        auto rest = digits.drop_while(decimal);
        bool hasDigits = rest.size() != digits.size();
        if (rest.consume_front(".")) {
            const auto fraction = rest.drop_while(decimal);
            hasDigits |= fraction.size() != rest.size();
            rest = fraction;
        }
        if (!hasDigits) { return std::nullopt; }
        if (rest.consume_front_insensitive("e")) {
            if (!rest.consume_front("+")) { rest.consume_front("-"); }
            unsigned exponent = 0;
            // string_to_number adds the exponent to the mantissa's digit count
            // in an int; reject exponents that could overflow it, keeping a
            // margin of at least 32 or the input length.
            if (rest.empty() || !llvm::all_of(rest, decimal) || rest.getAsInteger(10, exponent) ||
                exponent > static_cast<unsigned>(std::numeric_limits<int>::max()) -
                               std::max<std::size_t>(32, text.size())) {
                return std::nullopt;
            }
            rest = {};
        }
        if (!rest.empty()) { return std::nullopt; }
    } else if ((radix != 10 && digits.empty()) || !llvm::all_of(digits, [radix](char c) {
                   const auto digit = ctbrowser::hex_value(c);
                   return digit >= 0 && digit < radix;
               })) {
        return std::nullopt;
    }
    const double converted = ctbrowser::string_to_number({text.data(), text.size()});
    // Decimal overflow gives Infinity, which is the right ToNumber result.
    // string_to_number returns NaN for radix overflow, which JavaScript does
    // not, so that result is refused.
    if (std::isnan(converted)) { return std::nullopt; }
    return converted;
}

} // namespace ctcompile::ctnative
