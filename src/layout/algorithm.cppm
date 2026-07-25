module;
#include <algorithm>
#include <span>
#include <concepts>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

export module ctbrowser.layout:algorithm;

import ctbrowser.core;
import :box;
import :fragment;
import :values;

// Formatting contexts, one algorithm each.
//
// v1 laid everything out in one 300-line function with a chain of ifs for
// tables, inputs, textareas, selects and flow content. Adding flex to that
// means adding branches to a function that already does five unrelated jobs.
// Here a formatting context is a TYPE that satisfies a concept, so flex and
// grid arrive as new types rather than new conditionals - and the concept is
// what makes "did I implement the required shape" a compile error instead of
// a runtime surprise.

export namespace ctbrowser::layout {

// The contract every formatting context implements.
//
//   measure  how wide the content wants to be, before deciding how wide it
//            gets. Shrink-to-fit and table columns need this.
//   arrange  place the children and produce the fragment.
template <typename A>
concept LayoutAlgorithm =
    requires(const A & a, const box_node & b, const constraints & c, const measure_text_fn & m) {
	    { a.measure(b, c, m) } -> std::same_as<intrinsic_sizes>;
	    { a.arrange(b, c, m) } -> std::same_as<fragment>;
    };

// Resolved box edges, in px.
struct resolved_edges {
	float margin_top = 0, margin_right = 0, margin_bottom = 0, margin_left = 0;
	float pad_top = 0, pad_right = 0, pad_bottom = 0, pad_left = 0;

	[[nodiscard]] float horizontal_margin() const noexcept { return margin_left + margin_right; }
	[[nodiscard]] float horizontal_padding() const noexcept { return pad_left + pad_right; }
	[[nodiscard]] float vertical_padding() const noexcept { return pad_top + pad_bottom; }
};

[[nodiscard]] inline resolved_edges resolve_edges(const box_node & b, const constraints & c) {
	const float basis = c.available_width;
	return resolved_edges{
	    b.margin.top.resolve(basis, b.font_size),  b.margin.right.resolve(basis, b.font_size),
	    b.margin.bottom.resolve(basis, b.font_size), b.margin.left.resolve(basis, b.font_size),
	    b.padding.top.resolve(basis, b.font_size), b.padding.right.resolve(basis, b.font_size),
	    b.padding.bottom.resolve(basis, b.font_size), b.padding.left.resolve(basis, b.font_size)};
}

// How wide a block-level box gets, and how much of that its content sees.
// Factored out because the parallel driver has to compute the SAME width to
// hand its workers, and two copies of this arithmetic would be two things to
// keep in agreement forever.
[[nodiscard]] inline float outer_width_of(const box_node & b, const constraints & c,
                                          const resolved_edges & e) {
	return b.width.is_auto() ? c.available_width - e.horizontal_margin()
	                         : b.width.resolve(c.available_width, b.font_size);
}
[[nodiscard]] inline float content_width_of(const box_node & b, const constraints & c,
                                            const resolved_edges & e) {
	return std::max(0.0f, outer_width_of(b, c, e) - e.horizontal_padding());
}

// Fragments already laid out for ONE box's children.
//
// This is how the parallel driver hands its results back to the ordinary
// sequential pass rather than reimplementing the assembly itself. That matters:
// it means "parallel layout equals sequential layout" is STRUCTURAL - the same
// stacking code runs either way - instead of two implementations that have to
// be kept agreeing by hand.
struct precomputed {
	const box_node * parent = nullptr;
	std::span<fragment> children; // index-parallel to parent->children
};

// Forward declaration: block and inline contexts nest inside each other, so
// each needs to be able to lay the other out.
[[nodiscard]] fragment layout_box(const box_node & b, const constraints & c,
                                  const measure_text_fn & measure, precomputed * ready = nullptr);

// --- inline formatting context -------------------------------------------
// Text and inline boxes on shared lines, wrapping at the content width. Line
// breaking is greedy and breaks at spaces, which is what browsers do for
// ordinary text; the interesting cases it does not handle (hyphenation,
// bidi, shaping across an inline boundary) all belong to a text shaper.
struct inline_flow {
	static constexpr float line_height_factor = 1.25f;

