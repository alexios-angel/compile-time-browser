// The rendering suite: does what the engine COMPUTES actually reach
// the SCREEN correctly? Runs the real SDL3 shell headless (dummy video
// driver, software renderer - deterministic output), captures the
// composed frame, and verifies it two ways:
//   1. pixel-sample asserts at known coordinates (robust, primary);
//   2. byte-exact golden compare against tests/golden/render.ppm
//      (regenerate with REGOLDEN=1 ./tests/render).
// A PNG of the same frame lands in tests/render-out.png for humans.
#include <ctbrowser.hpp>
#include <ctbrowser/app.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++failures; \
		} \
	} while (0)

using scene = ctbrowser::page<R"(<!DOCTYPE html>
<title>render test</title>
<style>
	body { padding: 0; margin: 0; font-size: 16px; }
	#red { background-color: #ff0000; width: 100px; height: 40px; }
</style>
<div id=red></div>
<canvas id=c width=64 height=64></canvas>
<script>
	let ctx = getContext("c");
	ctx.fillStyle = "#0000ff";
	ctx.fillRect(0, 0, 64, 64);
	ctx.fillStyle = "#ffaa33";
	ctx.fillCircle(32, 32, 10);
	ctx.fillStyle = "#ffffff";
	ctx.fillText("HI", 2, 2);
	let img = loadImage("examples/assets/sprites.bmp");
	ctx.drawImage(img, 40, 40);
</script>)">;

struct ppm {
	int w = 0, h = 0;
	std::vector<unsigned char> rgb;

	static ppm read(const char * path) {
		ppm out;
		std::ifstream in(path, std::ios::binary);
		std::string magic;
		in >> magic;
		if (magic != "P6") { return {}; }
		int maxval = 0;
		in >> out.w >> out.h >> maxval;
		in.get(); // the single whitespace after maxval
		out.rgb.resize(static_cast<size_t>(out.w) * static_cast<size_t>(out.h) * 3);
		in.read(reinterpret_cast<char *>(out.rgb.data()),
		        static_cast<std::streamsize>(out.rgb.size()));
		return in ? out : ppm{};
	}
	// (r, g, b) at a pixel
	std::array<int, 3> at(int x, int y) const {
		const size_t i =
		    (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3;
		return {rgb[i], rgb[i + 1], rgb[i + 2]};
	}
};

static bool rgb_is(const ppm & img, int x, int y, int r, int g, int b) {
	const auto p = img.at(x, y);
	return p[0] == r && p[1] == g && p[2] == b;
}

int main() {
	// headless by default; a caller may override to watch it render
	setenv("SDL_VIDEODRIVER", "dummy", 0);
	setenv("SDL_AUDIODRIVER", "dummy", 0);

	ctbrowser::app_options o;
	o.width = 400;
	o.height = 300;
	o.max_frames = 3;
	o.screenshot_path = "tests/render-out.ppm";
	CHECK(ctbrowser::run_app<scene>(o) == 0);

	// a PNG of the same deterministic frame, for human eyes and CI artifacts
	o.screenshot_path = "tests/render-out.png";
	CHECK(ctbrowser::run_app<scene>(o) == 0);

	const ppm img = ppm::read("tests/render-out.ppm");
	CHECK(img.w == 400 && img.h == 300);
	if (img.w == 400) {
		// the styled #red box: 100x40 at the top-left
		CHECK(rgb_is(img, 50, 20, 255, 0, 0));
		CHECK(rgb_is(img, 99, 39, 255, 0, 0));
		// page background beyond it
		CHECK(rgb_is(img, 200, 20, 255, 255, 255));
		// the canvas sits below the red box (y offset 40)
		CHECK(rgb_is(img, 60, 60, 0, 0, 255));           // blue fill (clear of the text)
		CHECK(rgb_is(img, 32, 32 + 40, 255, 170, 51));   // orange circle center
		CHECK(rgb_is(img, 42, 40 + 40, 102, 255, 102));  // alien sprite pixel (drawImage)
		// fillText left white pixels near the canvas top-left
		int text_px = 0;
		for (int y = 40 + 2; y < 40 + 10; ++y) {
			for (int x = 2; x < 18; ++x) {
				if (rgb_is(img, x, y, 255, 255, 255)) { ++text_px; }
			}
		}
		CHECK(text_px > 8);
	}

	// golden compare (byte-exact; the software renderer is deterministic)
	const char * golden_path = "tests/golden/render.ppm";
	if (std::getenv("REGOLDEN") != nullptr) {
		std::ifstream src("tests/render-out.ppm", std::ios::binary);
		std::ofstream dst(golden_path, std::ios::binary);
		dst << src.rdbuf();
		std::printf("golden regenerated: %s\n", golden_path);
	} else if (std::ifstream gold{golden_path, std::ios::binary}) {
		std::ifstream cur("tests/render-out.ppm", std::ios::binary);
		const std::string a((std::istreambuf_iterator<char>(gold)),
		                    std::istreambuf_iterator<char>());
		const std::string b((std::istreambuf_iterator<char>(cur)),
		                    std::istreambuf_iterator<char>());
		CHECK(a == b);
	} else {
		std::printf("note: no golden at %s (run REGOLDEN=1 ./tests/render)\n", golden_path);
	}

	if (failures == 0) { std::printf("render suite: all checks passed\n"); }
	return failures;
}
