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

module ctbrowser.app;

// The window, the event loop and the only place SDL is read. See the note in
// app.cppm about why its headers are not in the interface.


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

// An SDL scancode as the DOM `code` of the physical key.
//
// SCANCODE, not keycode: `code` is defined as the key's position, so the key
// left of Z is "KeyZ" on QWERTY and on AZERTY alike. The keycode would give
// "KeyW" on AZERTY, which is what `key` is for and this is not.
//
// The table this replaces had FIFTEEN entries and no letters or digits at all,
// so a page bound to WASD received nothing - translate() returned false and the
// event was dropped before the browser ever saw it.
[[nodiscard]] inline std::string dom_key_code(SDL_Scancode code) {
	if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) {
		return std::string{"Key"} + static_cast<char>('A' + (code - SDL_SCANCODE_A));
	}
	if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) {
		return std::string{"Digit"} + static_cast<char>('1' + (code - SDL_SCANCODE_1));
	}
	if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12) {
		return std::string{"F"} + std::to_string(1 + (code - SDL_SCANCODE_F1));
	}
	switch (code) {
	case SDL_SCANCODE_0: return "Digit0";
	case SDL_SCANCODE_LEFT: return "ArrowLeft";
	case SDL_SCANCODE_RIGHT: return "ArrowRight";
	case SDL_SCANCODE_DOWN: return "ArrowDown";
	case SDL_SCANCODE_UP: return "ArrowUp";
	case SDL_SCANCODE_PAGEDOWN: return "PageDown";
	case SDL_SCANCODE_PAGEUP: return "PageUp";
	case SDL_SCANCODE_HOME: return "Home";
	case SDL_SCANCODE_END: return "End";
	case SDL_SCANCODE_SPACE: return "Space";
	case SDL_SCANCODE_BACKSPACE: return "Backspace";
	case SDL_SCANCODE_DELETE: return "Delete";
	case SDL_SCANCODE_RETURN:
	case SDL_SCANCODE_KP_ENTER: return "Enter";
	case SDL_SCANCODE_TAB: return "Tab";
	case SDL_SCANCODE_ESCAPE: return "Escape";
	case SDL_SCANCODE_LSHIFT: return "ShiftLeft";
	case SDL_SCANCODE_RSHIFT: return "ShiftRight";
	case SDL_SCANCODE_LCTRL: return "ControlLeft";
	case SDL_SCANCODE_RCTRL: return "ControlRight";
	case SDL_SCANCODE_LALT: return "AltLeft";
	case SDL_SCANCODE_RALT: return "AltRight";
	case SDL_SCANCODE_MINUS: return "Minus";
	case SDL_SCANCODE_EQUALS: return "Equal";
	case SDL_SCANCODE_COMMA: return "Comma";
	case SDL_SCANCODE_PERIOD: return "Period";
	case SDL_SCANCODE_SLASH: return "Slash";
	case SDL_SCANCODE_SEMICOLON: return "Semicolon";
	case SDL_SCANCODE_APOSTROPHE: return "Quote";
	case SDL_SCANCODE_LEFTBRACKET: return "BracketLeft";
	case SDL_SCANCODE_RIGHTBRACKET: return "BracketRight";
	case SDL_SCANCODE_BACKSLASH: return "Backslash";
	case SDL_SCANCODE_GRAVE: return "Backquote";
	case SDL_SCANCODE_CAPSLOCK: return "CapsLock";
	case SDL_SCANCODE_INSERT: return "Insert";
	default: return {};
	}
}

