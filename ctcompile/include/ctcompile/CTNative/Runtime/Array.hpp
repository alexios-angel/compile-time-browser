//===- Array.hpp - Native arrays of proved element type ---------*- C++ -*-===//
//
// The Array and String methods generated code calls on a
// `std::vector<T>` whose element type the compiler proved. Only programs
// that build such arrays include this header. T = js_string today.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctnative.hpp"

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/number_format.hpp>

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

/// Array.prototype.push (ECMA-262 23.1.3.23) with one argument: appends it and
/// returns the new length.
template <class T> js_num array_push(std::vector<T> & v, T x) {
    v.push_back(std::move(x));
    return js_num{static_cast<double>(v.size())};
}

/// `a[i]` (ECMA-262 10.4.2, an Array's [[Get]] of an array index): the element
/// at an integral index below the length, else undefined.
inline nullable_string vec_at(const std::vector<js_string> & values, nullable_scalar key) {
    std::size_t index = 0;
    if (!element_index(key, values.size(), index)) { return {}; }
    return values[index].value();
}

/// Array.prototype.join (ECMA-262 23.1.3.18) of Strings: the elements with
/// `separator` between them, "" for no elements. Lone surrogates that meet at
/// a seam pair up, as String concatenation does.
inline js_string array_join(const std::vector<js_string> & values, const js_string & separator) {
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) { out += separator.value(); }
        out += values[i].value();
    }
    ctbrowser::join_surrogates(out);
    return js_string{std::move(out)};
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
} // namespace detail

/// IsLessThan on two Strings (ECMA-262 7.2.13 step 3): code-unit order over
/// UTF-16, which is not byte order over WTF-8 once a code point above U+FFFF
/// (high surrogate D800-DBFF) meets one in U+E000-U+FFFF.
inline bool string_less(const js_string & left, const js_string & right) {
    detail::utf16_units a{left.value()}, b{right.value()};
    for (;;) {
        char16_t x = 0, y = 0;
        const bool hasX = a.next(x), hasY = b.next(y);
        if (!hasX || !hasY) { return !hasX && hasY; }
        if (x != y) { return x < y; }
    }
}

/// Array.prototype.sort (ECMA-262 23.1.3.30) of Strings with no comparator:
/// SortCompare's String order (string_less), stable as the spec requires.
inline void array_sort(std::vector<js_string> & values) {
    std::ranges::stable_sort(values, string_less);
}

/// String.prototype.split (ECMA-262 22.1.3.23) with a String separator and no
/// limit. An empty separator splits into UTF-16 code units (a pair's halves
/// apart); an empty text gives [""] unless the separator is empty too.
inline std::vector<js_string> string_split(const js_string & text, const js_string & separator) {
    const std::string & s = text.value();
    const std::string & sep = separator.value();
    std::vector<js_string> out;
    // A separator holding a lone surrogate (lead byte ED) can match half of a
    // pair that WTF-8 stores as one four-byte code point, so that case and
    // the empty separator work on UTF-16 units. Otherwise UTF-8 is
    // self-synchronizing and a byte match is a code-unit match.
    if (sep.empty() || sep.find('\xED') != std::string::npos) {
        const std::u16string units = ctbrowser::wtf8_to_utf16(s);
        const std::u16string needle = ctbrowser::wtf8_to_utf16(sep);
        const std::u16string_view all{units};
        if (needle.empty()) {
            for (std::size_t i = 0; i < all.size(); ++i) {
                out.emplace_back(ctbrowser::utf16_to_wtf8(all.substr(i, 1)));
            }
            return out;
        }
        std::size_t from = 0;
        for (auto at = all.find(needle); at != std::u16string_view::npos;
             at = all.find(needle, from)) {
            out.emplace_back(ctbrowser::utf16_to_wtf8(all.substr(from, at - from)));
            from = at + needle.size();
        }
        out.emplace_back(ctbrowser::utf16_to_wtf8(all.substr(from)));
        return out;
    }
    std::size_t from = 0;
    for (auto at = s.find(sep); at != std::string::npos; at = s.find(sep, from)) {
        out.emplace_back(std::string_view{s}.substr(from, at - from));
        from = at + sep.size();
    }
    out.emplace_back(std::string_view{s}.substr(from));
    return out;
}

/// `text.split(/\s+/)` (ECMA-262 22.2.6.14, RegExp.prototype[@@split]): the
/// text cut at every maximal run of WhiteSpace or LineTerminator, which is
/// what `\s` matches (22.2.2.9). A leading or trailing run leaves an empty
/// first or last piece, and the empty text gives [""].
inline std::vector<js_string> split_js_space(const js_string & text) {
    const std::string_view s{text.value()};
    std::vector<js_string> out;
    std::size_t piece = 0;
    for (std::size_t at = 0; at < s.size();) {
        const std::string_view rest = s.substr(at);
        const std::size_t run = rest.size() - ctbrowser::trim_js_space(rest, true, false).size();
        if (run == 0) {
            (void)ctbrowser::decode_utf8(s, at);
            continue;
        }
        out.emplace_back(s.substr(piece, at - piece));
        at += run;
        piece = at;
    }
    out.emplace_back(s.substr(piece));
    return out;
}

} // namespace ctnative
