#ifndef CTBROWSER__APP__HPP
#define CTBROWSER__APP__HPP

#include "engine.hpp"
#include "font8x8.hpp"
#include <SDL3/SDL.h>
#ifndef CTBROWSER_IN_A_MODULE
#include <map>
#include <string>
#endif

// The SDL3 shell: a window, a renderer, an event loop. Boxes render as
// filled rects, text as the embedded 8x8 font scaled to the computed
// font-size, and every <canvas> streams its pixel buffer into an SDL
// texture. Mouse clicks hit-test the layout and reach the script's
// onClick(id); keys reach onKey(name, down); every frame calls
// onFrame(dt). SDL3 carries this everywhere it runs - Windows, macOS,
// Linux, the BSDs - and `SDL_VIDEODRIVER=dummy` runs it headless
// (paired with max_frames, that is how CI drives it).

namespace ctbrowser {

struct app_options {
	int width = 800;
	int height = 600;
	int max_frames = 0;      // 0 = run until quit; >0 = auto-exit (tests/CI)
	bool clear_white = true; // page background
};

namespace detail {

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

struct canvas_textures {
	SDL_Renderer * renderer = nullptr;
	std::map<const node *, SDL_Texture *> cache;

	SDL_Texture * of(node * n) {
		if (const auto it = cache.find(n); it != cache.end()) { return it->second; }
		SDL_Texture * t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
		                                    SDL_TEXTUREACCESS_STREAMING, n->canvas_w,
		                                    n->canvas_h);
		SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
		cache.emplace(n, t);
		return t;
	}
	~canvas_textures() {
		for (auto & [n, t] : cache) { SDL_DestroyTexture(t); }
	}
};

} // namespace detail

// run a page as a windowed application; returns the process exit code.
// CTBROWSER_TEST_FRAMES=N in the environment bounds the run (CI uses
// it with SDL_VIDEODRIVER=dummy to drive examples headless).
template <typename Page> int run_app(app_options opts = {}) {
	if (opts.max_frames == 0) {
		if (const char * env = SDL_getenv("CTBROWSER_TEST_FRAMES")) {
			opts.max_frames = SDL_atoi(env);
		}
	}
	engine<Page> e;

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("ctbrowser: SDL_Init failed: %s", SDL_GetError());
		return 1;
	}
	SDL_Window * window =
	    SDL_CreateWindow(e.title.c_str(), opts.width, opts.height, SDL_WINDOW_RESIZABLE);
	SDL_Renderer * renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
	if (renderer == nullptr) {
		SDL_Log("ctbrowser: window/renderer failed: %s", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	detail::canvas_textures textures{renderer, {}};
	std::string shown_title = e.title;
	Uint64 last = SDL_GetTicks();
	int frames = 0;
	bool running = true;
	while (running) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			switch (ev.type) {
				case SDL_EVENT_QUIT:
					running = false;
					break;
				case SDL_EVENT_MOUSE_BUTTON_DOWN:
					e.click_at(static_cast<int>(ev.button.x), static_cast<int>(ev.button.y));
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

		const Uint64 now = SDL_GetTicks();
		const double dt = static_cast<double>(now - last) / 1000.0;
		last = now;
		e.tick(dt);

		if (e.title != shown_title) {
			shown_title = e.title;
			SDL_SetWindowTitle(window, shown_title.c_str());
		}

		int win_w = opts.width;
		int win_h = opts.height;
		SDL_GetWindowSize(window, &win_w, &win_h);
		const std::vector<paint_cmd> paints = e.frame(win_w);

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
		SDL_RenderPresent(renderer);

		if (opts.max_frames > 0 && ++frames >= opts.max_frames) { running = false; }
	}

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}

} // namespace ctbrowser

#endif
