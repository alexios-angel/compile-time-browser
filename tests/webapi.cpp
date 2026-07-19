// The library-boot proof: this script walks the exact path p5.js walks
// at startup - document.createElement("canvas"), setAttribute sizing,
// document.body.appendChild, getContext("2d"), transformed path
// drawing, window probing, a requestAnimationFrame loop - all through
// GENERAL web APIs (no library-specific bindings anywhere). Headless.
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

using app = ctbrowser::page<R"(<!DOCTYPE html>
<title>webapi</title>
<body>
<script>
	var canvas = document.createElement("canvas");
	canvas.setAttribute("width", "64");
	canvas.setAttribute("height", "48");
	canvas.setAttribute("id", "stage");
	document.body.appendChild(canvas);
	var ctx = canvas.getContext("2d");

	ctx.fillStyle = "#ff0000";
	ctx.translate(10, 10);
	ctx.beginPath();
	ctx.moveTo(0, 0);
	ctx.lineTo(20, 0);
	ctx.lineTo(20, 20);
	ctx.lineTo(0, 20);
	ctx.closePath();
	ctx.fill();

	ctx.resetTransform();
	ctx.strokeStyle = "#00ff00";
	ctx.lineWidth = 3;
	ctx.beginPath();
	ctx.moveTo(0, 40);
	ctx.lineTo(63, 40);
	ctx.stroke();

	ctx.fillStyle = "#0000ff";
	ctx.beginPath();
	ctx.arc(50, 10, 6, 0, Math.PI);
	ctx.fill();

	var frames = 0;
	function loop(ts) { frames = frames + 1; requestAnimationFrame(loop); }
	requestAnimationFrame(loop);
	var dpr = window.devicePixelRatio;

	// timers share the tick clock: 5 frames at 60fps = ~83ms
	var timeout_fired = false;
	var interval_count = 0;
	var cancelled_fired = false;
	setTimeout(() => { timeout_fired = true; }, 50);
	var iv = setInterval(() => {
		interval_count += 1;
		if (interval_count >= 3) { clearInterval(iv); }
	}, 20);
	var dead = setTimeout(() => { cancelled_fired = true; }, 10);
	clearTimeout(dead);
</script>
</body>)">;

int main() {
	ctbrowser::engine<app> e;
	if (!e.script.ok()) {
		std::printf("FAIL script: %s\n", e.script.exception_message().c_str());
		return 1;
	}

	// the created canvas is a real tree member with the requested size
	ctbrowser::node * stage = e.doc.by_id("stage");
	CHECK(stage != nullptr);
	if (stage == nullptr) { return 1; }
	CHECK(stage->parent == e.doc.body());
	CHECK(stage->canvas_w == 64 && stage->canvas_h == 48);
	const auto px = [stage](int x, int y) {
		return stage->pixels[static_cast<size_t>(y) * 64 + static_cast<size_t>(x)];
	};

	// translated path fill: square lives at device (10,10)-(30,30)
	CHECK(px(15, 15) == 0xFFFF0000u);
	CHECK(px(5, 5) != 0xFFFF0000u);
	// 3px-wide stroke along y=40
	CHECK(px(30, 40) == 0xFF00FF00u);
	CHECK(px(30, 35) != 0xFF00FF00u);
	// arc 0..PI = the LOWER half in canvas coords (+y down)
	CHECK(px(50, 13) == 0xFF0000FFu);
	CHECK(px(50, 5) != 0xFF0000FFu);

	// the rAF loop re-arms itself; window's clock ticks
	CHECK(e.ev.raf.size() == 1);
	for (int i = 0; i < 5; ++i) { e.tick(1.0 / 60.0); }
	CHECK(e.script["frames"].to_number() == 5);
	CHECK(e.ev.raf.size() == 1);
	CHECK(e.script["dpr"].to_number() == 1.0);

	// timers: the 50ms timeout fired, the 20ms interval ran exactly 3
	// times before clearing itself, the cancelled timeout never ran
	CHECK(e.script["timeout_fired"].to<bool>());
	CHECK(e.script["interval_count"].to_number() == 3);
	CHECK(!e.script["cancelled_fired"].to<bool>());
	CHECK(e.ev.timers.empty()); // nothing left armed

	if (failures == 0) { std::printf("webapi suite: all checks passed\n"); }
	return failures == 0 ? 0 : 1;
}
