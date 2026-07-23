#ifndef CTBROWSER__PAGE__HPP
#define CTBROWSER__PAGE__HPP

#include "embed.hpp"
#include <cthtml.hpp>
#include <ctcss.hpp>
#include <ctjs.hpp>
#include <ctc/string.hpp>
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
// ctc::string - a structural BYTE string, so the template parameter object IS
// the UTF-8 source and is viewed directly; the <style>/<script>/<title> text
// is pulled out with a cheap LINEAR left-to-right scan (raw_tag_text). No
// per-node template instantiation: a large app costs the translation unit
// almost nothing.

namespace ctbrowser {

namespace detail {

// the page source, viewed straight out of the template parameter
// object (static storage - ctc::string stores bytes)
template <ctc::string Src> constexpr std::string_view src_view() noexcept {
	return std::string_view{Src.data(), Src.size()};
}

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

template <ctc::string Src, typename Tag> struct raw_tag_text {
	static constexpr std::size_t length = scan_raw_tag(src_view<Src>(), Tag::view(), nullptr);
	static constexpr auto compute() noexcept {
		std::array<char, length + 1> out{};
		(void)scan_raw_tag(src_view<Src>(), Tag::view(), out.data());
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
template <ctc::string Src> struct page {
	using style_source = detail::raw_tag_text<Src, detail::style_tag>;
	using script_source = detail::raw_tag_text<Src, detail::script_tag>;

	// the raw HTML, straight from the NTTP; the engine builds the DOM by value
	static constexpr std::string_view html_text() noexcept { return detail::src_view<Src>(); }

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
