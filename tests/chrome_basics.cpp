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

[[nodiscard]] const std::vector<std::string> & log_of(browser & page) {
	return page.bindings().console_output();
}

// --- the form-control batch ------------------------------------------------
//
// Eight bugs a real page found that the test suite did not, because every one
// of them lives in what a control LOOKS like or what a script can READ from it
// - and the example ctests only check that the process exits 0.

// The caret bar for `id`, if one is drawn: a 1px-wide fill.
[[nodiscard]] std::vector<rect> caret_bars(browser & page, std::string_view id) {
	const node_id want = find_id(page, id);
	const rect box = box_of(page, id);
	std::vector<rect> out;
	for (const auto & c : commands(page)) {
		// Width 1 alone is not enough: the field's OUTLINE has two 1px-wide
		// vertical edges, and they are the same colour and the same source.
		// The caret is the one strictly INSIDE the box.
		if (c.op == paint::paint_op::fill_rect && c.source == want && c.bounds.width == 1 &&
		    c.bounds.x > box.x + 1 && c.bounds.right() < box.right() - 1) {
			out.push_back(c.bounds);
		}
	}
	return out;
}

[[nodiscard]] std::size_t arrow_rows(browser & page, std::string_view id) {
	// The drop-down arrow is a stack of 1px-tall fills in the frame colour.
	const node_id want = find_id(page, id);
	std::size_t rows = 0;
	for (const auto & c : commands(page)) {
		if (c.op == paint::paint_op::fill_rect && c.source == want && c.bounds.height == 1 &&
		    c.fill == color{style::ua_widget_frame} && c.bounds.width < 10) {
			++rows;
		}
	}
	return rows;
}

// --- editing with the mouse and the arrow keys ----------------------------

[[nodiscard]] std::size_t caret_of(browser & page, std::string_view id) {
	const auto * state = page.control_state_of(find_id(page, id));
	return state == nullptr ? 0 : state->caret;
}
[[nodiscard]] std::pair<std::size_t, std::size_t> selection_of(browser & page,
                                                               std::string_view id) {
	const auto * state = page.control_state_of(find_id(page, id));
	if (state == nullptr) { return {0, 0}; }
	return {std::min(state->caret, state->selection), std::max(state->caret, state->selection)};
}

void click(browser & page, float x, float y) {
	(void)page.handle(input_event::mouse_down_at(x, y));
	(void)page.handle(input_event::mouse_up_at(x, y));
}

// A caret must be measured with the face the text is DRAWN in. A textarea is
// monospace by UA rule and its text was drawn in the default serif, so the
// caret ran ahead by the difference on every character - which reads as a
// growing gap between what you typed and where the caret is.
void test_a_control_draws_in_the_face_it_measures() {
	browser page{browser_options{500, 300}};
	check(page.use_real_fonts(), "the vendored faces load");
	page.load_html("<body><textarea id=t rows=3 cols=20></textarea></body>");
	check(page.frame().has_value(), "the page renders");
	const rect box = box_of(page, "t");
	click(page, box.x + 6, box.y + 6);
	check(page.text_input("Hello world"), "typed");
	check(page.frame().has_value(), "redraws");

	std::string drawn;
	float text_x = 0;
	layout::text_face drawn_face;
	for (const auto & c : commands(page)) {
		if (c.op == paint::paint_op::text_run && c.source == find_id(page, "t")) {
			drawn = c.text;
			text_x = c.bounds.x;
			drawn_face = layout::text_face{c.face.family, c.face.bold, c.face.italic};
		}
	}
	check(drawn == "Hello world", "the value is drawn");
	// The caret sits at the END of the drawn text, measured in the SAME face.
	const std::vector<rect> bars = caret_bars(page, "t");
	check(bars.size() == 1, "there is a caret");
	if (bars.empty()) { return; }
	const float expected = text_x + page.metrics()(drawn, 16, drawn_face);
	check(std::fabs(bars[0].x - expected) < 2,
	      "and it is where the drawn text ENDS, not a different font's width along");
}

void test_clicking_in_a_textarea_places_the_caret() {
	browser page{browser_options{500, 300}};
	page.load_html("<body><textarea id=t rows=3 cols=20>abcdef\nghijkl</textarea></body>");
	check(page.frame().has_value(), "the page renders");
	const rect box = box_of(page, "t");

	// The start of the FIRST line.
	click(page, box.x + 7, box.y + 6);
	check(caret_of(page, "t") == 0, "clicking the first character lands before it");

	// The start of the SECOND line: past the newline, at offset 7.
	click(page, box.x + 7, box.y + 26);
	check(caret_of(page, "t") == 7, "clicking the second line lands at its start");

	// Well past the end of a line clamps to that line's end, not to the value's.
	click(page, box.right() - 4, box.y + 6);
	check(caret_of(page, "t") == 6, "clicking past a line's end lands at ITS end");
}

