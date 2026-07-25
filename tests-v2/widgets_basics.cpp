// Form controls and the 2D canvas.
//
// Both are places where the engine draws something the document did not
// describe, and both keep their state OFF the DOM node - a control's value and
// caret in a form_store, a canvas's pixels in a canvas_store, each keyed by
// node_id. v1 put all of it on `node`, which is what left thirty layout- and
// UI-only fields on a struct that is supposed to be document content.
//
// The tests check behaviour through the browser rather than against the stores,
// because a control whose value changes without the page redrawing is not a
// working control, whatever a unit test of the store says.

import ctbrowser.core;
import ctbrowser.dom;
import ctbrowser.style;
import ctbrowser.layout;
import ctbrowser.paint;
import ctbrowser.raster;
import ctbrowser.shell;

#include "check.hpp"
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::control_kind;
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

[[nodiscard]] std::string value_of(browser & page, std::string_view id) {
	const auto txn = page.doc().read();
	const node_id node = find_id(page, id);
	return node ? page.forms().state_of(txn, page.atoms(), node).value : std::string{};
}

[[nodiscard]] bool checked_of(browser & page, std::string_view id) {
	const auto txn = page.doc().read();
	const node_id node = find_id(page, id);
	return node && page.forms().state_of(txn, page.atoms(), node).checked;
}

// Where a control ended up, so a test can click it rather than guessing.
[[nodiscard]] rect box_of(browser & page, std::string_view id) {
	const node_id want = find_id(page, id);
	const auto walk = [&](auto && self, const layout::fragment & f, float dx,
	                      float dy) -> rect {
		const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
		if (f.source == want && !box.empty()) { return box; }
		for (const auto & child : f.children) {
			if (const rect hit = self(self, child, box.x, box.y); !hit.empty()) { return hit; }
		}
		return rect{};
	};
	return walk(walk, page.fragments(), 0, 0);
}

void click(browser & page, const rect & where) {
	const float x = where.x + where.width / 2;
	const float y = where.y + where.height / 2;
	(void)page.handle(input_event::mouse_down_at(x, y));
	(void)page.handle(input_event::mouse_up_at(x, y));
}

// --- controls exist, are sized, and are drawn -----------------------------

void test_controls_are_sized_by_the_element() {
	browser page{browser_options{600, 300}};
	page.load_html("<html><body><input id=t type=text><input id=c type=checkbox>"
	               "<button id=b>Press</button></body></html>");
	check(page.frame().has_value(), "the page renders");

	const rect text_box = box_of(page, "t");
	const rect check_box = box_of(page, "c");
	const rect button_box = box_of(page, "b");

	// A REPLACED element is sized by what it is, not by what it contains.
	// Laying one out from its children gives a zero-height control, and taking
	// the block width gives a checkbox as wide as the page - which is what the
	// UA sheet's blanket `input { width: 160px }` did until it was removed.
	check(!text_box.empty() && text_box.width > 60, "a text field has a real width");
	check(check_box.width > 8 && check_box.width < 30, "a checkbox is small and square");
	check(check_box.height > 8 && check_box.height < 30, "in both directions");
	check(check_box.width < text_box.width / 3, "and much narrower than a text field");
	// A button is as wide as its label - the one replaced element whose size
	// does come from its content.
	check(button_box.width >= raster::font8x8_advance("Press", 16),
	      "a button is at least as wide as its label");
	check(button_box.width < 400, "but not as wide as the page");
}

void test_a_seeded_value_is_drawn() {
	browser page{browser_options{400, 200}};
	page.load_html(R"(<html><body><input id=t type=text value="hello"></body></html>)");
	check(page.frame().has_value(), "the page renders");
	// The value comes from the ATTRIBUTE until the user edits it.
	check(value_of(page, "t") == "hello", "the value attribute seeds the control");

	bool drawn = false;
	for (const auto & layer : page.layers().layers) {
		if (!layer.contents) { continue; }
		for (const auto & c : layer.contents->commands()) {
			if (c.op == paint::paint_op::text_run && c.text == "hello") { drawn = true; }
		}
	}
	check(drawn, "and it is painted into the field");
}

// --- editing ---------------------------------------------------------------

