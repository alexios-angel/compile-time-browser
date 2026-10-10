//===- app.hpp - Application shell ---------------------------- -*- C++ -*-===//
//
// The application shell: a window, an event loop, and one function to call
// (run_app, run_app_file or run_bundle), configured through app_options.
//
// No SDL type appears in this interface, and <SDL3/SDL.h> is not included here:
// only lib/App/app/internal.hpp includes it, behind `#if CTBROWSER_WITH_SDL3`,
// so the library builds either way (with a window when SDL3 was found, headless
// when it was not).
//
//===----------------------------------------------------------------------===//

#pragma once
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <span>
// <ostream> must be included explicitly. `out << "P6\n"` needs the
// const char* inserter; without it the const void* one is chosen, which
// compiles and writes the pointer as hex into the file. libstdc++ makes it
// visible transitively but libc++ does not.
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>

namespace ctbrowser {

/// A file baked into the application that the page can reach by name. An asset
/// lookup checks these before it touches the filesystem.
struct asset {
    std::string name;
    std::vector<std::byte> bytes;
};

/// What a link that leaves the page may do. The page chooses the href and can
/// click it from script, so handing it unchecked to the operating system's URL
/// opener (ShellExecute, xdg-open) would let a shipped game open file:, UNC or
/// custom-scheme targets. Nothing leaves unless the application opts in.
enum class navigate_verdict {
    deny,
    handled,
    open_external
};

/// A request to follow a link out of the current document.
struct navigate_request {
    std::string url;     ///< Resolved against the document.
    std::string href;    ///< The author's text.
    bool user_activated; ///< A real input gesture was behind it (and was consumed).
};

/// The default policy: open_external only for an http(s) URL (judged by the
/// resolved address's scheme, ASCII case-folded) that a user gesture asked for.
/// Everything else (javascript:, data:, file:, UNC, drive letters, relative,
/// custom schemes) is denied, as is any URL containing a control byte, space or
/// DEL.
[[nodiscard]] inline navigate_verdict default_navigate_verdict(const navigate_request & request) {
    if (!request.user_activated) { return navigate_verdict::deny; }
    const std::string_view url = request.url;
    for (const char c : url) {
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f) { return navigate_verdict::deny; }
    }
    const auto colon = url.find("://");
    if (colon == std::string_view::npos) { return navigate_verdict::deny; }
    const std::string_view scheme = url.substr(0, colon);
    return ascii_iequals(scheme, "http") || ascii_iequals(scheme, "https")
               ? navigate_verdict::open_external
               : navigate_verdict::deny;
}

/// Everything run_app needs to know about the window, the loop and what the
/// page may do. apply_environment() can override some fields.
struct app_options {
    std::string title = "ctbrowser";

    /// Parse the source as XML, not HTML. `run_app_file` sets it from the
    /// extension (`.xhtml`, `.xht`, `.xml`), because a file on disk has no
    /// content type. Never sniffed from the bytes: `<?xml ...?>` is optional in
    /// XML 1.0 and appears in documents served as text/html. See dom/xml.hpp
    /// for where the two parsers differ.
    bool xml = false;
    int width = 1024;
    int height = 768;

    /// When >0, render at this fixed resolution and letterbox it into the
    /// window. Fixed-resolution pages (games) want it; documents do not.
    int logical_width = 0;
    int logical_height = 0;

    /// When >0, stop after this many frames. This is how a page becomes a
    /// test; apply_environment() then forces a fixed timestep so the run is
    /// reproducible.
    int max_frames = 0;
    /// The CPU throttle. An application redraws at most this often while
    /// something is changing, and not at all while nothing is (an idle page
    /// blocks on the event queue). So a busy page costs roughly this number
    /// times the cost of one frame.
    ///
    /// 0 means uncapped, for benchmarks and games with their own pacing.
    /// Fixed-step pages depend on the cap. `CTBROWSER_MAX_FPS` sets it.
    int max_fps = 60;
    double fixed_dt = 0; ///< When >0, pretend every frame took exactly this long (seconds).
    /// How long an idle application blocks before checking again. Not forever:
    /// that would need every source of change to be an SDL event, and the
    /// browser has its own clock. A driver whose commands arrive on a socket
    /// (no event at all) wants this short. `CTBROWSER_IDLE_WAIT_MS` sets it.
    int idle_wait_ms = 250;

