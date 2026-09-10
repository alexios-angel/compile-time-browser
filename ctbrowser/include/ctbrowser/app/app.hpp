#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <span>
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
// NO SDL TYPE APPEARS IN THIS INTERFACE, and <SDL3/SDL.h> is not included here:
// it is included by lib/App/app/internal.hpp and by nothing else, behind
// `#if CTBROWSER_WITH_SDL3`, so the library BUILDS EITHER WAY - with a window
// when SDL3 was found, headless when it was not. An application that wants to
// talk to SDL still can, through `app_options::on_native_window`.

namespace ctbrowser {

// A file the page can reach by name - what an asset lookup finds before it
// touches the filesystem.
struct asset {
    std::string name;
    std::vector<std::byte> bytes;
};

struct app_options {
    std::string title = "ctbrowser";

    // PARSE THE SOURCE AS XML, not HTML. `run_app_file` sets it from the
    // extension - `.xhtml`, `.xht`, `.xml` - because a file on disk has no
    // content type and the extension is the only signal there is. Never
    // sniffed from the bytes: `<?xml ...?>` is optional in XML 1.0 and turns
    // up in documents served as text/html. See dom/xml.hpp for what the two
    // front ends actually disagree about.
    bool xml = false;
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

    // Half the caret's blink period, in milliseconds; 0 holds it solid.
    // Chrome's 500 by default, which is what a person wants and what a
    // SCREENSHOT does not: a caret that is present in one run and absent in
    // the next is the difference between two otherwise identical images.
    double caret_blink_ms = 500;

    std::string screenshot_path; // "" = never
    int screenshot_frame = -1;   // -1 = the last frame

    std::vector<asset> assets;
    // PRECOMPILED SCRIPTS, baked in the same way assets are. Each entry is one
    // program image - the bytes `script::write_image` produced for ONE classic
    // <script> - and the page matches them to its own scripts by source hash, so
    // the order they are given in does not matter and an image for a script this
    // page does not have is simply never used.
    //
    // This is what makes a packaged application start without parsing its own
    // JavaScript, which is about forty percent of a page load
    // (ctcompile/docs/baseline/page-load.json: 69.65 ms to 19.93 on p5-basic).
    // Without it the launcher could bake in every PNG a page needs and not the
    // one thing that actually costs.
    std::vector<std::vector<std::byte>> script_images;
    // AND A PACKAGING MISTAKE IS AN ERROR, NOT A SLOW START. An image built by
    // another engine build is refused at the door, and one that matches no
    // script on the page is silently never used - both leave the application
    // working and slow, which is the failure this project treats as worst
    // because nothing reports it. With this set, `run_app` refuses to run an
    // application whose scripts did not all come from images. Off by default:
    // an embedder handing over a partial set is doing something reasonable.
    bool require_script_images = false;

    // A PACKAGED APPLICATION LOOKS NOWHERE BUT INSIDE ITSELF. With this set the
    // asset registry answers from what was baked in and from data: URLs, and
    // never from the disk - see assets.hpp. Off for an ordinary page, which is
    // opened from a directory and means to read it.
    bool sealed_assets = false;
    // Where a relative asset path (an <img src>, a page-local fetch) resolves
    // from when the registry misses. Empty means the working directory.
    std::filesystem::path asset_path;
    // And what a SERVER-ABSOLUTE one resolves against - a leading `/`. Empty
    // means the root of this filesystem, which is what it has always meant
    // here. A page written for a server names its shared resources that way:
    // `<script src="/resources/testharness.js">` is the first line of every
    // web-platform-test, and this is the document root that would have served
    // it. `CTBROWSER_DOC_ROOT` sets it; see shell::asset_registry.
    std::filesystem::path document_root;
    // Real outline fonts, from the vendored OFL faces and whatever the page's
    // @font-face rules ask for. ON by default - an application wants text that
    // looks like text - and falling back to the built-in bitmap font when
    // SDL3_ttf is absent or the files are not found.
    //
    // `CTBROWSER_FONTS=font8x8` forces the bitmap font, which is what makes a
    // run reproducible ACROSS MACHINES: two FreeType versions do not rasterize
    // identically, so a cross-platform byte comparison has to ask for font8x8.
    bool real_fonts = true;
    // EMPTY MEANS "wherever this build keeps them" - $CTBROWSER_FONT_PATH if it
    // is set, and `fonts` beside the executable otherwise, which is what
    // browser::use_real_fonts() resolves an empty directory to. Set this to
    // override both.
    std::filesystem::path font_path;

    // Whether fetch() may open a socket for a url the registry does not have.
    // On by default - it is a browser - and CTBROWSER_NETWORK=0 turns it off,
    // which is what makes an example's ctest hermetic.
    bool network = true;

