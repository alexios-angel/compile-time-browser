//===- ctbrowser/lib/App/app/run.cpp - The application event loop ---------===//
//
// run_app, run_app_file and run_bundle: the audio device, the optional
// SDL3_image decoder, the event loop, and the file and bundle entry points.
// The `host` (window) interface is declared in internal.hpp.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::detail {

// --- Audio ----------------------------------------------------------------

/// Plays WAV samples, mixed by SDL3 itself. There is no SDL3_mixer, because a
/// game firing a blip on every shot needs exactly one thing: play these
/// samples now, overlapping. An audio stream per sound, fed once and left to
/// drain, is that.
///
/// Sound is not part of the engine: the shell library is deliberately
/// SDL-free, so this lives here with the window and reaches the page through
/// `browser::define_native`.
class audio_device {
public:
    ~audio_device() { shutdown(); }
    audio_device() = default;
    audio_device(const audio_device &) = delete;
    audio_device & operator=(const audio_device &) = delete;

    /// Plays `name`, resolved through the same registry images use. Returns
    /// whether it started, so a page can tell "no sound in this build" from "that
    /// file does not exist".
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

    /// Stops every playing sound and closes the device.
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
    /// One WAV file decoded to PCM; `bytes` is empty when decoding failed.
    struct decoded {
        std::string name;
        SDL_AudioSpec spec{};
        std::vector<std::uint8_t> bytes;
    };

    /// Decodes `name`, once per name (failures included): a shot fires the same
    /// blip a hundred times and re-parsing the file each time is the
    /// difference between a game and a stutter. Null when it does not decode.
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

    /// Opens the default playback device on first use.
    [[nodiscard]] bool open(const SDL_AudioSpec & spec) {
        if (device_ != 0) { return true; }
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { return false; }
        device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
        return device_ != 0;
    }