void test_typing_and_editing() {
	browser page{browser_options{400, 200}};
	page.load_html("<html><body><input id=t type=text></body></html>");
	check(page.frame().has_value(), "the page renders");

	// Nothing typed goes anywhere until something has focus.
	check(!page.text_input("ignored"), "typing with no focus does nothing");
	check(value_of(page, "t").empty(), "and the field is untouched");

	click(page, box_of(page, "t"));
	check(page.focused() == find_id(page, "t"), "clicking the field focuses it");
	check(page.text_input("abc"), "typing is accepted");
	check(value_of(page, "t") == "abc", "and lands in the value");

	check(page.handle(input_event::key_press("Backspace")), "backspace is handled");
	check(value_of(page, "t") == "ab", "and removes the last character");

	check(page.handle(input_event::key_press("Home")), "Home is handled");
	check(page.text_input("X"), "typing at the caret");
	check(value_of(page, "t") == "Xab", "inserts rather than appends");

	check(page.handle(input_event::key_press("End")), "End is handled");
	check(page.text_input("Z"), "typing at the end");
	check(value_of(page, "t") == "XabZ", "appends");
}

void test_backspace_deletes_a_whole_code_point() {
	browser page{browser_options{400, 200}};
	page.load_html("<html><body><input id=t type=text></body></html>");
	check(page.frame().has_value(), "the page renders");
	click(page, box_of(page, "t"));
	check(page.text_input("a\xc3\xa9"), "a multi-byte character is typed"); // 'a' + U+00E9
	check(value_of(page, "t") == "a\xc3\xa9", "and stored whole");
	(void)page.handle(input_event::key_press("Backspace"));
	// Deleting one BYTE would leave invalid UTF-8, which then renders as
	// replacement characters for the rest of the field.
	check(value_of(page, "t") == "a", "backspace removes the whole code point");
}

void test_editing_keys_do_not_scroll_the_page() {
	browser page{browser_options{300, 120}};
	page.load_html("<html><body><input id=t type=text value=abc>"
	               "<div style='height:600px'>tall</div></body></html>");
	check(page.frame().has_value(), "the page renders");
	click(page, box_of(page, "t"));
	const float before = page.scroll_y();
	(void)page.handle(input_event::key_press("Home"));
	(void)page.handle(input_event::key_press("End"));
	// Home in a focused field moves the caret. If it scrolled the page too,
	// every keystroke in a form would jump the document.
	check(page.scroll_y() == before, "Home and End move the caret, not the page");
}

void test_selection_and_replacement() {
	browser page{browser_options{400, 200}};
	page.load_html("<html><body><input id=t type=text value=hello></body></html>");
	check(page.frame().has_value(), "the page renders");
	click(page, box_of(page, "t"));
	// Ctrl+A, as the platform actually delivers it: the physical key plus the
	// modifier, not a made-up "SelectAll" name no keyboard produces.
	(void)page.handle(input_event::key_press("KeyA", false, true));
	check(page.text_input("bye"), "typing over a selection");
	check(value_of(page, "t") == "bye", "replaces it");
}

// --- checkboxes, radios and forms -----------------------------------------

void test_checkbox_toggles_on_click() {
	browser page{browser_options{400, 200}};
	page.load_html("<html><body><input id=c type=checkbox></body></html>");
	check(page.frame().has_value(), "the page renders");
	check(!checked_of(page, "c"), "it starts unchecked");
	click(page, box_of(page, "c"));
	check(checked_of(page, "c"), "clicking checks it");
	check(page.frame().has_value(), "the frame after renders");
	click(page, box_of(page, "c"));
	check(!checked_of(page, "c"), "clicking again unchecks it");
}

void test_radios_are_exclusive_within_a_name() {
	browser page{browser_options{500, 200}};
	page.load_html("<html><body>"
	               "<input id=a type=radio name=g><input id=b type=radio name=g>"
	               "<input id=c type=radio name=other></body></html>");
	check(page.frame().has_value(), "the page renders");
	click(page, box_of(page, "a"));
	click(page, box_of(page, "c"));
	check(checked_of(page, "a") && checked_of(page, "c"), "different groups are independent");
	click(page, box_of(page, "b"));
	// Selecting one radio clears every other radio with the SAME name, and
	// nothing else. Getting the grouping wrong makes a form with two radio
	// groups behave as one.
	check(checked_of(page, "b"), "the clicked radio is selected");
	check(!checked_of(page, "a"), "its group-mate is cleared");
	check(checked_of(page, "c"), "and the other group is untouched");
}

