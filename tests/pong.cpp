// The web-compat proof: the MDN breakout tutorial (examples/pong.html)
// runs UNMODIFIED. COMPILE TIME: the real .html file (via the generated
// raw-string .inc) parses into the page type - markup, stylesheet
// (universal selector), a ~4.8 KB script. RUNTIME: document.*,
// addEventListener + event objects, requestAnimationFrame, the canvas
// path API and font-scaled fillText all behave enough like a browser
// that the ball flies, the paddle obeys ArrowRight and mousemove, and
// the bricks are where MDN says they are. No SDL anywhere.
#include <ctbrowser.hpp>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++failures; \
		} \
	} while (0)

using pong = ctbrowser::page<
#include "../examples/pong.inc"
>;

static constexpr uint32_t BLUE = 0xFF0095DDu; // the tutorial's #0095DD

int main() {
	ctbrowser::engine<pong> e;
	if (!e.script.ok()) {
		std::printf("FAIL script: %s\n", e.script.exception_message().c_str());
		return 1;
	}
	CHECK(e.title == "Pong");

	ctbrowser::node * canvas = e.doc.by_id("myCanvas");
	CHECK(canvas != nullptr);
	if (canvas == nullptr) { return 1; }
	CHECK(canvas->canvas_w == 480 && canvas->canvas_h == 320);
	const auto px = [canvas](int x, int y) {
		return canvas->pixels[static_cast<size_t>(y) * 480 + static_cast<size_t>(x)];
	};

	// draw() ran once at script top level and re-registered itself
	CHECK(e.ev.raf.size() == 1);

	e.frame(480); // layout (also refreshes offsetLeft on the handle)

	// initial scene, straight from the tutorial's constants:
	CHECK(px(35, 35) == BLUE);    // first brick (30,30 75x20)
	CHECK(px(240, 315) == BLUE);  // paddle centered: x in [202,277], y in [310,320)
	CHECK(px(240, 290) == BLUE);  // ball at (width/2, height-30), r=10
	CHECK(px(470, 200) == 0u);    // empty area stays transparent
	bool score_text = false;      // "Score: 0" at baseline (8,20), 16px -> 2x font8x8
	for (int y = 4; y < 20 && !score_text; ++y) {
		for (int x = 8; x < 120 && !score_text; ++x) {
			if (px(x, y) == BLUE) { score_text = true; }
		}
	}
	CHECK(score_text);

	// 30 rAF frames: ball travels (+2,-2) per frame, nothing hit yet
	for (int i = 0; i < 30; ++i) { e.tick(1.0 / 60.0); }
	CHECK(px(300, 230) == BLUE); // ball now at (300,230)
	CHECK(px(240, 290) == 0u);   // old ball position cleared
	CHECK(e.ev.raf.size() == 1); // still re-registering every frame

	// hold ArrowRight (arrives as the SDL name "Right") for 10 frames.
	// draw() paints THEN moves, so the last rendered paddle sits at
	// 202.5 + 7*9 = 265.5, i.e. x in [265,340)
	e.key("Right", true);
	for (int i = 0; i < 10; ++i) { e.tick(1.0 / 60.0); }
	e.key("Right", false);
	CHECK(px(335, 315) == BLUE); // inside the moved paddle
	CHECK(px(205, 315) == 0u);   // old left edge vacated

	// mousemove overrides: clientX=100 -> paddleX = 100 - offsetLeft - 37.5
	e.mouse_move(100, 10);
	e.tick(1.0 / 60.0);
	CHECK(px(100, 315) == BLUE);
	CHECK(px(340, 315) == 0u);

	// no stray alerts, no reload requested
	CHECK(e.ev.alerts.empty());
	CHECK(!e.ev.reload);

	if (failures == 0) { std::printf("pong: all checks passed\n"); }
	return failures == 0 ? 0 : 1;
}
