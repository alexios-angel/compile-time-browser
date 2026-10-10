//===- String.hpp - The native JavaScript String ----------------*- C++ -*-===//
//
// js_string, the C++ type generated native code uses for a JavaScript String,
// string_length, which counts UTF-16 code units as JavaScript does, and the
// String object whose prototype methods native code calls
// (`ctnative::String.prototype.trim.call(s)`); `s[i]` is string_at in
// ctnative.hpp, beside nullable_string.
//
//===----------------------------------------------------------------------===//

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
#include <vector>

namespace ctnative {

class js_boolean_t;

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

    /// Callers emit this only when the native admission check has proved
    /// `prefix` is ASCII; only then does a byte comparison agree with the
    /// UTF-16 comparison.
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

/// String.prototype.toLowerCase (ECMA-262 22.1.3.28): Unicode's default full
/// lowercase, Final_Sigma included, from the same core tables the VM uses.
struct to_lower_case_method {
    js_string call(const js_string & self) const {
        return js_string{ctbrowser::unicode_case(self.value(), false)};
    }
};

/// String.prototype.trim (ECMA-262 22.1.3.32): TrimString with both ends, over
/// WhiteSpace and LineTerminator (the StrWhiteSpaceChar set).
struct trim_method {
    js_string call(const js_string & self) const {
        return js_string{ctbrowser::trim_js_space(self.value())};
    }
};

/// String.prototype.split (ECMA-262 22.1.3.23) with no limit.
struct split_method {
    /// A String separator. An empty separator splits into UTF-16 code units
    /// (a pair's halves apart); an empty text gives [""] unless the separator
    /// is empty too.
    std::vector<js_string> call(const js_string & self, const js_string & separator) const {
        const std::string & s = self.value();
        const std::string & sep = separator.value();
        std::vector<js_string> out;
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
    /// The separator `/\s+/` (ECMA-262 22.2.6.14, RegExp.prototype[@@split]):
    /// the text cut at every maximal run of WhiteSpace or LineTerminator, which
    /// is what `\s` matches (22.2.2.9). A leading or trailing run leaves an
    /// empty first or last piece, and the empty text gives [""].
    std::vector<js_string> call(const js_string & self, whitespace_run_t) const {
        const std::string_view s{self.value()};
        std::vector<js_string> out;
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
};

/// The String.prototype methods native code calls, as stateless method
/// objects: generated code reads `ctnative::String.prototype.trim.call(s)`,
/// the JavaScript spelling. Only the methods the compiler lowers are here.
struct string_prototype {
    to_lower_case_method toLowerCase;
    trim_method trim;
    split_method split;
};
/// The String constructor's members. `String(x)` conversion is not lowered,
/// so it has no call operator.
struct string_constructor {
    string_prototype prototype;
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
