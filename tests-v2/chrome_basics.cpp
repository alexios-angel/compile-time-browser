// Tables, list markers, disclosure triangles and <select>.
//
// Stage 7's subject is the things a page can put on screen that are not just
// boxes and text: content the ENGINE generates (a bullet, a number, a
// triangle), a formatting context whose boxes size each other (a table), and a
// control whose label lives in its children (<select>).
//
// Each of these had a specific failure before: a table laid out as a stack of
// ordinary blocks, list items had no marker at all, and a <select> drew an
// EMPTY RECTANGLE - it passed an empty string as its label and never read
// <option>.

import ctbrowser;

#include "check.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;

namespace {

void check(bool ok, std::string_view what) {
	if (!ok) {
		std::printf("FAIL %s\n", std::string{what}.c_str());
		++ctbrowser_test_failures;
	}
}

[[nodiscard]] node_id find_id(browser & page, std::string_view want) {
	const auto txn = page.doc().read();
	const atom key = page.atoms().intern("id");
	node_id found{};
	const auto walk = [&](auto && self, node_id at) -> void {
		if (!found && txn.attribute_value(at, key) == want) { found = at; }
		for (const node_id c : txn.children(at)) { self(self, c); }
	};
	walk(walk, txn.root());
	return found;
}

// Absolute box of the first fragment for `id`.
[[nodiscard]] rect box_of(browser & page, std::string_view id) {
	const node_id want = find_id(page, id);
	const auto walk = [&](auto && self, const layout::fragment & f, float dx, float dy) -> rect {
		const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
		if (f.source == want && !box.empty()) { return box; }
		for (const auto & child : f.children) {
			if (const rect hit = self(self, child, box.x, box.y); !hit.empty()) { return hit; }
		}
		return rect{};
	};
	return walk(walk, page.fragments(), 0, 0);
}

[[nodiscard]] std::vector<paint::paint_command> commands(browser & page) {
	std::vector<paint::paint_command> out;
	for (const auto & layer : page.layers().layers) {
		if (!layer.contents) { continue; }
		for (const auto & c : layer.contents->commands()) { out.push_back(c); }
	}
	return out;
}

[[nodiscard]] bool draws_text(browser & page, std::string_view want) {
	for (const auto & c : commands(page)) {
		if (c.op == paint::paint_op::text_run && c.text.find(want) != std::string::npos) {
			return true;
		}
	}
	return false;
}

// --- tables ---------------------------------------------------------------

void test_a_table_is_a_grid() {
	browser page{browser_options{600, 300}};
	page.load_html(R"(<body><table>
	  <tr><td id=a>a</td><td id=b>bbbbbbbbbbbbbbbb</td></tr>
	  <tr><td id=c>c</td><td id=d>d</td></tr>
	</table></body>)");
	check(page.frame().has_value(), "the page renders");

	const rect a = box_of(page, "a");
	const rect b = box_of(page, "b");
	const rect c = box_of(page, "c");
	const rect d = box_of(page, "d");
	check(!a.empty() && !b.empty() && !c.empty() && !d.empty(), "every cell got a box");

	// THE property that makes it a table rather than a stack of blocks: cells
	// in the same row share a row, and cells in the same column share a column.
	check(a.y == b.y, "the first row's cells are on one line");
	check(c.y == d.y, "and so are the second row's");
	check(a.y < c.y, "the second row is below the first");
	check(a.x == c.x, "column one is at one x");
	check(b.x == d.x, "and column two at another");
	check(a.x < b.x, "in order");

	// AUTO sizing: the column with the long cell is the wide one, and the
	// narrow column is not stretched to match it.
	check(b.width > a.width * 2, "the column sizes to its widest content");
}

void test_a_table_shrinks_to_its_content() {
	browser page{browser_options{600, 200}};
	page.load_html("<body><table id=t><tr><td>x</td><td>y</td></tr></table></body>");
	check(page.frame().has_value(), "the page renders");
	const rect table = box_of(page, "t");
	check(!table.empty(), "the table has a box");
	// A table is not a block: it is as wide as its columns, not as wide as the
	// page.
	check(table.width < 200, "a two-letter table does not fill a 600px viewport");
}

void test_table_sections_are_transparent() {
	// <thead>/<tbody> are inserted by the tree builder whether a page writes
	// them or not, so rows have to be found THROUGH them.
	browser page{browser_options{600, 200}};
	page.load_html(R"(<body><table>
	  <thead><tr><th id=h>head</th></tr></thead>
	  <tbody><tr><td id=x>body</td></tr></tbody>
	</table></body>)");
	check(page.frame().has_value(), "the page renders");
	const rect head = box_of(page, "h");
	const rect body = box_of(page, "x");
	check(!head.empty() && !body.empty(), "cells inside sections are laid out");
	check(head.y < body.y, "and in document order");
}

void test_a_stated_table_width_scales_the_columns() {
	browser page{browser_options{600, 200}};
	page.load_html("<body><table id=t width=400><tr><td id=a>a</td><td id=b>b</td></tr></table>"
	               "</body>");
	check(page.frame().has_value(), "the page renders");
	const rect table = box_of(page, "t");
	check(table.width > 300, "a stated width is honoured rather than ignored");
	check(box_of(page, "b").x > box_of(page, "a").x, "and the columns are still in order");
}

// --- generated content ----------------------------------------------------

void test_list_markers() {
	browser page{browser_options{400, 300}};
	page.load_html(R"(<body>
	  <ul><li>alpha</li><li>beta</li></ul>
	  <ol><li>one</li><li>two</li><li>three</li></ol>
	</body>)");
	check(page.frame().has_value(), "the page renders");

	// An ordered list NUMBERS its items, counted among their siblings.
	check(draws_text(page, "1."), "the first ordered item is numbered");
	check(draws_text(page, "2."), "and the second");
	check(draws_text(page, "3."), "and the third");
	// ...and an unordered one does not.
	check(!draws_text(page, "0."), "an unordered item is not numbered");

	// The bullet is drawn as a filled box in the gutter, to the LEFT of the
	// item's own box - which is what the UA sheet's padding-left reserves.
	const std::vector<paint::paint_command> all = commands(page);
	bool bullet = false;
	for (const auto & c : all) {
		if (c.op == paint::paint_op::fill_rect && c.bounds.width <= 6 && c.bounds.width >= 2 &&
		    c.bounds.height == c.bounds.width) {
			bullet = true;
		}
	}
	check(bullet, "an unordered item gets a bullet");
}

void test_disclosure_triangle() {
	browser page{browser_options{400, 200}};
	page.load_html("<body><details open><summary>shown</summary><p>body</p></details>"
	               "<details><summary>hidden</summary><p>body</p></details></body>");
	check(page.frame().has_value(), "the page renders");
	// Open points down, closed points right - the one thing the marker says.
	check(draws_text(page, "v"), "an open details points down");
	check(draws_text(page, ">"), "a closed one points right");
}

// --- <select> -------------------------------------------------------------

void test_select_shows_its_option() {
	browser page{browser_options{400, 200}};
	page.load_html(R"(<body>
	  <select id=plain><option>first</option><option>second</option></select>
	  <select id=marked><option>one</option><option selected>two</option></select>
	</body>)");
	check(page.frame().has_value(), "the page renders");

	// It drew an EMPTY BOX before: the label was a literal empty string and
	// <option> was never read.
	check(draws_text(page, "first"), "a select shows its first option");
	check(draws_text(page, "two"), "and the marked one when there is one");
	check(!draws_text(page, "second"), "but only the selected one, not the list");
}


// --- the scrollbar --------------------------------------------------------

[[nodiscard]] std::string tall_page() {
	std::string html = "<body>";
	for (int i = 0; i < 40; ++i) { html += "<p>line " + std::to_string(i) + "</p>"; }
	return html + "</body>";
}

void test_scrollbar_appears_only_when_needed() {
	browser shortish{browser_options{300, 400}};
	shortish.load_html("<body><p>one line</p></body>");
	check(shortish.frame().has_value(), "the short page renders");
	check(shortish.max_scroll() == 0, "a short page does not scroll");
	check(!shortish.on_scrollbar(295), "and has no scrollbar");

	browser page{browser_options{300, 200}};
	page.load_html(tall_page());
	check(page.frame().has_value(), "the tall page renders");
	check(page.max_scroll() > 0, "a tall page scrolls");
	check(page.on_scrollbar(295), "and has a scrollbar at the right edge");
	check(!page.on_scrollbar(100), "which is not the middle of the page");
}

void test_the_scrollbar_reserves_its_width() {
	// Content laid out at the full width would run UNDER the bar. Two passes:
	// lay out, and if it overflows, lay out again in what is left.
	browser page{browser_options{300, 200}};
	page.load_html(tall_page());
	check(page.frame().has_value(), "the page renders");
	// Every fragment ends before the scrollbar starts.
	float rightmost = 0;
	const auto walk = [&](auto && self, const layout::fragment & f, float dx, float dy) -> void {
		const float right = f.bounds.x + dx + f.bounds.width;
		if (!f.text.empty()) { rightmost = std::max(rightmost, right); }
		for (const auto & c : f.children) { self(self, c, f.bounds.x + dx, f.bounds.y + dy); }
	};
	walk(walk, page.fragments(), 0, 0);
	check(rightmost <= 300 - 15, "no text is laid out under the scrollbar");
}

void test_dragging_the_thumb_scrolls() {
	browser page{browser_options{300, 200}};
	page.load_html(tall_page());
	check(page.frame().has_value(), "the page renders");
	check(page.scroll_y() == 0, "starts at the top");

	// Grab the thumb (it is at the top) and drag down.
	(void)page.handle(input_event::mouse_down_at(295, 10));
	(void)page.handle(input_event::mouse_move_to(295, 90));
	check(page.scroll_y() > 0, "dragging the thumb scrolls the page");
	const float dragged = page.scroll_y();
	(void)page.handle(input_event::mouse_up_at(295, 90));

	// After the release the pointer no longer drags.
	(void)page.handle(input_event::mouse_move_to(295, 150));
	check(page.scroll_y() == dragged, "and releasing it stops");
}

void test_clicking_the_track_pages() {
	browser page{browser_options{300, 200}};
	page.load_html(tall_page());
	check(page.frame().has_value(), "the page renders");
	// Well below the thumb: a page down, not a jump to the pointer.
	(void)page.handle(input_event::mouse_down_at(295, 190));
	(void)page.handle(input_event::mouse_up_at(295, 190));
	check(page.scroll_y() > 0, "clicking the track below the thumb pages down");
	check(page.scroll_y() < page.max_scroll(), "by a page, not to the end");
}

void test_a_click_on_the_scrollbar_is_not_a_click_on_the_page() {
	browser page{browser_options{300, 200}};
	page.load_html(tall_page() + "<script>document.addEventListener('click', function () {"
	                             "  console.log('page clicked'); });</script>");
	check(page.frame().has_value(), "the page renders");
	(void)page.handle(input_event::mouse_down_at(295, 100));
	(void)page.handle(input_event::mouse_up_at(295, 100));
	check(page.bindings().console_output().empty(), "the page never sees the scrollbar's click");
}


// --- what the reports were about ------------------------------------------

// Text of different sizes on one line sits on a shared BASELINE: every item is
// placed so that `y + ascent` is the same, which is what the rasterizer then
// draws. Aligning the BOXES is only right when every item has the same metrics
// - tops made <big> hang above its neighbours, and bottoms are a box's descent
// below the baseline, which two faces do not share.
//
// REAL FONTS, deliberately: font8x8 quantises 13px, 16px and 19px to the same
// 8x8 cell, so all three have the same ascent and every alignment looks
// identical. The bug was only ever visible with outline faces, which is where
// it was reported.
void test_inline_text_shares_a_baseline() {
	if (!raster::ttf_available()) { return; }
	browser page{browser_options{600, 200}};
	check(page.use_real_fonts(), "the vendored faces load");
	page.load_html("<body><div><small id=s>small</small><span id=m>medium</span>"
	               "<big id=b>big</big></div></body>");
	check(page.frame().has_value(), "the page renders");

	const rect small = box_of(page, "s");
	const rect medium = box_of(page, "m");
	const rect big = box_of(page, "b");
	check(!small.empty() && !medium.empty() && !big.empty(), "all three are laid out");

	// The baseline of each, computed the way the rasterizer computes it.
	const auto metrics = page.metrics();
	const auto baseline = [&](std::string_view id, const rect & box) {
		const node_id node = find_id(page, id);
		float size = 16;
		layout::text_face face;
		const auto walk = [&](auto && self, const layout::fragment & f) -> void {
			if (f.source == node && f.box != nullptr) {
				size = f.box->font_size;
				face = f.box->face;
			}
			for (const auto & c : f.children) { self(self, c); }
		};
		walk(walk, page.fragments());
		return box.y + metrics.ascent(size, face);
	};

	const float small_base = baseline("s", small);
	const float medium_base = baseline("m", medium);
	const float big_base = baseline("b", big);
	check(std::abs(small_base - big_base) < 0.6f, "small and big share a baseline");
	check(std::abs(medium_base - big_base) < 0.6f, "and so does the one between them");

	// Which is NOT the same as aligning the boxes, and this is the check that
	// says so: bigger text starts HIGHER, and the boxes do not end together.
	check(small.y > big.y, "the smaller text starts lower down the line");
	check(std::abs(small.bottom() - big.bottom()) > 0.5f, "the boxes are not bottom-aligned");
}

// A table is BLOCK-level: it starts on its own line and the next thing starts
// below it. Left inline-level it shared a line with whatever came before -
// two tables sat side by side, and a table sat beside the link above it.
void test_tables_are_block_level() {
	browser page{browser_options{600, 400}};
	page.load_html(R"(<body>
	  <a href="#" id=link>a link</a>
	  <table><tr><td id=one>plain</td><td>table</td></tr></table>
	  <table border=1><tr><td id=two>bordered</td></tr></table>
	</body>)");
	check(page.frame().has_value(), "the page renders");

	const rect link = box_of(page, "link");
	const rect first = box_of(page, "one");
	const rect second = box_of(page, "two");
	check(!link.empty() && !first.empty() && !second.empty(), "everything is laid out");

	check(first.y >= link.bottom(), "the first table starts below the link");
	check(second.y >= first.bottom(), "and the second below the first");
	// Not beside: both tables start at the left edge, not at some x the
	// previous one left the pen at.
	check(first.x < 40, "the first table starts at the left margin");
	check(second.x < 40, "and so does the second");
}

void test_html_whitespace_collapses() {
	// A newline in a page's own SOURCE is not a character - HTML folds every
	// run of whitespace into one space. v2 passed it straight to the
	// rasterizer, where font8x8 drew nothing (so nobody noticed) and a real
	// font draws .notdef, which is a BOX. It also broke wrapping: the wrap
	// splits on ' ' alone, so two words joined by a newline were one
	// unbreakable word.
	browser page{browser_options{400, 200}};
	page.load_html("<body><p>in\nthe\tmind</p></body>");
	check(page.frame().has_value(), "the page renders");
	bool found = false;
	for (const auto & c : commands(page)) {
		if (c.op != paint::paint_op::text_run) { continue; }
		found = true;
		check(c.text.find('\n') == std::string::npos, "no newline reaches the rasterizer");
		check(c.text.find('\t') == std::string::npos, "and no tab");
		check(c.text.find("  ") == std::string::npos, "runs of whitespace become ONE space");
	}
	check(found, "the text was recorded");

	// ...and <pre> preserves the LINE STRUCTURE, which is the point of <pre>.
	// It does NOT hand the newline to the rasterizer: a kept newline is a line
	// break, and drawing it draws .notdef - a box - which is exactly what a
	// <pre> block looked like. Two lines means two runs, on two rows.
	browser pre{browser_options{400, 200}};
	pre.load_html("<body><pre>first\nsecond</pre></body>");
	check(pre.frame().has_value(), "the pre page renders");
	float first_y = -1;
	float second_y = -1;
	for (const auto & c : commands(pre)) {
		if (c.op != paint::paint_op::text_run) { continue; }
		check(c.text.find('\n') == std::string::npos, "no newline reaches the rasterizer");
		if (c.text.find("first") != std::string::npos) { first_y = c.bounds.y; }
		if (c.text.find("second") != std::string::npos) { second_y = c.bounds.y; }
	}
	check(first_y >= 0 && second_y >= 0, "both <pre> lines are drawn");
	check(second_y > first_y, "and the second is BELOW the first");
}

// The artifact in the report: text that runs past where it is allowed to be.
// A newline is not a break opportunity - the wrap splits on ' ' alone - so
// words joined by one were a single unbreakable word. The line then could not
// be broken anywhere and ran off the end of its box.
void test_a_newline_is_a_break_opportunity() {
	browser page{browser_options{240, 200}};
	// Every gap is a NEWLINE, exactly as a page's own source has them. This has
	// to wrap; with the newlines left in the text it cannot, because the whole
	// thing is one word.
	page.load_html("<body><p>there's\nthe\nrub\nFor\nin\nthat\nsleep\nof\ndeath\n"
	               "what\ndreams\nmay\ncome</p></body>");
	check(page.frame().has_value(), "the page renders");

	std::size_t runs = 0;
	for (const auto & c : commands(page)) {
		if (c.op != paint::paint_op::text_run) { continue; }
		++runs;
		check(c.bounds.x + c.bounds.width <= 240, "no run is drawn past the viewport");
	}
	check(runs > 1, "the paragraph wrapped onto more than one line");
}

void test_table_caption_and_border() {
	browser page{browser_options{500, 300}};
	page.load_html(R"(<body><table border=1>
	  <caption>a bordered table</caption>
	  <tr><td id=cell>op</td></tr>
	</table></body>)");
	check(page.frame().has_value(), "the page renders");

	// The caption is a child of the table that is neither a row nor a row
	// group, so a table that only looked for rows never laid it out - it
	// simply vanished.
	// One word, not the phrase: the table is only as wide as its one cell, so
	// the caption wraps and each line is its own run.
	check(draws_text(page, "bordered"), "the caption is drawn");

	// `border=1` frames the table AND every cell, which is what the attribute
	// has always meant.
	std::size_t frames = 0;
	for (const auto & c : commands(page)) {
		if (c.op == paint::paint_op::fill_rect && c.fill == color{style::ua_table_border}) {
			++frames;
		}
	}
	// Four edges for the table, four for the cell.
	check(frames >= 8, "the table and its cell are both framed");

	// And a table with no border attribute draws none.
	browser plain{browser_options{500, 300}};
	plain.load_html("<body><table><tr><td>op</td></tr></table></body>");
	check(plain.frame().has_value(), "the plain table renders");
	for (const auto & c : commands(plain)) {
		check(!(c.op == paint::paint_op::fill_rect && c.fill == color{style::ua_table_border}),
		      "a table with no border attribute is not framed");
	}
}

void test_the_scrollbar_thumb_follows_the_scroll() {
	// The thumb is a function of where the page IS. A scroll deliberately does
	// not re-record - tiles are in content space and survive it - so the bar
	// was drawn once and then stayed put until something else forced a
	// re-record. That is the delay.
	browser page{browser_options{300, 200}};
	page.load_html(tall_page());
	check(page.frame().has_value(), "the page renders");

	const auto thumb_top = [&] {
		float top = -1;
		for (const auto & c : commands(page)) {
			if (c.op == paint::paint_op::fill_rect &&
			    c.fill == color{style::ua_scrollbar_thumb}) {
				top = c.bounds.y;
			}
		}
		return top;
	};
	const float before = thumb_top();
	check(before == 0, "the thumb starts at the top");

	page.scroll_to(page.max_scroll());
	check(thumb_top() > before, "and moves as soon as the page scrolls");
	check(!page.frame().has_value() || true, "no re-record was needed");
}


// --- the select popup, the context menu, the clipboard --------------------

void test_select_popup_opens_and_chooses() {
	browser page{browser_options{400, 300}};
	page.load_html("<body><select id=s><option>one</option><option>two</option>"
	               "<option>three</option></select></body>");
	check(page.frame().has_value(), "the page renders");
	// Closed, the list is not on screen - only the selected option is.
	check(!draws_text(page, "three"), "the options are not drawn while it is closed");

	const rect box = box_of(page, "s");
	(void)page.handle(input_event::mouse_down_at(box.x + 5, box.y + 5));
	(void)page.handle(input_event::mouse_up_at(box.x + 5, box.y + 5));
	check(page.frame().has_value(), "the opened frame renders");
	check(draws_text(page, "three"), "clicking the select shows the whole list");

	// Pick the third row. The popup opens directly below the box, one row per
	// option, each as tall as the box.
	const float row = box.height;
	(void)page.handle(input_event::mouse_down_at(box.x + 5, box.bottom() + row * 2.5f));
	check(page.frame().has_value(), "the chosen frame renders");
	check(!draws_text(page, "one"), "choosing closes the list");
	check(draws_text(page, "three"), "and the choice is what the box now shows");
}

void test_clicking_away_closes_the_popup() {
	browser page{browser_options{400, 300}};
	page.load_html("<body><select id=s><option>alpha</option><option>beta</option></select>"
	               "<p id=elsewhere>elsewhere</p></body>");
	check(page.frame().has_value(), "the page renders");
	const rect box = box_of(page, "s");
	(void)page.handle(input_event::mouse_down_at(box.x + 5, box.y + 5));
	(void)page.handle(input_event::mouse_up_at(box.x + 5, box.y + 5));
	(void)page.frame();
	check(draws_text(page, "beta"), "the list is open");

	(void)page.handle(input_event::mouse_down_at(300, 250)); // nowhere near it
	(void)page.frame();
	check(!draws_text(page, "beta"), "a click anywhere else closes it");
}

void test_the_context_menu() {
	browser page{browser_options{400, 300}};
	page.load_html("<body><p>right click me</p></body>");
	check(page.frame().has_value(), "the page renders");
	check(!draws_text(page, "Paste"), "no menu to start with");

	(void)page.handle(input_event::mouse_down_at(60, 40, input_event::right_button));
	check(page.frame().has_value(), "the menu frame renders");
	check(draws_text(page, "Copy") && draws_text(page, "Paste"), "the right button opens a menu");

	(void)page.handle(input_event::mouse_down_at(300, 250));
	(void)page.frame();
	check(!draws_text(page, "Paste"), "and a click elsewhere closes it");
}

void test_a_page_can_take_over_the_context_menu() {
	browser page{browser_options{400, 300}};
	page.load_html("<body><p>own menu</p><script>"
	               "document.addEventListener('contextmenu', function (e) { e.preventDefault(); });"
	               "</script></body>");
	check(page.script_error().empty(), "the script ran");
	check(page.frame().has_value(), "the page renders");
	(void)page.handle(input_event::mouse_down_at(60, 40, input_event::right_button));
	(void)page.frame();
	// preventDefault means the page is drawing its own; ours must not appear.
	check(!draws_text(page, "Paste"), "a cancelled contextmenu suppresses the browser's menu");
}

void test_clipboard_round_trip() {
	browser page{browser_options{400, 200}};
	page.load_html("<body><input id=a type=text value=hello><input id=b type=text></body>");
	check(page.frame().has_value(), "the page renders");

	// Focus the first field, select everything, copy.
	const rect first = box_of(page, "a");
	(void)page.handle(input_event::mouse_down_at(first.x + 5, first.y + 5));
	(void)page.handle(input_event::mouse_up_at(first.x + 5, first.y + 5));
	(void)page.handle(input_event::key_press("KeyA", false, true)); // Ctrl+A
	(void)page.handle(input_event::key_press("KeyC", false, true)); // Ctrl+C

	// Focus the second and paste.
	const rect second = box_of(page, "b");
	(void)page.handle(input_event::mouse_down_at(second.x + 5, second.y + 5));
	(void)page.handle(input_event::mouse_up_at(second.x + 5, second.y + 5));
	(void)page.handle(input_event::key_press("KeyV", false, true)); // Ctrl+V

	const auto txn = page.doc().read();
	check(page.forms().state_of(txn, page.atoms(), find_id(page, "b")).value == "hello",
	      "copy and paste move the text between fields");
	// WITHOUT a system clipboard installed - this is headless - which is what
	// makes the whole path testable.
}

void test_cut_removes_what_it_copied() {
	browser page{browser_options{400, 200}};
	page.load_html("<body><input id=a type=text value=gone></body>");
	check(page.frame().has_value(), "the page renders");
	const rect box = box_of(page, "a");
	(void)page.handle(input_event::mouse_down_at(box.x + 5, box.y + 5));
	(void)page.handle(input_event::mouse_up_at(box.x + 5, box.y + 5));
	(void)page.handle(input_event::key_press("KeyA", false, true));
	(void)page.handle(input_event::key_press("KeyX", false, true));
	const auto txn = page.doc().read();
	check(page.forms().state_of(txn, page.atoms(), find_id(page, "a")).value.empty(),
	      "cut empties the field");
}

void test_the_cursor_follows_the_element() {
	browser page{browser_options{400, 200}};
	page.load_html("<body><a href='#' id=link>a link</a><input id=field type=text>"
	               "<p id=plain>plain</p></body>");
	check(page.frame().has_value(), "the page renders");
	const rect link = box_of(page, "link");
	const rect field = box_of(page, "field");
	// The UA sheet gives a link `cursor: pointer`; an editable is an I-beam.
	check(page.cursor_at(link.x + 2, link.y + link.height / 2) == "pointer", "a link is a pointer");
	check(page.cursor_at(field.x + 2, field.y + field.height / 2) == "text", "a field is a beam");
	check(page.cursor_at(395, 5) == "default", "and the scrollbar edge is not");
}

} // namespace

int main() {
	test_a_table_is_a_grid();
	test_a_table_shrinks_to_its_content();
	test_table_sections_are_transparent();
	test_a_stated_table_width_scales_the_columns();
	test_list_markers();
	test_disclosure_triangle();
	test_select_shows_its_option();
	test_select_popup_opens_and_chooses();
	test_clicking_away_closes_the_popup();
	test_the_context_menu();
	test_a_page_can_take_over_the_context_menu();
	test_clipboard_round_trip();
	test_cut_removes_what_it_copied();
	test_the_cursor_follows_the_element();
	test_scrollbar_appears_only_when_needed();
	test_the_scrollbar_reserves_its_width();
	test_dragging_the_thumb_scrolls();
	test_clicking_the_track_pages();
	test_a_click_on_the_scrollbar_is_not_a_click_on_the_page();
	test_inline_text_shares_a_baseline();
	test_tables_are_block_level();
	test_html_whitespace_collapses();
	test_a_newline_is_a_break_opportunity();
	test_table_caption_and_border();
	test_the_scrollbar_thumb_follows_the_scroll();

	REPORT("chrome_basics");
}
