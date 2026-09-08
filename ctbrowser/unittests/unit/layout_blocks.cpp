// Block layout: the box tree is not the DOM tree, and a block's geometry -
// widths, lengths, min/max clamps, auto margins, and what a fragment does not
// carry back to the DOM. Carved out of unit/layout_basics.cpp on 2026-09-08,
// when that file was 1,364 lines; the three other layout_*.cpp files beside
// this one are the rest of it, and layout_fixture.hpp carries the fixture, the
// prose assertions, and that file's own banner - the three claims it made are
// now three files.

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
    f.load("<html><body><div id=m>text <span>inline</span><p id=p>block</p></div></body></html>",
           "");
    const box_node * m = box_for(f.root, f.find_id("m"));
    check(m != nullptr, "mixed container has a box");
    if (m == nullptr) { return; }
    // "text " and <span> are inline, <p> is block. The inline run has to be
    // wrapped, or the two formatting contexts interleave.
    check(count_kind(*m, box_kind::anonymous) == 1,
          "the inline run is wrapped in ONE anonymous box");
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
           "<div id=b>text <span>more</span></div></body></html>",
           "");
    const box_node * a = box_for(f.root, f.find_id("a"));
    const box_node * b = box_for(f.root, f.find_id("b"));
    // Wrapping content that needs no wrapping would add a level of boxes to
    // every ordinary paragraph in every document.
    check(a != nullptr && count_kind(*a, box_kind::anonymous) == 0,
          "all-block content is not wrapped");
    check(b != nullptr && count_kind(*b, box_kind::anonymous) == 0,
          "all-inline content is not wrapped");
}

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

