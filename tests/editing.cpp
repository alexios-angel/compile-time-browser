// Text editing: typing via text_input, the editing keys (code-point
// aware), caret rendering, change-on-blur, input events, and implicit
// form submission from a text input. Headless.

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
<title>editing</title>
<style> body { margin: 0 } </style>
<form id=f>
	<input type=text id=t value=hi>
	<input type=password id=pw>
</form>
<textarea id=ta rows=3 cols=10>ab
cd</textarea>
<script>
	let inputs = 0, changes = 0, submits = 0, edits_blocked = 0;
	getElementById("t").addEventListener("input", function (e) { inputs += 1; });
	getElementById("t").addEventListener("change", function (e) { changes += 1; });
	getElementById("f").onsubmit = function (e) { submits += 1; };
	function block_keys() {
		document.addEventListener("keydown", function (e) { e.preventDefault(); edits_blocked += 1; });
	}
	function read_t() { return getElementById("t").value; }
	function set_t(v) { getElementById("t").value = v; }
</script>)">;

int main() {
	ctbrowser::engine<page> e;
	CHECK(e.script.ok());
	e.resize_viewport(400, 300);
	e.frame(400);

	ctbrowser::node * t = e.doc.by_id("t");
	ctbrowser::node * ta = e.doc.by_id("ta");
	CHECK(t && ta);
	CHECK(t->value == "hi");     // value attribute initializes
	CHECK(ta->value == "ab\ncd"); // textarea content initializes

	const auto click = [&e](ctbrowser::node * n) {
		const double x = n->x + n->w / 2.0, y = n->y + n->h / 2.0;
		e.mouse_button(x, y, true);
		e.mouse_button(x, y, false);
	};

	// --- focus, type, and the caret follows
	click(t);
	CHECK(t->focused);
	CHECK(t->caret == 2); // caret at end on focus
	e.text_input("!");
	CHECK(t->value == "hi!");
	CHECK(t->caret == 3);
	CHECK(e.script["inputs"].to<int>() == 1);

	// --- editing keys: Left/Backspace/Home/End/Delete, code-point aware
	e.key("Left", true);
	e.key("Left", false);
	CHECK(t->caret == 2);
	e.key("Backspace", true);
	e.key("Backspace", false);
	CHECK(t->value == "h!");
	CHECK(t->caret == 1);
	e.key("Home", true);
	e.key("Home", false);
	CHECK(t->caret == 0);
	e.key("Delete", true);
	e.key("Delete", false);
	CHECK(t->value == "!");
	e.key("End", true);
	e.key("End", false);
	e.text_input("\xc3\xa9"); // é - two bytes, one code point
	CHECK(t->value == "!\xc3\xa9");
	e.key("Backspace", true);
	e.key("Backspace", false);
	CHECK(t->value == "!"); // the WHOLE code point went

	// --- caret bar paints while focused
	{
		bool caret_bar = false;
		for (const auto & cmd : e.frame(400)) {
			if (cmd.what == ctbrowser::paint_cmd::kind::box && cmd.w == 1 && cmd.h > 8 &&
			    cmd.y >= t->y && cmd.y < t->y + t->h) {
				caret_bar = true;
			}
		}
		CHECK(caret_bar);
	}

	// --- change fires on BLUR of an edited control
	CHECK(e.script["changes"].to<int>() == 0);
	click(ta); // focus moves away
	CHECK(e.script["changes"].to<int>() == 1);

	// --- textarea editing: Enter inserts a newline; Up/Down move lines
	CHECK(ta->focused);
	e.key("End", true);
	e.key("End", false);
	e.text_input("!");
	CHECK(ta->value == "ab\ncd!");
	e.key("Return", true);
	e.key("Return", false);
	e.text_input("ef");
	CHECK(ta->value == "ab\ncd!\nef");
	e.key("Up", true);
	e.key("Up", false);
	e.text_input("X");
	CHECK(ta->value == "ab\ncdX!\nef");

	// --- programmatic .value: no events fire
	const int inputs_before = e.script["inputs"].to<int>();
	e.script.call("set_t", std::string{"reset"});
	CHECK(t->value == "reset");
	CHECK(e.script["inputs"].to<int>() == inputs_before); // unchanged
	CHECK(e.script.call("read_t").to<std::string>() == "reset");

	// --- implicit submission: Enter in a text input submits its form
	click(t);
	e.key("Return", true);
	e.key("Return", false);
	CHECK(e.script["submits"].to<int>() == 1);

	// --- textarea SOFT WRAP: a long line spans multiple visual lines,
	// Up/Down navigate them, nothing is lost
	{
		ta->value = "the quick brown fox jumps over the lazy dog again and again";
		ta->caret = static_cast<std::int32_t>(ta->value.size());
		ta->sel_anchor = -1;
		e.frame(400);
		CHECK(ta->ui_lines.size() > 1); // wrapped (cols=10 box)
		CHECK(!ta->ui_lines.empty() &&
		      ta->ui_lines.back().cp_end == static_cast<std::int32_t>(ta->value.size()));
		const auto click_ta = [&] {
			const double x = ta->x + 5.0, y = ta->y + 5.0;
			e.mouse_button(x, y, true);
			e.mouse_button(x, y, false);
		};
		click_ta();
		e.key("End", true);
		e.key("End", false); // END of the FIRST visual line, not the value
		CHECK(ta->caret < static_cast<std::int32_t>(ta->value.size()));
		const std::int32_t first_end = ta->caret;
		e.key("Down", true);
		e.key("Down", false); // down a VISUAL line
		CHECK(ta->caret > first_end);
	}

	// --- input horizontal scroll: the view holds still while the caret
	// moves inside it, and scrolls minimally at the edges
	{
		click(t);
		e.script.call("set_t", std::string{"abcdefghijklmnopqrstuvwxyz0123456789"});
		t->caret = static_cast<std::int32_t>(t->value.size());
		e.frame(400); // the view scrolls to show the caret at the end
		const std::int32_t scrolled = t->scroll_cp;
		CHECK(scrolled > 0);
		e.key("Left", true);
		e.key("Left", false); // moving WITHIN the view: the view stays put
		e.frame(400);
		CHECK(t->scroll_cp == scrolled);
		e.key("Home", true);
		e.key("Home", false); // caret left of the view: minimal scroll to it
		e.frame(400);
		CHECK(t->scroll_cp == 0);
		// clicking in the visible window maps through the scroll offset
		e.key("End", true);
		e.key("End", false);
		e.frame(400);
		const std::int32_t back = t->scroll_cp;
		CHECK(back > 0);
		e.mouse_button(t->ui_text_x + 2.0, t->y + t->h / 2.0, true);
		e.mouse_button(t->ui_text_x + 2.0, t->y + t->h / 2.0, false);
		CHECK(t->caret >= back); // the first VISIBLE char, not char 0
	}

	// --- the caret blinks on Chrome's cadence (500 ms halves)
	{
		click(t);
		e.frame(400);
		CHECK(t->ui_caret_on); // fresh activity: solid
		e.tick(0.7);           // 700 ms with no caret activity: off phase
		e.frame(400);
		CHECK(!t->ui_caret_on);
		e.tick(0.4); // 1.1 s: back on
		e.frame(400);
		CHECK(t->ui_caret_on);
		e.text_input("!"); // typing restarts the phase: solid again
		e.frame(400);
		CHECK(t->ui_caret_on);
		e.script.call("set_t", std::string{"reset"}); // restore for the block below
	}

	// --- preventDefault on keydown suppresses the editing default
	e.script.call("block_keys");
	e.text_input("Z"); // text_input is not a key event - still lands
	CHECK(t->value == "resetZ");
	const std::string before = t->value;
	e.key("Backspace", true);
	e.key("Backspace", false);
	CHECK(t->value == before); // the listener blocked the edit
	CHECK(e.script["edits_blocked"].to<int>() >= 1);

	if (fails == 0) { std::printf("editing suite: all checks passed\n"); }
	return fails ? 1 : 0;
}
