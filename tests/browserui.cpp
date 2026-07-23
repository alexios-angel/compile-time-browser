// Browser chrome behaviors: hover cursors (CSS-overridable), the
// draggable overlay scrollbar (scrollbar-width overrides), text
// selection + clipboard (Ctrl+C/X/V/A, cancelable events, user-select
// overrides), and the right-click context menu (contextmenu
// preventDefault suppresses it). Headless: clipboard hooks are stubs.

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
<title>browserui</title>
<style>
	body { margin: 0 }
	.blk { height: 120px; margin: 0 }
	#grab { cursor: pointer }
	#nosel { user-select: none }
</style>
<a href=https://example.com id=lnk>a link</a>
<div id=grab>styled grabber</div>
<p id=para>selectable paragraph text</p>
<p id=nosel>never selected</p>
<input type=text id=t value=hello>
<div class=blk></div><div class=blk></div><div class=blk></div>
<div class=blk></div><div class=blk></div><div class=blk></div>
<script>
	let menus = 0, copies = 0;
	function arm_menu_block() {
		document.addEventListener("contextmenu", function (e) { menus += 1; e.preventDefault(); });
	}
	document.addEventListener("copy", function (e) { copies += 1; });
</script>)">;

int main() {
	ctbrowser::engine<page> e;
	CHECK(e.script.ok());
	std::string clip; // the test clipboard
	e.clipboard_set = [&clip](std::string_view t) { clip = std::string{t}; };
	e.clipboard_get = [&clip]() { return clip; };
	e.resize_viewport(400, 300);
	e.frame(400);

	ctbrowser::node * lnk = e.doc.by_id("lnk");
	ctbrowser::node * grab = e.doc.by_id("grab");
	ctbrowser::node * para = e.doc.by_id("para");
	ctbrowser::node * nosel = e.doc.by_id("nosel");
	ctbrowser::node * t = e.doc.by_id("t");
	CHECK(lnk && grab && para && nosel && t);
	using ck = ctbrowser::engine<page>::cursor_kind;

	// --- cursors: pointer over links (UA), CSS override, I-beam over text
	e.mouse_move(lnk->x + 5.0, lnk->y + 5.0);
	CHECK(e.cursor() == ck::pointer);
	e.mouse_move(grab->x + 5.0, grab->y + 5.0);
	CHECK(e.cursor() == ck::pointer); // page CSS cursor: pointer
	e.mouse_move(t->x + 5.0, t->y + 5.0);
	CHECK(e.cursor() == ck::text); // editable: I-beam
	e.mouse_move(para->x + 5.0, para->y + 5.0);
	CHECK(e.cursor() == ck::text); // bare selectable text: I-beam

	// --- the scrollbar: visible when scrollable, draggable, track-jumps
	{
		std::int32_t sx = 0, sy = 0, sw = 0, sh = 0;
		CHECK(e.scrollbar_thumb(sx, sy, sw, sh));
		CHECK(sw == 12 && sy == 0);
		bool thumb_painted = false;
		for (const auto & c : e.frame(400)) {
			if (c.what == ctbrowser::paint_cmd::kind::box && c.fixed && c.x == sx && c.w == sw &&
			    c.argb == 0xFFB0B0B0u) {
				thumb_painted = true;
			}
		}
		CHECK(thumb_painted);
		// drag the thumb halfway down
		e.mouse_button(sx + 3.0, sy + 3.0, true);
		e.mouse_move(sx + 3.0, sy + 3.0 + 120.0);
		e.mouse_button(sx + 3.0, sy + 123.0, false);
		e.frame(400);
		CHECK(e.scroll_y() > 0);
		const std::int32_t dragged_to = e.scroll_y();
		// click the track above the thumb: page-jump up
		e.scrollbar_thumb(sx, sy, sw, sh);
		if (sy > 2) {
			e.mouse_button(sx + 3.0, 1.0, true);
			e.mouse_button(sx + 3.0, 1.0, false);
			e.frame(400);
			CHECK(e.scroll_y() < dragged_to);
		}
		e.key("Home", true);
		e.key("Home", false);
		e.frame(400);
	}

	// --- editable selection: click-drag + shift arrows + clipboard
	{
		const double y = t->y + t->h / 2.0;
		e.mouse_button(t->ui_text_x + 0.0, y, true); // caret at the start
		e.mouse_move(t->ui_text_x + 16.0 * 5.0, y);  // drag to the end
		e.mouse_button(t->ui_text_x + 16.0 * 5.0, y, false);
		CHECK(t->has_selection());
		CHECK(t->sel_begin() == 0 && t->sel_end() == 5);
		e.key("Left Ctrl", true);
		e.key("C", true);
		e.key("C", false);
		e.key("Left Ctrl", false);
		CHECK(clip == "hello");
		CHECK(e.script["copies"].to<int>() == 1);
		// selection highlight paints
		bool hl = false;
		for (const auto & c : e.frame(400)) {
			if (c.what == ctbrowser::paint_cmd::kind::box && c.argb == 0xFFB4D5FEu) { hl = true; }
		}
		CHECK(hl);
		// typing replaces the selection
		e.text_input("Y");
		CHECK(t->value == "Y");
		// paste at the caret
		e.key("Left Ctrl", true);
		e.key("V", true);
		e.key("V", false);
		e.key("Left Ctrl", false);
		CHECK(t->value == "Yhello");
		// select-all + cut empties the field into the clipboard
		e.key("Left Ctrl", true);
		e.key("A", true);
		e.key("A", false);
		e.key("X", true);
		e.key("X", false);
		e.key("Left Ctrl", false);
		CHECK(t->value.empty());
		CHECK(clip == "Yhello");
	}

	// --- page selection: drag a band over the paragraph, copy it;
	//     user-select:none text never joins
	{
		e.mouse_button(1.0, 1.0, true); // click empty-ish space clears focus
		e.mouse_button(1.0, 1.0, false);
		const double y0 = para->y + 2.0, y1 = nosel->y + nosel->h - 2.0;
		e.mouse_button(30.0, y0, true);
		e.mouse_move(30.0, y1);
		e.mouse_button(30.0, y1, false);
		CHECK(para->selected);
		CHECK(!nosel->selected); // user-select: none held
		e.key("Left Ctrl", true);
		e.key("C", true);
		e.key("C", false);
		e.key("Left Ctrl", false);
		CHECK(clip.find("selectable paragraph text") != std::string::npos);
		CHECK(clip.find("never selected") == std::string::npos);
	}

	// --- the context menu: right-click opens, item acts, Esc closes,
	//     and a preventDefault listener suppresses it entirely
	{
		// select the paragraph so Copy is enabled
		e.mouse_button(30.0, para->y + 2.0, true);
		e.mouse_move(30.0, para->y + para->h - 2.0);
		e.mouse_button(30.0, para->y + para->h - 2.0, false);
		clip.clear();
		e.mouse_button(50.0, 50.0, true, 2); // right-click
		bool menu_painted = false;
		for (const auto & c : e.frame(400)) {
			if (c.what == ctbrowser::paint_cmd::kind::text && c.fixed && c.text == U"Copy") {
				menu_painted = true;
			}
		}
		CHECK(menu_painted);
		e.mouse_button(60.0, 50.0 + 12.0, true); // the Copy row
		CHECK(clip.find("selectable paragraph") != std::string::npos);
		// Esc closes a fresh menu without acting
		e.mouse_button(50.0, 50.0, true, 2);
		e.key("Escape", true);
		e.key("Escape", false);
		bool still_open = false;
		for (const auto & c : e.frame(400)) {
			if (c.what == ctbrowser::paint_cmd::kind::text && c.fixed && c.text == U"Copy") {
				still_open = true;
			}
		}
		CHECK(!still_open);
		// a page can take the menu over (Chrome/Firefox semantics)
		e.script.call("arm_menu_block");
		e.mouse_button(50.0, 50.0, true, 2);
		CHECK(e.script["menus"].to<int>() == 1);
		bool suppressed_open = false;
		for (const auto & c : e.frame(400)) {
			if (c.what == ctbrowser::paint_cmd::kind::text && c.fixed && c.text == U"Copy") {
				suppressed_open = true;
			}
		}
		CHECK(!suppressed_open);
	}

	// --- scrollbar-width: none hides the bar (the CSS override)
	{
		e.doc.root->inline_style.set("scrollbar-width", "none");
		std::int32_t sx = 0, sy = 0, sw = 0, sh = 0;
		CHECK(!e.scrollbar_thumb(sx, sy, sw, sh));
	}

	if (fails == 0) { std::printf("browserui suite: all checks passed\n"); }
	return fails ? 1 : 0;
}
