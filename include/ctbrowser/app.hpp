#ifndef CTBROWSER__APP__HPP
#define CTBROWSER__APP__HPP

#include <cstdint>

#include <cstddef>

#include "engine.hpp"
#include "font8x8.hpp"
#include "fonts.hpp"
#include "audio.hpp"
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
	std::int32_t max_frames = 0;          // 0 = run until quit; >0 = auto-exit (tests/CI)
	double fixed_dt = 0;         // 0 = real time; >0 = deterministic timestep
	std::int32_t max_fps = 60;            // interactive frame cap (0 = uncapped); browsers
	                             // throttle requestAnimationFrame the same way -
	                             // fixed-step pages (examples/pong.html) depend on it
	std::int32_t logical_w = 0;           // >0: fixed-resolution presentation,
	std::int32_t logical_h = 0;           //     letterboxed and scaled to the window
	bool fullscreen = false;
	bool clear_white = true;     // page background
	std::string screenshot_path; // capture to PNG...
	std::int32_t screenshot_frame = -1;   // ...at this frame (-1 = the last one)
	// a TrueType font for page text (SDL3_ttf builds); "" probes common
	// system locations and falls back to the embedded 8x8 font
	std::string font_path;
	// compile-time-embedded assets (std::embed / #embed builds), keyed
	// by the exact strings scripts pass to loadImage/playSound; loaders
	// fall back to the filesystem for anything not listed
	std::vector<embedded_asset> assets;
};

namespace detail {

// Resolve an asset path independently of the CURRENT DIRECTORY: try it
// as-is (cwd), then relative to the executable's directory, then to
// its parent. A game must find its sprites no matter where it was
// launched from - "" when nothing exists (callers log loudly).
inline std::string resolve_asset(const std::string & path) {
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

inline void draw_text(SDL_Renderer * r, const paint_cmd & cmd) {
	const float scale = static_cast<float>(cmd.font_px) / 8.0f;
	SDL_SetRenderDrawColor(r, static_cast<Uint8>((cmd.argb >> 16) & 0xFF),
	                       static_cast<Uint8>((cmd.argb >> 8) & 0xFF),
	                       static_cast<Uint8>(cmd.argb & 0xFF),
	                       static_cast<Uint8>((cmd.argb >> 24) & 0xFF));
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
		bool usable() const { return mem != nullptr || !path.empty(); }
	};
	std::map<std::tuple<std::string, bool, bool>, face_src> faces;
	std::string fallback_path; // opts.font_path or the probed system font
	// opened fonts per (face-key, px, synth-style-bits)
	std::map<std::tuple<std::string, bool, bool, std::int32_t>, TTF_Font *> fonts;
	std::map<std::tuple<std::string, std::int32_t, std::uint8_t>, SDL_Texture *> cache;

	bool ok() const { return !faces.empty() || !fallback_path.empty(); }

	static std::string fold(std::string_view s) {
		std::string out;
		for (const char c : s) { out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c); }
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
		if (has("sans") || has("arial") || has("helvetica") || has("system-ui") || has("ui-sans")) { return "sans-serif"; }
		if (has("serif") || has("times") || has("georgia")) { return "serif"; }
		return {};
	}
	// resolve (family-list, bold, italic) -> the face key to open + which
	// synthetic styles TTF must add because the exact variant is missing
	std::tuple<std::string, bool, bool, std::uint8_t> resolve(std::string_view family_list, bool bold,
	                                                          bool italic) {
		const auto try_family = [this, bold, italic](std::string name)
		    -> std::optional<std::tuple<std::string, bool, bool, std::uint8_t>> {
			// exact variant, then regular + synthetic styling
			if (faces.contains({name, bold, italic})) { return std::tuple{name, bold, italic, std::uint8_t{0}}; }
			std::uint8_t synth = 0;
			if (bold) { synth |= TTF_STYLE_BOLD; }
			if (italic) { synth |= TTF_STYLE_ITALIC; }
			if (faces.contains({name, false, false})) { return std::tuple{name, false, false, synth}; }
			return std::nullopt;
		};
		std::string_view rest = family_list;
		while (!rest.empty()) {
			const std::size_t comma = rest.find(',');
			std::string_view tok = comma == std::string_view::npos ? rest : rest.substr(0, comma);
			rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
			while (!tok.empty() && (tok.front() == ' ' || tok.front() == '"' || tok.front() == '\'')) { tok.remove_prefix(1); }
			while (!tok.empty() && (tok.back() == ' ' || tok.back() == '"' || tok.back() == '\'')) { tok.remove_suffix(1); }
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
		if (const auto it = fonts.find(key); it != fonts.end()) { return it->second; }
		TTF_Font * f = nullptr;
		if (!name.empty()) {
			const face_src & src = faces.at({name, fb, fi});
			// a fresh IO per size; closeio=true has TTF read the font fully
			// in and close it, so the bytes need only outlive this call
			f = src.mem != nullptr
			        ? TTF_OpenFontIO(SDL_IOFromConstMem(src.mem, src.mem_size), true, static_cast<float>(px))
			        : TTF_OpenFont(src.path.c_str(), static_cast<float>(px));
		} else if (!fallback_path.empty()) {
			f = TTF_OpenFont(fallback_path.c_str(), static_cast<float>(px));
		}
		if (f != nullptr && synth != 0) { TTF_SetFontStyle(f, synth); }
		fonts.emplace(key, f);
		return f;
	}
	std::int32_t measure(std::u32string_view text, std::int32_t px, std::string_view family, bool bold,
	                     bool italic) {
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
			t = it->second;
		} else {
			if (cache.size() > 256) { // texts change rarely; cap the cache
				for (auto & [k, tex] : cache) { SDL_DestroyTexture(tex); }
				cache.clear();
			}
			SDL_Surface * s = TTF_RenderText_Blended(f, utf8.c_str(), utf8.size(),
			                                         SDL_Color{255, 255, 255, 255});
			if (s == nullptr) { return; }
			t = SDL_CreateTextureFromSurface(renderer, s);
			SDL_DestroySurface(s);
			if (t == nullptr) { return; }
			cache.emplace(key, t);
		}
		SDL_SetTextureColorMod(t, static_cast<Uint8>((cmd.argb >> 16) & 0xFF),
		                       static_cast<Uint8>((cmd.argb >> 8) & 0xFF),
		                       static_cast<Uint8>(cmd.argb & 0xFF));
		SDL_SetTextureAlphaMod(t, static_cast<Uint8>((cmd.argb >> 24) & 0xFF));
		float tw = 0, th = 0;
		SDL_GetTextureSize(t, &tw, &th);
		const SDL_FRect dst{static_cast<float>(cmd.x), static_cast<float>(cmd.y), tw, th};
		SDL_RenderTexture(renderer, t, nullptr, &dst);
	}
	~ttf_text() {
		for (auto & [k, t] : cache) { SDL_DestroyTexture(t); }
		for (auto & [k, f] : fonts) { if (f != nullptr) { TTF_CloseFont(f); } }
	}
};

