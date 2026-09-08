// Margin collapsing: adjacent siblings, an empty block collapsing through,
// parent and child edges, and the four things that stop it - a line box,
// padding or border, overflow, and min-height. Carved out of
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

// Adjacent vertical margins form one margin, rather than two pieces of space.
// Positive margins take the largest positive; negative margins take the most
// negative; a positive and a negative are then added.
void test_adjacent_block_margins_collapse() {
    {
        fixture f;
        f.load("<html><body><main id=flow><div id=a></div><div id=b></div>"
               "</main></body></html>",
               "body { margin: 0; padding: 0 } #flow { padding: 1px 0 } "
               "div { height: 10px } #a { margin-bottom: 12px } #b { margin-top: 20px }");
        engine eng;
        const fragment out = eng.run(f.root, 200);
        const fragment * a = out.find(f.find_id("a"));
        const fragment * b = out.find(f.find_id("b"));
        check(a != nullptr && b != nullptr, "positive-margin siblings produced fragments");
        if (a != nullptr && b != nullptr) {
            const float gap = b->bounds.y - (a->bounds.y + a->bounds.height);
            expect_near(gap, 20, "two positive sibling margins collapse to the larger one");
        }
    }
    {
        fixture f;
        f.load("<html><body><main id=flow><div id=a></div><div id=b></div><div id=c></div>"
               "</main></body></html>",
               "body { margin: 0; padding: 0 } #flow { padding: 1px 0 } "
               "div { height: 10px } #a { margin-bottom: 20px } "
               "#b { margin-top: -5px; margin-bottom: -12px } #c { margin-top: -20px }");
        engine eng;
        const fragment out = eng.run(f.root, 200);
        const fragment * a = out.find(f.find_id("a"));
        const fragment * b = out.find(f.find_id("b"));
        const fragment * c = out.find(f.find_id("c"));
        check(a != nullptr && b != nullptr && c != nullptr,
              "mixed-sign margin siblings produced fragments");
        if (a != nullptr && b != nullptr && c != nullptr) {
            const float mixed_gap = b->bounds.y - (a->bounds.y + a->bounds.height);
            const float negative_gap = c->bounds.y - (b->bounds.y + b->bounds.height);
            expect_near(mixed_gap, 15, "a positive and a negative sibling margin are added");
            expect_near(negative_gap, -20,
                        "two negative sibling margins collapse to the most negative one");
        }
    }
}

void test_an_empty_blocks_margins_collapse_through_it() {
    // All four adjoining margins are ONE group. Keeping only a pairwise scalar
    // loses the +20 when -30 is seen and gives the wrong answer once +15 arrives;
    // the group needs both its largest positive and its most-negative member.
    fixture f;
    f.load("<html><body><main id=flow><div id=a></div><div id=empty></div><div id=b></div>"
           "</main></body></html>",
           "body { margin: 0; padding: 0 } #flow { padding: 1px 0 } "
           "#a, #b { height: 10px } #a { margin-bottom: 10px } "
           "#empty { margin-top: 20px; margin-bottom: -30px } #b { margin-top: 15px }");
    engine eng;
    const fragment out = eng.run(f.root, 200);
    const fragment * a = out.find(f.find_id("a"));
    const fragment * empty = out.find(f.find_id("empty"));
    const fragment * b = out.find(f.find_id("b"));
    check(a != nullptr && empty != nullptr && b != nullptr,
          "the empty block and both neighbours produced fragments");
    if (a != nullptr && empty != nullptr && b != nullptr) {
        const float gap = b->bounds.y - (a->bounds.y + a->bounds.height);
        expect_near(empty->bounds.height, 0, "the empty block has no border-box height");
        expect_near(empty->bounds.y - (a->bounds.y + a->bounds.height), 20,
                    "its top edge uses its top group, not its joined bottom margin");
        expect_near(gap, -10, "margins collapse through an empty block as one group");
    }
}

void test_an_empty_inline_subtree_does_not_create_a_line_box() {
    // CSS ignores a line containing only zero-edge empty inline boxes when it
    // determines block height. The element still gets a zero fragment, but it
    // must not interrupt this four-edge adjoining margin group.
    fixture f;
    f.load("<html><body><main id=flow><div id=a></div><div id=empty><span></span></div>"
           "<div id=b></div></main></body></html>",
           "body { margin: 0; padding: 0 } #flow { padding: 1px 0 } "
           "#a, #b { height: 10px } #empty { margin: 20px 0 30px }");
    engine eng;
    const fragment out = eng.run(f.root, 200);
    const fragment * a = out.find(f.find_id("a"));
    const fragment * empty = out.find(f.find_id("empty"));
    const fragment * b = out.find(f.find_id("b"));
    check(a != nullptr && empty != nullptr && b != nullptr,
          "the empty inline subtree produced every element fragment");
    if (a != nullptr && empty != nullptr && b != nullptr) {
        expect_near(empty->bounds.height, 0, "an empty inline subtree creates no effective line");
        expect_near(empty->bounds.y - (a->bounds.y + a->bounds.height), 20,
                    "the empty block's own top edge keeps its top-margin position");
        expect_near(b->bounds.y - (a->bounds.y + a->bounds.height), 30,
                    "its margins still collapse through as one group");
    }
}

