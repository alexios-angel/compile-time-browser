module;
#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

export module ctbrowser.app;

import ctbrowser.core;
import ctbrowser.raster;
import ctbrowser.shell;

// The SDL3 window and event loop.
//
// This is the ONLY part of the engine that knows SDL exists, and it is a
// translator: SDL events become shell::input_event, and the composited surface
// becomes a texture. Everything upstream of it renders with no display at all,
// which is what lets the whole browser be tested headlessly.
//
// The renderer is chosen by the caller - ctbrowser.gpu's create_renderer() where
// SDL3 is present, software otherwise - so this file has no opinion about which
// backend is in use, and a machine with no GPU takes the same path as one with.

export namespace ctbrowser::app {

using ctbrowser::shell::browser;
using ctbrowser::shell::input_event;

struct window_options {
	std::string title = "ctbrowser";
	int width = 1024;
	int height = 768;
	bool resizable = true;
};

// SDL handles as RAII. SDL_Quit is not called here: a process may open more
// than one window, and whoever called SDL_Init owns the teardown.
struct sdl_deleter {
	void operator()(SDL_Window * w) const noexcept { SDL_DestroyWindow(w); }
};
using window_ptr = std::unique_ptr<SDL_Window, sdl_deleter>;

// SDL key names to the DOM-ish names the browser understands. Only the keys
// that do something today - a table of every key mapping to nothing would be a
// list of unimplemented features wearing a lookup table.
[[nodiscard]] inline std::string dom_key_name(SDL_Keycode key) {
	switch (key) {
	case SDLK_DOWN: return "Down";
	case SDLK_UP: return "Up";
	case SDLK_PAGEDOWN: return "PageDown";
	case SDLK_PAGEUP: return "PageUp";
	case SDLK_HOME: return "Home";
	case SDLK_END: return "End";
	case SDLK_SPACE: return "Space";
	default: return {};
	}
}

// Translate one SDL event. Returns false for events the browser has no use for,
// so the caller can tell "nothing happened" from "something did".
[[nodiscard]] inline bool translate(const SDL_Event & event, input_event & out) {
	switch (event.type) {
	case SDL_EVENT_MOUSE_MOTION:
		out = input_event::mouse_move_to(event.motion.x, event.motion.y);
		return true;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		out = input_event::mouse_down_at(event.button.x, event.button.y,
		                                 static_cast<std::uint8_t>(event.button.button));
		return true;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		out = input_event::mouse_up_at(event.button.x, event.button.y,
		                               static_cast<std::uint8_t>(event.button.button));
		return true;
	case SDL_EVENT_MOUSE_WHEEL: out = input_event::wheel_by(event.wheel.y); return true;
	case SDL_EVENT_KEY_DOWN: {
		std::string name = dom_key_name(event.key.key);
		if (name.empty()) { return false; }
		out = input_event::key_press(std::move(name));
		return true;
	}
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		out = input_event::resized(event.window.data1, event.window.data2);
		return true;
	default: return false;
	}
}

[[nodiscard]] inline window_ptr create_window(const window_options & options) {
	SDL_WindowFlags flags = 0;
	if (options.resizable) { flags |= SDL_WINDOW_RESIZABLE; }
	return window_ptr{
	    SDL_CreateWindow(options.title.c_str(), options.width, options.height, flags)};
}

// Put a composited image on screen through SDL's 2D renderer.
//
// Used by the SOFTWARE path, where the engine hands back a pixel buffer. The
// GPU path does not come through here at all: its backend owns the swapchain
// and has already presented. Blitting GPU output back through SDL_Renderer
// would mean downloading pixels the GPU just drew, which is the exact copy the
// hardware path exists to avoid.
struct present_target {
	SDL_Renderer * renderer = nullptr;
	SDL_Texture * texture = nullptr;
	int width = 0;
	int height = 0;
};

[[nodiscard]] inline bool present_software(present_target & target,
                                           const ctbrowser::raster::surface & image) {
	if (target.renderer == nullptr || image.empty()) { return false; }
	if (target.texture == nullptr || target.width != image.width() ||
	    target.height != image.height()) {
		if (target.texture != nullptr) { SDL_DestroyTexture(target.texture); }
		target.texture = SDL_CreateTexture(target.renderer, SDL_PIXELFORMAT_ARGB8888,
		                                   SDL_TEXTUREACCESS_STREAMING, image.width(),
		                                   image.height());
		target.width = image.width();
		target.height = image.height();
	}
	if (target.texture == nullptr) { return false; }
	SDL_UpdateTexture(target.texture, nullptr, image.pixels().data(),
	                  static_cast<int>(image.stride() * sizeof(std::uint32_t)));
	SDL_RenderClear(target.renderer);
	SDL_RenderTexture(target.renderer, target.texture, nullptr, nullptr);
	SDL_RenderPresent(target.renderer);
	return true;
}

} // namespace ctbrowser::app
