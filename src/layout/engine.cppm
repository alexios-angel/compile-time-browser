module;
#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

export module ctbrowser.layout:engine;

import ctbrowser.core;
import :algorithm;
import :box;
import :fragment;
import :values;

// Driving a layout pass.
//
// The parallel path is the reason the box tree was separated from the DOM at
// all, so it is worth being precise about WHY it is safe rather than just
// asserting that it is:
//
//   * layout READS the box tree and writes only its own fragments. The box
//     tree is const throughout; v1 wrote geometry back onto shared nodes,
//     which is what made concurrency impossible there.
//   * a block child's layout depends on exactly one thing from its siblings:
//     the content width, which every sibling shares. Its HEIGHT is computed
//     from its own content.
//   * so children can be arranged in any order, or at the same time, and only
//     the final vertical stacking has to be sequential - and that is pure
//     arithmetic over already-computed heights.
//
// That second point is only true because margins do not collapse yet (see
// block_flow). Collapsing makes a child's offset depend on its siblings'
// margins, and the honest fix is a sequential margin-resolution pass feeding
// the parallel arrange - not quietly dropping the parallelism.

export namespace ctbrowser::layout {

// A deterministic stand-in for a real font stack. Layout has to be testable
// without fonts, and goldens have to be reproducible, so the default measure
// is an exact function of the string rather than anything platform-dependent.
[[nodiscard]] inline measure_text_fn monospace_measure(float advance_ratio = 0.6f) {
	return [advance_ratio](std::string_view text, float font_size, const text_face &) {
		return static_cast<float>(text.size()) * font_size * advance_ratio;
	};
}

// A measure that asks a raster font backend. The two structs are deliberately
// separate types - :values depends on nothing, and layout importing paint would
// invert the dependency the pipeline is built on - so somebody has to convert,
// and doing it here means every caller does not.
//
// Declared in the SHELL rather than here for the same reason: this partition
// must not import raster either. See browser::measure_with_fonts.

class engine {
public:
	explicit engine(measure_text_fn measure = monospace_measure()) : measure_(std::move(measure)) {}

	// One layout pass, single-threaded.
	[[nodiscard]] fragment run(const box_node & root, float viewport_width) const {
		return layout_box(root, constraints{viewport_width, 0, root.font_size}, measure_);
	}

	// Below this many boxes, distributing the work costs more than doing it.
	//
	// MEASURED (tests-v2/bench_layout.cpp, 22-core WSL2), parallel vs sequential
	// by box count: 0.08x at 60 boxes, 0.26x at 244, 0.70x at 2164, 1.15x at
	// 4764, 1.72x at 10084, 1.84x at 37204. The crossover is between 2164 and
	// 4764, which is where this is set.
	//
	// It is a member rather than a constant for two reasons: the number is
	// machine-specific, and the layout test sets it to zero so it exercises the
	// real parallel path instead of silently testing the sequential one twice.
	// The principled version of this is cost-aware scheduling rather than a
	// threshold, and it needs a real page workload to tune against.
	std::size_t parallel_min_boxes = 4096;

	// The same pass, with one level's block children arranged across the pool.
	[[nodiscard]] fragment run_parallel(const box_node & root, float viewport_width,
	                                    scheduler & pool) const {
		if (root.descendant_count() < parallel_min_boxes) { return run(root, viewport_width); }
		const box_node * split = split_point(&root);
		// An inline container's children share lines, so they are not
		// independent and must not be split. normalise() guarantees a box's
		// children are all inline or all block, so this one test covers it.
		if (split == nullptr || split->children.size() < 2 || split->establishes_inline_context()) {
			return run(root, viewport_width); // nothing worth distributing
		}
		return arrange_parallel(root, constraints{viewport_width, 0, root.font_size}, split, pool);
	}

	// Where run_parallel will fan out. Public because the granularity decision
	// is the interesting part of the driver and deserves to be testable rather
	// than inferred from a timing.
	//
	// Splitting at the root is wrong for essentially every document: the root
	// has one child (<html>), which has two (<head>, <body>), of which one holds
	// the entire page. Handing that to a pool gives one worker everything and
	// the rest an empty box. So the rule is to keep descending into the largest
	// child while it dominates - and stop at the first level where the work is
	// actually spread out.
	//
	// Cost: one subtree walk per level of the dominant spine, and only at levels
	// that branch. Chains take the cheap path and count nothing.
	[[nodiscard]] static const box_node * split_point(const box_node * at) {
		constexpr std::size_t dominant_percent = 80;
		while (at != nullptr && !at->children.empty()) {
			if (at->children.size() == 1) {
				at = &at->children.front();
				continue;
			}
			std::size_t total = 1;
			const box_node * biggest = nullptr;
			std::size_t biggest_n = 0;
			for (const box_node & c : at->children) {
				const std::size_t n = c.descendant_count();
				total += n;
				if (n > biggest_n) {
					biggest_n = n;
					biggest = &c;
				}
			}
			// One child holding nearly everything means this level is a chain
			// wearing a disguise. Descend. This terminates because biggest_n is
			// strictly less than total, so the subtree shrinks every step.
			if (biggest != nullptr && biggest_n * 100 >= total * dominant_percent) {
				at = biggest;
				continue;
			}
			return at;
		}
		return at;
	}

