module;
#include <charconv>
#include <functional>
#include <cstdint>
#include <optional>
#include <string_view>

export module ctbrowser.layout:values;

import ctbrowser.core;

// Turning style strings into layout numbers.
//
// This is where "12px" becomes 12, and it happens HERE rather than in the
// style engine on purpose: a computed style holds many declarations the box
// tree never asks about, and parsing every value at resolution time would be
// work done for properties nobody reads. Layout parses what it needs, when it
// needs it.

export namespace ctbrowser::layout {

// How text is measured. Injected rather than assumed, because the real answer
// needs a font stack that belongs to the raster layer - and because a
// deterministic stub is what makes layout testable without fonts at all.
//
// It lives HERE, in the partition that depends on nothing, because both the box
// tree and the fragment tree need it. Putting it with the fragments made :box
// import :fragment, which imports :box - a cycle the module system rejects
// outright rather than letting it become a subtle build-order problem.
using measure_text_fn = std::function<float(std::string_view, float)>;

enum class unit : std::uint8_t { px, percent, em, rem, auto_, none };

struct length {
	float value = 0;
	unit u = unit::auto_;

	// `auto` is the ONLY value a caller has to special-case. Every other unit
	// answers resolve() given a basis, so there is deliberately no
	// "is_definite" predicate here - one existed, and every call site used it
	// to mean "not auto", which silently dropped percentages and em.
	[[nodiscard]] constexpr bool is_auto() const noexcept { return u == unit::auto_; }
	// Resolve against a containing-block basis. `auto` has no answer here -
	// the caller decides what auto means for the property it is resolving,
	// which differs between width (fill) and height (fit content).
	[[nodiscard]] constexpr float resolve(float basis, float font_size) const noexcept {
		switch (u) {
		case unit::px:
		case unit::none: return value;
		case unit::percent: return value / 100.0f * basis;
		case unit::em: return value * font_size;
		case unit::rem: return value * 16.0f;
		case unit::auto_: return 0;
		}
		return 0;
	}
};

[[nodiscard]] inline length parse_length(std::string_view text) {
	std::size_t i = 0;
	while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) { ++i; }
	text.remove_prefix(i);
	if (text.empty()) { return length{}; }
	if (text == "auto") { return length{0, unit::auto_}; }

	float value = 0;
	const auto [rest, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (ec != std::errc{}) { return length{}; }
	const std::string_view suffix{rest, static_cast<std::size_t>(text.data() + text.size() - rest)};
	if (suffix.starts_with("px")) { return length{value, unit::px}; }
	if (suffix.starts_with('%')) { return length{value, unit::percent}; }
	if (suffix.starts_with("rem")) { return length{value, unit::rem}; }
	if (suffix.starts_with("em")) { return length{value, unit::em}; }
	return length{value, unit::none}; // unitless: treated as px, like v1 did
}

// The `display` values the box tree distinguishes. Everything else collapses
// into one of these for now - a box tree that models `display` exhaustively
// before there is a flex or grid algorithm to consume it would be modelling
// nothing.
enum class display_kind : std::uint8_t { none, block, inline_level, inline_block };

[[nodiscard]] inline display_kind parse_display(std::string_view text, display_kind fallback) {
	if (text == "none") { return display_kind::none; }
	if (text == "block") { return display_kind::block; }
	if (text == "inline") { return display_kind::inline_level; }
	if (text == "inline-block") { return display_kind::inline_block; }
	if (!text.empty()) { return display_kind::block; } // flex/grid/table: block for now
	return fallback;
}

// The CSS box sides, from a 1-to-4-value shorthand plus per-side overrides.
struct side_lengths {
	length top, right, bottom, left;
};

// Tags that generate NO box unless a sheet overrides them. This is not an
// optimisation - without it a page's <script> source and <style> rules render
// as visible text, which is what v1's layout::detail::skipped_tag existed to
// prevent. It is a stand-in for the UA stylesheet's display:none rules, which
// arrive with ua.hpp's port.
[[nodiscard]] inline bool generates_no_box(std::string_view tag) {
	constexpr std::string_view hidden[] = {"head",  "style", "script", "title", "meta",
	                                       "link",  "base",  "template"};
	for (const std::string_view t : hidden) {
		if (t == tag) { return true; }
	}
	return false;
}

// The tag list HTML renders inline by default, when the sheet says nothing.
[[nodiscard]] inline bool is_inline_by_default(std::string_view tag) {
	constexpr std::string_view inline_tags[] = {
	    "a",    "span", "b",   "i",    "u",     "s",      "em",  "strong", "code", "small",
	    "big",  "mark", "sub", "sup",  "tt",    "kbd",    "samp","cite",   "var",  "dfn",
	    "abbr", "ins",  "del", "img",  "q",     "time",   "output", "label", "br"};
	for (const std::string_view t : inline_tags) {
		if (t == tag) { return true; }
	}
	return false;
}

// Elements sized by what they ARE rather than by what they contain. A <canvas>
// is its pixel buffer; an <input> is a field wide enough to type in. Laying
// either out from its children gives a box of zero height, which is what
// happens to every parser that does not know about replaced elements.
[[nodiscard]] inline bool is_replaced_tag(std::string_view tag) {
	constexpr std::string_view names[] = {"canvas", "img",    "input",  "select", "textarea",
	                                      "button", "video",  "iframe", "embed",  "object"};
	for (const std::string_view t : names) {
		if (t == tag) { return true; }
	}
	return false;
}

// What `display` a tag has before any sheet speaks.
[[nodiscard]] inline display_kind default_display_for(std::string_view tag) {
	if (generates_no_box(tag)) { return display_kind::none; }
	return is_inline_by_default(tag) ? display_kind::inline_level : display_kind::block;
}

} // namespace ctbrowser::layout