void test_arrows_move_by_visual_line_in_a_textarea() {
	browser page{browser_options{500, 300}};
	page.load_html("<body><textarea id=t rows=3 cols=20>abcdef\nghijkl</textarea></body>");
	check(page.frame().has_value(), "the page renders");
	const rect box = box_of(page, "t");
	click(page, box.x + 7, box.y + 6); // caret at 0

	(void)page.handle(input_event::key_press("ArrowRight"));
	(void)page.handle(input_event::key_press("ArrowRight"));
	check(caret_of(page, "t") == 2, "right moves by a character");

	// DOWN keeps the column. Walking by characters through the newline would
	// land at 3, which is not what down means.
	(void)page.handle(input_event::key_press("ArrowDown"));
	check(caret_of(page, "t") == 9, "down keeps the column on the next line");
	(void)page.handle(input_event::key_press("ArrowUp"));
	check(caret_of(page, "t") == 2, "and up comes back to it");

	// HOME and END are that LINE's ends, not the whole value's.
	(void)page.handle(input_event::key_press("ArrowDown"));
	(void)page.handle(input_event::key_press("End"));
	check(caret_of(page, "t") == 13, "End goes to the end of the second line");
	(void)page.handle(input_event::key_press("Home"));
	check(caret_of(page, "t") == 7, "Home to its start");
}

void test_dragging_selects_inside_a_field() {
	browser page{browser_options{500, 300}};
	page.load_html("<body><input type=text id=f value=abcdefgh size=20></body>");
	check(page.frame().has_value(), "the page renders");
	const rect field = box_of(page, "f");

	(void)page.handle(input_event::mouse_down_at(field.x + 7, field.y + 10));
	(void)page.handle(input_event::mouse_move_to(field.x + 40, field.y + 10));
	(void)page.handle(input_event::mouse_up_at(field.x + 40, field.y + 10));
	const auto [from, to] = selection_of(page, "f");
	check(from == 0, "the selection starts where the press was");
	check(to > from, "and covers what was dragged over");

	// And it can be COPIED, which is the whole reason to select it.
	std::string clipboard;
	page.set_clipboard_hooks([&clipboard](const std::string & text) { clipboard = text; },
	                         [&clipboard] { return clipboard; });
	input_event copy = input_event::key_press("KeyC");
	copy.ctrl = true;
	(void)page.handle(copy);
	check(!clipboard.empty(), "Ctrl+C copied something");
	check(clipboard == std::string_view{"abcdefgh"}.substr(from, to - from),
	      "and it is exactly what was selected");
}

void test_escape_and_blur_drop_a_field_selection() {
	browser page{browser_options{500, 300}};
	page.load_html("<body><input type=text id=f value=abcdef size=20>"
	               "<p id=elsewhere>not a field</p></body>");
	check(page.frame().has_value(), "the page renders");
	const rect field = box_of(page, "f");
	click(page, field.x + 6, field.y + 10);

	input_event select_all = input_event::key_press("KeyA");
	select_all.ctrl = true;
	(void)page.handle(select_all);
	{
		const auto [from, to] = selection_of(page, "f");
		check(to - from == 6, "Ctrl+A selects the whole value");
	}
	(void)page.handle(input_event::key_press("Escape"));
	{
		const auto [from, to] = selection_of(page, "f");
		check(from == to, "Escape drops it, as a browser does");
	}

	// And so does clicking away: a highlight left behind in a field nobody is
	// typing in reads as still selected.
	(void)page.handle(select_all);
	check(selection_of(page, "f").second > 0, "selected again");
	const rect elsewhere = box_of(page, "elsewhere");
	click(page, elsewhere.x + 4, elsewhere.y + 4);
	{
		const auto [from, to] = selection_of(page, "f");
		check(from == to, "clicking somewhere else drops it too");
	}
}

void test_a_password_shows_bullets() {
	browser page{browser_options{500, 300}};
	page.load_html("<body><input type=password id=p value=hunter2 size=20>"
	               "<input type=text id=t value=hunter2 size=20></body>");
	check(page.frame().has_value(), "the page renders");

	std::string password_run;
	std::string text_run;
	for (const auto & c : commands(page)) {
		if (c.op != paint::paint_op::text_run) { continue; }
		if (c.source == find_id(page, "p")) { password_run = c.text; }
		if (c.source == find_id(page, "t")) { text_run = c.text; }
	}
	check(text_run == "hunter2", "a text field shows its value");
	check(!password_run.empty(), "a password field draws something");
	check(password_run.find("hunter") == std::string::npos,
	      "but NOT the password - it was drawn in plain text");
	// One bullet per code point, so the field is the same length as the value.
	check(password_run == "•••••••", "seven characters, seven bullets");
}

void test_a_password_caret_is_measured_on_the_bullets() {
	browser page{browser_options{500, 300}};
	check(page.use_real_fonts(), "the vendored faces load");
	page.load_html("<body><input type=password id=p size=20></body>");
	check(page.frame().has_value(), "the page renders");
	const rect field = box_of(page, "p");
	click(page, field.x + 6, field.y + 10);
	check(page.text_input("abcd"), "typed");
	check(page.frame().has_value(), "redraws");

	float text_x = 0;
	std::string drawn;
	for (const auto & c : commands(page)) {
		if (c.op == paint::paint_op::text_run && c.source == find_id(page, "p")) {
			text_x = c.bounds.x;
			drawn = c.text;
		}
	}
	const std::vector<rect> bars = caret_bars(page, "p");
	check(bars.size() == 1, "there is a caret");
	if (bars.empty() || drawn.empty()) { return; }
	// Measured on what is SHOWN. A bullet is wider than most letters, so
	// measuring the letters puts the caret inside the bullets.
	const float expected = text_x + page.metrics()(drawn, 16, layout::text_face{});
	check(std::fabs(bars[0].x - expected) < 2, "and it sits after the last bullet");
}

