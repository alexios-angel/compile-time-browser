// The .onclick PROPERTY idiom (el.onclick = fn), headless: a panel sets its
// onclick, and a click on its child button must bubble up and fire it - exactly
// how the Space-Invaders "PLAY AGAIN" panel restarts the game (it uses
// panel.onclick, not addEventListener). Also checks the onclick getter, direct
// (non-bubbling) onclick, reassignment, and that a miss does nothing. No SDL.
#include <ctbrowser.hpp>
#include <cstdio>

using page = ctbrowser::page<R"(<!DOCTYPE html>
<title>onclick</title>
<style>
  #panel{position:fixed;top:100px;left:100px;width:200px;height:120px}
  #btn{width:140px;height:50px}
  #other{position:fixed;top:400px;left:20px;width:60px;height:30px}
</style>
<div id="panel"><button id="btn">PLAY AGAIN</button></div>
<div id="other">x</div>
<script>
  var hits = 0;
  var direct = 0;
  var panel = document.getElementById("panel");
  panel.onclick = function () { hits = hits + 1; };
  var btn = document.getElementById("btn");
  btn.onclick = function () { direct = direct + 1; };
  var hasHandler = panel.onclick ? 1 : 0;   // reads back through the getter
</script>)">;

static int fails = 0;
#define CHECK(c)                                                                   \
	do {                                                                           \
		if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } \
	} while (0)

static int num(ctbrowser::engine<page> & e, const char * g) {
	return static_cast<int>(e.script[g].to_number());
}

int main() {
	ctbrowser::engine<page> e;
	CHECK(e.script.ok());
	if (!e.script.ok()) { std::printf("script threw: %s\n", e.script.exception_message().c_str()); return 1; }
	e.resize_viewport(500, 500);
	e.frame(500);

	ctbrowser::node * btn = e.doc.by_id("btn");
	ctbrowser::node * other = e.doc.by_id("other");
	CHECK(btn != nullptr && btn->w > 0);
	if (btn == nullptr) { return 1; }

	// baseline
	CHECK(num(e, "hits") == 0);
	CHECK(num(e, "direct") == 0);

	// click the button: fires btn.onclick (direct) AND bubbles to panel.onclick
	e.click_at(btn->x + btn->w / 2, btn->y + btn->h / 2);
	CHECK(num(e, "direct") == 1);
	CHECK(num(e, "hits") == 1); // <-- the "PLAY AGAIN" path: bubbled to the panel

	// a second click fires again
	e.click_at(btn->x + btn->w / 2, btn->y + btn->h / 2);
	CHECK(num(e, "hits") == 2);
	CHECK(num(e, "direct") == 2);

	// clicking elsewhere (outside the panel) fires neither
	if (other != nullptr && other->w > 0) {
		e.click_at(other->x + other->w / 2, other->y + other->h / 2);
	}
	CHECK(num(e, "hits") == 2);
	CHECK(num(e, "direct") == 2);

	// the onclick getter returned the assigned function at script time
	CHECK(num(e, "hasHandler") == 1);

	if (fails == 0) {
		std::printf("onclick: PASS (property handler fires + bubbles like the DOM)\n");
	}
	return fails == 0 ? 0 : 1;
}
