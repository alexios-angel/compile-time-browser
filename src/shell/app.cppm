module;
#if CTBROWSER_WITH_SDL3
#include <SDL3/SDL.h>
#if CTBROWSER_WITH_IMAGE
#include <SDL3_image/SDL_image.h>
#endif
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

export module ctbrowser.app;

import ctbrowser.core;
import ctbrowser.paint;
import ctbrowser.raster;
import ctbrowser.script;
import ctbrowser.shell;

// The application shell: a window, an event loop, and one function to call.
//
// NO SDL TYPE APPEARS IN THIS INTERFACE. That is the point of the file. SDL sits
// behind `#if CTBROWSER_WITH_SDL3` in the global module fragment, so the module
// BUILDS EITHER WAY - with a window when SDL3 was found, headless when it was
// not. An application that wants to talk to SDL still can, through
// `app_options::on_native_window`, but nothing forces it to and nothing about
// the build requires it.
//
// The previous version of this file was a translator, not a shell: it exported
// SDL_Window*, SDL_Event and SDL_Renderer*, and left SDL_Init, the poll loop,
// the renderer object, frame pacing and quit handling to the caller. The
// reference consumer wrote a dozen SDL calls by hand to open one page. v1's
// `run_app<page>(opts)` was better than that, and this is v1's shape without
// the compile-time machinery.

export namespace ctbrowser {

// A file the page can reach by name - what an asset lookup finds before it
// touches the filesystem.
struct asset {
	std::string name;
	std::vector<std::byte> bytes;
};

enum class renderer_preference : std::uint8_t { automatic, prefer_gpu, force_software };

struct app_options {
	std::string title = "ctbrowser";
	int width = 1024;
	int height = 768;

	// >0: render at this fixed resolution and letterbox it into the window.
	// Fixed-resolution pages (games) want it; documents do not.
	int logical_width = 0;
	int logical_height = 0;

	// >0: stop after this many frames. This is how a page becomes a CI test,
	// and it forces a fixed timestep so the run is reproducible.
	int max_frames = 0;
	int max_fps = 60;    // 0 = uncapped. Fixed-step pages depend on the cap.
	double fixed_dt = 0; // >0: pretend every frame took exactly this long

	bool fullscreen = false;

	std::string screenshot_path; // "" = never
	int screenshot_frame = -1;   // -1 = the last frame

	std::vector<asset> assets;
	// Where a relative asset path (an <img src>, a page-local fetch) resolves
	// from when the registry misses. Empty means the working directory.
	std::filesystem::path asset_path;
	// Whether fetch() may open a socket for a url the registry does not have.
	// On by default - it is a browser - and CTBROWSER_NETWORK=0 turns it off,
	// which is what makes an example's ctest hermetic.
	bool network = true;
	renderer_preference renderer = renderer_preference::automatic;

	// THE ESCAPE HATCH. Called once with the native window handle - an
	// SDL_Window* - for callers who want to drive SDL themselves. Null on the
	// headless backend. Nothing else here mentions SDL, and a caller who does
	// not set this never learns it exists.
	std::function<void(void *)> on_native_window;

	// Called once before the first frame with the live browser, for
	// applications that want to inspect the document or drive it themselves.
	std::function<void(shell::browser &)> on_ready;
};

// Environment overrides, applied by run_app before anything else:
//
//   CTBROWSER_TEST_FRAMES  -> max_frames (and therefore a fixed timestep)
//   CTBROWSER_SCREENSHOT   -> screenshot_path
//   CTBROWSER_RENDERER     -> software | gpu
//   CTBROWSER_NETWORK      -> 0 disables fetch()'s network access
//
// Carried over from v1 because it is what lets an example BE a ctest without
// the example containing any test scaffolding.
inline void apply_environment(app_options & options) {
	if (const char * frames = std::getenv("CTBROWSER_TEST_FRAMES")) {
		options.max_frames = std::atoi(frames);
	}
	if (const char * shot = std::getenv("CTBROWSER_SCREENSHOT")) { options.screenshot_path = shot; }
	if (const char * want = std::getenv("CTBROWSER_RENDERER")) {
		const std::string_view text{want};
		if (text == "software" || text == "cpu") {
			options.renderer = renderer_preference::force_software;
		} else if (text == "gpu" || text == "hardware") {
			options.renderer = renderer_preference::prefer_gpu;
		}
	}
	if (const char * network = std::getenv("CTBROWSER_NETWORK")) {
		const std::string_view text{network};
		options.network = !(text == "0" || text == "off" || text == "no");
	}
	// A bounded run has to be reproducible, or comparing its screenshot is a
	// coin flip.
	if (options.max_frames > 0 && options.fixed_dt <= 0) { options.fixed_dt = 1.0 / 60.0; }
}

// Write a composited image as a binary PPM. Deliberately not PNG: PPM needs no
// encoder, and a golden a test byte-compares gains nothing from compression.
[[nodiscard]] inline bool write_ppm(const std::filesystem::path & path,
                                    const raster::surface & image) {
	std::ofstream out{path, std::ios::binary};
	if (!out) { return false; }
	out << "P6\n" << image.width() << " " << image.height() << "\n255\n";
	for (int y = 0; y < image.height(); ++y) {
		for (int x = 0; x < image.width(); ++x) {
			const std::uint32_t p = image.row(y)[static_cast<std::size_t>(x)];
			const char rgb[3] = {static_cast<char>((p >> 16) & 0xFFu),
			                     static_cast<char>((p >> 8) & 0xFFu),
			                     static_cast<char>(p & 0xFFu)};
			out.write(rgb, 3);
		}
	}
	return out.good();
}

} // namespace ctbrowser

