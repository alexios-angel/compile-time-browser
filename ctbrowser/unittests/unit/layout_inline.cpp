// Inline layout: text wrapping, line boxes, inline-block, replaced elements,
// baselines, text-align and line-height. Carved out of unit/layout_basics.cpp
// on 2026-09-08 - layout_blocks.cpp names the family, and layout_fixture.hpp
// is the fixture every file in it shares.

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

// `display: inline-block` - a BLOCK formatting context inside, at an INLINE level
// outside. Both halves matter and each is a different bug when it is missing:
// without the inside it cannot hold block children, and without the outside it
// fills its containing block, which is what made every Bootstrap `.badge` a
// full-width bar and `.btn` on an `<a>` span the whole row.
void test_inline_block_shrinks_to_fit() {
    fixture f;
    f.load("<html><body><div id=a><span id=s>hi</span></div></body></html>",
           "body { margin: 0; padding: 0 } #a { display: inline-block; font-size: 10px } "
           "#s { font-size: 10px }");
    engine eng{monospace_measure()};
    const fragment out = eng.run(f.root, 400);
    const fragment * a = out.find(f.find_id("a"));
    if (a != nullptr) {
        expect_near(a->bounds.width, 2 * 10 * 0.6f, "an inline-block wraps its content");
    }
}

void test_inline_blocks_share_a_line() {
    fixture f;
    f.load("<html><body><div id=a>hi</div><div id=b>yo</div></body></html>",
           "body { margin: 0; padding: 0 } div { display: inline-block; font-size: 10px }");
    engine eng{monospace_measure()};
    const fragment out = eng.run(f.root, 400);
    const fragment * a = out.find(f.find_id("a"));
    const fragment * b = out.find(f.find_id("b"));
    check(a != nullptr && b != nullptr, "both fragments exist");
    if (a == nullptr || b == nullptr) { return; }
    expect_near(a->bounds.y, b->bounds.y, "two inline-blocks sit on the same line");
    check(b->bounds.x > a->bounds.x, "the second is to the RIGHT of the first, not below");
}

void test_an_inline_levels_margins_belong_to_the_line() {
    // Bootstrap separates every label from its field with `margin-bottom: .5rem`
    // on an inline-block, and inline_flow ignored child margins outright - so the
    // gap was 0 and every form control sat hard against its label.
    fixture f;
    f.load("<html><body><span id=a>hi</span><span id=b>yo</span>"
           "<div id=c>next</div></body></html>",
           "body { margin: 0; padding: 0; font-size: 10px; line-height: 10px } "
           "span { display: inline-block; margin: 0 4px 6px 0 }");
    engine eng{monospace_measure()};
    const fragment out = eng.run(f.root, 400);
    const fragment * a_box = out.find(f.find_id("a"));
    const fragment * b = out.find(f.find_id("b"));
    const fragment * c = out.find(f.find_id("c"));
    check(a_box != nullptr && b != nullptr && c != nullptr, "all three fragments exist");
    if (a_box == nullptr || b == nullptr || c == nullptr) { return; }
    expect_near(b->bounds.x, a_box->bounds.width + 4, "a right margin separates two on a line");
    // The margin BELOW extends the line box, so what follows clears it.
    expect_near(c->bounds.y, 16, "a bottom margin pushes the next line down");
}

void test_a_stated_size_on_a_replaced_element_is_the_border_box() {
    // Every other box here treats a stated width as the border box, which is what
    // `box-sizing: border-box` asks for and what Bootstrap sets on `*`. The
    // replaced branch used to add the padding and border AROUND it, making every
    // `.form-control` 26px wider than Chrome's.
    fixture f;
    f.load("<html><body><input id=a><input id=b></body></html>",
           "body { margin: 0; padding: 0 } input { padding: 0 12px; border: 1px solid #000 } "
           "#a { width: 320px }");
    engine eng{monospace_measure()};
    const fragment out = eng.run(f.root, 400);
    const fragment * a_box = out.find(f.find_id("a"));
    const fragment * b = out.find(f.find_id("b"));
    check(a_box != nullptr && b != nullptr, "both fragments exist");
    if (a_box == nullptr || b == nullptr) { return; }
    expect_near(a_box->bounds.width, 320, "a stated width IS the border box");
    // The other half of the same rule: an INTRINSIC width is a content size, so
    // the padding and the border are added around that one.
    check(b->bounds.width > 26, "an intrinsic width grows by the padding and border");
}

