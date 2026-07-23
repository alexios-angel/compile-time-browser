#ifndef CTBROWSER__ANIM__HPP
#define CTBROWSER__ANIM__HPP

#include <cstdint>

#include <cstddef>

#include "dom.hpp"
#include <ctcss.hpp>
#ifndef CTBROWSER_IN_A_MODULE
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
#endif

// CSS @keyframes at RUNTIME. ctcss::parse_value captures @keyframes into the
// value_sheet; this drives them: each frame, for every element whose computed
// `animation-name` (or `animation` shorthand) names a captured @keyframes, the
// current progress is interpolated from the keyframe stops and written into the
// element's inline_style - which computed_style() lets win, so the next layout/
// paint reflects it. Properties interpolate as colors (#hex/rgb) or by a shared
// numeric skeleton (0px->100px, rotate(0deg)->rotate(360deg)); anything else
// steps. animation-duration/-delay are honored; the animation loops.

namespace ctbrowser {
namespace detail {

// leading (optionally signed/fractional) number; trailing units ignored
inline double anim_num(std::string_view s) noexcept {
	std::size_t i = 0;
	bool neg = false;
	if (i < s.size() && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; ++i; }
	double v = 0;
	while (i < s.size() && s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); ++i; }
	if (i < s.size() && s[i] == '.') {
		++i;
		double f = 0.1;
		while (i < s.size() && s[i] >= '0' && s[i] <= '9') { v += (s[i] - '0') * f; f *= 0.1; ++i; }
	}
	return neg ? -v : v;
}

inline std::string_view anim_trim(std::string_view s) noexcept {
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) { s.remove_prefix(1); }
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) { s.remove_suffix(1); }
	return s;
}

// a CSS <time> ("2s", "500ms", ".5s") in milliseconds; 0 if none
inline double css_time_ms(std::string_view s) noexcept {
	s = anim_trim(s);
	if (s.empty()) { return 0; }
	const double n = anim_num(s);
	const bool ms = s.size() >= 2 && s[s.size() - 2] == 'm' && s.back() == 's';
	return ms ? n : n * 1000.0;
}

inline std::string anim_fmt(double v) {
	char buf[32];
	std::snprintf(buf, sizeof buf, "%g", v);
	return std::string{buf};
}