void test_min_and_max_height_clamp_a_block_border_box() {
    {
        // The empty child's two margins join the parent's open top, but the
        // non-zero minimum keeps the parent's own top and bottom apart. The
        // resulting border box still has to take the minimum used height.
        fixture f;
        f.load("<html><body><main id=flow><section id=parent><div id=empty></div></section>"
               "<div id=after></div></main></body></html>",
               "body { margin: 0; padding: 0 } #flow { padding: 1px 0 } "
               "#parent { min-height: 10px } #empty { margin: 20px 0 30px } "
               "#after { height: 1px }");
        engine eng;
        const fragment out = eng.run(f.root, 200);
        const fragment * parent = out.find(f.find_id("parent"));
        const fragment * empty = out.find(f.find_id("empty"));
        const fragment * after = out.find(f.find_id("after"));
        check(parent != nullptr && empty != nullptr && after != nullptr,
              "the empty min-height fixture produced every fragment");
        if (parent != nullptr && empty != nullptr && after != nullptr) {
            expect_near(parent->bounds.height, 10, "min-height sets the used border-box height");
            expect_near(empty->bounds.y, 0, "the empty child's margin group escapes at the top");
            expect_near(after->bounds.y, parent->bounds.y + parent->bounds.height,
                        "the parent's separate bottom edge follows its minimum height");
            check(!parent->block_margins.through,
                  "a non-zero min-height prevents the parent collapsing through");
        }
    }
    {
        // Content may overflow a maximum; the next normal-flow sibling still
        // starts after the clamped border box rather than after that overflow.
        fixture f;
        f.load("<html><body><main id=flow><section id=parent><div></div></section>"
               "<div id=after></div></main></body></html>",
               "body { margin: 0; padding: 0 } #flow { padding: 1px 0 } "
               "#parent { max-height: 15px } #parent > div { height: 30px } "
               "#after { height: 1px }");
        engine eng;
        const fragment out = eng.run(f.root, 200);
        const fragment * parent = out.find(f.find_id("parent"));
        const fragment * after = out.find(f.find_id("after"));
        check(parent != nullptr && after != nullptr,
              "the overflowing max-height fixture produced every fragment");
        if (parent != nullptr && after != nullptr) {
            expect_near(parent->bounds.height, 15, "max-height clamps the used border box");
            expect_near(after->bounds.y, parent->bounds.y + parent->bounds.height,
                        "normal flow advances by the clamped height");
        }
    }
    {
        // A percentage max-height has no percentage basis when the containing
        // block is auto-height. CSS makes that maximum `none`, not zero.
        fixture f;
        f.load("<html><body><div id=parent><div id=child></div></div></body></html>",
               "body { margin: 0; padding: 0 } #child { height: 20px; max-height: 100% }");
        engine eng;
        const fragment out = eng.run(f.root, 200);
        const fragment * child = out.find(f.find_id("child"));
        check(child != nullptr, "the indefinite percentage max-height produced a fragment");
        if (child != nullptr) {
            expect_near(child->bounds.height, 20,
                        "an indefinite percentage max-height behaves as none");
        }
    }
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

void test_a_percentage_height_needs_a_containing_block_to_be_one() {
    // CSS 2.1 §10.5: a percentage height whose containing block's height depends
    // on its own content behaves as `auto`. Resolving it against zero instead
    // COLLAPSES the box - and Bootstrap's `.h-100` on a card is exactly that
    // declaration, so a row of three cards came out zero-high and everything
    // below drew straight through them.
    {
        fixture f;
        f.load("<html><body><div id=outer><div id=a>x</div></div></body></html>",
               "body { margin: 0; padding: 0 } #a { height: 100%; font-size: 10px }");
        engine eng{monospace_measure()};
        const fragment out = eng.run(f.root, 400);
        const fragment * a = out.find(f.find_id("a"));
        if (a != nullptr) {
            check(a->bounds.height > 0, "a percentage of an auto height is auto, not zero");
        }
    }
    {
        // And it IS a percentage when the containing block has a real height.
        fixture f;
        f.load("<html><body><div id=outer><div id=a>x</div></div></body></html>",
               "body { margin: 0; padding: 0 } #outer { height: 200px } "
               "#a { height: 50%; font-size: 10px }");
        engine eng{monospace_measure()};
        const fragment out = eng.run(f.root, 400);
        const fragment * a = out.find(f.find_id("a"));
        if (a != nullptr) { expect_near(a->bounds.height, 100, "and half of 200 is 100"); }
    }
}

// A table's gaps and insets come from CSS, not from two constants. They used to
// be a hardcoded 2px of spacing and a hardcoded 2px of padding, and both were
// visible: Bootstrap's Reboot collapses every table's borders, so a striped
// table drew a sliver of the table's own background between every pair of cells
// - and the cell's own CSS padding was applied INSIDE it as well, so each cell
// was inset twice.
void test_a_tables_spacing_comes_from_css() {
    const auto cell_x = [](std::string_view css) {
        fixture f;
        f.load("<html><body><table><tr><td id=a>xx</td><td id=b>yy</td></tr></table></body></html>",
               std::string{"body { margin: 0; padding: 0 } td { padding: 0; font-size: 10px } "}
                   .append(css)
                   .c_str());
        engine eng{monospace_measure()};
        const fragment out = eng.run(f.root, 400);
        const fragment * a = out.find(f.find_id("a"));
        const fragment * b = out.find(f.find_id("b"));
        return std::pair{a != nullptr ? a->bounds.x : -1.0f, b != nullptr ? b->bounds.x : -1.0f};
    };
    {
        // COLLAPSED: no gap anywhere, so the second cell starts exactly where the
        // first one ends and the first starts at the table's own edge.
        const auto [a, b] = cell_x("table { border-collapse: collapse }");
        expect_near(a, 0, "a collapsed table's first cell is flush with its edge");
        expect_near(b, 2 * 10 * 0.6f, "and the second begins where the first ends");
    }
    {
        // SEPARATE is the browser default and 2px is its default spacing - which
        // is where the old constant came from, and it is right for this case and
        // this case only.
        const auto [a, b] = cell_x("table { border-collapse: separate; border-spacing: 4px }");
        expect_near(a, 4, "a separated table insets its first cell by the spacing");
        expect_near(b, 4 + 2 * 10 * 0.6f + 4, "and puts the spacing between them too");
    }
    {
        // A CELL'S PADDING IS APPLIED ONCE. Twice, and every column is four
        // pixels too wide and every cell's text four pixels in from where it
        // belongs.
        fixture f;
        f.load("<html><body><table><tr><td id=a>xx</td></tr></table></body></html>",
               "body { margin: 0; padding: 0 } table { border-collapse: collapse } "
               "td { padding: 5px; font-size: 10px }");
        engine eng{monospace_measure()};
        const fragment out = eng.run(f.root, 400);
        const fragment * a = out.find(f.find_id("a"));
        if (a != nullptr) {
            expect_near(a->bounds.width, 2 * 10 * 0.6f + 10,
                        "the column holds its text plus ONE padding");
        }
    }
}

void test_a_list_marker_belongs_to_the_display() {
    // Not to the tag. A `<li class="d-flex">` is a flex container and not a list
    // item, which is why Bootstrap's list groups and navs show no bullets - and
    // keying the marker off the tag drew one beside every one of them.
    const auto markers = [](std::string_view css) {
        fixture f;
        f.load("<html><body><ul><li id=a>one</li></ul></body></html>", css);
        const box_node * a = box_for(f.root, f.find_id("a"));
        return a != nullptr && a->list_marker;
    };
    check(markers("body { margin: 0 }"), "a plain <li> draws a marker");
    check(!markers("body { margin: 0 } li { display: flex }"),
          "and any other display takes it away");
    check(!markers("body { margin: 0 } li { display: block }"), "...including block");
    check(!markers("body { margin: 0 } ul { list-style: none }"), "and so does list-style: none");
}

void test_fragments_carry_no_geometry_back_to_the_dom() {
    fixture f;
    f.load("<html><body><div id=a></div></body></html>", "#a { height: 40px }");
    engine eng;
    const fragment first = eng.run(f.root, 300);
    const fragment second = eng.run(f.root, 600);
    // Two passes over the SAME box tree at different widths. If layout wrote
    // anything back - which is exactly what the previous engine did - the second pass would be
    // contaminated by the first.
    const fragment * a1 = first.find(f.find_id("a"));
    const fragment * a2 = second.find(f.find_id("a"));
    check(a1 != nullptr && a2 != nullptr, "both passes produced the fragment");
    if (a1 == nullptr || a2 == nullptr) { return; }
    expect_near(a1->bounds.width, 300, "first pass sees the first viewport");
    expect_near(a2->bounds.width, 600, "second pass is unaffected by the first");
}

// `max-width` and `min-width` were ignored ENTIRELY, and `margin: 0 auto` resolved
// both margins to 0 - so a page could not be centred at all and `.container` filled
// its parent instead of stopping at a breakpoint's width.
void test_min_and_max_width_clamp() {
    {
        fixture f;
        f.load("<html><body><div id=a></div></body></html>",
               "div { height: 10px; max-width: 300px } body { padding: 0; margin: 0 }");
        engine eng;
        const fragment out = eng.run(f.root, 800);
        const fragment * a = out.find(f.find_id("a"));
        check(a != nullptr, "the block produced a fragment");
        if (a != nullptr) { expect_near(a->bounds.width, 300, "max-width clamps a fill"); }
    }
    {
        fixture f;
        f.load(
            "<html><body><div id=a></div></body></html>",
            "div { height: 10px; width: 50px; min-width: 200px } body { padding: 0; margin: 0 }");
        engine eng;
        const fragment out = eng.run(f.root, 800);
        const fragment * a = out.find(f.find_id("a"));
        if (a != nullptr) { expect_near(a->bounds.width, 200, "min-width raises a stated width"); }
    }
    {
        // MIN WINS over max, which is what CSS 2.1 says and what applying max first
        // and min second produces without a special case.
        fixture f;
        f.load("<html><body><div id=a></div></body></html>",
               "div { height: 10px; min-width: 400px; max-width: 100px } "
               "body { padding: 0; margin: 0 }");
        engine eng;
        const fragment out = eng.run(f.root, 800);
        const fragment * a = out.find(f.find_id("a"));
        if (a != nullptr) { expect_near(a->bounds.width, 400, "min beats max"); }
    }
}

void test_auto_margins_centre() {
    {
        fixture f;
        f.load("<html><body><div id=a></div></body></html>",
               "div { height: 10px; width: 200px; margin-left: auto; margin-right: auto } "
               "body { padding: 0; margin: 0 }");
        engine eng;
        const fragment out = eng.run(f.root, 800);
        const fragment * a = out.find(f.find_id("a"));
        check(a != nullptr, "the block produced a fragment");
        if (a != nullptr) { expect_near(a->bounds.x, 300, "both autos centre it"); }
    }
    {
        // ONE auto takes the whole remainder, which is how a box is pushed right.
        fixture f;
        f.load("<html><body><div id=a></div></body></html>",
               "div { height: 10px; width: 200px; margin-left: auto } "
               "body { padding: 0; margin: 0 }");
        engine eng;
        const fragment out = eng.run(f.root, 800);
        const fragment * a = out.find(f.find_id("a"));
        if (a != nullptr) { expect_near(a->bounds.x, 600, "one auto pushes it right"); }
    }
    {
        // AN UNSET MARGIN IS NOT auto, and this is the case that matters most: a
        // `length{}` defaults to unit::auto_, so an unset margin and an explicit
        // `auto` are the same VALUE. Reading the length rather than a flag centred
        // every definite-width box that declared no margins at all - which the
        // event tests caught by clicking where an element used to be.
        fixture f;
        f.load("<html><body><div id=a></div></body></html>",
               "div { height: 10px; width: 200px } body { padding: 0; margin: 0 }");
        engine eng;
        const fragment out = eng.run(f.root, 800);
        const fragment * a = out.find(f.find_id("a"));
        if (a != nullptr) { expect_near(a->bounds.x, 0, "no margins means no centring"); }
    }
    {
        // An AUTO WIDTH absorbs the remainder itself, so the margins stay at zero.
        fixture f;
        f.load("<html><body><div id=a></div></body></html>",
               "div { height: 10px; margin-left: auto; margin-right: auto } "
               "body { padding: 0; margin: 0 }");
        engine eng;
        const fragment out = eng.run(f.root, 800);
        const fragment * a = out.find(f.find_id("a"));
        if (a != nullptr) {
            expect_near(a->bounds.x, 0, "an auto width is not centred");
            expect_near(a->bounds.width, 800, "it fills instead");
        }
    }
}

} // namespace

int main() {
    test_min_and_max_width_clamp();
    test_auto_margins_centre();
    test_display_none_produces_no_box();
    test_whitespace_between_blocks_produces_no_box();
    test_mixed_content_generates_anonymous_boxes();
    test_homogeneous_content_is_not_wrapped();
    test_block_fills_width_and_stacks();
    test_padding_and_margin_resolve();
    test_min_and_max_height_clamp_a_block_border_box();
    test_percent_and_em_lengths();
    test_a_percentage_height_needs_a_containing_block_to_be_one();
    test_a_tables_spacing_comes_from_css();
    test_a_list_marker_belongs_to_the_display();
    test_fragments_carry_no_geometry_back_to_the_dom();
    REPORT("layout_blocks");
}
