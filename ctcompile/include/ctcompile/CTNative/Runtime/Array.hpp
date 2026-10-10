//===- Array.hpp - Native arrays of proved element type ---------*- C++ -*-===//
//
// ctnative::Array, whose prototype methods generated code calls on a
// `std::vector<T>` whose element type the compiler proved
// (`ctnative::Array.prototype.push.call(a, x)`), and the literal and index
// helpers the emitter uses for such arrays. Only programs that build one
// include this header. T is js_string or a std::pair row today.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctnative.hpp"

#include <ctbrowser/core/algorithms.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctnative {

/// Appends one element of an array literal, in source order.
inline void vec_push(std::vector<js_string> & v, js_string x) {
    v.push_back(std::move(x));
}

/// `a[i]` (ECMA-262 10.4.2, an Array's [[Get]] of an array index): the element
/// at an integral index below the length, else undefined.
inline nullable_string vec_at(const std::vector<js_string> & values, nullable_scalar key) {
    std::size_t index = 0;
    if (!element_index(key, values.size(), index)) { return {}; }
    return values[index].value();
}

/// `a[i]` where the compiler proved `i` an integer in [0, a.length) (a loop
/// counter checked against this array's length; element arrays never shrink),
/// so the read is the element itself rather than an optional.
template <class T> const T & vec_element(const std::vector<T> & values, js_num index) {
    return values[static_cast<std::size_t>(index.value())];
}

namespace detail {
/// The UTF-16 code units of WTF-8 text, one at a time, without converting the
/// whole string.
class utf16_units {
    std::string_view text;
    std::size_t at = 0;
    char16_t pending = 0;

public:
    explicit utf16_units(std::string_view wtf8) : text(wtf8) {}
    /// The next unit, or false at the end.
    bool next(char16_t & unit) {
        if (pending != 0) {
            unit = std::exchange(pending, char16_t{0});
            return true;
        }
        if (at >= text.size()) { return false; }
        char32_t cp = ctbrowser::decode_utf8(text, at);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            unit = static_cast<char16_t>(0xD800 + (cp >> 10));
            pending = static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
        } else {
            unit = static_cast<char16_t>(cp);
        }
        return true;
    }
};

/// IsLessThan on two Strings (ECMA-262 7.2.13 step 3): code-unit order over
/// UTF-16, which is not byte order over WTF-8 once a code point above U+FFFF
/// (high surrogate D800-DBFF) meets one in U+E000-U+FFFF.
inline bool string_less(const js_string & left, const js_string & right) {
    utf16_units a{left.value()}, b{right.value()};
    for (;;) {
        char16_t x = 0, y = 0;
        const bool hasX = a.next(x), hasY = b.next(y);
        if (!hasX || !hasY) { return !hasX && hasY; }
        if (x != y) { return x < y; }
    }
}
} // namespace detail

/// Array.prototype.push (ECMA-262 23.1.3.23) with one argument: appends it and
/// returns the new length.
struct push_method {
    template <class T> js_num call(std::vector<T> & self, T value) const {
        self.push_back(std::move(value));
        return js_num{static_cast<double>(self.size())};
    }
};

/// Array.prototype.join (ECMA-262 23.1.3.18) of Strings: the elements with
/// `separator` between them, "" for no elements. Lone surrogates that meet at
/// a seam pair up, as String concatenation does. The emitter passes "," for
/// a join with no argument (step 3).
struct join_method {
    js_string call(const std::vector<js_string> & self, const js_string & separator) const {
        std::string out;
        for (std::size_t i = 0; i < self.size(); ++i) {
            if (i != 0) { out += separator.value(); }
            out += self[i].value();
        }
        ctbrowser::join_surrogates(out);
        return js_string{std::move(out)};
    }
};

/// Array.prototype.sort (ECMA-262 23.1.3.30) of Strings with no comparator:
/// SortCompare's String order (UTF-16 code units), stable as the spec
/// requires. Sorts in place and answers the array, as `a.sort()` answers `a`.
struct sort_method {
    std::vector<js_string> & call(std::vector<js_string> & self) const {
        std::ranges::stable_sort(self, detail::string_less);
        return self;
    }
};

/// The Array.prototype methods native code calls, as stateless method
/// objects: generated code reads `ctnative::Array.prototype.push.call(a, x)`.
/// Only the methods the compiler lowers are here.
struct array_prototype {
    push_method push;
    join_method join;
    sort_method sort;
};
/// The Array constructor's members. `Array(n)` and `new Array` are not
/// lowered, so it has no call operator.
struct array_constructor {
    array_prototype prototype;
};
inline constexpr array_constructor Array{};

} // namespace ctnative
