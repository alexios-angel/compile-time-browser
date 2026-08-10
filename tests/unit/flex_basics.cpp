// ctbrowser.layout: the flex formatting context, CSS Flexible Box Layout 1 §9.
//
// The point of this file is the arithmetic, not the plumbing. A flex item's size
// is a function of its SIBLINGS - free space is shared out, one item's min-width
// violation is redistributed among the others, and that redistribution can
// create a new violation - so almost every case here is one that a single
// proportional pass gets subtly and silently wrong. §9.7 is a fixed-point
// iteration; these are the fixed points.
//
// Text is measured with monospace_measure, so every width in here is an exact
// number rather than a font's opinion: one character is `font_size * 0.6`, which
// is 9.6px at the default 16.
//
// THE TRACED CASE, and the reason this rung exists at all, is
// test_the_bootstrap_grid_traced: a 960px `.container` with 12px padding gives
// 936 of content, `.row`'s -12px side margins widen it back to 960, and three
// `.col`s with `flex: 1 0 0` take 320 each. Getting the sign of those margins
// wrong is invisible until the gutters double, so it is asserted rather than
// inferred.

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

    void load(std::string_view html, std::string_view css) {
        (void)parse_html(doc, html);
        styles.add_sheet(css, 1);
        const auto txn = doc.read();
        resolved = styles.resolve_all(txn);
        box_builder builder{atoms, resolved};
        root = builder.build(txn, txn.root());
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
};

const box_node * box_for(const box_node & at, node_id id) {
    if (at.source == id) { return &at; }
    for (const box_node & c : at.children) {
        if (const box_node * hit = box_for(c, id)) { return hit; }
    }
    return nullptr;
}

std::size_t count_kind(const box_node & at, box_kind kind) {
    std::size_t n = at.kind == kind ? 1u : 0u;
    for (const box_node & c : at.children) { n += count_kind(c, kind); }
    return n;
}

bool near(float a, float b) {
    return std::fabs(a - b) < 0.01f;
}

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %s\n", std::string{what}.c_str());
        ++ctbrowser_test_failures;
    }
}

void expect_near(float got, float want, std::string_view what) {
    if (!near(got, want)) {
        std::printf("FAIL %-52s got %.3f want %.3f\n", std::string{what}.c_str(),
                    static_cast<double>(got), static_cast<double>(want));
        ++ctbrowser_test_failures;
    }
}

// Every test lays a document out at a viewport width and asks about elements by
// id, so the whole of the boilerplate is this.
struct laid_out {
    fixture f;
    fragment out;

    void run(std::string_view html, std::string_view css, float viewport = 800) {
        f.load(html, css);
        engine eng{monospace_measure()};
        out = eng.run(f.root, viewport);
    }
    [[nodiscard]] const fragment * at(std::string_view id) { return out.find(f.find_id(id)); }
    // Absolute x, which is what a reader of these tests cares about: bounds are
    // stored relative to the containing block, so a nested item's own x says
    // nothing on its own.
    [[nodiscard]] float x_of(std::string_view id) {
        const node_id want = f.find_id(id);
        float found = 0;
        const auto walk = [&](auto && self, const fragment & at, float dx) -> void {
            if (at.source == want) { found = at.bounds.x + dx; }
            for (const fragment & c : at.children) { self(self, c, dx + at.bounds.x); }
        };
        walk(walk, out, 0);
        return found;
    }
    [[nodiscard]] float y_of(std::string_view id) {
        const node_id want = f.find_id(id);
        float found = 0;
        const auto walk = [&](auto && self, const fragment & at, float dy) -> void {
            if (at.source == want) { found = at.bounds.y + dy; }
            for (const fragment & c : at.children) { self(self, c, dy + at.bounds.y); }
        };
        walk(walk, out, 0);
        return found;
    }
};

// The preamble every test shares: a body with no box of its own, so a number is
// about flex rather than about the UA sheet.
constexpr std::string_view reset = "body { margin: 0; padding: 0 } ";

// --- 1. the box tree ------------------------------------------------------

void test_display_flex_makes_a_flex_container() {
    fixture f;
    f.load("<html><body><div id=a><div id=b>x</div></div></body></html>",
           "#a { display: flex }");
    const box_node * a = box_for(f.root, f.find_id("a"));
    check(a != nullptr && a->kind == box_kind::flex, "display:flex makes a flex box");
    check(a != nullptr && a->is_block_level(), "a flex container is block-level");
    // THE TRAP THE PARALLEL DRIVER RESTS ON. A flex container's children are flex
    // items whatever they are made of, so it never establishes an inline context
    // - and engine::run_parallel reads exactly this predicate to decide whether a
    // box's children are independent.
    check(a != nullptr && !a->establishes_inline_context(),
          "a flex container establishes no inline context");
}

