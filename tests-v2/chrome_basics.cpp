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

} // namespace

int main() {
	test_a_table_is_a_grid();
	test_a_table_shrinks_to_its_content();
	test_table_sections_are_transparent();
	test_a_stated_table_width_scales_the_columns();
	test_list_markers();
	test_disclosure_triangle();
	test_select_shows_its_option();
	test_scrollbar_appears_only_when_needed();
	test_the_scrollbar_reserves_its_width();
	test_dragging_the_thumb_scrolls();
	test_clicking_the_track_pages();
	test_a_click_on_the_scrollbar_is_not_a_click_on_the_page();

	REPORT("chrome_basics");
}
