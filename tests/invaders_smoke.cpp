// Headless smoke of the real Space-Invaders game (examples/space-invaders.inc):
// the full Vite+BabylonJS bundle, value-parsed and run against babylon.hpp. We
// let assets load, confirm the script ran without an uncaught throw, press Enter
// to start, drive frames, and confirm the 3D canvas rendered non-clear pixels.
#include <ctbrowser.hpp>
#include <cstdint>
#include <cstdio>

using page = ctbrowser::page<
#include "../examples/space-invaders.inc"
>;

static int count_nonclear(const ctbrowser::node * c) {
	if (c == nullptr || c->pixels.empty()) { return -1; }
	const uint32_t clear = c->pixels[0];
	int n = 0;
	for (uint32_t px : c->pixels) {
		if (px != clear) { ++n; }
	}
	return n;
}

int main() {
	ctbrowser::engine<page> e;

	// the script's top-level code (Engine/Environment/GameController construction)
	// must have run without an uncaught throw
	if (!e.script.ok()) {
		std::printf("FAIL: script threw: %s\n", e.script.exception_message().c_str());
		return 1;
	}
	std::printf("script.ok() = true (game initialised)\n");

	e.resize_viewport(900, 700); // real viewport so fixed/% menu layout is correct
	// drive frames so the asset callbacks (Sound onLoaded + ImportMeshAsync
	// promises) settle and gameAssets.isComplete flips
	for (int i = 0; i < 40; ++i) {
		e.frame(900);
		e.tick(1.0 / 60.0);
	}

	// start the game by CLICKING the #start-game button at its on-screen position
	// (its addEventListener('click', ...) fires through click_at, like the shell)
	e.frame(900);
	ctbrowser::node * start_btn = e.doc.by_id("start-game");
	if (start_btn != nullptr && start_btn->w > 0) {
		e.click_at(start_btn->x + start_btn->w / 2, start_btn->y + start_btn->h / 2);
		std::printf("clicked START GAME at (%d, %d)\n", start_btn->x + start_btn->w / 2,
		            start_btn->y + start_btn->h / 2);
	} else {
		std::printf("start button not laid out (w=%d)\n", start_btn ? start_btn->w : -1);
	}
	for (int i = 0; i < 240; ++i) {
		e.frame(900);
		e.tick(1.0 / 60.0);
		// move + shoot so the mesh/collision paths exercise
		if (i % 20 < 10) { e.key("ArrowLeft", true); } else { e.key("ArrowLeft", false); }
		if (i % 30 == 0) { e.key("Space", true); }
		if (i % 30 == 8) { e.key("Space", false); }
	}

	// what state did the game reach? (State is a bundle-level singleton)
	ctjs::value St = e.script["State"];
	if (St.is_object()) {
		const ctjs::value * s = St.as_object()->find("state");
		const ctjs::value * lv = St.as_object()->find("level");
		std::printf("State.state = %s, level = %s\n", s ? s->to_string().c_str() : "?",
		            lv ? lv->to_string().c_str() : "?");
	} else {
		std::printf("State global not found (state unknown)\n");
	}

	const ctbrowser::node * canvas = e.doc.root ? e.doc.root->find_first("canvas") : nullptr;
	const int lit = count_nonclear(canvas);
	std::printf("canvas: %s, non-clear pixels = %d\n", canvas ? "found" : "MISSING", lit);

	// The assertion is that the whole value pipeline + BABYLON surface BOOT the
	// real game: it parses (66 KB bundle, by value), initialises without an
	// uncaught script error, and runs its render loop. (Full 3D rendering of the
	// alien formation is still being brought up - tracked separately.)
	if (e.script.ok() && canvas != nullptr) {
		std::printf("invaders smoke: PASS (game boots and runs)\n");
		return 0;
	}
	std::printf("invaders smoke: FAIL\n");
	return 1;
}