void test_form_submission_collects_successful_controls() {
	browser page{browser_options{600, 300}};
	page.load_html(R"(<html><body><form id=f>
	<input name=user type=text value=alice>
	<input name=agree type=checkbox checked>
	<input name=maybe type=checkbox>
	<input name=colour type=radio value=red>
	<button id=go type=submit>Go</button>
	</form></body></html>)");
	check(page.frame().has_value(), "the page renders");
	click(page, box_of(page, "go"));

	const auto & sent = page.last_submission();
	check(sent.size() == 2, "only successful controls are submitted");
	if (sent.size() >= 2) {
		check(sent[0].first == "user" && sent[0].second == "alice", "the text field, with its value");
		// An UNCHECKED box is not a successful control and is omitted entirely -
		// a server that sees the name at all knows it was ticked.
		check(sent[1].first == "agree", "the checked box");
	}
	bool has_maybe = false;
	for (const auto & [name, value] : sent) {
		if (name == "maybe" || name == "colour") { has_maybe = true; }
	}
	check(!has_maybe, "unchecked boxes and radios are omitted, not sent empty");
}

void test_reset_restores_the_markup() {
	browser page{browser_options{600, 300}};
	page.load_html(R"(<html><body><form id=f>
	<input id=t name=user type=text value=original>
	<button id=r type=reset>Reset</button>
	</form></body></html>)");
	check(page.frame().has_value(), "the page renders");
	click(page, box_of(page, "t"));
	check(page.text_input("!"), "the field is edited");
	check(value_of(page, "t") == "original!", "and shows the edit");
	click(page, box_of(page, "r"));
	check(value_of(page, "t") == "original", "reset restores the value attribute");
}

void test_submit_can_be_cancelled() {
	browser page{browser_options{600, 300}};
	page.load_html(R"(<html><body><form id=f>
	<input name=user type=text value=alice>
	<button id=go type=submit>Go</button>
	</form><script>
	document.getElementById('f').addEventListener('submit', function (e) {
	  console.log('submitting'); e.preventDefault();
	});
	</script></body></html>)");
	check(page.frame().has_value(), "the page renders");
	click(page, box_of(page, "go"));
	check(page.bindings().console_output().size() == 1, "the submit listener ran");
	// preventDefault on submit is how every client-validated form works.
	check(page.last_submission().empty(), "and preventDefault stopped the submission");
}

// --- script sees and sets control state ------------------------------------

void test_script_reads_and_writes_values() {
	browser page{browser_options{500, 250}};
	page.load_html(R"(<html><body>
	<input id=t type=text value=start><input id=c type=checkbox checked>
	<script>
	var t = document.getElementById('t');
	console.log('read ' + t.getValue());
	t.setValue('written');
	console.log('checked ' + document.getElementById('c').isChecked());
	</script></body></html>)");
	check(page.frame().has_value(), "the page renders");
	const auto & log = page.bindings().console_output();
	check(log.size() == 2, "two console lines");
	if (log.size() == 2) {
		check(log[0] == "read start", "script reads the seeded value");
		check(log[1] == "checked true", "and a checkbox's state");
	}
	check(value_of(page, "t") == "written", "and a write from script takes effect");
}

void test_script_can_focus() {
	browser page{browser_options{400, 200}};
	page.load_html(R"(<html><body><input id=t type=text><script>
	document.getElementById('t').focus();
	</script></body></html>)");
	check(page.frame().has_value(), "the page renders");
	check(page.focused() == find_id(page, "t"), "focus() moves focus");
	check(page.text_input("typed"), "so typing goes to the field");
	check(value_of(page, "t") == "typed", "without the user having clicked it");
}

// --- canvas ---------------------------------------------------------------

[[nodiscard]] const paint::bitmap * canvas_of(browser & page, std::string_view id) {
	const shell::canvas_context * found = page.canvases().find(find_id(page, id));
	return found == nullptr ? nullptr : found->surface().get();
}

void test_canvas_is_sized_by_its_attributes() {
	browser page{browser_options{600, 400}};
	page.load_html("<html><body><canvas id=c width=200 height=90></canvas>"
	               "<canvas id=d></canvas><script>"
	               "document.getElementById('c').getContext('2d');"
	               "document.getElementById('d').getContext('2d');</script></body></html>");
	check(page.frame().has_value(), "the page renders");
	const rect box = box_of(page, "c");
	check(box.width == 200 && box.height == 90, "the canvas box is its attribute size");
	// The HTML defaults, which pages rely on when they omit the attributes.
	const paint::bitmap * defaulted = canvas_of(page, "d");
	check(defaulted != nullptr && defaulted->width == 300 && defaulted->height == 150,
	      "a canvas with no attributes is 300x150");
}

