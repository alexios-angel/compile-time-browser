//===- String.hpp - The native JavaScript String ----------------*- C++ -*-===//
//
// js_string, the C++ type generated native code uses for a JavaScript String,
// with the String.prototype methods native code calls as members of the same
// name (`s.trim()`, `s.length()`), string_length, which counts UTF-16 code
// units as JavaScript does, and the String object whose prototype methods
// call those members (`ctnative::String.prototype.trim.call(s)`). `s[i]` is
// defined in ctnative.hpp, beside nullable_string, which it returns.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "Number.hpp"

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/number_format.hpp>

#include <cmath>
#include <compare>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ctnative {

class js_boolean_t;
struct nullable_string;

/// A String's `length`: UTF-16 code units over the WTF-8 storage, counted like
/// the interpreter (script/utf16.hpp), so a code point above U+FFFF counts as
/// two.
inline std::size_t string_length(std::string_view text) {
    std::size_t units = 0;
    for (std::size_t at = 0; at < text.size();) {
        units += ctbrowser::decode_utf8(text, at) >= 0x10000 ? 2 : 1;
    }
    return units;
}

/// The RegExp literal `/\s+/` with no flags, which the compiler recognises as
/// split's separator. A tag rather than a RegExp: the runtime has no regular
/// expression engine, so each recognised literal is its own overload.
struct whitespace_run_t {};
inline constexpr whitespace_run_t whitespace_run{};

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

/// A JavaScript string stored as WTF-8. JavaScript indexing and `length` count
/// UTF-16 code units (see string_length), so any byte-offset operation needs a
/// proof that the text involved is ASCII.
template <class T>
requires std::is_same_v<T, char>
class js_basic_string {
    std::string text;

