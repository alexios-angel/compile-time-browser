// ctbrowser.paint: colours, recording, and what a display list is FOR.
//
// The interesting property is not that a fragment tree can be turned into
// drawing commands - it is that the result outlives the frame. v1 returned a
// paint_cmd vector that the shell drew and discarded, so a scroll or a caret
// blink re-ran layout. The tests here pin the properties that make a recorded
// list reusable: it is complete, it is ordered back-to-front, it is
// deterministic, and it can be queried per region so a tile pays only for what
// touches it.

import ctbrowser.core;
import ctbrowser.dom;
import ctbrowser.style;
import ctbrowser.layout;
import ctbrowser.paint;

#include "check.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using namespace ctbrowser::paint;

namespace {

void check(bool ok, std::string_view what) {
	if (!ok) {
		std::printf("FAIL %s\n", std::string{what}.c_str());
		++ctbrowser_test_failures;
	}
}

// The whole front of the pipeline, since a display list is only meaningful at
// the end of it.
struct fixture {
	atom_table atoms;
	document doc{atoms};
	style::engine styles{atoms};
	style::style_map resolved;
	layout::box_node tree;
	layout::fragment placed;

	void load(std::string_view html, std::string_view css, float viewport = 200) {
		(void)parse_html(doc, html);
		styles.add_sheet(css, 1);
		const auto txn = doc.read();
		resolved = styles.resolve_all(txn);
		layout::box_builder builder{atoms, resolved};
		tree = builder.build(txn, txn.root());
		const layout::engine eng{layout::monospace_measure()};
		placed = eng.run(tree, viewport);
	}

