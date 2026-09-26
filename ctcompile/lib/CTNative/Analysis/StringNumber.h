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

inline std::optional<double> literalStringNumber(llvm::StringRef text) {
    // Callers must bound or charge source bytes before this linear scan. Core
    // counts mantissa digits in int when resolving overflow/underflow.
    if (text.size() > static_cast<unsigned>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    llvm::StringRef digits = ctbrowser::trim_js_space({text.data(), text.size()});
    // Only these exact tokens may supply a nonfinite conversion proof. Other
    // spellings that Core converts to NaN still require validated grammar below.
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
            // Core adds an int exponent to the mantissa order on range failure.
            // Retain the old 32-byte margin and widen it with the input.
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
    // Decimal overflow may feed bitwise conversion. Radix overflow in Core is
    // NaN, so it still cannot supply a JavaScript conversion proof.
    if (std::isnan(converted)) { return std::nullopt; }
    return converted;
}

} // namespace ctcompile::ctnative