// find a usable font when none was configured
inline std::string probe_font() {
	static const char * candidates[] = {
	    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
	    "/usr/share/fonts/TTF/DejaVuSans.ttf",
	    "/System/Library/Fonts/Helvetica.ttc",
	    "C:\\Windows\\Fonts\\arial.ttf",
	};
	std::error_code ignored;
	for (const char * c : candidates) {
		if (std::filesystem::exists(c, ignored)) { return c; }
	}
	return {};
}

#endif // CTBROWSER_WITH_TTF

struct canvas_textures {
	SDL_Renderer * renderer = nullptr;
	struct entry { SDL_Texture * tex; std::int32_t w, h; };
	std::map<const node *, entry> cache;

	SDL_Texture * of(node * n) {
		if (const auto it = cache.find(n); it != cache.end()) {
			// the canvas kept its size: reuse; else recreate (engine.resize)
			if (it->second.w == n->canvas_w && it->second.h == n->canvas_h) { return it->second.tex; }
			SDL_DestroyTexture(it->second.tex);
			cache.erase(it);
		}
		SDL_Texture * t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
		                                    SDL_TEXTUREACCESS_STREAMING, n->canvas_w,
		                                    n->canvas_h);
		SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
		SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND); // clearRect shows the page
		cache.emplace(n, entry{t, n->canvas_w, n->canvas_h});
		return t;
	}
	~canvas_textures() {
		for (auto & [n, e] : cache) { SDL_DestroyTexture(e.tex); }
	}
};

} // namespace detail

