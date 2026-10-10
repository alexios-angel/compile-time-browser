//===- ctcompile/test/Runtime/Launcher/Page.cpp - AOT page launcher -------===//
//
// A real page, running compiled bodies.
//
// Application.cpp runs a JavaScript program against a bare `script::context`,
// which is the smallest thing that can be called an application. This runs
// `ctbrowser/examples/pages/invaders.html` - a game, with a canvas, a sprite
// sheet, event listeners and requestAnimationFrame - through the whole engine,
// with every one of its four functions compiled ahead of time and installed by
// `browser::set_script_prepared_hook`.
//
// `set_script_prepared_hook` is the only way to reach a page's compiled
// programs: classic scripts are compiled inside `run_scripts` and
// `classic_programs_` is private, so the hook is where compiled bodies get
// installed.
//
// One source, two executables, as in Application.cpp: this file is compiled
// once alone and once with the generated bodies and their entry table, and
// page.test runs both and byte-compares the canvases they write.
//
// The canvas is the output, and it is the right one for this page. `invaders`
// draws nothing into the document - the whole game is `ctx.drawImage` and
// `ctx.fillRect` into a 320x240 canvas - so the pixels are where a difference
// between interpreted and compiled execution would show up. A DOM comparison
// would compare two identical documents.
//
// The clock is fixed. `frame(now)` computes `now - previous` and moves
// everything by it, so a wall clock would make the two builds disagree for
// reasons that have nothing to do with the compiler. Ticking a constant
// millisecond count is what `run_app`'s `fixed_dt` does for the same reason.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser.hpp>

#include <ctbrowser/script/dispatch.hpp>

#ifdef CTCOMPILE_LAUNCHER_AOT
#include <ctcompile/AOT/EntryTable.hpp>

extern const ctcompile::aot::entry_table CTCOMPILE_LAUNCHER_TABLE;
#endif

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>

#include "options.hpp"

using ctbrowser::script::transition;