void test_inline_flex_is_inline_level() {
    fixture f;
    f.load("<html><body><div id=a>x</div></body></html>", "#a { display: inline-flex }");
    const box_node * a = box_for(f.root, f.find_id("a"));
    check(a != nullptr && a->kind == box_kind::flex, "inline-flex is the same formatting context");
    check(a != nullptr && a->inline_level, "...at an inline level");
    check(a != nullptr && !a->is_block_level(), "so it is not block-level");
}

void test_inline_runs_become_anonymous_items() {
    {
        // A homogeneous run STILL WRAPS, which is what makes the flex rule
        // different from the block one: a flex container has no inline formatting
        // context to fall back on.
        fixture f;
        f.load("<html><body><div id=a>plain text</div></body></html>", "#a { display: flex }");
        const box_node * a = box_for(f.root, f.find_id("a"));
        check(a != nullptr && a->children.size() == 1 &&
                  a->children[0].kind == box_kind::anonymous,
              "text alone in a flex container becomes one anonymous item");
    }
    {
        // TEXT is what gets wrapped, not "everything inline-level". A replaced
        // element is inline-level and is an item in its own right - wrapping two
        // <img>s in an anonymous box would make them ONE item and lay them out on
        // a line inside it.
        fixture f;
        f.load("<html><body><div id=a>alpha<img id=i>beta</div></body></html>",
               "#a { display: flex }");
        const box_node * a = box_for(f.root, f.find_id("a"));
        check(a != nullptr && a->children.size() == 3, "run, replaced, run: three items");
        check(a != nullptr && count_kind(*a, box_kind::anonymous) == 2,
              "each contiguous run of TEXT is one anonymous item");
        const box_node * i = box_for(f.root, f.find_id("i"));
        check(i != nullptr && i->kind == box_kind::replaced, "and the <img> is an item itself");
    }
    {
        // An inline ELEMENT is not part of the run either: being a flex item
        // blockified it, so it flushes whatever text came before it.
        fixture f;
        f.load("<html><body><div id=a>one <b id=t>two</b></div></body></html>",
               "#a { display: flex }");
        const box_node * a = box_for(f.root, f.find_id("a"));
        check(a != nullptr && a->children.size() == 2, "text then <b>: two items");
        check(a != nullptr && count_kind(*a, box_kind::anonymous) == 1, "one of them anonymous");
    }
    {
        // A run of ONLY WHITESPACE is not rendered, which is what stops the
        // newline between two items becoming a third column.
        fixture f;
        f.load("<html><body><div id=a>\n  <img id=i>\n  <img id=j>\n</div></body></html>",
               "#a { display: flex }");
        const box_node * a = box_for(f.root, f.find_id("a"));
        check(a != nullptr && a->children.size() == 2, "whitespace between items produces no item");
    }
}

void test_a_flex_items_display_is_blockified() {
    fixture f;
    f.load("<html><body><div id=a><span id=s>x</span></div></body></html>", "#a { display: flex }");
    const box_node * s = box_for(f.root, f.find_id("s"));
    // CSS Display 3 §2.7. Without it a <span> item is an inline box inside a
    // container that has no inline algorithm to lay it out with - and Chrome
    // reports `block` for `<a class=nav-link>` on 50 of one fixture's elements.
    check(s != nullptr && s->kind == box_kind::block, "an inline flex item is blockified");
}

// --- 2. main sizing -------------------------------------------------------

void test_items_share_one_line() {
    laid_out t;
    t.run("<html><body><div id=f><div id=a>a</div><div id=b>b</div></div></body></html>",
          std::string{reset}
              .append("#f { display: flex; width: 400px } "
                      "#f > div { flex: 0 0 100px }")
              .c_str());
    expect_near(t.x_of("a"), 0, "first item at the start");
    expect_near(t.y_of("a"), 0, "...");
    // THE HEADLINE. Before this rung both divs were blocks and b sat BELOW a,
    // which is why 102 of the grid fixture's 111 elements differed on @y.
    expect_near(t.x_of("b"), 100, "the second item sits BESIDE the first");
    expect_near(t.y_of("b"), 0, "not below it");
}

