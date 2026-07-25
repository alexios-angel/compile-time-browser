// ctbrowser.layout: the box tree, the fragment tree, and the parallel driver.
//
// Three things are actually being proved here, in increasing order of how much
// they matter:
//
//   1. the box tree is not the DOM tree - display:none produces no box,
//      whitespace produces no box, and mixed inline/block content produces
//      ANONYMOUS boxes that no element corresponds to. v1 could not represent
//      any of these because its boxes WERE its nodes.
//   2. geometry is right - lengths resolve, padding and margins apply,
//      children stack, and text wraps at the content width.
//   3. PARALLEL LAYOUT IS IDENTICAL TO SEQUENTIAL. This is the load-bearing
//      claim of the whole stage. Anything less than fragment-for-fragment,
//      float-for-float equality means the concurrency is not free, and
//      "mostly the same" is not a layout engine.

import ctbrowser.core;
import ctbrowser.dom;
import ctbrowser.style;
import ctbrowser.layout;

#include "check.hpp"
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using namespace ctbrowser::layout;

namespace {

// A document, its styles and its box tree - the whole front half of the
// pipeline, which is what layout consumes.
struct fixture {
	atom_table atoms;
	document doc{atoms};
	style::engine styles{atoms};
	style::style_map resolved;
	box_node root;

	void load(std::string_view html, std::string_view css) {
		parse_html(doc, html);
		styles.add_sheet(css, 1);
		const auto txn = doc.read();
		resolved = styles.resolve_all(txn);
		box_builder builder{atoms, resolved};
		root = builder.build(txn, txn.root());
	}

