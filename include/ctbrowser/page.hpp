#ifndef CTBROWSER__PAGE__HPP
#define CTBROWSER__PAGE__HPP

#include "embed.hpp"
#include <cthtml.hpp>
#include <ctcss.hpp>
#include <ctjs.hpp>
#include <ctll/fixed_string.hpp>
#ifndef CTBROWSER_IN_A_MODULE
#include <array>
#include <cstddef>
#include <string_view>
#include <utility>
#endif

// The compile-time assembly. ONE HTML source is the whole application, and
// ctbrowser parses ALL of it with the bricks' constexpr VALUE parsers (the
// only parsers they have, since the type-level grammar paths were removed):
//   * the DOM        - cthtml::parse(html_text())        (engine: instantiate_html)
//   * the stylesheet - ctcss::parse_value(style_text())  (engine: css_sheet)
//   * the script     - ctjs::run_value(script_text())    (engine: run_value)
//
// page<> only has to hand the engine three runtime strings. The page NTTP is a
// ctll::fixed_string of char32_t code points; the <style>/<script>/<title> text
// is pulled out with a cheap LINEAR left-to-right scan (raw_tag_text) and the
// whole document is re-encoded to UTF-8 bytes (html_bytes) - no per-node
// template instantiation, so a large app costs the translation unit almost
// nothing.

namespace ctbrowser {

namespace detail {

// --- the HTML source as runtime UTF-8 BYTES.
//
// The page NTTP is a ctll::fixed_string of char32_t code points (the raw .inc
// bytes decoded as UTF-8). Re-encode it back to bytes so the DOM can be built at
// RUNTIME with cthtml's linear VALUE parser (instantiate_html).
constexpr std::size_t utf8_len(char32_t c) noexcept {
	return c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
}
constexpr std::size_t put_utf8(char * out, char32_t c) noexcept {
	if (c < 0x80) { out[0] = static_cast<char>(c); return 1; }
	if (c < 0x800) {
		out[0] = static_cast<char>(0xC0 | (c >> 6));
		out[1] = static_cast<char>(0x80 | (c & 0x3F));
		return 2;
	}
	if (c < 0x10000) {
		out[0] = static_cast<char>(0xE0 | (c >> 12));
		out[1] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
		out[2] = static_cast<char>(0x80 | (c & 0x3F));
		return 3;
	}
	out[0] = static_cast<char>(0xF0 | (c >> 18));
	out[1] = static_cast<char>(0x80 | ((c >> 12) & 0x3F));
	out[2] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
	out[3] = static_cast<char>(0x80 | (c & 0x3F));
	return 4;
}

template <ctll::fixed_string Src> struct html_bytes {
	// ctll::fixed_string stores the source as char32_t, but WITHOUT the UTF-8
	// decode (CTRE_STRING_IS_UTF8 is not set) each unit is one raw source byte
	// (0..255). The source is already UTF-8, so copy the bytes verbatim; do NOT
	// re-encode them as code points (that double-encodes every multi-byte glyph).
	static constexpr std::size_t length = Src.size();
	static constexpr auto compute() noexcept {
		std::array<char, length + 1> out{};
		for (std::size_t i = 0; i < Src.size(); ++i) { out[i] = static_cast<char>(Src.content[i]); }
		return out;
	}
	static constexpr std::array<char, length + 1> storage = compute();
	static constexpr std::string_view get() noexcept { return {storage.data(), length}; }
};

// --- linear <style>/<script>/<title> text extraction (a raw-text element's
// content, concatenated in document order with '\n' after each). Inline
// elements only (a <script src> is bundled ahead of time by tools/js-bundle.py).
constexpr bool tag_at(std::string_view h, std::size_t i, std::string_view tag) noexcept {
	if (i + tag.size() > h.size()) { return false; }
	for (std::size_t k = 0; k < tag.size(); ++k) {
		char c = h[i + k];
		if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c - 'A' + 'a'); }
		if (c != tag[k]) { return false; }
	}
	// the char after the name must end the tag name
	char nx = (i + tag.size() < h.size()) ? h[i + tag.size()] : '>';
	return nx == '>' || nx == ' ' || nx == '\t' || nx == '\n' || nx == '\r' || nx == '/';
}

// writes to `out` when non-null; returns the number of bytes produced
constexpr std::size_t scan_raw_tag(std::string_view h, std::string_view tag, char * out) noexcept {
	std::size_t written = 0, i = 0;
	while (i < h.size()) {
		if (h[i] == '<' && tag_at(h, i + 1, tag)) {
			std::size_t j = i + 1 + tag.size();
			while (j < h.size() && h[j] != '>') { ++j; }   // skip the open tag's attrs
			if (j < h.size()) { ++j; }                       // past '>'
			std::size_t start = j;
			while (j < h.size() && !(h[j] == '<' && j + 1 < h.size() && h[j + 1] == '/' &&
			                         tag_at(h, j + 2, tag))) { ++j; }
			for (std::size_t k = start; k < j; ++k) { if (out) { out[written] = h[k]; } ++written; }
			if (out) { out[written] = '\n'; } ++written;
			i = j;
		} else {
			++i;
		}
	}
	return written;
}

template <ctll::fixed_string Src, typename Tag> struct raw_tag_text {
	static constexpr std::size_t length = scan_raw_tag(html_bytes<Src>::get(), Tag::view(), nullptr);
	static constexpr auto compute() noexcept {
		std::array<char, length + 1> out{};
		(void)scan_raw_tag(html_bytes<Src>::get(), Tag::view(), out.data());
		return out;
	}
	static constexpr std::array<char, length + 1> storage = compute();
	static constexpr std::string_view get() noexcept { return {storage.data(), length}; }
};

struct style_tag {
	static constexpr std::string_view view() noexcept { return "style"; }
};
struct script_tag {
	static constexpr std::string_view view() noexcept { return "script"; }
};
struct title_tag {
	static constexpr std::string_view view() noexcept { return "title"; }
};

} // namespace detail

// the assembled application, as three runtime strings, parsed by the engine
// with the bricks' value parsers (instantiate_html / ctcss::parse_value /
// ctjs::run_value).
template <ctll::fixed_string Src> struct page {
	using style_source = detail::raw_tag_text<Src, detail::style_tag>;
	using script_source = detail::raw_tag_text<Src, detail::script_tag>;

	// the raw HTML as runtime bytes; the engine builds the DOM from this by value
	static constexpr std::string_view html_text() noexcept { return detail::html_bytes<Src>::get(); }

	// the page's CSS as a runtime string; the engine parses+queries it by value
	// (ctcss::parse_value)
	static constexpr std::string_view style_text() noexcept { return style_source::get(); }

	// the page's JS as a runtime string; the engine parses+runs it by value
	// (ctjs::run_value)
	static constexpr std::string_view script_text() noexcept { return script_source::get(); }

	// the <title> text ("ctbrowser" when absent), extracted linearly
	static constexpr std::string_view title() noexcept {
		std::string_view t = detail::raw_tag_text<Src, detail::title_tag>::get();
		while (!t.empty() && (t.back() == '\n' || t.back() == ' ' || t.back() == '\t')) { t.remove_suffix(1); }
		while (!t.empty() && (t.front() == ' ' || t.front() == '\t' || t.front() == '\n')) { t.remove_prefix(1); }
		return t.empty() ? std::string_view{"ctbrowser"} : t;
	}
};

} // namespace ctbrowser

#endif