class sdl_host final : public host {
public:
	~sdl_host() override {
		for (SDL_Cursor * cursor : {arrow_, hand_, beam_}) {
			if (cursor != nullptr) { SDL_DestroyCursor(cursor); }
		}
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
			// The PAGE is authored at the logical size and stays there; the
			// window only decides how big that gets drawn.
			letterboxed_ = true;
		}
		// Without this, SDL_EVENT_TEXT_INPUT never arrives and no <input> can
		// be typed into.
		SDL_StartTextInput(window_.get());
		// System cursors, made once. A pointer over a link and an I-beam over
		// text are most of what makes a page feel like a page.
		arrow_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
		hand_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
		beam_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
		return true;
	}

	[[nodiscard]] bool pump(browser & page, bool & changed) override {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) { return false; }
			// Window coordinates are not page coordinates when the page is
			// presented letterboxed: a 320x240 game in a 960x720 window gets
			// every pointer event at three times the position it should be, and
			// most of them outside the page entirely.
			SDL_ConvertEventToRenderCoordinates(renderer_, &event);
			if (event.type == SDL_EVENT_MOUSE_MOTION) {
				mouse_x_ = event.motion.x;
				mouse_y_ = event.motion.y;
			}
			input_event translated;
			if (translate(event, translated) && page.handle(translated)) { changed = true; }
		}
		apply_cursor(page);
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
	[[nodiscard]] static std::uint8_t dom_button(std::uint8_t sdl_button) noexcept {
		if (sdl_button == SDL_BUTTON_RIGHT) { return input_event::right_button; }
		if (sdl_button == SDL_BUTTON_MIDDLE) { return 1; }
		return input_event::left_button;
	}

	[[nodiscard]] bool translate(const SDL_Event & event, input_event & out) const {
		switch (event.type) {
		case SDL_EVENT_MOUSE_MOTION:
			out = input_event::mouse_move_to(event.motion.x, event.motion.y);
			return true;
		// SDL numbers buttons from 1 and calls the right one 3; the DOM numbers
		// from 0 and calls it 2, and so does input_event. Passing SDL's number
		// straight through made a right-click look like button 3, which nothing
		// was looking for.
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			out = input_event::mouse_down_at(event.button.x, event.button.y,
			                                 dom_button(event.button.button));
			return true;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			out = input_event::mouse_up_at(event.button.x, event.button.y,
			                               dom_button(event.button.button));
			return true;
		case SDL_EVENT_MOUSE_WHEEL: out = input_event::wheel_by(event.wheel.y); return true;
		case SDL_EVENT_TEXT_INPUT:
			// The typed text itself, which is not derivable from key codes.
			out = input_event::typed(std::string{event.text.text});
			return true;
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP: {
			std::string code = dom_key_code(event.key.scancode);
			if (code.empty()) { return false; }
			const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
			const bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
			// A key RELEASE is half the information a game needs. Without it
			// every held key stays down forever, so a paddle that starts moving
			// never stops.
			out = event.type == SDL_EVENT_KEY_DOWN
			          ? input_event::key_press(std::move(code), shift, ctrl)
			          : input_event::key_release(std::move(code), shift, ctrl);
			return true;
		}
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			// A LETTERBOXED page must not be resized by its window. SDL sends
			// this on the very first frame with the window's size, so a
			// 320x240 game in a 960x720 window had its viewport widened to the
			// window immediately - leaving the canvas, which is 320x240 by its
			// own attributes, occupying a ninth of the page.
			if (letterboxed_) { return false; }
			out = input_event::resized(event.window.data1, event.window.data2);
			return true;
		default: return false;
		}
	}

	// What the pointer should look like where it is now. Asked of the browser
	// each frame rather than pushed, so the engine needs no cursor vocabulary
	// beyond a name.
	void apply_cursor(browser & page) {
		SDL_Cursor * want = arrow_;
		const std::string_view name = page.cursor_at(mouse_x_, mouse_y_);
		if (name == "pointer") { want = hand_; }
		else if (name == "text") { want = beam_; }
		if (want != nullptr && want != current_cursor_) {
			SDL_SetCursor(want);
			current_cursor_ = want;
		}
	}

	float mouse_x_ = 0;
	float mouse_y_ = 0;
	SDL_Cursor * arrow_ = nullptr;
	SDL_Cursor * hand_ = nullptr;
	SDL_Cursor * beam_ = nullptr;
	SDL_Cursor * current_cursor_ = nullptr;
	std::unique_ptr<SDL_Window, window_deleter> window_;
	SDL_Renderer * renderer_ = nullptr;
	SDL_Texture * texture_ = nullptr;
	int width_ = 0;
	int height_ = 0;
	bool letterboxed_ = false;
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
namespace ctbrowser {

// Run a page. Returns a process exit code.
//
// This is the whole application API: it owns the window, the event loop, the
// clock, the frame pacing and the teardown.
int run_app(std::string_view html, app_options options) {
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
	// The system clipboard. The engine keeps its own when this is absent, so
	// copy and paste work headlessly - they just do not leave the process.
#if CTBROWSER_WITH_SDL3
	page.set_clipboard_hooks([](const std::string & text) { (void)SDL_SetClipboardText(text.c_str()); },
	                         [] {
		                         char * owned = SDL_GetClipboardText();
		                         std::string text = owned != nullptr ? owned : "";
		                         SDL_free(owned);
		                         return text;
	                         });
	// alert() as a real modal, and a link that leaves the page handed to the
	// system browser. Both are what a user expects and neither can live in the
	// engine, which has no window and no idea what a browser is.
	page.set_alert_hook([&host](const std::string & message) {
		(void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "ctbrowser", message.c_str(),
		                               static_cast<SDL_Window *>(host->native_window()));
	});
	page.set_navigate_hook([](const std::string & url) { (void)SDL_OpenURL(url.c_str()); });
#endif
	if (options.real_fonts) { (void)page.use_real_fonts(options.font_path.string()); }

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
int run_app_file(const std::filesystem::path & path, app_options options) {
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