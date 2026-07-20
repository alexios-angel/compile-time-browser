#ifndef CTBROWSER__ASSETS__HPP
#define CTBROWSER__ASSETS__HPP

#include "embed.hpp"
#include "fetch.hpp"
#include "image.hpp"
#ifndef CTBROWSER_IN_A_MODULE
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <utility>
#include <vector>
#endif

// AUTOMATIC compile-time assets. The page's script is a compile-time
// string, so the engine can SEE every loadImage("...")/playSound("...")/
// fetch("...") literal at compile time - and with the std::embed clang
// it bakes those resources straight into the binary and registers them
// itself. File paths go through ctbrowser::try_embed (embed.hpp); an
// http(s):// URL goes through ctbrowser::try_fetch (fetch.hpp) instead
// - scripts, stylesheets, fonts, JSON data, sprites: whatever the URL
// serves is fetched AT COMPILE TIME and embedded, which is also what
// backs the script-visible `await fetch(url)` (script.hpp). No
// per-example boilerplate: a TU opts in with one guarded #depend
// directive (see embed.hpp), and URL fetches additionally need the
// build to pass --fetch-allow=<url-glob> (nothing is allowed by
// default, so offline/default builds never touch the network).
// Everything is OPPORTUNISTIC: no builtin, no #depend, no file, or no
// --fetch-allow -> that asset silently stays a runtime load (files) or
// a rejected fetch (URLs).

namespace ctbrowser {

namespace detail {

// find the string literal arguments of loadImage("...") / playSound("...")
// / fetch("...")
template <typename Src> struct script_asset_scan {
	static constexpr std::string_view text = Src::get();
	static constexpr std::array<std::string_view, 5> needles{"loadImage(\"",
	                                                         "playSound(\"",
	                                                         "fetch(\"",
	                                                         "AppendSceneAsync(\"",
	                                                         "url(\""}; // CSS @font-face src, e.g. url("...")

	// {offset, length} of the path at position i, or length 0
	static consteval std::pair<size_t, size_t> ref_at(size_t i) {
		for (const std::string_view needle : needles) {
			if (text.size() - i < needle.size()) { continue; }
			if (text.substr(i, needle.size()) != needle) { continue; }
			const size_t start = i + needle.size();
			const size_t close = text.find('"', start);
			if (close != std::string_view::npos && close > start) {
				return {start, close - start};
			}
		}
		return {0, 0};
	}

	static consteval size_t count() {
		size_t n = 0;
		for (size_t i = 0; i < text.size(); ++i) {
			if (ref_at(i).second > 0) { ++n; }
		}
		return n;
	}
	static constexpr size_t N = count();

	static consteval std::array<std::pair<size_t, size_t>, N> compute() {
		std::array<std::pair<size_t, size_t>, N> out{};
		size_t n = 0;
		for (size_t i = 0; i < text.size(); ++i) {
			if (const auto r = ref_at(i); r.second > 0) { out[n++] = r; }
		}
		return out;
	}
	static constexpr std::array<std::pair<size_t, size_t>, N> refs = compute();

	static constexpr std::string_view path(size_t i) {
		return text.substr(refs[i].first, refs[i].second);
	}
};

#ifdef CTBROWSER_HAS_STD_EMBED

constexpr bool is_fetch_url(std::string_view path) {
	return path.starts_with("http://") || path.starts_with("https://");
}

template <typename Src, size_t I> struct auto_embedded {
	static constexpr std::string_view path = script_asset_scan<Src>::path(I);
	// URLs fetch over the network AT COMPILE TIME (allow-list gated),
	// everything else embeds from the filesystem; either way the bytes
	// land in the same registry under the exact literal the script uses
	static consteval std::span<const unsigned char> load() {
		if constexpr (is_fetch_url(path)) {
#ifdef CTBROWSER_HAS_STD_FETCH
			return try_fetch<unsigned char>(path);
#else
			return {};
#endif
		} else {
			return try_embed<unsigned char>(path);
		}
	}
	static constexpr std::span<const unsigned char> blob = load();
};

template <typename Src, size_t... I>
inline std::vector<embedded_asset> collect_auto_assets(std::index_sequence<I...>) {
	std::vector<embedded_asset> out;
	(
	    [&] {
		    constexpr std::span<const unsigned char> b = auto_embedded<Src, I>::blob;
		    if constexpr (!b.empty()) {
			    const std::string path{auto_embedded<Src, I>::path};
			    if (find_asset(&out, path) == nullptr) { // literals repeat
				    out.push_back({path, b.data(), b.size()});
			    }
		    }
	    }(),
	    ...);
	return out;
}

#endif // CTBROWSER_HAS_STD_EMBED

// user-supplied assets win over automatic ones of the same path
inline std::vector<embedded_asset> merge_assets(std::vector<embedded_asset> user,
                                                std::vector<embedded_asset> automatic) {
	for (embedded_asset & a : automatic) {
		if (find_asset(&user, a.path) == nullptr) { user.push_back(std::move(a)); }
	}
	return user;
}

} // namespace detail

// every asset the page's script names, baked in at compile time - or
// an empty vector on compilers without std::embed
template <typename Page> inline std::vector<embedded_asset> auto_assets() {
#ifdef CTBROWSER_HAS_STD_EMBED
	// scan BOTH the script (loadImage/playSound/fetch) and the stylesheet
	// (@font-face src url("...")) - fonts referenced in CSS embed the same way
	using sscan = detail::script_asset_scan<typename Page::script_source>;
	using cscan = detail::script_asset_scan<typename Page::style_source>;
	std::vector<embedded_asset> from_script = detail::collect_auto_assets<typename Page::script_source>(
	    std::make_index_sequence<sscan::N>{});
	std::vector<embedded_asset> from_style = detail::collect_auto_assets<typename Page::style_source>(
	    std::make_index_sequence<cscan::N>{});
	return detail::merge_assets(std::move(from_script), std::move(from_style));
#else
	return {};
#endif
}

} // namespace ctbrowser

#endif
