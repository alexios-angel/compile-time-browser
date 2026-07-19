// The game-engine proof: space invaders as ONE HTML string. Sprites
// come from a BMP sheet (drawImageRegion), sound from a WAV
// (playSound), movement from polled keys (isKeyDown), the HUD from
// fillText, and the whole 320x240 playfield scales to the window
// through SDL3's logical presentation - pixel-perfect, cross-platform.
// Left/Right move, Space shoots.
//
// Build: make invaders   (or the CMake examples; needs SDL3)
// Assets: python3 ../tools/gen-assets.py (checked in, deterministic)

#include <ctbrowser.hpp>
#include <ctbrowser/app.hpp>
#include <SDL3/SDL_main.h>

using app = ctbrowser::page<R"(<!DOCTYPE html>
<title>ctinvaders</title>
<style>
	body { padding: 0; margin: 0; }
</style>
<canvas id=game width=320 height=240></canvas>
<script>
	let ctx = getContext("game");
	let sheet = loadImage("examples/assets/sprites.bmp");
	let aliens = [];
	for (let r = 0; r < 3; r++) {
		for (let c = 0; c < 6; c++) {
			aliens.push({x: 20 + c * 40, y: 20 + r * 24, alive: true});
		}
	}
	let dir = 1;
	let px = 152;
	let bullet = null;
	let score = 0;
	let t = 0;

	function onKey(key, down) {
		if (down && key === "Space" && bullet === null) {
			bullet = {x: px + 7, y: 210};
			playSound("examples/assets/blip.wav");
		}
	}

	function onFrame(dt) {
		if (dt > 0.05) { dt = 0.05; }
		t += dt;
		let anim = Math.floor(t * 4) % 2;

		if (isKeyDown("Left")) { px -= 140 * dt; }
		if (isKeyDown("Right")) { px += 140 * dt; }
		if (px < 0) { px = 0; }
		if (px > 304) { px = 304; }

		let minx = 999;
		let maxx = -999;
		for (const a of aliens) {
			if (a.alive) {
				if (a.x < minx) { minx = a.x; }
				if (a.x > maxx) { maxx = a.x; }
			}
		}
		let step = 26 * dt * dir;
		if (maxx + step > 298 || minx + step < 6) {
			dir = -dir;
			for (const a of aliens) { a.y += 8; }
		} else {
			for (const a of aliens) { a.x += step; }
		}

		if (bullet !== null) {
			bullet.y -= 260 * dt;
			for (const a of aliens) {
				if (a.alive && bullet !== null &&
				    bullet.x >= a.x && bullet.x <= a.x + 16 &&
				    bullet.y >= a.y && bullet.y <= a.y + 16) {
					a.alive = false;
					bullet = null;
					score += 10;
					playSound("examples/assets/blip.wav");
				}
			}
			if (bullet !== null && bullet.y < -6) { bullet = null; }
		}

		ctx.fillStyle = "#0a0a1e";
		ctx.fillRect(0, 0, 320, 240);
		for (const a of aliens) {
			if (a.alive) {
				ctx.drawImageRegion(sheet, anim * 8, 0, 8, 8, a.x, a.y, 16, 16);
			}
		}
		ctx.drawImageRegion(sheet, 16, 0, 8, 8, px, 216, 16, 16);
		if (bullet !== null) {
			ctx.fillStyle = "#ffff00";
			ctx.fillRect(bullet.x, bullet.y, 2, 6);
		}
		ctx.fillStyle = "#ffffff";
		ctx.fillText("SCORE " + score, 4, 12);
	}
</script>)">;

static_assert(app::script_valid);

// OPPORTUNISTIC compile-time assets: the ENGINE reads the script's
// loadImage/playSound literals at compile time and, on a
// std::embed-capable compiler (tools/clang-std-embed), bakes those
// files into the binary itself (assets.hpp). All this TU contributes
// is the #depend gate the patched compiler requires before its
// constant evaluator may read files; other compilers skip the
// directive and fall back to runtime loading of the same files.
#if defined(__has_builtin)
#	if __has_builtin(__builtin_std_embed)
#		pragma clang diagnostic push
#		pragma clang diagnostic ignored "-Wc++2d-extensions"
#depend "examples/assets/**"
#		pragma clang diagnostic pop
#	endif
#endif

int main(int, char **) {
	ctbrowser::app_options o;
	o.width = 960;
	o.height = 720;
	o.logical_w = 320;
	o.logical_h = 240;
	return ctbrowser::run_app<app>(o);
}
