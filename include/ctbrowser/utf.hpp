#ifndef CTBROWSER__UTF__HPP
#define CTBROWSER__UTF__HPP

#ifndef CTBROWSER_IN_A_MODULE
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#endif

// Full Unicode transcoding (UTF-8 / UTF-16 / UTF-32) per the Unicode standard.
// The engine works in UTF-32 code points (one char32_t = one code point) for
// layout, measurement and glyph lookup; source text (HTML/JS) is UTF-8 and TTF
// wants UTF-8, so these convert at the boundaries. Everything is constexpr so the
// compile-time DOM/layout path keeps folding.
//
// Conformance: decoding rejects ill-formed UTF-8 by the "substitution of maximal
// subparts" rule of Unicode 3.9 (Table 3-7) - overlong forms, out-of-range code
// points and surrogate code units in UTF-8 all become U+FFFD; encoding maps any
// surrogate or >U+10FFFF value to U+FFFD; UTF-16 handles surrogate pairs and
// substitutes lone surrogates. The full scalar range U+0000..U+10FFFF is handled.

namespace ctbrowser {

inline constexpr char32_t REPLACEMENT_CHAR = 0xFFFD;
inline constexpr char32_t MAX_CODEPOINT = 0x10FFFF;

// decode one code point from UTF-8 starting at s[i]; advances i past the bytes
// consumed (at least one). Ill-formed sequences yield U+FFFD.
constexpr char32_t utf8_next(std::string_view s, std::size_t & i) noexcept {
	const std::size_t n = s.size();
	if (i >= n) { return 0; }
	const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
	const unsigned char b0 = byte(i);
	if (b0 < 0x80) { ++i; return b0; } // ASCII
	const auto cont = [&](std::size_t k, std::uint32_t lo, std::uint32_t hi) {
		return i + k < n && byte(i + k) >= lo && byte(i + k) <= hi;
	};
	if (b0 >= 0xC2 && b0 <= 0xDF) { // 2-byte
		if (cont(1, 0x80, 0xBF)) {
			const char32_t cp = static_cast<char32_t>((b0 & 0x1F) << 6) | (byte(i + 1) & 0x3F);
			i += 2;
			return cp;
		}
	} else if (b0 >= 0xE0 && b0 <= 0xEF) { // 3-byte
		const std::uint32_t lo1 = b0 == 0xE0 ? 0xA0 : 0x80;              // guard overlong
		const std::uint32_t hi1 = b0 == 0xED ? 0x9F : 0xBF;              // guard surrogates
		if (cont(1, lo1, hi1) && cont(2, 0x80, 0xBF)) {
			const char32_t cp = static_cast<char32_t>((b0 & 0x0F) << 12) |
			                    static_cast<char32_t>((byte(i + 1) & 0x3F) << 6) |
			                    (byte(i + 2) & 0x3F);
			i += 3;
			return cp;
		}
	} else if (b0 >= 0xF0 && b0 <= 0xF4) { // 4-byte
		const std::uint32_t lo1 = b0 == 0xF0 ? 0x90 : 0x80;             // guard overlong
		const std::uint32_t hi1 = b0 == 0xF4 ? 0x8F : 0xBF;             // guard > U+10FFFF
		if (cont(1, lo1, hi1) && cont(2, 0x80, 0xBF) && cont(3, 0x80, 0xBF)) {
			const char32_t cp = static_cast<char32_t>((b0 & 0x07) << 18) |
			                    static_cast<char32_t>((byte(i + 1) & 0x3F) << 12) |
			                    static_cast<char32_t>((byte(i + 2) & 0x3F) << 6) |
			                    (byte(i + 3) & 0x3F);
			i += 4;
			return cp;
		}
	}
	++i; // ill-formed: substitute one replacement char, resync at the next byte
	return REPLACEMENT_CHAR;
}

// number of code points a UTF-8 string decodes to
constexpr std::size_t utf8_length(std::string_view s) noexcept {
	std::size_t count = 0, i = 0;
	while (i < s.size()) {
		(void)utf8_next(s, i);
		++count;
	}
	return count;
}

// UTF-8 -> UTF-32
constexpr std::u32string utf8_to_utf32(std::string_view s) {
	std::u32string out;
	std::size_t i = 0;
	while (i < s.size()) { out.push_back(utf8_next(s, i)); }
	return out;
}

// append one code point to a UTF-8 string (invalid -> U+FFFD)
constexpr void utf8_append(std::string & out, char32_t cp) {
	if (cp > MAX_CODEPOINT || (cp >= 0xD800 && cp <= 0xDFFF)) { cp = REPLACEMENT_CHAR; }
	if (cp < 0x80) {
		out.push_back(static_cast<char>(cp));
	} else if (cp < 0x800) {
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else {
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}

// UTF-32 -> UTF-8
constexpr std::string utf32_to_utf8(std::u32string_view s) {
	std::string out;
	for (const char32_t cp : s) { utf8_append(out, cp); }
	return out;
}

// UTF-16 -> UTF-32 (decodes surrogate pairs; lone surrogates -> U+FFFD)
constexpr std::u32string utf16_to_utf32(std::u16string_view s) {
	std::u32string out;
	std::size_t i = 0;
	while (i < s.size()) {
		const char16_t u = s[i++];
		if (u >= 0xD800 && u <= 0xDBFF) { // high surrogate
			if (i < s.size() && s[i] >= 0xDC00 && s[i] <= 0xDFFF) {
				const char32_t lo = s[i++];
				out.push_back(0x10000 + ((static_cast<char32_t>(u - 0xD800) << 10) | (lo - 0xDC00)));
			} else {
				out.push_back(REPLACEMENT_CHAR);
			}
		} else if (u >= 0xDC00 && u <= 0xDFFF) { // lone low surrogate
			out.push_back(REPLACEMENT_CHAR);
		} else {
			out.push_back(u);
		}
	}
	return out;
}

// UTF-32 -> UTF-16 (emits surrogate pairs; invalid scalars -> U+FFFD)
constexpr std::u16string utf32_to_utf16(std::u32string_view s) {
	std::u16string out;
	for (char32_t cp : s) {
		if (cp > MAX_CODEPOINT || (cp >= 0xD800 && cp <= 0xDFFF)) { cp = REPLACEMENT_CHAR; }
		if (cp < 0x10000) {
			out.push_back(static_cast<char16_t>(cp));
		} else {
			cp -= 0x10000;
			out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
			out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
		}
	}
	return out;
}

} // namespace ctbrowser

#endif
