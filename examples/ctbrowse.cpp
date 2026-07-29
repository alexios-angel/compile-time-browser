// ctbrowse - open an HTML file.
//
//   ctbrowse page.html                    a window
//   ctbrowse page.html --frames 30 --shot out.ppm    render and exit
//   ctbrowse page.html --size 800 600 --software
//
// The whole program is argument parsing plus one call. That is the point: the
// engine's application API is `run_app_file`, and everything a browser needs to
// do - window, event loop, clock, frame pacing, screenshots, teardown - is
// behind it. There is no SDL header here.

#include <ctbrowser.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

int main(int argc, char ** argv) {
    std::string path;
    ctbrowser::app_options options;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--size" && i + 2 < argc) {
            options.width = std::atoi(argv[++i]);
            options.height = std::atoi(argv[++i]);
        } else if (arg == "--frames" && i + 1 < argc) {
            options.max_frames = std::atoi(argv[++i]);
        } else if (arg == "--shot" && i + 1 < argc) {
            options.screenshot_path = argv[++i];
        } else if (arg == "--software") {
            options.renderer = ctbrowser::renderer_preference::force_software;
        } else if (!arg.starts_with("--")) {
            path = arg;
        }
    }

    if (path.empty()) {
        std::printf("usage: ctbrowse <page.html> [--size W H] [--frames N] [--shot out.ppm]\n"
                    "                [--software]\n");
        return 2;
    }
    // Report a page's script error rather than rendering a silently broken
    // page. A browser that says nothing when the script failed is the single
    // most annoying thing to debug a page against.
    // The live browser, so a followed link can replace the document. run_app
    // owns it; this is the handle it hands out.
    ctbrowser::browser * live = nullptr;
    options.on_ready = [&live](ctbrowser::browser & page) {
        live = &page;
        if (!page.script_error().empty()) {
            std::printf("script error: %s\n", page.script_error().c_str());
        }
    };
    // AND EVERY FRAME, once each. A page's scripts can fail long after it
    // loads - a timer or an animation frame that throws is a script error too,
    // and on_ready has already run by the time the first frame is driven. A
    // sketch that loaded cleanly and then died on its first draw showed a blank
    // window and said nothing at all.
    //
    // From the frame hook rather than after run_app_file returns: the browser
    // is destroyed with the app, so reading it afterwards is a use-after-free -
    // one that printed a garbage string rather than crashing, which is worse.
    std::string reported;
    options.on_frame = [&reported](ctbrowser::browser & page) {
        if (page.script_error().empty() || page.script_error() == reported) { return; }
        reported = page.script_error();
        std::printf("script error: %s\n", reported.c_str());
    };
    // FOLLOW A LINK TO ANOTHER LOCAL PAGE. The engine hands out an href and
    // stops - it has no idea what a URL means. Deciding that a relative one
    // names a file next to the current page is a BROWSER's business, and this
    // program is the browser. Returning false for anything else hands it to
    // the system browser rather than swallowing it, which is what an http://
    // link in a page is for.
    options.on_navigate = [&path, &live](const std::string & href) {
        if (href.find("://") != std::string::npos) { return false; }
        namespace fs = std::filesystem;
        const fs::path target = fs::path{path}.parent_path() / href;
        std::ifstream in{target, std::ios::binary};
        if (!in || live == nullptr) {
            std::printf("cannot open %s\n", target.string().c_str());
            return true; // claimed, and reported - not the system browser's problem
        }
        const std::string html{std::istreambuf_iterator<char>{in},
                               std::istreambuf_iterator<char>{}};
        path = target.string(); // relative links on the NEW page resolve there
        live->assets().set_base_path(target.parent_path().string());
        live->load_html(html);
        if (!live->script_error().empty()) {
            std::printf("script error: %s\n", live->script_error().c_str());
        }
        return true;
    };
    return ctbrowser::run_app_file(path, std::move(options));
}
