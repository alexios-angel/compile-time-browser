// The hero demo: one HTML string is the whole application. The markup,
// the CSS and the JS all parse WHILE THIS FILE COMPILES - break any of
// the three and the build fails - and at runtime SDL3 opens a window,
// clicks flow into the script, the script mutates the DOM, ctcss
// restyles, and the layout reflows.
//
// Build: make counter   (or the CMake examples; needs SDL3)

#include <ctbrowser.hpp>
#include <ctbrowser/app.hpp>
#include <SDL3/SDL_main.h>

using app = ctbrowser::page<R"(<!DOCTYPE html>
<title>counter</title>
<style>
	body    { font-size: 16px; padding: 16px; }
	h1      { font-size: 32px; color: #222222; }
	#count  { font-size: 48px; color: gray; padding: 8px; }
	#count.hot { color: #ff8800; }
	#panel  { background-color: #eeeeee; padding: 12px; width: 320px; }
	.hint   { color: #888888; }
</style>
<h1>Clicks</h1>
<div id=panel>
	<p id=count>0</p>
	<p class=hint>click the number</p>
</div>
<script>
	let clicks = 0;
	function onClick(id) {
		if (id !== "count" && id !== "panel") { return; }
		clicks += 1;
		let el = getElementById("count");
		el.setText(String(clicks));
		if (clicks >= 5) { el.addClass("hot"); }
		setTitle("clicked " + clicks);
	}
</script>)">;

// initial style, proven during compilation
constexpr ctcss::element_ref initial[] = {{"html"}, {"body"}, {"div", "panel", ""},
                                          {"p", "count", ""}};
static_assert(ctcss::query(ctcss::parse_value(app::style_text()), initial, "color") == "gray");
static_assert(ctjs::vp::is_valid(app::script_text()));

int main(int, char **) {
	return ctbrowser::run_app<app>();
}