// interpolate two CSS values at t in [0,1]: colors channel-wise, else values
// sharing a numeric skeleton number-wise, else a step at t==1.
inline std::string interp_value(std::string_view a, std::string_view b, double t) {
	const ctcss::color ca = ctcss::parse_color(a);
	const ctcss::color cb = ctcss::parse_color(b);
	if (ca.ok && cb.ok) {
		const auto L = [t](std::int32_t x, std::int32_t y) {
			std::int32_t v = static_cast<std::int32_t>(std::lround(x + (y - x) * t));
			return v < 0 ? 0 : v > 255 ? 255 : v;
		};
		char buf[8];
		std::snprintf(buf, sizeof buf, "#%02x%02x%02x", L(ca.r, cb.r), L(ca.g, cb.g), L(ca.b, cb.b));
		return std::string{buf};
	}
	// number-shape interpolation: identical non-numeric runs, matching numbers
	const auto is_num = [](char c) {
		return (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+';
	};
	std::string out;
	std::size_t ia = 0, ib = 0;
	while (ia < a.size() && ib < b.size()) {
		if (is_num(a[ia]) && is_num(b[ib])) {
			std::size_t ja = ia;
			while (ja < a.size() && is_num(a[ja])) { ++ja; }
			std::size_t jb = ib;
			while (jb < b.size() && is_num(b[jb])) { ++jb; }
			const double na = anim_num(a.substr(ia, ja - ia));
			const double nb = anim_num(b.substr(ib, jb - ib));
			out += anim_fmt(na + (nb - na) * t);
			ia = ja;
			ib = jb;
		} else if (a[ia] == b[ib]) {
			out += a[ia];
			++ia;
			++ib;
		} else {
			return std::string{t < 1.0 ? a : b}; // skeleton mismatch -> step
		}
	}
	if (ia != a.size() || ib != b.size()) { return std::string{t < 1.0 ? a : b}; }
	return out;
}

inline void anim_apply_progress(node * e, const ctcss::value_sheet::keyframes_rule & kf, double p) {
	std::vector<const ctcss::value_sheet::keyframe *> fs;
	for (const auto & f : kf.frames) { fs.push_back(&f); }
	std::sort(fs.begin(), fs.end(), [](auto x, auto y) { return x->at < y->at; });
	if (fs.empty()) { return; }
	const ctcss::value_sheet::keyframe * f0 = fs.front();
	const ctcss::value_sheet::keyframe * f1 = fs.back();
	for (auto * f : fs) {
		if (f->at <= p) { f0 = f; }
	}
	for (auto it = fs.rbegin(); it != fs.rend(); ++it) {
		if ((*it)->at >= p) { f1 = *it; }
	}
	const double span = f1->at - f0->at;
	double t = span > 1e-9 ? (p - f0->at) / span : 0.0;
	t = t < 0 ? 0 : t > 1 ? 1 : t;

	const auto decl_get = [](const ctcss::value_sheet::keyframe * f, const std::string & prop) -> std::string_view {
		for (const auto & d : f->decls) {
			if (d.property == prop) { return d.value; }
		}
		return {};
	};
	std::vector<std::string> props;
	for (const auto & d : f0->decls) { props.push_back(d.property); }
	for (const auto & d : f1->decls) {
		if (std::find(props.begin(), props.end(), d.property) == props.end()) { props.push_back(d.property); }
	}
	for (const std::string & prop : props) {
		std::string_view va = decl_get(f0, prop);
		std::string_view vb = decl_get(f1, prop);
		if (va.empty()) { va = vb; }
		if (vb.empty()) { vb = va; }
		if (va.empty()) { continue; }
		e->inline_style.set(prop, interp_value(va, vb, t));
	}
}

inline void anim_node(node * e, const ctcss::value_sheet & sheet, double now_ms) {
	const std::vector<ctcss::element_ref> chain = e->chain();
	const auto q = [&](std::string_view prop) -> std::string_view {
		if (e->inline_style.has(prop)) { return e->inline_style.get(prop); }
		return ctcss::query(sheet, chain.data(), chain.size(), prop);
	};

	std::string name{anim_trim(q("animation-name"))};
	double dur = css_time_ms(q("animation-duration"));
	double delay = css_time_ms(q("animation-delay"));

	// `animation` shorthand: find the @keyframes name + the first <time>
	if (name.empty() || dur == 0) {
		const std::string_view sh = q("animation");
		std::size_t start = 0;
		bool got_time = false;
		for (std::size_t k = 0; k <= sh.size(); ++k) {
			if (k == sh.size() || sh[k] == ' ' || sh[k] == '\t') {
				const std::string_view tok = anim_trim(sh.substr(start, k - start));
				start = k + 1;
				if (tok.empty()) { continue; }
				const double tms = css_time_ms(tok);
				if (tms > 0 && !got_time) { dur = tms; got_time = true; }
				else if (name.empty() && sheet.animation(tok) != nullptr) { name = std::string{tok}; }
			}
		}
	}

	if (!name.empty() && dur > 0) {
		if (const ctcss::value_sheet::keyframes_rule * kf = sheet.animation(name)) {
			double p = (now_ms - delay) / dur;
			if (p < 0) { p = 0; }
			p = p - std::floor(p); // loop
			anim_apply_progress(e, *kf, p);
		}
	}
	for (auto & c : e->children) { anim_node(c.get(), sheet, now_ms); }
}

// advance every @keyframes-driven element to time `now_ms`. Cheap no-op when
// the sheet has no @keyframes.
inline void apply_animations(document & doc, const ctcss::value_sheet & sheet, double now_ms) {
	if (sheet.keyframes.empty() || !doc.root) { return; }
	anim_node(doc.root.get(), sheet, now_ms);
}

} // namespace detail
} // namespace ctbrowser

#endif
