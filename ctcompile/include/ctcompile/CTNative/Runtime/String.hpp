#pragma once

#include "Number.hpp"

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/number_format.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ctnative {

// The storage is WTF-8; JavaScript indexing still requires UTF-16 proofs.
template <class T>
requires std::is_same_v<T, char>
class js_basic_string {
    std::string text;

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
};

using js_string = js_basic_string<char>;

} // namespace ctnative