    template <class U> static js_basic_string primitive_text(U value) {
        if constexpr (std::is_same_v<U, bool> || std::is_same_v<U, js_boolean_t>) {
            return value ? js_basic_string{"true"} : js_basic_string{"false"};
        } else {
            // Raw C++ arithmetic becomes a JavaScript (binary64) Number. A long
            // double beyond double's range prints as +/-Infinity, so check that
            // before narrowing.
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

    /// String.prototype.toString (ECMA-262 22.1.3.33): the String itself.
    js_basic_string toString() const { return *this; }
    /// `s.length` (ECMA-262 22.1.5.1): the count of UTF-16 code units.
    js_num length() const { return js_num{static_cast<double>(string_length(text))}; }
    /// `s[i]` (10.4.3.5): the one code unit at an integral index below the
    /// length, else undefined. Defined in ctnative.hpp beside nullable_string.
    nullable_string operator[](js_num index) const;

    /// String.prototype.toLowerCase (ECMA-262 22.1.3.28): Unicode's default
    /// full lowercase, Final_Sigma included, from the same core tables the VM
    /// uses.
    js_basic_string toLowerCase() const {
        return js_basic_string{ctbrowser::unicode_case(text, false)};
    }
    /// String.prototype.toUpperCase (ECMA-262 22.1.3.30): Unicode's default
    /// full uppercase (ß becomes SS), from the same core tables the VM uses.
    js_basic_string toUpperCase() const {
        return js_basic_string{ctbrowser::unicode_case(text, true)};
    }
    /// String.prototype.trim (ECMA-262 22.1.3.32): TrimString with both ends,
    /// over WhiteSpace and LineTerminator (the StrWhiteSpaceChar set).
    js_basic_string trim() const { return js_basic_string{ctbrowser::trim_js_space(text)}; }
    /// String.prototype.split (ECMA-262 22.1.3.23) with a String separator
    /// and no limit. An empty separator splits into UTF-16 code units (a
    /// pair's halves apart); an empty text gives [""] unless the separator is
    /// empty too.
    std::vector<js_basic_string> split(const js_basic_string & separator) const {
        const std::string & s = text;
        const std::string & sep = separator.text;
        std::vector<js_basic_string> out;
        // A separator holding a lone surrogate (lead byte ED) can match half
        // of a pair that WTF-8 stores as one four-byte code point, so that
        // case and the empty separator work on UTF-16 units. Otherwise UTF-8
        // is self-synchronizing and a byte match is a code-unit match.
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
    /// String.prototype.split with the separator `/\s+/` (ECMA-262 22.2.6.14,
    /// RegExp.prototype[@@split]): the text cut at every maximal run of
    /// WhiteSpace or LineTerminator, which is what `\s` matches (22.2.2.9). A
    /// leading or trailing run leaves an empty first or last piece, and the
    /// empty text gives [""].
    std::vector<js_basic_string> split(whitespace_run_t) const {
        const std::string_view s{text};
        std::vector<js_basic_string> out;
        std::size_t piece = 0;
        for (std::size_t at = 0; at < s.size();) {
            const std::string_view rest = s.substr(at);
            const std::size_t run =
                rest.size() - ctbrowser::trim_js_space(rest, true, false).size();
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

    /// Callers emit this only when the native admission check has proved
    /// `prefix` is ASCII; only then does a byte comparison agree with the
    /// UTF-16 comparison.
    bool startsWith(const js_basic_string & prefix) const { return text.starts_with(prefix.text); }
    /// `a === b` on two Strings (ECMA-262 7.2.15): the same code units. WTF-8
    /// stores each code-unit sequence one way (a pair is always one four-byte
    /// code point; see operator+), so equal bytes are equal units.
    friend bool operator==(const js_basic_string &, const js_basic_string &) = default;
    /// `a < b` and the other relational operators on two Strings (ECMA-262
    /// 7.2.13 IsLessThan, step 3): UTF-16 code-unit order, which is not byte
    /// order over WTF-8 once a code point above U+FFFF (high surrogate
    /// D800-DBFF) meets one in U+E000-U+FFFF. A proper prefix is less.
    friend std::strong_ordering operator<=>(const js_basic_string & left,
                                            const js_basic_string & right) {
        detail::utf16_units a{left.text}, b{right.text};
        for (;;) {
            char16_t x = 0, y = 0;
            const bool hasX = a.next(x), hasY = b.next(y);
            if (!hasX || !hasY) { return hasX <=> hasY; }
            if (x != y) { return x <=> y; }
        }
    }
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

inline js_string js_num::toString() const {
    return js_string{ctbrowser::number_to_string(number)};
}

/// ToString (ECMA-262 7.1.17) of a String: the String itself. ctnative.hpp
/// adds the other primitive carriers; String(x) and the formatter both print
/// through this overload set.
inline js_string to_js_string(const js_string & value) {
    return value;
}

/// String.prototype.toLowerCase; see js_string::toLowerCase.
struct to_lower_case_method {
    js_string call(const js_string & self) const { return self.toLowerCase(); }
};

/// String.prototype.toUpperCase; see js_string::toUpperCase.
struct to_upper_case_method {
    js_string call(const js_string & self) const { return self.toUpperCase(); }
};

/// String.prototype.trim; see js_string::trim.
struct trim_method {
    js_string call(const js_string & self) const { return self.trim(); }
};

/// String.prototype.split with no limit; see js_string::split.
struct split_method {
    std::vector<js_string> call(const js_string & self, const js_string & separator) const {
        return self.split(separator);
    }
    std::vector<js_string> call(const js_string & self, whitespace_run_t) const {
        return self.split(whitespace_run);
    }
};

class reg_exp;

/// String.prototype.replace with a RegExp and a function replacement, defined
/// in RegExp.hpp, which only the programs that call it include.
struct replace_method {
    template <class Callback>
    js_string call(const js_string & self, const reg_exp & pattern, Callback callback) const;
    js_string call(const js_string & self, const reg_exp & pattern,
                   const js_string & replacement) const;
};

/// The String.prototype methods native code calls, as stateless method
/// objects: generated code reads `ctnative::String.prototype.trim.call(s)`,
/// the JavaScript spelling. Only the methods the compiler lowers are here.
class match_result;

/// String.prototype.match with a RegExp without `g`, defined in RegExp.hpp,
/// which only the programs that call it include.
struct match_method {
    match_result call(const js_string & self, const reg_exp & pattern) const;
};

struct string_prototype {
    to_lower_case_method toLowerCase;
    to_upper_case_method toUpperCase;
    trim_method trim;
    split_method split;
    replace_method replace;
    match_method match;
};
/// The String constructor's members: generated code reads `ctnative::String(x)`
/// and `ctnative::String.prototype.trim.call(s)`.
struct string_constructor {
    string_prototype prototype;
    /// `String(value)` (ECMA-262 22.1.1.1) of a primitive carrier: ToString
    /// (7.1.17), through the to_js_string overload of the value's carrier.
    /// Objects and Symbols have none, so they do not convert here.
    template <class T> js_string operator()(const T & value) const { return to_js_string(value); }
};
inline constexpr string_constructor String{};

/// The lowering of `text.replace(/[set]+$/, '')` (ECMA-262 22.1.3.19 with the
/// one RegExp literal shape the compiler recognises there, whose only match
/// is the maximal run of `set` characters at the end): `text` without that
/// run. Not String.prototype.replace itself, which would need a RegExp and
/// GetSubstitution. `set` must be ASCII; an ASCII byte is never part of a
/// multibyte WTF-8 sequence, so the byte scan matches the code point scan.
inline js_string strip_trailing_run(const js_string & text, std::string_view set) {
    const std::string & bytes = text.value();
    const auto last = bytes.find_last_not_of(set);
    return js_string{std::string_view{bytes}.substr(0, last == std::string::npos ? 0 : last + 1)};
}

} // namespace ctnative