    /// Half the caret's blink period, in milliseconds; 0 holds it solid.
    /// Chrome's 500 by default. Screenshot tests want 0, or a caret present in
    /// one run and absent in the next makes otherwise identical images differ.
    double caret_blink_ms = 500;

    std::string screenshot_path; ///< Written on the last frame; "" means never.

    std::vector<asset> assets; ///< Files baked into the application.
    /// Precompiled scripts, baked in like assets. Each entry is one program
    /// image (the bytes `script::write_image` produced for one classic
    /// <script>). The page matches them to its own scripts by source hash, so
    /// order does not matter and an image for a script the page lacks is never
    /// used.
    ///
    /// This lets a packaged application start without parsing its own
    /// JavaScript, about forty percent of a page load
    /// (ctcompile/docs/baseline/page-load.json: 69.65 ms to 19.93 on p5-basic).
    std::vector<std::vector<std::byte>> script_images;
    /// Turns a packaging mistake into an error instead of a silently slow
    /// start. An image from another engine build is refused, and one that
    /// matches no script is never used; both leave the application working but
    /// slow. With this set, `run_app` refuses to run an application whose
    /// scripts did not all come from images. Off by default, because an
    /// embedder may reasonably supply a partial set.
    bool require_script_images = false;

    /// When set, the asset registry answers only from baked-in assets and
    /// data: URLs, never from the disk (see shell/page/assets.hpp), as a
    /// packaged application wants. Off for an ordinary page opened from a
    /// directory.
    bool sealed_assets = false;
    /// Where a relative asset path (an <img src>, a page-local fetch) resolves
    /// from when the registry misses. Empty means the working directory.
    std::filesystem::path asset_path;
    /// What a server-absolute path (leading `/`) resolves against. Empty means
    /// the filesystem root. Pages written for a server use such paths, e.g.
    /// `<script src="/resources/testharness.js">` in every web-platform-test;
    /// this is the document root that would have served them.
    /// `CTBROWSER_DOC_ROOT` sets it; see shell::asset_registry.
    std::filesystem::path document_root;
    /// Real outline fonts, from the vendored OFL faces and the page's
    /// @font-face rules. On by default; falls back to the built-in bitmap font
    /// when SDL3_ttf is absent or the files are not found.
    ///
    /// `CTBROWSER_FONTS=font8x8` forces the bitmap font, which makes a run
    /// reproducible across machines: two FreeType versions do not rasterise
    /// identically, so a cross-platform byte comparison must use font8x8.
    bool real_fonts = true;
    /// Directory of font faces. Empty means $CTBROWSER_FONT_PATH if set, else
    /// `fonts` beside the executable (browser::use_real_fonts() resolves it).
    /// Set this to override both.
    std::filesystem::path font_path;

    /// Whether fetch() may open a socket for a URL the registry does not have.
    /// On by default; CTBROWSER_NETWORK=0 turns it off, which makes an
    /// example's ctest hermetic.
    bool network = true;
    /// Passed to shell::browser_options::on_request; ctdrive uses it to
    /// emulate WPT's server-side handlers.
    shell::request_handler on_request{};

    /// Called once before the first frame with the live browser, for
    /// applications that want to inspect the document or drive it themselves.
    std::function<void(shell::browser &)> on_ready;

    /// Called every iteration, after the window's events and before the clock
    /// advances, so an application can drive the page as it runs (replaying a
    /// script, taking commands off a socket, as ctdrive does).
    ///
    /// Called on the loop's own thread, which also ticks and draws the page;
    /// driving the browser from any other thread would be a data race.
    ///
    /// An empty hook costs one std::function check per iteration.
    std::function<void(shell::browser &)> on_frame;

