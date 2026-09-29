#pragma once
// A JAVASCRIPT STRING IS A SEQUENCE OF UTF-16 CODE UNITS, AND THIS ENGINE
// STORES IT AS WTF-8.
//
// The storage stays UTF-8 because everything the engine hands a string to -
// the DOM, the style engine, the asset registry, the native backend - speaks
// UTF-8, and converting at every one of those boundaries would cost more than
// it fixes. WTF-8 rather than UTF-8 because a JS string may hold a LONE
// surrogate (`"\uD83D"`), which is written as its three-byte form ED A0..BF xx;
// two halves that meet are joined into the one four-byte code point they are
// (core's join_surrogates, which context::string runs on every string).
//
// What a script SEES is UTF-16: `length`, every index and every position is a
// count of code units, a code point above U+FFFF is two of them, and a slice
// that splits one leaves each half as its lone surrogate. These are the
// helpers that give the bytes that meaning - over a bare string_view here, and
// remembered per string on string_object (value/base.hpp), which is what the
// VM and the String built-ins ask.
//
// THE WALK IS core's decode_utf8, and its policy for a byte that begins no
// sequence - it stands for itself, one unit - is what keeps every function
// here total on any bytes at all, and in agreement with wtf8_to_utf16.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <ctbrowser/core/algorithms.hpp>

