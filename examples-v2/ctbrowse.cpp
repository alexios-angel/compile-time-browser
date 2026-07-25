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

import ctbrowser;

#include <cstdlib>
#include <cstdio>
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
	options.on_ready = [](ctbrowser::browser & page) {
		if (!page.script_error().empty()) {
			std::printf("script error: %s\n", page.script_error().c_str());
		}
	};
	return ctbrowser::run_app_file(path, std::move(options));
}
