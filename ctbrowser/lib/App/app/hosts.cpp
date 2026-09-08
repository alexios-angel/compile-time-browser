// run_app's hosts: the headless one, the SDL window with its event translation,
// and make_host, which chooses between them at run time.
//
// One of two files carved out of a 1,034-line App/app.cpp on 2026-09-08. The
// `host` interface they share is in internal.hpp beside this, with the
// includes app.cpp had; nothing about include/ctbrowser/app/app.hpp changed.

#include "internal.hpp"

namespace ctbrowser::detail {

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

[[nodiscard]] std::unique_ptr<host> make_host(const app_options & options) {
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