    /// A script error the page could not report for itself.
    ///
    /// Called once per distinct message, with the loop still running: like a
    /// browser, the engine does not tear down a page that throws
    /// (`note_callback_fault` implements this one level down).
    ///
    /// When unset, the message goes to stderr, since a page whose callbacks
    /// throw otherwise just looks frozen. Set it to an empty function to
    /// silence the default without replacing it.
    std::function<void(const std::string & message)> on_script_error;

    /// A link the page followed that leaves this document. navigate_policy,
    /// when set, decides; otherwise on_navigate (given the raw href) returning
    /// true means the application handled it (ctbrowse loads a local .html this
    /// way), and false falls through to default_navigate_verdict.
    ///
    /// Use these hooks, not browser::set_navigate_hook, which replaces this
    /// wiring and so would swallow every external link.
    std::function<navigate_verdict(const navigate_request &)> navigate_policy;
    std::function<bool(const std::string & href)> on_navigate;
    /// The system browser is reached only when this is true and the verdict is
    /// open_external. Off by default: a sealed game never hands a URL to the OS.
    bool open_external_links = false;

    /// Where an `<a download>` (p5's save()) may write. Unset by default, which
    /// disables the write: the export is still recorded but no file appears, so
    /// a page cannot drop version.dll into the game's own directory. Set it to a
    /// folder the application owns (a saves directory, never the install one).
    std::optional<std::filesystem::path> download_directory;
    /// The largest single download written (64 MiB); a bigger one is dropped.
    std::size_t max_download_bytes = std::size_t{64} << 20;

    /// Whether run_app reads the CTBROWSER_* variables (apply_environment). On
    /// for an application built from source, which is how an example becomes a
    /// ctest. Off for a packaged one unless it was built with
    /// `ctcompile --allow-env-overrides` (run_bundle reads that from the
    /// bundle); otherwise CTBROWSER_TEST_FRAMES would be a speedhack,
    /// CTBROWSER_MAX_FPS=0 would lift the frame cap and CTBROWSER_NETWORK could
    /// re-enable a sealed game's network. Hygiene, not a security boundary: the
    /// player owns the binary.
    bool environment_overrides = true;
    /// Date.now() pinned to this many milliseconds since the epoch, for a golden
    /// that draws a date. Unset means the real clock. `CTBROWSER_CLOCK` sets it.
    std::optional<double> fixed_clock;

    /// Confined assets: the filesystem answers only from inside asset_path,
    /// document_root and the font directory, never `/etc/passwd`, `../..` or a
    /// symlink out (shell::asset_registry::set_confine). Turn off only for
    /// trusted local pages, like the in-tree examples that reach `../../vendor/`.
    bool confine_assets = true;
    /// What fetch()/XHR may reach: https and public addresses unless widened.
    shell::fetch_policy network_policy{};
};

/// What run_app does with a link that leaves the page: open_external only when
/// the application enabled it. A free function so it is testable without a
/// window.
[[nodiscard]] inline navigate_verdict navigate_verdict_for(const app_options & options,
                                                           const navigate_request & request) {
    navigate_verdict verdict = navigate_verdict::deny;
    if (options.navigate_policy) {
        verdict = options.navigate_policy(request);
    } else if (options.on_navigate && options.on_navigate(request.href)) {
        verdict = navigate_verdict::handled;
    } else {
        verdict = default_navigate_verdict(request);
    }
    if (verdict == navigate_verdict::open_external && !options.open_external_links) {
        return navigate_verdict::deny;
    }
    return verdict;
}

