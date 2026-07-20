#ifndef CTBROWSER__PAGE__HPP
#define CTBROWSER__PAGE__HPP

#include "embed.hpp"
#include <cthtml.hpp>
#include <ctcss.hpp>
#include <ctjs.hpp>
#ifndef CTBROWSER_IN_A_MODULE
#include <array>
#include <cstddef>
#include <string_view>
#include <utility>
#endif

// The compile-time assembly. ONE HTML source is the whole application:
// cthtml parses it into the DOM type, then the text of its <style>
// elements is COLLECTED FROM THAT TYPE and re-entered into
// ctcss::parse as a template argument, and the text of its <script>
// elements likewise into ctjs - all during compilation. A CSS typo or
// a JS syntax error inside your page is a compile error pointing at
// the right sub-language.
//
// The pivot is to_fixed<Provider>(): a constexpr string_view living in
// one type's static storage becomes a ctll::fixed_string usable as the
// next parser's NTTP. That is what lets three compile-time parsers
// chain: type -> text -> type.

namespace ctbrowser {

namespace detail {

// re-enter a constexpr string as a template argument
template <typename Provider> consteval auto to_fixed() {
	constexpr std::string_view v = Provider::get();
	std::array<char, v.size()> buf{};
	for (size_t i = 0; i < v.size(); ++i) { buf[i] = v[i]; }
	return ctll::fixed_string<v.size()>{buf};
}

// --- external scripts: <script src="..."> resolves AT COMPILE TIME.
// The src path (repo-root-relative, like every asset path; the build
// passes --embed-dir) is a compile-time string, so std::embed reads
// the FILE during constant evaluation and its text joins the page's
// script chain in document order - the web's shared-global in-order
// semantics, settled before the program exists. The consuming TU
// gates file reads with one #depend directive (see embed.hpp).

template <typename Node> struct script_text_of {
	static constexpr std::string_view get() {
		if constexpr (Node::template has_attribute<"src">()) {
#ifdef CTBROWSER_HAS_STD_EMBED
			constexpr auto bytes = ctbrowser::embed<char>(
			    std::string_view{Node::template attribute<"src">().view()});
			return std::string_view{bytes.data(), bytes.size()};
#else
			return {};
#endif
		} else {
			return std::string_view{Node::text()};
		}
	}
};

// --- collect the concatenated text of every <tag> element in the
// document type (document order, '\n' between pieces); script elements
// contribute their EMBEDDED src file when they have one

template <typename Node, typename Tag> struct tag_collect {
	static constexpr std::string_view piece() noexcept {
		if constexpr (Tag::view() == std::string_view{"script"}) {
			return script_text_of<Node>::get();
		} else {
			return std::string_view{Node::text()};
		}
	}
	template <size_t... I>
	static constexpr size_t children_size(std::index_sequence<I...>) noexcept {
		return (tag_collect<decltype(Node::template child<I>()), Tag>::size() + ... + 0);
	}
	static constexpr size_t size() noexcept {
		if constexpr (Node::type == cthtml::kind::element) {
			size_t n = 0;
			if (Node::name() == Tag::view()) { n += piece().size() + 1; }
			n += children_size(std::make_index_sequence<Node::child_count()>{});
			return n;
		} else {
			return 0;
		}
	}

	template <size_t... I>
	static constexpr char * children_fill(char * at, std::index_sequence<I...>) noexcept {
		((at = tag_collect<decltype(Node::template child<I>()), Tag>::fill(at)), ...);
		return at;
	}
	static constexpr char * fill(char * at) noexcept {
		if constexpr (Node::type == cthtml::kind::element) {
			if (Node::name() == Tag::view()) {
				for (const char c : piece()) { *at++ = c; }
				*at++ = '\n';
			}
			return children_fill(at, std::make_index_sequence<Node::child_count()>{});
		} else {
			return at;
		}
	}
};

template <typename Doc, typename Tag> struct tag_text {
	static constexpr size_t length = tag_collect<Doc, Tag>::size();
	static constexpr auto compute() noexcept {
		std::array<char, length + 1> out{};
		(void)tag_collect<Doc, Tag>::fill(out.data());
		return out;
	}
	static constexpr std::array<char, length + 1> storage = compute();
	static constexpr std::string_view get() noexcept {
		return std::string_view{storage.data(), length};
	}
};

struct style_tag {
	static constexpr std::string_view view() noexcept { return "style"; }
};
struct script_tag {
	static constexpr std::string_view view() noexcept { return "script"; }
};

// --- the HTML source as runtime UTF-8 BYTES.
//
// The page NTTP is a ctll::fixed_string of char32_t code points (the raw .inc
// bytes decoded as UTF-8). Re-encode it back to bytes so the DOM can be built
// at RUNTIME with cthtml's linear VALUE parser (instantiate_html) instead of
// the compile-time Earley parse - the Earley HTML parse is superlinear and is
// the build-time wall for large pages.
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
	static constexpr std::size_t length = [] {
		std::size_t n = 0;
		for (std::size_t i = 0; i < Src.size(); ++i) { n += utf8_len(Src.content[i]); }
		return n;
	}();
	static constexpr auto compute() noexcept {
		std::array<char, length + 1> out{};
		std::size_t k = 0;
		for (std::size_t i = 0; i < Src.size(); ++i) { k += put_utf8(out.data() + k, Src.content[i]); }
		return out;
	}
	static constexpr std::array<char, length + 1> storage = compute();
	static constexpr std::string_view get() noexcept { return {storage.data(), length}; }
};

// --- linear <style>/<script> text extraction, WITHOUT the type DOM.
//
// A cheap left-to-right scan for a raw-text element's content, concatenated
// (document order, '\n' after each) exactly like tag_text did off the type DOM
// - so sheet_type / script_valid are unchanged, but nothing forces the Earley
// HTML parse. (Inline elements only; <script src> is left to the type path.)
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

} // namespace detail

