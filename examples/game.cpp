// The games-and-emulators proof: a <canvas> driven by page JavaScript.
// The script was parsed at compile time; at runtime onFrame(dt) runs
// as optimizer-specialized code, draws into the canvas pixel buffer,
// and SDL3 streams it to the GPU. Arrow keys move the paddle.
//
// Build: make game   (or the CMake examples; needs SDL3)

#include <ctbrowser.hpp>
#include <ctbrowser/app.hpp>
#include <SDL3/SDL_main.h>

using app = ctbrowser::page<R"(<!DOCTYPE html>
<title>ctpong</title>
<style>
	body   { padding: 12px; font-size: 16px; }
	h1     { font-size: 24px; color: #222222; }
	#score { color: #ff8800; }
	canvas { margin: 8px; }
</style>
<h1>ctpong</h1>
<p id=score>score 0</p>
<canvas id=game width=320 height=200></canvas>
<p>left/right arrows move the paddle</p>
<script>
	let ctx = getContext("game");
	let x = 160; let y = 60;
	let vx = 120; let vy = 95;
	let paddle = 130; let held = 0;
	let score = 0; let best = 0;

	function onKey(key, down) {
		if (key === "Left") { held = down ? -1 : 0; }
		if (key === "Right") { held = down ? 1 : 0; }
	}

	function onFrame(dt) {
		if (dt > 0.1) { dt = 0.1; }
		paddle += held * 220 * dt;
		if (paddle < 0) { paddle = 0; }
		if (paddle > 260) { paddle = 260; }

		x += vx * dt;
		y += vy * dt;
		if (x < 4) { x = 4; vx = -vx; }
		if (x > 316) { x = 316; vx = -vx; }
		if (y < 4) { y = 4; vy = -vy; }
		if (y >= 186 && y <= 196 && vy > 0 && x >= paddle && x <= paddle + 60) {
			y = 186; vy = -vy;
			vx *= 1.05; vy *= 1.05;
			score += 1;
			if (score > best) { best = score; }
			getElementById("score").setText("score " + score + "  best " + best);
		}
		if (y > 210) {
			x = 160; y = 60; vx = 120; vy = 95;
			score = 0;
			getElementById("score").setText("score 0  best " + best);
		}

		ctx.fillStyle = "#102030";
		ctx.fillRect(0, 0, 320, 200);
		ctx.fillStyle = "#ffffff";
		ctx.fillRect(0, 0, 320, 2);
		ctx.fillStyle = "#ff8800";
		ctx.fillRect(x - 4, y - 4, 8, 8);
		ctx.fillStyle = "#00cc66";
		ctx.fillRect(paddle, 190, 60, 6);
	}
</script>)">;

static_assert(app::script_valid);

int main(int, char **) {
	return ctbrowser::run_app<app>();
}
