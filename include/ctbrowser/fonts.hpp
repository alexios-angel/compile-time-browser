#ifndef CTBROWSER__FONTS__HPP
#define CTBROWSER__FONTS__HPP

#include "embed.hpp"
#include "image.hpp" // embedded_asset
#ifndef CTBROWSER_IN_A_MODULE
#include <span>
#include <string>
#include <vector>
#endif

// The DEFAULT TYPEFACES, std::embed-ded from the vendored fonts/
// directory (see fonts/README.md - google/fonts, all SIL OFL 1.1):
//
//   serif      -> Tinos      (metric-compatible with Times New Roman,
//                             what Firefox's default `serif` maps to)
//   sans-serif -> Fira Sans  (Mozilla's own typeface)
//   monospace  -> Cousine    (metric-compatible with Courier New)
//
// each in Regular/Bold/Italic/BoldItalic. run_app() appends these to
// opts.assets under the well-known `ctbrowser:font/<generic>-<style>`
// registry keys, and the shell's text renderer resolves per-element
// font-family/-weight/-style against them - multiple faces coexist in
// one document. try_embed is OPPORTUNISTIC: a checkout without fonts/
// (or a compiler without std::embed) builds fine and falls back to a
// probed system font, or the built-in 8x8 bitmap face.

#if defined(__has_builtin)
#	if __has_builtin(__builtin_std_embed)
#		pragma clang diagnostic push
#		pragma clang diagnostic ignored "-Wc++2d-extensions"
#depend "fonts/**"
#		pragma clang diagnostic pop
#	endif
#endif

namespace ctbrowser::detail {

inline std::vector<embedded_asset> default_font_assets() {
	std::vector<embedded_asset> out;
#ifdef CTBROWSER_HAS_STD_EMBED
	const auto add = [&out](const char * key, std::span<const unsigned char> bytes) {
		if (!bytes.empty()) { out.push_back({key, bytes.data(), bytes.size()}); }
	};
	static constexpr auto serif_r = ctbrowser::try_embed<unsigned char>("fonts/Tinos-Regular.ttf");
	static constexpr auto serif_b = ctbrowser::try_embed<unsigned char>("fonts/Tinos-Bold.ttf");
	static constexpr auto serif_i = ctbrowser::try_embed<unsigned char>("fonts/Tinos-Italic.ttf");
	static constexpr auto serif_z = ctbrowser::try_embed<unsigned char>("fonts/Tinos-BoldItalic.ttf");
	static constexpr auto sans_r = ctbrowser::try_embed<unsigned char>("fonts/FiraSans-Regular.ttf");
	static constexpr auto sans_b = ctbrowser::try_embed<unsigned char>("fonts/FiraSans-Bold.ttf");
	static constexpr auto sans_i = ctbrowser::try_embed<unsigned char>("fonts/FiraSans-Italic.ttf");
	static constexpr auto sans_z = ctbrowser::try_embed<unsigned char>("fonts/FiraSans-BoldItalic.ttf");
	static constexpr auto mono_r = ctbrowser::try_embed<unsigned char>("fonts/Cousine-Regular.ttf");
	static constexpr auto mono_b = ctbrowser::try_embed<unsigned char>("fonts/Cousine-Bold.ttf");
	static constexpr auto mono_i = ctbrowser::try_embed<unsigned char>("fonts/Cousine-Italic.ttf");
	static constexpr auto mono_z = ctbrowser::try_embed<unsigned char>("fonts/Cousine-BoldItalic.ttf");
	add("ctbrowser:font/serif-regular", serif_r);
	add("ctbrowser:font/serif-bold", serif_b);
	add("ctbrowser:font/serif-italic", serif_i);
	add("ctbrowser:font/serif-bolditalic", serif_z);
	add("ctbrowser:font/sans-regular", sans_r);
	add("ctbrowser:font/sans-bold", sans_b);
	add("ctbrowser:font/sans-italic", sans_i);
	add("ctbrowser:font/sans-bolditalic", sans_z);
	add("ctbrowser:font/mono-regular", mono_r);
	add("ctbrowser:font/mono-bold", mono_b);
	add("ctbrowser:font/mono-italic", mono_i);
	add("ctbrowser:font/mono-bolditalic", mono_z);
#endif
	return out;
}

} // namespace ctbrowser::detail

#endif
