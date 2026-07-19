// The headless engine suite: one page exercises the whole pipeline.
// COMPILE TIME: the page parses (HTML), its <style> parses (CSS - the
// static_asserts on the sheet prove the extraction), its <script>
// parses (JS). RUNTIME: the DOM instantiates, the script runs against
// the DOM bindings, clicks mutate, ctcss restyles, layout reflows -
// no SDL anywhere.
#include <ctbrowser.hpp>
#include <cstdio>
#include <string>

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++failures; \
		} \
	} while (0)

using app = ctbrowser::page<R"(<!DOCTYPE html>
<title>engine test</title>
<style>
	body   { font-size: 16px; }
	h1     { font-size: 32px; color: #222222; }
	#count { color: gray; }
	#count.hot { color: #ff8800 !important; }
	.hidden { display: none; }
	#panel { background-color: #eeeeee; padding: 8px; width: 300px; }
</style>
<h1>Counter</h1>
<div id=panel>
	<p id=count>0</p>
	<p id=note class=hidden>secret</p>
</div>
<canvas id=game width=64 height=32></canvas>
<script>
	let clicks = 0;
	function onClick(id) {
		if (id === "count" || id === "panel") {
			clicks += 1;
			let el = getElementById("count");
			el.setText(String(clicks));
			if (clicks >= 2) { el.addClass("hot"); }
		}
		return clicks;
	}
	function onKey(key, down) {
		if (down && key === "H") { getElementById("note").toggleClass("hidden"); }
	}
	let ctx = getContext("game");
	ctx.fillStyle = "#0000ff";
	ctx.fillRect(0, 0, 64, 32);
	ctx.fillStyle = "#ff0000";
	ctx.fillRect(10, 10, 4, 4);
	function drawMore() {
		ctx.fillStyle = "#ffaa33";
		ctx.fillCircle(50, 16, 5);
		ctx.fillStyle = "#ffffff";
		ctx.fillText("HI", 0, 28); // y = BASELINE (DOM): glyphs land at y 20..27
		ctx.strokeStyle = "#00ff00";
		ctx.strokeRect(0, 0, 64, 32);
		ctx.clearRect(30, 0, 2, 2);
		let img = loadImage("examples/assets/sprites.bmp");
		ctx.drawImage(img, 20, 2);
		ctx.drawImageRegion(img, 16, 0, 8, 8, 0, 0, 8, 8);
		return img;
	}
	function pollLeft() { return isKeyDown("Left"); }
	function pollMouse() { return mouseX() + "," + mouseY() + "," + isMouseDown(); }
	function panelWidth() { return getElementById("panel").rect().w; }
	setTitle("booted");
	console.log("script ran");
</script>)">;

// --- the compile-time claims

// the whole page is valid (HTML + CSS + JS all parsed during this build)
static_assert(app::script_valid);
static_assert(app::title() == "engine test");
// the extracted stylesheet is a real ctcss sheet
static_assert(app::sheet_type::rule_count() == 6);
// initial styles resolve AT COMPILE TIME against the page structure
constexpr ctcss::element_ref count_chain[] = {
    {"html"}, {"body"}, {"div", "panel", ""}, {"p", "count", ""}};
static_assert(ctcss::query(app::sheet_type{}, count_chain, "color") == "gray");
constexpr ctcss::element_ref hot_chain[] = {
    {"html"}, {"body"}, {"div", "panel", ""}, {"p", "count", "hot"}};
static_assert(ctcss::query(app::sheet_type{}, hot_chain, "color") == "#ff8800");

