// The interaction model: :hover/:active/:focus restyling driven by real
// mouse state, mouseover/mouseout dispatch, click-on-release semantics
// (browser-correct), a REAL preventDefault, and the disabled gate.
// Headless: drives engine::mouse_move/mouse_button directly.

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
<title>interact</title>
<style>
	body { margin: 0 }
	div { margin: 0; padding: 8px }
	#b        { width: 100px; height: 40px }
	#b:hover  { color: #ff0000 }
	#b:active { color: #0000ff }
	#panel:hover .child { color: #00ff00 }
	#fb:focus { background-color: #123456 }
</style>
<div id=b>hover me</div>
<div id=panel><div class=child id=kid>nested</div></div>
<button id=fb>focus me</button>
<button id=db disabled>disabled</button>
<input type=checkbox id=guard>
<script>
	let overs = 0, outs = 0, clicks = 0, panel_clicks = 0, disabled_clicks = 0;
	document.addEventListener("mouseover", function (e) { overs += 1; });
	document.addEventListener("mouseout", function (e) { outs += 1; });
	getElementById("b").onclick = function (e) { clicks += 1; };
	getElementById("panel").onclick = function (e) { panel_clicks += 1; };
	getElementById("db").onclick = function (e) { disabled_clicks += 1; };
	getElementById("guard").addEventListener("click", function (e) { e.preventDefault(); });
</script>)">;

static std::string_view color_of(ctbrowser::node * n, const ctbrowser::engine<page> & e) {
	const auto chain = n->chain();
	return e.resolve(chain.data(), chain.size(), "color");
}

int main() {
	ctbrowser::engine<page> e;
	CHECK(e.script.ok());
	e.resize_viewport(400, 400);
	e.frame(400);

	ctbrowser::node * b = e.doc.by_id("b");
	ctbrowser::node * kid = e.doc.by_id("kid");
	ctbrowser::node * fb = e.doc.by_id("fb");
	ctbrowser::node * db = e.doc.by_id("db");
	ctbrowser::node * guard = e.doc.by_id("guard");
	CHECK(b && kid && fb && db && guard);

	const auto cx = [](ctbrowser::node * n) { return static_cast<double>(n->x + n->w / 2); };
	const auto cy = [](ctbrowser::node * n) { return static_cast<double>(n->y + n->h / 2); };

	// --- :hover restyles, and un-restyles on leave
	CHECK(color_of(b, e).empty());
	e.mouse_move(cx(b), cy(b));
	CHECK(b->hovered);
	CHECK(color_of(b, e) == "#ff0000");
	e.mouse_move(1.0, 399.0); // empty space at the bottom
	CHECK(!b->hovered);
	CHECK(color_of(b, e).empty());

	// --- ancestor hover: #panel:hover .child
	CHECK(color_of(kid, e).empty());
	e.mouse_move(cx(kid), cy(kid));
	CHECK(color_of(kid, e) == "#00ff00");
	e.mouse_move(1.0, 399.0);

	// --- mouseover/mouseout fired on target changes
	CHECK(e.script["overs"].to<int>() >= 2);
	CHECK(e.script["outs"].to<int>() >= 2);

	// --- click fires on RELEASE, not press
	e.mouse_button(cx(b), cy(b), true);
	CHECK(e.script["clicks"].to<int>() == 0);
	CHECK(color_of(b, e) == "#0000ff"); // :active while held
	e.mouse_button(cx(b), cy(b), false);
	CHECK(e.script["clicks"].to<int>() == 1);
	CHECK(color_of(b, e) == "#ff0000"); // released, still hovered

	// --- press inside, release outside: no click
	e.mouse_button(cx(b), cy(b), true);
	e.mouse_button(1.0, 399.0, false);
	CHECK(e.script["clicks"].to<int>() == 1);

	// --- press on child, release on parent: the common ancestor clicks
	e.mouse_button(cx(kid), cy(kid), true);
	ctbrowser::node * panel = e.doc.by_id("panel");
	e.mouse_button(static_cast<double>(panel->x + 1), static_cast<double>(panel->y + 1), false);
	CHECK(e.script["panel_clicks"].to<int>() == 1);

	// --- :focus lands on mousedown and persists after release
	e.mouse_button(cx(fb), cy(fb), true);
	e.mouse_button(cx(fb), cy(fb), false);
	CHECK(fb->focused);
	{
		const auto chain = fb->chain();
		CHECK(e.resolve(chain.data(), chain.size(), "background-color") == "#123456");
	}
	e.mouse_button(1.0, 399.0, true); // click empty space
	e.mouse_button(1.0, 399.0, false);
	CHECK(!fb->focused);

	// --- preventDefault suppresses the checkbox default action
	e.frame(400);
	CHECK(!guard->checked);
	e.mouse_button(cx(guard), cy(guard), true);
	e.mouse_button(cx(guard), cy(guard), false);
	CHECK(!guard->checked); // the listener prevented the toggle

	// --- a disabled control dispatches nothing and takes no focus
	e.mouse_button(cx(db), cy(db), true);
	e.mouse_button(cx(db), cy(db), false);
	CHECK(e.script["disabled_clicks"].to<int>() == 0);
	CHECK(!db->focused);
	{
		const auto chain = db->chain();
		CHECK(e.resolve(chain.data(), chain.size(), "color") == "#8f8f9d"); // UA :disabled
	}

	if (fails == 0) { std::printf("interact suite: all checks passed\n"); }
	return fails ? 1 : 0;
}
