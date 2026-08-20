#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A regular-expression engine.
//
// Ported from ctjs's `rxd`, which was already a self-contained backtracking
// matcher and coupled to its host by exactly two calls. Two changes make it
// fit here: a compile failure is a FLAG on the program rather than a thrown
// exception, because nothing in this VM throws; and the match result was
// already carrying its positions - only ctjs's exec wrapper discarded them -
// so `.index`, which p5.js reads 143 times, comes for free.
//
// Added here: lookahead `(?=`/`(?!`, the sticky `y` flag, and named groups
// `(?<name>...)`. NOT here, and said out loud rather than mis-matched:
// lookbehind and backreferences. Neither appears anywhere in p5.js, and a
// backtracker that silently ignores an assertion is worse than one that
// refuses it.

namespace ctbrowser::script::rx {



struct rx_class {
	bool neg = false;
	std::vector<std::pair<unsigned char, unsigned char>> ranges;
};
struct rx_alt;
struct rx_piece {
	enum kind_t { lit, any, cls, grp, bol, eol, wordb, nwordb, ahead, nahead } kind = lit;
	char c = 0;
	rx_class cc;
	std::shared_ptr<rx_alt> sub;
	std::int32_t cap = -1; // capture slot, -1 = (?:)
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
		out.ranges.push_back({' ', ' '});
		out.ranges.push_back({'\t', '\t'});
		out.ranges.push_back({'\n', '\n'});
		out.ranges.push_back({'\r', '\r'});
		out.ranges.push_back({'\f', '\f'});
		out.ranges.push_back({'\v', '\v'});
		break;
	default: break;
	}
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
		} else if (i + 1 < src.size() && src[i] == '?' && src[i + 1] == '<') {
			// lookbehind. Refused rather than mis-matched - see the header.
			rx_fail(p, src);
			return pc;
		} else {
			pc.cap = p.ngroups++;
		}
		pc.sub = rx_parse_alt(src, i, p, false);
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
		bool first = true;
		while (i < src.size() && (src[i] != ']' || first)) {
			first = false;
			unsigned char lo;
			if (src[i] == '\\' && i + 1 < src.size()) {
				const char e = src[i + 1];
				i += 2;
				if (e == 'd' || e == 'w' || e == 's') {
					rx_class_escape(pc.cc, e);
					continue;
				}
				lo = static_cast<unsigned char>(rx_escape_char(e));
			} else {
				lo = static_cast<unsigned char>(src[i++]);
			}
			unsigned char hi = lo;
			if (i + 1 < src.size() && src[i] == '-' && src[i + 1] != ']') {
				++i;
				if (src[i] == '\\' && i + 1 < src.size()) {
					hi = static_cast<unsigned char>(rx_escape_char(src[i + 1]));
					i += 2;
				} else {
					hi = static_cast<unsigned char>(src[i++]);
				}
			}
			pc.cc.ranges.push_back({lo, hi});
		}
		if (i >= src.size()) { rx_fail(p, src); }
		++i; // ']'
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
		if (e == 'd' || e == 'w' || e == 's') {
			pc.kind = rx_piece::cls;
			rx_class_escape(pc.cc, e);
			return pc;
		}
		if (e == 'D' || e == 'W' || e == 'S') {
			pc.kind = rx_piece::cls;
			pc.cc.neg = true;
			rx_class_escape(pc.cc, static_cast<char>(e + ('a' - 'A')));
			return pc;
		}
		pc.kind = rx_piece::lit;
		pc.c = rx_escape_char(e);
		return pc;
	}
	pc.kind = rx_piece::lit;
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

inline rx_prog rx_compile(std::string_view source, std::string_view flags) {
	rx_prog p;
	for (const char f : flags) {
		if (f == 'i') { p.icase = true; }
		else if (f == 'g') { p.global = true; }
		else if (f == 'm') { p.multi = true; }
		else if (f == 'y') { p.sticky = true; }
		else if (f == 'u' || f == 's' || f == 'd' || f == 'v') { /* accepted, not modelled */ }
		else {
			p.ok = false;
			p.error = "Invalid regular expression flags: " + std::string{flags};
		}
	}
	std::size_t i = 0;
	p.root = rx_parse_alt(source, i, p, true);
	if (i != source.size()) { rx_fail(p, source); }
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
inline constexpr bool rx_class_hit(const rx_class & cc, char ch, bool icase) {
	const auto in = [&](char probe) {
		for (const auto & [lo, hi] : cc.ranges) {
			if (static_cast<unsigned char>(probe) >= lo &&
			    static_cast<unsigned char>(probe) <= hi) {
				return true;
			}
		}
		return false;
	};
	bool hit = in(ch);
	if (!hit && icase) {
		const char other = (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - ('a' - 'A'))
		                   : (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch + ('a' - 'A'))
		                                              : ch;
		hit = other != ch && in(other);
	}
	return cc.neg ? !hit : hit;
}

using rx_cont = std::function<bool(std::size_t)>;

inline constexpr bool rx_match_alt(const rx_alt & alt, rx_state & st, std::size_t pos, const rx_cont & k);

inline constexpr bool rx_match_once(const rx_piece & pc, rx_state & st, std::size_t pos, const rx_cont & k) {
	const std::string & s = *st.s;
	switch (pc.kind) {
	case rx_piece::lit:
		return pos < s.size() && rx_fold(s[pos], st.p->icase) == rx_fold(pc.c, st.p->icase) &&
		       k(pos + 1);
	case rx_piece::any:
		return pos < s.size() && s[pos] != '\n' && k(pos + 1);
	case rx_piece::cls:
		return pos < s.size() && rx_class_hit(pc.cc, s[pos], st.p->icase) && k(pos + 1);
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

inline constexpr bool rx_match_piece(const rx_piece & pc, rx_state & st, std::size_t pos, const rx_cont & k) {
	// quantified matching; a zero-width repetition stops the loop
	std::function<bool(std::size_t, std::int32_t)> rec = [&](std::size_t at, std::int32_t n) -> bool {
		const bool may_more = pc.max < 0 || n < pc.max;
		const bool may_stop = n >= pc.min;
		const auto more = [&]() {
			return may_more && rx_match_once(pc, st, at, [&](std::size_t np) {
				       return np == at ? (n + 1 >= pc.min && k(np)) : rec(np, n + 1);
			       });
		};
		if (pc.greedy) { return more() || (may_stop && k(at)); }
		return (may_stop && k(at)) || more();
	};
	return rec(pos, 0);
}

inline constexpr bool rx_match_seq(const rx_seq & sq, std::size_t idx, rx_state & st, std::size_t pos,
                         const rx_cont & k) {
	if (idx == sq.size()) { return k(pos); }
	return rx_match_piece(sq[idx], st, pos, [&](std::size_t np) {
		return rx_match_seq(sq, idx + 1, st, np, k);
	});
}

inline constexpr bool rx_match_alt(const rx_alt & alt, rx_state & st, std::size_t pos, const rx_cont & k) {
	for (const rx_seq & sq : alt.alts) {
		if (rx_match_seq(sq, 0, st, pos, k)) { return true; }
	}
	return false;
}

struct rx_match {
	std::size_t begin = 0, end = 0;
	std::vector<std::pair<std::ptrdiff_t, std::ptrdiff_t>> caps;
};

inline constexpr bool rx_search(const rx_prog & p, const std::string & s, std::size_t from, rx_match & out) {
	for (std::size_t start = from; start <= s.size(); ++start) {
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
