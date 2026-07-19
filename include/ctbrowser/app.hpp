#ifndef CTBROWSER__APP__HPP
#define CTBROWSER__APP__HPP

#include "engine.hpp"
#include "font8x8.hpp"
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
	int width = 800;
	int height = 600;
	int max_frames = 0;          // 0 = run until quit; >0 = auto-exit (tests/CI)
	double fixed_dt = 0;         // 0 = real time; >0 = deterministic timestep
	int max_fps = 60;            // interactive frame cap (0 = uncapped); browsers
	                             // throttle requestAnimationFrame the same way -
	                             // fixed-step pages (examples/pong.html) depend on it
	int logical_w = 0;           // >0: fixed-resolution presentation,
	int logical_h = 0;           //     letterboxed and scaled to the window
	bool fullscreen = false;
	bool clear_white = true;     // page background
	std::string screenshot_path; // capture to PNG...
	int screenshot_frame = -1;   // ...at this frame (-1 = the last one)
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
	for (const char c : cmd.text) {
		for (int row = 0; row < 8; ++row) {
			for (int col = 0; col < 8; ++col) {
				if (!glyph_pixel(c, row, col)) { continue; }
				const SDL_FRect px{pen_x + static_cast<float>(col) * scale,
				                   pen_y + static_cast<float>(row) * scale, scale, scale};
				SDL_RenderFillRect(r, &px);
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
	std::string path;
	std::map<int, TTF_Font *> fonts;
	std::map<std::pair<std::string, int>, SDL_Texture *> cache;

	bool ok() const { return !path.empty(); }

	TTF_Font * font(int px) {
		if (const auto it = fonts.find(px); it != fonts.end()) { return it->second; }
		TTF_Font * f = TTF_OpenFont(path.c_str(), static_cast<float>(px));
		fonts.emplace(px, f);
		return f;
	}
	int measure(std::string_view text, int px) {
		TTF_Font * f = font(px);
		if (f == nullptr) { return static_cast<int>(text.size()) * px; }
		int w = 0, h = 0;
		TTF_GetStringSize(f, text.data(), text.size(), &w, &h);
		return w;
	}
	void draw(const paint_cmd & cmd) {
		TTF_Font * f = font(cmd.font_px);
		if (f == nullptr) { return; }
		SDL_Texture * t = nullptr;
		const std::pair<std::string, int> key{cmd.text, cmd.font_px};
		if (const auto it = cache.find(key); it != cache.end()) {
			t = it->second;
		} else {
			if (cache.size() > 256) { // texts change rarely; cap the cache
				for (auto & [k, tex] : cache) { SDL_DestroyTexture(tex); }
				cache.clear();
			}
			SDL_Surface * s = TTF_RenderText_Blended(f, cmd.text.c_str(), cmd.text.size(),
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
		for (auto & [px, f] : fonts) {
			if (f != nullptr) { TTF_CloseFont(f); }
		}
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
	std::map<const node *, SDL_Texture *> cache;

	SDL_Texture * of(node * n) {
		if (const auto it = cache.find(n); it != cache.end()) { return it->second; }
		SDL_Texture * t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
		                                    SDL_TEXTUREACCESS_STREAMING, n->canvas_w,
		                                    n->canvas_h);
		SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
		SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND); // clearRect shows the page
		cache.emplace(n, t);
		return t;
	}
	~canvas_textures() {
		for (auto & [n, t] : cache) { SDL_DestroyTexture(t); }
	}
};

} // namespace detail

// run a page as a windowed application; returns the process exit code
template <typename Page> int run_app(app_options opts = {}) {
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
		out.pixels.resize(static_cast<size_t>(argb->w) * static_cast<size_t>(argb->h));
		for (int y = 0; y < argb->h; ++y) {
			const uint32_t * row = reinterpret_cast<const uint32_t *>(
			    static_cast<const unsigned char *>(argb->pixels) +
			    static_cast<size_t>(y) * static_cast<size_t>(argb->pitch));
			for (int x = 0; x < argb->w; ++x) {
				out.pixels[static_cast<size_t>(y) * static_cast<size_t>(argb->w) +
				           static_cast<size_t>(x)] = row[x];
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

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("ctbrowser: SDL_Init failed: %s", SDL_GetError());
		return 1;
	}
	SDL_Window * window = SDL_CreateWindow(
	    e.title.c_str(), opts.width, opts.height,
	    SDL_WINDOW_RESIZABLE | (want_fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
	SDL_Renderer * renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
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
	detail::ttf_text ttf{renderer, {}, {}, {}};
	if (TTF_Init()) {
		ttf.path = opts.font_path.empty() ? detail::probe_font() : opts.font_path;
		if (ttf.ok()) {
			e.measure = [&ttf](std::string_view text, int px) {
				return ttf.measure(text, px);
			};
		}
	}
#endif

	detail::canvas_textures textures{renderer, {}};
	std::string shown_title = e.title;
	Uint64 last = SDL_GetTicks();
	Uint64 frame_start_ns = SDL_GetTicksNS();
	int frame = 0;
	bool running = true;
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
					               ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
					break;
				case SDL_EVENT_KEY_DOWN:
				case SDL_EVENT_KEY_UP:
					if (!ev.key.repeat) {
						e.key(SDL_GetKeyName(ev.key.key), ev.type == SDL_EVENT_KEY_DOWN);
					}
					break;
				default:
					break;
			}
		}

		if (fullscreen_dirty) {
			fullscreen_dirty = false;
			SDL_SetWindowFullscreen(window, want_fullscreen);
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

		int view_w = opts.width;
		int view_h = opts.height;
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
	} // resource scope

#ifdef CTBROWSER_WITH_TTF
	TTF_Quit();
#endif
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}

} // namespace ctbrowser

#endif