    // THE ESCAPE HATCH. Called once with the native window handle - an
    // SDL_Window* - for callers who want to drive SDL themselves. Null on the
    // headless backend. Nothing else here mentions SDL, and a caller who does
    // not set this never learns it exists.
    std::function<void(void *)> on_native_window;

    // Called once before the first frame with the live browser, for
    // applications that want to inspect the document or drive it themselves.
    std::function<void(shell::browser &)> on_ready;

    // Called EVERY iteration, just after the window's events and before the
    // clock advances. `on_ready` fires once and the loop is otherwise closed,
    // so an application that wants to drive the page as it runs - replaying a
    // script, taking commands off a socket, which is what `ctdrive` does - had
    // nowhere to stand.
    //
    // Called on the LOOP'S OWN THREAD. That is the point rather than a
    // limitation: the page is ticked and drawn from this thread, so a browser
    // driven from any other while that happens is a data race. Handle input
    // here and there is nothing to synchronise.
    //
    // An empty hook costs one std::function check per iteration.
    std::function<void(shell::browser &)> on_frame;

    // A SCRIPT ERROR the page could not report for itself.
    //
    // Called once per DISTINCT message, with the loop still running - a page
    // that throws is not a page that should be torn down, which is what a
    // browser does and what `note_callback_fault` already implements one level
    // down.
    //
    // WITH NO HOOK THE MESSAGE GOES TO stderr: a page whose callbacks throw
    // looks frozen, and an engine that knows why a page stopped and does not
    // say so is worse than one that does not know.
    //
    // Set it to an empty function to silence the default without replacing it.
    std::function<void(const std::string & message)> on_script_error;

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
};

// Environment overrides, applied by run_app before anything else:
//
//   CTBROWSER_TEST_FRAMES  -> max_frames (and therefore a fixed timestep)
//   CTBROWSER_SCREENSHOT   -> screenshot_path
//   CTBROWSER_NETWORK      -> 0 disables fetch()'s network access
//   CTBROWSER_FONTS        -> font8x8 forces the built-in bitmap font
//   CTBROWSER_FONT_PATH    -> where the OFL faces are, when it is not `fonts`
//                             beside the executable. Read by
//                             browser::use_real_fonts(), not here: an empty
//                             font_path means "ask the build", and the build
//                             is what sets the variable - CTTest.cmake for the
//                             suite, examples/CMakeLists.txt for the examples,
//                             because in the source tree they live in
//                             ctbrowser/resources/fonts/.
//   CTBROWSER_DOC_ROOT     -> document_root, what a leading `/` in a
//                             resource name resolves against
//   CTBROWSER_MAX_FPS      -> max_fps, the redraw cap (0 = uncapped)
//
// This is what lets an example BE a ctest without the example containing any
// test scaffolding.
inline void apply_environment(app_options & options) {
    if (const char * frames = std::getenv("CTBROWSER_TEST_FRAMES")) {
        options.max_frames = std::atoi(frames);
    }
    if (const char * shot = std::getenv("CTBROWSER_SCREENSHOT")) { options.screenshot_path = shot; }
    if (const char * fonts = std::getenv("CTBROWSER_FONTS")) {
        const std::string_view text{fonts};
        options.real_fonts = !(text == "font8x8" || text == "bitmap" || text == "0");
    }
    if (const char * network = std::getenv("CTBROWSER_NETWORK")) {
        const std::string_view text{network};
        options.network = !(text == "0" || text == "off" || text == "no");
    }
    // THE DOCUMENT ROOT, for a page that was written to be SERVED. Unset for
    // every application in this tree; tools/wpt/run-wpt.py is the one caller,
    // and it is here rather than a flag because ctdrive parses four arguments
    // and this is a property of the corpus rather than of the driver.
    if (const char * root = std::getenv("CTBROWSER_DOC_ROOT")) { options.document_root = root; }
    if (const char * fps = std::getenv("CTBROWSER_MAX_FPS")) { options.max_fps = std::atoi(fps); }
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

// AND A PACKAGED APPLICATION: the page, its resources and its already-compiled
// scripts, all out of one blob of bytes.
//
// This is the other end of `ctcompile`. Everything it does could be done by a
// caller filling an `app_options` by hand - which is exactly why it is here
// once rather than in every launcher: a bundle whose script images were
// unpacked into `assets` instead of `script_images`, or seeded after the page
// rather than before it, would produce an application that runs correctly and
// slowly with nothing said.
//
// `overrides` is applied on top of what the bundle says, so a launcher can
// still force a window size or a screenshot for a test.
[[nodiscard]] int run_bundle(std::span<const std::byte> bytes, app_options overrides = {});

} // namespace ctbrowser