void test_real_line_boxes_stop_empty_margin_collapse_even_at_zero_height() {
    {
        // A BR creates a line box; it is not the same as an empty span merely
        // because both inline fragments have no intrinsic width.
        fixture f;
        f.load("<html><body><main id=flow><div id=a></div><div id=middle><br></div>"
               "<div id=b></div></main></body></html>",
               "body { margin: 0; padding: 0 } #flow { padding: 1px 0 } "
               "#a, #b { height: 10px } #middle { margin: 20px 0 30px; line-height: 10px }");
        engine eng;
        const fragment out = eng.run(f.root, 200);
        const fragment * a = out.find(f.find_id("a"));
        const fragment * middle = out.find(f.find_id("middle"));
        const fragment * b = out.find(f.find_id("b"));
        check(a != nullptr && middle != nullptr && b != nullptr,
              "the BR line-box fixture produced every element");
        if (a != nullptr && middle != nullptr && b != nullptr) {
            expect_near(middle->bounds.height, 10, "a BR creates one line box");
            expect_near(middle->bounds.y - (a->bounds.y + a->bounds.height), 20,
                        "its top margin remains one sibling group");
            expect_near(b->bounds.y - (middle->bounds.y + middle->bounds.height), 30,
                        "and its bottom margin remains the next group");
        }
    }
    {
        // Existence, not geometric height, is the rule. Text with line-height
        // zero still creates a line box and keeps the margins on its two sides
        // from adjoining into max(20, 30).
        fixture f;
        f.load("<html><body><main id=flow><div id=a></div><div id=middle>x</div>"
               "<div id=b></div></main></body></html>",
               "body { margin: 0; padding: 0 } #flow { padding: 1px 0 } "
               "#a, #b { height: 10px } #middle { margin: 20px 0 30px; line-height: 0 }");
        engine eng;
        const fragment out = eng.run(f.root, 200);
        const fragment * a = out.find(f.find_id("a"));
        const fragment * middle = out.find(f.find_id("middle"));
        const fragment * b = out.find(f.find_id("b"));
        check(a != nullptr && middle != nullptr && b != nullptr,
              "the zero-height line-box fixture produced every element");
        if (a != nullptr && middle != nullptr && b != nullptr) {
            expect_near(middle->bounds.height, 0, "line-height zero has zero geometry");
            expect_near(b->bounds.y - (a->bounds.y + a->bounds.height), 50,
                        "but its real line box keeps the two margin groups separate");
            check(!middle->block_margins.through,
                  "a real zero-height line box prevents collapse-through");
        }
    }
}

void test_parent_and_child_edge_margins_collapse() {
    fixture f;
    f.load("<html><body><main id=flow><div id=before></div><section id=parent>"
           "<div id=first></div><div id=last></div></section><div id=after></div>"
           "</main></body></html>",
           "body { margin: 0; padding: 0 } #flow { padding: 1px 0 } "
           "#before, #after, #first, #last { height: 10px } "
           "#parent { margin-top: 10px; margin-bottom: 12px } "
           "#first { margin-top: 20px } #last { margin-bottom: 30px }");
    engine eng;
    const fragment out = eng.run(f.root, 200);
    const fragment * before = out.find(f.find_id("before"));
    const fragment * parent = out.find(f.find_id("parent"));
    const fragment * first = out.find(f.find_id("first"));
    const fragment * last = out.find(f.find_id("last"));
    const fragment * after = out.find(f.find_id("after"));
    check(before != nullptr && parent != nullptr && first != nullptr && last != nullptr &&
              after != nullptr,
          "the parent-edge collapse fixture produced every fragment");
    if (before != nullptr && parent != nullptr && first != nullptr && last != nullptr &&
        after != nullptr) {
        const float before_gap = parent->bounds.y - (before->bounds.y + before->bounds.height);
        const float after_gap = after->bounds.y - (parent->bounds.y + parent->bounds.height);
        expect_near(before_gap, 20, "parent and first-child top margins collapse outside parent");
        expect_near(first->bounds.y, 0, "the first child's border edge is the parent's edge");
        expect_near(last->bounds.y, 10, "ordinary children still stack inside the parent");
        expect_near(parent->bounds.height, 20,
                    "a last-child margin that escapes does not grow the parent");
        expect_near(after_gap, 30, "parent and last-child bottom margins collapse outside parent");
    }
}

