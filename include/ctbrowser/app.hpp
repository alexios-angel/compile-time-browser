#ifndef CTBROWSER__APP__HPP
#define CTBROWSER__APP__HPP

#include <cstdint>

#include <cstddef>

#include "audio.hpp"
#include "engine.hpp"
#include "font8x8.hpp"
#include "fonts.hpp"
#include "screenshot.hpp"
#include <SDL3/SDL.h>
#ifdef CTBROWSER_WITH_IMAGE
#include <SDL3_image/SDL_image.h>
#endif
#ifdef CTBROWSER_WITH_TTF
#include <SDL3_ttf/SDL_ttf.h>
#endif
#ifndef CTBROWSER_IN_A_MODULE
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#endif

// The SDL3 shell: a window, a renderer, an event loop. Boxes render as
// filled rects, text as the embedded 8x8 font scaled to the computed
// font-size, and every <canvas> streams its pixel buffer into an SDL
// texture. Input flows into the engine (polling state + onKey/onClick/
// onMouse* events), sound plays through the audio mixer, and
// screenshots capture the renderer to PNG. SDL3 carries all of it to
// Windows, macOS, Linux and the BSDs.
//
// Headless operation (tests, CI): SDL_VIDEODRIVER=dummy renders in
// software, SDL_AUDIODRIVER=dummy swallows sound, CTBROWSER_TEST_FRAMES
// bounds the run (and switches to a fixed 1/60s timestep for
// deterministic frames), CTBROWSER_SCREENSHOT=path captures the last
// frame.