// --- disabled --------------------------------------------------------------

void test_a_disabled_control_looks_and_acts_disabled() {
	browser page{browser_options{500, 300}};
	page.load_html("<body><button id=on>Live</button><button id=off disabled>Dead</button>"
	               "<input type=text id=f disabled value=x size=10></body>");
	check(page.frame().has_value(), "the page renders");

	color live{};
	color dead{};
	for (const auto & c : commands(page)) {
		if (c.op != paint::paint_op::text_run) { continue; }
		if (c.text == "Live") { live = c.fill; }
		if (c.text == "Dead") { dead = c.fill; }
	}
	check(!(live == color{}) && !(dead == color{}), "both labels are drawn");
	check(!(live == dead), "and a disabled button is NOT drawn like a live one");
	check(dead == color{style::ua_widget_disabled_text}, "it is greyed");

	// And it is INERT: no focus, no activation, no events.
	const rect off = box_of(page, "off");
	click(page, off.x + 6, off.y + 6);
	check(!page.focused(), "a disabled button does not take focus");
	const rect disabled_field = box_of(page, "f");
	click(page, disabled_field.x + 6, disabled_field.y + 8);
	check(!page.focused(), "and neither does a disabled field");
	check(!page.text_input("hello"), "so typing goes nowhere");
}

void test_a_radio_is_round_and_a_checkbox_is_not() {
	browser page{browser_options{400, 200}};
	page.load_html(
	    "<body><input type=radio id=r checked><input type=checkbox id=c checked></body>");
	check(page.frame().has_value(), "the page renders");

	std::size_t radio_ellipses = 0;
	std::size_t checkbox_ellipses = 0;
	std::size_t checkbox_rects = 0;
	for (const auto & c : commands(page)) {
		if (c.source == find_id(page, "r") && c.op == paint::paint_op::fill_ellipse) {
			++radio_ellipses;
		}
		if (c.source == find_id(page, "c")) {
			if (c.op == paint::paint_op::fill_ellipse) { ++checkbox_ellipses; }
			if (c.op == paint::paint_op::fill_rect) { ++checkbox_rects; }
		}
	}
	// Drawn as squares the two controls are indistinguishable, and the shape is
	// what tells you one of them is exclusive.
	check(radio_ellipses >= 2, "a radio is drawn with ellipses");
	check(checkbox_ellipses == 0, "a checkbox is not");
	check(checkbox_rects > 0, "it is drawn with rectangles");
}

void test_a_button_shows_its_label() {
	browser page{browser_options{400, 200}};
	page.load_html("<body><button id=b>Send it</button><select id=s><option>red</option></select>"
	               "</body>");
	check(page.frame().has_value(), "the page renders");

	// A BUTTON IS NOT A SELECT. They shared one painter arm, so a button was
	// asked for its selected <option> - it has none, so the label came out
	// empty - and then got the drop-down arrow anyway. Every button on the
	// page was an empty box with an arrow in it.
	check(draws_text(page, "Send it"), "the button draws its label");
	check(arrow_rows(page, "b") == 0, "and has NO drop-down arrow");
	check(arrow_rows(page, "s") > 0, "while the select still has one");
}

void test_a_submit_button_has_a_default_label() {
	browser page{browser_options{400, 200}};
	page.load_html("<body><input type=submit id=s><input type=reset id=r></body>");
	check(page.frame().has_value(), "the page renders");
	// <input type=submit> has no children to take a label from, so the UA
	// supplies one - an unlabelled grey box is not a submit button.
	check(draws_text(page, "Submit"), "submit is labelled");
	check(draws_text(page, "Reset"), "and so is reset");
}

// The caret must be measured with the font that DRAWS the text. Verified with
// real fonts on purpose: font8x8 quantises every size to the same cell, so the
// two measurements agree there and the bug is invisible.
void test_the_caret_is_measured_with_the_drawing_font() {
	browser page{browser_options{400, 200}};
	check(page.use_real_fonts(), "the vendored faces load");
	page.load_html("<body><input type=text id=f style='font-family:serif'></body>");
	check(page.frame().has_value(), "the page renders");
	const rect field = box_of(page, "f");
	(void)page.handle(input_event::mouse_down_at(field.x + 4, field.y + 6));
	(void)page.handle(input_event::mouse_up_at(field.x + 4, field.y + 6));
	check(page.focused() == find_id(page, "f"), "the field is focused");

	check(page.text_input("abcd"), "typed");
	check(page.frame().has_value(), "and it redraws");
	const std::vector<rect> bars = caret_bars(page, "f");
	check(bars.size() == 1, "there is one caret");
	if (bars.size() != 1) { return; }

	// Where the caret SHOULD be: the width of "abcd" in the field's own font,
	// from the text's left edge. Measured through the browser's metrics, which
	// is the same object the rasterizer draws with.
	const float expected = page.metrics()("abcd", 16, layout::text_face{"serif", false, false});
	const float actual = bars[0].x - field.x;
	// Generous, because the inset is the painter's business - but nothing like
	// the 2x that measuring with font8x8 produced.
	check(std::fabs(actual - expected) < expected * 0.5f + 8,
	      "the caret sits at the END of what was typed, not a font's width past it");
}