namespace detail {
struct title_tag {
	static constexpr std::string_view view() noexcept { return "title"; }
};
} // namespace detail

// the assembled application. The DOM is built at RUNTIME from the value parser
// (engine: instantiate_html(html_text())) - the Earley HTML parse is superlinear
// and is the build-time wall for large pages, so nothing here forces it. The
// stylesheet is still compile-time (ctcss is type-level) but its <style> text is
// extracted by a LINEAR scan, not by walking the type DOM. The compile-time DOM
// TYPE is available on demand as `typed_dom<>` (a lazy member alias template, so
// naming the page never triggers the Earley parse) for the folds-at-compile-time
// tests.
template <CTJS_STRING_INPUT Src> struct page {
	// the compile-time DOM type, on demand only (does NOT fold unless used)
	template <int = 0> using typed_dom = decltype(cthtml::parse<Src>());

	using style_source = detail::raw_tag_text<Src, detail::style_tag>;
	using script_source = detail::raw_tag_text<Src, detail::script_tag>;

	// the raw HTML as runtime bytes; the engine builds the DOM from this by value
	static constexpr std::string_view html_text() noexcept { return detail::html_bytes<Src>::get(); }

	// the page's CSS, parsed at compile time (a ctcss stylesheet type)
	static constexpr auto sheet() noexcept {
		return ctcss::parse<detail::to_fixed<style_source>()>();
	}
	using sheet_type = decltype(sheet());

	// the page's JS, parsed at compile time by the type/Earley path (only when
	// script_valid/script_type is used - the engine runs the script BY VALUE).
	using script_type = ctjs::script_t<detail::to_fixed<script_source>()>;

	// the page's JS as a runtime string. The engine parses+runs this BY VALUE
	// (ctjs::run_value) - no Earley parse, no per-script template instantiation.
	static constexpr std::string_view script_text() noexcept { return script_source::get(); }

	// the <title> text ("ctbrowser" when absent), extracted linearly
	static constexpr std::string_view title() noexcept {
		std::string_view t = detail::raw_tag_text<Src, detail::title_tag>::get();
		while (!t.empty() && (t.back() == '\n' || t.back() == ' ' || t.back() == '\t')) { t.remove_suffix(1); }
		while (!t.empty() && (t.front() == ' ' || t.front() == '\t' || t.front() == '\n')) { t.remove_prefix(1); }
		return t.empty() ? std::string_view{"ctbrowser"} : t;
	}

	static constexpr bool script_valid = script_type::valid;
};

} // namespace ctbrowser

#endif
