// PARALLEL LAYOUT IS IDENTICAL TO SEQUENTIAL - fragment for fragment, to the
// float. This is the load-bearing claim of the whole stage, and `identical`
// below is what makes it a claim rather than an approximation. Carved out of
// unit/layout_basics.cpp on 2026-09-08 - layout_blocks.cpp names the family,
// and layout_fixture.hpp is the fixture every file in it shares.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "layout_fixture.hpp"
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace ctbrowser;
using namespace ctbrowser::layout;

namespace {

// --- 3. parallel == sequential -------------------------------------------

// Structural equality, to the float. Approximate agreement is not the claim.
bool identical(const fragment & a, const fragment & b, std::string & where) {
    if (a.source != b.source) {
        where = "source";
        return false;
    }
    if (a.box != b.box) {
        where = "box";
        return false;
    }
    if (a.text != b.text) {
        where = "text: '" + a.text + "' vs '" + b.text + "'";
        return false;
    }
    if (!near(a.bounds.x, b.bounds.x)) {
        where = "x";
        return false;
    }
    if (!near(a.bounds.y, b.bounds.y)) {
        where = "y";
        return false;
    }
    if (!near(a.bounds.width, b.bounds.width)) {
        where = "width";
        return false;
    }
    if (!near(a.bounds.height, b.bounds.height)) {
        where = "height";
        return false;
    }
    if (!near(a.block_margins.before.positive, b.block_margins.before.positive) ||
        !near(a.block_margins.before.negative, b.block_margins.before.negative)) {
        where = "before margin strut";
        return false;
    }
    if (!near(a.block_margins.after.positive, b.block_margins.after.positive) ||
        !near(a.block_margins.after.negative, b.block_margins.after.negative)) {
        where = "after margin strut";
        return false;
    }
    if (a.block_margins.through != b.block_margins.through) {
        where = "margin collapse-through state";
        return false;
    }
    if (a.has_line_box != b.has_line_box) {
        where = "line-box state";
        return false;
    }
    if (a.children.size() != b.children.size()) {
        where = "child count";
        return false;
    }
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
            std::printf("FAIL parallel layout diverged on attempt %d at %s\n", attempt,
                        where.c_str());
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

// THE CASE THIS RUNG COULD HAVE SHIPPED BROKEN.
//
// The driver's whole licence to fan out is that a block child's layout depends
// on exactly one thing from its siblings - the content width they share. A FLEX
// container's children are not independent at all: free space is distributed
// across the line, so item i's width is a function of item j's. Handing them to
// different workers would give an answer that depends on the interleaving.
//
// Wrong only above parallel_min_boxes, which every real Bootstrap page exceeds
// and no test does unless it sets the threshold to zero on purpose - which is
// exactly what these do.
void test_parallel_matches_sequential_with_flex() {
    // Sixty-four flex rows under <body>: the split is at body, and each row is
    // laid out WHOLE by one worker. That is the legal split, and it is the one
    // the driver must find.
    std::string html = "<html><body>";
    for (int i = 0; i < 64; ++i) {
        html += "<div class=row><div class=col>alpha beta gamma</div>"
                "<div class=col>one two three</div><div class=col>x</div></div>";
    }
    html += "</body></html>";
    fixture f;
    f.load(html, "body { margin: 0; padding: 0 } "
                 ".row { display: flex; flex-wrap: wrap; margin-left: -12px; margin-right: -12px } "
                 ".col { flex-grow: 1; flex-shrink: 0; flex-basis: 0; "
                 "       padding-left: 12px; padding-right: 12px }");
    engine eng{monospace_measure()};
    eng.parallel_min_boxes = 0;
    scheduler pool;
    const fragment sequential = eng.run(f.root, 960);
    for (int attempt = 0; attempt < 8; ++attempt) {
        const fragment parallel_result = eng.run_parallel(f.root, 960, pool);
        std::string where;
        if (!identical(sequential, parallel_result, where)) {
            std::printf("FAIL parallel flex layout diverged on attempt %d at %s\n", attempt,
                        where.c_str());
            ++ctbrowser_test_failures;
            return;
        }
    }
    const box_node * split = engine::split_point(&f.root);
    check(split != nullptr && split->children.size() == 64,
          "the driver split at the 64 flex rows, not inside one");
    check(split != nullptr && split->kind != box_kind::flex,
          "and the split point is not itself a flex container");
}

void test_parallel_refuses_to_split_inside_a_flex_container() {
    // The other shape: ONE flex container holding everything. The dominant-child
    // descent would walk straight into it, so split_point has to stop and the
    // driver has to fall back to a sequential pass rather than fan out over items
    // that share free space.
    std::string html = "<html><body><div class=row>";
    for (int i = 0; i < 64; ++i) { html += "<div class=col>item " + std::to_string(i) + "</div>"; }
    html += "</div></body></html>";
    fixture f;
    f.load(html, "body { margin: 0; padding: 0 } "
                 ".row { display: flex; flex-wrap: wrap } "
                 ".col { flex-grow: 1; flex-shrink: 1; flex-basis: 0; min-width: 0 }");
    engine eng{monospace_measure()};
    eng.parallel_min_boxes = 0;
    scheduler pool;
    check(engine::split_point(&f.root) == nullptr,
          "split_point refuses to descend into a flex container");
    const fragment sequential = eng.run(f.root, 960);
    const fragment parallel_result = eng.run_parallel(f.root, 960, pool);
    std::string where;
    check(identical(sequential, parallel_result, where),
          "so run_parallel falls back and still matches sequential");
}

void test_parallel_survives_a_document_that_is_all_one_subtree() {
    fixture f;
    // One wrapper holding everything: the split point has to be found BELOW
    // it, not at the root, or the pool gets one item and does nothing.
    std::string html = "<html><body><main>";
    for (int i = 0; i < 32; ++i) {
        html += "<section><p>content " + std::to_string(i) + "</p></section>";
    }
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

void test_parallel_matches_sequential_with_collapsed_margins() {
    // Each worker lays out one section, but the section's exposed margins depend
    // on its first and last child. The body then has to merge those returned
    // margin groups sequentially. Dropping that metadata from `precomputed`
    // leaves the parallel tree with the old additive spacing.
    std::string html = "<html><body>";
    for (int i = 0; i < 64; ++i) {
        html += i % 2 == 0 ? "<section><div></div></section>"
                           : "<section class=negative><div></div></section>";
    }
    html += "</body></html>";
    fixture f;
    f.load(html, "body { margin: 0; padding: 1px 0 } "
                 "section { margin-top: 5px; margin-bottom: 9px } "
                 "section > div { height: 10px; margin-top: 11px; margin-bottom: 13px } "
                 "section.negative { margin-top: -7px } "
                 "section.negative > div { margin-top: -11px }");
    engine eng{monospace_measure()};
    eng.parallel_min_boxes = 0;
    scheduler pool;
    const fragment sequential = eng.run(f.root, 320);
    for (int attempt = 0; attempt < 8; ++attempt) {
        const fragment parallel_result = eng.run_parallel(f.root, 320, pool);
        std::string where;
        if (!identical(sequential, parallel_result, where)) {
            std::printf("FAIL parallel margin collapse diverged on attempt %d at %s\n", attempt,
                        where.c_str());
            ++ctbrowser_test_failures;
            return;
        }
    }
    const box_node * split = engine::split_point(&f.root);
    check(split != nullptr && split->children.size() == 64,
          "the margin-collapse case really split across the 64 siblings");
}

void test_parallel_margin_metadata_uses_the_parents_definite_height() {
    // The parallel driver derives constraints down to its split point before
    // workers run. Dropping the parent's definite height there makes a
    // percentage-height empty child become auto-height and collapse through in
    // parallel only - both its geometry and its exposed margin state diverge.
    std::string html = "<html><body>";
    for (int i = 0; i < 64; ++i) { html += "<div></div>"; }
    html += "</body></html>";
    fixture f;
    f.load(html, "body { height: 640px; max-height: 320px; margin: 0; padding: 1px 0 } "
                 "div { height: 1%; margin: 2px 0 3px }");
    engine eng{monospace_measure()};
    eng.parallel_min_boxes = 0;
    scheduler pool;
    const fragment sequential = eng.run(f.root, 320);
    const fragment parallel_result = eng.run_parallel(f.root, 320, pool);
    std::string where;
    check(identical(sequential, parallel_result, where),
          "parallel percentage heights keep identical margin metadata");
    const box_node * split = engine::split_point(&f.root);
    check(split != nullptr && split->children.size() == 64,
          "the percentage-height case really split at its definite-height parent");
}

} // namespace

int main() {
    test_parallel_matches_sequential();
    test_parallel_falls_back_when_there_is_nothing_to_split();
    test_parallel_matches_sequential_with_flex();
    test_parallel_refuses_to_split_inside_a_flex_container();
    test_parallel_survives_a_document_that_is_all_one_subtree();
    test_parallel_matches_sequential_with_collapsed_margins();
    test_parallel_margin_metadata_uses_the_parents_definite_height();
    REPORT("layout_parallel");
}
