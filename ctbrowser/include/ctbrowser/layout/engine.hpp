#pragma once
#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/layout/algorithm.hpp>
#include <ctbrowser/layout/box.hpp>
#include <ctbrowser/layout/fragment.hpp>
#include <ctbrowser/layout/position.hpp>
#include <ctbrowser/layout/values.hpp>

// Driving a layout pass.
//
// The parallel path is the reason the box tree was separated from the DOM at
// all, so it is worth being precise about WHY it is safe rather than just
// asserting that it is:
//
//   * layout READS the box tree and writes only its own fragments. The box
//     tree is const throughout; the previous engine wrote geometry back onto shared nodes,
//     which is what made concurrency impossible there.
//   * a block child's LOCAL layout depends on exactly one thing from its
//     siblings: the content width, which every sibling shares. Its height and
//     the margin struts it exposes are computed from its own subtree.
//   * so children can be arranged in any order, or at the same time. The final
//     vertical stacking and margin collapse are sequential arithmetic over
//     those already-computed results.
//
// Margin collapsing is the proof of the LOCAL distinction: a child's absolute
// `y` depends on its siblings, but nothing inside the child does. Carrying the
// associative struts on its fragment lets the ordinary assembly resolve that
// dependency without a second layout implementation or lost parallelism.

namespace ctbrowser::layout {

// A deterministic stand-in for a real font stack. Layout has to be testable
// without fonts, and goldens have to be reproducible, so the default measure
// is an exact function of the string rather than anything platform-dependent.
[[nodiscard]] inline measure_text_fn monospace_measure(float advance_ratio = 0.6f) {
    measure_text_fn out;
    out.measure = [advance_ratio](std::string_view text, float font_size, const text_face &) {
        return static_cast<float>(text.size()) * font_size * advance_ratio;
    };
    // A plausible split for a stand-in face; the real numbers come from the
    // font backend through shell::metrics_for.
    out.ascent_of = [](float size, const text_face &) { return size * 0.8f; };
    out.descent_of = [](float size, const text_face &) { return size * 0.2f; };
    return out;
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
    //
    // `viewport_height` is what a `position: fixed` box is placed against, and
    // ONLY that - nothing else in layout has any use for it, because a document
    // is as tall as its content. Zero means "no viewport", and fixed then falls
    // back to the document, which is what a test with no window wants.
    [[nodiscard]] fragment run(const box_node & root, float viewport_width,
                               float viewport_height = 0) const;

    // Below this many boxes, distributing the work costs more than doing it.
    //
    // MEASURED (benchmarks/bench_layout.cpp, 22-core WSL2), parallel vs sequential
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
                                        scheduler & pool, float viewport_height = 0) const {
        if (root.descendant_count() < parallel_min_boxes) {
            return run(root, viewport_width, viewport_height);
        }
        const box_node * split = split_point(&root);
        // An inline container's children share lines, so they are not
        // independent and must not be split. normalise() guarantees a box's
        // children are all inline or all block, so this one test covers it.
        //
        // A FLEX CONTAINER'S CHILDREN ARE NOT INDEPENDENT EITHER, and for a
        // stronger reason: an inline container's children merely share a line,
        // where a flex container's share FREE SPACE, so one item's width is a
        // function of every other item's. split_point already refuses to return
        // one; this is where the invariant is stated, and it is the difference
        // between the two being a fact about the code rather than about one
        // function. Wrong only above parallel_min_boxes, which every real
        // Bootstrap page exceeds and no unit test does unless it sets the
        // threshold to zero on purpose - which the parallel-equals-sequential
        // test does, with a flex case, in the commit that added this.
        if (split == nullptr || split->children.size() < 2 || split->kind == box_kind::flex ||
            split->establishes_inline_context()) {
            return run(root, viewport_width, viewport_height); // nothing worth distributing
        }
        return arrange_parallel(root, constraints{viewport_width, 0, root.font_size}, split, pool,
                                viewport_height);
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
    [[nodiscard]] static const box_node * split_point(const box_node * at);

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
                                            const box_node * split, scheduler & pool,
                                            float viewport_height) const {
        // Widths flow strictly top-down: the constraints at `split` are a
        // function of the path from the root and nothing else - no sibling and
        // nothing below can affect them. That is what makes it legal to compute
        // them before the pass that will use them.
        std::vector<const box_node *> path;
        if (!find_path(root, split, path)) { return run(root, c.available_width, viewport_height); }

        constraints inner = c;
        for (std::size_t i = 0; i + 1 < path.size(); ++i) {
            const resolved_edges edges = resolve_edges(*path[i], inner);
            const float stated_height =
                has_definite_height(*path[i], inner)
                    ? clamp_used_height(
                          *path[i], inner,
                          std::max(0.0f, path[i]->height.resolve(inner.available_height,
                                                                 path[i]->font_size)))
                    : -1.0f;
            const float content_height =
                stated_height >= 0 ? std::max(0.0f, stated_height - edges.vertical_inner()) : 0.0f;
            inner = constraints{content_width_of(*path[i], inner, edges), content_height,
                                path[i + 1]->font_size};
        }
        const resolved_edges split_edges = resolve_edges(*split, inner);
        const float content_width = content_width_of(*split, inner, split_edges);
        const float split_height =
            has_definite_height(*split, inner)
                ? std::max(0.0f, clamp_used_height(*split, inner,
                                                   split->height.resolve(inner.available_height,
                                                                         split->font_size)) -
                                     split_edges.vertical_inner())
                : 0.0f;

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
                pieces[i] = layout_box(
                    child, constraints{content_width, split_height, child.font_size}, measure_);
            }
        });

        precomputed ready{split, pieces};
        fragment out = layout_box(root, c, measure_, &ready);
        // The same pass the sequential path runs, for the same reason and with
        // the same inputs - which is what keeps "parallel equals sequential"
        // true through positioning as well as through flow.
        apply_positioning(out, rect{0, 0, c.available_width, out.bounds.height},
                          viewport_height > 0 ? viewport_height : out.bounds.height, measure_);
        return out;
    }

    // A contiguous run of a box's children, [first, last).
    struct chunk {
        std::size_t first = 0;
        std::size_t last = 0;
    };

    // Split `parent`'s children into at most `want` runs of roughly equal work,
    // measured in boxes rather than in children.
    [[nodiscard]] static std::vector<chunk> plan_chunks(const box_node & parent, std::size_t want);

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
