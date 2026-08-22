#if CTBROWSER_WITH_SDL3
#include <SDL3/SDL.h>
#if CTBROWSER_WITH_IMAGE
#include <SDL3_image/SDL_image.h>
#endif
#endif

#include <algorithm>
#include <charconv>
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

#include <ctbrowser/app/app.hpp>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/app_bundle.hpp>
#include <ctbrowser/shell/shell.hpp>

// The window, the event loop and the only place SDL is read. See the note in
// app.hpp about why SDL's headers are not in the header.

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
    // Block until an event arrives or the timeout expires. The headless host
    // has no events, so it sleeps - which is the same thing from the loop's
    // point of view and keeps a headless run from spinning either.
    virtual void wait_for_event(std::int32_t timeout_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds{timeout_ms});
    }
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
        window_.reset(
            SDL_CreateWindow(options.title.c_str(), options.width, options.height, flags));
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
            texture_ =
                SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                  image->width(), image->height());
            // NEAREST, because SDL3 defaults a texture to LINEAR and the only
            // time this one is SCALED is under logical presentation - where a
            // 320x240 playfield is stretched over a 960x720 window and bilinear
            // filtering smears every sprite edge into a soft ramp.
            //
            // Unconditional rather than gated on letterboxing: without it a
            // resize reflows the page and this texture is recreated at the new
            // size, so the blit is 1:1 and the filter cannot be observed - bar
            // the single frame between the resize event and the reflow, where
            // the old texture is stretched and NEAREST is the right answer too.
            if (texture_ != nullptr) { SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST); }
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

    void wait_for_event(std::int32_t timeout_ms) override {
        // NULL: wait for an event to be THERE without taking it, so the next
        // pump() sees it. Taking it and pushing it back looks equivalent and is
        // not - SDL posts an internal poll sentinel to bound PollEvent loops,
        // and pushing that back re-arms it, so the wait returns instantly,
        // forever. Measured at eight million iterations in ten seconds.
        (void)SDL_WaitEventTimeout(nullptr, timeout_ms);
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
        // The TRACKED pointer, not event.wheel.mouse_x. SDL3's wheel event does
        // carry a position, but SDL_ConvertEventToRenderCoordinates documents
        // itself as converting mouse, touch and pen events and does not name
        // the wheel's - and under logical presentation an unconverted position
        // is wrong by the letterbox factor, which is exactly the bug the
        // pointer events already had once. mouse_x_/mouse_y_ are taken from
        // motion events AFTER the blanket convert, so they are definitely in
        // render coordinates.
        case SDL_EVENT_MOUSE_WHEEL:
            out = input_event::wheel_at(mouse_x_, mouse_y_, event.wheel.y);
            return true;
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
        if (name == "pointer") {
            want = hand_;
        } else if (name == "text") {
            want = beam_;
        }
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

// --- the profiler ---------------------------------------------------------
//
// One record per loop iteration. Kept in memory and written at the end rather
// than streamed, because writing to a file inside the loop is itself a cost
// and would show up in its own measurements.
struct frame_record {
    double at_ms = 0;    // since the run started
    double poll_ms = 0;  // events in
    double tick_ms = 0;  // timers, rAF, the caret clock
    double frame_ms = 0; // style, layout, record, raster - and the four below
    // THE SAME FRAME, BROKEN UP. frame_ms is their sum plus whatever the frame
    // did around them; these say WHICH stage, which is the question a slow page
    // actually raises. A skipped stage is 0, and on a scroll or a caret blink
    // three of them SHOULD be - see browser::frame_timing.
    double styles_ms = 0;
    double layout_ms = 0;
    double record_ms = 0;
    double raster_ms = 0;
    double present_ms = 0;
    double wait_ms = 0; // deliberately asleep
    std::uint32_t layouts = 0;
    bool rendered = false;
};

inline void report_profile(const std::vector<frame_record> & history, double wall_seconds,
                           double cpu_seconds, const std::string & path) {
    if (history.empty()) { return; }
    const auto total = [&history](double frame_record::* field) {
        double sum = 0;
        for (const frame_record & r : history) { sum += r.*field; }
        return sum;
    };
    const auto percentile = [](std::vector<double> values, double fraction) {
        if (values.empty()) { return 0.0; }
        const auto at = static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1));
        std::ranges::nth_element(values, values.begin() + static_cast<std::ptrdiff_t>(at));
        return values[at];
    };

    std::size_t rendered = 0;
    std::vector<double> frame_times;
    for (const frame_record & r : history) {
        if (!r.rendered) { continue; }
        ++rendered;
        frame_times.push_back(r.frame_ms + r.present_ms);
    }
    const std::uint32_t layouts = history.back().layouts - history.front().layouts;

    std::printf("\n--- ctbrowser profile ------------------------------------\n");
    std::printf("  wall            %8.2f s\n", wall_seconds);
    std::printf("  CPU             %8.2f s   (%.1f%% of one core)\n", cpu_seconds,
                wall_seconds > 0 ? 100.0 * cpu_seconds / wall_seconds : 0.0);
    std::printf("  iterations      %8zu     (%.0f/s)\n", history.size(),
                wall_seconds > 0 ? static_cast<double>(history.size()) / wall_seconds : 0.0);
    std::printf("  frames drawn    %8zu     (%.0f/s, %.0f%% of iterations)\n", rendered,
                wall_seconds > 0 ? static_cast<double>(rendered) / wall_seconds : 0.0,
                100.0 * static_cast<double>(rendered) / static_cast<double>(history.size()));
    std::printf("  layouts run     %8u\n", layouts);
    std::printf("  ---- where the wall time went (ms, total) ----\n");
    std::printf("    poll events   %8.1f\n", total(&frame_record::poll_ms));
    std::printf("    tick clock    %8.1f\n", total(&frame_record::tick_ms));
    std::printf("    frame         %8.1f\n", total(&frame_record::frame_ms));
    // Indented under `frame` because that is what they add up to. Printed even
    // when zero: a stage the frame never ran is the dirty-level design working,
    // and seeing `layout 0.0` on a scroll is the confirmation.
    std::printf("      styles      %8.1f\n", total(&frame_record::styles_ms));
    std::printf("      layout      %8.1f\n", total(&frame_record::layout_ms));
    std::printf("      record      %8.1f\n", total(&frame_record::record_ms));
    std::printf("      raster      %8.1f\n", total(&frame_record::raster_ms));
    std::printf("    present       %8.1f\n", total(&frame_record::present_ms));
    std::printf("    asleep        %8.1f\n", total(&frame_record::wait_ms));
    if (!frame_times.empty()) {
        std::printf("  ---- per drawn frame (ms) ----\n");
        std::printf("    median %6.2f   p95 %6.2f   max %6.2f\n", percentile(frame_times, 0.5),
                    percentile(frame_times, 0.95), *std::ranges::max_element(frame_times));
    }
    std::printf("-----------------------------------------------------------\n");

    if (path.empty() || path == "-") { return; }
    std::ofstream out{path};
    if (!out) {
        std::printf("ctbrowser: cannot write %s\n", path.c_str());
        return;
    }
    out << "at_ms,poll_ms,tick_ms,frame_ms,styles_ms,layout_ms,record_ms,raster_ms,"
           "present_ms,wait_ms,layouts,rendered\n";
    for (const frame_record & r : history) {
        out << r.at_ms << ',' << r.poll_ms << ',' << r.tick_ms << ',' << r.frame_ms << ','
            << r.styles_ms << ',' << r.layout_ms << ',' << r.record_ms << ',' << r.raster_ms << ','
            << r.present_ms << ',' << r.wait_ms << ',' << r.layouts << ',' << (r.rendered ? 1 : 0)
            << '\n';
    }
    std::printf("ctbrowser: profile history written to %s\n", path.c_str());
}

} // namespace ctbrowser::detail
namespace ctbrowser {

// Run a page. Returns a process exit code.
//
// This is the whole application API: it owns the window, the event loop, the
// clock, the frame pacing and the teardown.
// How long an idle application blocks before looking around anyway. Not
// forever: a wait that never times out would need every source of change to be
// an SDL event, and the browser has its own clock.
constexpr std::int32_t idle_wait_ms = 250;

// How many iterations a profile keeps. Past this it counts and drops: see the
// note where it is used.
constexpr std::size_t profile_history_limit = 200'000;

int run_app(std::string_view html, app_options options) {
    apply_environment(options);

    std::unique_ptr<detail::host> host = detail::make_host(options);
    if (options.on_native_window) { options.on_native_window(host->native_window()); }

    shell::browser_options browser_options;
    browser_options.width = options.logical_width > 0 ? options.logical_width : options.width;
    browser_options.height = options.logical_height > 0 ? options.logical_height : options.height;
    browser_options.caret_blink_ms = options.caret_blink_ms;
    shell::browser page{browser_options};
    // WHICH WebGL BACK END, chosen from the environment because an example
    // program should not have to know there is a choice.
    //
    // Stage 3 of docs/plans/angle.md needs the SAME example rendered both ways -
    // once through the software rasteriser, which is what the goldens hold, and
    // once through ANGLE - so the switch has to be reachable without editing
    // fourteen example programs. `CTBROWSER_CLOCK` is the same shape: a knob a
    // golden run turns.
    if (const char * backend = std::getenv("CTBROWSER_WEBGL");
        backend != nullptr && std::string_view{backend} == "angle") {
        page.prefer_angle_webgl(true);
    }

    // Resources BEFORE the page: an <img src> is resolved while the document
    // loads, so a registry seeded afterwards would be seeded too late.
    for (const asset & item : options.assets) { page.assets().add(item.name, item.bytes); }
    // AND THE COMPILED SCRIPTS, before the page for the same reason: they are
    // consulted while it loads. A refusal here is a PACKAGING fault - bytes that
    // are not an image this build would load, usually because they were built by
    // another one - and it is reported rather than swallowed, because the
    // application would otherwise run correctly and slowly with nothing said.
    std::size_t images_refused = 0;
    for (const std::vector<std::byte> & image : options.script_images) {
        if (!page.add_script_image(image)) { ++images_refused; }
    }
    if (images_refused != 0) {
        std::fprintf(stderr,
                     "ctbrowser: %zu of %zu script images were refused - they were not built by "
                     "this engine build\n",
                     images_refused, options.script_images.size());
    }
    page.assets().set_base_path(options.asset_path);
    page.assets().set_sealed(options.sealed_assets);
    page.allow_network(options.network);
    detail::install_image_decoder(page.images());
    // THE REAL WALL CLOCK, because this is an application and a page showing the
    // wrong date is a bug. The engine's own default is deterministic - a fixed
    // base plus the page's elapsed time - so a headless golden run is unaffected
    // by this line, which is the whole reason the clock is a hook. Pinned by
    // CTBROWSER_CLOCK (milliseconds since the epoch) when a golden does need to
    // draw a date.
    if (const char * pinned = std::getenv("CTBROWSER_CLOCK"); pinned != nullptr) {
        // from_chars rather than strtod: this pins the clock so a golden can
        // draw a date, and a value that parsed differently under another
        // locale would move the very render it exists to hold still.
        double fixed = 0.0;
        const std::string_view text{pinned};
        std::from_chars(text.data(), text.data() + text.size(), fixed);
        page.set_clock([fixed] { return fixed; });
    } else {
        page.set_clock([] {
            return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::system_clock::now().time_since_epoch())
                                           .count());
        });
    }
    // The system clipboard. The engine keeps its own when this is absent, so
    // copy and paste work headlessly - they just do not leave the process.
