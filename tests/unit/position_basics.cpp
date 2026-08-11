// ctbrowser.layout: positioning - CSS 2.1 §9.3 and §10.
//
// The claim being tested is the one that makes positioning different from every
// other part of layout: THE BOX THAT DECIDES WHERE A CHILD GOES IS NOT ITS
// PARENT. An absolutely positioned box is placed against the nearest positioned
// ANCESTOR, which may be ten levels up and which finished laying out long before
// the child was reached - so the flows leave an empty fragment at the child's
// static position and layout::apply_positioning places it afterwards, when the
// chain is known.
//
// Almost every case here distinguishes two things that look alike and are not:
// `relative` leaves its slot behind and `absolute` does not; an `auto` offset is
// the static position and not zero; a containing block is a PADDING box and not
// a content box; and both offsets on an axis STRETCH a box where one of them
// shrink-wraps it.
//
// Text is measured with monospace_measure: one character is `font_size * 0.6`.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

using namespace ctbrowser;
using namespace ctbrowser::layout;

namespace {

struct fixture {
    atom_table atoms;
    document doc{atoms};
    style::engine styles{atoms};
    style::style_map resolved;
    box_node root;
    fragment out;

    void load(std::string_view html, std::string_view css, float width = 800, float height = 600) {
        (void)parse_html(doc, html);
        styles.add_sheet(css, 1);
        const auto txn = doc.read();
        resolved = styles.resolve_all(txn);
        box_builder builder{atoms, resolved};
        root = builder.build(txn, txn.root());
        const engine eng{monospace_measure()};
        out = eng.run(root, width, height);
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

    // ABSOLUTE coordinates, which is the only frame in which a positioned box's
    // place can be stated: its fragment's own bounds are relative to whichever
    // parent it happens to hang off, and that parent is not its containing block.
    [[nodiscard]] rect at(std::string_view id) {
        const node_id want = find_id(id);
        rect found{};
        const auto walk = [&](auto && self, const fragment & f, float dx, float dy) -> void {
            if (f.source == want) {
                found = rect{dx + f.bounds.x, dy + f.bounds.y, f.bounds.width, f.bounds.height};
            }
            for (const fragment & c : f.children) {
                self(self, c, dx + f.bounds.x, dy + f.bounds.y);
            }
        };
        walk(walk, out, 0, 0);
        return found;
    }
};

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %s\n", std::string{what}.c_str());
        ++ctbrowser_test_failures;
    }
}

void expect_near(float got, float want, std::string_view what) {
    if (std::fabs(got - want) >= 0.01f) {
        std::printf("FAIL %-56s got %.3f want %.3f\n", std::string{what}.c_str(),
                    static_cast<double>(got), static_cast<double>(want));
        ++ctbrowser_test_failures;
    }
}

constexpr std::string_view reset = "body { margin: 0; padding: 0 } ";

// --- relative -------------------------------------------------------------

void test_relative_moves_but_keeps_its_slot() {
    // THE WHOLE DIFFERENCE between relative and absolute, in one document. The
    // moved box is drawn 10 across and 20 down from where it was; the box AFTER
    // it does not notice, because the slot is still occupied.
    fixture f;
    f.load("<html><body><div id=a></div><div id=b></div></body></html>",
           std::string{reset}
               .append("div { height: 40px } "
                       "#a { position: relative; top: 20px; left: 10px }")
               .c_str());
    expect_near(f.at("a").x, 10, "a relative box moves right by `left`");
    expect_near(f.at("a").y, 20, "...and down by `top`");
    expect_near(f.at("b").y, 40, "and its sibling stays where it was");
}

void test_relative_with_bottom_and_right_moves_the_other_way() {
    fixture f;
    f.load("<html><body><div id=a></div></body></html>",
           std::string{reset}
               .append("div { height: 40px } "
                       "#a { position: relative; bottom: 15px; right: 5px }")
               .c_str());
    // `bottom` and `top` are opposites, so a bottom offset moves the box UP.
    expect_near(f.at("a").y, -15, "`bottom` moves it up");
    expect_near(f.at("a").x, -5, "and `right` moves it left");
}

// --- absolute -------------------------------------------------------------

void test_absolute_leaves_no_slot_behind() {
    fixture f;
    f.load("<html><body><div id=a></div><div id=b></div></body></html>",
           std::string{reset}
               .append("div { height: 40px } #a { position: absolute; top: 100px }")
               .c_str());
    expect_near(f.at("a").y, 100, "the absolute box goes where it was told");
    // The sibling moves UP into the space, which a relative box would have kept.
    expect_near(f.at("b").y, 0, "and its sibling takes the space it vacated");
}