void test_the_bootstrap_grid_traced() {
    // The real rules, spelled out rather than linked: `.container` at a 1024
    // viewport is 960 wide with 12px of padding a side, `.row` cancels that
    // padding with -12px margins, and `.col` is `flex: 1 0 0` under a
    // `width: 100%; max-width: 100%` that only the clamp ever sees.
    laid_out t;
    t.run("<html><body><div id=c><div id=r>"
          "<div class=col id=a>a</div><div class=col id=b>b</div><div class=col id=d>c</div>"
          "</div></div></body></html>",
          std::string{reset}
              .append("#c { width: 960px; padding-left: 12px; padding-right: 12px } "
                      "#r { display: flex; flex-wrap: wrap; margin-left: -12px; "
                      "     margin-right: -12px } "
                      ".col { flex-grow: 1; flex-shrink: 0; flex-basis: 0; "
                      "       width: 100%; max-width: 100%; "
                      "       padding-left: 12px; padding-right: 12px }")
              .c_str(),
          1024);
    const fragment * row = t.at("r");
    check(row != nullptr, "the row produced a fragment");
    if (row == nullptr) { return; }
    // THE SIGN TRAP. `.row`'s negative margins WIDEN it: 936 of content less
    // twice -12 is 960. Subtracting them the other way gives 912, which looks
    // plausible and doubles every gutter on the page.
    expect_near(row->bounds.width, 960, "negative side margins widen the row to 960");
    expect_near(t.x_of("r"), 0, "and pull it back over the container's padding");

    // Base size 0, so all 960 is free space; three equal grow factors; each
    // clamped to [min-content, 960] and none of them violating.
    for (const std::string_view id : {"a", "b", "d"}) {
        const fragment * col = t.at(id);
        if (col != nullptr) { expect_near(col->bounds.width, 320, "each .col takes a third"); }
    }
    expect_near(t.x_of("a"), 0, "first column");
    expect_near(t.x_of("b"), 320, "second column");
    expect_near(t.x_of("d"), 640, "third column");
}

void test_a_stated_width_is_the_flex_base_size() {
    // `.col-6 { flex: 0 0 auto; width: 50% }`: a basis of `auto` falls back to
    // the item's own width, and with no grow and no shrink that is final.
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div><div id=b>b</div></div></body></html>",
          std::string{reset}
              .append("#r { display: flex; width: 960px } "
                      "#r > div { flex-grow: 0; flex-shrink: 0; flex-basis: auto; width: 50% }")
              .c_str());
    const fragment * a = t.at("a");
    if (a != nullptr) { expect_near(a->bounds.width, 480, "a 50% basis is half the row"); }
    expect_near(t.x_of("b"), 480, "and the second starts where the first ends");
}

void test_a_flexible_item_takes_what_the_stated_ones_leave() {
    // The case the freeze loop earns itself on: one item is inflexible at a third
    // of the row and the other must take exactly the rest.
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div><div id=b>b</div></div></body></html>",
          std::string{reset}
              .append("#r { display: flex; width: 900px } "
                      "#a { flex: 0 0 auto; width: 300px } "
                      "#b { flex-grow: 1; flex-shrink: 1; flex-basis: 0; min-width: 0 }")
              .c_str());
    const fragment * b = t.at("b");
    if (b != nullptr) { expect_near(b->bounds.width, 600, "the flexible item takes the remainder"); }
    expect_near(t.x_of("b"), 300, "starting after the stated one");
}

void test_shrinking_is_weighted_by_the_base_size() {
    // GROW IS NOT WEIGHTED AND SHRINK IS. Two items with the same `flex-shrink`
    // give up the same PROPORTION of themselves, not the same number of pixels -
    // which is what stops a narrow item collapsing beside a wide one.
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div><div id=b>b</div></div></body></html>",
          std::string{reset}
              .append("#r { display: flex; width: 300px } "
                      "#r > div { flex-grow: 0; flex-shrink: 1; min-width: 0 } "
                      "#a { flex-basis: 400px } #b { flex-basis: 200px }")
              .c_str());
    const fragment * a = t.at("a");
    const fragment * b = t.at("b");
    if (a != nullptr) { expect_near(a->bounds.width, 200, "the wide item gives up 200"); }
    if (b != nullptr) { expect_near(b->bounds.width, 100, "the narrow one gives up 100"); }
}

