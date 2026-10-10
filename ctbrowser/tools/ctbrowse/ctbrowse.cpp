//===- ctbrowser/tools/ctbrowse/ctbrowse.cpp - Open an HTML file ----------===//
//
//   ctbrowse page.html                    a window
//   ctbrowse page.html --frames 30 --shot out.ppm    render and exit
//   ctbrowse page.html --size 800 600
//
// The whole program is argument parsing plus one call. That is the point: the
// engine's application API is `run_app_file`, and everything a browser needs to
// do - window, event loop, clock, frame pacing, screenshots, teardown - is
// behind it. There is no SDL header here.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

#include "options.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char ** argv) {
    namespace po = boost::program_options;
    std::string path;
    std::vector<int> size; // --size W H
    ctbrowser::app_options options;

    po::options_description cli{
        "usage: ctbrowse <page.html> [--size W H] [--frames N] [--shot out.ppm]"};
    auto add = cli.add_options();
    add("page", po::value(&path)->required(), "the HTML file to open");
    add("size", new ctbrowser_cli::exactly<int>(&size, 2), "the window's width and height");
    add("frames", po::value(&options.max_frames), "stop after N frames, at a fixed timestep");
    add("shot", po::value(&options.screenshot_path),
        "with --frames, write the last frame to this PPM file");
    po::positional_options_description positional;
    positional.add("page", 1);
    if (!ctbrowser_cli::parse_options(argc, argv, cli, positional)) { return 2; }
    if (!size.empty()) {
        options.width = size[0];
        options.height = size[1];
    }
    // The live browser, so a followed link can replace the document. run_app
    // owns it; on_ready stores the handle it hands out.
    ctbrowser::browser * live = nullptr;
    // Print a load-time script error instead of rendering a silently broken
    // page: a browser that says nothing when the script failed is very hard to
    // debug a page against.
    options.on_ready = [&live](ctbrowser::browser & page) {
        live = &page;
        if (!page.script_error().empty()) {
            std::printf("script error: %s\n", page.script_error().c_str());
        }
        // A <link rel=stylesheet> that did not resolve, for the same reason and
        // once only: it is decided during the load and cannot change later.
        // Silence here is worse than for a script: an unstyled page still draws
        // its text, so it looks like a rendering bug rather than a missing file.
        if (!page.style_error().empty()) {
            std::printf("style error: %s\n", page.style_error().c_str());
        }
    };
    // Also check every frame, reporting each new error once. A page's scripts
    // can fail long after it loads (a timer or an animation frame that throws),
    // and on_ready has already run by the time the first frame is driven.
    //
    // This runs from the frame hook rather than after run_app_file returns:
    // the browser is destroyed with the app, so reading it afterwards would be
    // a use-after-free.
    std::string reported;
    options.on_frame = [&reported](ctbrowser::browser & page) {
        if (page.script_error().empty() || page.script_error() == reported) { return; }
        reported = page.script_error();
        std::printf("script error: %s\n", reported.c_str());
    };
    // Send clicked http(s) links to the system browser; this is an
    // interactive browser, so it opts in.
    options.open_external_links = true;
    // Assets are not confined to the page's directory: the user named a local
    // page to open, and the in-tree ones reach `../../vendor/` above it.
    options.confine_assets = false;
    // Follow a relative link to another local file. The engine only hands out
    // the href; deciding it names a file next to the current page is this
    // program's job. Returning false (any URL with a scheme) falls back to the
    // default policy above.
    options.on_navigate = [&path, &live](const std::string & href) {
        if (href.find("://") != std::string::npos) { return false; }
        namespace fs = std::filesystem;
        const fs::path target = fs::path{path}.parent_path() / href;
        std::ifstream in{target, std::ios::binary};
        if (!in || live == nullptr) {
            std::printf("cannot open %s\n", target.string().c_str());
            return true; // Claimed and reported; not the system browser's problem.
        }
        const std::string html{std::istreambuf_iterator<char>{in},
                               std::istreambuf_iterator<char>{}};
        path = target.string(); // Relative links on the new page resolve there.
        live->assets().set_base_path(target.parent_path().string());
        live->load_html(html);
        if (!live->script_error().empty()) {
            std::printf("script error: %s\n", live->script_error().c_str());
        }
        return true;
    };
    return ctbrowser::run_app_file(path, std::move(options));
}
