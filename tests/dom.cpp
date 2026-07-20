// The runtime DOM is CONSTEXPR. The tree is std::string/std::vector/
// std::unique_ptr, so a page can be parsed (cthtml's value parser),
// instantiated into the ctbrowser node tree, mutated and queried
// entirely at COMPILE TIME - proven by the static_assert below. At
// RUNTIME the same instantiate_html() path builds the exact tree the
// compile-time TYPE path (instantiate<Page::typed_dom<>>) builds.
#include <ctbrowser.hpp>
#include <cstdio>
#include <string_view>

#define PAGE_HTML R"(<!DOCTYPE html>
<title>dom test</title>
<div id=app class="wrap main">
	<h1>Hello</h1>
	<ul id=list><li>a<li>b<li>c</ul>
	<canvas id=c width=200 height=120></canvas>
</div>)"

constexpr std::string_view kHtml = PAGE_HTML;

// ===================== compile-time: build + query =====================

constexpr bool dom_compile_time() {
	ctbrowser::document d = ctbrowser::instantiate_html(kHtml);
	if (!d.root || d.root->tag != "html") { return false; }

	ctbrowser::node * app = d.by_id("app");
	if (app == nullptr || app->tag != "div") { return false; }
	if (!app->has_class("wrap") || !app->has_class("main")) { return false; }

	ctbrowser::node * list = d.by_id("list");
	if (list == nullptr) { return false; }
	int lis = 0;
	for (const auto & c : list->children) {
		if (c->tag == "li") { ++lis; }
	}
	if (lis != 3) { return false; }

	ctbrowser::node * h1 = d.root->find_first("h1");
	if (h1 == nullptr || h1->text != "Hello") { return false; }

	ctbrowser::node * canvas = d.by_id("c");
	if (canvas == nullptr || !canvas->is_canvas()) { return false; }
	if (canvas->canvas_w != 200 || canvas->canvas_h != 120) { return false; }
	if (canvas->pixels.size() != static_cast<size_t>(200) * 120) { return false; }

	// chain is root-first: html > body > div(app)
	if (app->chain().size() != 3) { return false; }

	// mutate at compile time (classes + inline style)
	h1->add_class("big");
	if (!h1->has_class("big")) { return false; }
	h1->inline_style.set("color", "red");
	if (h1->inline_style.get("color") != std::string_view{"red"}) { return false; }
	h1->remove_class("big");
	if (h1->has_class("big")) { return false; }

	return true;
}
static_assert(dom_compile_time(),
              "ctbrowser DOM must parse, instantiate, mutate and query at compile time");

// ===================== runtime: value path == type path ================

static int failures = 0;
#define CHECK(cond)                                                          \
	do {                                                                     \
		if (!(cond)) {                                                       \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
			++failures;                                                      \
		}                                                                    \
	} while (0)

using Page = ctbrowser::page<PAGE_HTML>;

// ===================== compile-time: layout folds too ==================
// The style resolver is a constexpr callable (ctjs::cfunction) over the
// page's compile-time stylesheet, and ctcss::query is constexpr - so a
// whole layout pass runs during constant evaluation.

constexpr bool layout_compile_time() {
	ctbrowser::document d = ctbrowser::instantiate<Page::typed_dom<>>();
	ctbrowser::style_fn resolve =
	    [](const ctcss::element_ref * c, size_t n, std::string_view p) {
		    return ctcss::query(Page::sheet_type{}, c, n, p);
	    };
	std::vector<ctbrowser::paint_cmd> cmds = ctbrowser::layout(d, 800, resolve);
	bool saw_hello = false, saw_canvas = false;
	for (const ctbrowser::paint_cmd & cmd : cmds) {
		if (cmd.what == ctbrowser::paint_cmd::kind::text && cmd.text == "Hello") { saw_hello = true; }
		if (cmd.what == ctbrowser::paint_cmd::kind::canvas) { saw_canvas = true; }
	}
	return !cmds.empty() && saw_hello && saw_canvas;
}
static_assert(layout_compile_time(), "ctbrowser layout must fold at compile time");

static bool same_tree(const ctbrowser::node * a, const ctbrowser::node * b) {
	if ((a == nullptr) != (b == nullptr)) { return false; }
	if (a == nullptr) { return true; }
	if (a->tag != b->tag || a->id != b->id || a->classes != b->classes ||
	    a->text != b->text || a->attributes != b->attributes ||
	    a->canvas_w != b->canvas_w || a->canvas_h != b->canvas_h ||
	    a->children.size() != b->children.size()) {
		return false;
	}
	for (size_t i = 0; i < a->children.size(); ++i) {
		if (!same_tree(a->children[i].get(), b->children[i].get())) { return false; }
	}
	return true;
}

int main() {
	// the constexpr paths also run fine at runtime
	CHECK(dom_compile_time());
	CHECK(layout_compile_time());

	// value path (runtime string) reproduces the compile-time TYPE path
	ctbrowser::document typed = ctbrowser::instantiate<Page::typed_dom<>>();
	ctbrowser::document value = ctbrowser::instantiate_html(kHtml);
	CHECK(same_tree(typed.root.get(), value.root.get()));

	// and it works on a genuinely runtime-built string
	std::string dynamic = "<ul id=";
	dynamic += "menu><li>x<li>y</ul>";
	ctbrowser::document d = ctbrowser::instantiate_html(dynamic);
	ctbrowser::node * menu = d.by_id("menu");
	CHECK(menu != nullptr);
	CHECK(menu->children.size() == 2);

	if (failures == 0) { std::printf("dom suite: all checks passed\n"); }
	return failures;
}