void test_a_flex_factor_under_one_takes_only_its_share() {
    // §9.7.3. With the unfrozen factors summing to less than one, only that
    // FRACTION of the free space is distributed - so `flex-grow: .5` takes half
    // the room rather than all of it, which is the case an unguarded
    // free * grow / sum gets exactly backwards.
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div></div></body></html>",
          std::string{reset}
              .append("#r { display: flex; width: 300px } "
                      "#a { flex-grow: .5; flex-shrink: 1; flex-basis: 0; min-width: 0 }")
              .c_str());
    const fragment * a = t.at("a");
    if (a != nullptr) { expect_near(a->bounds.width, 150, "half the free space, not all of it"); }
}

void test_the_sub_one_share_is_of_the_INITIAL_free_space() {
    // §9.7.4, and the case that separates a correct freeze loop from a plausible
    // one. Two items grow from zero in 1000px with factors summing to .75; the
    // first hits `max-width: 100px` and freezes, and the second's share is then
    // .25 of the free space as it was BEFORE anything froze - 250 - not .25 of
    // what is left after the freeze, which would be 225.
    //
    // Nothing catches this with one item or with factors summing past one, which
    // is why every other test here is blind to it.
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div><div id=b>b</div></div></body></html>",
          std::string{reset}
              .append("#r { display: flex; width: 1000px } "
                      "#r > div { flex-shrink: 1; flex-basis: 0; min-width: 0 } "
                      "#a { flex-grow: .5; max-width: 100px } #b { flex-grow: .25 }")
              .c_str(),
          1200);
    const fragment * a = t.at("a");
    const fragment * b = t.at("b");
    if (a != nullptr) { expect_near(a->bounds.width, 100, "the capped item freezes at its max"); }
    if (b != nullptr) {
        expect_near(b->bounds.width, 250, "the other takes a quarter of the INITIAL free space");
    }
}

void test_shrinking_is_weighted_by_the_INNER_base_size() {
    // §9.7.4 weights a shrink by the item's CONTENT-box base size, not its border
    // box - a distinction with no effect until one item is padded, and then a
    // large one. Two 200px items shrinking into 300px are 150/150 unpadded; give
    // one of them 100px of padding and its inner base is 100 against the other's
    // 200, so it gives up a third of the deficit rather than half.
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div><div id=b>b</div></div></body></html>",
          std::string{reset}
              .append("#r { display: flex; width: 300px } "
                      "#r > div { flex-grow: 0; flex-shrink: 1; flex-basis: 200px; min-width: 0 } "
                      "#a { padding-left: 50px; padding-right: 50px }")
              .c_str());
    const fragment * a = t.at("a");
    const fragment * b = t.at("b");
    if (a != nullptr) { expect_near(a->bounds.width, 200 - 100.0f / 3, "the padded item gives 1/3"); }
    if (b != nullptr) { expect_near(b->bounds.width, 200 - 200.0f / 3, "the bare one gives 2/3"); }
}

void test_a_stretched_item_still_honours_its_own_maximum() {
    // §9.4.11: stretch is clamped by the item's used min and max cross size. It
    // was not, on the row axis only, so the two axes disagreed about one rule.
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div></div></body></html>",
          std::string{reset}
              .append("#r { display: flex; width: 300px; height: 200px } "
                      "#a { flex: 0 0 100px; max-height: 50px; min-width: 0 }")
              .c_str());
    const fragment * a = t.at("a");
    if (a != nullptr) { expect_near(a->bounds.height, 50, "max-height caps the stretch"); }
}

void test_a_percentage_maximum_that_cannot_resolve_is_no_maximum() {
    // A column with no stated height has an INDEFINITE main size, so `max-height:
    // 100%` has nothing to resolve against and behaves as `none`. Resolving it
    // against zero instead made it a definite maximum of 0, which then clamped the
    // automatic minimum to 0 as well and collapsed the item to nothing.
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div></div></body></html>",
          std::string{reset}
              .append("#r { display: flex; flex-direction: column; width: 200px } "
                      "#a { height: 20px; max-height: 100% }")
              .c_str());
    const fragment * a = t.at("a");
    if (a != nullptr) { expect_near(a->bounds.height, 20, "an unresolvable percentage max is none"); }
}