	[[nodiscard]] std::shared_ptr<const display_list> record() {
		const recorder rec{atoms};
		return rec.record(placed);
	}
};

[[nodiscard]] std::size_t count_op(const display_list & list, paint_op op) {
	std::size_t n = 0;
	for (const paint_command & c : list.commands()) {
		if (c.op == op) { ++n; }
	}
	return n;
}

[[nodiscard]] const paint_command * first_fill_of(const display_list & list, color want) {
	for (const paint_command & c : list.commands()) {
		if (c.op == paint_op::fill_rect && c.fill == want) { return &c; }
	}
	return nullptr;
}

// --- colours --------------------------------------------------------------

void test_color_syntaxes() {
	const auto is = [](std::string_view text, std::uint32_t argb, std::string_view what) {
		const auto got = parse_color(text);
		check(got.has_value() && got->argb == argb, what);
		if (got && got->argb != argb) {
			std::printf("     %s -> %08X want %08X\n", std::string{text}.c_str(), got->argb, argb);
		}
	};
	is("#f00", 0xFFFF0000, "#rgb shorthand doubles each digit");
	is("#ff0000", 0xFFFF0000, "#rrggbb");
	is("#ff000080", 0x80FF0000, "#rrggbbaa carries alpha");
	is("#f008", 0x88FF0000, "#rgba shorthand");
	is("red", 0xFFFF0000, "a named colour");
	is("  blue  ", 0xFF0000FF, "surrounding space is ignored");
	is("rgb(255, 0, 0)", 0xFFFF0000, "rgb()");
	is("rgba(255, 0, 0, 0.5)", 0x80FF0000, "rgba() with a fractional alpha");
	is("transparent", 0x00000000, "transparent");

	// The failure case matters as much: an unparseable colour must not become
	// black, or every typo in a stylesheet paints a black box.
	check(!parse_color("").has_value(), "empty is not a colour");
	check(!parse_color("notacolour").has_value(), "an unknown name is not a colour");
	check(!parse_color("#12345").has_value(), "a five-digit hex is not a colour");
	check(!parse_color("#gg0000").has_value(), "non-hex digits are not a colour");
}

// --- recording ------------------------------------------------------------

void test_background_is_recorded() {
	fixture f;
	f.load("<html><body><div id=a></div></body></html>",
	       "body { margin: 0; padding: 0 } #a { height: 30px; background-color: #ff0000 }");
	const auto list = f.record();
	const paint_command * fill = first_fill_of(*list, color::rgba(255, 0, 0));
	check(fill != nullptr, "a background-color becomes a fill");
	if (fill == nullptr) { return; }
	check(fill->bounds.width == 200 && fill->bounds.height == 30, "the fill covers the border box");
}

void test_nothing_is_recorded_for_nothing() {
	fixture f;
	f.load("<html><body><div id=a></div></body></html>", "#a { height: 30px }");
	const auto list = f.record();
	// A box with no background and no text draws nothing. Recording an
	// invisible command per element would make every list mostly noise, and
	// tile culling would have to filter it back out.
	check(count_op(*list, paint_op::fill_rect) == 0, "an unstyled box records no fill");
}

void test_text_is_recorded_with_the_inherited_colour() {
	fixture f;
	f.load("<html><body><div id=a>hello</div></body></html>",
	       "body { color: #0000ff } #a { }");
	const auto list = f.record();
	std::size_t runs = 0;
	for (const paint_command & c : list->commands()) {
		if (c.op != paint_op::text_run) { continue; }
		++runs;
		check(c.text == "hello", "the run carries its text");
		check(c.fill == color::rgba(0, 0, 255), "the text colour is inherited from body");
	}
	check(runs == 1, "one line of text records one run");
}

void test_paint_order_is_back_to_front() {
	fixture f;
	f.load("<html><body><div id=outer><div id=inner></div></div></body></html>",
	       "body { margin: 0 } #outer { height: 40px; background-color: red } "
	       "#inner { height: 10px; background-color: blue }");
	const auto list = f.record();
	std::size_t red_at = 0, blue_at = 0, i = 0;
	for (const paint_command & c : list->commands()) {
		if (c.fill == color::rgba(255, 0, 0)) { red_at = i; }
		if (c.fill == color::rgba(0, 0, 255)) { blue_at = i; }
		++i;
	}
	// The parent's background has to be recorded before the child's, or the
	// child disappears under it. v1 needed a separate collect_backgrounds()
	// pre-pass to get this; emitting in tree order into an ordered list is
	// enough.
	check(red_at < blue_at, "a parent's background is recorded before its child's");
}

void test_overflow_hidden_brackets_its_subtree() {
	fixture f;
	f.load("<html><body><div id=a><div id=b></div></div></body></html>",
	       "#a { height: 20px; overflow: hidden } #b { height: 5px; background-color: red }");
	const auto list = f.record();
	check(count_op(*list, paint_op::push_clip) == 1, "overflow:hidden pushes one clip");
	check(count_op(*list, paint_op::pop_clip) == 1, "and pops it");
	// Balance is the property that matters - an unbalanced clip corrupts
	// everything recorded after it, not just the subtree that pushed it.
	int depth = 0, worst = 0;
	for (const paint_command & c : list->commands()) {
		if (c.op == paint_op::push_clip) { ++depth; }
		if (c.op == paint_op::pop_clip) { --depth; }
		worst = depth < worst ? depth : worst;
	}
	check(depth == 0 && worst == 0, "clips are balanced and never pop below zero");
}

void test_recording_is_deterministic() {
	fixture f;
	f.load("<html><body><div id=a>text here</div><p>more</p></body></html>",
	       "#a { background-color: #123456 } p { color: green }");
	const auto first = f.record();
	const auto second = f.record();
	check(first->size() == second->size(), "two recordings produce the same number of commands");
	bool same = first->size() == second->size();
	for (std::size_t i = 0; same && i < first->size(); ++i) {
		same = first->commands()[i] == second->commands()[i];
	}
	check(same, "...and identical commands - a golden image needs this");
}

// --- per-region query, which is what makes tiling worth doing --------------

void test_intersecting_culls_by_region() {
	display_list list;
	list.fill(rect{0, 0, 10, 10}, color::rgba(255, 0, 0));
	list.fill(rect{500, 500, 10, 10}, color::rgba(0, 255, 0));
	check(list.intersecting(rect{0, 0, 100, 100}).size() == 1, "a region sees only what touches it");
	check(list.intersecting(rect{400, 400, 200, 200}).size() == 1, "and the far region sees the other");
	check(list.intersecting(rect{2000, 2000, 10, 10}).empty(), "an empty region sees nothing");
}

void test_intersecting_drops_whole_clipped_groups() {
	display_list list;
	list.push_clip(rect{0, 0, 10, 10});
	list.fill(rect{0, 0, 10, 10}, color::rgba(255, 0, 0));
	list.pop_clip();
	list.fill(rect{500, 500, 10, 10}, color::rgba(0, 255, 0));

	const auto far_away = list.intersecting(rect{400, 400, 200, 200});
	// A clip that misses the region excludes everything inside it, so the whole
	// group goes rather than being clipped away pixel by pixel later.
	check(far_away.size() == 1, "a clipped group whose clip misses the region is dropped whole");
	if (far_away.size() == 1) {
		check(far_away[0].op == paint_op::fill_rect, "and what survives is the unclipped fill");
	}
}

void test_intersecting_keeps_clips_balanced() {
	display_list list;
	list.push_clip(rect{0, 0, 1000, 1000}); // kept
	list.push_clip(rect{900, 900, 10, 10}); // dropped for a near region
	list.fill(rect{900, 900, 10, 10}, color::rgba(255, 0, 0));
	list.pop_clip();
	list.push_clip(rect{0, 0, 5, 5}); // kept
	list.fill(rect{0, 0, 5, 5}, color::rgba(0, 255, 0));
	list.pop_clip();
	list.pop_clip();

	const auto near = list.intersecting(rect{0, 0, 50, 50});
	int depth = 0, worst = 0;
	for (const paint_command & c : near) {
		if (c.op == paint_op::push_clip) { ++depth; }
		if (c.op == paint_op::pop_clip) { --depth; }
		worst = depth < worst ? depth : worst;
	}
	// This is the case that a naive skip-counter gets wrong: a nested clip
	// inside a dropped group has to deepen the skip, or its pop unbalances the
	// list and every later command is drawn under the wrong clip.
	check(depth == 0 && worst == 0, "culling a nested clipped group leaves clips balanced");
}

void test_bounds_is_the_union() {
	display_list list;
	check(list.bounds().empty(), "an empty list has empty bounds");
	list.fill(rect{10, 10, 10, 10}, color::rgba(255, 0, 0));
	list.fill(rect{100, 50, 20, 20}, color::rgba(0, 255, 0));
	const rect b = list.bounds();
	check(b.x == 10 && b.y == 10 && b.right() == 120 && b.bottom() == 70,
	      "bounds is the union of what was recorded");
}

// --- layers ---------------------------------------------------------------

void test_scrolling_moves_layers_not_commands() {
	fixture f;
	f.load("<html><body><div id=a></div></body></html>", "#a { height: 30px; background-color: red }");
	const recorder rec{f.atoms};
	layer_tree tree = rec.record_layers(f.placed);
	check(tree.size() == 1, "one page, one layer for now");
	const auto * before = tree.layers[0].contents.get();

	tree.scroll_to(0, 100);
	check(tree.layers[0].offset.y == -100, "scrolling sets the layer offset");
	// The identity check is the point: a scroll must not touch the recording.
	check(tree.layers[0].contents.get() == before, "and does not re-record anything");
}

void test_a_non_scrolling_layer_stays_put() {
	layer_tree tree;
	tree.layers.push_back(layer{std::make_shared<display_list>(), point{}, rect{}, true});
	tree.layers.push_back(layer{std::make_shared<display_list>(), point{}, rect{}, false});
	tree.scroll_to(0, 50);
	check(tree.layers[0].offset.y == -50, "a scrolling layer moves");
	check(tree.layers[1].offset.y == 0, "position:fixed content is a layer that does not");
}

} // namespace

int main() {
	test_color_syntaxes();

	test_background_is_recorded();
	test_nothing_is_recorded_for_nothing();
	test_text_is_recorded_with_the_inherited_colour();
	test_paint_order_is_back_to_front();
	test_overflow_hidden_brackets_its_subtree();
	test_recording_is_deterministic();

	test_intersecting_culls_by_region();
	test_intersecting_drops_whole_clipped_groups();
	test_intersecting_keeps_clips_balanced();
	test_bounds_is_the_union();

	test_scrolling_moves_layers_not_commands();
	test_a_non_scrolling_layer_stays_put();

	REPORT("paint_basics");
}
