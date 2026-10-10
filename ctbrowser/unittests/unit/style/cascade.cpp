//===- cascade.cpp - Cascade, inheritance and @media tests ------*- C++ -*-===//
//
// The cascade: specificity and source order, the origins, the style attribute
// and !important, inheritance and the explicit defaulting keywords, @media,
// @font-face, and the `ch` unit. The fixture shared by every test in
// unittests/unit/style/ is in fixture.hpp.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "fixture.hpp"

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using namespace ctbrowser::style;

namespace {

void test_specificity_and_source_order() {
    fixture f;
    f.load("<div id=x class=c>hi</div>",
           "div { color: tag }"  // 0,0,1
           ".c { color: class }" // 0,1,0 - beats tag
           "#x { color: id }");  // 1,0,0 - beats class
    const node_id div = f.find("div");
    expect_value(f, div, "color", "id", "id beats class beats tag");

    fixture g;
    g.load("<div class=c>hi</div>", ".c { color: first } .c { color: second }");
    expect_value(g, g.find("div"), "color", "second", "equal specificity: later wins");
}

// Author rules beat user-agent rules even when the UA selector is more
// specific - origin outranks specificity in the cascade.
void test_author_beats_user_agent() {
    fixture f;
    f.load("<div id=x>hi</div>",
           "div { color: author }", // Author, low specificity.
           "#x { color: ua }");     // UA, high specificity
    expect_value(f, f.find("div"), "color", "author", "author origin outranks UA specificity");
}

// The point of interning: elements that resolve identically must share one
// style object, so comparing them is a pointer compare.
void test_identical_styles_are_shared() {
    fixture f;
    f.load("<ul><li>a</li><li>b</li><li>c</li><li>d</li></ul>", "li { color: red; margin: 0 }");
    const auto txn = f.doc.read();
    const node_id ul = f.find("ul");
    const auto items = txn.children(ul);
    CHECK(items.size() >= 4);

    const computed_style_ptr first = f.style_of(items[0]);
    CHECK(static_cast<bool>(first));
    bool all_shared = true;
    for (const node_id li : items) {
        if (f.style_of(li).get() != first.get()) { all_shared = false; }
    }
    CHECK(all_shared); // Pointer equality, not just value equality.

    // And the table holds only a few distinct styles for this document.
    const std::size_t distinct = f.styles.styles().distinct_styles();
    std::printf("  4 <li> + ul + text nodes -> %zu distinct styles\n", distinct);
    CHECK(distinct <= 3);
}

// --- the style attribute --------------------------------------------------
//
// Not a separate origin: author-level with a specificity above every selector.
// Chrome and Firefox both order it
//
//   normal selector < normal inline < important selector < important inline
//
// and every one of those four steps is a separate test below, because getting
// the middle two the wrong way round is the easy mistake and it is invisible
// until a page uses !important to override a widget's inline style.

void test_inline_style_applies() {
    fixture f;
    f.load("<p style='color: red'>hi</p>", "");
    expect_value(f, f.find("p"), "color", "red", "a style attribute is read at all");

    // Several declarations, and the whitespace and trailing semicolon real
    // pages write.
    fixture g;
    g.load("<p style=' color : blue ; height:20px; '>hi</p>", "");
    expect_value(g, g.find("p"), "color", "blue", "the first of several");
    expect_value(g, g.find("p"), "height", "20px", "and the last, past a trailing ;");
}

void test_inline_beats_any_selector() {
    fixture f;
    // #x is the most specific selector there is short of !important, and a
    // plain style attribute still wins.
    f.load("<p id=x class=c style='color: green'>hi</p>", "p { color: red } .c { color: blue } "
                                                          "#x { color: purple }");
    expect_value(f, f.find("p"), "color", "green", "inline beats even an id selector");
}

void test_important_selector_beats_inline() {
    fixture f;
    // This is the step that is easy to get wrong: appending the style
    // attribute after everything would make it win here, and it must not.
    f.load("<p style='color: green'>hi</p>", "p { color: red !important }");
    expect_value(f, f.find("p"), "color", "red",
                 "!important in a stylesheet beats a normal style attribute");
}

void test_important_inline_beats_everything() {
    fixture f;
    f.load("<p style='color: green !important'>hi</p>", "p { color: red !important }");
    expect_value(f, f.find("p"), "color", "green", "!important inline wins outright");

    // And an important inline declaration does not disturb the normal ones
    // beside it.
    fixture g;
    g.load("<p style='color: green !important; height: 5px'>hi</p>",
           "p { color: red !important; height: 9px !important }");
    expect_value(g, g.find("p"), "color", "green", "the important one wins");
    expect_value(g, g.find("p"), "height", "9px", "the normal one still loses to !important");
}

void test_inline_style_oddities() {
    fixture f;
    // A malformed declaration is dropped, and the ones around it survive -
    // which is what a browser does rather than discarding the whole attribute.
    f.load("<p style='color: red; nonsense; height: 3px'>hi</p>", "");
    expect_value(f, f.find("p"), "color", "red", "before the rubbish");
    expect_value(f, f.find("p"), "height", "3px", "after it");

    // An empty attribute is not a style, and must not resolve to one.
    fixture g;
    g.load("<p style=''>hi</p>", "p { color: red }");
    expect_value(g, g.find("p"), "color", "red", "an empty attribute changes nothing");

    // Later wins within the attribute itself, like any declaration block.
    fixture h;
    h.load("<p style='color: red; color: blue'>hi</p>", "");
    expect_value(h, h.find("p"), "color", "blue", "the last declaration wins");

    // The property name is case-insensitive, the way CSS is.
    fixture i;
    i.load("<p style='COLOR: red'>hi</p>", "");
    expect_value(i, i.find("p"), "color", "red", "property names fold case");
}

void test_inline_style_is_per_element() {
    fixture f;
    f.load("<div><p id=a style='color: red'>one</p><p id=b style='color: blue'>two</p>"
           "<p id=c>three</p></div>",
           "p { color: black }");
    // The parse is cached by attribute text, so this is also the check that the
    // cache is not handing every element the first one it saw.
    expect_value(f, f.find_id("a"), "color", "red", "the first element");
    expect_value(f, f.find_id("b"), "color", "blue", "the second");
    expect_value(f, f.find_id("c"), "color", "black", "and one with no attribute");
}

// Inheritance is done by the cascade itself, not by layout or painting, and
// interned styles must still be shared after it.
void test_inheritance() {
    {
        fixture f;
        f.load("<div><p><em id=deep></em></p></div>", "div { color: #010101 }");
        expect_value(f, f.find_id("deep"), "color", "#010101", "inherits through two levels");
    }
    {
        // A non-inherited property does not travel.
        fixture f;
        f.load("<div><p id=child></p></div>", "div { background-color: #010101 }");
        CHECK(f.value_of(f.find_id("child"), "background-color").empty());
    }
    {
        // An element's own declaration beats what came down to it, whatever their
        // specificities: this is `own` before `inherited` in get(), not the cascade.
        fixture f;
        f.load("<div><p id=child></p></div>",
               "#nothing { color: #030303 } div { color: #010101 } p { color: #020202 }");
        expect_value(f, f.find_id("child"), "color", "#020202", "own beats inherited");
    }
    {
        // Custom properties inherit. This is what lets a button read Bootstrap's
        // `--bs-*` variables declared on `:root`.
        fixture f;
        f.load("<html><body><button id=b></button></body></html>", ":root { --bs-blue: #0d6efd }");
        expect_value(f, f.find_id("b"), "--bs-blue", "#0d6efd", "a custom property inherits");
    }
    {
        // ...and a nearer definition wins, which is how a component overrides a theme.
        fixture f;
        f.load("<html><body><button id=b class=btn></button></body></html>",
               ":root { --x: #010101 } .btn { --x: #020202 }");
        expect_value(f, f.find_id("b"), "--x", "#020202", "the nearer definition wins");
    }
    {
        // The sharing invariant. Four <li> that declare nothing inherited must
        // share their parent's inherited half by pointer, not copy it, or a large
        // object would be duplicated per element and interning would cost memory
        // rather than save it.
        fixture f;
        f.load("<ul><li></li><li></li><li></li><li></li></ul>", "ul { color: #010101 }");
        const auto txn = f.doc.read();
        std::vector<node_id> items;
        const auto walk = [&](auto && self, node_id at) -> void {
            if (txn.tag(at).value_or(atom{}) == f.atoms.intern_lower("li")) { items.push_back(at); }
            for (const node_id c : txn.children(at)) { self(self, c); }
        };
        walk(walk, txn.root());
        CHECK(items.size() == 4);
        const auto first = f.style_of(items.front());
        CHECK(static_cast<bool>(first));
        for (const node_id item : items) {
            CHECK(f.style_of(item)->inherited == first->inherited); // Pointer equality.
        }
        // At most two inherited halves for the whole document: the root's and the
        // <ul>'s.
        std::printf("  4 <li> under a coloured <ul> -> %zu inherited halves\n",
                    f.styles.styles().distinct_inherited());
        CHECK(f.styles.styles().distinct_inherited() <= 2);
    }
}

// `inherit`, `initial`, `unset` and `revert` are resolved by the cascade and
// never reach layout as keyword strings (for example, `display: inherit` must
// not fall back to `block`).
void test_explicit_defaulting_keywords() {
    {
        fixture f;
        f.load("<div><p id=child></p></div>", "div { color: #010101 } p { color: inherit }");
        expect_value(f, f.find_id("child"), "color", "#010101", "inherit takes the parent's");
    }
    {
        // `inherit` works on a non-inherited property too - that is what makes it
        // different from `unset`.
        fixture f;
        f.load("<div><p id=child></p></div>",
               "div { background-color: #010101 } p { background-color: inherit }");
        expect_value(f, f.find_id("child"), "background-color", "#010101",
                     "inherit works on a non-inherited property");
    }
    {
        // `initial` must block inheritance, not fall through to it.
        fixture f;
        f.load("<div><p id=child></p></div>", "div { color: #010101 } p { color: initial }");
        CHECK(f.value_of(f.find_id("child"), "color").empty());
    }
    {
        // `unset` on an inherited property lets the inherited value through, which is
        // exactly what the spec says it does.
        fixture f;
        f.load("<div><p id=child></p></div>",
               "div { color: #010101 } p { color: #020202 } p { color: unset }");
        expect_value(f, f.find_id("child"), "color", "#010101", "unset falls back to inherited");
    }
    {
        // ...and on a non-inherited one it is the same as initial.
        fixture f;
        f.load("<p id=a></p>", "p { background-color: #020202 } p { background-color: unset }");
        CHECK(f.value_of(f.find_id("a"), "background-color").empty());
    }
    {
        // `display: inherit` must not become `block`. It is the value the parent has,
        // and an absent one reads as absent rather than as a wrong keyword.
        fixture f;
        f.load("<div><p id=child></p></div>", "div { display: inline } p { display: inherit }");
        expect_value(f, f.find_id("child"), "display", "inline", "display: inherit");
    }
    {
        // `revert` is the user-agent origin's answer, and `unset` where the UA
        // said nothing (CSS Cascade 4 §7.3, attr-css-wide-keywords).
        fixture f;
        f.load("<div><em id=a></em><em id=b></em></div>",
               "#a { font-style: revert; color: revert } #b { font-style: normal }"
               "div { color: #010101 }",
               "em { font-style: italic }");
        expect_value(f, f.find_id("a"), "font-style", "italic", "revert to the UA's italic");
        expect_value(f, f.find_id("a"), "color", "#010101", "revert with no UA value inherits");
        expect_value(f, f.find_id("b"), "font-style", "normal", "the author's value stands");
    }
}

// @media conditions are evaluated against the viewport, so only the matching
// breakpoints apply (with every block applied, Bootstrap's `.container` would
// take the widest breakpoint's max-width at every size).
void test_media_queries() {
    const auto at = [](fixture & f, float w, float h) {
        ctbrowser::style::css::media_environment env;
        env.viewport_width = w;
        env.viewport_height = h;
        (void)f.styles.set_environment(env);
        const auto txn = f.doc.read();
        f.resolved = f.styles.resolve_all(txn);
    };
    {
        // min-width: the rule applies at and above the breakpoint, and not below.
        fixture f;
        f.load("<p id=a></p>", "@media (min-width: 600px) { p { color: #010101 } }");
        at(f, 800, 600);
        expect_value(f, f.find_id("a"), "color", "#010101", "min-width above");
        at(f, 500, 600);
        CHECK(f.value_of(f.find_id("a"), "color").empty()); // Below.
        at(f, 600, 600);
        expect_value(f, f.find_id("a"), "color", "#010101", "min-width is inclusive");
    }
    {
        // max-width, and a max-width/min-width pair, which is how a breakpoint range
        // is written.
        fixture f;
        f.load("<p id=a></p>", "@media (max-width: 599.98px) { p { color: #010101 } }"
                               "@media (min-width: 600px) { p { color: #020202 } }");
        at(f, 500, 600);
        expect_value(f, f.find_id("a"), "color", "#010101", "the small breakpoint");
        at(f, 800, 600);
        expect_value(f, f.find_id("a"), "color", "#020202", "the large one, exclusively");
    }
    {
        // Bootstrap's shape: ascending mobile-first breakpoints, where only the
        // widest one that matches may win.
        fixture f;
        f.load("<p id=a></p>", "p { width: 100px }"
                               "@media (min-width: 576px) { p { width: 540px } }"
                               "@media (min-width: 768px) { p { width: 720px } }"
                               "@media (min-width: 1200px) { p { width: 1140px } }");
        at(f, 400, 600);
        expect_value(f, f.find_id("a"), "width", "100px", "below every breakpoint");
        at(f, 700, 600);
        expect_value(f, f.find_id("a"), "width", "540px", "the sm breakpoint");
        at(f, 1000, 600);
        expect_value(f, f.find_id("a"), "width", "720px", "the md one");
        at(f, 1400, 600);
        expect_value(f, f.find_id("a"), "width", "1140px", "and the xl one");
    }
    {
        // A media type. `print` must not apply on screen, and `screen` must.
        fixture f;
        f.load("<p id=a></p>", "@media print { p { color: #010101 } }"
                               "@media screen { p { background-color: #020202 } }");
        at(f, 800, 600);
        CHECK(f.value_of(f.find_id("a"), "color").empty());
        expect_value(f, f.find_id("a"), "background-color", "#020202", "screen applies");
    }
    {
        // `not`, which applies to the whole query rather than per feature.
        fixture f;
        f.load("<p id=a></p>", "@media not print { p { color: #010101 } }");
        at(f, 800, 600);
        expect_value(f, f.find_id("a"), "color", "#010101", "not print, on screen");
    }
    {
        // A comma list is an OR: either arm matching is enough.
        fixture f;
        f.load("<p id=a></p>",
               "@media (min-width: 2000px), (max-width: 900px) { p { color: #010101 } }");
        at(f, 800, 600);
        expect_value(f, f.find_id("a"), "color", "#010101", "the second arm matched");
        at(f, 1200, 600);
        CHECK(f.value_of(f.find_id("a"), "color").empty()); // Neither arm.
    }
    {
        // `and` is a conjunction, so both have to hold.
        fixture f;
        f.load("<p id=a></p>",
               "@media (min-width: 600px) and (max-width: 900px) { p { color: #010101 } }");
        at(f, 800, 600);
        expect_value(f, f.find_id("a"), "color", "#010101", "inside the range");
        at(f, 1000, 600);
        CHECK(f.value_of(f.find_id("a"), "color").empty()); // Above it.
    }
    {
        // Orientation, which is derived from the viewport rather than stored.
        fixture f;
        f.load("<p id=a></p>",
               "@media (orientation: portrait) { p { color: #010101 } }"
               "@media (orientation: landscape) { p { background-color: #020202 } }");
        at(f, 400, 800);
        expect_value(f, f.find_id("a"), "color", "#010101", "taller than wide");
        CHECK(f.value_of(f.find_id("a"), "background-color").empty());
        at(f, 800, 400);
        expect_value(f, f.find_id("a"), "background-color", "#020202", "wider than tall");
    }
    {
        // Nesting: the inner condition ANDs with the outer. Each rule's index of
        // its enclosing @media makes this work without flattening at parse time.
        fixture f;
        f.load("<p id=a></p>",
               "@media (min-width: 600px) { @media (max-width: 900px) { p { color: #010101 } } }");
        at(f, 800, 600);
        expect_value(f, f.find_id("a"), "color", "#010101", "both hold");
        at(f, 1000, 600);
        CHECK(f.value_of(f.find_id("a"), "color").empty()); // The inner fails.
        at(f, 500, 600);
        CHECK(f.value_of(f.find_id("a"), "color").empty()); // The outer fails.
    }
    {
        // An unmodelled feature makes the query false rather than being skipped.
        // Skipping would apply rules the author gated on something the engine does not
        // understand, which is the wrong direction to fail in.
        fixture f;
        f.load("<p id=a></p>", "@media (device-aspect-ratio: 16/9) { p { color: #010101 } }");
        at(f, 800, 600);
        CHECK(f.value_of(f.find_id("a"), "color").empty());
    }
    {
        // prefers-reduced-motion, which many of Bootstrap's blocks are gated on.
        fixture f;
        f.load(
            "<p id=a></p>",
            "@media (prefers-reduced-motion: reduce) { p { color: #010101 } }"
            "@media (prefers-reduced-motion: no-preference) { p { background-color: #020202 } }");
        at(f, 800, 600);
        CHECK(f.value_of(f.find_id("a"), "color").empty()); // Not reduced by default.
        expect_value(f, f.find_id("a"), "background-color", "#020202", "no-preference");
        ctbrowser::style::css::media_environment env;
        env.reduced_motion = true;
        CHECK(f.styles.set_environment(env)); // It reports that a query flipped.
        const auto txn = f.doc.read();
        f.resolved = f.styles.resolve_all(txn);
        expect_value(f, f.find_id("a"), "color", "#010101", "reduce, once asked for");
    }
    {
        // set_environment reports whether any query flipped, which lets a resize
        // skip the cascade. A page with no @media never re-resolves.
        fixture f;
        f.load("<p id=a></p>", "p { color: #010101 }");
        ctbrowser::style::css::media_environment env;
        env.viewport_width = 300;
        CHECK(!f.styles.set_environment(env)); // Nothing to flip.
    }
}

// @font-face `src` is read from tokens, not from reassembled text: an
// unquoted `url(f.ttf)` is a single url token whose text no longer contains
// `url(`, so a text search would drop every unquoted source silently. A quoted
// `url("f.ttf")` is a function token plus a string.
void test_font_face_sources() {
    const auto only_font = [](std::string_view css) -> std::string {
        ctbrowser::atom_table atoms;
        ctbrowser::style::engine styles{atoms};
        styles.add_sheet(css, 1);
        if (styles.page_fonts().size() != 1) { return "<none>"; }
        return styles.page_fonts().front().family + " <- " + styles.page_fonts().front().source;
    };

    CHECK_EQ(only_font("@font-face { font-family: Fira; src: url(f.ttf) }"),
             std::string{"Fira <- f.ttf"});
    CHECK_EQ(only_font("@font-face { font-family: \"Fira\"; src: url(\"f.ttf\") }"),
             std::string{"Fira <- f.ttf"});
    CHECK_EQ(only_font("@font-face { font-family: Fira; src: url('f.ttf') }"),
             std::string{"Fira <- f.ttf"});
    // `format()` rides along on essentially every real @font-face.
    CHECK_EQ(only_font("@font-face { font-family: Fira; src: url(f.ttf) format(\"truetype\") }"),
             std::string{"Fira <- f.ttf"});
    // Whitespace inside the parens belongs to the token but not to the file name.
    CHECK_EQ(only_font("@font-face { font-family: Fira; src: url( f.ttf ) }"),
             std::string{"Fira <- f.ttf"});
    // A list is about formats a browser might not support rather than about
    // different fonts, so the first url wins - and a leading local() is skipped
    // rather than taken as one.
    CHECK_EQ(only_font("@font-face { font-family: Fira; src: local(Fira), "
                       "url(f.woff2) format(\"woff2\"), url(f.ttf) }"),
             std::string{"Fira <- f.woff2"});
    // And a face this engine cannot load is not registered at all.
    CHECK_EQ(only_font("@font-face { font-family: Fira; src: local(Fira) }"),
             std::string{"<none>"});
}

// `ch` is the advance of `0` in the element's font when the shell injects a
// measurement, and half an em when nothing does (CSS Values 4 §6.1.1,
// line-break-ch-unit). The face is the element's own: family, weight (600 and
// up is bold) and style, over what it inherited; in `font-size` itself the
// `ch` is the parent's, like `em`.
void test_ch_measures_the_zero_glyph() {
    fixture f;
    // A stand-in backend: 0.5em for monospace, 0.25em otherwise, bold +0.25em
    // - ratios exact in a float, so the answers below are too.
    f.styles.set_text_measure(
        [](std::string_view text, float size, std::string_view family, bool bold, bool) {
            const float ratio = (family == "monospace" ? 0.5f : 0.25f) + (bold ? 0.25f : 0.0f);
            return static_cast<float>(text.size()) * size * ratio;
        });
    f.load("<div id=a><p id=b>x</p><i id=c>y</i></div>",
           "#a { font-family: monospace, serif; font-size: 10px; width: 10ch }"
           "#b { font-weight: 700; width: 10ch; font-size: 3ch }"
           "#c { font-family: 'Fira Sans'; padding-left: 4rch; width: 10ch }");
    expect_value(f, f.find_id("a"), "width", "50px", "10ch of a 10px monospace");
    // 3ch of the parent's face and size (15px), then 10ch of a bold 15px face.
    expect_value(f, f.find_id("b"), "font-size", "15px", "font-size: 3ch uses the parent");
    expect_value(f, f.find_id("b"), "width", "112.5px", "10ch of a bold 15px monospace");
    expect_value(f, f.find_id("c"), "width", "25px", "10ch of a proportional face");
    // The root's `0` advance for rch: the root is 16px in the default face.
    expect_value(f, f.find_id("c"), "padding-left", "16px", "4rch of the root's face");

    fixture bare;
    bare.load("<div>x</div>", "div { font-size: 10px; width: 10ch }");
    expect_value(bare, bare.find("div"), "width", "50px", "10ch with nothing measuring");
}

} // namespace

int main() {
    test_ch_measures_the_zero_glyph();
    test_specificity_and_source_order();
    test_author_beats_user_agent();
    test_identical_styles_are_shared();
    test_inheritance();
    test_explicit_defaulting_keywords();
    test_media_queries();
    test_font_face_sources();
    test_inline_style_applies();
    test_inline_beats_any_selector();
    test_important_selector_beats_inline();
    test_important_inline_beats_everything();
    test_inline_style_oddities();
    test_inline_style_is_per_element();
    REPORT("style_cascade");
}
