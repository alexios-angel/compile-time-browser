// The native <select> widget, headless: a page with a <select> + onchange is
// driven through the engine's mouse events - open on click, pick an option,
// verify the collapsed state, the popup hit rects, .value, and that onchange
// fires with the new value. No SDL.
#include <ctbrowser.hpp>
#include <cstdio>
#include <string>

using page = ctbrowser::page<R"(<!DOCTYPE html>
<title>select</title>
<style>#s{position:fixed;top:100px;left:100px;width:220px;font-size:16px}</style>
<select id="s">
  <option value="a">Alpha</option>
  <option value="b">Beta</option>
  <option value="c">Gamma</option>
</select>
<script>
  var changed = 0;
  var lastValue = "";
  var s = document.getElementById("s");
  s.getElementsByTagName("option")[1].selected = true;   // start on Beta
  s.onchange = function () { changed = changed + 1; lastValue = s.value; };
</script>)">;

static int fails = 0;
#define CHECK(c)                                                              \
	do {                                                                      \
		if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } \
	} while (0)

int main() {
	ctbrowser::engine<page> e;
	CHECK(e.script.ok());
	if (!e.script.ok()) { std::printf("script threw: %s\n", e.script.exception_message().c_str()); return 1; }
	e.resize_viewport(400, 400);
	e.frame(400);

	ctbrowser::node * sel = e.doc.by_id("s");
	CHECK(sel != nullptr && sel->is_select());
	if (sel == nullptr) { return 1; }
	CHECK(sel->option_count() == 3);
	CHECK(sel->selected_option() == 1); // option[1].selected = true -> Beta
	CHECK(!sel->select_open);
	CHECK(sel->nth_option(0)->w == 0);  // collapsed: options are not hit targets

	// click the collapsed control -> opens the popup
	const int cx = sel->x + sel->w / 2, cy = sel->y + sel->h / 2;
	e.mouse_button(cx, cy, true);
	e.mouse_button(cx, cy, false);
	CHECK(sel->select_open);
	const std::vector<ctbrowser::paint_cmd> paints = e.frame(400); // popup laid out
	ctbrowser::node * opt2 = sel->nth_option(2);
	CHECK(opt2 != nullptr && opt2->w > 0 && opt2->h > 0);
	// the popup's "Gamma" text paint must land on opt2's row (regression: the
	// overlay paints were left at the origin when the <select> was inside a
	// positioned ancestor, so they rendered at the top instead of below it)
	bool gamma_aligned = false;
	for (const ctbrowser::paint_cmd & p : paints) {
		if (p.what == ctbrowser::paint_cmd::kind::text && p.text == "Gamma" &&
		    p.y >= opt2->y && p.y < opt2->y + opt2->h) {
			gamma_aligned = true;
		}
	}
	CHECK(gamma_aligned);

	// click the third option (Gamma) -> selects it, closes, fires onchange
	const int ox = opt2->x + opt2->w / 2, oy = opt2->y + opt2->h / 2;
	e.mouse_button(ox, oy, true);
	e.mouse_button(ox, oy, false);
	CHECK(!sel->select_open);
	CHECK(sel->select_index == 2);
	CHECK(static_cast<int>(e.script["changed"].to_number()) == 1);
	CHECK(e.script["lastValue"].to_string() == "c"); // s.value read live in onchange

	// re-open and click OFF the list -> closes without changing selection
	e.mouse_button(cx, cy, true);
	e.mouse_button(cx, cy, false);
	CHECK(sel->select_open);
	e.frame(400);
	e.mouse_button(5, 5, true); // far from the popup
	e.mouse_button(5, 5, false);
	CHECK(!sel->select_open);
	CHECK(sel->select_index == 2);                                     // unchanged
	CHECK(static_cast<int>(e.script["changed"].to_number()) == 1);     // no extra onchange

	if (fails == 0) { std::printf("select suite: all checks passed\n"); }
	return fails ? 1 : 0;
}