#if CTBROWSER_WITH_SDL3
    page.set_clipboard_hooks(
        [](const std::string & text) { (void)SDL_SetClipboardText(text.c_str()); },
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
#endif
    // A LINK THAT LEAVES THE PAGE. The application gets first refusal - that is
    // how `ctbrowse` follows a link to another local file - and anything it
    // does not claim goes to the system browser, which is what clicking an
    // http:// link means.
    page.set_navigate_hook([&options](const std::string & url) {
        if (options.on_navigate && options.on_navigate(url)) { return; }
#if CTBROWSER_WITH_SDL3
        (void)SDL_OpenURL(url.c_str());
#endif
    });
    if (options.real_fonts) { (void)page.use_real_fonts(options.font_path.string()); }

    // Sound. `playSound(name [, volume])` is what the page calls; the HTML
    // <audio> element is not implemented, and a native the embedder installs is
    // the honest way to say so rather than a half-built element that looks like
    // the real thing.
    detail::audio_device audio;
    page.define_native(
        "playSound", [&audio, &page](script::context & cx, std::span<script::value> args) {
            const std::string name = args.empty() ? std::string{} : cx.to_string(args[0]);
            const float volume =
                args.size() > 1 ? static_cast<float>(script::context::to_number(args[1])) : 1.0F;
            return script::value::boolean(audio.play(page.assets(), name, volume));
        });

    page.load_html(html);
    // DID THE PACKAGING ACTUALLY WORK? An image that matches no script on this
    // page is not refused by anything - it is simply never looked up, and the
    // page compiles from source and runs correctly. That is the silent failure
    // worth catching, and the counter is the only thing that can see it.
    //
    // AND THE CONDITION IS NOT "SOME IMAGES ARRIVED". It was, and that deleted
    // the sharpest case: an application packaged with NO images at all - every
    // script a module, say, which nothing here can compile ahead of time - has
    // an empty image list, so the check was skipped and the guard whose entire
    // job is "did the packaging work" never ran. A caller that says its images
    // are meant to be complete is asking to be checked either way.
    if (!options.script_images.empty() || options.require_script_images) {
        // MODULE SCRIPTS ARE NOT PACKAGEABLE YET and they are counted by
        // nothing above: there is no image path into load_module, so a page of
        // modules compiles all of its JavaScript at every start while
        // scripts_compiled_from_source() reads a truthful, useless zero.
        if (options.require_script_images && !page.module_sources().empty()) {
            std::fprintf(stderr,
                         "ctbrowser: this application has %zu module script(s), which cannot be "
                         "compiled ahead of time yet - it would parse them at every start\n",
                         page.module_sources().size());
            return 1;
        }
        const std::size_t compiled = page.scripts_compiled_from_source();
        if (compiled != 0) {
            std::fprintf(stderr,
                         "ctbrowser: %zu of this page's %zu classic scripts were compiled from "
                         "source rather than loaded from an image\n",
                         compiled, page.script_sources().size());
            if (options.require_script_images) {
                std::fprintf(stderr, "ctbrowser: refusing to run - the application was packaged "
                                     "with images that do not match its scripts\n");
                return 1;
            }
        }
    }
    if (options.on_ready) { options.on_ready(page); }

    scheduler pool;
    using clock = std::chrono::steady_clock;
    auto previous = clock::now();
    int frame = 0;
    bool needs_frame = true;
    bool running = true;
    // The last message reported, so a fault that repeats every frame is said
    // once. `script_error()` keeps the FIRST callback fault rather than the
    // latest, so this settles rather than churning.
    std::string last_script_error;

    const bool profiling = !options.profile_path.empty() || options.profile_seconds > 0;
    std::vector<detail::frame_record> history;
    std::size_t dropped_records = 0;
    const auto started = clock::now();
    const double cpu_started = process_cpu_seconds();
    const auto since = [&started](clock::time_point at) {
        return std::chrono::duration<double, std::milli>(at - started).count();
    };

    while (running) {
        const auto iteration_began = clock::now();
        detail::frame_record record;
        record.at_ms = since(iteration_began);

        if (!host->pump(page, needs_frame)) { break; }
        // THE EMBEDDER'S TURN, right after the window's. An application that
        // drives the page itself - replaying a script, taking commands off a
        // socket - has nowhere else to stand: on_ready is called once and the
        // loop is otherwise closed, which is why `ctbrowse` cannot be scripted.
        //
        // HERE, on the main thread, rather than from a thread of the
        // embedder's own: the page is ticked and drawn from this one, and a
        // browser mutated from another while that happens is a data race the
        // suite would find under TSan.
        if (options.on_frame) {
            options.on_frame(page);
            // It may have handled input, so the frame it caused must be drawn
            // the same way one from the window would be.
            if (page.needs_frame()) { needs_frame = true; }
        }
        const auto after_poll = clock::now();
        record.poll_ms =
            std::chrono::duration<double, std::milli>(after_poll - iteration_began).count();

        const auto now = after_poll;
        const double elapsed_ms =
            options.fixed_dt > 0
                ? options.fixed_dt * 1000.0
                : std::chrono::duration<double, std::milli>(now - previous).count();
        previous = now;

        // THE CLOCK. Timers and requestAnimationFrame fire from here. The
        // previous reference loop never called it, so every animated page was
        // frozen and nothing said so.
        if (page.tick(elapsed_ms) > 0) { needs_frame = true; }
        // AND SAY SO WHEN THE PAGE BREAKS. The loop ticked the clock and never
        // once looked at what came back: a page whose callbacks throw keeps
        // rendering whatever it last drew, so it looks frozen and reports
        // nothing at all. That is how the Phaser invaders page hid an undefined
        // method for an afternoon - update() threw on every frame and the
        // window showed create()'s output forever.
        //
        // ONCE PER DISTINCT MESSAGE, not once per frame: a callback that faults
        // every frame at 60 Hz would otherwise bury the first and only useful
        // line under thousands of copies of itself, which is the same as saying
        // nothing at all.
        if (const std::string & failure = page.script_error();
            !failure.empty() && failure != last_script_error) {
            last_script_error = failure;
            if (options.on_script_error) {
                options.on_script_error(failure);
            } else {
                // stderr, so a screenshot pipeline reading stdout is unaffected.
                std::fprintf(stderr, "ctbrowser: script error: %s\n", failure.c_str());
            }
        }
        // ASK THE PAGE. Not everything that changes what should be on screen
        // comes from an event or a callback: a caret blinks, and a scrollbar
        // appears when a relayout makes the page taller. A loop that redraws
        // only when IT did something shows neither until the user happens to
        // move the mouse - which is exactly how both of those looked.
        if (page.needs_frame()) { needs_frame = true; }
        const auto after_tick = clock::now();
        record.tick_ms = std::chrono::duration<double, std::milli>(after_tick - after_poll).count();

        if (needs_frame) {
            if (!page.frame(&pool)) { break; }
            const auto after_frame = clock::now();
            record.frame_ms =
                std::chrono::duration<double, std::milli>(after_frame - after_tick).count();
            // The stage split the engine just measured for itself.
            const auto & stages = page.last_frame_timing();
            record.styles_ms = stages.styles_ms;
            record.layout_ms = stages.layout_ms;
            record.record_ms = stages.record_ms;
            record.raster_ms = stages.raster_ms;
            host->present(page);
            record.present_ms =
                std::chrono::duration<double, std::milli>(clock::now() - after_frame).count();
            record.rendered = true;
            needs_frame = false;
        }
        record.layouts = static_cast<std::uint32_t>(page.layout_count());

        ++frame;
        const bool last = (options.max_frames > 0 && frame >= options.max_frames) ||
                          (options.profile_seconds > 0 &&
                           std::chrono::duration<double>(clock::now() - started).count() >=
                               options.profile_seconds);
        if (!options.screenshot_path.empty() &&
            (frame - 1 == options.screenshot_frame || (options.screenshot_frame < 0 && last))) {
            if (const auto image = page.read_pixels()) {
                (void)write_ppm(options.screenshot_path, *image);
            }
        }
        // WHAT THE PAGE ASKED FOR AND THE BACKEND DID NOT DO.
        //
        // `webgl_context` records every call it declined to forward to ANGLE
        // instead of ignoring it, and that ledger has been right every time it
        // was read and wrong none - while reasoning about which missing call
        // mattered has now picked wrong three times on the same page. This
        // makes it readable from a RUN rather than from a grep: the grep lists
        // what COULD go unforwarded, and only a run says what a given page
        // actually called.
        if (last && std::getenv("CTBROWSER_GL_LEDGER") != nullptr) {
            const auto missing = page.bindings().unforwarded_gl_calls();
            std::fprintf(stderr, "[gl] %zu unforwarded call(s)\n", missing.size());
            for (const std::string & call : missing) {
                std::fprintf(stderr, "[gl]   %s\n", call.c_str());
            }
        }
        if (last) { running = false; }

        // PACING, and the difference between a browser and a spin loop.
        //
        // A bounded run sprints: nobody is watching it, and a 30-frame test
        // should not take half a second of wall clock to say so.
        const auto before_wait = clock::now();
        if (options.max_fps > 0 && options.max_frames == 0 && running) {
            const auto budget = std::chrono::nanoseconds{1'000'000'000 / options.max_fps};
            const auto spent = clock::now() - now;
            const auto left = budget > spent ? budget - spent : std::chrono::nanoseconds{0};

            // DID WE DRAW? That is the question, not "is there more to draw" -
            // which is false the instant a frame finishes, so asking it took
            // the wait branch after every single frame and max_fps threw its
            // cap away. An animating page ran at whatever rate vsync allowed
            // however low the cap was set.
            if (record.rendered) {
                if (left.count() > 0) { std::this_thread::sleep_for(left); }
            } else if (!page.needs_frame()) {
                // Nothing to draw: WAIT FOR AN EVENT rather than sleeping out
                // the budget and looking again. An idle page costs nothing at
                // all this way, and an event is acted on the moment it arrives
                // instead of up to a frame later.
                //
                // The timeout is however long until the page next has
                // something to do on its own - a timer, an animation frame,
                // the caret's next blink - so a blinking caret still blinks on
                // time and a page with neither sleeps until the user acts.
                const double due = page.next_wakeup_ms();
                const std::int32_t timeout = due >= static_cast<double>(idle_wait_ms)
                                                 ? idle_wait_ms
                                                 : static_cast<std::int32_t>(std::max(1.0, due));
                host->wait_for_event(timeout);
            }
        }
        record.wait_ms =
            std::chrono::duration<double, std::milli>(clock::now() - before_wait).count();
        // BOUNDED. A profile of a loop that has gone wrong is exactly when the
        // history explodes - the first run of this wrote 306 MB in ten seconds
        // - and a profiler that fills the disk cannot be used on the bug it is
        // there to find.
        if (profiling && history.size() < profile_history_limit) {
            history.push_back(record);
        } else if (profiling) {
            ++dropped_records;
        }
    }

    if (profiling) {
        if (dropped_records > 0) {
            std::printf("ctbrowser: profile kept the first %zu iterations and dropped %zu more\n",
                        history.size(), dropped_records);
        }
        detail::report_profile(history,
                               std::chrono::duration<double>(clock::now() - started).count(),
                               process_cpu_seconds() - cpu_started, options.profile_path);
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

// A PACKAGED APPLICATION, unpacked into the options `run_app` already takes.
//
// The unpacking is the whole of it, and every line is a place a launcher could
// get it subtly wrong: an image put in `assets` instead of `script_images` is
// a resource nobody asks for, and a page whose images arrive after `load_html`
// is a page that already compiled from source. Doing it once here is what
// stops a second launcher doing it differently.
int run_bundle(std::span<const std::byte> bytes, app_options overrides) {
    auto loaded = shell::read_bundle(bytes);
    if (!loaded.ok) {
        std::fprintf(stderr, "ctbrowser: %s\n", loaded.error.c_str());
        return 2;
    }
    const shell::app_bundle & bundle = loaded.value;

    app_options options = std::move(overrides);
    if (options.title == "ctbrowser") {
        if (const std::string title = bundle.meta("title"); !title.empty()) {
            options.title = title;
        }
    }
    for (const shell::bundle_entry & one : bundle.entries) {
        switch (one.kind) {
        case shell::bundle_kind::asset: options.assets.push_back(asset{one.name, one.bytes}); break;
        case shell::bundle_kind::script_image: options.script_images.push_back(one.bytes); break;
        case shell::bundle_kind::html:
        case shell::bundle_kind::meta: break;
        }
    }
    // A PACKAGED APPLICATION CARRIES EVERYTHING IT NEEDS, so a script that
    // still compiles from source means the packaging is wrong - not that the
    // application should start anyway and be slow. This is the one caller that
    // can say that, because it is the one that knows the images were meant to
    // be complete.
    options.require_script_images = true;
    // AND IT LOOKS NOWHERE ELSE FOR THEM. Without this the registry falls back
    // to the filesystem, probing the WORKING DIRECTORY first - so a packaged
    // application missing a resource reads whatever happens to sit next to the
    // user, under the name its own document asked for. A miss has to be a miss.
    options.sealed_assets = true;

    const std::span<const std::byte> html = bundle.html();
    return run_app(std::string_view{reinterpret_cast<const char *>(html.data()), html.size()},
                   std::move(options));
}

} // namespace ctbrowser
