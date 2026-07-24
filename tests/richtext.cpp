// The Firefox-fidelity rendering pass, headless: table grid geometry,
// list markers, bold/italic/decoration stamping on paint cmds, the
// serif/sans/mono UA families, and form submit/reset defaults.

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
<title>richtext</title>
<style> body { margin: 0 } </style>
<h1 id=hd>Title</h1>
<p><b id=bold>bold bit</b></p>
<p><i id=ital>italic bit</i></p>
<a href=#x id=lnk>a link</a>
<s id=struck>gone</s>
<code id=cd>mono()</code>
<ul id=ul><li>one</li><li>two</li></ul>
<ol id=ol><li>first</li><li>second</li></ol>
<table id=tb border=1>
	<caption id=cap>numbers</caption>
	<tr><th id=h1>a</th><th>b</th></tr>
	<tr><td id=c11>1</td><td id=c12>2</td></tr>
</table>
<form id=f>
	<input type=text id=t value=orig>
	<input type=checkbox id=cb checked>
	<button id=go>Send</button>
	<button type=reset id=undo>Reset</button>
</form>
<script>
	let submits = 0;
	getElementById("f").addEventListener("submit", function (e) { submits += 1; });
	function do_submit() { getElementById("f").submit(); }
	function do_reset() { getElementById("f").reset(); }
</script>)">;

int main() {
	ctbrowser::engine<page> e;
	CHECK(e.script.ok());
	e.resize_viewport(600, 800);
	const auto cmds = e.frame(600);

	const auto text_cmd = [&cmds](std::u32string_view needle) -> const ctbrowser::paint_cmd * {
		for (const auto & c : cmds) {
			if (c.what == ctbrowser::paint_cmd::kind::text &&
			    c.text.find(std::u32string{needle}) != std::u32string::npos) {
				return &c;
			}
		}
		return nullptr;
	};

	// --- weight/style/decoration stamped from the UA sheet
	const auto * hd = text_cmd(U"Title");
	CHECK(hd && hd->bold && hd->font_px == 32); // h1: bold 2em
	const auto * bd = text_cmd(U"bold bit");
	CHECK(bd && bd->bold && !bd->italic);
	const auto * it = text_cmd(U"italic bit");
	CHECK(it && it->italic && !it->bold);
	const auto * lk = text_cmd(U"a link");
	CHECK(lk && lk->deco == ctbrowser::paint_cmd::strike::underline);
	CHECK(lk && (lk->argb & 0xFFFFFFu) == 0x0000EEu); // Gecko link blue
	const auto * st = text_cmd(U"gone");
	CHECK(st && st->deco == ctbrowser::paint_cmd::strike::line_through);
	const auto * cd = text_cmd(U"mono()");
	CHECK(cd && cd->font_family.find("monospace") != std::string::npos);
	CHECK(hd && hd->font_family.find("serif") != std::string::npos); // inherited from body

	// decoration bands: a 1px box right under the link text
	{
		bool band = false;
		for (const auto & c : cmds) {
			if (c.what == ctbrowser::paint_cmd::kind::box && c.h == 1 && lk != nullptr &&
			    c.y == lk->y + lk->h + 1 && c.x == lk->x) {
				band = true;
			}
		}
		CHECK(band);
	}

	// --- list markers: a disc box left of ul items, "1." text for ol
	{
		ctbrowser::node * ul = e.doc.by_id("ul");
		bool disc = false;
		for (const auto & c : cmds) {
			if (c.what == ctbrowser::paint_cmd::kind::box && ul != nullptr && c.y > ul->y &&
			    c.y < ul->y + ul->h && c.w >= 3 && c.w == c.h) {
				disc = true;
			}
		}
		CHECK(disc);
		CHECK(text_cmd(U"1.") != nullptr);
		CHECK(text_cmd(U"2.") != nullptr);
	}

	// --- table: caption above, equal columns, th centered-bold, frames
	{
		ctbrowser::node * cap = e.doc.by_id("cap");
		ctbrowser::node * h1c = e.doc.by_id("h1");
		ctbrowser::node * c11 = e.doc.by_id("c11");
		ctbrowser::node * c12 = e.doc.by_id("c12");
		CHECK(cap && h1c && c11 && c12);
		CHECK(cap->y < h1c->y);           // caption above the grid
		CHECK(c11->w == c12->w);          // equal columns
		CHECK(c12->x > c11->x + c11->w);  // side by side with spacing
		CHECK(c11->y == c12->y);          // same row
		const ctbrowser::paint_cmd * th = nullptr;
		for (const auto & c : cmds) {
			if (c.what == ctbrowser::paint_cmd::kind::text && c.text == U"a") { th = &c; }
		}
		CHECK(th && th->bold); // th: bold (+ centered via UA text-align)
	}

	// --- submit/reset defaults + script methods
	ctbrowser::node * t = e.doc.by_id("t");
	ctbrowser::node * cb = e.doc.by_id("cb");
	const auto click = [&e](ctbrowser::node * n) {
		const double x = n->x + n->w / 2.0, y = n->y + n->h / 2.0;
		e.mouse_button(x, y, true);
		e.mouse_button(x, y, false);
	};
	click(e.doc.by_id("go")); // <button> with no type submits
	CHECK(e.script["submits"].to<int>() == 1);
	e.script.call("do_submit");
	CHECK(e.script["submits"].to<int>() == 2);
	// dirty the controls, then reset restores initial state
	t->value = "changed";
	click(cb);
	CHECK(!cb->checked);
	click(e.doc.by_id("undo"));
	CHECK(t->value == "orig");
	CHECK(cb->checked);
	t->value = "changed2";
	e.script.call("do_reset");
	CHECK(t->value == "orig");

	// --- inline flow: consecutive inline-level siblings share a row,
	// wrapping labels put the control BEFORE their text, and inline
	// containers shrink-wrap
	{
		ctbrowser::node * go = e.doc.by_id("go");
		ctbrowser::node * undo = e.doc.by_id("undo");
		CHECK(go && undo);
		CHECK(go->y == undo->y || go->y + go->h > undo->y); // same line band
		CHECK(undo->x > go->x + go->w);                     // side by side
		ctbrowser::node * lnk2 = e.doc.by_id("lnk");
		CHECK(lnk2->w < 400); // the <a> shrink-wrapped to its text
	}
	// --- the details disclosure triangle paints (right-pointing: closed)
	{
		auto out2 = e.frame(600);
		(void)out2; // markers checked in forms.cpp where a <details> exists
	}

	if (fails == 0) { std::printf("richtext suite: all checks passed\n"); }
	return fails ? 1 : 0;
}