	[[nodiscard]] const measure_text_fn & measure() const noexcept { return measure_; }

private:

	// Lay `split`'s children out concurrently, then let the ORDINARY sequential
	// pass assemble the document around them.
	//
	// The assembly is deliberately not reimplemented here. An earlier version
	// rebuilt the ancestor chain by hand and had its own copy of the width and
	// stacking arithmetic; that is two implementations of layout that have to
	// stay bit-identical forever, which they will not. Handing the finished
	// children to block_flow instead means there is only ever one.
	[[nodiscard]] fragment arrange_parallel(const box_node & root, const constraints & c,
	                                        const box_node * split, scheduler & pool) const {
		// Widths flow strictly top-down: the constraints at `split` are a
		// function of the path from the root and nothing else - no sibling and
		// nothing below can affect them. That is what makes it legal to compute
		// them before the pass that will use them.
		std::vector<const box_node *> path;
		if (!find_path(root, split, path)) { return run(root, c.available_width); }

		constraints inner = c;
		for (std::size_t i = 0; i + 1 < path.size(); ++i) {
			const resolved_edges edges = resolve_edges(*path[i], inner);
			inner = constraints{content_width_of(*path[i], inner, edges), 0, path[i + 1]->font_size};
		}
		const float content_width =
		    content_width_of(*split, inner, resolve_edges(*split, inner));

		// THE PARALLEL PART. Each chunk of children is laid out into its own
		// slots against the same content width; the box tree is const and
		// nothing else is shared, so there is no synchronisation here at all.
		//
		// The chunking is not incidental. One task per child reads naturally and
		// measured 2.8x SLOWER than sequential on a 2000-box document: a task
		// costs a lock and a condition-variable wake, and a small subtree is
		// less work than that. Chunking by DESCENDANT COUNT rather than by child
		// count is what also stops one enormous child serialising the tail.
		const std::vector<chunk> chunks = plan_chunks(*split, pool.worker_count() + 1);
		std::vector<fragment> pieces(split->children.size());
		pool.parallel_for(chunks.size(), [&](std::size_t k) {
			for (std::size_t i = chunks[k].first; i < chunks[k].last; ++i) {
				const box_node & child = split->children[i];
				pieces[i] = layout_box(child, constraints{content_width, 0, child.font_size}, measure_);
			}
		});

		precomputed ready{split, pieces};
		return layout_box(root, c, measure_, &ready);
	}

	// A contiguous run of a box's children, [first, last).
	struct chunk {
		std::size_t first = 0;
		std::size_t last = 0;
	};

	// Split `parent`'s children into at most `want` runs of roughly equal work,
	// measured in boxes rather than in children.
	[[nodiscard]] static std::vector<chunk> plan_chunks(const box_node & parent, std::size_t want) {
		std::vector<std::size_t> sizes(parent.children.size());
		std::size_t total = 0;
		for (std::size_t i = 0; i < parent.children.size(); ++i) {
			sizes[i] = parent.children[i].descendant_count();
			total += sizes[i];
		}
		std::vector<chunk> out;
		if (want == 0) { want = 1; }
		const std::size_t target = std::max<std::size_t>(1, total / want);
		std::size_t first = 0;
		std::size_t running = 0;
		for (std::size_t i = 0; i < sizes.size(); ++i) {
			running += sizes[i];
			// Close the chunk once it has its share - unless this is the last
			// chunk we are allowed, in which case it takes the remainder.
			if (running >= target && out.size() + 1 < want) {
				out.push_back(chunk{first, i + 1});
				first = i + 1;
				running = 0;
			}
		}
		if (first < sizes.size()) { out.push_back(chunk{first, sizes.size()}); }
		return out;
	}

	// The chain of boxes from `root` down to `want`, inclusive.
	[[nodiscard]] static bool find_path(const box_node & at, const box_node * want,
	                                    std::vector<const box_node *> & path) {
		path.push_back(&at);
		if (&at == want) { return true; }
		for (const box_node & c : at.children) {
			if (find_path(c, want, path)) { return true; }
		}
		path.pop_back();
		return false;
	}

	measure_text_fn measure_;
};

} // namespace ctbrowser::layout
