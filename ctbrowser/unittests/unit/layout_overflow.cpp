// ctbrowser.layout: the scrollable overflow and scrolling area of a finished
// box (CSS Overflow 3 §3.3, CSSOM View §2) - what scrollWidth/scrollHeight
// and a scroll offset's clamp are made of. Each case names the css/cssom-view
// file whose numbers it pins.

#include "layout_fixture.hpp"

namespace {

struct laid_out {
    fixture f;
    fragment out;

    void run(std::string_view html, std::string_view css, float viewport = 800) {
        f.load(html, css);
        const engine eng{monospace_measure()};
        out = eng.run(f.root, viewport);
    }
    [[nodiscard]] const fragment * at(std::string_view id) { return out.find(f.find_id(id)); }
    [[nodiscard]] rect area(std::string_view id) { return scrolling_area_of(*at(id)); }
};

constexpr std::string_view reset = "body { margin: 0; padding: 0 } ";

// scrollWidthHeight-negative-margin-001: a child pulled out by negative
// margins reaches exactly the padding edge, so the area IS the padding box -
// whatever the overflow value.
void test_a_child_ending_at_the_padding_edge_adds_nothing() {
    for (const char * overflow : {"visible", "hidden", "auto", "clip"}) {
        laid_out page;
        page.run(R"(<div id=w><div id=i></div></div>)",
                 std::string{reset} + "#w { width: 90px; border: 10px solid; overflow: " +
                     overflow + " } #i { margin: -10px; height: 100px; width: 100px }");
        const rect a = page.area("w");
        expect_near(a.width, 90,
                    std::string{"scrollWidth is the padding box, overflow: "} + overflow);
        expect_near(a.height, 90, std::string{"scrollHeight, overflow: "} + overflow);
        expect_near(padding_box_of(*page.at("w")).x, 10,
                    "the padding box starts inside the border");
    }
}

// scrollWidthHeight-negative-margin-002: the child's border box past the
// padding edge grows the area; what it spills up and left is unreachable and
// does not.
void test_a_child_past_the_padding_edge_grows_the_area() {
    laid_out page;
    page.run(R"(<div id=w><div id=i></div></div>)",
             std::string{reset} +
                 "#w { width: 80px; height: 80px; padding: 1px 4px 8px 16px; border-style: solid;"
                 " border-width: 1px 50px 40px 4px } #i { margin: -100px; height: 300px; width: "
                 "300px }");
    const rect a = page.area("w");
    expect_near(a.x, 4, "the area starts at the left padding edge");
    expect_near(a.y, 1, "and the top one");
    expect_near(a.width, 216, "scrollWidth: to the child's right border edge (4+16-100+300 - 4)");
    expect_near(a.height, 201, "scrollHeight: to the child's bottom border edge (1+1-100+300 - 1)");
}

// scrollWidthHeight-flex-column-padding-001: a scroll container keeps its
// end padding past its overflowing in-flow content.
void test_a_scroll_container_keeps_its_end_padding() {
    laid_out page;
    page.run(
        R"(<div id=s><div class=i></div><div class=i></div></div>)",
        std::string{reset} +
            "#s { box-sizing: border-box; display: flex; flex-direction: column; overflow-y: auto;"
            " width: 100px; height: 206px; padding: 100px 0 } .i { flex-shrink: 0; height: 38px }");
    expect_near(padding_box_of(*page.at("s")).height, 206, "clientHeight is the padding box");
    expect_near(page.area("s").height, 276, "scrollHeight: 100 + 38 + 38 + 100");
    // ...and a box that does not scroll keeps nothing past its content.
    laid_out plain;
    plain.run(
        R"(<div id=s><div class=i></div><div class=i></div></div>)",
        std::string{reset} +
            "#s { box-sizing: border-box; display: flex; flex-direction: column;"
            " width: 100px; height: 206px; padding: 100px 0 } .i { flex-shrink: 0; height: 38px }");
    expect_near(plain.area("s").height, 206, "overflow: visible adds no end padding");
}

// A descendant that clips its own overflow contributes only its border box;
// an absolutely positioned box belongs to its containing block's area and to
// the viewport's when there is none.
void test_clipping_and_containing_blocks() {
    laid_out page;
    page.run(
        R"(<div id=w><div id=c><div id=big></div></div><div id=rel><div id=abs></div></div>
                <div id=loose></div></div>)",
        std::string{reset} +
            "#w { width: 100px; height: 50px } #c { width: 20px; height: 20px; overflow: hidden }"
            " #big { width: 500px; height: 500px } #rel { position: relative; width: 10px; height: "
            "10px }"
            " #abs { position: absolute; left: 0; top: 0; width: 150px; height: 5px }"
            " #loose { position: absolute; left: 0; top: 400px; width: 5px; height: 5px }");
    const rect a = page.area("w");
    expect_near(a.width, 150,
                "the relative child's absolute box counts, the clipped one's does not");
    expect_near(a.height, 50, "the ICB-anchored box does not count toward the element");
    const rect viewport = viewport_scrolling_area(page.out, 800, 300);
    expect_near(viewport.width, 800, "the viewport area is at least the viewport");
    expect_near(viewport.height, 405, "...and reaches the ICB-anchored box");
}

} // namespace

int main() {
    test_a_child_ending_at_the_padding_edge_adds_nothing();
    test_a_child_past_the_padding_edge_grows_the_area();
    test_a_scroll_container_keeps_its_end_padding();
    test_clipping_and_containing_blocks();
    REPORT("layout_overflow");
}