namespace ctbrowser::script::utf16 {

// How many units one code point is.
[[nodiscard]] constexpr std::size_t units_of(char32_t cp) noexcept {
    return cp >= 0x10000 ? 2 : 1;
}

// Unit `half` (0 or 1) of a code point: the code point itself below U+10000,
// otherwise its high or low surrogate.
[[nodiscard]] constexpr char16_t code_unit(char32_t cp, std::size_t half) noexcept {
    if (cp < 0x10000) { return static_cast<char16_t>(cp); }
    const char32_t v = cp - 0x10000;
    return static_cast<char16_t>(half == 0 ? 0xD800 + (v >> 10) : 0xDC00 + (v & 0x3FF));
}

// Every byte below 0x80, so byte i IS unit i and nothing below has to decode.
// Eight bytes at a time: this is the test every String method pays first.
[[nodiscard]] inline bool is_ascii(std::string_view s) noexcept {
    std::size_t i = 0;
    for (; i + 8 <= s.size(); i += 8) {
        std::uint64_t word = 0;
        std::memcpy(&word, s.data() + i, 8);
        if ((word & 0x8080808080808080ull) != 0) { return false; }
    }
    for (; i < s.size(); ++i) {
        if (static_cast<unsigned char>(s[i]) >= 0x80) { return false; }
    }
    return true;
}

// The length in code units.
[[nodiscard]] inline std::size_t length(std::string_view s) noexcept {
    if (is_ascii(s)) { return s.size(); }
    std::size_t units = 0;
    for (std::size_t at = 0; at < s.size();) { units += units_of(decode_utf8(s, at)); }
    return units;
}

// One code unit as WTF-8 - a surrogate is its lone three-byte form. What
// `s[i]`, charAt and a split by "" hand back.
[[nodiscard]] inline std::string unit_text(char16_t unit) {
    std::string out;
    append_utf8(out, unit);
    return out;
}

// WHERE UNIT `i` LIVES: the byte its code point starts at, and whether `i` is
// that code point's SECOND unit - a pair's low half. Past the end is
// {size, false}. The walk starts from (`byte`, `unit`), a code point known to
// begin at or before `i`, so a caller holding a nearer one than the start
// (string_object's marks) passes it.
struct position {
    std::size_t byte;
    bool low_half;
};
[[nodiscard]] inline position locate(std::string_view s, std::size_t i, std::size_t byte = 0,
                                     std::size_t unit = 0) noexcept {
    while (byte < s.size()) {
        std::size_t next = byte;
        const std::size_t n = units_of(decode_utf8(s, next));
        if (i < unit + n) { return {byte, i != unit}; }
        unit += n;
        byte = next;
    }
    return {s.size(), false};
}

// The units before byte offset `to`, which starts a code point (or is the
// end) - a byte position turned back into the index a script sees. Walked
// from (`byte`, `unit`) as locate is.
[[nodiscard]] inline std::size_t units_before(std::string_view s, std::size_t to,
                                              std::size_t byte = 0, std::size_t unit = 0) noexcept {
    while (byte < to && byte < s.size()) { unit += units_of(decode_utf8(s, byte)); }
    return unit;
}

// The unit at a position locate found.
[[nodiscard]] inline char16_t unit_at(std::string_view s, position at) noexcept {
    std::size_t next = at.byte;
    return code_unit(decode_utf8(s, next), at.low_half ? 1 : 0);
}

// Units [from, to) as WTF-8, `from` before `to`. A pair split by either edge
// leaves its half behind as the lone surrogate, which is what `"\u{1F600}"
// .slice(0, 1)` is in every engine.
[[nodiscard]] inline std::string slice(std::string_view s, position from, position to) {
    std::string out;
    std::size_t begin = from.byte;
    if (from.low_half) {
        std::size_t next = begin;
        append_utf8(out, code_unit(decode_utf8(s, next), 1));
        begin = next;
    }
    if (to.byte > begin) { out.append(s.substr(begin, to.byte - begin)); }
    if (to.low_half) {
        std::size_t next = to.byte;
        append_utf8(out, code_unit(decode_utf8(s, next), 0));
    }
    return out;
}

// Does the text hold a lone surrogate - ED A0..BF? The one case in which a
// byte search cannot stand in for a unit search: `"\uD83D"` is half of the
// four-byte `"\u{1F600}"` and no byte of it matches.
[[nodiscard]] inline bool has_lone_surrogate(std::string_view s) noexcept {
    for (std::size_t at = s.find('\xED'); at != std::string_view::npos && at + 1 < s.size();
         at = s.find('\xED', at + 1)) {
        if (static_cast<unsigned char>(s[at + 1]) >= 0xA0) { return true; }
    }
    return false;
}

// Does the text hold a code point above U+FFFF - a four-byte sequence, which
// is two units?
[[nodiscard]] inline bool has_pair(std::string_view s) noexcept {
    return std::any_of(s.begin(), s.end(),
                       [](char c) { return static_cast<unsigned char>(c) >= 0xF0; });
}

// THE SAME UNITS WITH EVERY PAIR WRITTEN AS ITS TWO LONE HALVES (CESU-8):
// the text as a walk over code points meets it when the walk has to see CODE
// UNITS - a RegExp without `u` or `v`, whose pattern and input are both
// sequences of units (22.2.2), so `.` takes one half of "\u{1D306}". Nothing
// keeps this form: context::string joins the halves of any string made from
// it again.
[[nodiscard]] inline std::string split_pairs(std::string_view s) {
    if (!has_pair(s)) { return std::string{s}; }
    std::string out;
    out.reserve(s.size() + s.size() / 2);
    for (std::size_t at = 0; at < s.size();) {
        const std::size_t from = at;
        const char32_t cp = decode_utf8(s, at);
        if (cp < 0x10000) {
            out.append(s.substr(from, at - from));
            continue;
        }
        append_utf8(out, code_unit(cp, 0));
        append_utf8(out, code_unit(cp, 1));
    }
    return out;
}

// IsLessThan's string arm (7.2.13 step 3) - and the default sort order, which
// is the same comparison: CODE UNIT order, as a three-way answer. UTF-8's
// byte order is CODE POINT order, and the two part only where a code point
// above U+FFFF (a pair, whose first unit is D800-DBFF) meets one in
// U+E000-U+FFFF, or a lone surrogate meets a pair. So the bytes decide up to
// the first difference, and from the code point holding it the units do.
[[nodiscard]] inline int compare(std::string_view a, std::string_view b) noexcept {
    const std::size_t shared = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < shared && a[i] == b[i]) { ++i; }
    if (i == shared) { return a.size() < b.size() ? -1 : (a.size() > b.size() ? 1 : 0); }
    const auto x = static_cast<unsigned char>(a[i]);
    const auto y = static_cast<unsigned char>(b[i]);
    if (x < 0x80 && y < 0x80) { return x < y ? -1 : 1; }
    // Back to the start of the code point the difference is in - the bytes
    // before it are the same in both.
    const auto trail = [](char c) { return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u; };
    while (i > 0 && (trail(a[i]) || trail(b[i]))) { --i; }
    // Unit by unit from there; the answer comes within two code points.
    std::size_t at_a = i;
    std::size_t at_b = i;
    char16_t low_a = 0;
    char16_t low_b = 0;
    const auto next = [](std::string_view s, std::size_t & at, char16_t & low, char16_t & out) {
        if (low != 0) {
            out = low;
            low = 0;
            return true;
        }
        if (at >= s.size()) { return false; }
        const char32_t cp = decode_utf8(s, at);
        out = code_unit(cp, 0);
        if (cp >= 0x10000) { low = code_unit(cp, 1); }
        return true;
    };
    while (true) {
        char16_t u = 0;
        char16_t v = 0;
        const bool more_a = next(a, at_a, low_a, u);
        const bool more_b = next(b, at_b, low_b, v);
        if (!more_a || !more_b) { return more_a ? 1 : (more_b ? -1 : 0); }
        if (u != v) { return u < v ? -1 : 1; }
    }
}

} // namespace ctbrowser::script::utf16