	[[nodiscard]] intrinsic_sizes measure(const box_node & b, const constraints & c,
	                                      const measure_text_fn & measure_text) const {
		intrinsic_sizes out;
		float line = 0;
		for (const box_node & child : b.children) {
			const float w = child.kind == box_kind::text
			                    ? measure_text(child.text, child.font_size)
			                    : measure(child, c, measure_text).max_content;
			line += w;
			out.min_content = std::max(out.min_content, longest_word(child, measure_text));
		}
		out.max_content = line;
		return out;
	}

	[[nodiscard]] fragment arrange(const box_node & b, const constraints & c,
	                               const measure_text_fn & measure_text,
	                               precomputed * ready = nullptr) const {
		fragment out;
		out.box = &b;
		out.source = b.source;
		const float line_height = b.font_size * line_height_factor;

		float pen_x = 0;
		float pen_y = 0;
		for (const box_node & child : b.children) {
			if (child.kind == box_kind::text) {
				place_text(child, c, measure_text, line_height, pen_x, pen_y, out);
				continue;
			}
			// an inline box: measured whole, wrapped to the next line if it
			// does not fit beside what is already there
			const float w = measure(child, c, measure_text).max_content;
			if (pen_x > 0 && pen_x + w > c.available_width) {
				pen_x = 0;
				pen_y += line_height;
			}
			fragment f = layout_box(child, constraints{c.available_width, 0, child.font_size},
			                        measure_text, ready);
			f.bounds.x = pen_x;
			f.bounds.y = pen_y;
			if (f.bounds.width <= 0) { f.bounds.width = w; }
			if (f.bounds.height <= 0) { f.bounds.height = line_height; }
			pen_x += f.bounds.width;
			out.children.push_back(std::move(f));
		}
		out.bounds.width = c.available_width;
		out.bounds.height = pen_y + (out.children.empty() ? 0 : line_height);
		return out;
	}

private:
	// Greedy wrap: take words while they fit, then break. Each visual line
	// becomes its own fragment, which is exactly the case the fragment tree
	// exists to represent.
	static void place_text(const box_node & child, const constraints & c,
	                       const measure_text_fn & measure_text, float line_height, float & pen_x,
	                       float & pen_y, fragment & out) {
		std::string_view rest = child.text;
		while (!rest.empty()) {
			const std::size_t take = words_that_fit(rest, c.available_width - pen_x, child.font_size,
			                                        measure_text);
			if (take == 0) {
				// nothing fits in what is left of this line: start a new one
				if (pen_x == 0) { break; } // ...unless the line is already empty
				pen_x = 0;
				pen_y += line_height;
				continue;
			}
			const std::string_view run = rest.substr(0, take);
			fragment f;
			f.box = &child;
			f.source = child.source;
			f.text = std::string{run};
			f.bounds = rect{pen_x, pen_y, measure_text(run, child.font_size), line_height};
			out.children.push_back(std::move(f));
			pen_x += out.children.back().bounds.width;
			rest.remove_prefix(take);
			while (!rest.empty() && rest.front() == ' ') { rest.remove_prefix(1); }
			if (!rest.empty()) {
				pen_x = 0;
				pen_y += line_height;
			}
		}
	}

	[[nodiscard]] static std::size_t words_that_fit(std::string_view text, float available,
	                                                float font_size,
	                                                const measure_text_fn & measure_text) {
		if (available <= 0) { return 0; }
		std::size_t fits = 0;
		std::size_t at = 0;
		while (at < text.size()) {
			const std::size_t space = text.find(' ', at);
			const std::size_t end = space == std::string_view::npos ? text.size() : space;
			if (measure_text(text.substr(0, end), font_size) > available) { break; }
			fits = end;
			if (space == std::string_view::npos) { break; }
			at = space + 1;
		}
		// A single word longer than the line still has to go somewhere, or
		// layout makes no progress and loops forever.
		if (fits == 0 && available > 0) {
			const std::size_t space = text.find(' ');
			return space == std::string_view::npos ? text.size() : space;
		}
		return fits;
	}