namespace ctbrowser {

struct app_options {
	std::int32_t width = 800;
	std::int32_t height = 600;
	std::int32_t max_frames = 0; // 0 = run until quit; >0 = auto-exit (tests/CI)
	double fixed_dt = 0;         // 0 = real time; >0 = deterministic timestep
	std::int32_t max_fps = 60;   // interactive frame cap (0 = uncapped); browsers
	                             // throttle requestAnimationFrame the same way -
	                             // fixed-step pages (examples/pong.html) depend on it
	std::int32_t logical_w = 0;  // >0: fixed-resolution presentation,
	std::int32_t logical_h = 0;  //     letterboxed and scaled to the window
	bool fullscreen = false;
	bool clear_white = true;            // page background
	std::string screenshot_path;        // capture to PNG...
	std::int32_t screenshot_frame = -1; // ...at this frame (-1 = the last one)
	// a TrueType font for page text (SDL3_ttf builds); "" probes common
	// system locations and falls back to the embedded 8x8 font
	std::string font_path;
	// compile-time-embedded assets (std::embed / #embed builds), keyed
	// by the exact strings scripts pass to loadImage/playSound; loaders
	// fall back to the filesystem for anything not listed
	std::vector<embedded_asset> assets;
};

namespace detail {

// --- SDL handles, with scope-bound lifetimes -------------------------
// SDL hands out raw pointers with matching destroy functions, which is
// fine right up until an early return skips the destroy. Owning these
// means the teardown cannot be forgotten - and it un-forgets one: the
// renderer-creation failure path used to return without destroying the
// window and the three cursors it had already made.
template <auto Destroy> struct sdl_deleter {
	template <typename T> void operator()(T * p) const noexcept {
		if (p != nullptr) { Destroy(p); }
	}
};
using window_ptr = std::unique_ptr<SDL_Window, sdl_deleter<SDL_DestroyWindow>>;
using renderer_ptr = std::unique_ptr<SDL_Renderer, sdl_deleter<SDL_DestroyRenderer>>;
using cursor_ptr = std::unique_ptr<SDL_Cursor, sdl_deleter<SDL_DestroyCursor>>;
using texture_ptr = std::unique_ptr<SDL_Texture, sdl_deleter<SDL_DestroyTexture>>;

// SDL_Init/SDL_Quit as a scope; SDL_Quit runs only if the init succeeded
struct sdl_session {
	bool ok = false;
	explicit sdl_session(SDL_InitFlags flags) : ok(SDL_Init(flags)) {}
	~sdl_session() {
		if (ok) { SDL_Quit(); }
	}
	sdl_session(const sdl_session &) = delete;
	sdl_session & operator=(const sdl_session &) = delete;
};

#ifdef CTBROWSER_WITH_TTF
// the same for SDL_ttf - and this one is a fix: TTF_Quit used to run
// unconditionally, including when TTF_Init had failed
struct ttf_session {
	bool ok = false;
	ttf_session() : ok(TTF_Init()) {}
	~ttf_session() {
		if (ok) { TTF_Quit(); }
	}
	ttf_session(const ttf_session &) = delete;
	ttf_session & operator=(const ttf_session &) = delete;
};
#endif

// strip CSS quoting and surrounding spaces off a font-family name or a
// url(...) payload - both arrive quoted, unquoted or padded
[[nodiscard]] inline std::string unquote(std::string_view v) {
	constexpr std::string_view trim = " \t\"'";
	const std::size_t b = v.find_first_not_of(trim);
	if (b == std::string_view::npos) { return {}; }
	return std::string{v.substr(b, v.find_last_not_of(trim) - b + 1)};
}

// Resolve an asset path independently of the CURRENT DIRECTORY: try it
// as-is (cwd), then relative to the executable's directory, then to
// its parent. A game must find its sprites no matter where it was
// launched from - "" when nothing exists (callers log loudly).
[[nodiscard]] inline std::string resolve_asset(const std::string & path) {
	namespace fs = std::filesystem;
	std::error_code ignored;
	if (fs::exists(path, ignored)) { return path; }
	if (const char * base = SDL_GetBasePath()) {
		for (const fs::path & candidate :
		     {fs::path{base} / path, fs::path{base}.parent_path().parent_path() / path}) {
			if (fs::exists(candidate, ignored)) { return candidate.string(); }
		}
	}
	return {};
}

// paint_cmd carries 0xAARRGGBB; SDL wants the channels apart. This
// unpacking appeared at every place a paint reached the renderer.
[[nodiscard]] constexpr SDL_Color sdl_color_of(std::uint32_t argb) noexcept {
	return SDL_Color{static_cast<Uint8>((argb >> 16) & 0xFF),
	                 static_cast<Uint8>((argb >> 8) & 0xFF), static_cast<Uint8>(argb & 0xFF),
	                 static_cast<Uint8>((argb >> 24) & 0xFF)};
}
inline void set_draw_color(SDL_Renderer * r, std::uint32_t argb) {
	const SDL_Color c = sdl_color_of(argb);
	SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

inline void draw_text(SDL_Renderer * r, const paint_cmd & cmd) {
	const float scale = static_cast<float>(cmd.font_px) / 8.0f;
	set_draw_color(r, cmd.argb);
	float pen_x = static_cast<float>(cmd.x);
	const float pen_y = static_cast<float>(cmd.y);
	for (const char32_t c : cmd.text) { // UTF-32 code points
		for (std::int32_t row = 0; row < 8; ++row) {
			// italic: shear the glyph - upper rows shift right
			const float shear = cmd.italic ? static_cast<float>(7 - row) * scale / 3.0f : 0.0f;
			for (std::int32_t col = 0; col < 8; ++col) {
				if (!glyph_pixel(c, row, col)) { continue; }
				const SDL_FRect px{pen_x + shear + static_cast<float>(col) * scale,
				                   pen_y + static_cast<float>(row) * scale, scale, scale};
				SDL_RenderFillRect(r, &px);
				if (cmd.bold) { // double-strike one pixel right
					const SDL_FRect px2{px.x + scale, px.y, px.w, px.h};
					SDL_RenderFillRect(r, &px2);
				}
			}
		}
		pen_x += static_cast<float>(cmd.font_px);
	}
}

#ifdef CTBROWSER_WITH_TTF

using font_ptr = std::unique_ptr<TTF_Font, sdl_deleter<TTF_CloseFont>>;

// The typefaces fonts.hpp embeds: three generic families in four styles
// each, keyed by their asset name. This was twelve near-identical call
// lines; as a table the pattern (and any gap in it) is visible at a
// glance. `key` is NUL-terminated - find_asset takes a C string.
struct default_face {
	std::string_view key;
	std::string_view family;
	bool bold;
	bool italic;
};
inline constexpr std::array<default_face, 12> default_faces{{
    {"ctbrowser:font/serif-regular", "serif", false, false},
    {"ctbrowser:font/serif-bold", "serif", true, false},
    {"ctbrowser:font/serif-italic", "serif", false, true},
    {"ctbrowser:font/serif-bolditalic", "serif", true, true},
    {"ctbrowser:font/sans-regular", "sans-serif", false, false},
    {"ctbrowser:font/sans-bold", "sans-serif", true, false},
    {"ctbrowser:font/sans-italic", "sans-serif", false, true},
    {"ctbrowser:font/sans-bolditalic", "sans-serif", true, true},
    {"ctbrowser:font/mono-regular", "monospace", false, false},
    {"ctbrowser:font/mono-bold", "monospace", true, false},
    {"ctbrowser:font/mono-italic", "monospace", false, true},
    {"ctbrowser:font/mono-bolditalic", "monospace", true, true},
}};

// TrueType page text: fonts opened per size, glyphs rendered white
// and tinted with a color mod, textures cached per (text, size)
struct ttf_text {
	SDL_Renderer * renderer = nullptr;
	// the FACE REGISTRY: (family-lowercase, bold, italic) -> bytes or a
	// file path. A page's @font-face entries and the embedded defaults
	// (ctbrowser:font/*) all register here - multiple faces coexist in
	// one document, resolved per text cmd.
	struct face_src {
		const void * mem = nullptr;
		std::size_t mem_size = 0;
		std::string path;
		[[nodiscard]] bool usable() const { return mem != nullptr || !path.empty(); }
	};
	std::map<std::tuple<std::string, bool, bool>, face_src> faces;
	std::string fallback_path; // opts.font_path or the probed system font
	// opened fonts per (face-key, px, synth-style-bits)
	std::map<std::tuple<std::string, bool, bool, std::int32_t>, font_ptr> fonts;
	std::map<std::tuple<std::string, std::int32_t, std::uint8_t>, texture_ptr> cache;

	[[nodiscard]] bool ok() const { return !faces.empty() || !fallback_path.empty(); }

	static std::string fold(std::string_view s) {
		std::string out;
		for (const char c : s) {
			out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
		}
		return out;
	}
	void register_face(std::string_view family, bool bold, bool italic, face_src src) {
		faces.emplace(std::tuple{fold(family), bold, italic}, std::move(src));
	}

	// walk the cmd's font-family fallback list; first registered family
	// wins. Generic keywords (and a few well-known aliases) map to the
	// embedded defaults registered under serif/sans-serif/monospace.
	static std::string generic_of(std::string_view name) {
		const auto has = [name](std::string_view needle) {
			return fold(name).find(needle) != std::string::npos;
		};
		if (has("mono") || has("courier") || has("consol")) { return "monospace"; }
		if (has("sans") || has("arial") || has("helvetica") || has("system-ui") || has("ui-sans")) {
			return "sans-serif";
		}
		if (has("serif") || has("times") || has("georgia")) { return "serif"; }
		return {};
	}
	// resolve (family-list, bold, italic) -> the face key to open + which
	// synthetic styles TTF must add because the exact variant is missing
	std::tuple<std::string, bool, bool, std::uint8_t> resolve(std::string_view family_list,
	                                                          bool bold, bool italic) {
		const auto try_family = [this, bold, italic](std::string name)
		    -> std::optional<std::tuple<std::string, bool, bool, std::uint8_t>> {
			// exact variant, then regular + synthetic styling
			if (faces.contains({name, bold, italic})) {
				return std::tuple{name, bold, italic, std::uint8_t{0}};
			}
			std::uint8_t synth = 0;
			if (bold) { synth |= TTF_STYLE_BOLD; }
			if (italic) { synth |= TTF_STYLE_ITALIC; }
			if (faces.contains({name, false, false})) {
				return std::tuple{name, false, false, synth};
			}
			return std::nullopt;
		};
		std::string_view rest = family_list;
		while (!rest.empty()) {
			const std::size_t comma = rest.find(',');
			std::string_view tok = comma == std::string_view::npos ? rest : rest.substr(0, comma);
			rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
			while (!tok.empty() &&
			       (tok.front() == ' ' || tok.front() == '"' || tok.front() == '\'')) {
				tok.remove_prefix(1);
			}
			while (!tok.empty() && (tok.back() == ' ' || tok.back() == '"' || tok.back() == '\'')) {
				tok.remove_suffix(1);
			}
			if (tok.empty()) { continue; }
			if (auto hit = try_family(fold(tok))) { return *hit; }
			const std::string gen = generic_of(tok);
			if (!gen.empty()) {
				if (auto hit = try_family(gen)) { return *hit; }
			}
		}
		// no family matched: serif is the document default (Firefox)
		if (auto hit = try_family("serif")) { return *hit; }
		std::uint8_t synth = 0;
		if (bold) { synth |= TTF_STYLE_BOLD; }
		if (italic) { synth |= TTF_STYLE_ITALIC; }
		return {std::string{}, false, false, synth}; // the fallback face
	}

	TTF_Font * font(std::string_view family_list, bool bold, bool italic, std::int32_t px) {
		auto [name, fb, fi, synth] = resolve(family_list, bold, italic);
		const std::tuple key{name + (fb ? "/b" : "") + (fi ? "/i" : ""), bold, italic, px};
		if (const auto it = fonts.find(key); it != fonts.end()) { return it->second.get(); }
		font_ptr f;
		if (!name.empty()) {
			const face_src & src = faces.at({name, fb, fi});
			// a fresh IO per size; closeio=true has TTF read the font fully
			// in and close it, so the bytes need only outlive this call
			f.reset(src.mem != nullptr ? TTF_OpenFontIO(SDL_IOFromConstMem(src.mem, src.mem_size),
			                                            true, static_cast<float>(px))
			                           : TTF_OpenFont(src.path.c_str(), static_cast<float>(px)));
		} else if (!fallback_path.empty()) {
			f.reset(TTF_OpenFont(fallback_path.c_str(), static_cast<float>(px)));
		}
		if (f && synth != 0) { TTF_SetFontStyle(f.get(), synth); }
		TTF_Font * raw = f.get(); // the map owns it from here
		fonts.emplace(key, std::move(f));
		return raw;
	}
	std::int32_t measure(std::u32string_view text, std::int32_t px, std::string_view family,
	                     bool bold, bool italic) {
		TTF_Font * f = font(family, bold, italic, px);
		const std::string utf8 = utf32_to_utf8(text); // SDL_ttf takes UTF-8
		if (f == nullptr) { return static_cast<std::int32_t>(text.size()) * px; }
		std::int32_t w = 0, h = 0;
		TTF_GetStringSize(f, utf8.data(), utf8.size(), &w, &h);
		return w;
	}
	void draw(const paint_cmd & cmd) {
		TTF_Font * f = font(cmd.font_family, cmd.bold, cmd.italic, cmd.font_px);
		if (f == nullptr) { return; }
		SDL_Texture * t = nullptr;
		const std::string utf8 = utf32_to_utf8(cmd.text); // SDL_ttf takes UTF-8
		const std::uint8_t stylebits =
		    static_cast<std::uint8_t>((cmd.bold ? 1u : 0u) | (cmd.italic ? 2u : 0u));
		const std::tuple<std::string, std::int32_t, std::uint8_t> key{
		    utf8 + "\x1f" + fold(cmd.font_family), cmd.font_px, stylebits};
		if (const auto it = cache.find(key); it != cache.end()) {
			t = it->second.get();
		} else {
			// rendered strings change rarely, so the cache is dropped
			// wholesale rather than evicted entry by entry
			if (cache.size() > texture_cache_cap) { cache.clear(); }
			SDL_Surface * s =
			    TTF_RenderText_Blended(f, utf8.c_str(), utf8.size(), SDL_Color{255, 255, 255, 255});
			if (s == nullptr) { return; }
			texture_ptr owned{SDL_CreateTextureFromSurface(renderer, s)};
			SDL_DestroySurface(s);
			if (!owned) { return; }
			t = owned.get();
			cache.emplace(key, std::move(owned));
		}
		const SDL_Color tint = sdl_color_of(cmd.argb);
		SDL_SetTextureColorMod(t, tint.r, tint.g, tint.b);
		SDL_SetTextureAlphaMod(t, tint.a);
		float tw = 0, th = 0;
		SDL_GetTextureSize(t, &tw, &th);
		const SDL_FRect dst{static_cast<float>(cmd.x), static_cast<float>(cmd.y), tw, th};
		SDL_RenderTexture(renderer, t, nullptr, &dst);
	}

	static constexpr std::size_t texture_cache_cap = 256;
};

// find a usable font when none was configured
[[nodiscard]] inline std::string probe_font() {
	static constexpr std::array<std::string_view, 5> candidates{
	    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
	    "/usr/share/fonts/TTF/DejaVuSans.ttf",
	    "/System/Library/Fonts/Helvetica.ttc",
	    "C:\\Windows\\Fonts\\arial.ttf",
	};
	std::error_code ignored;
	const auto hit = std::ranges::find_if(
	    candidates, [&](std::string_view c) { return std::filesystem::exists(c, ignored); });
	return hit != candidates.end() ? std::string{*hit} : std::string{};
}

#endif // CTBROWSER_WITH_TTF

struct canvas_textures {
	SDL_Renderer * renderer = nullptr;
	struct entry {
		texture_ptr tex;
		std::int32_t w = 0, h = 0;
	};
	std::map<const node *, entry> cache;

	SDL_Texture * of(node * n) {
		if (const auto it = cache.find(n); it != cache.end()) {
			// the canvas kept its size: reuse; else recreate (engine.resize)
			if (it->second.w == n->canvas_w && it->second.h == n->canvas_h) {
				return it->second.tex.get();
			}
			cache.erase(it); // erasing destroys the stale texture
		}
		texture_ptr t{SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
		                                SDL_TEXTUREACCESS_STREAMING, n->canvas_w, n->canvas_h)};
		SDL_SetTextureScaleMode(t.get(), SDL_SCALEMODE_NEAREST);
		SDL_SetTextureBlendMode(t.get(), SDL_BLENDMODE_BLEND); // clearRect shows the page
		SDL_Texture * raw = t.get();                           // the map owns it from here
		cache.emplace(n, entry{std::move(t), n->canvas_w, n->canvas_h});
		return raw;
	}
};

// The environment gets a say in the options: CTBROWSER_TEST_FRAMES bounds
// a run and CTBROWSER_SCREENSHOT captures one, both so CI and ctest can
// drive a windowed app headlessly without the caller coding for it. An
// explicitly-set option always wins. A bounded run also becomes a
// DETERMINISTIC one - a fixed 1/60 s step, so frame N is always frame N.
inline void apply_env_defaults(app_options & opts) {
	// the embedded default typefaces (fonts.hpp) join the asset registry
	// AFTER any caller-provided assets - user entries win on key clashes
	for (embedded_asset & fa : default_font_assets()) { opts.assets.push_back(std::move(fa)); }
	if (opts.max_frames == 0) {
		if (const char * env = SDL_getenv("CTBROWSER_TEST_FRAMES")) {
			opts.max_frames = SDL_atoi(env);
		}
	}
	if (opts.screenshot_path.empty()) {
		if (const char * env = SDL_getenv("CTBROWSER_SCREENSHOT")) { opts.screenshot_path = env; }
	}
	if (opts.fixed_dt == 0 && opts.max_frames > 0) { opts.fixed_dt = 1.0 / 60.0; }
}

// the engine's BMP reader runs against the literal path first; this
// shell decoder then retries with cwd-independent path resolution -
// and, with SDL3_image, decodes PNG/JPG/WebP too. Failures LOG: a
// missing sprite sheet must never be a silently invisible game.
[[nodiscard]] inline std::function<image(const std::string &)> make_image_decoder() {
	return [](const std::string & path) -> image {
		const std::string resolved = detail::resolve_asset(path);
		if (resolved.empty()) {
			SDL_Log("ctbrowser: loadImage: no such file: %s", path.c_str());
			return {};
		}
		if (image bmp = load_bmp(resolved); bmp.ok()) { return bmp; }
#ifdef CTBROWSER_WITH_IMAGE
		SDL_Surface * s = IMG_Load(resolved.c_str());
		if (s == nullptr) {
			SDL_Log("ctbrowser: loadImage: undecodable: %s", resolved.c_str());
			return {};
		}
		SDL_Surface * argb = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
		SDL_DestroySurface(s);
		if (argb == nullptr) { return {}; }
		image out;
		out.w = argb->w;
		out.h = argb->h;
		out.pixels.resize(static_cast<std::size_t>(argb->w) * static_cast<std::size_t>(argb->h));
		for (std::int32_t y = 0; y < argb->h; ++y) {
			const uint32_t * row = reinterpret_cast<const uint32_t *>(
			    static_cast<const unsigned char *>(argb->pixels) +
			    static_cast<std::size_t>(y) * static_cast<std::size_t>(argb->pitch));
			for (std::int32_t x = 0; x < argb->w; ++x) {
				out.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(argb->w) +
				           static_cast<std::size_t>(x)] = row[x];
			}
		}
		SDL_DestroySurface(argb);
		return out;
#else
		SDL_Log("ctbrowser: loadImage: not a readable BMP (install SDL3_image "
		        "for PNG/JPG): %s",
		        resolved.c_str());
		return {};
#endif
	};
}

#ifdef CTBROWSER_WITH_TTF
// Fill the face registry, in the order that decides which face wins:
//   1. the embedded defaults (fonts.hpp) under the generic family names,
//   2. every page @font-face - family + url(), embedded copy preferred,
//      then resolved like any asset, with a public-root "/x" also tried
//      repo-relative - so a page may declare MANY families and variants,
//   3. a last-resort fallback: opts.font_path, else a probed system font.
template <typename Engine>
void register_faces(ttf_text & ttf, Engine & e, const app_options & opts) {
	for (const default_face & f : default_faces) {
		if (const embedded_asset * a = find_asset(&e.assets, f.key.data())) {
			ttf.register_face(f.family, f.bold, f.italic, {a->data, a->size, {}});
		}
	}
	for (const auto & ff : e.font_faces()) {
		const std::string family = unquote(ff.get("font-family"));
		if (family.empty()) { continue; }
		const std::string src{ff.get("src")};
		const std::size_t open = src.find("url(");
		if (open == std::string::npos) { continue; }
		const std::size_t s = open + 4, close = src.find(')', s);
		if (close == std::string::npos) { continue; }
		const std::string path = unquote(src.substr(s, close - s));
		const std::string weight{ff.get("font-weight")};
		const std::string style{ff.get("font-style")};
		const bool bold =
		    weight.find("bold") != std::string::npos || weight.find("700") != std::string::npos ||
		    weight.find("800") != std::string::npos || weight.find("900") != std::string::npos;
		const bool italic =
		    style.find("italic") != std::string::npos || style.find("oblique") != std::string::npos;
		if (const embedded_asset * emb = find_asset(&e.assets, path)) {
			ttf.register_face(family, bold, italic, {emb->data, emb->size, {}});
			continue;
		}
		std::string file = resolve_asset(path);
		if (file.empty() && path.size() > 1 && path[0] == '/') {
			file = resolve_asset(path.substr(1)); // a public-root path, tried repo-relative
		}
		if (!file.empty()) { ttf.register_face(family, bold, italic, {nullptr, 0, file}); }
	}
	ttf.fallback_path = !opts.font_path.empty() ? opts.font_path : probe_font();
}
#endif

// One frame's paint list onto the renderer. How TEXT is drawn depends on
// whether a TrueType face loaded, which is the caller's business, so it
// arrives as a callable and this stays free of the #ifdef.
template <typename DrawText>
void draw_paints(SDL_Renderer * r, const std::vector<paint_cmd> & paints,
                 canvas_textures & textures, DrawText && draw_text_cmd) {
	for (const paint_cmd & cmd : paints) {
		switch (cmd.what) {
		case paint_cmd::kind::box: {
			set_draw_color(r, cmd.argb);
			const SDL_FRect box{static_cast<float>(cmd.x), static_cast<float>(cmd.y),
			                    static_cast<float>(cmd.w), static_cast<float>(cmd.h)};
			SDL_RenderFillRect(r, &box);
			break;
		}
		case paint_cmd::kind::text: draw_text_cmd(cmd); break;
		case paint_cmd::kind::canvas: {
			SDL_Texture * t = textures.of(cmd.canvas_node);
			SDL_UpdateTexture(t, nullptr, cmd.canvas_node->pixels.data(),
			                  cmd.canvas_node->canvas_w * 4);
			const SDL_FRect dst{static_cast<float>(cmd.x), static_cast<float>(cmd.y),
			                    static_cast<float>(cmd.w), static_cast<float>(cmd.h)};
			SDL_RenderTexture(r, t, nullptr, &dst);
			break;
		}
		}
	}
}

// Drain SDL's queue into the engine. Returns false when the user quit.
// Mouse coordinates go through SDL_ConvertEventToRenderCoordinates first
// so a letterboxed presentation still reports page-space positions.
template <typename Engine> [[nodiscard]] bool pump_events(Engine & e, SDL_Renderer * r) {
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_EVENT_QUIT: return false;
		case SDL_EVENT_MOUSE_MOTION:
			SDL_ConvertEventToRenderCoordinates(r, &ev);
			e.mouse_move(ev.motion.x, ev.motion.y);
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			SDL_ConvertEventToRenderCoordinates(r, &ev);
			e.mouse_button(ev.button.x, ev.button.y, ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN,
			               ev.button.button == SDL_BUTTON_RIGHT    ? 2
			               : ev.button.button == SDL_BUTTON_MIDDLE ? 1
			                                                       : 0);
			break;
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
			// repeats replay the editing default (held Backspace)
			if (!ev.key.repeat || ev.type == SDL_EVENT_KEY_DOWN) {
				e.key(SDL_GetKeyName(ev.key.key), ev.type == SDL_EVENT_KEY_DOWN);
			}
			break;
		case SDL_EVENT_TEXT_INPUT: e.text_input(ev.text.text); break;
		case SDL_EVENT_MOUSE_WHEEL:
			SDL_ConvertEventToRenderCoordinates(r, &ev);
			e.wheel(ev.wheel.mouse_x, ev.wheel.mouse_y, ev.wheel.y);
			break;
		default: break;
		}
	}
	return true;
}