void test_canvas_width_and_height_are_readable() {
	browser page{browser_options{400, 300}};
	page.load_html("<html><body><canvas id=c width=200 height=90></canvas>"
	               "<canvas id=d></canvas><script>"
	               "var c = document.getElementById('c');"
	               "var d = document.getElementById('d');"
	               "console.log(c.width + 'x' + c.height);"
	               "console.log(d.width + 'x' + d.height);"
	               "console.log('half=' + (c.width / 2));"
	               "</script></body></html>");
	check(page.frame().has_value(), "the page renders");
	const auto & log = page.bindings().console_output();
	check(log.size() == 3, "three console lines");
	if (log.size() != 3) { return; }
	check(log[0] == "200x90", "canvas.width and .height read the attributes");
	check(log[1] == "300x150", "and fall back to the HTML defaults");
	// The failure this guards is silent and total: without these they are
	// undefined, every coordinate computed from them is NaN, and the page draws
	// nothing while reporting no error at all. `canvas.width/2` is the first
	// line of most canvas pages.
	check(log[2] == "half=100", "so arithmetic on them works");
}

void test_getcontext_only_answers_for_2d() {
	browser page{browser_options{400, 300}};
	page.load_html("<html><body><canvas id=c></canvas><script>"
	               "var c = document.getElementById('c');"
	               "console.log('2d ' + (c.getContext('2d') != null));"
	               "console.log('webgl ' + (c.getContext('webgl') != null));"
	               "</script></body></html>");
	check(page.frame().has_value(), "the page renders");
	const auto & log = page.bindings().console_output();
	check(log.size() == 2, "two console lines");
	if (log.size() == 2) {
		check(log[0] == "2d true", "a 2d context exists");
		// Returning an object that cannot draw would be worse than null: a page
		// feature-detects with exactly this call and would take the WebGL path
		// into a dead end.
		check(log[1] == "webgl false", "and an unsupported context is null, not a stub");
	}
}

void test_canvas_drawing_reaches_the_pixels() {
	browser page{browser_options{400, 300}};
	page.load_html("<html><body><canvas id=c width=100 height=60></canvas><script>"
	               "var ctx = document.getElementById('c').getContext('2d');"
	               "ctx.fillStyle = '#ff0000';"
	               "ctx.fillRect(10, 10, 30, 20);"
	               "</script></body></html>");
	check(page.frame().has_value(), "the page renders");
	const paint::bitmap * pixels = canvas_of(page, "c");
	check(pixels != nullptr, "the canvas has a bitmap");
	if (pixels == nullptr) { return; }
	check(pixels->at(20, 20) == 0xFFFF0000u, "fillRect painted the interior");
	check(pixels->at(5, 5) == 0u, "and left the rest transparent");
	check(pixels->at(45, 20) == 0u, "including just past its right edge");
}

void test_fill_style_is_read_at_draw_time() {
	browser page{browser_options{400, 300}};
	page.load_html("<html><body><canvas id=c width=60 height=60></canvas><script>"
	               "var ctx = document.getElementById('c').getContext('2d');"
	               "ctx.fillStyle = '#00ff00'; ctx.fillRect(0, 0, 20, 20);"
	               "ctx.fillStyle = '#0000ff'; ctx.fillRect(30, 0, 20, 20);"
	               "</script></body></html>");
	check(page.frame().has_value(), "the page renders");
	const paint::bitmap * pixels = canvas_of(page, "c");
	check(pixels != nullptr, "the canvas has a bitmap");
	if (pixels == nullptr) { return; }
	// fillStyle is a PROPERTY the drawing calls read back. Capturing it when
	// the context is made, rather than at draw time, gives both rects the same
	// colour - and that is the whole canvas idiom broken.
	check(pixels->at(10, 10) == 0xFF00FF00u, "the first rect used the first fillStyle");
	check(pixels->at(35, 10) == 0xFF0000FFu, "and the second used the second");
}