	[[nodiscard]] static float longest_word(const box_node & b, const measure_text_fn & measure_text) {
		if (b.kind != box_kind::text) { return 0; }
		float widest = 0;
		std::size_t at = 0;
		while (at <= b.text.size()) {
			const std::size_t space = b.text.find(' ', at);
			const std::size_t end = space == std::string::npos ? b.text.size() : space;
			widest = std::max(widest, measure_text(std::string_view{b.text}.substr(at, end - at),
			                                       b.font_size));
			if (space == std::string::npos) { break; }
			at = space + 1;
		}
		return widest;
	}
};

// --- block formatting context ---------------------------------------------
// Children stack vertically, each filling the content width unless it says
// otherwise.
//
// NOTE: margins do not collapse here. That is a real omission and it is load
// bearing for the parallel driver - collapsing makes a child's position depend
// on its SIBLINGS' margins, which is exactly the dependency that would stop
// children being laid out independently. Implementing it means the driver
// needs a sequential pass to resolve collapsed margins before the parallel
// one, which is how real engines do it and is the right next step.
struct block_flow {
	[[nodiscard]] intrinsic_sizes measure(const box_node & b, const constraints & c,
	                                      const measure_text_fn & measure_text) const {
		intrinsic_sizes out;
		for (const box_node & child : b.children) {
			const intrinsic_sizes child_sizes =
			    child.establishes_inline_context() || child.kind == box_kind::text
			        ? inline_flow{}.measure(child, c, measure_text)
			        : measure(child, c, measure_text);
			out.min_content = std::max(out.min_content, child_sizes.min_content);
			out.max_content = std::max(out.max_content, child_sizes.max_content);
		}
		return out;
	}

	[[nodiscard]] fragment arrange(const box_node & b, const constraints & c,
	                               const measure_text_fn & measure_text,
	                               precomputed * ready = nullptr) const {
		const resolved_edges edges = resolve_edges(b, c);
		const float outer_width = outer_width_of(b, c, edges);
		const float content_width = content_width_of(b, c, edges);
		const bool use_ready = ready != nullptr && ready->parent == &b &&
		                       ready->children.size() == b.children.size();

		fragment out;
		out.box = &b;
		out.source = b.source;

		float cursor = edges.pad_top;
		for (std::size_t i = 0; i < b.children.size(); ++i) {
			const box_node & child = b.children[i];
			const constraints child_c{content_width, 0, child.font_size};
			const resolved_edges child_edges = resolve_edges(child, child_c);
			fragment f = use_ready ? std::move(ready->children[i])
			                       : layout_box(child, child_c, measure_text, ready);
			f.bounds.x = edges.pad_left + child_edges.margin_left;
			f.bounds.y = cursor + child_edges.margin_top;
			cursor = f.bounds.y + f.bounds.height + child_edges.margin_bottom;
			out.children.push_back(std::move(f));
		}
		cursor += edges.pad_bottom;

		out.bounds.width = outer_width;
		out.bounds.height =
		    !b.height.is_auto() ? b.height.resolve(c.available_height, b.font_size) : cursor;
		return out;
	}
};

static_assert(LayoutAlgorithm<block_flow>);
static_assert(LayoutAlgorithm<inline_flow>);

// Dispatch: pick the formatting context this box establishes.
[[nodiscard]] inline fragment layout_box(const box_node & b, const constraints & c,
                                         const measure_text_fn & measure, precomputed * ready) {
	if (b.kind == box_kind::text) {
		fragment f;
		f.box = &b;
		f.source = b.source;
		f.text = b.text;
		f.bounds = rect{0, 0, measure(b.text, b.font_size), b.font_size * inline_flow::line_height_factor};
		return f;
	}
	if (b.establishes_inline_context()) { return inline_flow{}.arrange(b, c, measure, ready); }
	return block_flow{}.arrange(b, c, measure, ready);
}

} // namespace ctbrowser::layout
