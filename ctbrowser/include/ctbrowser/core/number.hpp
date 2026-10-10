//===- number.hpp - JS integer conversions -------------------- -*- C++ -*-===//
//
// ECMA-262 ToUint32 and ToInt32 on an already converted Number.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>
#include <cstdint>

namespace ctbrowser {

/// ECMA-262 ToUint32 after ToNumber: non-finite values become zero; finite
/// values truncate toward zero and wrap modulo 2^32.
[[nodiscard]] inline std::uint32_t number_to_uint32(double value) noexcept {
    if (!std::isfinite(value)) { return 0; }
    return static_cast<std::uint32_t>(
        static_cast<std::int64_t>(std::fmod(std::trunc(value), 4294967296.0)));
}

/// ECMA-262 ToInt32: ToUint32 reinterpreted as two's complement.
[[nodiscard]] inline std::int32_t number_to_int32(double value) noexcept {
    return static_cast<std::int32_t>(number_to_uint32(value));
}

} // namespace ctbrowser