    /// Destroys the streams that have finished playing.
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

/// Installs SDL3_image as the decoder for formats the engine does not decode
/// itself (it handles BMP, PNG and JPEG). SDL3_image is optional; without it
/// this does nothing. This is the only place in the tree where SDL and image
/// decoding meet: the shell must not learn about SDL.
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

// Runs a page. Returns a process exit code.
//
// This is the whole application API: it owns the window, the event loop, the
// clock, the frame pacing and the teardown.
int run_app(std::string_view html, app_options options) {
    if (options.environment_overrides) { apply_environment(options); }

    std::unique_ptr<detail::host> host = detail::make_host(options);

    shell::browser_options browser_options;
    browser_options.width = options.logical_width > 0 ? options.logical_width : options.width;
    browser_options.height = options.logical_height > 0 ? options.logical_height : options.height;
    browser_options.caret_blink_ms = options.caret_blink_ms;
    browser_options.on_request = options.on_request;
    shell::browser page{browser_options};

    // Resources before the page: an <img src> is resolved while the document
    // loads, so a registry seeded afterwards would be seeded too late.
    for (const asset & item : options.assets) { page.assets().add(item.name, item.bytes); }
    // The compiled script images too, before the page for the same reason:
    // they are consulted while it loads. A refusal here is a packaging fault
    // (bytes that are not an image this build would load, usually because
    // another build made them), and it is reported rather than swallowed,
    // because the application would otherwise run correctly and slowly with
    // nothing said.
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
    // Where a URL path with a leading `/` resolves. Empty for every application
    // in this tree, which leaves it meaning the root of the filesystem.
    // tools/wpt/run-wpt.py is the one caller that sets it: a web-platform-test
    // names its harness `/resources/testharness.js`, and this is the document
    // root a server would have resolved that against.
    page.assets().set_document_root(options.document_root);
    page.assets().set_sealed(options.sealed_assets);
    page.assets().set_confine(options.confine_assets);
    page.assets().set_fetch_policy(options.network_policy);
    page.allow_network(options.network);
    if (options.download_directory) { page.set_download_directory(*options.download_directory); }
    page.set_download_limit(options.max_download_bytes);
    detail::install_image_decoder(page.images());
    // The real wall clock, because this is an application and a page showing
    // the wrong date is a bug. The engine's own default is deterministic (a
    // fixed base plus the page's elapsed time), so a headless golden run is
    // unaffected by this line, which is the whole reason the clock is a hook. Pinned by
    // fixed_clock (CTBROWSER_CLOCK) when a golden does need to draw a date.
    if (options.fixed_clock) {
        page.set_clock([fixed = *options.fixed_clock] { return fixed; });
    } else {
        page.set_clock([] {
            return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::system_clock::now().time_since_epoch())
                                           .count());
        });
    }
    // The system clipboard. The engine keeps its own when this is absent, so
    // copy and paste work headlessly; they just do not leave the process.
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
    // A link that leaves the page. The application gets first refusal (that is
    // how `ctbrowse` follows a link to another local file), and only what the
    // policy (navigate_verdict_for) lets out reaches the system browser.
    page.set_navigate_hook(
        [&options](const std::string & url, const std::string & href, bool activated) {
            const navigate_request request{url, href, activated};
            if (navigate_verdict_for(options, request) != navigate_verdict::open_external) {
                return;
            }
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

    page.load_document(html, options.xml ? shell::browser::source_kind::xml
                                         : shell::browser::source_kind::html);
    // Check that the packaging worked. An image that matches no script on this
    // page is not refused by anything: it is simply never looked up, and the
    // page compiles from source and runs correctly. That is the silent failure
    // worth catching, and the counter is the only thing that can see it.
    //
    // Check whenever the caller requires complete images, even if it supplied
    // none: an application whose scripts are all modules has an empty image
    // list and must still be checked.
    if (!options.script_images.empty() || options.require_script_images) {
        // Module scripts cannot be packaged as images and are not counted by
        // the check below: there is no image path into load_module, so a page of
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
    // once. `script_error()` keeps the first callback fault rather than the
    // latest, so this settles rather than churning.
    std::string last_script_error;

    while (running) {
        bool rendered = false;

        if (!host->pump(page, needs_frame)) { break; }
        // The embedder's turn, right after the window's. An application that
        // drives the page itself (replaying a script, taking commands off a
        // socket) has nowhere else to stand: on_ready is called once and the
        // loop is otherwise closed.
        //
        // It runs here, on the main thread, rather than from a thread of the
        // embedder's own: the page is ticked and drawn from this one, and a
        // browser mutated from another while that happens is a data race the
        // suite would find under TSan.
        if (options.on_frame) {
            options.on_frame(page);
            // It may have handled input, so the frame it caused must be drawn
            // the same way one from the window would be.
            if (page.needs_frame()) { needs_frame = true; }
        }
        const auto now = clock::now();
        const double elapsed_ms =
            options.fixed_dt > 0
                ? options.fixed_dt * 1000.0
                : std::chrono::duration<double, std::milli>(now - previous).count();
        previous = now;

        // Advance the page clock: timers and requestAnimationFrame callbacks run
        // here.
        if (page.tick(elapsed_ms) > 0) { needs_frame = true; }
        // Report script errors, or a page whose callbacks throw looks frozen with
        // no explanation. Each distinct message is reported once, so an error
        // thrown every frame does not flood the output.
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
        // Ask the page too. Not everything that changes what should be on
        // screen comes from an event or a callback: a caret blinks, and a
        // scrollbar appears when a relayout makes the page taller. A loop that
        // redraws only when it did something shows neither until the user
        // happens to move the mouse.
        if (page.needs_frame()) { needs_frame = true; }

        if (needs_frame) {
            page.frame(&pool);
            host->present(page);
            rendered = true;
            needs_frame = false;
        }

        ++frame;
        const bool last = options.max_frames > 0 && frame >= options.max_frames;
        if (last && !options.screenshot_path.empty()) {
            (void)write_ppm(options.screenshot_path, page.read_pixels());
        }
        // With CTBROWSER_GL_LEDGER set, print on the last frame every WebGL
        // call the page made that `webgl_context` did not forward to ANGLE.
        // Unlike grepping the source, this lists only what this page actually
        // called.
        if (last && std::getenv("CTBROWSER_GL_LEDGER") != nullptr) {
            const auto missing = page.bindings().unforwarded_gl_calls();
            std::fprintf(stderr, "[gl] %zu unforwarded call(s)\n", missing.size());
            for (const std::string & call : missing) {
                std::fprintf(stderr, "[gl]   %s\n", call.c_str());
            }
        }
        if (last) { running = false; }

        // Pacing: the difference between a browser and a spin loop.
        //
        // A bounded run sprints: nobody is watching it, and a 30-frame test
        // should not take half a second of wall clock to say so.
        if (options.max_fps > 0 && options.max_frames == 0 && running) {
            const auto budget = std::chrono::nanoseconds{1'000'000'000 / options.max_fps};
            const auto spent = clock::now() - now;
            const auto left = budget > spent ? budget - spent : std::chrono::nanoseconds{0};

            // Pace on whether a frame was drawn, not on page.needs_frame(),
            // which is false as soon as a frame finishes and would skip the
            // sleep, so max_fps would have no effect.
            if (rendered) {
                if (left.count() > 0) { std::this_thread::sleep_for(left); }
            } else if (!page.needs_frame()) {
                // Nothing to draw: wait for an event rather than sleeping out
                // the budget and looking again. An idle page costs nothing at
                // all this way, and an event is acted on the moment it arrives
                // instead of up to a frame later.
                //
                // The timeout is however long until the page next has
                // something to do on its own (a timer, an animation frame,
                // the caret's next blink), so a blinking caret still blinks on
                // time and a page with neither sleeps until the user acts.
                const double due = page.next_wakeup_ms();
                const std::int32_t timeout = due >= static_cast<double>(options.idle_wait_ms)
                                                 ? options.idle_wait_ms
                                                 : static_cast<std::int32_t>(std::max(1.0, due));
                host->wait_for_event(timeout);
            }
        }
    }

#if CTBROWSER_WITH_SDL3
    host.reset(); // The window and the renderer go before SDL_Quit.
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0) { SDL_Quit(); }
#endif
    return 0;
}

// A page loaded from a file resolves its images and page-local fetches next to
// itself, which is what a `<img src="cat.bmp">` beside the html means.
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
    // The extension, and only the extension. A caller that already knows
    // (because it fetched the bytes and has a content type) sets `options.xml`
    // itself and this leaves it alone.
    if (!options.xml) { options.xml = is_xml_extension(path.filename().string()); }
    return run_app(buffer.str(), std::move(options));
}

// Runs a packaged application, unpacked into the options `run_app` already
// takes.
//
// The unpacking is the whole of it, and every line is a place a launcher could
// get it subtly wrong: an image put in `assets` instead of `script_images` is
// a resource nobody asks for, and a page whose images arrive after `load_html`
// is a page that already compiled from source. Doing it once here is what
// stops a second launcher doing it differently.
int run_bundle(std::span<const std::byte> bytes, app_options overrides, bool trust_environment) {
    const auto loaded = shell::read_bundle(bytes);
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
    // A packaged application carries everything it needs, so a script that
    // still compiles from source means the packaging is wrong, not that the
    // application should start anyway and be slow. This is the one caller that
    // can say that, because it is the one that knows the images were meant to
    // be complete.
    options.require_script_images = true;
    // It looks nowhere else for its assets. Without this the registry falls
    // back to the filesystem, probing the working directory first, so a packaged
    // application missing a resource reads whatever happens to sit next to the
    // user, under the name its own document asked for. A miss has to be a miss.
    options.sealed_assets = true;
    // Ignore CTBROWSER_* environment overrides (CTBROWSER_TEST_FRAMES would give
    // a fixed-step speedhack, CTBROWSER_MAX_FPS=0 an uncapped loop) unless the
    // bundle was packaged with --allow-env-overrides or the caller trusts them
    // (`trust_environment`, e.g. ctrun given a loose bundle file).
    options.environment_overrides = trust_environment || bundle.meta("env_overrides") == "1";
    // Use the font faces it was packaged with. The registry key for a face is
    // "<directory>/Tinos-Regular.ttf", so the directory is part of the name and
    // the bundle's answer has to win over this machine's: a run that resolved
    // CTBROWSER_FONT_PATH to somewhere else would ask a sealed registry for a
    // name nothing in it carries, and drop to the bitmap font without a word.
    if (const std::string packaged_fonts = bundle.meta("font_path"); !packaged_fonts.empty()) {
        options.real_fonts = true;
        options.font_path = packaged_fonts;
    }

    const std::span<const std::byte> html = bundle.html();
    return run_app(std::string_view{reinterpret_cast<const char *>(html.data()), html.size()},
                   std::move(options));
}

} // namespace ctbrowser