// --- the backends, private to this module ---------------------------------

namespace ctbrowser::detail {

using ctbrowser::shell::browser;
using ctbrowser::shell::input_event;

// What a host has to do. Two implementations - one that opens a window and one
// that does not - chosen at RUNTIME, which is why an application never has to
// care whether SDL3 was there when the engine was built.
class host {
public:
	virtual ~host() = default;
	[[nodiscard]] virtual bool start(const app_options &) = 0;
	// Drain input into the browser. False means the user asked to quit.
	[[nodiscard]] virtual bool pump(browser &, bool & changed) = 0;
	virtual void present(browser &) = 0;
	[[nodiscard]] virtual void * native_window() { return nullptr; }
};

// No display at all. Always available, and the ONLY backend in a build without
// SDL3 - so a headless render, a CI run and an SDL-less machine are one code
// path rather than three special cases.
class headless_host final : public host {
public:
	[[nodiscard]] bool start(const app_options &) override { return true; }
	[[nodiscard]] bool pump(browser &, bool &) override { return true; }
	void present(browser &) override {}
};

#if CTBROWSER_WITH_SDL3

struct window_deleter {
	void operator()(SDL_Window * w) const noexcept { SDL_DestroyWindow(w); }
};

// SDL keys to the DOM-ish names the browser understands.
//
// The old table had seven entries and omitted every editing key, so typing into
// an <input> was impossible through this layer even though the forms stack
// supported it completely. The text itself arrives via SDL_EVENT_TEXT_INPUT,
// not from key codes - that is the only path that gets IME, dead keys and
// non-Latin layouts right.
[[nodiscard]] inline std::string dom_key_name(SDL_Keycode key, SDL_Keymod mod) {
	if ((mod & SDL_KMOD_CTRL) != 0 && key == SDLK_A) { return "SelectAll"; }
	switch (key) {
	case SDLK_LEFT: return "Left";
	case SDLK_RIGHT: return "Right";
	case SDLK_DOWN: return "Down";
	case SDLK_UP: return "Up";
	case SDLK_PAGEDOWN: return "PageDown";
	case SDLK_PAGEUP: return "PageUp";
	case SDLK_HOME: return "Home";
	case SDLK_END: return "End";
	case SDLK_SPACE: return "Space";
	case SDLK_BACKSPACE: return "Backspace";
	case SDLK_DELETE: return "Delete";
	case SDLK_RETURN:
	case SDLK_KP_ENTER: return "Return";
	case SDLK_TAB: return "Tab";
	case SDLK_ESCAPE: return "Escape";
	default: return {};
	}
}

class sdl_host final : public host {
public:
	~sdl_host() override {
		if (texture_ != nullptr) { SDL_DestroyTexture(texture_); }
		if (renderer_ != nullptr) { SDL_DestroyRenderer(renderer_); }
	}