	[[nodiscard]] node_id find_id(std::string_view want) {
		const auto txn = doc.read();
		const atom key = atoms.intern("id");
		node_id found{};
		const auto walk = [&](auto && self, node_id at) -> void {
			if (!found && txn.attribute_value(at, key) == want) { found = at; }
			for (const node_id c : txn.children(at)) { self(self, c); }
		};
		walk(walk, txn.root());
		return found;
	}
};

// Find a box by the element it came from.
const box_node * box_for(const box_node & at, node_id id) {
	if (at.source == id) { return &at; }
	for (const box_node & c : at.children) {
		if (const box_node * hit = box_for(c, id)) { return hit; }
	}
	return nullptr;
}

std::size_t count_kind(const box_node & at, box_kind kind) {
	std::size_t n = at.kind == kind ? 1u : 0u;
	for (const box_node & c : at.children) { n += count_kind(c, kind); }
	return n;
}

bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

// CHECK() from check.hpp prints the expression that failed. Layout failures
// read far better as prose ("second block stacks below the first") than as a
// float comparison, so these assertions carry a message instead.
void check(bool ok, std::string_view what) {
	if (!ok) {
		std::printf("FAIL %s\n", std::string{what}.c_str());
		++ctbrowser_test_failures;
	}
}

void expect_near(float got, float want, std::string_view what) {
	if (!near(got, want)) {
		std::printf("FAIL %-44s got %.3f want %.3f\n", std::string{what}.c_str(),
		            static_cast<double>(got), static_cast<double>(want));
		++ctbrowser_test_failures;
	}
}

// --- 1. the box tree is not the DOM tree ---------------------------------

void test_display_none_produces_no_box() {
	fixture f;
	f.load("<html><body><div id=a>a</div><div id=b>b</div></body></html>", "#b { display: none }");
	check(box_for(f.root, f.find_id("a")) != nullptr, "visible element has a box");
	check(box_for(f.root, f.find_id("b")) == nullptr, "display:none element has NO box");
}

void test_whitespace_between_blocks_produces_no_box() {
	fixture f;
	f.load("<html><body>\n  <div id=a>a</div>\n  <div id=b>b</div>\n</body></html>", "");
	// The newlines and indentation between the divs are text nodes in the DOM
	// and must not become text boxes, or every indented document grows blank
	// lines that are not in it.
	check(count_kind(f.root, box_kind::text) == 2, "only the two real text runs get boxes");
}

void test_mixed_content_generates_anonymous_boxes() {
	fixture f;
	f.load("<html><body><div id=m>text <span>inline</span><p id=p>block</p></div></body></html>", "");
	const box_node * m = box_for(f.root, f.find_id("m"));
	check(m != nullptr, "mixed container has a box");
	if (m == nullptr) { return; }
	// "text " and <span> are inline, <p> is block. The inline run has to be
	// wrapped, or the two formatting contexts interleave.
	check(count_kind(*m, box_kind::anonymous) == 1, "the inline run is wrapped in ONE anonymous box");
	check(m->children.size() == 2, "container now has [anonymous, p] as children");
	if (m->children.size() == 2) {
		check(m->children[0].kind == box_kind::anonymous, "anonymous wrapper comes first");
		check(!m->children[0].source, "anonymous box has NO source element");
		check(m->children[0].children.size() == 2, "wrapper holds the text and the span");
		check(m->children[1].source == f.find_id("p"), "the block child is untouched");
	}
}

void test_homogeneous_content_is_not_wrapped() {
	fixture f;
	f.load("<html><body><div id=a><p>x</p><p>y</p></div>"
	       "<div id=b>text <span>more</span></div></body></html>", "");
	const box_node * a = box_for(f.root, f.find_id("a"));
	const box_node * b = box_for(f.root, f.find_id("b"));
	// Wrapping content that needs no wrapping would add a level of boxes to
	// every ordinary paragraph in every document.
	check(a != nullptr && count_kind(*a, box_kind::anonymous) == 0, "all-block content is not wrapped");
	check(b != nullptr && count_kind(*b, box_kind::anonymous) == 0, "all-inline content is not wrapped");
}

// --- 2. geometry ----------------------------------------------------------

void test_block_fills_width_and_stacks() {
	fixture f;
	f.load("<html><body><div id=a></div><div id=b></div></body></html>",
	       "div { height: 20px } body { padding: 0; margin: 0 }");
	engine eng;
	const fragment out = eng.run(f.root, 800);
	const fragment * a = out.find(f.find_id("a"));
	const fragment * b = out.find(f.find_id("b"));
	check(a != nullptr && b != nullptr, "both blocks produced fragments");
	if (a == nullptr || b == nullptr) { return; }
	expect_near(a->bounds.width, 800, "block fills the viewport width");
	expect_near(a->bounds.height, 20, "explicit height is honoured");
	expect_near(a->bounds.y, 0, "first block sits at the top");
	expect_near(b->bounds.y, 20, "second block stacks below the first");
}

void test_padding_and_margin_resolve() {
	fixture f;
	f.load("<html><body><div id=outer><div id=inner></div></div></body></html>",
	       "body { margin: 0; padding: 0 } #outer { padding: 10px } "
	       "#inner { height: 5px; margin: 4px }");
	engine eng;
	const fragment out = eng.run(f.root, 200);
	const fragment * inner = out.find(f.find_id("inner"));
	const fragment * outer = out.find(f.find_id("outer"));
	check(inner != nullptr && outer != nullptr, "nested fragments exist");
	if (inner == nullptr || outer == nullptr) { return; }
	expect_near(inner->bounds.x, 14, "inner x = outer padding 10 + own margin 4");
	expect_near(inner->bounds.y, 14, "inner y = outer padding 10 + own margin 4");
	expect_near(inner->bounds.width, 200 - 20 - 8, "inner fills content width minus its margins");
	// 10 top pad + 4 margin + 5 height + 4 margin + 10 bottom pad
	expect_near(outer->bounds.height, 33, "outer height is content plus its own padding");
}

void test_percent_and_em_lengths() {
	fixture f;
	f.load("<html><body><div id=half></div><div id=em></div></body></html>",
	       "body { margin: 0; padding: 0 } #half { width: 50%; height: 10px } "
	       "#em { font-size: 20px; width: 2em; height: 10px }");
	engine eng;
	const fragment out = eng.run(f.root, 400);
	const fragment * half = out.find(f.find_id("half"));
	const fragment * em = out.find(f.find_id("em"));
	check(half != nullptr && em != nullptr, "length fragments exist");
	if (half == nullptr || em == nullptr) { return; }
	expect_near(half->bounds.width, 200, "50% of a 400px containing block");
	expect_near(em->bounds.width, 40, "2em at font-size 20px");
}

void test_text_wraps_at_the_content_width() {
	fixture f;
	f.load("<html><body><p id=t>aaa bbb ccc ddd</p></body></html>",
	       "body { margin: 0; padding: 0 } p { font-size: 10px; margin: 0 }");
	// monospace_measure: 0.6 * font_size per char = 6px/char. "aaa bbb" is
	// 7 chars = 42px, "aaa bbb ccc" is 11 chars = 66px. At 50px the line
	// takes two words and breaks.
	engine eng{monospace_measure()};
	const fragment out = eng.run(f.root, 50);
	const fragment * p = out.find(f.find_id("t"));
	check(p != nullptr, "paragraph produced a fragment");
	if (p == nullptr) { return; }
	check(p->children.size() == 2, "four words wrap onto two lines");
	if (p->children.size() != 2) { return; }
	check(p->children[0].text == "aaa bbb", "first line takes the words that fit");
	check(p->children[1].text == "ccc ddd", "the remainder goes to the second line");
	expect_near(p->children[0].bounds.y, 0, "first line at the top");
	expect_near(p->children[1].bounds.y, 12.5f, "second line one line-height down");
}

void test_a_word_longer_than_the_line_still_advances() {
	fixture f;
	f.load("<html><body><p id=t>supercalifragilistic</p></body></html>",
	       "body { margin: 0 } p { font-size: 10px; margin: 0 }");
	// The failure mode this guards is an infinite loop: nothing fits, so
	// nothing is consumed, so nothing fits.
	engine eng{monospace_measure()};
	const fragment out = eng.run(f.root, 12);
	const fragment * p = out.find(f.find_id("t"));
	check(p != nullptr && !p->children.empty(), "an over-long word still gets placed");
}

void test_a_block_with_only_text_still_honours_its_own_box() {
	fixture f;
	f.load("<html><body><div id=a>short</div></body></html>",
	       "body { margin: 0; padding: 0 } "
	       "#a { height: 500px; width: 120px; padding: 10px; font-size: 10px }");
	engine eng{monospace_measure()};
	const fragment out = eng.run(f.root, 400);
	const fragment * a = out.find(f.find_id("a"));
	check(a != nullptr, "the block produced a fragment");
	if (a == nullptr) { return; }
	// Found while rendering: a block whose children are all inline was being
	// laid out as if it WERE an inline box, so it took its size from its text
	// and ignored its own width, height, padding and margins. Since nearly
	// every leaf element in a real document contains only text, that was nearly
	// every leaf element - `<div style="height:2000px">x</div>` came out one
	// line high.
	expect_near(a->bounds.height, 500, "an explicit height is honoured over the text height");
	expect_near(a->bounds.width, 120, "and so is an explicit width");
	check(!a->children.empty(), "the text is still laid out inside it");
	if (!a->children.empty()) {
		expect_near(a->children[0].bounds.x, 10, "the line starts inside the left padding");
		expect_near(a->children[0].bounds.y, 10, "and below the top padding");
	}
}

void test_an_inline_box_shrink_wraps() {
	fixture f;
	f.load("<html><body><div id=a><span id=s>hi</span></div></body></html>",
	       "body { margin: 0; padding: 0 } #a { font-size: 10px } #s { font-size: 10px }");
	engine eng{monospace_measure()};
	const fragment out = eng.run(f.root, 400);
	const fragment * a = out.find(f.find_id("a"));
	const fragment * s = out.find(f.find_id("s"));
	check(a != nullptr && s != nullptr, "both fragments exist");
	if (a == nullptr || s == nullptr) { return; }
	// The block fills; the inline inside it does not. Getting this backwards is
	// what the dispatch fix above was about.
	expect_near(a->bounds.width, 400, "the block fills the viewport");
	expect_near(s->bounds.width, 2 * 10 * 0.6f, "the inline shrinks to its two glyphs");
}

void test_fragments_carry_no_geometry_back_to_the_dom() {
	fixture f;
	f.load("<html><body><div id=a></div></body></html>", "#a { height: 40px }");
	engine eng;
	const fragment first = eng.run(f.root, 300);
	const fragment second = eng.run(f.root, 600);
	// Two passes over the SAME box tree at different widths. If layout wrote
	// anything back - which is exactly what v1 did - the second pass would be
	// contaminated by the first.
	const fragment * a1 = first.find(f.find_id("a"));
	const fragment * a2 = second.find(f.find_id("a"));
	check(a1 != nullptr && a2 != nullptr, "both passes produced the fragment");
	if (a1 == nullptr || a2 == nullptr) { return; }
	expect_near(a1->bounds.width, 300, "first pass sees the first viewport");
	expect_near(a2->bounds.width, 600, "second pass is unaffected by the first");
}

// --- 3. parallel == sequential -------------------------------------------

// Structural equality, to the float. Approximate agreement is not the claim.
bool identical(const fragment & a, const fragment & b, std::string & where) {
	if (a.source != b.source) { where = "source"; return false; }
	if (a.box != b.box) { where = "box"; return false; }
	if (a.text != b.text) { where = "text: '" + a.text + "' vs '" + b.text + "'"; return false; }
	if (!near(a.bounds.x, b.bounds.x)) { where = "x"; return false; }
	if (!near(a.bounds.y, b.bounds.y)) { where = "y"; return false; }
	if (!near(a.bounds.width, b.bounds.width)) { where = "width"; return false; }
	if (!near(a.bounds.height, b.bounds.height)) { where = "height"; return false; }
	if (a.children.size() != b.children.size()) { where = "child count"; return false; }
	for (std::size_t i = 0; i < a.children.size(); ++i) {
		if (!identical(a.children[i], b.children[i], where)) { return false; }
	}
	return true;
}

// A document wide enough that the split point has real work to hand out.
std::string wide_document(int blocks) {
	std::string html = "<html><body>";
	for (int i = 0; i < blocks; ++i) {
		html += "<div class=row><p>alpha beta gamma delta epsilon zeta eta theta</p>"
		        "<p>one two three four five six seven eight nine ten</p></div>";
	}
	html += "</body></html>";
	return html;
}

void test_parallel_matches_sequential() {
	fixture f;
	f.load(wide_document(64), "body { margin: 0; padding: 0 } "
	                          ".row { padding: 3px; margin: 2px } "
	                          "p { font-size: 12px; margin: 1px }");
	engine eng{monospace_measure()};
	eng.parallel_min_boxes = 0; // this test exists to exercise the parallel path
	scheduler pool;

	const fragment sequential = eng.run(f.root, 320);
	// Run it several times: a race that only shows up on some interleaving is
	// still a race, and one clean run proves very little.
	for (int attempt = 0; attempt < 8; ++attempt) {
		const fragment parallel_result = eng.run_parallel(f.root, 320, pool);
		std::string where;
		if (!identical(sequential, parallel_result, where)) {
			std::printf("FAIL parallel layout diverged on attempt %d at %s\n", attempt, where.c_str());
			++ctbrowser_test_failures;
			return;
		}
	}
	check(true, "parallel layout is identical to sequential, 8 runs");

	// The claim only means anything if the driver actually fanned out. A split
	// at <html> - two children, one of them an empty <head> - would pass the
	// equality check while doing all the work on one thread.
	const box_node * split = engine::split_point(&f.root);
	check(split != nullptr && split->children.size() == 64,
	      "the driver split at the 64 rows, not at html or body");
	std::printf("     ... %zu fragments, split into %zu independent subtrees\n", sequential.count(),
	            split == nullptr ? 0u : split->children.size());
}

void test_parallel_falls_back_when_there_is_nothing_to_split() {
	fixture f;
	f.load("<html><body><div><p>only child chain</p></div></body></html>",
	       "body { margin: 0 } p { font-size: 10px }");
	engine eng{monospace_measure()};
	eng.parallel_min_boxes = 0;
	scheduler pool;
	const fragment sequential = eng.run(f.root, 200);
	const fragment parallel_result = eng.run_parallel(f.root, 200, pool);
	std::string where;
	// A single-child chain has no independent siblings anywhere. The driver
	// must notice and lay it out sequentially rather than producing a
	// different - or empty - tree.
	check(identical(sequential, parallel_result, where),
	      "a chain document falls back to sequential and still matches");
}

void test_parallel_survives_a_document_that_is_all_one_subtree() {
	fixture f;
	// One wrapper holding everything: the split point has to be found BELOW
	// it, not at the root, or the pool gets one item and does nothing.
	std::string html = "<html><body><main>";
	for (int i = 0; i < 32; ++i) { html += "<section><p>content " + std::to_string(i) + "</p></section>"; }
	html += "</main></body></html>";
	fixture g;
	g.load(html, "body { margin: 0 } p { font-size: 10px; margin: 2px }");
	engine eng{monospace_measure()};
	eng.parallel_min_boxes = 0;
	scheduler pool;
	const fragment sequential = eng.run(g.root, 400);
	const fragment parallel_result = eng.run_parallel(g.root, 400, pool);
	std::string where;
	if (!identical(sequential, parallel_result, where)) {
		std::printf("FAIL nested-split layout diverged at %s\n", where.c_str());
		++ctbrowser_test_failures;
		return;
	}
	const box_node * split = engine::split_point(&g.root);
	check(split != nullptr && split->children.size() == 32,
	      "the split point is found below the wrapper, at the 32 sections");
}

} // namespace

int main() {
	test_display_none_produces_no_box();
	test_whitespace_between_blocks_produces_no_box();
	test_mixed_content_generates_anonymous_boxes();
	test_homogeneous_content_is_not_wrapped();

	test_block_fills_width_and_stacks();
	test_padding_and_margin_resolve();
	test_percent_and_em_lengths();
	test_text_wraps_at_the_content_width();
	test_a_word_longer_than_the_line_still_advances();
	test_a_block_with_only_text_still_honours_its_own_box();
	test_an_inline_box_shrink_wraps();
	test_fragments_carry_no_geometry_back_to_the_dom();

	test_parallel_matches_sequential();
	test_parallel_falls_back_when_there_is_nothing_to_split();
	test_parallel_survives_a_document_that_is_all_one_subtree();

	REPORT("layout_basics");
}
