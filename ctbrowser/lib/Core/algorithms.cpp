//===- ctbrowser/lib/Core/algorithms.cpp - Small shared algorithms --------===//
//
// ASCII case folding and comparison, top-level list splitting, WTF-8 <-> UTF-16
// conversion, and base64 (declared in core/algorithms.hpp). Everything here is
// ASCII-only and locale-independent so goldens compare byte for byte across
// platforms.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/core/algorithms.hpp>

#include <simdutf.h>

#include <algorithm>

namespace ctbrowser {

bool ascii_iequals(std::string_view a, std::string_view b) noexcept {
    return std::ranges::equal(a, b, {}, ascii_lower, ascii_lower);
}

bool ascii_iequals_any(std::string_view text, std::span<const std::string_view> names) noexcept {
    return std::ranges::any_of(names,
                               [text](std::string_view one) { return ascii_iequals(text, one); });
}

std::vector<std::string_view> split_top_level(std::string_view text, std::string_view separators) {
    std::vector<std::string_view> out;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t begin = text.find_first_not_of(separators, at);
        if (begin == std::string_view::npos) { break; }
        std::size_t end = begin;
        int depth = 0;
        char quote = 0;
        for (; end < text.size(); ++end) {
            const char c = text[end];
            if (quote != 0) {
                if (c == '\\' && end + 1 < text.size()) {
                    ++end;
                } else if (c == quote) {
                    quote = 0;
                }
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')') {
                if (depth > 0) { --depth; }
            } else if (depth == 0 && separators.find(c) != std::string_view::npos) {
                break;
            }
        }
        out.push_back(text.substr(begin, end - begin));
        at = end + (end < text.size() ? 1 : 0);
    }
    return out;
}

bool ascii_istarts_with(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && ascii_iequals(text.substr(0, prefix.size()), prefix);
}

void ascii_lower_in_place(std::string & text) noexcept {
    std::ranges::transform(text, text.begin(), ascii_lower);
}

std::string ascii_lower_copy(std::string_view text) {
    std::string out{text};
    ascii_lower_in_place(out);
    return out;
}

void ascii_upper_in_place(std::string & text) noexcept {
    std::ranges::transform(text, text.begin(), ascii_upper);
}

std::u16string wtf8_to_utf16(std::string_view text) {
    std::u16string units;
    units.reserve(text.size());
    for (std::size_t at = 0; at < text.size();) {
        // A truncated or invalid sequence: the byte stands for itself, which
        // keeps this total on any bytes at all, since the document's text comes
        // from a tokenizer that does not promise well-formedness.
        const char32_t cp = decode_utf8(text, at);
        if (cp >= 0x10000) {
            units.push_back(static_cast<char16_t>(0xD800 + ((cp - 0x10000) >> 10)));
            units.push_back(static_cast<char16_t>(0xDC00 + ((cp - 0x10000) & 0x3FF)));
        } else {
            units.push_back(static_cast<char16_t>(cp));
        }
    }
    return units;
}

std::string utf16_to_wtf8(std::u16string_view units) {
    std::string out;
    out.reserve(units.size());
    for (std::size_t at = 0; at < units.size(); ++at) {
        char32_t cp = units[at];
        if (cp >= 0xD800 && cp <= 0xDBFF && at + 1 < units.size() && units[at + 1] >= 0xDC00 &&
            units[at + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (units[at + 1] - 0xDC00);
            ++at;
        }
        append_utf8(out, cp);
    }
    return out;
}

// The value of one base64 alphabet character, or -1 for anything else
// (padding, whitespace and garbage alike, all of which the lenient path skips).
[[nodiscard]] constexpr int base64_sextet(char c) noexcept {
    if (c >= 'A' && c <= 'Z') { return c - 'A'; }
    if (c >= 'a' && c <= 'z') { return c - 'a' + 26; }
    if (c >= '0' && c <= '9') { return c - '0' + 52; }
    if (c == '+') { return 62; }
    if (c == '/') { return 63; }
    return -1;
}

std::string base64_encode(std::string_view bytes, bool url, bool omit_padding) {
    const std::string_view alphabet =
        url ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
            : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned b0 = static_cast<unsigned char>(bytes[i]);
        const unsigned b1 = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0;
        const unsigned b2 = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0;
        const unsigned triple = (b0 << 16) | (b1 << 8) | b2;
        out += alphabet[(triple >> 18) & 0x3F];
        out += alphabet[(triple >> 12) & 0x3F];
        // The padding is what says how many of the last three bytes were real.
        if (i + 1 < bytes.size()) {
            out += alphabet[(triple >> 6) & 0x3F];
        } else if (!omit_padding) {
            out += '=';
        }
        if (i + 2 < bytes.size()) {
            out += alphabet[triple & 0x3F];
        } else if (!omit_padding) {
            out += '=';
        }
    }
    return out;
}

std::string base64_decode(std::string_view text) {
    // Fast path: simdutf, which decodes at ~48 GB/s against this loop's
    // ~1.1 GB/s (42x, measured on a payload the size of the base64 PNGs Phaser
    // decodes at boot; see docs/performance.md).
    //
    // It runs in strict mode, and the fallback below is not a formality.
    // simdutf's `accept_garbage` option looks like this function's documented
    // leniency and is not: it stops at the first `=` where this loop reads past
    // it, and the two disagreed on 4.7% of 200,000 malformed inputs. Restricted
    // to input strict mode accepts, they agreed 60,856 times and differed zero
    // times, which is the precondition relied on here.
    //
    // So every well-formed payload (every `data:` URL and every real `atob`)
    // goes through simdutf, and anything it refuses falls through to the
    // lenient loop below. The observable behaviour is the same either way.
    if (!text.empty()) {
        std::string fast;
        fast.resize(simdutf::maximal_binary_length_from_base64(text.data(), text.size()));
        const simdutf::result decoded =
            simdutf::base64_to_binary(text.data(), text.size(), fast.data());
        if (decoded.error == simdutf::error_code::SUCCESS) {
            fast.resize(decoded.count);
            return fast;
        }
    }

    // The buffer is sized by rounding the character count up to a whole group:
    // `n / 4 * 3` assumes the padding is present, and unpadded "aGVsbG8" is
    // seven characters that decode to five bytes, not three.
    std::string out;
    out.resize((text.size() + 3) / 4 * 3);
    char * write = out.data();

    unsigned group[4] = {0, 0, 0, 0};
    std::size_t filled = 0;
    for (const char raw : text) {
        const int six = base64_sextet(raw);
        if (six < 0) { continue; }
        group[filled] = static_cast<unsigned>(six);
        if (++filled == 4) {
            *write++ = static_cast<char>((group[0] << 2) | (group[1] >> 4));
            *write++ = static_cast<char>(((group[1] & 0xF) << 4) | (group[2] >> 2));
            *write++ = static_cast<char>(((group[2] & 0x3) << 6) | group[3]);
            filled = 0;
        }
    }
    // A trailing partial group carries one fewer byte than it has characters,
    // which is what makes the padding optional: `aGVsbG8` and `aGVsbG8=` decode
    // to the same five bytes. A lone leftover character encodes nothing.
    if (filled >= 2) {
        *write++ = static_cast<char>((group[0] << 2) | (group[1] >> 4));
        if (filled == 3) {
            *write++ = static_cast<char>(((group[1] & 0xF) << 4) | (group[2] >> 2));
        }
    }
    out.resize(static_cast<std::size_t>(write - out.data()));
    return out;
}

} // namespace ctbrowser