	[[nodiscard]] bool start(const app_options & options) override {
		SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
		if (options.fullscreen) { flags |= SDL_WINDOW_FULLSCREEN; }
		window_.reset(SDL_CreateWindow(options.title.c_str(), options.width, options.height, flags));
		if (!window_) { return false; }

		renderer_ = SDL_CreateRenderer(window_.get(), nullptr);
		if (renderer_ == nullptr) { return false; }
		SDL_SetRenderVSync(renderer_, 1);
		if (options.logical_width > 0 && options.logical_height > 0) {
			SDL_SetRenderLogicalPresentation(renderer_, options.logical_width,
			                                 options.logical_height,
			                                 SDL_LOGICAL_PRESENTATION_LETTERBOX);
		}
		// Without this, SDL_EVENT_TEXT_INPUT never arrives and no <input> can
		// be typed into.
		SDL_StartTextInput(window_.get());
		return true;
	}

	[[nodiscard]] bool pump(browser & page, bool & changed) override {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) { return false; }
			input_event translated;
			if (translate(event, translated) && page.handle(translated)) { changed = true; }
		}
		return true;
	}

	void present(browser & page) override {
		const auto image = page.read_pixels();
		if (!image || image->empty()) { return; }
		if (texture_ == nullptr || width_ != image->width() || height_ != image->height()) {
			if (texture_ != nullptr) { SDL_DestroyTexture(texture_); }
			texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
			                             SDL_TEXTUREACCESS_STREAMING, image->width(),
			                             image->height());
			width_ = image->width();
			height_ = image->height();
		}
		if (texture_ == nullptr) { return; }
		SDL_UpdateTexture(texture_, nullptr, image->pixels().data(),
		                  static_cast<int>(image->stride() * sizeof(std::uint32_t)));
		SDL_RenderClear(renderer_);
		SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
		SDL_RenderPresent(renderer_);
	}

	[[nodiscard]] void * native_window() override { return window_.get(); }

private:
	[[nodiscard]] static bool translate(const SDL_Event & event, input_event & out) {
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
		case SDL_EVENT_TEXT_INPUT:
			// The typed text itself, which is not derivable from key codes.
			out = input_event::typed(std::string{event.text.text});
			return true;
		case SDL_EVENT_KEY_DOWN: {
			std::string name = dom_key_name(event.key.key, event.key.mod);
			if (name.empty()) { return false; }
			out = input_event::key_press(std::move(name), (event.key.mod & SDL_KMOD_SHIFT) != 0);
			return true;
		}
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			out = input_event::resized(event.window.data1, event.window.data2);
			return true;
		default: return false;
		}
	}

	std::unique_ptr<SDL_Window, window_deleter> window_;
	SDL_Renderer * renderer_ = nullptr;
	SDL_Texture * texture_ = nullptr;
	int width_ = 0;
	int height_ = 0;
};

#endif // CTBROWSER_WITH_SDL3

[[nodiscard]] inline std::unique_ptr<host> make_host(const app_options & options) {
#if CTBROWSER_WITH_SDL3
	// A bounded run that only wants a screenshot needs no window, and asking a
	// machine with no display for one is how a headless CI job fails for the
	// wrong reason.
	const bool wants_window = options.max_frames == 0 || options.screenshot_path.empty();
	if (wants_window && SDL_Init(SDL_INIT_VIDEO)) {
		auto sdl = std::make_unique<sdl_host>();
		if (sdl->start(options)) { return sdl; }
	}
#endif
	(void)options;
	return std::make_unique<headless_host>();
}

} // namespace ctbrowser::detail

namespace ctbrowser::detail {

// --- audio ----------------------------------------------------------------
//
// WAV only, mixed by SDL3 itself - no SDL3_mixer, because a game firing a blip
// on every shot needs exactly one thing: play these samples now, overlapping.
// An audio stream per sound, fed once and left to drain, is that.
//
// Sound is NOT part of the engine: `ctbrowser.shell` is deliberately SDL-free,
// so this lives here with the window and reaches the page through
// `browser::define_native`.
class audio_device {
public:
	~audio_device() { shutdown(); }
	audio_device() = default;
	audio_device(const audio_device &) = delete;
	audio_device & operator=(const audio_device &) = delete;

