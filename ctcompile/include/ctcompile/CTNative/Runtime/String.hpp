#pragma once

#include "Number.hpp"

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/number_format.hpp>

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ctnative {

class js_boolean_t;

// The storage is WTF-8; JavaScript indexing still requires UTF-16 proofs.
template <class T>
requires std::is_same_v<T, char>
class js_basic_string {
    std::string text;

    template <class U> static js_basic_string primitive_text(U value) {
        if constexpr (std::is_same_v<U, bool> || std::is_same_v<U, js_boolean_t>) {
            return value ? js_basic_string{"true"} : js_basic_string{"false"};
        } else {
            // Raw C++ arithmetic enters the JavaScript binary64 Number domain.
            // Check extended floating-point range before narrowing to double.
            if constexpr (std::is_same_v<U, long double>) {
                constexpr auto maximum = std::numeric_limits<double>::max();
                if (value > maximum) { return js_basic_string{"Infinity"}; }
                if (value < -maximum) { return js_basic_string{"-Infinity"}; }
            }
            return js_basic_string{ctbrowser::number_to_string(static_cast<double>(value))};
        }
    }

public:
    js_basic_string() = default;
    explicit js_basic_string(std::string value) : text(std::move(value)) {}
    explicit js_basic_string(std::string_view value) : text(value) {}
    template <std::size_t N>
    explicit js_basic_string(const char (&value)[N]) : text(value, N - 1) {}
    explicit js_basic_string(const char * value, std::size_t length) : text(value, length) {}

    explicit operator bool() const { return !text.empty(); }
    const std::string & value() const & { return text; }
    std::string value() && { return std::move(text); }
    js_num to_number() const { return js_num{ctbrowser::string_to_number(text)}; }

    // Admission proves an ASCII prefix, where byte and UTF-16 searches agree.
    bool startsWith(const js_basic_string & prefix) const { return text.starts_with(prefix.text); }
    friend bool operator==(const js_basic_string &, const js_basic_string &) = default;
    friend js_basic_string operator+(js_basic_string left, const js_basic_string & right) {
        left.text += right.text;
        ctbrowser::join_surrogates(left.text);
        return left;
    }
    friend js_basic_string operator+(js_basic_string left, js_num right) {
        return std::move(left) + js_basic_string{ctbrowser::number_to_string(right.value())};
    }
    friend js_basic_string operator+(js_num left, const js_basic_string & right) {
        return js_basic_string{ctbrowser::number_to_string(left.value())} + right;
    }
    template <class U>
    requires(std::is_arithmetic_v<U> || std::is_same_v<U, js_boolean_t>)
    friend js_basic_string operator+(js_basic_string left, U right) {
        return std::move(left) + primitive_text(right);
    }
    template <class U>
    requires(std::is_arithmetic_v<U> || std::is_same_v<U, js_boolean_t>)
    friend js_basic_string operator+(U left, const js_basic_string & right) {
        return primitive_text(left) + right;
    }
};

using js_string = js_basic_string<char>;

} // namespace ctnative
