#pragma once
#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>

// A regular-expression engine.
//
// Ported from ctjs's `rxd`, which was already a self-contained backtracking
// matcher and coupled to its host by exactly two calls. Two changes make it
// fit here: a compile failure is a FLAG on the program rather than a thrown
// exception, because nothing in this VM throws; and the match result was
// already carrying its positions - only ctjs's exec wrapper discarded them -
// so `.index`, which p5.js reads 143 times, comes for free.
//
// Added here: lookahead `(?=`/`(?!`, the sticky `y` flag, named groups
// `(?<name>...)`, and - 2026-09-12, for test262's String.prototype rows -
// backreferences (`\1`, `\k<name>`), lookbehind (`(?<=`/`(?<!`), the `s`
// flag, and the specification's rule that a quantified group's captures are
// cleared on every repetition (`/(a)|(b)/` twice over "ab" leaves group 1
// undefined). A pattern character above U+00FF is matched as its UTF-8 bytes,
// which is what the subject is made of, and a CLASS is a set of CODE POINT
// ranges matched against the code point decoded at the position.
//
// 2026-09-17: `\p{...}` / `\P{...}` (22.2.2.9) over the Unicode Character
// Database in lib/Script/regex_properties.inc, Canonicalize under `iu` by
// simple case folding (22.2.2.7.3), and a class is SORTED so a hit is one
// binary search - a property brings hundreds of ranges and the subject a
// million code points. A quantified single-character piece is matched by a
// loop rather than a recursion per repetition, for the same subjects.

