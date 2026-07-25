module;
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

export module ctbrowser.paint:values;

import ctbrowser.core;

// CSS colours, parsed where they are consumed.
//
// Same reasoning as layout's :values partition - a computed style holds many
// declarations, and parsing every colour at resolution time would be work done
// for properties nobody paints. This parses on demand.

export namespace ctbrowser::paint {

using ctbrowser::color;

[[nodiscard]] constexpr std::optional<std::uint8_t> hex_digit(char c) noexcept {
	if (c >= '0' && c <= '9') { return static_cast<std::uint8_t>(c - '0'); }
	if (c >= 'a' && c <= 'f') { return static_cast<std::uint8_t>(c - 'a' + 10); }
	if (c >= 'A' && c <= 'F') { return static_cast<std::uint8_t>(c - 'A' + 10); }
	return std::nullopt;
}

// The CSS named colours a real page actually uses. This is deliberately not
// the full 148-entry table: the long tail is generated data that belongs with
// the UA stylesheet port, and a partial list that says so beats a partial list
// that pretends to be complete.
[[nodiscard]] inline std::optional<color> named_color(std::string_view name) {
	struct entry {
		std::string_view name;
		std::uint32_t rgb;
	};
	static constexpr entry table[] = {
	    {"black", 0x000000},   {"silver", 0xC0C0C0},  {"gray", 0x808080},   {"grey", 0x808080},
	    {"white", 0xFFFFFF},   {"maroon", 0x800000},  {"red", 0xFF0000},    {"purple", 0x800080},
	    {"fuchsia", 0xFF00FF}, {"magenta", 0xFF00FF}, {"green", 0x008000},  {"lime", 0x00FF00},
	    {"olive", 0x808000},   {"yellow", 0xFFFF00},  {"navy", 0x000080},   {"blue", 0x0000FF},
	    {"teal", 0x008080},    {"aqua", 0x00FFFF},    {"cyan", 0x00FFFF},   {"orange", 0xFFA500},
	    {"pink", 0xFFC0CB},    {"brown", 0xA52A2A},   {"gold", 0xFFD700},   {"beige", 0xF5F5DC},
	    {"ivory", 0xFFFFF0},   {"khaki", 0xF0E68C},   {"crimson", 0xDC143C},
	    {"lightgray", 0xD3D3D3}, {"lightgrey", 0xD3D3D3}, {"darkgray", 0xA9A9A9},
	    {"darkgrey", 0xA9A9A9}, {"lightblue", 0xADD8E6}, {"darkblue", 0x00008B},
	    {"lightgreen", 0x90EE90}, {"darkgreen", 0x006400}, {"whitesmoke", 0xF5F5F5},
	    {"gainsboro", 0xDCDCDC}, {"tomato", 0xFF6347}, {"salmon", 0xFA8072},
	    {"indigo", 0x4B0082}, {"violet", 0xEE82EE}, {"turquoise", 0x40E0D0},
	};
	for (const entry & e : table) {
		if (e.name == name) { return color{0xFF000000u | e.rgb}; }
	}
	if (name == "transparent") { return color{0}; }
	return std::nullopt;
}

// #rgb, #rgba, #rrggbb, #rrggbbaa, rgb(...), rgba(...), or a name.
// Returns nullopt for anything unrecognised so callers can fall back rather
// than silently painting black - `background-color: <garbage>` must leave the
// element unpainted, not fill it.
[[nodiscard]] inline std::optional<color> parse_color(std::string_view text) {
	while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) { text.remove_prefix(1); }
	while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) { text.remove_suffix(1); }
	if (text.empty()) { return std::nullopt; }

	if (text.front() == '#') {
		const std::string_view digits = text.substr(1);
		std::uint8_t v[8]{};
		if (digits.size() != 3 && digits.size() != 4 && digits.size() != 6 && digits.size() != 8) {
			return std::nullopt;
		}
		for (std::size_t i = 0; i < digits.size(); ++i) {
			const auto d = hex_digit(digits[i]);
			if (!d) { return std::nullopt; }
			v[i] = *d;
		}
		if (digits.size() <= 4) { // shorthand: each digit is doubled
			const std::uint8_t a = digits.size() == 4 ? static_cast<std::uint8_t>(v[3] * 17) : 255;
			return color::rgba(static_cast<std::uint8_t>(v[0] * 17), static_cast<std::uint8_t>(v[1] * 17),
			                   static_cast<std::uint8_t>(v[2] * 17), a);
		}
		const auto byte = [&](std::size_t i) {
			return static_cast<std::uint8_t>(v[i] * 16 + v[i + 1]);
		};
		const std::uint8_t a = digits.size() == 8 ? byte(6) : 255;
		return color::rgba(byte(0), byte(2), byte(4), a);
	}

	if (text.starts_with("rgb(") || text.starts_with("rgba(")) {
		const std::size_t open = text.find('(');
		if (text.back() != ')') { return std::nullopt; }
		std::string_view args = text.substr(open + 1, text.size() - open - 2);
		float parts[4] = {0, 0, 0, 1};
		std::size_t count = 0;
		while (!args.empty() && count < 4) {
			while (!args.empty() && (args.front() == ' ' || args.front() == ',')) {
				args.remove_prefix(1);
			}
			if (args.empty()) { break; }
			float value = 0;
			const auto [rest, ec] = std::from_chars(args.data(), args.data() + args.size(), value);
			if (ec != std::errc{}) { return std::nullopt; }
			args.remove_prefix(static_cast<std::size_t>(rest - args.data()));
			if (!args.empty() && args.front() == '%') {
				value = value / 100.0f * 255.0f;
				args.remove_prefix(1);
			}
			parts[count++] = value;
		}
		if (count < 3) { return std::nullopt; }
		// Rounded, not truncated: alpha 0.5 is 127.5, and every browser shows
		// 128. Truncating puts rgba(...,0.5) one step off from #...80, which
		// makes two spellings of the same colour disagree.
		const auto clamp8 = [](float f) {
			const float r = f + 0.5f;
			return static_cast<std::uint8_t>(r < 0 ? 0 : (r > 255 ? 255 : r));
		};
		return color::rgba(clamp8(parts[0]), clamp8(parts[1]), clamp8(parts[2]),
		                   clamp8(parts[3] <= 1.0f ? parts[3] * 255.0f : parts[3]));
	}

	return named_color(text);
}

} // namespace ctbrowser::paint