// The host bindings only the SHELL can supply: audio, screenshots and
// fullscreen all need the window the engine deliberately knows nothing
// about. They drive shell state, so they take it by reference rather
// than owning it.
[[nodiscard]] inline std::vector<ctjs::binding> make_shell_bindings(audio_mixer & mixer,
                                                                    std::string & pending_shot,
                                                                    bool & want_fullscreen,
                                                                    bool & fullscreen_dirty) {
	std::vector<ctjs::binding> out;
	out.push_back({"playSound", ctjs::native(
	                                [&mixer](const std::vector<ctjs::value> & a) -> ctjs::value {
		                                if (a.empty()) { return ctjs::value{false}; }
		                                const std::string resolved = resolve_asset(arg_str(a, 0));
		                                if (resolved.empty()) {
			                                SDL_Log("ctbrowser: playSound: no such file: %s",
											        arg_str(a, 0).c_str());
			                                return ctjs::value{false};
		                                }
		                                return ctjs::value{mixer.play(resolved)};
	                                },
	                                "playSound")});
	out.push_back({"setVolume", ctjs::native(
	                                [&mixer](const std::vector<ctjs::value> & a) -> ctjs::value {
		                                if (!a.empty()) {
			                                mixer.set_volume(static_cast<float>(arg_num(a, 0)));
		                                }
		                                return {};
	                                },
	                                "setVolume")});
	out.push_back(
	    {"screenshot", ctjs::native(
	                       [&pending_shot](const std::vector<ctjs::value> & a) -> ctjs::value {
		                       if (!a.empty()) { pending_shot = arg_str(a, 0); }
		                       return {};
	                       },
	                       "screenshot")});
	out.push_back({"setFullscreen", ctjs::native(
	                                    [&want_fullscreen, &fullscreen_dirty](
	                                        const std::vector<ctjs::value> & a) -> ctjs::value {
		                                    want_fullscreen = arg_bool(a, 0);
		                                    fullscreen_dirty = true;
		                                    return {};
	                                    },
	                                    "setFullscreen")});
	return out;
}

} // namespace detail