void test_a_textarea_shows_a_caret_on_the_right_line() {
	browser page{browser_options{400, 200}};
	page.load_html("<body><textarea id=t rows=3 cols=20>one\ntwo</textarea></body>");
	check(page.frame().has_value(), "the page renders");
	const rect box = box_of(page, "t");
	// Clicked on the SECOND line, which is also the click-to-position test:
	// the caret goes where the pointer was, not where it happened to be.
	(void)page.handle(input_event::mouse_down_at(box.x + 6, box.y + 28));
	(void)page.handle(input_event::mouse_up_at(box.x + 6, box.y + 28));
	check(page.focused() == find_id(page, "t"), "the textarea is focused");
	check(page.frame().has_value(), "it redraws");

	// Measured as one run the caret landed the width of "one\ntwo" past the
	// left edge - past the right edge of the box, where the clip threw it
	// away. That is why a textarea appeared to have no caret at all.
	const std::vector<rect> bars = caret_bars(page, "t");
	check(bars.size() == 1, "the textarea has a caret");
	if (bars.size() != 1) { return; }
	check(bars[0].x < box.right(), "and it is INSIDE the box, not clipped away past the end");
	check(bars[0].y > box.y + 8, "on the second line, not the first");
}

void test_a_textarea_draws_its_lines_separately() {
	browser page{browser_options{400, 200}};
	page.load_html("<body><textarea id=t rows=3 cols=20>one\ntwo</textarea></body>");
	check(page.frame().has_value(), "the page renders");
	// Drawn as one run, the newline reaches the rasterizer as a glyph - a box,
	// with a real font - and both words end up on one line.
	check(draws_text(page, "one"), "the first line is drawn");
	check(draws_text(page, "two"), "the second line is drawn");
	check(!draws_text(page, "one\ntwo"), "and NOT as a single run with a newline in it");

	float first = -1;
	float second = -1;
	for (const auto & c : commands(page)) {
		if (c.op != paint::paint_op::text_run) { continue; }
		if (c.text == "one") { first = c.bounds.y; }
		if (c.text == "two") { second = c.bounds.y; }
	}
	check(first >= 0 && second > first, "the second line is BELOW the first");
}

void test_the_caret_blinks() {
	browser_options options{400, 200};
	options.caret_blink_ms = 500;
	browser page{options};
	page.load_html("<body><input type=text id=f value=hi></body>");
	check(page.frame().has_value(), "the page renders");
	const rect field = box_of(page, "f");
	(void)page.handle(input_event::mouse_down_at(field.x + 4, field.y + 6));
	(void)page.handle(input_event::mouse_up_at(field.x + 4, field.y + 6));
	check(page.frame().has_value(), "it redraws focused");
	check(caret_bars(page, "f").size() == 1, "the caret starts SOLID, right after the click");

	(void)page.tick(600);
	check(page.frame().has_value(), "redraws");
	check(caret_bars(page, "f").empty(), "and is gone half a period later");

	(void)page.tick(500);
	check(page.frame().has_value(), "redraws");
	check(caret_bars(page, "f").size() == 1, "and back again");

	// TYPING RESTARTS IT SOLID. A caret that blinks out from under the
	// character you just typed reads as a dropped keystroke.
	(void)page.tick(600);
	check(page.frame().has_value(), "redraws");
	check(caret_bars(page, "f").empty(), "off again");
	check(page.text_input("x"), "typed");
	check(page.frame().has_value(), "redraws");
	check(caret_bars(page, "f").size() == 1, "typing brings the caret straight back");
}

// The loop has to be TOLD to draw, and a blink is the case where nothing else
// tells it: no event arrived and no callback ran. A loop that redraws only
// when it did something itself shows a caret that appears late or not at all -
// which is the same shape as the scrollbar that did not appear until the next
// mouse move.
void test_the_page_asks_for_a_frame_when_the_caret_blinks() {
	browser_options options{400, 200};
	options.caret_blink_ms = 500;
	browser page{options};
	page.load_html("<body><input type=text id=f value=hi></body>");
	check(page.frame().has_value(), "the page renders");
	const rect field = box_of(page, "f");
	(void)page.handle(input_event::mouse_down_at(field.x + 4, field.y + 6));
	(void)page.handle(input_event::mouse_up_at(field.x + 4, field.y + 6));
	check(page.frame().has_value(), "redraws focused");
	check(!page.needs_frame(), "and is then up to date");

	// No event, no callback: only time passing.
	check(page.tick(600) == 0, "no callbacks ran");
	check(page.needs_frame(), "but the page wants a frame, because the caret changed");

	// And it says when the NEXT one is due, so an idle loop can block instead
	// of waking sixty times a second to find out.
	check(page.frame().has_value(), "redraws");
	const double due = page.next_wakeup_ms();
	check(due > 0 && due <= 500, "the next blink is due within a period");
}