void test_reverse_puts_the_leading_margin_at_the_flex_start_edge() {
    // Under `row-reverse` the main-start edge is the RIGHT one, so the margin that
    // leads is `margin-right`. Naming the margins physically offset every reversed
    // item by exactly (trailing - leading), which is invisible whenever the two
    // are equal - so a symmetric test cannot see it.
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div></div></body></html>",
          std::string{reset}
              .append("#r { display: flex; flex-direction: row-reverse; width: 300px } "
                      "#a { flex: 0 0 100px; margin-left: 20px; margin-right: 0; min-width: 0 }")
              .c_str());
    const fragment * a = t.at("a");
    // The item's margin box occupies [180, 300]; with no right margin its border
    // box starts at 200, and the 20px left margin sits to its left.
    if (a != nullptr) { expect_near(t.x_of("a"), 200, "the RIGHT margin leads in row-reverse"); }
}

void test_a_min_width_violation_is_redistributed() {
    // THE CASE THAT NEEDS THE LOOP. Three items grow from zero in a 300px row:
    // one pass gives 100 each, but the first has a 200px minimum. Freezing it
    // there leaves 100 for the other two, which is 50 apiece - an answer no
    // single proportional pass produces.
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div><div id=b>b</div><div id=d>c</div>"
          "</div></body></html>",
          std::string{reset}
              .append("#r { display: flex; width: 300px } "
                      "#r > div { flex-grow: 1; flex-shrink: 1; flex-basis: 0; min-width: 0 } "
                      // `#r > div#a`, not `#a`: the general rule above carries an
                      // id AND a type, so a bare `#a` loses to it on specificity
                      // and the minimum this test is about would never be set.
                      "#r > div#a { min-width: 200px }")
              .c_str());
    const fragment * a = t.at("a");
    const fragment * b = t.at("b");
    const fragment * d = t.at("d");
    if (a != nullptr) { expect_near(a->bounds.width, 200, "the clamped item keeps its minimum"); }
    if (b != nullptr) { expect_near(b->bounds.width, 50, "and the rest re-share what is left"); }
    if (d != nullptr) { expect_near(d->bounds.width, 50, "..."); }
}

void test_a_max_width_violation_is_redistributed() {
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div><div id=b>b</div></div></body></html>",
          std::string{reset}
              .append("#r { display: flex; width: 300px } "
                      "#r > div { flex-grow: 1; flex-shrink: 1; flex-basis: 0; min-width: 0 } "
                      "#a { max-width: 100px }")
              .c_str());
    const fragment * a = t.at("a");
    const fragment * b = t.at("b");
    if (a != nullptr) { expect_near(a->bounds.width, 100, "the capped item stops at its maximum"); }
    if (b != nullptr) { expect_near(b->bounds.width, 200, "the other absorbs what it gave back"); }
}

void test_the_automatic_minimum_size() {
    // §4.5: `min-width: auto` on a flex item is the CONTENT-BASED minimum, not
    // zero. A ten-character word is 96px wide at the monospace measure, and an
    // item with a zero basis and no grow is exactly that wide rather than empty.
    const std::string html =
        "<html><body><div id=r><div id=a>xxxxxxxxxx</div><div id=b>y</div></div></body></html>";
    {
        laid_out t;
        t.run(html, std::string{reset}
                        .append("#r { display: flex; width: 600px } "
                                "#r > div { flex-grow: 0; flex-shrink: 1; flex-basis: 0 }")
                        .c_str());
        const fragment * a = t.at("a");
        if (a != nullptr) {
            expect_near(a->bounds.width, 96, "an unbreakable word sets the item's floor");
        }
    }
    {
        // AND `min-width: 0` DEFEATS IT, which is exactly why Bootstrap's `.card`
        // carries that declaration - the evidence that the rule above is real.
        laid_out t;
        t.run(html, std::string{reset}
                        .append("#r { display: flex; width: 600px } "
                                "#r > div { flex-grow: 0; flex-shrink: 1; flex-basis: 0; "
                                "           min-width: 0 }")
                        .c_str());
        const fragment * a = t.at("a");
        if (a != nullptr) { expect_near(a->bounds.width, 0, "min-width: 0 defeats it"); }
    }
}

// --- 3. lines -------------------------------------------------------------

void test_wrapping_collects_into_lines() {
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div><div id=b>b</div><div id=d>c</div>"
          "</div></body></html>",
          std::string{reset}
              .append("#r { display: flex; flex-wrap: wrap; width: 300px } "
                      "#r > div { flex: 0 0 140px; height: 20px; min-width: 0 }")
              .c_str());
    expect_near(t.x_of("a"), 0, "first item on line one");
    expect_near(t.x_of("b"), 140, "second item fits beside it");
    expect_near(t.y_of("b"), 0, "...on the same line");
    // The third does not fit, and the second line's cross position is the first
    // line's cross SIZE - which is why lines are collected before anything is
    // placed rather than as it goes.
    expect_near(t.x_of("d"), 0, "the third starts a new line");
    expect_near(t.y_of("d"), 20, "below the first line's cross size");
}