void test_absolute_is_placed_against_the_nearest_POSITIONED_ancestor() {
    // Not against its parent, which is the whole point. `#mid` is an ordinary
    // static box between the anchor and the child, and it must make no difference.
    fixture f;
    f.load("<html><body><div id=pad></div><div id=anchor><div id=mid>"
           "<div id=a></div></div></div></body></html>",
           std::string{reset}
               .append("#pad { height: 30px } "
                       "#anchor { position: relative; margin-left: 50px; height: 100px } "
                       "#mid { margin-top: 25px } "
                       "#a { position: absolute; top: 0; left: 0; height: 10px }")
               .c_str());
    expect_near(f.at("a").y, 30, "top: 0 is the ANCHOR's top, not the page's");
    expect_near(f.at("a").x, 50, "and left: 0 is the anchor's left");
}

void test_an_auto_offset_is_the_static_position() {
    // `.position-absolute` with no offsets at all is a real and common
    // declaration - it takes the element out of flow and leaves it exactly where
    // it was. That is what the empty fragment the flows leave behind is for.
    fixture f;
    f.load("<html><body><div id=anchor><div id=pad></div><div id=a></div></div></body></html>",
           std::string{reset}
               .append("#anchor { position: relative; height: 200px } "
                       "#pad { height: 60px } "
                       "#a { position: absolute; height: 10px }")
               .c_str());
    expect_near(f.at("a").y, 60, "with no offsets it stays at its static position");
    expect_near(f.at("a").x, 0, "...on both axes");
}

void test_both_offsets_stretch_and_one_shrink_wraps() {
    const std::string html = "<html><body><div id=anchor><div id=a>hi</div></div></body></html>";
    {
        // Both given and the width auto: the box stretches BETWEEN them, which is
        // how a full-width overlay is written.
        fixture f;
        f.load(html, std::string{reset}
                         .append("#anchor { position: relative; width: 300px; height: 100px } "
                                 "#a { position: absolute; left: 20px; right: 30px; "
                                 "     font-size: 10px }")
                         .c_str());
        expect_near(f.at("a").x, 20, "it starts at `left`");
        expect_near(f.at("a").width, 250, "and stretches to `right`");
    }
    {
        // Only one given: it SHRINKS TO FIT, which is why
        // `.position-absolute.top-0.start-0` is the size of its text and not the
        // size of the page.
        fixture f;
        f.load(html, std::string{reset}
                         .append("#anchor { position: relative; width: 300px; height: 100px } "
                                 "#a { position: absolute; left: 20px; font-size: 10px }")
                         .c_str());
        expect_near(f.at("a").width, 2 * 10 * 0.6f, "with one offset it wraps its content");
    }
}

void test_right_and_bottom_measure_from_the_far_edge() {
    fixture f;
    f.load("<html><body><div id=anchor><div id=a>hi</div></div></body></html>",
           std::string{reset}
               .append("#anchor { position: relative; width: 300px; height: 100px } "
                       "#a { position: absolute; right: 0; bottom: 0; height: 10px; "
                       "     font-size: 10px }")
               .c_str());
    expect_near(f.at("a").x, 300 - 2 * 10 * 0.6f, "right: 0 puts its RIGHT edge at the anchor's");
    expect_near(f.at("a").y, 90, "and bottom: 0 puts its bottom at the anchor's");
}

void test_the_containing_block_is_the_PADDING_box() {
    // Not the content box. Invisible until the anchor has padding, and then wrong
    // by exactly that padding on every descendant.
    fixture f;
    f.load("<html><body><div id=anchor><div id=a></div></div></body></html>",
           std::string{reset}
               .append("#anchor { position: relative; padding: 12px; height: 100px; "
                       "          border: 4px solid #000 } "
                       "#a { position: absolute; top: 0; left: 0; height: 10px }")
               .c_str());
    // The border is outside the padding box and the padding is inside it, so
    // `top: 0` lands past the border and level with the padding - at 4, not 16.
    expect_near(f.at("a").y, 4, "top: 0 is the padding edge, past the border");
    expect_near(f.at("a").x, 4, "...on both axes");
}

void test_with_no_positioned_ancestor_it_uses_the_page() {
    fixture f;
    f.load("<html><body><div id=wrap><div id=a></div></div></body></html>",
           std::string{reset}
               .append("#wrap { margin-left: 40px; margin-top: 40px; height: 100px } "
                       "#a { position: absolute; top: 0; left: 0; height: 10px }")
               .c_str());
    // `#wrap` is static, so it is not an anchor and its margins do not count.
    expect_near(f.at("a").x, 0, "the initial containing block is the page");
    expect_near(f.at("a").y, 0, "...");
}