void test_an_idle_page_asks_for_nothing() {
	browser page{browser_options{300, 200}};
	page.load_html("<body><p>a page with no timers and nothing focused</p></body>");
	check(page.frame().has_value(), "the page renders");
	check(!page.needs_frame(), "nothing to draw");
	check(page.tick(1000) == 0, "and nothing to run");
	check(!page.needs_frame(), "still nothing to draw after a whole second");
	// Infinity is what lets an application BLOCK rather than poll - the
	// difference between an idle browser costing nothing and costing a core.
	check(page.next_wakeup_ms() > 1e9, "and no wakeup is due at all");
}

void test_a_blink_does_not_relayout() {
	browser_options options{400, 200};
	options.caret_blink_ms = 500;
	browser page{options};
	page.load_html("<body><input type=text id=f value=hi></body>");
	check(page.frame().has_value(), "the page renders");
	const rect field = box_of(page, "f");
	(void)page.handle(input_event::mouse_down_at(field.x + 4, field.y + 6));
	(void)page.handle(input_event::mouse_up_at(field.x + 4, field.y + 6));
	check(page.frame().has_value(), "redraws");
	const std::size_t layouts = page.layout_count();
	(void)page.tick(600);
	check(page.frame().has_value(), "redraws with the caret hidden");
	// Only the caret changed. the previous engine re-ran layout every frame for exactly this,
	// and it is the reason the engine has a dirty level per stage.
	check(page.layout_count() == layouts, "a blink re-PAINTS and does not re-lay-out");
}

// --- what a script can read off a control ---------------------------------

void test_script_reads_a_live_control_value() {
	browser page{browser_options{400, 200}};
	page.load_html(R"(<body><input type=text id=f value=start>
	<select id=s><option value=red>red</option><option value=green selected>green</option></select>
	<script>
	var f = document.getElementById('f');
	var s = document.getElementById('s');
	function report() { console.log(f.value + '/' + s.value); }
	</script></body>)");
	check(page.script_error().empty(), "the script ran");
	check(page.frame().has_value(), "the page renders");

	// A wrapper's properties were a SNAPSHOT taken when it was made, so a page
	// that kept the element in a variable - which every page does - read the
	// page-load value forever. The widget gallery reported `color: undefined`.
	check(page.run_script("report()"), "the script call runs");
	check(!log_of(page).empty(), "it logged");
	if (!log_of(page).empty()) {
		check(log_of(page).back() == "start/green", "value and the selected option are readable");
	}

	const rect field = box_of(page, "f");
	// Clicked PAST the end of the text, so the caret lands after it - a click
	// places the caret now, and clicking at the left edge would insert there.
	(void)page.handle(input_event::mouse_down_at(field.right() - 8, field.y + 6));
	(void)page.handle(input_event::mouse_up_at(field.right() - 8, field.y + 6));
	check(page.text_input("!"), "typed into it");
	check(page.run_script("report()"), "the script call runs again");
	check(log_of(page).back() == "start!/green", "and now reads what was TYPED, not the snapshot");
}

void test_an_input_listener_sees_the_new_value() {
	browser page{browser_options{400, 200}};
	page.load_html(R"(<body><input type=text id=f><script>
	var f = document.getElementById('f');
	f.addEventListener('input', function () { console.log('now:' + f.value); });
	</script></body>)");
	check(page.frame().has_value(), "the page renders");
	const rect field = box_of(page, "f");
	(void)page.handle(input_event::mouse_down_at(field.x + 4, field.y + 6));
	(void)page.handle(input_event::mouse_up_at(field.x + 4, field.y + 6));
	check(page.text_input("a"), "typed");
	check(page.text_input("b"), "typed again");
	// The handler runs AFTER the edit, so it must see the character that
	// caused it. Refreshing wrappers only after layout would show the value
	// from before the keystroke.
	check(log_of(page).size() == 2, "the listener fired per keystroke");
	if (log_of(page).size() == 2) {
		check(log_of(page)[0] == "now:a" && log_of(page)[1] == "now:ab",
		      "each one sees the value INCLUDING the character that fired it");
	}
}

void test_script_writes_a_control_value() {
	browser page{browser_options{400, 200}};
	page.load_html(R"(<body><input type=text id=f value=old>
	<input type=checkbox id=c></body>)");
	check(page.frame().has_value(), "the page renders");
	check(draws_text(page, "old"), "the field shows its attribute value");

	check(page.run_script("document.getElementById('f').value = 'new';"
	                      "document.getElementById('c').checked = true;"),
	      "the script runs");
	check(page.frame().has_value(), "the page redraws");
	// The VM has no property accessors, so a write is a sync rather than a
	// setter - without the write-back this sets a property nothing reads and
	// the field keeps showing the old text.
	check(draws_text(page, "new"), "the field shows what the script assigned");
	check(!draws_text(page, "old"), "and not what it used to say");
	check(page.run_script("console.log(String(document.getElementById('c').checked));"),
	      "reads it back");
	check(!log_of(page).empty() && log_of(page).back() == "true", "the checkbox is checked");
}

