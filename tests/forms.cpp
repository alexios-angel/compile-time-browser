// Default click activations: checkbox toggling, radio groups, label
// forwarding, details/summary disclosure, and the anchor default (open
// the OS browser - here a test hook). Plus the UA stylesheet basics.

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
<title>forms</title>
<style>
	body { margin: 0 }
	input:checked { color: #112233 }
</style>
<h1 id=hd>Heading</h1>
<input type=checkbox id=c1 checked>
<input type=checkbox id=c2>
<input type=radio name=g id=r1 checked>
<input type=radio name=g id=r2>
<label for=c2 id=lab>toggle c2</label>
<label id=wrap><input type=checkbox id=c3>wrapped</label>
<details id=d><summary id=sum>more</summary><div id=content>hidden words</div></details>
<details id=d2 open><summary>always</summary><div id=content2>visible</div></details>
<a href=https://example.com/docs id=link>docs</a>
<a href=#sec id=frag>jump</a>
<hr id=rule>
<input type=text id=field value=secret>
<script>
	let changes = 0, r_changes = 0;
	getElementById("c1").addEventListener("change", function (e) { changes += 1; });
	getElementById("r2").onchange = function () { r_changes += 1; };
	function readback() { return document.location.href; }
	function set_checked() { getElementById("c2").checked = true; }
	function radio_set() { getElementById("r1").checked = true; }
	function read_open() { return getElementById("d").open; }
</script>)">;