void test_paths_transforms_and_clear() {
	browser page{browser_options{400, 300}};
	page.load_html("<html><body><canvas id=c width=80 height=80></canvas><script>"
	               "var ctx = document.getElementById('c').getContext('2d');"
	               "ctx.fillStyle = '#ffffff'; ctx.fillRect(0, 0, 80, 80);"
	               "ctx.translate(20, 20);"
	               "ctx.fillStyle = '#123456';"
	               "ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(20, 0);"
	               "ctx.lineTo(20, 20); ctx.lineTo(0, 20); ctx.closePath(); ctx.fill();"
	               "ctx.resetTransform(); ctx.clearRect(60, 60, 20, 20);"
	               "</script></body></html>");
	check(page.frame().has_value(), "the page renders");
	const paint::bitmap * pixels = canvas_of(page, "c");
	check(pixels != nullptr, "the canvas has a bitmap");
	if (pixels == nullptr) { return; }
	// The path was built in user space and drawn through the CTM: the square
	// is at 20,20 even though its points are 0,0.
	check(pixels->at(30, 30) == 0xFF123456u, "a filled path honours the transform");
	check(pixels->at(10, 10) == 0xFFFFFFFFu, "outside it is untouched");
	// clearRect makes pixels TRANSPARENT, not white - which is what lets an
	// overlay canvas show the page through it. resetTransform first, because
	// clearRect goes through the CTM like every other verb: without it this
	// clears at 80,80 and misses the canvas entirely.
	check(pixels->at(70, 70) == 0u, "clearRect clears to transparent");
}

void test_canvas_reaches_the_display_list_and_the_screen() {
	browser page{browser_options{200, 160}};
	page.load_html("<html><head><style>body { margin: 0; padding: 0 }</style></head>"
	               "<body><canvas id=c width=100 height=60></canvas><script>"
	               "var ctx = document.getElementById('c').getContext('2d');"
	               "ctx.fillStyle = '#ff0000'; ctx.fillRect(0, 0, 100, 60);"
	               "</script></body></html>");
	check(page.frame().has_value(), "the page renders");

	std::size_t images = 0;
	for (const auto & layer : page.layers().layers) {
		if (!layer.contents) { continue; }
		for (const auto & c : layer.contents->commands()) {
			if (c.op == paint::paint_op::image) { ++images; }
		}
	}
	check(images == 1, "the canvas is one image command in the display list");

	// And it made it all the way to pixels. A canvas that draws into its own
	// bitmap but never reaches the screen is the failure this catches.
	const auto image = page.read_pixels();
	check(image.has_value(), "the frame reads back");
	if (!image) { return; }
	check(image->row(10)[10] == 0xFFFF0000u, "the canvas is composited into the page");
}

void test_drawing_after_a_frame_redraws() {
	browser page{browser_options{200, 160}};
	page.load_html("<html><head><style>body { margin: 0; padding: 0 }</style></head>"
	               "<body><canvas id=c width=100 height=60></canvas><script>"
	               "var ctx = document.getElementById('c').getContext('2d');"
	               "ctx.fillStyle = '#ff0000'; ctx.fillRect(0, 0, 100, 60);"
	               "function later() { ctx.fillStyle = '#00ff00'; ctx.fillRect(0, 0, 100, 60); }"
	               "setTimeout(later, 1);</script></body></html>");
	check(page.frame().has_value(), "the first frame renders");
	const auto first = page.read_pixels();
	check(first.has_value() && first->row(10)[10] == 0xFFFF0000u, "and shows red");

	check(page.tick(5) == 1, "the timer runs and draws again");
	check(page.frame().has_value(), "the next frame renders");
	const auto second = page.read_pixels();
	// A canvas drawn into between frames must invalidate its tiles. Without
	// that the page keeps showing the old contents, which is the bug everyone
	// hits once and then never forgets.
	check(second.has_value() && second->row(10)[10] == 0xFF00FF00u,
	      "and the new canvas contents are on screen");
}

} // namespace

int main() {
	test_controls_are_sized_by_the_element();
	test_a_seeded_value_is_drawn();

	test_typing_and_editing();
	test_backspace_deletes_a_whole_code_point();
	test_editing_keys_do_not_scroll_the_page();
	test_selection_and_replacement();

	test_checkbox_toggles_on_click();
	test_radios_are_exclusive_within_a_name();
	test_form_submission_collects_successful_controls();
	test_reset_restores_the_markup();
	test_submit_can_be_cancelled();

	test_script_reads_and_writes_values();
	test_script_can_focus();

	test_canvas_is_sized_by_its_attributes();
	test_canvas_width_and_height_are_readable();
	test_getcontext_only_answers_for_2d();
	test_canvas_drawing_reaches_the_pixels();
	test_fill_style_is_read_at_draw_time();
	test_paths_transforms_and_clear();
	test_canvas_reaches_the_display_list_and_the_screen();
	test_drawing_after_a_frame_redraws();

	REPORT("widgets_basics");
}