void test_nowrap_overflows_rather_than_wrapping() {
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div><div id=b>b</div><div id=d>c</div>"
          "</div></body></html>",
          std::string{reset}
              .append("#r { display: flex; width: 300px } "
                      "#r > div { flex: 0 0 140px; height: 20px; min-width: 0 }")
              .c_str());
    expect_near(t.y_of("d"), 0, "nowrap keeps everything on one line");
    expect_near(t.x_of("d"), 280, "...overflowing rather than wrapping");
}

// --- 4. alignment ---------------------------------------------------------

void test_justify_content() {
    const std::string html =
        "<html><body><div id=r><div id=a>a</div><div id=b>b</div></div></body></html>";
    const auto sheet = [](std::string_view justify) {
        return std::string{reset}
            .append("#r { display: flex; width: 300px; justify-content: ")
            .append(justify)
            .append(" } #r > div { flex: 0 0 100px; min-width: 0 }");
    };
    struct expectation {
        std::string_view justify;
        float first, second;
        std::string_view what;
    };
    // All six, and `normal` - which is `flex-start` for justify-content and
    // `stretch` for the align-* trio, the one place the shared enum's two
    // properties disagree about what "nothing was said" means.
    constexpr expectation cases[] = {
        {"normal", 0, 100, "normal packs at the start"},
        {"flex-start", 0, 100, "flex-start packs at the start"},
        {"flex-end", 100, 200, "flex-end packs at the end"},
        {"center", 50, 150, "center splits the free space"},
        {"space-between", 0, 200, "space-between puts it all in the middle"},
        {"space-around", 25, 175, "space-around gives each item half a share"},
        {"space-evenly", 33.3333f, 166.6667f, "space-evenly makes all three gaps equal"}};
    for (const expectation & c : cases) {
        laid_out t;
        t.run(html, sheet(c.justify).c_str());
        expect_near(t.x_of("a"), c.first, c.what);
        expect_near(t.x_of("b"), c.second, c.what);
    }
}

void test_align_items_and_align_self() {
    const std::string html =
        "<html><body><div id=r><div id=a>a</div><div id=b>b</div></div></body></html>";
    {
        laid_out t;
        t.run(html, std::string{reset}
                        .append("#r { display: flex; width: 300px; height: 100px; "
                                "     align-items: center } "
                                "#r > div { flex: 0 0 100px; height: 20px; min-width: 0 }")
                        .c_str());
        expect_near(t.y_of("a"), 40, "align-items: center centres in the line");
    }
    {
        laid_out t;
        t.run(html, std::string{reset}
                        .append("#r { display: flex; width: 300px; height: 100px; "
                                "     align-items: flex-end } "
                                "#r > div { flex: 0 0 100px; height: 20px; min-width: 0 } "
                                "#b { align-self: flex-start }")
                        .c_str());
        expect_near(t.y_of("a"), 80, "align-items: flex-end pushes to the cross end");
        // `align-self` beats the container, which is the whole reason it exists.
        expect_near(t.y_of("b"), 0, "align-self overrides it for one item");
    }
    {
        // STRETCH IS A SIZE, not a position - and it is the default, which is why
        // every column of a Bootstrap row is as tall as the tallest.
        laid_out t;
        t.run(html, std::string{reset}
                        .append("#r { display: flex; width: 300px; height: 100px } "
                                "#r > div { flex: 0 0 100px; min-width: 0 }")
                        .c_str());
        const fragment * a = t.at("a");
        if (a != nullptr) { expect_near(a->bounds.height, 100, "an auto cross size stretches"); }
    }
}