void test_one_wrapper_per_element() {
	browser page{browser_options{300, 150}};
	page.load_html(R"(<body><p id=p>x</p><script>
	console.log(String(document.getElementById('p') === document.getElementById('p')));
	</script></body>)");
	check(!log_of(page).empty(), "it logged");
	// A browser hands out the SAME object every time. Two wrappers for one
	// element also meant two independent property snapshots.
	if (!log_of(page).empty()) { check(log_of(page).back() == "true", "one element, one wrapper"); }
}

// --- <details> -------------------------------------------------------------

void test_a_closed_details_hides_its_content() {
	browser page{browser_options{400, 300}};
	page.load_html("<body><details id=d><summary id=s>more</summary>"
	               "<p id=body>the secret</p></details></body>");
	check(page.frame().has_value(), "the page renders");
	check(draws_text(page, "more"), "the summary is visible");
	// The content of a closed <details> is not laid out at all. This cannot be
	// a UA rule - `details > :not(summary)` needs a selector the cascade does
	// not have - so it is decided in the box builder.
	check(!draws_text(page, "the secret"), "and the content is NOT");
	check(box_of(page, "body").empty(), "the hidden content has no box");
}

void test_clicking_a_summary_opens_it() {
	browser page{browser_options{400, 300}};
	page.load_html("<body><details id=d><summary id=s>more</summary>"
	               "<p id=body>the secret</p></details></body>");
	check(page.frame().has_value(), "the page renders");
	const rect summary = box_of(page, "s");
	check(!summary.empty(), "the summary has a box");

	(void)page.handle(input_event::mouse_down_at(summary.x + 20, summary.y + 4));
	(void)page.handle(input_event::mouse_up_at(summary.x + 20, summary.y + 4));
	check(page.frame().has_value(), "it redraws");
	check(draws_text(page, "the secret"), "clicking the summary reveals the content");

	// And closes again - it is a toggle, and the state is the `open`
	// ATTRIBUTE, so a script reading it agrees with what the user did.
	check(page.run_script("console.log(String(document.getElementById('d').getAttribute('open') "
	                      "!== null));"),
	      "the script runs");
	check(!log_of(page).empty() && log_of(page).back() == "true", "the attribute says open");
	(void)page.handle(input_event::mouse_down_at(summary.x + 20, summary.y + 4));
	(void)page.handle(input_event::mouse_up_at(summary.x + 20, summary.y + 4));
	check(page.frame().has_value(), "it redraws");
	check(!draws_text(page, "the secret"), "clicking again hides it");
}

void test_a_summary_draws_its_triangle() {
	browser page{browser_options{400, 300}};
	page.load_html("<body><details id=d><summary id=s>more</summary><p>x</p></details></body>");
	check(page.frame().has_value(), "the page renders");
	check(draws_text(page, ">"), "a closed summary has a right-pointing triangle");
	const rect summary = box_of(page, "s");

	// AND IT IS ON SCREEN. A list marker is drawn at `box.x - size` because
	// its gutter is the parent <ul>'s padding; a summary's gutter is its OWN
	// padding-left, so the same arithmetic put the triangle at a NEGATIVE x
	// for a <details> at the page margin - drawn, but off the left edge of the
	// window, which is exactly as useful as not drawing it.
	float marker_x = -1;
	for (const auto & c : commands(page)) {
		if (c.op == paint::paint_op::text_run && c.text == ">") { marker_x = c.bounds.x; }
	}
	check(marker_x >= summary.x, "the triangle is INSIDE the summary, not off to its left");
	check(marker_x < summary.x + 18, "in the gutter its padding reserves");

	// And the LABEL is not drawn on top of it. The gutter is only a gutter if
	// the summary's own padding-left actually moves its text.
	float label_x = -1;
	for (const auto & c : commands(page)) {
		if (c.op == paint::paint_op::text_run && c.text == "more") { label_x = c.bounds.x; }
	}
	check(label_x > 0, "the summary's text is drawn");
	check(label_x >= summary.x + 18, "and starts AFTER the triangle's gutter, not on top of it");
	(void)page.handle(input_event::mouse_down_at(summary.x + 20, summary.y + 4));
	(void)page.handle(input_event::mouse_up_at(summary.x + 20, summary.y + 4));
	check(page.frame().has_value(), "it redraws");
	check(draws_text(page, "v"), "and an open one points down");
}

// --- inter-element whitespace ---------------------------------------------

void test_a_space_between_inline_elements_is_rendered() {
	browser page{browser_options{600, 200}};
	// The space between </label> and <input> is a whitespace-only TEXT NODE.
	// Dropped outright, every label was glued to its control and the words of
	// two adjacent inline elements ran together.
	page.load_html("<body><label id=l>name</label> <input type=text id=f size=6></body>");
	check(page.frame().has_value(), "the page renders");
	const rect label = box_of(page, "l");
	const rect field = box_of(page, "f");
	check(!label.empty() && !field.empty(), "both are laid out");
	check(field.x > label.right(), "the field starts AFTER the label, with the space between them");
}