namespace ctbrowser::script::rx {

struct rx_range {
	char32_t lo, hi;
};
struct rx_property_set {
	std::span<const rx_range> ranges;    // sorted, disjoint
	std::span<const char32_t> sequences; // {length, code points...} runs; table 69 only
};
// 22.2.2.9 UnicodeMatchProperty + UnicodeMatchPropertyValue over the UCD.
// `value` is empty for the `\p{Name}` form; `strings` admits the properties
// of strings (table 69, the `v` flag). Nullopt: not a name the specification
// lists, which is a SyntaxError. Defined in lib/Script/regex_properties.cpp.
std::optional<rx_property_set> rx_property_lookup(std::string_view name, std::string_view value,
                                                  bool strings);
// CaseFolding.txt's simple fold (C + S) - Canonicalize under `iu` - and every
// OTHER code point that folds to `folded`, at most eight, written to `out`.
char32_t rx_fold_simple(char32_t cp);
std::size_t rx_unfold(char32_t folded, char32_t (&out)[8]);

struct rx_class {
	bool neg = false;
	std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
	// Under `v`, a class may hold STRINGS too (`\q{ab|c}`, a property of
	// strings): UTF-8, tried longest first, before the code points.
	std::vector<std::string> strings;
};

// Sorted and merged, so a hit is one binary search and a complement is a
// walk. Every class goes through here once its atoms are read.
inline void rx_class_normalize(rx_class & cc) {
	std::sort(cc.ranges.begin(), cc.ranges.end());
	std::vector<std::pair<std::uint32_t, std::uint32_t>> merged;
	for (const auto & r : cc.ranges) {
		if (!merged.empty() &&
		    (r.first <= merged.back().second || r.first == merged.back().second + 1)) {
			merged.back().second = std::max(merged.back().second, r.second);
		} else {
			merged.push_back(r);
		}
	}
	cc.ranges = std::move(merged);
	// Longest first, so `\q{ab|a}` prefers "ab" (22.2.2.9: the strings of a
	// class are tried by descending length).
	std::stable_sort(cc.strings.begin(), cc.strings.end(),
	                 [](const std::string & a, const std::string & b) { return a.size() > b.size(); });
	cc.strings.erase(std::unique(cc.strings.begin(), cc.strings.end()), cc.strings.end());
}

// The complement over the code space, of a NORMALIZED range list.
inline std::vector<std::pair<std::uint32_t, std::uint32_t>>
rx_complement(const std::vector<std::pair<std::uint32_t, std::uint32_t>> & ranges) {
	std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
	std::uint32_t next = 0;
	for (const auto & [lo, hi] : ranges) {
		if (lo > next) { out.push_back({next, lo - 1}); }
		if (hi >= 0x10FFFFu) { return out; }
		next = hi + 1;
	}
	out.push_back({next, 0x10FFFFu});
	return out;
}
struct rx_alt;
struct rx_piece {
	enum kind_t { lit, any, cls, grp, bol, eol, wordb, nwordb, ahead, nahead, behind, nbehind, backref } kind = lit;
	char c = 0;
	// A literal above one byte: the UTF-8 sequence of the code point, matched
	// as a run. Empty for the ordinary one-byte case, which stays in `c`.
	std::string text;
	rx_class cc;
	std::shared_ptr<rx_alt> sub;
	std::int32_t cap = -1; // capture slot, -1 = (?:); for backref, the slot it names
	// `\k<name>` before the name is resolved (a group may be defined later in
	// the pattern), and the capture slots a group ENCLOSES - [caps_lo, caps_hi)
	// - which a repetition resets.
	std::string ref_name;
	std::int32_t caps_lo = 0, caps_hi = 0;
	std::int32_t min = 1;
	std::int32_t max = 1; // -1 = unbounded
	bool greedy = true;
};
using rx_seq = std::vector<rx_piece>;
struct rx_alt {
	std::vector<rx_seq> alts;
};
struct rx_prog {
	std::shared_ptr<rx_alt> root;
	std::int32_t ngroups = 0;
	bool icase = false, global = false, multi = false, sticky = false;
	bool dotall = false, unicode = false, unicode_sets = false, indices = false;
	// A pattern that did not compile. Checked by the caller; a program that is
	// not ok never matches.
	bool ok = true;
	std::string error;
	// Named groups: `(?<name>...)` -> the capture slot it fills.
	std::vector<std::pair<std::string, std::int32_t>> names;
};

// A malformed pattern marks the program instead of throwing: this VM has no
// exceptions, and a caller that gets `ok == false` can report it in its own
// terms.
inline void rx_fail(rx_prog & p, std::string_view src) {
    if (p.error.empty()) { p.error = "Invalid regular expression: /" + std::string{src} + "/"; }
    p.ok = false;
}

inline constexpr void rx_class_escape(rx_class & out, char e) {
	switch (e) {
	case 'd': out.ranges.push_back({'0', '9'}); break;
	case 'w':
		out.ranges.push_back({'a', 'z'});
		out.ranges.push_back({'A', 'Z'});
		out.ranges.push_back({'0', '9'});
		out.ranges.push_back({'_', '_'});
		break;
	case 's':
		// WhiteSpace (12.2) and LineTerminator (12.3): the ASCII five, the
		// Zs category, and NBSP, LS, PS and the BOM.
		out.ranges.push_back({'\t', '\r'});
		out.ranges.push_back({' ', ' '});
		out.ranges.push_back({0xA0u, 0xA0u});
		out.ranges.push_back({0x1680u, 0x1680u});
		out.ranges.push_back({0x2000u, 0x200Au});
		out.ranges.push_back({0x2028u, 0x2029u});
		out.ranges.push_back({0x202Fu, 0x202Fu});
		out.ranges.push_back({0x205Fu, 0x205Fu});
		out.ranges.push_back({0x3000u, 0x3000u});
		out.ranges.push_back({0xFEFFu, 0xFEFFu});
		break;
	default: break;
	}
}

// `\xHH`, `\uHHHH` and `\u{H..}` AS A NUMBER, which they were not.
//
// `rx_escape_char` falls through to "the char itself" for anything it does not
// know, so `\u` was the letter `u` and the four hex digits after it became four
// more class members. `[\udc00-\udfff]` therefore parsed as {u,d,c,0,f} plus
// the RANGE '0'-'u' - which matches most of ASCII, and matched it silently.
//
// WPT is what found it, and the damage was not in the tests but in the REPORT:
// testharness.js sanitises every test NAME and every assertion MESSAGE through
// `str.replace(/([\ud800-\udbff]+)...|.../g, ...)`, so that bogus range hit
// nearly every character of every string the harness had to say. 2,343 failing
// subtests in dom/nodes came back with their names rewritten into runs of
// `U+61U+73U+73...` - a corpus-wide corruption of the results, from one escape.
//
// `i` is positioned just after the escape letter `e`; on success it advances
// past the digits. A malformed escape returns false and is left to
// rx_escape_char, which keeps the previous lenient behaviour.
inline bool rx_code_point_escape(std::string_view src, std::size_t & i, char e,
                                 std::uint32_t & cp) {
	std::size_t want = 0;
	if (e == 'x') {
		want = 2;
	} else if (e == 'u') {
		if (i < src.size() && src[i] == '{') {
			std::uint32_t value = 0;
			std::size_t j = i + 1;
			bool any = false;
			for (; j < src.size() && hex_value(src[j]) >= 0; ++j) {
				value = value * 16 + static_cast<std::uint32_t>(hex_value(src[j]));
				any = true;
			}
			if (!any || j >= src.size() || src[j] != '}') { return false; }
			cp = value;
			i = j + 1;
			return true;
		}
		want = 4;
	} else {
		return false;
	}
	if (i + want > src.size()) { return false; }
	std::uint32_t value = 0;
	for (std::size_t k = 0; k < want; ++k) {
		const int d = hex_value(src[i + k]);
		if (d < 0) { return false; }
		value = value * 16 + static_cast<std::uint32_t>(d);
	}
	cp = value;
	i += want;
	return true;
}

// One code point decoded forward from `at`, and its width in bytes. Core's
// decode_utf8 has the contract this needs: a byte that does not start a
// well-formed sequence is one code point of width 1 (its own value), so a
// scan never stalls and a stray byte still compares.
inline std::uint32_t rx_utf8_decode(std::string_view s, std::size_t at, std::size_t & width) {
	const std::size_t start = at;
	const auto cp = static_cast<std::uint32_t>(decode_utf8(s, at));
	width = at - start;
	return cp;
}

inline char rx_escape_char(char e) {
	switch (e) {
	case 'n': return '\n';
	case 't': return '\t';
	case 'r': return '\r';
	case 'f': return '\f';
	case 'v': return '\v';
	case '0': return '\0';
	default: return e; // \. \/ \[ \\ etc: the char itself
	}
}

// `\p{Name}` / `\p{Name=Value}` (22.2.1 UnicodePropertyValueExpression),
// `i` just past the `p` or `P`. No loose matching: the name is ControlLetters
// and `_`, the value adds digits, and the spelling must be one the tables
// carry. The set lands in `out` - complemented for `\P`, which is the
// CharacterComplement of 22.2.2.9 and happens BEFORE Canonicalize, unlike
// `[^...]`. False for a bad spelling, an unknown name, or a negated property
// of strings (22.2.1.1 MayContainStrings) - each a SyntaxError.
inline bool rx_parse_property(std::string_view src, std::size_t & i, const rx_prog & p, bool negate,
                              rx_class & out) {
	if (i >= src.size() || src[i] != '{') { return false; }
	std::size_t j = i + 1;
	const auto word = [&](bool digits) {
		const std::size_t start = j;
		while (j < src.size() && ((src[j] >= 'a' && src[j] <= 'z') || (src[j] >= 'A' && src[j] <= 'Z') ||
		                          src[j] == '_' || (digits && src[j] >= '0' && src[j] <= '9'))) {
			++j;
		}
		return src.substr(start, j - start);
	};
	const std::string_view name = word(false);
	std::string_view value;
	if (j < src.size() && src[j] == '=') {
		++j;
		value = word(true);
		if (value.empty()) { return false; }
	}
	if (name.empty() || j >= src.size() || src[j] != '}') { return false; }
	const auto set = rx_property_lookup(name, value, p.unicode_sets);
	if (!set) { return false; }
	const bool of_strings = value.empty() && !rx_property_lookup(name, value, false);
	if (of_strings && negate) { return false; }
	i = j + 1;
	std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
	ranges.reserve(set->ranges.size());
	for (const rx_range & r : set->ranges) { ranges.push_back({r.lo, r.hi}); }
	if (negate) { ranges = rx_complement(ranges); }
	out.ranges.insert(out.ranges.end(), ranges.begin(), ranges.end());
	for (std::size_t at = 0; at < set->sequences.size();) {
		const std::size_t len = set->sequences[at++];
		std::string seq;
		for (std::size_t n = 0; n < len; ++n) { append_utf8(seq, set->sequences[at++]); }
		out.strings.push_back(std::move(seq));
	}
	return true;
}

// THE `v` FLAG'S CLASS (22.2.1 ClassSetExpression): a union of characters,
// ranges, nested classes, `\q{a|bc}` string disjunctions and escapes, or ONE
// chain of `--` subtractions or `&&` intersections - the three do not mix.
// A set is code points plus strings; a string of one code point IS a code
// point (22.2.1.1 MayContainStrings), so `\q{a}` lands in the ranges.
//
// Under `vi` the specification folds every operand first (MaybeSimpleCase-
// Folding) and complements within the folded code points; here an operand
// is CLOSED under the fold instead - every code point sharing a member's
// fold joins - which the matcher's own fold check then reads identically,
// and which keeps the set operations plain range arithmetic.
using rx_ranges_t = std::vector<std::pair<std::uint32_t, std::uint32_t>>;
rx_ranges_t rx_fold_closure(const rx_ranges_t & ranges); // regex_properties.cpp

inline rx_ranges_t rx_ranges_intersect(const rx_ranges_t & a, const rx_ranges_t & b) {
	rx_ranges_t out;
	std::size_t x = 0, y = 0;
	while (x < a.size() && y < b.size()) {
		const std::uint32_t lo = std::max(a[x].first, b[y].first);
		const std::uint32_t hi = std::min(a[x].second, b[y].second);
		if (lo <= hi) { out.push_back({lo, hi}); }
		if (a[x].second < b[y].second) { ++x; } else { ++y; }
	}
	return out;
}

// The set an operand yields; ranges normalized, strings of two or more code
// points (or none) only.
struct rx_set {
	rx_ranges_t ranges;
	std::vector<std::string> strings;
};

inline void rx_set_normalize(rx_set & s) {
	rx_class cc;
	cc.ranges = std::move(s.ranges);
	cc.strings = std::move(s.strings);
	rx_class_normalize(cc);
	std::sort(cc.strings.begin(), cc.strings.end());
	cc.strings.erase(std::unique(cc.strings.begin(), cc.strings.end()), cc.strings.end());
	s.ranges = std::move(cc.ranges);
	s.strings = std::move(cc.strings);
}

inline bool rx_class_set_expression(std::string_view src, std::size_t & i, rx_prog & p, rx_set & out);

// ClassSetCharacter: one code point, or false. `i` at the character.
inline bool rx_class_set_character(std::string_view src, std::size_t & i, std::uint32_t & cp) {
	if (i >= src.size()) { return false; }
	const char c = src[i];
	if (c == '\\') {
		if (i + 1 >= src.size()) { return false; }
		const char e = src[i + 1];
		i += 2;
		if (rx_code_point_escape(src, i, e, cp)) { return true; }
		if (e == 'b') { cp = 0x08; return true; }
		if (e == 'c') {
			if (i < src.size() && ((src[i] >= 'a' && src[i] <= 'z') || (src[i] >= 'A' && src[i] <= 'Z'))) {
				cp = static_cast<std::uint32_t>(src[i++]) % 32;
				return true;
			}
			return false;
		}
		if (e == '0' && (i >= src.size() || src[i] < '0' || src[i] > '9')) { cp = 0; return true; }
		// CharacterEscape's ControlEscape and identity escapes (the syntax
		// characters and `/`), and the ClassSetReservedPunctuators.
		if (std::string_view{"nrtfv"}.find(e) != std::string_view::npos) {
			cp = static_cast<unsigned char>(rx_escape_char(e));
			return true;
		}
		if (std::string_view{"^$\\.*+?()[]{}|/&-!#%,:;<=>@`~"}.find(e) != std::string_view::npos) {
			cp = static_cast<unsigned char>(e);
			return true;
		}
		return false;
	}
	// ClassSetSyntaxCharacter may not stand bare, nor a ClassSetReserved-
	// DoublePunctuator (`&&`, `!!`, ...).
	if (std::string_view{"()[]{}/-\\|"}.find(c) != std::string_view::npos) { return false; }
	if (i + 1 < src.size() && src[i + 1] == c &&
	    std::string_view{"&!#$%*+,.:;<=>?@^`~"}.find(c) != std::string_view::npos) {
		return false;
	}
	std::size_t width = 1;
	cp = rx_utf8_decode(src, i, width);
	i += width;
	return true;
}

// ClassSetOperand, plus a ClassSetRange when `range` is allowed (a union).
inline bool rx_class_set_operand(std::string_view src, std::size_t & i, rx_prog & p, bool range,
                                 rx_set & out, bool * ranged = nullptr) {
	if (i >= src.size()) { return false; }
	const bool fold = p.icase;
	if (src[i] == '[') {
		// NestedClass, complemented when it starts with `^` - and then it
		// may not contain strings.
		++i;
		const bool negate = i < src.size() && src[i] == '^';
		if (negate) { ++i; }
		rx_set nested;
		if (!rx_class_set_expression(src, i, p, nested)) { return false; }
		if (negate) {
			if (!nested.strings.empty()) { return false; }
			nested.ranges = rx_complement(fold ? rx_fold_closure(nested.ranges) : nested.ranges);
		}
		out = std::move(nested);
		return true;
	}
	if (src[i] == '\\' && i + 1 < src.size()) {
		const char e = src[i + 1];
		if (e == 'q') {
			// ClassStringDisjunction: `\q{a|bc|}` - each alternative a run
			// of ClassSetCharacters; one code point is a code point.
			if (i + 2 >= src.size() || src[i + 2] != '{') { return false; }
			i += 3;
			std::string current;
			std::size_t points = 0;
			const auto finish = [&]() {
				if (points == 1) {
					std::size_t width = 1;
					const std::uint32_t only = rx_utf8_decode(current, 0, width);
					out.ranges.push_back({only, only});
				} else {
					out.strings.push_back(current);
				}
				current.clear();
				points = 0;
			};
			for (;;) {
				if (i >= src.size()) { return false; }
				if (src[i] == '}') { finish(); ++i; break; }
				if (src[i] == '|') { finish(); ++i; continue; }
				std::uint32_t cp = 0;
				if (!rx_class_set_character(src, i, cp)) { return false; }
				append_utf8(current, cp);
				++points;
			}
			if (fold) { out.ranges = rx_fold_closure(out.ranges); }
			return true;
		}
		if (e == 'p' || e == 'P') {
			i += 2;
			rx_class cc;
			if (!rx_parse_property(src, i, p, false, cc)) { return false; }
			if (e == 'P' && !cc.strings.empty()) { return false; }
			rx_class_normalize(cc);
			if (fold) { cc.ranges = rx_fold_closure(cc.ranges); }
			if (e == 'P') { cc.ranges = rx_complement(cc.ranges); }
			out.ranges = std::move(cc.ranges);
			out.strings = std::move(cc.strings);
			return true;
		}
		if (std::string_view{"dswDSW"}.find(e) != std::string_view::npos) {
			i += 2;
			rx_class cc;
			rx_class_escape(cc, static_cast<char>(e | 0x20));
			rx_class_normalize(cc);
			if (fold) { cc.ranges = rx_fold_closure(cc.ranges); }
			if (e < 'a') { cc.ranges = rx_complement(cc.ranges); }
			out.ranges = std::move(cc.ranges);
			return true;
		}
	}
	std::uint32_t lo = 0;
	if (!rx_class_set_character(src, i, lo)) { return false; }
	std::uint32_t hi = lo;
	if (range && i + 1 < src.size() && src[i] == '-' && src[i + 1] != '-') {
		++i;
		if (!rx_class_set_character(src, i, hi) || hi < lo) { return false; }
		if (ranged != nullptr) { *ranged = true; }
	}
	out.ranges.push_back({lo, hi});
	if (fold) { out.ranges = rx_fold_closure(out.ranges); }
	return true;
}

// ClassSetExpression up to and past the closing `]`.
inline bool rx_class_set_expression(std::string_view src, std::size_t & i, rx_prog & p, rx_set & out) {
	out = rx_set{};
	if (i < src.size() && src[i] == ']') { ++i; return true; }
	rx_set first;
	bool ranged = false;
	if (!rx_class_set_operand(src, i, p, true, first, &ranged)) { return false; }
	rx_set_normalize(first);
	const auto two = [&](char c) { return i + 1 < src.size() && src[i] == c && src[i + 1] == c; };
	if (two('-') || two('&')) {
		// ClassSubtraction / ClassIntersection: a chain of one operator,
		// operands without ranges (a range is a ClassUnion's).
		if (ranged) { return false; }
		const char op = src[i];
		out = std::move(first);
		while (two(op)) {
			i += 2;
			rx_set next;
			if (!rx_class_set_operand(src, i, p, false, next)) { return false; }
			rx_set_normalize(next);
			std::vector<std::string> strings;
			if (op == '&') {
				out.ranges = rx_ranges_intersect(out.ranges, next.ranges);
				std::set_intersection(out.strings.begin(), out.strings.end(), next.strings.begin(),
				                      next.strings.end(), std::back_inserter(strings));
			} else {
				out.ranges = rx_ranges_intersect(out.ranges, rx_complement(next.ranges));
				std::set_difference(out.strings.begin(), out.strings.end(), next.strings.begin(),
				                    next.strings.end(), std::back_inserter(strings));
			}
			out.strings = std::move(strings);
		}
		if (i >= src.size() || src[i] != ']') { return false; }
		++i;
		return true;
	}
	// ClassUnion
	out = std::move(first);
	while (i < src.size() && src[i] != ']') {
		if (two('-') || two('&')) { return false; }
		rx_set next;
		if (!rx_class_set_operand(src, i, p, true, next)) { return false; }
		out.ranges.insert(out.ranges.end(), next.ranges.begin(), next.ranges.end());
		out.strings.insert(out.strings.end(), next.strings.begin(), next.strings.end());
	}
	if (i >= src.size()) { return false; }
	++i;
	rx_set_normalize(out);
	return true;
}

inline std::shared_ptr<rx_alt> rx_parse_alt(std::string_view src, std::size_t & i, rx_prog & p,
                                            bool top);

inline rx_piece rx_parse_atom(std::string_view src, std::size_t & i, rx_prog & p) {
	rx_piece pc;
	const char c = src[i];
	if (c == '(') {
		++i;
		pc.kind = rx_piece::grp;
		if (i + 1 < src.size() && src[i] == '?' && src[i + 1] == ':') {
			i += 2;
		} else if (i + 1 < src.size() && src[i] == '?' && src[i + 1] == '=') {
			i += 2;
			pc.kind = rx_piece::ahead;
		} else if (i + 1 < src.size() && src[i] == '?' && src[i + 1] == '!') {
			i += 2;
			pc.kind = rx_piece::nahead;
		} else if (i + 2 < src.size() && src[i] == '?' && src[i + 1] == '<' && src[i + 2] != '=' &&
		           src[i + 2] != '!') {
			// `(?<name>...)` - an ordinary capture that also answers to a name
			i += 2;
			const std::size_t start = i;
			while (i < src.size() && src[i] != '>') { ++i; }
			const std::string name{src.substr(start, i - start)};
			if (i < src.size()) { ++i; } // past '>'
			pc.cap = p.ngroups++;
			p.names.emplace_back(name, pc.cap);
		} else if (i + 2 < src.size() && src[i] == '?' && src[i + 1] == '<') {
			// `(?<=...)` and `(?<!...)`, the two lookbehinds.
			pc.kind = src[i + 2] == '=' ? rx_piece::behind : rx_piece::nbehind;
			i += 3;
		} else {
			pc.cap = p.ngroups++;
		}
		pc.caps_lo = p.ngroups;
		pc.sub = rx_parse_alt(src, i, p, false);
		pc.caps_hi = p.ngroups;
		if (i >= src.size() || src[i] != ')') { rx_fail(p, src); }
		++i;
		return pc;
	}
	if (c == '[') {
		++i;
		pc.kind = rx_piece::cls;
		if (i < src.size() && src[i] == '^') {
			pc.cc.neg = true;
			++i;
		}
		if (p.unicode_sets) {
			rx_set set;
			if (!rx_class_set_expression(src, i, p, set) || (pc.cc.neg && !set.strings.empty())) {
				rx_fail(p, src);
				return pc;
			}
			pc.cc.ranges = std::move(set.ranges);
			pc.cc.strings = std::move(set.strings);
			rx_class_normalize(pc.cc);
			return pc;
		}
		// `[]` is the EMPTY class and `[^]` matches anything (22.2.2.9 - a
		// ClassContents may be empty); the `]` is never literal in a class.
		// One class atom: an escape (\xHH, \uHHHH, \u{...} or a letter), or
		// the code point written in the pattern itself, which is UTF-8 here.
		const auto class_atom = [&](std::uint32_t & cp) {
			if (src[i] == '\\' && i + 1 < src.size()) {
				const char e = src[i + 1];
				i += 2;
				if (rx_code_point_escape(src, i, e, cp)) { return; }
				cp = static_cast<unsigned char>(rx_escape_char(e));
				return;
			}
			std::size_t width = 1;
			cp = rx_utf8_decode(src, i, width);
			i += width;
		};
		const bool unicode_mode = p.unicode || p.unicode_sets;
		while (i < src.size() && src[i] != ']') {
			if (src[i] == '\\' && i + 1 < src.size() &&
			    (src[i + 1] == 'd' || src[i + 1] == 'w' || src[i + 1] == 's')) {
				rx_class_escape(pc.cc, src[i + 1]);
				i += 2;
				continue;
			}
			if (src[i] == '\\' && i + 1 < src.size() &&
			    (src[i + 1] == 'D' || src[i + 1] == 'W' || src[i + 1] == 'S')) {
				// The complement, spelled out: `[\D]` is every code point
				// but the digits, not the letter D.
				rx_class one;
				rx_class_escape(one, static_cast<char>(src[i + 1] + ('a' - 'A')));
				rx_class_normalize(one);
				const auto rest = rx_complement(one.ranges);
				pc.cc.ranges.insert(pc.cc.ranges.end(), rest.begin(), rest.end());
				i += 2;
				continue;
			}
			if (unicode_mode && src[i] == '\\' && i + 1 < src.size() &&
			    (src[i + 1] == 'p' || src[i + 1] == 'P')) {
				const bool negate = src[i + 1] == 'P';
				i += 2;
				if (!rx_parse_property(src, i, p, negate, pc.cc)) {
					rx_fail(p, src);
					return pc;
				}
				continue;
			}
			std::uint32_t lo = 0;
			class_atom(lo);
			std::uint32_t hi = lo;
			if (i + 1 < src.size() && src[i] == '-' && src[i + 1] != ']') {
				++i;
				class_atom(hi);
			}
			pc.cc.ranges.push_back({lo, hi});
		}
		if (i >= src.size()) { rx_fail(p, src); }
		++i; // ']'
		// A negated class may not contain strings (22.2.1.1 MayContainStrings).
		if (pc.cc.neg && !pc.cc.strings.empty()) { rx_fail(p, src); }
		rx_class_normalize(pc.cc);
		return pc;
	}
	if (c == '.') {
		++i;
		pc.kind = rx_piece::any;
		return pc;
	}
	if (c == '^') {
		++i;
		pc.kind = rx_piece::bol;
		return pc;
	}
	if (c == '$') {
		++i;
		pc.kind = rx_piece::eol;
		return pc;
	}
	if (c == '\\' && i + 1 < src.size()) {
		const char e = src[i + 1];
		i += 2;
		if (e == 'b') { pc.kind = rx_piece::wordb; return pc; }
		if (e == 'B') { pc.kind = rx_piece::nwordb; return pc; }
		// A BACKREFERENCE: `\1`..`\99` by number, `\k<name>` by name. The
		// number is read greedily; whether the group exists is checked after
		// the whole pattern is parsed, because a reference may point forward.
		// Annex B makes `\8` with no eighth group the literal "8", and that
		// leniency is kept for the number form only.
		if (e >= '1' && e <= '9') {
			const std::size_t start = i - 1;
			std::int32_t n = e - '0';
			while (i < src.size() && src[i] >= '0' && src[i] <= '9') {
				n = n * 10 + (src[i++] - '0');
			}
			pc.kind = rx_piece::backref;
			pc.cap = n - 1;
			pc.text = std::string{src.substr(start, i - start)}; // kept for Annex B
			return pc;
		}
		if (e == 'k' && i < src.size() && src[i] == '<') {
			const std::size_t start = i + 1;
			std::size_t close = start;
			while (close < src.size() && src[close] != '>') { ++close; }
			if (close >= src.size()) {
				rx_fail(p, src);
				return pc;
			}
			pc.kind = rx_piece::backref;
			pc.ref_name = std::string{src.substr(start, close - start)};
			i = close + 1;
			return pc;
		}
		if (e == 'd' || e == 'w' || e == 's') {
			pc.kind = rx_piece::cls;
			rx_class_escape(pc.cc, e);
			rx_class_normalize(pc.cc);
			return pc;
		}
		if (e == 'D' || e == 'W' || e == 'S') {
			pc.kind = rx_piece::cls;
			pc.cc.neg = true;
			rx_class_escape(pc.cc, static_cast<char>(e + ('a' - 'A')));
			rx_class_normalize(pc.cc);
			return pc;
		}
		if ((e == 'p' || e == 'P') && (p.unicode || p.unicode_sets)) {
			pc.kind = rx_piece::cls;
			if (p.unicode_sets) {
				// Under `v` the escape is a ClassSetOperand wherever it
				// stands, folded before it is complemented.
				i -= 2;
				rx_set set;
				if (!rx_class_set_operand(src, i, p, false, set)) {
					rx_fail(p, src);
					return pc;
				}
				pc.cc.ranges = std::move(set.ranges);
				pc.cc.strings = std::move(set.strings);
			} else if (!rx_parse_property(src, i, p, e == 'P', pc.cc)) {
				rx_fail(p, src);
				return pc;
			}
			rx_class_normalize(pc.cc);
			return pc;
		}
		std::uint32_t cp = 0;
		if (rx_code_point_escape(src, i, e, cp)) {
			pc.kind = rx_piece::lit;
			if (cp > 0x7F) {
				// THE SUBJECT IS UTF-8, so a code point above ASCII is the
				// run of bytes that spells it there, matched as a sequence.
				// (Below 0x80 a code point and its byte are the same thing.)
				append_utf8(pc.text, cp);
				return pc;
			}
			pc.c = static_cast<char>(cp);
			return pc;
		}
		pc.kind = rx_piece::lit;
		pc.c = rx_escape_char(e);
		return pc;
	}
	pc.kind = rx_piece::lit;
	if (static_cast<unsigned char>(c) >= 0xC0u) {
		// A pattern character above ASCII is ONE piece of however many
		// bytes, so `/é+/` repeats the character and `/k/iu` can fold it -
		// one byte at a time it was the last byte that got the quantifier.
		std::size_t width = 1;
		rx_utf8_decode(src, i, width);
		if (width > 1) {
			pc.text = std::string{src.substr(i, width)};
			i += width;
			return pc;
		}
	}
	pc.c = c;
	++i;
	return pc;
}

inline constexpr void rx_parse_quant(std::string_view src, std::size_t & i, rx_piece & pc) {
	if (i >= src.size()) { return; }
	const char c = src[i];
	if (c == '*') { pc.min = 0; pc.max = -1; ++i; }
	else if (c == '+') { pc.min = 1; pc.max = -1; ++i; }
	else if (c == '?') { pc.min = 0; pc.max = 1; ++i; }
	else if (c == '{') {
		std::size_t j = i + 1;
		std::int32_t lo = 0;
		bool has = false;
		while (j < src.size() && src[j] >= '0' && src[j] <= '9') {
			lo = lo * 10 + (src[j++] - '0');
			has = true;
		}
		if (!has) { return; } // literal '{'
		std::int32_t hi = lo;
		if (j < src.size() && src[j] == ',') {
			++j;
			if (j < src.size() && src[j] == '}') { hi = -1; }
			else {
				hi = 0;
				while (j < src.size() && src[j] >= '0' && src[j] <= '9') {
					hi = hi * 10 + (src[j++] - '0');
				}
			}
		}
		if (j >= src.size() || src[j] != '}') { return; }
		pc.min = lo;
		pc.max = hi;
		i = j + 1;
	} else {
		return;
	}
	if (i < src.size() && src[i] == '?') {
		pc.greedy = false;
		++i;
	}
}

inline std::shared_ptr<rx_alt> rx_parse_alt(std::string_view src, std::size_t & i, rx_prog & p,
                                            bool top) {
	auto out = std::make_shared<rx_alt>();
	out->alts.emplace_back();
	while (i < src.size()) {
		const char c = src[i];
		if (c == ')') {
			if (top) { rx_fail(p, src); }
			break;
		}
		if (c == '|') {
			++i;
			out->alts.emplace_back();
			continue;
		}
		rx_piece pc = rx_parse_atom(src, i, p);
		rx_parse_quant(src, i, pc);
		out->alts.back().push_back(std::move(pc));
	}
	return out;
}

// After the whole pattern is parsed: a `\k<name>` finds its slot (the first
// group of that name - a duplicate name in another alternative is matched
// by whichever participated, see the matcher), an unknown name is a
// SyntaxError, and a numbered reference past the last group falls back to
// Annex B's literal digits.
inline void rx_resolve_backrefs(rx_alt & alt, rx_prog & p, std::string_view src) {
	for (rx_seq & sq : alt.alts) {
		for (rx_piece & pc : sq) {
			if (pc.sub) { rx_resolve_backrefs(*pc.sub, p, src); }
			if (pc.kind != rx_piece::backref) { continue; }
			if (!pc.ref_name.empty()) {
				pc.cap = -1;
				for (const auto & [name, slot] : p.names) {
					if (name == pc.ref_name) {
						pc.cap = slot;
						break;
					}
				}
				if (pc.cap < 0) { rx_fail(p, src); }
			} else if (pc.cap >= p.ngroups) {
				// Annex B.1.2: a number past the last group is a LEGACY OCTAL
				// escape (`\1` with no group is U+0001, up to `\377`), except
				// that `\8` and `\9` are the identity escapes of their digit.
				// Whatever digits the octal does not consume are literal.
				const std::string digits = pc.text;
				pc.kind = rx_piece::lit;
				pc.text.clear();
				std::size_t used = 0;
				if (digits[0] == '8' || digits[0] == '9') {
					pc.text = digits;
				} else {
					std::uint32_t code = 0;
					while (used < digits.size() && used < 3 && digits[used] >= '0' &&
					       digits[used] <= '7' && code * 8 + static_cast<std::uint32_t>(digits[used] - '0') <= 0377) {
						code = code * 8 + static_cast<std::uint32_t>(digits[used++] - '0');
					}
					append_utf8(pc.text, code);
					pc.text += digits.substr(used);
				}
			} else {
				pc.text.clear();
			}
		}
	}
}

inline rx_prog rx_compile(std::string_view source, std::string_view flags) {
	rx_prog p;
	std::string seen;
	for (const char f : flags) {
		// A repeated flag is a SyntaxError (22.2.3.3 step 5): `/a/gg`.
		if (seen.find(f) != std::string::npos) {
			p.ok = false;
			p.error = "Invalid regular expression flags: " + std::string{flags};
			break;
		}
		seen.push_back(f);
		if (f == 'i') { p.icase = true; }
		else if (f == 'g') { p.global = true; }
		else if (f == 'm') { p.multi = true; }
		else if (f == 'y') { p.sticky = true; }
		else if (f == 's') { p.dotall = true; }
		else if (f == 'd') { p.indices = true; }
		else if (f == 'u') { p.unicode = true; }
		else if (f == 'v') { p.unicode_sets = true; }
		else {
			p.ok = false;
			p.error = "Invalid regular expression flags: " + std::string{flags};
		}
	}
	// `u` and `v` are two spellings of one mode and exclude each other
	// (22.2.3.4 step 6).
	if (p.unicode && p.unicode_sets) {
		p.ok = false;
		p.error = "Invalid regular expression flags: " + std::string{flags};
	}
	std::size_t i = 0;
	p.root = rx_parse_alt(source, i, p, true);
	if (i != source.size()) { rx_fail(p, source); }
	if (p.ok) { rx_resolve_backrefs(*p.root, p, source); }
	return p;
}

struct rx_state {
	const std::string * s = nullptr;
	const rx_prog * p = nullptr;
	std::vector<std::pair<std::ptrdiff_t, std::ptrdiff_t>> caps; // -1,-1 = unmatched
};

inline char rx_fold(char c, bool icase) {
	return icase && c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}
inline constexpr bool rx_is_word(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
	       c == '_';
}
inline bool rx_class_hit(const rx_class & cc, std::uint32_t ch, bool icase, bool unicode) {
	// The ranges are sorted and disjoint (rx_class_normalize): the first one
	// ending at or after the probe is the only one that can hold it.
	const auto in = [&](std::uint32_t probe) {
		const auto it = std::lower_bound(
		    cc.ranges.begin(), cc.ranges.end(), probe,
		    [](const std::pair<std::uint32_t, std::uint32_t> & r, std::uint32_t v) { return r.second < v; });
		return it != cc.ranges.end() && it->first <= probe;
	};
	bool hit = in(ch);
	if (!hit && icase && unicode) {
		// 22.2.2.9 CharacterSetMatcher: a member a matches when
		// Canonicalize(a) is Canonicalize(ch), and under `u` that is the
		// simple case fold - so the fold itself and every code point that
		// shares it are looked up.
		const char32_t folded = rx_fold_simple(ch);
		hit = folded != ch && in(folded);
		char32_t others[8];
		for (std::size_t n = rx_unfold(folded, others); !hit && n-- > 0;) {
			hit = others[n] != ch && in(others[n]);
		}
	} else if (!hit && icase) {
		// ASCII folding only, as everywhere in this engine (core/algorithms).
		const std::uint32_t other = (ch >= 'a' && ch <= 'z')   ? ch - ('a' - 'A')
		                            : (ch >= 'A' && ch <= 'Z') ? ch + ('a' - 'A')
		                                                       : ch;
		hit = other != ch && in(other);
	}
	return cc.neg ? !hit : hit;
}

using rx_cont = std::function<bool(std::size_t)>;

inline bool rx_match_alt(const rx_alt & alt, rx_state & st, std::size_t pos, const rx_cont & k);

inline bool rx_match_once(const rx_piece & pc, rx_state & st, std::size_t pos, const rx_cont & k) {
	const std::string & s = *st.s;
	switch (pc.kind) {
	case rx_piece::lit:
		if (st.p->icase && (st.p->unicode || st.p->unicode_sets) && pos < s.size()) {
			// Canonicalize under `iu` is the simple case fold of BOTH code
			// points (`/k/iu` takes the KELVIN SIGN), so a literal of any
			// width is compared as one decoded code point.
			std::size_t pattern_width = 1;
			const std::uint32_t want = pc.text.empty()
			                               ? static_cast<unsigned char>(pc.c)
			                               : rx_utf8_decode(pc.text, 0, pattern_width);
			if (pc.text.empty() || pattern_width == pc.text.size()) {
				std::size_t width = 1;
				const std::uint32_t got = rx_utf8_decode(s, pos, width);
				return rx_fold_simple(got) == rx_fold_simple(want) && k(pos + width);
			}
		}
		if (!pc.text.empty()) {
			if (s.compare(pos, pc.text.size(), pc.text) != 0) { return false; }
			return k(pos + pc.text.size());
		}
		return pos < s.size() && rx_fold(s[pos], st.p->icase) == rx_fold(pc.c, st.p->icase) &&
		       k(pos + 1);
	case rx_piece::any: {
		// ONE CODE POINT, however many bytes: `"é".match(/./)[0]` is "é". A
		// line terminator is not "any" unless `s` says so - \n, \r, and the
		// three-byte U+2028 and U+2029.
		if (pos >= s.size()) { return false; }
		std::size_t width = 1;
		const std::uint32_t cp = rx_utf8_decode(s, pos, width);
		if (!st.p->dotall && (cp == '\n' || cp == '\r' || cp == 0x2028u || cp == 0x2029u)) {
			return false;
		}
		return k(pos + width);
	}
	case rx_piece::backref: {
		// A group that has not captured matches EMPTY (22.2.2.7.2 step 3),
		// and one that has must be repeated here, folded like a literal. A
		// duplicated name resolves to its first slot; the alternatives' other
		// slots share the name, so the one that participated is looked up.
		std::pair<std::ptrdiff_t, std::ptrdiff_t> cap{-1, -1};
		if (pc.cap >= 0 && static_cast<std::size_t>(pc.cap) < st.caps.size()) {
			cap = st.caps[static_cast<std::size_t>(pc.cap)];
		}
		if (cap.first < 0 && !pc.ref_name.empty()) {
			for (const auto & [name, slot] : st.p->names) {
				if (name == pc.ref_name && st.caps[static_cast<std::size_t>(slot)].first >= 0) {
					cap = st.caps[static_cast<std::size_t>(slot)];
					break;
				}
			}
		}
		if (cap.first < 0) { return k(pos); }
		const auto len = static_cast<std::size_t>(cap.second - cap.first);
		if (pos + len > s.size()) { return false; }
		for (std::size_t j = 0; j < len; ++j) {
			if (rx_fold(s[pos + j], st.p->icase) !=
			    rx_fold(s[static_cast<std::size_t>(cap.first) + j], st.p->icase)) {
				return false;
			}
		}
		return k(pos + len);
	}
	case rx_piece::behind:
	case rx_piece::nbehind: {
		// ZERO WIDTH, LOOKING BACK: does the sub-pattern match some text that
		// ENDS exactly here? Tried from the nearest start outward, so the
		// shortest match wins the captures - the specification matches the
		// sub-pattern right to left, which prefers the same thing.
		const auto saved = st.caps;
		bool inner = false;
		for (std::size_t start = pos + 1; start-- > 0 && !inner;) {
			inner = rx_match_alt(*pc.sub, st, start, [&](std::size_t end) { return end == pos; });
		}
		if (pc.kind == rx_piece::nbehind) {
			st.caps = saved;
			return !inner && k(pos);
		}
		if (!inner) {
			st.caps = saved;
			return false;
		}
		return k(pos);
	}
	case rx_piece::cls: {
		// A class consumes ONE CODE POINT of the UTF-8 subject, however many
		// bytes it takes: `[\u1680]` matches the three bytes of U+1680.
		// The strings of a `v` class go first, longest first (22.2.2.9: a
		// ClassSetExpression's strings are matched by descending length).
		for (const std::string & str : pc.cc.strings) {
			if (s.compare(pos, str.size(), str) == 0 && k(pos + str.size())) { return true; }
		}
		if (pos >= s.size()) { return false; }
		std::size_t width = 1;
		const std::uint32_t cp = rx_utf8_decode(s, pos, width);
		return rx_class_hit(pc.cc, cp, st.p->icase, st.p->unicode || st.p->unicode_sets) &&
		       k(pos + width);
	}
	case rx_piece::bol:
		return (pos == 0 || (st.p->multi && s[pos - 1] == '\n')) && k(pos);
	case rx_piece::eol:
		return (pos == s.size() || (st.p->multi && s[pos] == '\n')) && k(pos);
	case rx_piece::wordb:
	case rx_piece::nwordb: {
		const bool before = pos > 0 && rx_is_word(s[pos - 1]);
		const bool after = pos < s.size() && rx_is_word(s[pos]);
		const bool boundary = before != after;
		return boundary == (pc.kind == rx_piece::wordb) && k(pos);
	}
	case rx_piece::grp: {
		const std::int32_t cap = pc.cap;
		const auto saved = cap >= 0 ? st.caps[static_cast<std::size_t>(cap)]
		                            : std::pair<std::ptrdiff_t, std::ptrdiff_t>{-1, -1};
		const bool ok = rx_match_alt(*pc.sub, st, pos, [&](std::size_t end) {
			if (cap >= 0) {
				st.caps[static_cast<std::size_t>(cap)] = {static_cast<std::ptrdiff_t>(pos),
				                                     static_cast<std::ptrdiff_t>(end)};
			}
			return k(end);
		});
		if (!ok && cap >= 0) { st.caps[static_cast<std::size_t>(cap)] = saved; }
		return ok;
	}
	case rx_piece::ahead:
	case rx_piece::nahead: {
		// ZERO WIDTH. The sub-pattern is matched at this position purely to ask
		// whether it can be - the continuation resumes from `pos` either way,
		// which is the whole point of an assertion. Natural in a backtracker:
		// the inner match simply succeeds with a continuation that accepts
		// anything and reports nothing.
		//
		// Captures made inside a lookahead are rolled back on a negative one,
		// because it did not match anything that survives.
		const auto saved = st.caps;
		const bool inner = rx_match_alt(*pc.sub, st, pos, [](std::size_t) { return true; });
		if (pc.kind == rx_piece::nahead) {
			st.caps = saved;
			return !inner && k(pos);
		}
		if (!inner) {
			st.caps = saved;
			return false;
		}
		return k(pos);
	}
	}
	return false;
}

// A piece that consumes one character and captures nothing, whose
// repetition is therefore a LOOP: `\p{Any}+` over the whole code space is
// a million repetitions, and a recursion per repetition is a stack overflow.
inline bool rx_simple_piece(const rx_piece & pc) {
	return pc.kind == rx_piece::lit || pc.kind == rx_piece::any ||
	       (pc.kind == rx_piece::cls && pc.cc.strings.empty());
}

inline bool rx_match_piece(const rx_piece & pc, rx_state & st, std::size_t pos, const rx_cont & k) {
	if ((pc.min != 1 || pc.max != 1) && rx_simple_piece(pc)) {
		std::size_t end = 0;
		const rx_cont take = [&](std::size_t np) {
			end = np;
			return true;
		};
		const auto step = [&](std::size_t at) { return rx_match_once(pc, st, at, take) ? end : at; };
		if (!pc.greedy) {
			std::size_t at = pos;
			for (std::int32_t n = 0;; ++n) {
				if (n >= pc.min && k(at)) { return true; }
				if (pc.max >= 0 && n >= pc.max) { return false; }
				const std::size_t np = step(at);
				if (np == at) { return false; }
				at = np;
			}
		}
		// ends[n] is where n repetitions leave the position; the longest
		// run is offered first, then each shorter one down to the minimum.
		std::vector<std::size_t> ends{pos};
		while (pc.max < 0 || static_cast<std::int32_t>(ends.size()) - 1 < pc.max) {
			const std::size_t np = step(ends.back());
			if (np == ends.back()) { break; }
			ends.push_back(np);
		}
		for (std::size_t n = ends.size(); n-- > 0;) {
			if (static_cast<std::int32_t>(n) >= pc.min && k(ends[n])) { return true; }
		}
		return false;
	}
	// quantified matching; a zero-width repetition stops the loop
	std::function<bool(std::size_t, std::int32_t)> rec = [&](std::size_t at, std::int32_t n) -> bool {
		const bool may_more = pc.max < 0 || n < pc.max;
		const bool may_stop = n >= pc.min;
		const auto more = [&]() {
			if (!may_more) { return false; }
			// 22.2.2.3.1 RepeatMatcher step 4: every capture INSIDE the atom
			// is cleared before each repetition, so `/(a)|(b)/` run twice by
			// a quantifier does not keep group 1 from the first round. Saved
			// and put back when the repetition fails, since the caller may
			// still take the shorter match.
			std::vector<std::pair<std::ptrdiff_t, std::ptrdiff_t>> saved;
			if (pc.caps_hi > pc.caps_lo) {
				saved.assign(st.caps.begin() + pc.caps_lo, st.caps.begin() + pc.caps_hi);
				for (std::int32_t c = pc.caps_lo; c < pc.caps_hi; ++c) {
					st.caps[static_cast<std::size_t>(c)] = {-1, -1};
				}
			}
			const bool ok = rx_match_once(pc, st, at, [&](std::size_t np) {
				return np == at ? (n + 1 >= pc.min && k(np)) : rec(np, n + 1);
			});
			if (!ok && !saved.empty()) {
				std::copy(saved.begin(), saved.end(), st.caps.begin() + pc.caps_lo);
			}
			return ok;
		};
		if (pc.greedy) { return more() || (may_stop && k(at)); }
		return (may_stop && k(at)) || more();
	};
	return rec(pos, 0);
}

inline bool rx_match_seq(const rx_seq & sq, std::size_t idx, rx_state & st, std::size_t pos,
                         const rx_cont & k) {
	if (idx == sq.size()) { return k(pos); }
	return rx_match_piece(sq[idx], st, pos, [&](std::size_t np) {
		return rx_match_seq(sq, idx + 1, st, np, k);
	});
}

inline bool rx_match_alt(const rx_alt & alt, rx_state & st, std::size_t pos, const rx_cont & k) {
	for (const rx_seq & sq : alt.alts) {
		if (rx_match_seq(sq, 0, st, pos, k)) { return true; }
	}
	return false;
}

struct rx_match {
	std::size_t begin = 0, end = 0;
	std::vector<std::pair<std::ptrdiff_t, std::ptrdiff_t>> caps;
};

inline bool rx_search(const rx_prog & p, const std::string & s, std::size_t from, rx_match & out) {
	for (std::size_t start = from; start <= s.size(); ++start) {
		// A MATCH STARTS ON A CODE POINT, never inside one: a continuation
		// byte is not a character, and `[^x]` at such a position would
		// "match" the tail of the very character the class excludes.
		if (start < s.size() && (static_cast<unsigned char>(s[start]) & 0xC0u) == 0x80u) {
			continue;
		}
		rx_state st;
		st.s = &s;
		st.p = &p;
		st.caps.assign(static_cast<std::size_t>(p.ngroups), {-1, -1});
		std::size_t got_end = 0;
		if (rx_match_alt(*p.root, st, start, [&](std::size_t end) {
			    got_end = end;
			    return true;
		    })) {
			out.begin = start;
			out.end = got_end;
			out.caps = std::move(st.caps);
			return true;
		}
	}
	return false;
}


} // namespace ctbrowser::script::rx