namespace {

/// The first element with this id, in document order. Kept local so this file
/// does not depend on ctbrowser/test/support.
[[nodiscard]] ctbrowser::node_id find_id(ctbrowser::browser & page, std::string_view want) {
    const auto txn = page.doc().read();
    const ctbrowser::atom key = page.atoms().intern("id");
    ctbrowser::node_id found{};
    const auto walk = [&](auto && self, ctbrowser::node_id at) -> void {
        if (!found && txn.attribute_value(at, key) == want) { found = at; }
        for (const ctbrowser::node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

/// Prints every dispatch transition counter to stderr for page.test to check.
void report_transitions() {
    for (unsigned which = 0; which < static_cast<unsigned>(transition::count); ++which) {
        const auto one = static_cast<transition>(which);
        std::fprintf(stderr, "transition \"%s\" = %llu\n", ctbrowser::script::transition_name(one),
                     static_cast<unsigned long long>(ctbrowser::script::transitions(one)));
    }
}

} // namespace

/// The one option is <canvas-output>, a required bare argument: where the
/// canvas is written. Everything else is a build parameter:
/// CTCOMPILE_LAUNCHER_PAGE is the .html and CTCOMPILE_LAUNCHER_ASSET_ROOT the
/// directory its `examples/assets/sprites.bmp` resolves against.
int main(int argc, char ** argv) {
    namespace po = boost::program_options;
    std::string canvas_output;
    po::options_description options{"usage: launcher-page <canvas-output>"};
    options.add_options()("canvas-output", po::value(&canvas_output)->required(),
                          "where the canvas is written, as a PPM");
    po::positional_options_description positional;
    positional.add("canvas-output", 1);
    if (!ctbrowser_cli::parse_options(argc, argv, options, positional)) { return 2; }

    const std::filesystem::path page_path{CTCOMPILE_LAUNCHER_PAGE};
    std::string html;
    {
        std::ifstream in{page_path, std::ios::binary};
        html.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
    }
    if (html.empty()) {
        std::fprintf(stderr, "launcher-page: cannot read %s\n", page_path.string().c_str());
        return 1;
    }

    ctbrowser::browser page{ctbrowser::browser_options{320, 240}};
    page.assets().set_base_path(CTCOMPILE_LAUNCHER_ASSET_ROOT);

    // How many scripts were seen and how many were installed, counted here
    // rather than inferred, because a hook that never fires and a hook that
    // fires and installs nothing look the same from outside.
    unsigned scripts_seen = 0;
#ifdef CTCOMPILE_LAUNCHER_AOT
    unsigned installed_total = 0;
    std::string refusal;
    page.set_script_prepared_hook([&](ctbrowser::script::program & compiled, std::string_view) {
        ++scripts_seen;
        // Strict: a page whose program still holds an uncompiled function is
        // not an AOT-only page, and this records the refusal rather than
        // installing part of it. The load carries on so that the failure is
        // reported here with a reason instead of as a blank canvas.
        const ctcompile::aot::install_report report =
            ctcompile::aot::install_strict(compiled, CTCOMPILE_LAUNCHER_TABLE);
        if (!report.ok()) {
            if (refusal.empty()) { refusal = report.error; }
            return;
        }
        installed_total += static_cast<unsigned>(report.installed);
    });
#else
    page.set_script_prepared_hook(
        [&](ctbrowser::script::program &, std::string_view) { ++scripts_seen; });
#endif

    // After the hook is set and before anything runs. `install_builtins` and
    // the document's own bootstrap are not this application, and counting them
    // would make the numbers depend on what the engine does before a page.
    ctbrowser::script::reset_transitions();
    page.load_html(html);

#ifdef CTCOMPILE_LAUNCHER_AOT
    if (!refusal.empty()) {
        std::fprintf(stderr, "launcher-page: %s\n", refusal.c_str());
        return 1;
    }
    std::fprintf(stderr, "installed %u across %u script(s)\n", installed_total, scripts_seen);
#else
    std::fprintf(stderr, "installed 0 across %u script(s)\n", scripts_seen);
#endif
    if (scripts_seen != 1) {
        std::fprintf(stderr, "launcher-page: the page has %u classic script(s), expected 1\n",
                     scripts_seen);
        return 1;
    }
    if (!page.script_error().empty()) {
        std::fprintf(stderr, "launcher-page: the page reported %s\n", page.script_error().c_str());
        report_transitions();
        return 1;
    }

    // Twenty frames of a fixed sixteen milliseconds. Enough that the formation
    // has moved and the animation frame has flipped - the page's own
    // `Math.floor(clock * 4) % 2` changes at 0.25 s - and few enough that this
    // stays a test rather than a benchmark.
    constexpr int frames = 20;
    for (int i = 0; i < frames; ++i) {
        page.tick(16.0);
        (void)page.frame();
    }

    const ctbrowser::node_id canvas = find_id(page, "game");
    const auto pixels = page.canvases().pixels_of(canvas);
    if (!pixels) {
        std::fprintf(stderr, "launcher-page: the canvas has no pixels - the page did not draw\n");
        report_transitions();
        return 1;
    }

    // Count the colours, because two blank canvases compare equal. The page
    // fills its background every frame, so a run in which the game drew nothing
    // at all is one uniform colour, and a byte comparison of two of those would
    // pass without testing anything.
    {
        std::set<std::uint32_t> colours;
        for (int y = 0; y < pixels->height; ++y) {
            for (int x = 0; x < pixels->width; ++x) { colours.insert(pixels->at(x, y)); }
        }
        std::fprintf(stderr, "colours = %zu\n", colours.size());
    }

    // A PPM, because it is the format this repository already byte-compares and
    // because a person can look at a failure rather than only diff it.
    {
        std::ofstream out{canvas_output, std::ios::binary};
        out << "P6\n" << pixels->width << ' ' << pixels->height << "\n255\n";
        for (int y = 0; y < pixels->height; ++y) {
            for (int x = 0; x < pixels->width; ++x) {
                // ARGB in one word, which is what `bitmap` holds; the alpha is
                // dropped because a PPM has no channel for it and this canvas
                // is opaque - the page fills it every frame.
                const std::uint32_t argb = pixels->at(x, y);
                const char rgb[3] = {static_cast<char>((argb >> 16) & 0xFFu),
                                     static_cast<char>((argb >> 8) & 0xFFu),
                                     static_cast<char>(argb & 0xFFu)};
                out.write(rgb, 3);
            }
        }
    }

    report_transitions();
    return 0;
}