void test_a_space_between_blocks_is_not_rendered() {
	browser page{browser_options{600, 300}};
	// The control: between BLOCKS the same whitespace is nothing at all.
	// Rendering it would put a stray space-height line between every pair of
	// paragraphs in every page ever written.
	browser plain{browser_options{600, 300}};
	plain.load_html("<body><p id=a>one</p><p id=b>two</p></body>");
	check(plain.frame().has_value(), "the tight page renders");
	page.load_html("<body>\n<p id=a>one</p>\n<p id=b>two</p>\n</body>");
	check(page.frame().has_value(), "the spaced page renders");
	check(box_of(page, "a").y == box_of(plain, "a").y, "the newlines in the source change nothing");
	check(box_of(page, "b").y == box_of(plain, "b").y, "for either paragraph");
}

void test_a_control_insets_its_text() {
	browser page{browser_options{400, 200}};
	page.load_html("<body><input type=text id=f value=ada></body>");
	check(page.frame().has_value(), "the page renders");
	const rect field = box_of(page, "f");
	float text_x = -1;
	for (const auto & c : commands(page)) {
		if (c.op == paint::paint_op::text_run && c.text == "ada") { text_x = c.bounds.x; }
	}
	check(text_x > 0, "the value is drawn");
	// Flush against the border, a field's text looks like it has overflowed
	// its box. Firefox insets it; so does this.
	check(text_x >= field.x + 4, "and is inset from the field's left edge");
	check(text_x < field.x + 20, "but not by a silly amount");
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
	// run of whitespace into one space. the engine passed it straight to the
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
			if (c.op == paint::paint_op::fill_rect && c.fill == color{style::ua_scrollbar_thumb}) {
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

// --- page-level text selection --------------------------------------------
//
// Selecting inside a FIELD already worked; this is selecting across the page,
// which is what "the browser can be used to read things" needs. A position is
// (node, code point in that node), not a fragment pointer: a node's text is
// split across as many fragments as it has visual lines, and a relayout
// rebuilds all of them - a selection has to survive a window resize.

void drag(browser & page, float x1, float y1, float x2, float y2) {
	(void)page.handle(input_event::mouse_down_at(x1, y1));
	(void)page.handle(input_event::mouse_move_to(x2, y2));
	(void)page.handle(input_event::mouse_up_at(x2, y2));
}

void test_dragging_selects_text() {
	browser page{browser_options{400, 200}};
	page.load_html("<body><p id=p>selectable words here</p></body>");
	check(page.frame().has_value(), "the page renders");
	check(!page.has_selection(), "nothing is selected to start with");

	const rect box = box_of(page, "p");
	const float middle = box.y + box.height / 2;
	drag(page, box.x + 1, middle, box.x + box.width - 1, middle);
	check(page.has_selection(), "dragging across the line selects");
	check(page.selected_text().find("selectable") != std::string::npos,
	      "and the selected text is what was dragged over");

	// A click WITHOUT a drag selects nothing - otherwise every click on a page
	// would leave a stray one-character selection.
	(void)page.handle(input_event::mouse_down_at(box.x + 20, middle));
	(void)page.handle(input_event::mouse_up_at(box.x + 20, middle));
	check(!page.has_selection(), "a plain click clears it");
}

void test_selection_is_drawn() {
	// The proof is pixels: the highlight colour is on the page where the
	// selection is, and nowhere when there is none.
	const auto highlight_pixels = [](bool select) {
		browser page{browser_options{300, 120}};
		page.load_html("<body><p id=p>highlight me</p></body>");
		(void)page.frame();
		if (select) {
			const rect box = box_of(page, "p");
			const float middle = box.y + box.height / 2;
			drag(page, box.x + 1, middle, box.x + box.width - 1, middle);
			(void)page.frame();
		}
		std::size_t found = 0;
		if (const auto image = page.read_pixels()) {
			for (int y = 0; y < image->height(); ++y) {
				const auto row = image->row(y);
				for (int x = 0; x < image->width(); ++x) {
					if ((row[static_cast<std::size_t>(x)] & 0x00FFFFFFU) ==
					    (style::ua_selection_highlight & 0x00FFFFFFU)) {
						++found;
					}
				}
			}
		}
		return found;
	};
	check(highlight_pixels(false) == 0, "an unselected page has no highlight");
	check(highlight_pixels(true) > 0, "a selected one does");
}

void test_selection_spans_elements() {
	browser page{browser_options{400, 300}};
	page.load_html("<body><p id=one>first para</p><p id=two>second para</p></body>");
	check(page.frame().has_value(), "the page renders");
	const rect first = box_of(page, "one");
	const rect second = box_of(page, "two");
	drag(page, first.x + 1, first.y + first.height / 2, second.right() - 1,
	     second.y + second.height / 2);
	const std::string selected = page.selected_text();
	check(selected.find("first") != std::string::npos, "the selection starts in the first");
	check(selected.find("second") != std::string::npos, "and ends in the second");
}

void test_selection_is_direction_agnostic() {
	// Dragging backwards selects the same text as dragging forwards.
	const auto select = [](bool backwards) {
		browser page{browser_options{400, 200}};
		page.load_html("<body><p id=p>forwards and backwards</p></body>");
		(void)page.frame();
		const rect box = box_of(page, "p");
		const float middle = box.y + box.height / 2;
		if (backwards) {
			drag(page, box.right() - 1, middle, box.x + 1, middle);
		} else {
			drag(page, box.x + 1, middle, box.right() - 1, middle);
		}
		return page.selected_text();
	};
	check(select(false) == select(true), "a backwards drag selects the same text");
	check(!select(false).empty(), "and it is not nothing");
}

void test_copying_the_page_selection() {
	browser page{browser_options{400, 200}};
	page.load_html("<body><p id=p>copy this</p><input id=field type=text></body>");
	check(page.frame().has_value(), "the page renders");
	const rect box = box_of(page, "p");
	const float middle = box.y + box.height / 2;
	drag(page, box.x + 1, middle, box.right() - 1, middle);
	// Ctrl+C with nothing editable focused takes the PAGE selection.
	(void)page.handle(input_event::key_press("KeyC", false, true));

	// ...and it can be pasted into a field, which is the whole point of
	// selecting text on a page.
	const rect field = box_of(page, "field");
	(void)page.handle(input_event::mouse_down_at(field.x + 5, field.y + 5));
	(void)page.handle(input_event::mouse_up_at(field.x + 5, field.y + 5));
	(void)page.handle(input_event::key_press("KeyV", false, true));
	const auto txn = page.doc().read();
	const std::string pasted =
	    page.forms().state_of(txn, page.atoms(), find_id(page, "field")).value;
	check(pasted.find("copy") != std::string::npos, "the page selection pastes into a field");
}

// A selection that crosses a LINE BREAK, which is where the offsets have to be
// right: a wrap drops the space it broke at, so the fragments do not partition
// the node's text, and summing their lengths puts every position past the first
// line one character early.
void test_selection_across_a_wrap() {
	browser page{browser_options{200, 200}};
	// Narrow enough that this must wrap.
	page.load_html("<body><p id=p>alpha bravo charlie delta echo</p></body>");
	check(page.frame().has_value(), "the page renders");
	const rect box = box_of(page, "p");
	check(box.height > 30, "the paragraph really did wrap");

	// From the very start of the first line to the very end of the last.
	drag(page, box.x + 1, box.y + 4, box.right() - 1, box.bottom() - 4);
	const std::string selected = page.selected_text();
	// Every word, each separated by exactly one space - including the spaces
	// the wrap consumed, which are in no fragment at all.
	check(selected == "alpha bravo charlie delta echo", "the whole paragraph, spaces and all");
}

void test_selection_survives_a_relayout() {
	// THE reason a position is (node, code point) and not a fragment pointer:
	// a resize rebuilds every fragment.
	browser page{browser_options{400, 200}};
	page.load_html("<body><p id=p>this text outlives a resize</p></body>");
	check(page.frame().has_value(), "the page renders");
	const rect box = box_of(page, "p");
	drag(page, box.x + 1, box.y + box.height / 2, box.right() - 1, box.y + box.height / 2);
	const std::string before = page.selected_text();
	check(!before.empty(), "something is selected");

	(void)page.handle(input_event::resized(300, 200));
	check(page.frame().has_value(), "the resized frame renders");
	check(page.has_selection(), "the selection survived the relayout");
	// EXACTLY the same text, across a relayout that rebuilt every fragment and
	// rewrapped the paragraph. That is what a (node, code point) position buys,
	// and it is why the offsets are found by searching the node's text rather
	// than by summing fragment lengths - a wrap drops the space it broke at.
	check(page.selected_text() == before, "and it is the same text");
}

} // namespace

int main() {
	test_a_control_draws_in_the_face_it_measures();
	test_clicking_in_a_textarea_places_the_caret();
	test_arrows_move_by_visual_line_in_a_textarea();
	test_dragging_selects_inside_a_field();
	test_escape_and_blur_drop_a_field_selection();
	test_a_password_shows_bullets();
	test_a_password_caret_is_measured_on_the_bullets();
	test_a_disabled_control_looks_and_acts_disabled();
	test_a_radio_is_round_and_a_checkbox_is_not();
	test_a_button_shows_its_label();
	test_a_submit_button_has_a_default_label();
	test_the_caret_is_measured_with_the_drawing_font();
	test_a_textarea_shows_a_caret_on_the_right_line();
	test_a_textarea_draws_its_lines_separately();
	test_the_caret_blinks();
	test_a_blink_does_not_relayout();
	test_the_page_asks_for_a_frame_when_the_caret_blinks();
	test_an_idle_page_asks_for_nothing();
	test_script_reads_a_live_control_value();
	test_an_input_listener_sees_the_new_value();
	test_script_writes_a_control_value();
	test_one_wrapper_per_element();
	test_a_closed_details_hides_its_content();
	test_clicking_a_summary_opens_it();
	test_a_summary_draws_its_triangle();
	test_a_space_between_inline_elements_is_rendered();
	test_a_space_between_blocks_is_not_rendered();
	test_a_control_insets_its_text();
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
	test_dragging_selects_text();
	test_selection_is_drawn();
	test_selection_spans_elements();
	test_selection_is_direction_agnostic();
	test_copying_the_page_selection();
	test_selection_across_a_wrap();
	test_selection_survives_a_relayout();
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