// run a page as a windowed application; returns the process exit code
template <typename Page> std::int32_t run_app(app_options opts = {}) {
	detail::apply_env_defaults(opts);

	// shell state the script bindings feed
	audio_mixer mixer;
	std::string pending_shot;
	bool want_fullscreen = opts.fullscreen;
	bool fullscreen_dirty = false;

	engine<Page> e{
	    detail::make_shell_bindings(mixer, pending_shot, want_fullscreen, fullscreen_dirty),
	    detail::make_image_decoder(), opts.assets};
	mixer.embedded = &e.assets;

	// the anchor default action: clicking <a href> opens the system's web
	// browser at that URL (fragment links never reach this hook)
	e.open_url = [](std::string_view url) { SDL_OpenURL(std::string{url}.c_str()); };
	// the system clipboard behind Ctrl+C/X/V and the context menu
	e.clipboard_set = [](std::string_view text) {
		SDL_SetClipboardText(std::string{text}.c_str());
	};
	e.clipboard_get = []() -> std::string {
		char * t = SDL_GetClipboardText();
		std::string out = t != nullptr ? t : "";
		SDL_free(t);
		return out;
	};

	// route BABYLON.Sound (babylon.hpp) through the mixer: it calls these hooks
	// with the sound's url; the resolver maps it to an embedded asset or a file
	e.ev.play_audio = [&mixer](const std::string & url, bool loop) -> std::int32_t {
		const std::string resolved = detail::resolve_asset(url);
		return resolved.empty() ? 0 : mixer.play(resolved, loop);
	};
	e.ev.stop_audio = [&mixer](std::int32_t handle) { mixer.stop(handle); };
	e.ev.set_audio_volume = [&mixer](float v) { mixer.set_volume(v); };

	// OWNERSHIP lives in these five (plus the session). Destructors run in
	// reverse declaration order, which reproduces the teardown this
	// function used to spell out by hand: cursors, then renderer, then
	// window, then SDL_Quit - and now on the early return as well.
	const detail::sdl_session sdl{SDL_INIT_VIDEO};
	if (!sdl.ok) {
		SDL_Log("ctbrowser: SDL_Init failed: %s", SDL_GetError());
		return 1;
	}
	detail::window_ptr owned_window{
	    SDL_CreateWindow(e.title.c_str(), opts.width, opts.height,
		                 SDL_WINDOW_RESIZABLE | (want_fullscreen ? SDL_WINDOW_FULLSCREEN : 0))};
	detail::renderer_ptr owned_renderer{
	    owned_window ? SDL_CreateRenderer(owned_window.get(), nullptr) : nullptr};
	// the hover cursors (arrow / hand over links / I-beam over text),
	// switched per frame from the engine's CSS-resolved cursor kind
	detail::cursor_ptr owned_arrow{SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT)};
	detail::cursor_ptr owned_pointer{SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER)};
	detail::cursor_ptr owned_text{SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT)};

	// non-owning views: the shell below only ever uses the handles
	SDL_Window * window = owned_window.get();
	SDL_Renderer * renderer = owned_renderer.get();
	SDL_Cursor * cur_arrow = owned_arrow.get();
	SDL_Cursor * cur_pointer = owned_pointer.get();
	SDL_Cursor * cur_text = owned_text.get();

	if (window != nullptr) {
		SDL_StartTextInput(window); // editable controls receive SDL_EVENT_TEXT_INPUT
	}
	auto shown_cursor = decltype(e.cursor()){};
	if (cur_arrow != nullptr) { SDL_SetCursor(cur_arrow); }
	if (renderer == nullptr) {
		// returning here used to leak the window and all three cursors
		SDL_Log("ctbrowser: window/renderer failed: %s", SDL_GetError());
		return 1;
	}
	if (opts.logical_w > 0 && opts.logical_h > 0) {
		SDL_SetRenderLogicalPresentation(renderer, opts.logical_w, opts.logical_h,
		                                 SDL_LOGICAL_PRESENTATION_LETTERBOX);
	}
	// vsync where the driver honors it; the explicit pacing below covers
	// the rest (dummy driver, disabled compositors, >60 Hz displays)
	SDL_SetRenderVSync(renderer, 1);