	// Play `name`, resolved through the same registry images use. Returns
	// whether it started - a page can tell "no sound in this build" from "that
	// file does not exist".
	bool play(shell::asset_registry & assets, const std::string & name, float volume) {
#if CTBROWSER_WITH_SDL3
		const decoded * sample = decode(assets, name);
		if (sample == nullptr) { return false; }
		if (!open(sample->spec)) { return false; }
		// Finished streams are reaped here rather than on a callback: this is
		// the only thread that touches them, so there is nothing to lock.
		reap();
		SDL_AudioStream * stream = SDL_CreateAudioStream(&sample->spec, &sample->spec);
		if (stream == nullptr) { return false; }
		SDL_SetAudioStreamGain(stream, volume);
		if (!SDL_PutAudioStreamData(stream, sample->bytes.data(),
		                            static_cast<int>(sample->bytes.size())) ||
		    !SDL_FlushAudioStream(stream) || !SDL_BindAudioStream(device_, stream)) {
			SDL_DestroyAudioStream(stream);
			return false;
		}
		playing_.push_back(stream);
		return true;
#else
		(void)assets;
		(void)name;
		(void)volume;
		return false;
#endif
	}

	void shutdown() {
#if CTBROWSER_WITH_SDL3
		for (SDL_AudioStream * stream : playing_) { SDL_DestroyAudioStream(stream); }
		playing_.clear();
		if (device_ != 0) {
			SDL_CloseAudioDevice(device_);
			device_ = 0;
		}
#endif
	}

private:
#if CTBROWSER_WITH_SDL3
	struct decoded {
		std::string name;
		SDL_AudioSpec spec{};
		std::vector<std::uint8_t> bytes;
	};

	// Decoded ONCE per name: a shot fires the same blip a hundred times and
	// re-parsing the file each time is the difference between a game and a
	// stutter.
	[[nodiscard]] const decoded * decode(shell::asset_registry & assets, const std::string & name) {
		for (const decoded & cached : samples_) {
			if (cached.name == name) { return cached.bytes.empty() ? nullptr : &cached; }
		}
		decoded sample;
		sample.name = name;
		const std::vector<std::byte> bytes = assets.load(name);
		if (!bytes.empty()) {
			SDL_IOStream * source = SDL_IOFromConstMem(bytes.data(), bytes.size());
			std::uint8_t * pcm = nullptr;
			std::uint32_t length = 0;
			if (source != nullptr && SDL_LoadWAV_IO(source, true, &sample.spec, &pcm, &length)) {
				sample.bytes.assign(pcm, pcm + length);
				SDL_free(pcm);
			}
		}
		samples_.push_back(std::move(sample));
		const decoded & stored = samples_.back();
		return stored.bytes.empty() ? nullptr : &stored;
	}

	[[nodiscard]] bool open(const SDL_AudioSpec & spec) {
		if (device_ != 0) { return true; }
		if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { return false; }
		device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
		return device_ != 0;
	}

	void reap() {
		std::erase_if(playing_, [](SDL_AudioStream * stream) {
			if (SDL_GetAudioStreamAvailable(stream) > 0) { return false; }
			SDL_DestroyAudioStream(stream);
			return true;
		});
	}

	SDL_AudioDeviceID device_ = 0;
	std::vector<SDL_AudioStream *> playing_;
	std::vector<decoded> samples_;
#endif
};

// Formats past BMP. SDL3_image is optional: without it a page still shows BMPs,
// which is what the engine decodes on its own. This is the only place in the
// tree where SDL and image decoding meet - the shell must not learn about SDL.
inline void install_image_decoder(shell::image_store & images) {
#if CTBROWSER_WITH_SDL3 && CTBROWSER_WITH_IMAGE
	images.set_decoder([](std::span<const std::byte> bytes, std::string_view) {
		paint::bitmap out;
		SDL_IOStream * source = SDL_IOFromConstMem(bytes.data(), bytes.size());
		if (source == nullptr) { return out; }
		SDL_Surface * decoded = IMG_Load_IO(source, true);
		if (decoded == nullptr) { return out; }
		SDL_Surface * argb = SDL_ConvertSurface(decoded, SDL_PIXELFORMAT_ARGB8888);
		SDL_DestroySurface(decoded);
		if (argb == nullptr) { return out; }
		out = paint::bitmap{argb->w, argb->h};
		for (int y = 0; y < argb->h; ++y) {
			const auto * row = reinterpret_cast<const std::uint32_t *>(
			    static_cast<const std::byte *>(argb->pixels) +
			    static_cast<std::size_t>(y) * static_cast<std::size_t>(argb->pitch));
			for (int x = 0; x < argb->w; ++x) { out.put(x, y, row[x]); }
		}
		SDL_DestroySurface(argb);
		return out;
	});
#else
	(void)images;
#endif
}

} // namespace ctbrowser::detail