void test_padding_and_border_stop_parent_child_margin_collapse() {
    fixture f;
    f.load("<html><body><main id=flow><section id=parent><div id=child></div></section>"
           "<div id=after></div></main></body></html>",
           "body { margin: 0; padding: 0 } #flow { padding: 1px 0 } "
           "#parent { padding: 3px 0 4px; border-top: 2px solid #000; "
           "          border-bottom: 2px solid #000 } "
           "#child { height: 10px; margin-top: 20px; margin-bottom: 30px } "
           "#after { height: 10px }");
    engine eng;
    const fragment out = eng.run(f.root, 200);
    const fragment * parent = out.find(f.find_id("parent"));
    const fragment * child = out.find(f.find_id("child"));
    const fragment * after = out.find(f.find_id("after"));
    check(parent != nullptr && child != nullptr && after != nullptr,
          "the non-collapsing boundary fixture produced every fragment");
    if (parent != nullptr && child != nullptr && after != nullptr) {
        expect_near(child->bounds.y, 25,
                    "top border and padding keep the child's top margin inside");
        expect_near(parent->bounds.height, 71,
                    "both child margins remain inside the bordered, padded parent");
        expect_near(after->bounds.y, parent->bounds.y + parent->bounds.height,
                    "the following block starts after the complete parent border box");
    }
}

void test_overflow_stops_parent_child_margin_collapse() {
    const auto run = [](std::string_view overflow, bool boundary, const char * message) {
        fixture f;
        std::string css = "body { margin: 0; padding: 0 } #flow { padding: 1px 0 } "
                          "#child, #after { height: 10px } "
                          "#child { margin-top: 20px; margin-bottom: 30px } #parent { ";
        css.append(overflow).append(" }");
        f.load("<html><body><main id=flow><section id=parent><div id=child></div></section>"
               "<div id=after></div></main></body></html>",
               css);
        engine eng;
        const fragment out = eng.run(f.root, 200);
        const fragment * parent = out.find(f.find_id("parent"));
        const fragment * child = out.find(f.find_id("child"));
        const fragment * after = out.find(f.find_id("after"));
        check(parent != nullptr && child != nullptr && after != nullptr, message);
        if (parent == nullptr || child == nullptr || after == nullptr) { return; }
        if (boundary) {
            expect_near(child->bounds.y, 20,
                        "scrollable overflow keeps the child's top margin inside");
            expect_near(parent->bounds.height, 60,
                        "scrollable overflow keeps both margins in the parent height");
            expect_near(after->bounds.y, parent->bounds.y + parent->bounds.height,
                        "the following block starts after the overflow boundary");
        } else {
            expect_near(child->bounds.y, 0, "non-scrollable overflow leaves the top edge open");
            expect_near(parent->bounds.height, 10,
                        "non-scrollable overflow lets both edge margins escape");
            expect_near(after->bounds.y - (parent->bounds.y + parent->bounds.height), 30,
                        "the escaped bottom margin collapses with the next sibling");
        }
    };

    // hidden/auto/scroll establish an independent block formatting context
    // even without a border or padding. `clip` deliberately does NOT; authors
    // combine it with `flow-root` when they need both behaviours.
    run("overflow: hidden", true, "overflow:hidden produced every fragment");
    run("overflow-x: auto", true, "one scrollable axis produced every fragment");
    run("overflow: visible clip", false, "visible/clip produced every fragment");
    // These two declarations also prove the shorthand was expanded at cascade
    // time: the later longhands reset both axes and remove the boundary.
    run("overflow: hidden; overflow-x: visible; overflow-y: visible", false,
        "later overflow longhands produced every fragment");
}

void test_min_height_only_blocks_a_margin_that_reaches_the_parent_top() {
    // CSS 2.2 §8.3.1's min-height exception is narrow: it stops an empty
    // child's bottom margin crossing all the way from the parent's top to its
    // bottom. A non-empty child has already separated those two groups, so its
    // bottom margin still escapes an auto-height parent.
    fixture f;
    f.load("<html><body><main id=flow><section id=parent><div id=child></div></section>"
           "<div id=after></div></main></body></html>",
           "body { margin: 0; padding: 0 } #flow { padding: 1px 0 } "
           "#parent { min-height: 1px } #child, #after { height: 10px } "
           "#child { margin-bottom: 20px }");
    engine eng;
    const fragment out = eng.run(f.root, 200);
    const fragment * parent = out.find(f.find_id("parent"));
    const fragment * after = out.find(f.find_id("after"));
    check(parent != nullptr && after != nullptr, "the min-height fixture produced fragments");
    if (parent != nullptr && after != nullptr) {
        expect_near(parent->bounds.height, 10, "content taller than min-height sets the parent");
        expect_near(after->bounds.y - (parent->bounds.y + parent->bounds.height), 20,
                    "a separated last-child margin still escapes past min-height");
    }
}

} // namespace

int main() {
    test_adjacent_block_margins_collapse();
    test_an_empty_blocks_margins_collapse_through_it();
    test_an_empty_inline_subtree_does_not_create_a_line_box();
    test_real_line_boxes_stop_empty_margin_collapse_even_at_zero_height();
    test_parent_and_child_edge_margins_collapse();
    test_padding_and_border_stop_parent_child_margin_collapse();
    test_overflow_stops_parent_child_margin_collapse();
    test_min_height_only_blocks_a_margin_that_reaches_the_parent_top();
    REPORT("layout_margins");
}