int main() {
	ctbrowser::engine<page> e;
	CHECK(e.script.ok());

	std::string opened; // the anchor default-action hook (SDL_OpenURL in the shell)
	e.open_url = [&opened](std::string_view url) { opened = std::string{url}; };

	e.resize_viewport(500, 600);
	e.frame(500);

	ctbrowser::node * c1 = e.doc.by_id("c1");
	ctbrowser::node * c2 = e.doc.by_id("c2");
	ctbrowser::node * c3 = e.doc.by_id("c3");
	ctbrowser::node * r1 = e.doc.by_id("r1");
	ctbrowser::node * r2 = e.doc.by_id("r2");
	CHECK(c1 && c2 && c3 && r1 && r2);

	const auto click = [&e](ctbrowser::node * n) {
		const double x = n->x + n->w / 2.0, y = n->y + n->h / 2.0;
		e.mouse_button(x, y, true);
		e.mouse_button(x, y, false);
	};

	// --- the checked attribute initializes state; :checked styling applies
	CHECK(c1->checked);
	CHECK(!c2->checked);
	{
		const auto chain = c1->chain();
		CHECK(e.resolve(chain.data(), chain.size(), "color") == "#112233");
	}

	// --- checkbox chrome paints the Firefox accent when checked
	{
		bool accent = false;
		for (const auto & cmd : e.frame(500)) {
			if (cmd.what == ctbrowser::paint_cmd::kind::box && cmd.argb == 0xFF0060DFu) { accent = true; }
		}
		CHECK(accent);
	}

	// --- clicking toggles, fires change; clicking again untoggles
	click(c1);
	CHECK(!c1->checked);
	CHECK(e.script["changes"].to<int>() == 1);
	click(c1);
	CHECK(c1->checked);
	CHECK(e.script["changes"].to<int>() == 2);

	// --- radio group: attribute init, exclusive check, change on the new one
	CHECK(r1->checked);
	CHECK(!r2->checked);
	click(r2);
	CHECK(r2->checked);
	CHECK(!r1->checked);
	CHECK(e.script["r_changes"].to<int>() == 1);
	click(r2); // clicking a checked radio is a no-op
	CHECK(r2->checked);
	CHECK(e.script["r_changes"].to<int>() == 1);

	// --- programmatic .checked: no change event; radio keeps the group invariant
	e.script.call("set_checked");
	CHECK(c2->checked);
	e.script.call("radio_set");
	CHECK(r1->checked);
	CHECK(!r2->checked);
	CHECK(e.script["r_changes"].to<int>() == 1);
	e.script.call("set_checked"); // reset c2 for the label test below
	c2->checked = false;

	// --- label for= forwards; a wrapping label forwards to its control
	click(e.doc.by_id("lab"));
	CHECK(c2->checked);
	ctbrowser::node * wrap = e.doc.by_id("wrap");
	{
		// click the label's text area (right of the wrapped checkbox)
		const double x = wrap->x + wrap->w - 4.0, y = wrap->y + wrap->h / 2.0;
		e.mouse_button(x, y, true);
		e.mouse_button(x, y, false);
	}
	CHECK(c3->checked);

	// --- details: closed hides content (zero rects, nothing painted there)
	ctbrowser::node * d = e.doc.by_id("d");
	ctbrowser::node * content = e.doc.by_id("content");
	CHECK(!d->open);
	CHECK(content->w == 0 && content->h == 0);
	CHECK(e.doc.by_id("content2")->w > 0); // the open one shows its body
	click(e.doc.by_id("sum"));
	CHECK(d->open);
	e.frame(500);
	CHECK(content->w > 0);
	CHECK(e.script.call("read_open").to<bool>());

	// --- the summary disclosure marker: present, and its direction
	// tracks the open state (a right/down triangle in the gutter)
	{
		ctbrowser::node * sum2 = e.doc.by_id("sum");
		std::size_t closed_marks = 0, open_marks = 0;
		CHECK(d->open); // opened by the click above
		for (const auto & cmd : e.frame(500)) {
			if (cmd.what == ctbrowser::paint_cmd::kind::box && cmd.h == 1 && cmd.w > 2 &&
			    sum2 != nullptr && cmd.y >= sum2->y && cmd.y < sum2->y + sum2->h && cmd.x < sum2->x + 18) {
				++open_marks; // down-triangle rows are wide-and-1px-tall
			}
		}
		CHECK(open_marks > 2);
		click(e.doc.by_id("sum")); // close it again
		for (const auto & cmd : e.frame(500)) {
			if (cmd.what == ctbrowser::paint_cmd::kind::box && cmd.w == 1 && cmd.h > 2 &&
			    sum2 != nullptr && cmd.y >= sum2->y && cmd.y < sum2->y + sum2->h && cmd.x < sum2->x + 18) {
				++closed_marks; // right-triangle columns are 1px-wide-and-tall
			}
		}
		CHECK(closed_marks > 2);
		click(e.doc.by_id("sum")); // restore open for later checks
		e.frame(500);
	}

	// --- anchor default: the OS-browser hook gets the href
	click(e.doc.by_id("link"));
	CHECK(opened == "https://example.com/docs");
	CHECK(e.script.call("readback").to<std::string>() == "https://example.com/docs");
	opened.clear();
	click(e.doc.by_id("frag")); // fragment: hash only, no browser launch
	CHECK(opened.empty());
	CHECK(e.ev.location_hash == "#sec");

	// --- UA defaults: Firefox heading scale + link color + hr band + input text
	{
		ctbrowser::node * hd = e.doc.by_id("hd");
		const auto chain = hd->chain();
		CHECK(e.resolve(chain.data(), chain.size(), "font-size") == "32px");
		ctbrowser::node * link = e.doc.by_id("link");
		const auto lchain = link->chain();
		CHECK(e.resolve(lchain.data(), lchain.size(), "color") == "#0000ee");
		ctbrowser::node * rule = e.doc.by_id("rule");
		CHECK(rule->h == 2);
		bool field_text = false;
		for (const auto & cmd : e.frame(500)) {
			if (cmd.what == ctbrowser::paint_cmd::kind::text && cmd.text == U"secret") { field_text = true; }
		}
		CHECK(field_text);
	}

	if (fails == 0) { std::printf("forms suite: all checks passed\n"); }
	return fails ? 1 : 0;
}