void test_align_content_distributes_the_lines() {
    const std::string html = "<html><body><div id=r><div id=a>a</div><div id=b>b</div>"
                             "<div id=d>c</div></div></body></html>";
    const auto sheet = [](std::string_view how) {
        return std::string{reset}
            .append("#r { display: flex; flex-wrap: wrap; width: 300px; height: 200px; "
                    "     align-content: ")
            .append(how)
            .append(" } #r > div { flex: 0 0 140px; height: 20px; min-width: 0 }");
    };
    {
        laid_out t;
        t.run(html, sheet("flex-start").c_str());
        expect_near(t.y_of("a"), 0, "flex-start stacks the lines at the cross start");
        expect_near(t.y_of("d"), 20, "...");
    }
    {
        laid_out t;
        t.run(html, sheet("center").c_str());
        // Two lines of 20 in 200 leaves 160; centring puts 80 above.
        expect_near(t.y_of("a"), 80, "center puts half the leftover above the lines");
        expect_near(t.y_of("d"), 100, "...");
    }
    {
        // STRETCH IS THE DEFAULT and the only value that changes a SIZE rather
        // than a position: the two lines share the leftover, so the second starts
        // halfway down.
        laid_out t;
        t.run(html, sheet("stretch").c_str());
        expect_near(t.y_of("a"), 0, "stretch leaves the first line where it is");
        expect_near(t.y_of("d"), 100, "and grows each line by an equal share");
    }
}

void test_auto_margins_absorb_the_free_space() {
    // §9.5, and it is how Bootstrap pushes half a navbar to the right:
    // `.navbar-nav { margin-right: auto }`. An auto margin eats the free space
    // BEFORE justify-content sees any of it.
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div><div id=b>b</div></div></body></html>",
          std::string{reset}
              .append("#r { display: flex; width: 300px } "
                      "#r > div { flex: 0 0 100px; min-width: 0 } "
                      "#a { margin-right: auto }")
              .c_str());
    expect_near(t.x_of("a"), 0, "the item before the auto margin stays put");
    expect_near(t.x_of("b"), 200, "and everything after it is pushed to the end");
}

// --- 5. the rest of the property surface ----------------------------------

void test_order_is_a_stable_sort() {
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div><div id=b>b</div><div id=d>c</div>"
          "</div></body></html>",
          std::string{reset}
              .append("#r { display: flex; width: 300px } "
                      "#r > div { flex: 0 0 100px; min-width: 0 } "
                      "#a { order: 3 } #b { order: 1 } #d { order: 2 }")
              .c_str());
    // Visual order b, d, a - and the geometry is the only thing that moves. The
    // DOM, the box tree and every id lookup are untouched.
    expect_near(t.x_of("b"), 0, "order 1 comes first");
    expect_near(t.x_of("d"), 100, "then order 2");
    expect_near(t.x_of("a"), 200, "then order 3, whatever the source said");
}

void test_gaps() {
    {
        laid_out t;
        t.run("<html><body><div id=r><div id=a>a</div><div id=b>b</div><div id=d>c</div>"
              "</div></body></html>",
              std::string{reset}
                  .append("#r { display: flex; width: 300px; gap: 10px } "
                          "#r > div { flex: 0 0 50px; min-width: 0 }")
                  .c_str());
        expect_near(t.x_of("b"), 60, "column-gap separates items along the main axis");
        expect_near(t.x_of("d"), 120, "...");
    }
    {
        // `gap: 1rem 2rem` is ROW then COLUMN - the block axis first, which is the
        // opposite of how every other two-value shorthand here reads.
        laid_out t;
        t.run("<html><body><div id=r><div id=a>a</div><div id=b>b</div><div id=d>c</div>"
              "</div></body></html>",
              std::string{reset}
                  .append("#r { display: flex; flex-wrap: wrap; width: 300px; gap: 8px 10px } "
                          "#r > div { flex: 0 0 140px; height: 20px; min-width: 0 }")
                  .c_str());
        expect_near(t.x_of("b"), 150, "the SECOND value is the column gap");
        expect_near(t.y_of("d"), 28, "the FIRST is the row gap, between the lines");
    }
}

void test_flex_direction() {
    const std::string html =
        "<html><body><div id=r><div id=a>a</div><div id=b>b</div></div></body></html>";
    {
        laid_out t;
        t.run(html, std::string{reset}
                        .append("#r { display: flex; flex-direction: column; width: 200px } "
                                "#r > div { height: 30px }")
                        .c_str());
        expect_near(t.y_of("a"), 0, "a column stacks down the main axis");
        expect_near(t.y_of("b"), 30, "...");
        const fragment * a = t.at("a");
        if (a != nullptr) { expect_near(a->bounds.width, 200, "and stretches across it"); }
    }
    {
        laid_out t;
        t.run(html, std::string{reset}
                        .append("#r { display: flex; flex-direction: row-reverse; width: 300px } "
                                "#r > div { flex: 0 0 100px; min-width: 0 }")
                        .c_str());
        expect_near(t.x_of("a"), 200, "row-reverse starts the first item at the end");
        expect_near(t.x_of("b"), 100, "and runs backwards");
    }
    {
        laid_out t;
        t.run(html,
              std::string{reset}
                  .append("#r { display: flex; flex-direction: column-reverse; height: 100px; "
                          "     width: 200px } "
                          "#r > div { flex: 0 0 30px }")
                  .c_str());
        expect_near(t.y_of("a"), 70, "column-reverse starts at the bottom");
        expect_near(t.y_of("b"), 40, "...");
    }
}