#ifdef CTBROWSER_WITH_TTF
	const detail::ttf_session ttf_lib; // declared out here so TTF_Quit follows `ttf`
#endif

	{ // scope: GPU/font resources release before the library teardown
#ifdef CTBROWSER_WITH_TTF
		detail::ttf_text ttf;
		ttf.renderer = renderer;
		if (ttf_lib.ok) {
			detail::register_faces(ttf, e, opts);
			if (ttf.ok()) {
				e.measure = [&ttf](std::u32string_view text, std::int32_t px,
				                   std::string_view family, bool bold, bool italic) {
					return ttf.measure(text, px, family, bold, italic);
				};
			}
		}
#endif

		detail::canvas_textures textures{renderer, {}};
		std::string shown_title = e.title;
		Uint64 last = SDL_GetTicks();
		Uint64 frame_start_ns = SDL_GetTicksNS();
		std::int32_t frame = 0;
		bool running = true;

		bool in_render = false;
		// The presentation size: a fixed logical resolution when one is
		// configured (letterboxed), else whatever the window currently is.
		// render_one needs this twice - once to keep the engine viewport in
		// sync, once to lay out - and the two must not drift apart.
		const auto view_size = [&opts, window] {
			std::pair<std::int32_t, std::int32_t> wh{opts.width, opts.height};
			if (opts.logical_w > 0) {
				wh = {opts.logical_w, opts.logical_h};
			} else {
				SDL_GetWindowSize(window, &wh.first, &wh.second);
			}
			return wh;
		};
		// one full frame: fullscreen, viewport (+ resize event), tick, layout,
		// paint. Factored out so the live-resize event watch below can drive it
		// while the OS modal resize loop has our while() blocked.
		std::function<void()> render_one = [&]() {
			if (in_render) { return; }
			in_render = true;
			if (fullscreen_dirty) {
				fullscreen_dirty = false;
				SDL_SetWindowFullscreen(window, want_fullscreen);
			}

			// keep the viewport in sync with the window BEFORE the frame runs;
			// a size change fires a DOM "resize" event so scripts can react
			// (BabylonJS: window.addEventListener('resize', ()=>engine.resize()))
			{
				const auto [vw, vh] = view_size();
				e.resize_viewport(vw, vh);
			}

			const Uint64 now = SDL_GetTicks();
			const double dt =
			    opts.fixed_dt > 0 ? opts.fixed_dt : static_cast<double>(now - last) / 1000.0;
			last = now;
			e.tick(dt);

			if (e.title != shown_title) {
				shown_title = e.title;
				SDL_SetWindowTitle(window, shown_title.c_str());
			}
			if (const auto want = e.cursor(); want != shown_cursor) {
				shown_cursor = want;
				using ck = std::remove_const_t<decltype(want)>;
				SDL_Cursor * c = want == ck::pointer ? cur_pointer
				                 : want == ck::text  ? cur_text
				                                     : cur_arrow;
				if (c != nullptr) { SDL_SetCursor(c); }
			}

			const auto [view_w, view_h] = view_size();
			const std::vector<paint_cmd> paints = e.frame(view_w);

			detail::set_draw_color(renderer, opts.clear_white ? 0xFFFFFFFFu : 0xFF000000u);
			SDL_RenderClear(renderer);
			detail::draw_paints(renderer, paints, textures, [&](const paint_cmd & cmd) {
#ifdef CTBROWSER_WITH_TTF
				if (ttf.ok()) {
					ttf.draw(cmd);
					return;
				}
#endif
				detail::draw_text(renderer, cmd);
			});
			in_render = false;
		};

		// live window resize: while the user drags a window edge the OS runs a
		// modal loop that blocks our while(); an SDL event watch still fires
		// there, so we render + present from it to track the drag smoothly.
		struct resize_watch {
			std::function<void()> * render;
			SDL_Renderer * renderer;
		};
		resize_watch rw{&render_one, renderer};
		SDL_EventFilter watch_cb = [](void * ud, SDL_Event * we) -> bool {
			static bool in_watch = false;
			auto * st = static_cast<resize_watch *>(ud);
			if (!in_watch && (we->type == SDL_EVENT_WINDOW_RESIZED ||
			                  we->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
			                  we->type == SDL_EVENT_WINDOW_EXPOSED)) {
				in_watch = true;
				(*st->render)();
				SDL_RenderPresent(st->renderer);
				in_watch = false;
			}
			return true;
		};
		SDL_AddEventWatch(watch_cb, &rw);

		while (running) {
			running = detail::pump_events(e, renderer);

			render_one();

			// screenshots capture BEFORE present (the composed frame)
			const bool auto_shot =
			    !opts.screenshot_path.empty() &&
			    ((opts.screenshot_frame >= 0 && frame == opts.screenshot_frame) ||
				 (opts.screenshot_frame < 0 && opts.max_frames > 0 &&
				  frame == opts.max_frames - 1));
			if (auto_shot) { save_screenshot(renderer, opts.screenshot_path.c_str()); }
			if (!pending_shot.empty()) {
				save_screenshot(renderer, pending_shot.c_str());
				pending_shot.clear();
			}

			SDL_RenderPresent(renderer);

			// interactive runs pace like a browser paces requestAnimationFrame;
			// bounded (test/CI) runs sprint through their frames instead
			if (opts.max_frames == 0 && opts.max_fps > 0) {
				const Uint64 target_ns = 1000000000ull / static_cast<Uint64>(opts.max_fps);
				const Uint64 elapsed_ns = SDL_GetTicksNS() - frame_start_ns;
				if (elapsed_ns < target_ns) { SDL_DelayNS(target_ns - elapsed_ns); }
				frame_start_ns = SDL_GetTicksNS();
			}

			if (opts.max_frames > 0 && ++frame >= opts.max_frames) {
				running = false;
			} else if (opts.max_frames == 0) {
				++frame;
			}
		}
		SDL_RemoveEventWatch(watch_cb, &rw);
	} // resource scope: the font cache and canvas textures release here

	// No teardown block. The TTF session, the cursors, the renderer, the
	// window and the SDL session all unwind from here in reverse
	// declaration order - the same sequence, now impossible to skip.
	return 0;
}

} // namespace ctbrowser

#endif
