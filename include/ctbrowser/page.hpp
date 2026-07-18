#ifndef CTBROWSER__PAGE__HPP
#define CTBROWSER__PAGE__HPP

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

// --- collect the concatenated text of every <tag> element in the
// document type (document order, '\n' between pieces)

template <typename Node, typename Tag> struct tag_collect {
	template <size_t... I>
	static constexpr size_t children_size(std::index_sequence<I...>) noexcept {
		return (tag_collect<decltype(Node::template child<I>()), Tag>::size() + ... + 0);
	}
	static constexpr size_t size() noexcept {
		if constexpr (Node::type == cthtml::kind::element) {
			size_t n = 0;
			if (Node::name() == Tag::view()) { n += Node::text().size() + 1; }
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
				for (const char c : Node::text()) { *at++ = c; }
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

} // namespace detail

// the assembled application: DOM type + stylesheet type + script type
template <CTJS_STRING_INPUT Src> struct page {
	using doc_type = decltype(cthtml::parse<Src>());

	using style_source = detail::tag_text<doc_type, detail::style_tag>;
	using script_source = detail::tag_text<doc_type, detail::script_tag>;

	// the page's CSS, parsed at compile time (a ctcss stylesheet type)
	static constexpr auto sheet() noexcept {
		return ctcss::parse<detail::to_fixed<style_source>()>();
	}
	using sheet_type = decltype(sheet());

	// the page's JS, parsed at compile time (a ctjs script)
	using script_type = ctjs::script_t<detail::to_fixed<script_source>()>;

	// the <title> text ("" when absent)
	static constexpr std::string_view title() noexcept {
		using head = decltype(doc_type::template get<"head">());
		if constexpr (head::template contains<"title">()) {
			return decltype(head::template get<"title">())::text();
		} else {
			return "ctbrowser";
		}
	}

	static constexpr bool script_valid = script_type::valid;
};

} // namespace ctbrowser

#endif