void test_inline_flex_shrinks_to_fit() {
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div><div id=b>b</div></div></body></html>",
          std::string{reset}
              .append("#r { display: inline-flex } "
                      "#r > div { flex: 0 0 100px; min-width: 0 }")
              .c_str());
    const fragment * r = t.at("r");
    // A block-level flex container would be 800 wide here. Shrink-to-fit is the
    // one thing `inline-flex` does differently, and the reason `inline_level` is
    // a field on box_node rather than a fifth box_kind.
    if (r != nullptr) { expect_near(r->bounds.width, 200, "an inline-flex box wraps its content"); }
}

void test_a_flex_container_can_be_a_flex_item() {
    // A nested row: the outer item's main size comes from the outer freeze loop,
    // and the inner container then lays its own items out inside exactly that.
    laid_out t;
    t.run("<html><body><div id=r><div id=a><div id=x>x</div><div id=y>y</div></div>"
          "<div id=b>b</div></div></body></html>",
          std::string{reset}
              .append("#r { display: flex; width: 800px } "
                      "#r > div { flex: 0 0 400px; min-width: 0 } "
                      "#a { display: flex } "
                      "#a > div { flex: 1 1 0; min-width: 0 }")
              .c_str());
    const fragment * x = t.at("x");
    if (x != nullptr) { expect_near(x->bounds.width, 200, "the nested row splits its own 400"); }
    expect_near(t.x_of("y"), 200, "and its second item starts halfway across it");
    expect_near(t.x_of("b"), 400, "while the outer row is unaffected");
}

void test_padding_is_inside_the_flex_base_size() {
    // The engine's convention throughout is that a stated size is the BORDER box
    // - `box-sizing: border-box`, which Bootstrap sets on `*`. An item's padding
    // therefore comes out of its flex base size rather than adding to it, and
    // its content is what is left.
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div></div></body></html>",
          std::string{reset}
              .append("#r { display: flex; width: 300px } "
                      "#a { flex: 0 0 200px; padding-left: 20px; padding-right: 20px; "
                      "     min-width: 0 }")
              .c_str());
    const fragment * a = t.at("a");
    if (a != nullptr) { expect_near(a->bounds.width, 200, "the basis names the border box"); }
}

} // namespace

int main() {
    test_display_flex_makes_a_flex_container();
    test_inline_flex_is_inline_level();
    test_inline_runs_become_anonymous_items();
    test_a_flex_items_display_is_blockified();

    test_items_share_one_line();
    test_the_bootstrap_grid_traced();
    test_a_stated_width_is_the_flex_base_size();
    test_a_flexible_item_takes_what_the_stated_ones_leave();
    test_shrinking_is_weighted_by_the_base_size();
    test_a_flex_factor_under_one_takes_only_its_share();
    test_the_sub_one_share_is_of_the_INITIAL_free_space();
    test_shrinking_is_weighted_by_the_INNER_base_size();
    test_a_stretched_item_still_honours_its_own_maximum();
    test_a_percentage_maximum_that_cannot_resolve_is_no_maximum();
    test_reverse_puts_the_leading_margin_at_the_flex_start_edge();
    test_a_min_width_violation_is_redistributed();
    test_a_max_width_violation_is_redistributed();
    test_the_automatic_minimum_size();

    test_wrapping_collects_into_lines();
    test_nowrap_overflows_rather_than_wrapping();

    test_justify_content();
    test_align_items_and_align_self();
    test_align_content_distributes_the_lines();
    test_auto_margins_absorb_the_free_space();

    test_order_is_a_stable_sort();
    test_gaps();
    test_flex_direction();
    test_inline_flex_shrinks_to_fit();
    test_a_flex_container_can_be_a_flex_item();
    test_padding_is_inside_the_flex_base_size();

    REPORT("flex_basics");
}
