// ctbrowse - the v2 browser.
//
// Loads an HTML file, lays it out, renders it and lets you scroll it. This is
// the first point at which v2 is a browser rather than a set of subsystems with
// tests, so it is also the first point at which the architecture has to hold up
// end to end: one renderer chosen at startup (GPU where there is one, software
// where there is not), a frame that skips whatever has not changed, and a
// scroll that only re-composites.
//
//   ctbrowse page.html                 open a window
//   ctbrowse page.html --headless out.ppm   render one frame and exit
//   ctbrowse page.html --software      force the CPU renderer
//
// --headless is not a test affordance bolted on; it is how the same engine runs
// with no display at all, which is what CI does.

import ctbrowser.core;
import ctbrowser.raster;
import ctbrowser.shell;
import ctbrowser.gpu;
import ctbrowser.app;

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;

namespace {

struct arguments {
	std::string path;
	std::string headless_output;
	bool headless = false;
	bool force_software = false;
	int width = 1024;
	int height = 768;
};

[[nodiscard]] arguments parse_arguments(int argc, char ** argv) {
	arguments out;
	for (int i = 1; i < argc; ++i) {
		const std::string_view arg{argv[i]};
		if (arg == "--headless" && i + 1 < argc) {
			out.headless = true;
			out.headless_output = argv[++i];
		} else if (arg == "--software") {
			out.force_software = true;
		} else if (arg == "--size" && i + 2 < argc) {
			out.width = std::atoi(argv[++i]);
			out.height = std::atoi(argv[++i]);
		} else if (!arg.starts_with("--")) {
			out.path = arg;
		}
	}
	return out;
}

[[nodiscard]] std::string read_file(const std::string & path) {
	std::ifstream in{path, std::ios::binary};
	if (!in) { return {}; }
	std::ostringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

bool write_ppm(const std::string & path, const raster::surface & image) {
	std::ofstream out{path, std::ios::binary};
	if (!out) { return false; }
	out << "P6\n" << image.width() << " " << image.height() << "\n255\n";
	for (int y = 0; y < image.height(); ++y) {
		for (int x = 0; x < image.width(); ++x) {
			const std::uint32_t p = image.row(y)[static_cast<std::size_t>(x)];
			const char rgb[3] = {static_cast<char>((p >> 16) & 0xFFu),
			                     static_cast<char>((p >> 8) & 0xFFu),
			                     static_cast<char>(p & 0xFFu)};
			out.write(rgb, 3);
		}
	}
	return true;
}

// Pick the renderer once, say what was picked, and never ask again.
void choose_renderer(shell::browser & page, const arguments & args, SDL_Window * window) {
	if (args.force_software) {
		std::printf("renderer: software (forced)\n");
		return; // the browser already starts on the software backend
	}
	const auto want = gpu::preference_from_environment();
	if (want == gpu::renderer_preference::force_software) {
		std::printf("renderer: software (CTBROWSER_RENDERER)\n");
		return;
	}
	auto choice = gpu::create_renderer(page.width(), page.height(), window,
	                                   raster::default_tile_extent, want);
	if (!choice.renderer) {
		std::printf("renderer: software (%s)\n", choice.fell_back_because.c_str());
		return;
	}
	std::printf("renderer: %s\n", std::string{choice.renderer.name()}.c_str());
	if (choice.software_adapter) {
		std::printf("  note: this adapter is a SOFTWARE implementation of the graphics API\n");
	}
	if (!choice.fell_back_because.empty()) {
		std::printf("  fell back: %s\n", choice.fell_back_because.c_str());
	}
	page.use_renderer(std::move(choice.renderer));
}

int run_headless(const arguments & args, const std::string & html) {
	shell::browser page{shell::browser_options{args.width, args.height}};
	page.load_html(html);
	// Software only: a headless run must produce pixels this process can WRITE,
	// and an on-screen GPU target belongs to the window system.
	scheduler pool;
	if (const auto drawn = page.frame(&pool); !drawn) {
		std::printf("render failed\n");
		return 1;
	}
	const auto image = page.read_pixels();
	if (!image) {
		std::printf("this renderer cannot read its own output back\n");
		return 1;
	}
	if (!write_ppm(args.headless_output, *image)) {
		std::printf("could not write %s\n", args.headless_output.c_str());
		return 1;
	}
	std::printf("%s: %dx%d, title '%s', content %.0fpx tall -> %s\n", args.path.c_str(),
	            image->width(), image->height(), std::string{page.title()}.c_str(),
	            static_cast<double>(page.content_height()), args.headless_output.c_str());
	return 0;
}

int run_windowed(const arguments & args, const std::string & html) {
	app::window_options window_options;
	window_options.title = "ctbrowse";
	window_options.width = args.width;
	window_options.height = args.height;
	app::window_ptr window = app::create_window(window_options);
	if (!window) {
		std::printf("could not create a window: %s\n", SDL_GetError());
		return 1;
	}

	shell::browser page{shell::browser_options{args.width, args.height}};
	page.load_html(html);
	choose_renderer(page, args, nullptr); // offscreen render, then blit - see below
	SDL_SetWindowTitle(window.get(),
	                   page.title().empty() ? "ctbrowse" : std::string{page.title()}.c_str());

	// The software presentation path. The GPU backend can own the swapchain
	// directly (pass the window to create_renderer), but then it presents and
	// this loop must not; keeping presentation in ONE place here is worth more
	// than the copy, and the copy is what the GPU path removes when it takes
	// the window.
	app::present_target target;
	target.renderer = SDL_CreateRenderer(window.get(), nullptr);

	scheduler pool;
	bool running = true;
	bool needs_frame = true;
	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) { running = false; }
			shell::input_event translated;
			if (app::translate(event, translated) && page.handle(translated)) { needs_frame = true; }
		}
		if (needs_frame) {
			if (const auto drawn = page.frame(&pool); !drawn) { running = false; }
			if (const auto image = page.read_pixels()) { (void)app::present_software(target, *image); }
			needs_frame = false;
		}
		SDL_Delay(8); // no vsync on this path; do not spin a core for nothing
	}

	if (target.texture != nullptr) { SDL_DestroyTexture(target.texture); }
	if (target.renderer != nullptr) { SDL_DestroyRenderer(target.renderer); }
	return 0;
}

} // namespace

int main(int argc, char ** argv) {
	const arguments args = parse_arguments(argc, argv);
	if (args.path.empty()) {
		std::printf("usage: ctbrowse <page.html> [--headless out.ppm] [--software]\n"
		            "                [--size W H]\n");
		return 2;
	}
	const std::string html = read_file(args.path);
	if (html.empty()) {
		std::printf("could not read %s\n", args.path.c_str());
		return 1;
	}

	if (args.headless && std::getenv("SDL_VIDEODRIVER") == nullptr) {
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
	}
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		// Headless still works: the engine needs no display, only the GPU
		// backend does. This is the fallback the whole renderer seam exists for.
		if (args.headless) {
			const int result = run_headless(args, html);
			return result;
		}
		std::printf("SDL video would not start: %s\n", SDL_GetError());
		return 1;
	}
	const int result = args.headless ? run_headless(args, html) : run_windowed(args, html);
	SDL_Quit();
	return result;
}