void test_fixed_is_placed_against_the_WINDOW() {
    // The document here is far taller than the window, which is the case that
    // tells the two apart: against the document a `bottom: 0` bar lands after the
    // last paragraph, where nobody will ever see it.
    fixture f;
    f.load("<html><body><div id=tall></div><div id=bar></div></body></html>",
           std::string{reset}
               .append("#tall { height: 3000px } "
                       "#bar { position: fixed; bottom: 0; left: 0; height: 20px }")
               .c_str(),
           800, 600);
    expect_near(f.at("bar").y, 580, "a fixed bar sits at the bottom of the WINDOW");
}

// --- transform ------------------------------------------------------------

void test_translate_offsets_by_a_share_of_the_box_itself() {
    // `.translate-middle` is `translate(-50%, -50%)` and it is how every centred
    // overlay in Bootstrap is anchored. The percentages are of the box's OWN
    // size, which is what makes it centre ON its anchor point.
    fixture f;
    f.load("<html><body><div id=anchor><div id=a>hi</div></div></body></html>",
           std::string{reset}
               .append("#anchor { position: relative; width: 300px; height: 100px } "
                       "#a { position: absolute; top: 50px; left: 100px; height: 20px; "
                       "     font-size: 10px; transform: translate(-50%, -50%) }")
               .c_str());
    expect_near(f.at("a").x, 100 - 2 * 10 * 0.6f / 2, "half its own width to the left");
    expect_near(f.at("a").y, 40, "and half its own height up");
}

void test_the_one_axis_translate_functions_are_their_own() {
    // `translateX(-50%)` is a different function from `translate(-50%)`, and
    // Bootstrap writes both - `.translate-middle-x` and `.translate-middle`. A
    // parser that reads only the two-argument spelling centres one and silently
    // leaves the other where it was.
    fixture f;
    f.load("<html><body><div id=anchor><div id=a>hi</div><div id=b>hi</div>"
           "</div></body></html>",
           std::string{reset}
               .append("#anchor { position: relative; width: 300px; height: 100px } "
                       "#a, #b { position: absolute; top: 40px; left: 100px; height: 20px; "
                       "         font-size: 10px } "
                       "#a { transform: translateX(-50%) } "
                       "#b { transform: translateY(-50%) }")
               .c_str());
    expect_near(f.at("a").x, 100 - 6, "translateX moves only across");
    expect_near(f.at("a").y, 40, "...and not down");
    expect_near(f.at("b").x, 100, "translateY moves only down");
    expect_near(f.at("b").y, 30, "...and not across");
}

// --- the shorthand --------------------------------------------------------

void test_the_inset_shorthand() {
    fixture f;
    f.load("<html><body><div id=anchor><div id=a></div></div></body></html>",
           std::string{reset}
               .append("#anchor { position: relative; width: 300px; height: 100px } "
                       "#a { position: absolute; inset: 10px 20px 30px 40px }")
               .c_str());
    expect_near(f.at("a").x, 40, "inset's fourth value is `left`");
    expect_near(f.at("a").y, 10, "and its first is `top`");
    expect_near(f.at("a").width, 300 - 40 - 20, "left and right together stretch it");
}

void test_a_positioned_box_is_still_an_anchor_with_no_offsets() {
    // `.position-relative` with nothing else is one of Bootstrap's commonest
    // declarations: it exists ONLY to be a containing block.
    fixture f;
    f.load("<html><body><div id=pad></div><div id=anchor><div id=a></div></div></body></html>",
           std::string{reset}
               .append("#pad { height: 50px } "
                       "#anchor { position: relative; height: 100px } "
                       "#a { position: absolute; top: 0; height: 10px }")
               .c_str());
    expect_near(f.at("anchor").y, 50, "the anchor itself has not moved");
    expect_near(f.at("a").y, 50, "and its absolute child is measured from it");
}

} // namespace

int main() {
    test_relative_moves_but_keeps_its_slot();
    test_relative_with_bottom_and_right_moves_the_other_way();

    test_absolute_leaves_no_slot_behind();
    test_absolute_is_placed_against_the_nearest_POSITIONED_ancestor();
    test_an_auto_offset_is_the_static_position();
    test_both_offsets_stretch_and_one_shrink_wraps();
    test_right_and_bottom_measure_from_the_far_edge();
    test_the_containing_block_is_the_PADDING_box();
    test_with_no_positioned_ancestor_it_uses_the_page();
    test_fixed_is_placed_against_the_WINDOW();

    test_translate_offsets_by_a_share_of_the_box_itself();
    test_the_one_axis_translate_functions_are_their_own();

    test_the_inset_shorthand();
    test_a_positioned_box_is_still_an_anchor_with_no_offsets();

    REPORT("position_basics");
}