export namespace ctbrowser {

// Run a page. Returns a process exit code.
//
// This is the whole application API: it owns the window, the event loop, the
// clock, the frame pacing and the teardown.
[[nodiscard]] inline int run_app(std::string_view html, app_options options = {}) {
	apply_environment(options);

	std::unique_ptr<detail::host> host = detail::make_host(options);
	if (options.on_native_window) { options.on_native_window(host->native_window()); }

	shell::browser_options browser_options;
	browser_options.width = options.logical_width > 0 ? options.logical_width : options.width;
	browser_options.height = options.logical_height > 0 ? options.logical_height : options.height;
	shell::browser page{browser_options};

	// Resources BEFORE the page: an <img src> is resolved while the document
	// loads, so a registry seeded afterwards would be seeded too late.
	for (const asset & item : options.assets) { page.assets().add(item.name, item.bytes); }
	page.assets().set_base_path(options.asset_path);
	page.allow_network(options.network);
	detail::install_image_decoder(page.images());

	// Sound. `playSound(name [, volume])` is what the page calls; the HTML
	// <audio> element is not implemented, and a native the embedder installs is
	// the honest way to say so rather than a half-built element that looks like
	// the real thing.
	detail::audio_device audio;
	page.define_native("playSound", [&audio, &page](script::context & cx,
	                                                std::span<script::value> args) {
		const std::string name = args.empty() ? std::string{} : cx.to_string(args[0]);
		const float volume =
		    args.size() > 1 ? static_cast<float>(script::context::to_number(args[1])) : 1.0F;
		return script::value::boolean(audio.play(page.assets(), name, volume));
	});

	page.load_html(html);
	if (options.on_ready) { options.on_ready(page); }

	scheduler pool;
	using clock = std::chrono::steady_clock;
	auto previous = clock::now();
	int frame = 0;
	bool needs_frame = true;
	bool running = true;

	while (running) {
		if (!host->pump(page, needs_frame)) { break; }

		const auto now = clock::now();
		const double elapsed_ms =
		    options.fixed_dt > 0
		        ? options.fixed_dt * 1000.0
		        : std::chrono::duration<double, std::milli>(now - previous).count();
		previous = now;

		// THE CLOCK. Timers and requestAnimationFrame fire from here. The
		// previous reference loop never called it, so every animated page was
		// frozen and nothing said so.
		if (page.tick(elapsed_ms) > 0) { needs_frame = true; }

		if (needs_frame) {
			if (!page.frame(&pool)) { break; }
			host->present(page);
			needs_frame = false;
		}

		++frame;
		const bool last = options.max_frames > 0 && frame >= options.max_frames;
		if (!options.screenshot_path.empty() &&
		    (frame - 1 == options.screenshot_frame || (options.screenshot_frame < 0 && last))) {
			if (const auto image = page.read_pixels()) {
				(void)write_ppm(options.screenshot_path, *image);
			}
		}
		if (last) { running = false; }

		if (options.max_fps > 0 && options.max_frames == 0) {
			// Pace only interactive runs. A bounded run sprints: nobody is
			// watching it, and a 30-frame test should not take half a second of
			// wall clock to say so.
			const auto budget = std::chrono::nanoseconds{1'000'000'000 / options.max_fps};
			const auto spent = clock::now() - now;
			if (spent < budget) { std::this_thread::sleep_for(budget - spent); }
		}
	}

#if CTBROWSER_WITH_SDL3
	host.reset(); // the window and the renderer go before SDL_Quit
	if (SDL_WasInit(SDL_INIT_VIDEO) != 0) { SDL_Quit(); }
#endif
	return 0;
}

// A page loaded from a file resolves its images and page-local fetches next to
// ITSELF, which is what a `<img src="cat.bmp">` beside the html means.
[[nodiscard]] inline int run_app_file(const std::filesystem::path & path,
                                      app_options options = {}) {
	std::ifstream in{path, std::ios::binary};
	if (!in) {
		std::printf("ctbrowser: cannot read %s\n", path.string().c_str());
		return 1;
	}
	std::ostringstream buffer;
	buffer << in.rdbuf();
	if (options.title == "ctbrowser") { options.title = path.filename().string(); }
	if (options.asset_path.empty()) { options.asset_path = path.parent_path(); }
	return run_app(buffer.str(), std::move(options));
}

} // namespace ctbrowser
