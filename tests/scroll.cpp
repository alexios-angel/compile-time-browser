// Scrolling and reflow: wheel/keyboard page scrolling with clamping,
// position:fixed exemption, Firefox-style scrollbar-less textarea
// scrolling with caret-follow, and resize-driven rewrap (words keep
// their size; the layout changes). Headless.

#include <ctbrowser.hpp>
#include <cstdio>
#include <string>

static int fails = 0;
#define CHECK(cond)                                                    \
	do {                                                               \
		if (!(cond)) {                                                 \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++fails;                                                   \
		}                                                              \
	} while (0)

using page = ctbrowser::page<R"(<!DOCTYPE html>
<title>scroll</title>
<style>
	body { margin: 0 }
	.blk { height: 100px; margin: 0 }
	#pin { position: fixed; top: 5px; left: 5px; width: 50px; height: 20px; background-color: #333333 }
	#wrapme { font-size: 16px }
</style>
<div id=pin>pinned</div>
<div class=blk id=b1>one</div>
<div class=blk id=b2>two</div>
<div class=blk id=b3>three</div>
<div class=blk id=b4>four</div>
<div class=blk id=b5>five</div>
<div class=blk id=b6>six</div>
<p id=wrapme>To be or not to be that is the question whether tis
nobler in the mind to suffer the slings and arrows of outrageous
fortune</p>
<textarea id=ta rows=3 cols=12>l1
l2
l3
l4
l5
l6
l7
l8</textarea>
<hr id=rule>
<script>
	let wheels = 0, last_dy = 0;
	document.addEventListener("wheel", function (e) { wheels += 1; last_dy = e.deltaY; });
</script>)">;

int main() {
	ctbrowser::engine<page> e;
	CHECK(e.script.ok());
	e.resize_viewport(400, 300); // 300 tall viewport; the page is much taller
	e.frame(400);

	ctbrowser::node * b1 = e.doc.by_id("b1");
	ctbrowser::node * pin = e.doc.by_id("pin");
	CHECK(b1 && pin);
	const std::int32_t b1_y0 = b1->y;
	const std::int32_t pin_y0 = pin->y;

	// --- wheel down scrolls the page: content shifts up, fixed stays
	e.wheel(200, 200, -1.0); // SDL: y < 0 = scroll down
	e.frame(400);
	CHECK(e.scroll_y() == 48);
	CHECK(b1->y == b1_y0 - 48);
	CHECK(pin->y == pin_y0); // position:fixed is viewport-anchored
	CHECK(e.script["wheels"].to<int>() == 1);
	CHECK(e.script["last_dy"].to<int>() == 100); // DOM deltaY > 0 = down

	// --- wheel up clamps at the top
	e.wheel(200, 200, 5.0);
	e.frame(400);
	CHECK(e.scroll_y() == 0);
	CHECK(b1->y == b1_y0);

	// --- End clamps to the bottom, Home returns
	e.key("End", true);
	e.key("End", false);
	e.frame(400);
	CHECK(e.scroll_y() > 0);
	const std::int32_t at_end = e.scroll_y();
	e.key("PageDown", true); // already at the end: clamp holds
	e.key("PageDown", false);
	e.frame(400);
	CHECK(e.scroll_y() == at_end);
	e.key("Home", true);
	e.key("Home", false);
	e.frame(400);
	CHECK(e.scroll_y() == 0);
	e.key("PageDown", true);
	e.key("PageDown", false);
	e.frame(400);
	CHECK(e.scroll_y() == 300 - 48);

	// --- clicks land on the right element while scrolled (rects shifted)
	{
		ctbrowser::node * b3 = e.doc.by_id("b3");
		ctbrowser::node * hit = e.doc.root->hit_test(10, b3->y + 5);
		bool inside = false;
		for (ctbrowser::node * n = hit; n != nullptr; n = n->parent) {
			if (n == b3) { inside = true; }
		}
		CHECK(inside);
	}
	e.key("Home", true);
	e.key("Home", false);
	e.frame(400);

	// --- textarea: wheel over it scrolls IT, not the page (no scrollbar)
	ctbrowser::node * ta = e.doc.by_id("ta");
	e.key("End", true); // bring the textarea into view
	e.key("End", false);
	e.frame(400);
	CHECK(ta->y >= 0 && ta->y < 300);
	const std::int32_t page_scroll = e.scroll_y();
	e.wheel(ta->x + 5, ta->y + 5, -1.0);
	e.frame(400);
	CHECK(e.scroll_y() == page_scroll); // the page did not move
	CHECK(ta->scroll_top == 48);
	e.wheel(ta->x + 5, ta->y + 5, 9.0); // scroll it far up: clamps at 0
	e.frame(400);
	CHECK(ta->scroll_top == 0);

	// --- caret-follow: editing at the end brings the caret into view
	{
		const double x = ta->x + 5.0, y = ta->y + 5.0;
		e.mouse_button(x, y, true);
		e.mouse_button(x, y, false);
	}
	CHECK(ta->focused);
	e.key("End", true); // caret ends up on the last line ("l8")
	e.key("End", false);
	for (int i = 0; i < 7; ++i) {
		e.key("Down", true);
		e.key("Down", false);
	}
	e.frame(400);
	CHECK(ta->scroll_top > 0); // scrolled down to keep the caret visible

	// --- resize reflows: same words, new wrap, same glyph size
	{
		std::size_t lines_wide = 0, lines_narrow = 0;
		std::int32_t px_wide = 0, px_narrow = 0;
		e.resize_viewport(600, 300);
		for (const auto & c : e.frame(600)) {
			if (c.what == ctbrowser::paint_cmd::kind::text && c.text.find(U"nobler") != std::u32string::npos) {
				px_wide = c.font_px;
			}
		}
		ctbrowser::node * wrapme = e.doc.by_id("wrapme");
		for (const auto & c : e.frame(600)) {
			if (c.what == ctbrowser::paint_cmd::kind::text && c.y >= wrapme->y &&
			    c.y < wrapme->y + wrapme->h) {
				++lines_wide;
			}
		}
		ctbrowser::node * rule = e.doc.by_id("rule");
		const std::int32_t hr_wide = rule->w;
		e.resize_viewport(250, 300);
		for (const auto & c : e.frame(250)) {
			if (c.what == ctbrowser::paint_cmd::kind::text && c.text.find(U"nobler") != std::u32string::npos) {
				px_narrow = c.font_px;
			}
		}
		for (const auto & c : e.frame(250)) {
			if (c.what == ctbrowser::paint_cmd::kind::text && c.y >= wrapme->y &&
			    c.y < wrapme->y + wrapme->h) {
				++lines_narrow;
			}
		}
		CHECK(px_wide == 16 && px_narrow == 16); // glyphs NEVER scale
		CHECK(lines_narrow > lines_wide);        // the text rewrapped
		CHECK(rule->w < hr_wide);                // <hr> tracks the window
	}

	if (fails == 0) { std::printf("scroll suite: all checks passed\n"); }
	return fails ? 1 : 0;
}