void test_an_inline_block_still_stacks_its_own_blocks() {
    // The INSIDE half. An inline BOX would put these on one line; an
    // inline-BLOCK stacks them, which is the whole reason the two exist.
    fixture f;
    f.load("<html><body><div id=a><p id=p>one</p><p id=q>two</p></div></body></html>",
           "body { margin: 0; padding: 0 } #a { display: inline-block } "
           "p { margin: 0; font-size: 10px }");
    engine eng{monospace_measure()};
    const fragment out = eng.run(f.root, 400);
    const fragment * p = out.find(f.find_id("p"));
    const fragment * q = out.find(f.find_id("q"));
    check(p != nullptr && q != nullptr, "both paragraphs exist");
    if (p != nullptr && q != nullptr) {
        check(q->bounds.y > p->bounds.y, "block children of an inline-block still stack");
    }
}

void test_an_inline_block_sits_on_its_last_lines_baseline() {
    // CSS 2.1 §10.8.1, and the padding is the point: a `.badge` is .35em of
    // padding, a line of text and .35em more. Aligning it by its FONT's ascent -
    // which is what every other inline box does - hangs the whole pill above the
    // sentence by exactly its top padding.
    fixture f;
    f.load("<html><body><div id=w>text <span id=s>tag</span></div></body></html>",
           "body { margin: 0; padding: 0 } div { font-size: 10px } "
           "#s { display: inline-block; padding: 6px; font-size: 10px }");
    engine eng{monospace_measure()};
    const fragment out = eng.run(f.root, 400);
    const fragment * s = out.find(f.find_id("s"));
    const fragment * w = out.find(f.find_id("w"));
    check(s != nullptr && w != nullptr, "the inline-block has a fragment");
    if (s == nullptr || w == nullptr) { return; }
    // monospace_measure's ascent is 0.8 of the size, so the plain run's baseline
    // is 8 below its top and the badge's is 6 (its padding) + 8 = 14 below its.
    // Sharing a baseline therefore leaves the TALLER one where it is and drops the
    // text by the difference. Using the badge's own font ascent instead would put
    // both tops at 0, and the badge's text would sit six pixels below the sentence
    // it is part of.
    expect_near(s->bounds.y, 0, "the taller inline-block sets the line's baseline");
    float text_y = -1;
    for (const fragment & c : w->children) {
        if (c.box != nullptr && c.box->kind == box_kind::text) {
            text_y = c.bounds.y;
            break;
        }
    }
    expect_near(text_y, 6, "and the run beside it drops to meet that baseline");
}

