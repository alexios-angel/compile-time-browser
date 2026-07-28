#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
// <ostream> EXPLICITLY. `out << "P6\n"` needs the const char* inserter, and
// without it the const void* one is chosen instead - which compiles, and
// writes the POINTER as hex into the file. libstdc++ happened to make it
// visible transitively and libc++ did not, so the screenshots the Windows
// build wrote had a corrupt header while the Linux ones were fine.
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>

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
// reference consumer wrote a dozen SDL calls by hand to open one page. the previous engine's
// `run_app<page>(opts)` was better than that, and this is the previous engine's shape without
// the compile-time machinery.

// SDL IS NOT INCLUDED ABOVE, and that is the point. A module's global module
// fragment is serialized into its BMI, so <SDL3/SDL.h> here cost every
// translation unit that imported the application API 26 MB of AST to
// deserialize - for a header whose types this module deliberately never
// exposes. It is included by app.cpp, once.

namespace ctbrowser {

// A file the page can reach by name - what an asset lookup finds before it
// touches the filesystem.
struct asset {
    std::string name;
    std::vector<std::byte> bytes;
};

enum class renderer_preference : std::uint8_t {
    automatic,
    prefer_gpu,
    force_software
};

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
    // THE CPU THROTTLE. An application redraws at most this often while
    // something is changing, and does not redraw AT ALL while nothing is - an
    // idle page blocks on the event queue and costs nothing, whatever this
    // says. So the cost of a busy page is roughly this number times what one
    // frame costs, and halving it halves the CPU.
    //
    // 0 = uncapped, which is what a benchmark wants and what a game with its
    // own pacing can ask for. Fixed-step pages depend on the cap.
    // `CTBROWSER_MAX_FPS` sets it from the environment.
    int max_fps = 60;
    double fixed_dt = 0; // >0: pretend every frame took exactly this long

    bool fullscreen = false;

    std::string screenshot_path; // "" = never
    int screenshot_frame = -1;   // -1 = the last frame

    std::vector<asset> assets;
    // Where a relative asset path (an <img src>, a page-local fetch) resolves
    // from when the registry misses. Empty means the working directory.
    std::filesystem::path asset_path;
    // Real outline fonts, from the vendored OFL faces and whatever the page's
    // @font-face rules ask for. ON by default - an application wants text that
    // looks like text - and falling back to the built-in bitmap font when
    // SDL3_ttf is absent or the files are not found.
    //
    // `CTBROWSER_FONTS=font8x8` forces the bitmap font, which is what makes a
    // run reproducible ACROSS MACHINES: two FreeType versions do not rasterize
    // identically, so a cross-platform byte comparison has to ask for font8x8.
    bool real_fonts = true;
    std::filesystem::path font_path = "fonts";

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

    // A link the page followed that LEAVES this document. Return true if the
    // application handled it - `ctbrowse` loads a local .html this way - and
    // false to let it go to the SYSTEM BROWSER, which is what happens with no
    // hook at all and what a user clicking an http:// link expects.
    //
    // A hook rather than a browser-level one so an application does not have to
    // remember to chain: setting browser::set_navigate_hook directly REPLACES
    // the system-browser fallback, which is how ctbrowse silently swallowed
    // every external link it was given.
    std::function<bool(const std::string & url)> on_navigate;

    // --- profiling ---------------------------------------------------------
    //
    // WHERE THE TIME GOES, per loop iteration, written as CSV and summarised
    // on stdout. Guessing at this is how you end up optimising the wrong
    // thing: an application that feels busy might be re-rasterising every
    // frame, or re-laying-out on every mouse move, or simply never sleeping,
    // and those have nothing to do with each other.
    //
    // Reports CPU time against wall time, because "it uses 65% of my CPU" is
    // the complaint and frames-per-second is not the same question.
    //
    // `CTBROWSER_PROFILE=out.csv` and `CTBROWSER_PROFILE_SECONDS=n` set these
    // from the environment, so any example can be profiled without a rebuild.
    std::string profile_path;   // "" = off; "-" = summary only, no file
    double profile_seconds = 0; // >0: stop after this long, like max_frames
};

// Environment overrides, applied by run_app before anything else:
//
//   CTBROWSER_TEST_FRAMES  -> max_frames (and therefore a fixed timestep)
//   CTBROWSER_SCREENSHOT   -> screenshot_path
//   CTBROWSER_RENDERER     -> software | gpu
//   CTBROWSER_NETWORK      -> 0 disables fetch()'s network access
//   CTBROWSER_FONTS        -> font8x8 forces the built-in bitmap font
//   CTBROWSER_MAX_FPS      -> max_fps, the redraw cap (0 = uncapped)
//   CTBROWSER_PROFILE      -> profile_path ("-" = summary only)
//   CTBROWSER_PROFILE_SECONDS -> profile_seconds
//
// Carried over from the previous engine because it is what lets an example BE a ctest without
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
    if (const char * fonts = std::getenv("CTBROWSER_FONTS")) {
        const std::string_view text{fonts};
        options.real_fonts = !(text == "font8x8" || text == "bitmap" || text == "0");
    }
    if (const char * network = std::getenv("CTBROWSER_NETWORK")) {
        const std::string_view text{network};
        options.network = !(text == "0" || text == "off" || text == "no");
    }
    if (const char * fps = std::getenv("CTBROWSER_MAX_FPS")) { options.max_fps = std::atoi(fps); }
    if (const char * profile = std::getenv("CTBROWSER_PROFILE")) { options.profile_path = profile; }
    if (const char * seconds = std::getenv("CTBROWSER_PROFILE_SECONDS")) {
        options.profile_seconds = std::atof(seconds);
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
                                 static_cast<char>((p >> 8) & 0xFFu), static_cast<char>(p & 0xFFu)};
            out.write(rgb, 3);
        }
    }
    return out.good();
}

// Run a page. Returns a process exit code.
//
// This is the whole application API: it owns the window, the event loop, the
// clock, the frame pacing and the teardown.
[[nodiscard]] int run_app(std::string_view html, app_options options = {});

// A page loaded from a file resolves its images and page-local fetches next to
// ITSELF, which is what a `<img src="cat.bmp">` beside the html means.
[[nodiscard]] int run_app_file(const std::filesystem::path & path, app_options options = {});

} // namespace ctbrowser