int main() {
	ctbrowser::engine<app> e;

	// the script already ran: canvas painted, title changed, console captured
	CHECK(e.script.ok());
	CHECK(e.script.console() == std::string_view{"script ran\n"});
	CHECK(e.title == "booted");

	ctbrowser::node * game = e.doc.by_id("game");
	CHECK(game != nullptr && game->is_canvas());
	CHECK(game->canvas_w == 64 && game->canvas_h == 32);
	CHECK(game->pixels[0] == 0xFF0000FFu);                     // blue fill
	CHECK(game->pixels[11 * 64 + 11] == 0xFFFF0000u);          // red square
	CHECK(game->pixels[0] != game->pixels[11 * 64 + 11]);

	// initial layout: hidden element collapsed, panel styled
	(void)e.frame(800);
	ctbrowser::node * note = e.doc.by_id("note");
	ctbrowser::node * panel = e.doc.by_id("panel");
	CHECK(note != nullptr && note->w == 0 && note->h == 0);    // display:none
	CHECK(panel != nullptr && panel->w == 300);                // width:300px

	// click -> script mutates DOM -> restyle changes the computed color
	ctbrowser::node * count = e.doc.by_id("count");
	CHECK(count != nullptr);
	const auto count_color = [&] {
		const auto chain = count->chain();
		return std::string{e.resolve(chain.data(), chain.size(), "color")};
	};
	CHECK(count->text == "0");
	CHECK(count_color() == "gray");

	e.click_at(count->x + 1, count->y + 1);
	CHECK(count->text == "1");
	CHECK(count_color() == "gray");

	e.click_at(count->x + 1, count->y + 1);
	CHECK(count->text == "2");
	CHECK(count_color() == "#ff8800"); // .hot kicked in, !important and all

	// key event un-hides the note; reflow gives it a box
	e.key("H", true);
	(void)e.frame(800);
	CHECK(note->h > 0);

	// --- v0.2 engine surface

	// canvas 2D upgrades + sprites, all headless (plain pixel buffers)
	ctjs::value img = e.script.call("drawMore");
	CHECK(img.to_number() >= 0); // the sprite sheet loaded
	const auto px = [&](int x, int y) {
		return game->pixels[static_cast<size_t>(y) * 64 + static_cast<size_t>(x)];
	};
	CHECK(px(50, 16) == 0xFFFFAA33u);        // fillCircle center, orange
	CHECK(px(0, 31) == 0xFF00FF00u);         // strokeRect bottom-left, green
	CHECK(px(30, 0) == 0x00000000u);         // clearRect -> transparent
	CHECK(px(22, 2) == 0xFF66FF66u);         // alien sprite pixel via drawImage
	CHECK(px(3, 0) == 0xFFFFAA33u);          // ship pixel via drawImageRegion
	{
		int text_px = 0; // fillText("HI") left a white glyph trail
		for (int y = 20; y < 28; ++y) {
			for (int x = 0; x < 16; ++x) {
				if (px(x, y) == 0xFFFFFFFFu) { ++text_px; }
			}
		}
		CHECK(text_px > 8);
	}

	// input polling state
	CHECK(!e.script.call("pollLeft").truthy());
	e.key("Left", true);
	CHECK(e.script.call("pollLeft").truthy());
	e.key("Left", false);
	CHECK(!e.script.call("pollLeft").truthy());
	e.mouse_move(5, 7);
	e.mouse_button(5, 7, true);
	CHECK(e.script.call("pollMouse").to<std::string>() == "5,7,true");
	e.mouse_button(5, 7, false);

	// element .rect() reads live layout
	CHECK(e.script.call("panelWidth").to<int>() == 300);

	// inline styles from script win over the sheet
	CHECK(e.script["onClick"].is_function());
	ctbrowser::node * h1 = e.doc.root->find_first("h1");
	CHECK(h1 != nullptr);
	h1->inline_style["color"] = "red";
	{
		ctbrowser::computed_style cs{h1, &e.resolve, h1->chain()};
		CHECK(cs.get("color") == std::string_view{"red"});
		CHECK(cs.px("font-size", 0) == 32);
	}

	if (failures == 0) { std::printf("engine suite: all checks passed\n"); }
	return failures;
}