// run a page as a windowed application; returns the process exit code
template <typename Page> std::int32_t run_app(app_options opts = {}) {
	// the embedded default typefaces (fonts.hpp) join the asset registry
	// AFTER any caller-provided assets - user entries win on key clashes
	for (embedded_asset & fa : detail::default_font_assets()) { opts.assets.push_back(std::move(fa)); }
	if (opts.max_frames == 0) {
		if (const char * env = SDL_getenv("CTBROWSER_TEST_FRAMES")) {
			opts.max_frames = SDL_atoi(env);
		}
	}
	if (opts.screenshot_path.empty()) {
		if (const char * env = SDL_getenv("CTBROWSER_SCREENSHOT")) {
			opts.screenshot_path = env;
		}
	}
	if (opts.fixed_dt == 0 && opts.max_frames > 0) {
		opts.fixed_dt = 1.0 / 60.0; // bounded runs are deterministic runs
	}

	// shell state the script bindings feed
	audio_mixer mixer;
	std::string pending_shot;
	bool want_fullscreen = opts.fullscreen;
	bool fullscreen_dirty = false;

	// the engine's BMP reader runs against the literal path first; this
	// shell decoder then retries with cwd-independent path resolution -
	// and, with SDL3_image, decodes PNG/JPG/WebP too. Failures LOG: a
	// missing sprite sheet must never be a silently invisible game.
	auto image_decoder = [](const std::string & path) -> image {
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

	engine<Page> e{{
	    {"playSound", ctjs::native([&mixer](const std::vector<ctjs::value> & a) -> ctjs::value {
		     if (a.empty()) { return ctjs::value{false}; }
		     const std::string resolved = detail::resolve_asset(a[0].to_string());
		     if (resolved.empty()) {
			     SDL_Log("ctbrowser: playSound: no such file: %s",
			             a[0].to_string().c_str());
			     return ctjs::value{false};
		     }
		     return ctjs::value{mixer.play(resolved)};
	     },
	     "playSound")},
	    {"setVolume", ctjs::native([&mixer](const std::vector<ctjs::value> & a) -> ctjs::value {
		     if (!a.empty()) { mixer.set_volume(static_cast<float>(a[0].to_number())); }
		     return {};
	     },
	     "setVolume")},
	    {"screenshot", ctjs::native([&pending_shot](const std::vector<ctjs::value> & a) -> ctjs::value {
		     if (!a.empty()) { pending_shot = a[0].to_string(); }
		     return {};
	     },
	     "screenshot")},
	    {"setFullscreen", ctjs::native([&want_fullscreen, &fullscreen_dirty](
	                                       const std::vector<ctjs::value> & a) -> ctjs::value {
		     want_fullscreen = !a.empty() && a[0].truthy();
		     fullscreen_dirty = true;
		     return {};
	     },
	     "setFullscreen")},
	}, image_decoder, opts.assets};
	mixer.embedded = &e.assets;

	// the anchor default action: clicking <a href> opens the system's web
	// browser at that URL (fragment links never reach this hook)
	e.open_url = [](std::string_view url) { SDL_OpenURL(std::string{url}.c_str()); };
	// the system clipboard behind Ctrl+C/X/V and the context menu
	e.clipboard_set = [](std::string_view text) { SDL_SetClipboardText(std::string{text}.c_str()); };
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

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("ctbrowser: SDL_Init failed: %s", SDL_GetError());
		return 1;
	}
	SDL_Window * window = SDL_CreateWindow(
	    e.title.c_str(), opts.width, opts.height,
	    SDL_WINDOW_RESIZABLE | (want_fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
	SDL_Renderer * renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
	if (window != nullptr) {
		SDL_StartTextInput(window); // editable controls receive SDL_EVENT_TEXT_INPUT
	}
	// the hover cursors (arrow / hand over links / I-beam over text),
	// switched per frame from the engine's CSS-resolved cursor kind
	SDL_Cursor * cur_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
	SDL_Cursor * cur_pointer = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
	SDL_Cursor * cur_text = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
	auto shown_cursor = decltype(e.cursor()){};
	if (cur_arrow != nullptr) { SDL_SetCursor(cur_arrow); }
	if (renderer == nullptr) {
		SDL_Log("ctbrowser: window/renderer failed: %s", SDL_GetError());
		SDL_Quit();
		return 1;
	}
	if (opts.logical_w > 0 && opts.logical_h > 0) {
		SDL_SetRenderLogicalPresentation(renderer, opts.logical_w, opts.logical_h,
		                                 SDL_LOGICAL_PRESENTATION_LETTERBOX);
	}
	// vsync where the driver honors it; the explicit pacing below covers
	// the rest (dummy driver, disabled compositors, >60 Hz displays)
	SDL_SetRenderVSync(renderer, 1);


	{ // scope: GPU/font resources release before the SDL teardown below
#ifdef CTBROWSER_WITH_TTF
	detail::ttf_text ttf;
	ttf.renderer = renderer;
	if (TTF_Init()) {
		// 1) the embedded default faces (fonts.hpp): serif/sans/mono in
		// four styles each, registered under the generic family names
		{
			const auto reg = [&](const char * key, const char * fam, bool b, bool i) {
				if (const embedded_asset * a = find_asset(&e.assets, key)) {
					ttf.register_face(fam, b, i, {a->data, a->size, {}});
				}
			};
			reg("ctbrowser:font/serif-regular", "serif", false, false);
			reg("ctbrowser:font/serif-bold", "serif", true, false);
			reg("ctbrowser:font/serif-italic", "serif", false, true);
			reg("ctbrowser:font/serif-bolditalic", "serif", true, true);
			reg("ctbrowser:font/sans-regular", "sans-serif", false, false);
			reg("ctbrowser:font/sans-bold", "sans-serif", true, false);
			reg("ctbrowser:font/sans-italic", "sans-serif", false, true);
			reg("ctbrowser:font/sans-bolditalic", "sans-serif", true, true);
			reg("ctbrowser:font/mono-regular", "monospace", false, false);
			reg("ctbrowser:font/mono-bold", "monospace", true, false);
			reg("ctbrowser:font/mono-italic", "monospace", false, true);
			reg("ctbrowser:font/mono-bolditalic", "monospace", true, true);
		}
		// 2) every page @font-face: family + src (embedded copy preferred,
		// else resolved like any asset; a public-root "/x" also tried
		// repo-relative) + optional font-weight/font-style descriptors -
		// a page can declare MANY families and variants, all live at once
		for (const auto & ff : e.font_faces()) {
			const std::string fam{ff.get("font-family")};
			if (fam.empty()) { continue; }
			std::string fam_clean = fam;
			while (!fam_clean.empty() && (fam_clean.front() == '"' || fam_clean.front() == '\'')) { fam_clean.erase(fam_clean.begin()); }
			while (!fam_clean.empty() && (fam_clean.back() == '"' || fam_clean.back() == '\'')) { fam_clean.pop_back(); }
			std::string src{ff.get("src")};
			const std::size_t up = src.find("url(");
			if (up == std::string::npos) { continue; }
			const std::size_t s = up + 4, en = src.find(')', s);
			if (en == std::string::npos) { continue; }
			std::string path = src.substr(s, en - s);
			while (!path.empty() && (path.front() == ' ' || path.front() == '"' || path.front() == '\'')) { path.erase(path.begin()); }
			while (!path.empty() && (path.back() == ' ' || path.back() == '"' || path.back() == '\'')) { path.pop_back(); }
			const std::string w{ff.get("font-weight")};
			const std::string st{ff.get("font-style")};
			const bool fb = w.find("bold") != std::string::npos || w.find("700") != std::string::npos ||
			                w.find("800") != std::string::npos || w.find("900") != std::string::npos;
			const bool fi = st.find("italic") != std::string::npos || st.find("oblique") != std::string::npos;
			if (const embedded_asset * emb = find_asset(&e.assets, path)) {
				ttf.register_face(fam_clean, fb, fi, {emb->data, emb->size, {}});
				continue;
			}
			std::string r = detail::resolve_asset(path);
			if (r.empty() && path.size() > 1 && path[0] == '/') { r = detail::resolve_asset(path.substr(1)); }
			if (!r.empty()) { ttf.register_face(fam_clean, fb, fi, {nullptr, 0, r}); }
		}
		// 3) the last resort: an explicit font path, else a system font
		ttf.fallback_path = !opts.font_path.empty() ? opts.font_path : detail::probe_font();
		if (ttf.ok()) {
			e.measure = [&ttf](std::u32string_view text, std::int32_t px, std::string_view family,
			                   bool bold, bool italic) {
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
			std::int32_t vw = opts.width, vh = opts.height;
			if (opts.logical_w > 0) { vw = opts.logical_w; vh = opts.logical_h; }
			else { SDL_GetWindowSize(window, &vw, &vh); }
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

		std::int32_t view_w = opts.width;
		std::int32_t view_h = opts.height;
		if (opts.logical_w > 0) {
			view_w = opts.logical_w;
			view_h = opts.logical_h;
		} else {
			SDL_GetWindowSize(window, &view_w, &view_h);
		}
		const std::vector<paint_cmd> paints = e.frame(view_w);

		if (opts.clear_white) {
			SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
		} else {
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		}
		SDL_RenderClear(renderer);

		for (const paint_cmd & cmd : paints) {
			switch (cmd.what) {
				case paint_cmd::kind::box: {
					SDL_SetRenderDrawColor(renderer,
					                       static_cast<Uint8>((cmd.argb >> 16) & 0xFF),
					                       static_cast<Uint8>((cmd.argb >> 8) & 0xFF),
					                       static_cast<Uint8>(cmd.argb & 0xFF),
					                       static_cast<Uint8>((cmd.argb >> 24) & 0xFF));
					const SDL_FRect r{static_cast<float>(cmd.x), static_cast<float>(cmd.y),
					                  static_cast<float>(cmd.w), static_cast<float>(cmd.h)};
					SDL_RenderFillRect(renderer, &r);
					break;
				}
				case paint_cmd::kind::text:
#ifdef CTBROWSER_WITH_TTF
					if (ttf.ok()) {
						ttf.draw(cmd);
						break;
					}
#endif
					detail::draw_text(renderer, cmd);
					break;
				case paint_cmd::kind::canvas: {
					SDL_Texture * t = textures.of(cmd.canvas_node);
					SDL_UpdateTexture(t, nullptr, cmd.canvas_node->pixels.data(),
					                  cmd.canvas_node->canvas_w * 4);
					const SDL_FRect dst{static_cast<float>(cmd.x), static_cast<float>(cmd.y),
					                    static_cast<float>(cmd.w), static_cast<float>(cmd.h)};
					SDL_RenderTexture(renderer, t, nullptr, &dst);
					break;
				}
			}
		}
		in_render = false;
	};

	// live window resize: while the user drags a window edge the OS runs a
	// modal loop that blocks our while(); an SDL event watch still fires
	// there, so we render + present from it to track the drag smoothly.
	struct resize_watch { std::function<void()> * render; SDL_Renderer * renderer; };
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
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			switch (ev.type) {
				case SDL_EVENT_QUIT:
					running = false;
					break;
				case SDL_EVENT_MOUSE_MOTION:
					SDL_ConvertEventToRenderCoordinates(renderer, &ev);
					e.mouse_move(ev.motion.x, ev.motion.y);
					break;
				case SDL_EVENT_MOUSE_BUTTON_DOWN:
				case SDL_EVENT_MOUSE_BUTTON_UP:
					SDL_ConvertEventToRenderCoordinates(renderer, &ev);
					e.mouse_button(ev.button.x, ev.button.y,
					               ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN,
					               ev.button.button == SDL_BUTTON_RIGHT    ? 2
					               : ev.button.button == SDL_BUTTON_MIDDLE ? 1
					                                                       : 0);
					break;
				case SDL_EVENT_KEY_DOWN:
				case SDL_EVENT_KEY_UP:
					if (!ev.key.repeat || ev.type == SDL_EVENT_KEY_DOWN) {
						// repeats replay the editing default (held Backspace)
						e.key(SDL_GetKeyName(ev.key.key), ev.type == SDL_EVENT_KEY_DOWN);
					}
					break;
				case SDL_EVENT_TEXT_INPUT:
					e.text_input(ev.text.text);
					break;
				case SDL_EVENT_MOUSE_WHEEL:
					SDL_ConvertEventToRenderCoordinates(renderer, &ev);
					e.wheel(ev.wheel.mouse_x, ev.wheel.mouse_y, ev.wheel.y);
					break;
				default:
					break;
			}
		}

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

		if (opts.max_frames > 0 && ++frame >= opts.max_frames) { running = false; }
		else if (opts.max_frames == 0) { ++frame; }
	}
	SDL_RemoveEventWatch(watch_cb, &rw);
	} // resource scope

#ifdef CTBROWSER_WITH_TTF
	TTF_Quit();
#endif
	SDL_DestroyCursor(cur_arrow);
	SDL_DestroyCursor(cur_pointer);
	SDL_DestroyCursor(cur_text);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}

} // namespace ctbrowser

#endif