/// Environment overrides, applied by run_app before anything else, and only
/// when options.environment_overrides is set (a packaged bundle turns it off):
///
///   CTBROWSER_TEST_FRAMES  -> max_frames (and therefore a fixed timestep)
///   CTBROWSER_SCREENSHOT   -> screenshot_path
///   CTBROWSER_NETWORK      -> 0 disables fetch()'s network access
///   CTBROWSER_FONTS        -> font8x8 forces the built-in bitmap font
///   CTBROWSER_FONT_PATH    -> where the OFL faces are, when not in `fonts`
///                             beside the executable. Read by
///                             browser::use_real_fonts(), not here. The build
///                             sets it (CTTest.cmake for the suite,
///                             examples/CMakeLists.txt for the examples) because
///                             in the source tree the faces live in
///                             ctbrowser/resources/fonts/.
///   CTBROWSER_DOC_ROOT     -> document_root, what a leading `/` in a
///                             resource name resolves against
///   CTBROWSER_MAX_FPS      -> max_fps, the redraw cap (0 = uncapped)
///   CTBROWSER_IDLE_WAIT_MS -> idle_wait_ms, the longest an idle loop blocks
///   CTBROWSER_CLOCK        -> fixed_clock, Date.now() in epoch milliseconds
///   CTBROWSER_CONFINE_ASSETS -> 0 turns confine_assets off (trusted local pages)
///
/// This lets an example be a ctest without containing any test scaffolding.
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
    // The document root, for a page written to be served. Only
    // tools/wpt/run-wpt.py sets it. It is an environment variable rather than
    // a ctdrive flag because it is a property of the corpus, not the driver.
    if (const char * root = std::getenv("CTBROWSER_DOC_ROOT")) { options.document_root = root; }
    if (const char * confine = std::getenv("CTBROWSER_CONFINE_ASSETS")) {
        options.confine_assets = std::string_view{confine} != "0";
    }
    if (const char * fps = std::getenv("CTBROWSER_MAX_FPS")) { options.max_fps = std::atoi(fps); }
    if (const char * wait = std::getenv("CTBROWSER_IDLE_WAIT_MS")) {
        options.idle_wait_ms = std::max(1, std::atoi(wait));
    }
    if (const char * pinned = std::getenv("CTBROWSER_CLOCK")) {
        // from_chars rather than strtod: this pins the clock so a golden can
        // draw a date, and a value that parsed differently under another
        // locale would move the very render it exists to hold still.
        double fixed = 0.0;
        const std::string_view text{pinned};
        std::from_chars(text.data(), text.data() + text.size(), fixed);
        options.fixed_clock = fixed;
    }
    // A bounded run must be reproducible, or comparing its screenshot is
    // meaningless.
    if (options.max_frames > 0 && options.fixed_dt <= 0) { options.fixed_dt = 1.0 / 60.0; }
}

/// Writes a composited image as a binary PPM; returns false on an I/O error.
/// Deliberately not PNG: PPM needs no encoder, and a golden that a test
/// byte-compares gains nothing from compression.
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

/// Runs a page given as HTML source. Returns a process exit code.
///
/// This is the whole application API: it owns the window, the event loop, the
/// clock, the frame pacing and the teardown.
[[nodiscard]] int run_app(std::string_view html, app_options options = {});

/// Runs a page loaded from a file. Its images and page-local fetches resolve
/// next to the file, which is what `<img src="cat.bmp">` beside the html
/// means.
[[nodiscard]] int run_app_file(const std::filesystem::path & path, app_options options = {});

/// Runs a packaged application: the page, its resources and its precompiled
/// scripts, all from one blob of bytes produced by `ctcompile`.
///
/// A caller could do this by filling an `app_options` by hand, but doing it
/// once here avoids launchers that unpack script images into `assets` instead
/// of `script_images`, or seed them after the page; either produces an
/// application that runs correctly but slowly with nothing reported.
///
/// `overrides` is applied on top of the bundle's settings, so a launcher can
/// still force a window size or a screenshot for a test. The exception is
/// environment_overrides, which the bundle decides: it is on when the bundle's
/// "env_overrides" meta is "1" (`ctcompile --allow-env-overrides`) or when
/// `trust_environment` is set, which `ctrun app.ctapp` passes for a loose
/// bundle on its command line.
[[nodiscard]] int run_bundle(std::span<const std::byte> bytes, app_options overrides = {},
                             bool trust_environment = false);

} // namespace ctbrowser