void test_text_align() {
    // A line is aligned by SHIFTING everything on it, against the content width
    // the container handed down - not against how wide the line turned out, which
    // is a number that has no leftover in it by construction.
    const std::string_view html = "<html><body><div id=a>hi</div></body></html>";
    const auto run = [&](std::string_view align) {
        fixture f;
        f.load(html, std::string{"body { margin: 0; padding: 0 } #a { font-size: 10px; "
                                 "text-align: "}
                         .append(align)
                         .append(" }")
                         .c_str());
        engine eng{monospace_measure()};
        const fragment out = eng.run(f.root, 200);
        const fragment * a = out.find(f.find_id("a"));
        // The TEXT run inside, not the block - the block still fills its parent.
        return a != nullptr && !a->children.empty() ? a->children[0].bounds.x : -1.0f;
    };
    const float width = 2 * 10 * 0.6f; // "hi"
    expect_near(run("left"), 0, "left leaves the line at the start");
    expect_near(run("start"), 0, "and `start` is the same thing here");
    expect_near(run("center"), (200 - width) / 2, "center splits the leftover");
    expect_near(run("right"), 200 - width, "right puts it all before the line");
    expect_near(run("end"), 200 - width, "and `end` is the same thing here");
    // INHERITED, which is what makes `.text-center` on a container centre
    // everything inside it rather than only its own text.
    {
        fixture f;
        f.load("<html><body><div id=outer><p id=a>hi</p></div></body></html>",
               "body { margin: 0; padding: 0 } #outer { text-align: center } "
               "p { margin: 0; font-size: 10px }");
        engine eng{monospace_measure()};
        const fragment out = eng.run(f.root, 200);
        const fragment * a = out.find(f.find_id("a"));
        if (a != nullptr && !a->children.empty()) {
            expect_near(a->children[0].bounds.x, (200 - width) / 2, "and it inherits");
        }
    }
}

// line-height, which was a hardcoded 1.25 for every box on every page.
void test_line_height() {
    {
        fixture f;
        f.load("<html><body><p id=a>hi</p></body></html>",
               "p { font-size: 20px; line-height: 2; margin: 0 } body { padding: 0; margin: 0 }");
        engine eng;
        const fragment out = eng.run(f.root, 800);
        const fragment * a = out.find(f.find_id("a"));
        check(a != nullptr, "the paragraph produced a fragment");
        if (a != nullptr) {
            expect_near(a->bounds.height, 40, "a unitless factor times font-size");
        }
    }
    {
        // A LENGTH is itself, not a factor - the case a naive implementation gets
        // backwards because parse_length calls a unitless value px.
        fixture f;
        f.load(
            "<html><body><p id=a>hi</p></body></html>",
            "p { font-size: 20px; line-height: 30px; margin: 0 } body { padding: 0; margin: 0 }");
        engine eng;
        const fragment out = eng.run(f.root, 800);
        const fragment * a = out.find(f.find_id("a"));
        if (a != nullptr) { expect_near(a->bounds.height, 30, "a length is absolute"); }
    }
    {
        fixture f;
        f.load(
            "<html><body><p id=a>hi</p></body></html>",
            "p { font-size: 20px; line-height: 150%; margin: 0 } body { padding: 0; margin: 0 }");
        engine eng;
        const fragment out = eng.run(f.root, 800);
        const fragment * a = out.find(f.find_id("a"));
        if (a != nullptr) { expect_near(a->bounds.height, 30, "a percentage is a factor"); }
    }
    {
        // `normal` keeps the 1.25 fallback, which is what stops every existing
        // render moving for a reason no stylesheet asked for.
        fixture f;
        f.load("<html><body><p id=a>hi</p></body></html>",
               "p { font-size: 20px; margin: 0 } body { padding: 0; margin: 0 }");
        engine eng;
        const fragment out = eng.run(f.root, 800);
        const fragment * a = out.find(f.find_id("a"));
        if (a != nullptr) { expect_near(a->bounds.height, 25, "normal is the fallback factor"); }
    }
}

} // namespace

int main() {
    test_line_height();
    test_text_wraps_at_the_content_width();
    test_a_word_longer_than_the_line_still_advances();
    test_a_block_with_only_text_still_honours_its_own_box();
    test_an_inline_box_shrink_wraps();
    test_inline_block_shrinks_to_fit();
    test_inline_blocks_share_a_line();
    test_an_inline_levels_margins_belong_to_the_line();
    test_a_stated_size_on_a_replaced_element_is_the_border_box();
    test_an_inline_block_still_stacks_its_own_blocks();
    test_an_inline_block_sits_on_its_last_lines_baseline();
    test_text_align();
    REPORT("layout_inline");
}
