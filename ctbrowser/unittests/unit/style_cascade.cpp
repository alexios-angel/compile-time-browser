// The cascade: specificity and source order, the origins, the style attribute
// and !important, inheritance and the explicit defaulting keywords, @media and
// @font-face. Carved out of unit/style_basics.cpp on 2026-09-08 -
// style_shorthands.cpp names the family, and style_fixture.hpp is the fixture
// every file in it shares.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "style_fixture.hpp"

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
           "div { color: author }", // author, low specificity
           "#x { color: ua }");     // UA, high specificity
    expect_value(f, f.find("div"), "color", "author", "author origin outranks UA specificity");
}

// The point of interning: elements that resolve identically must SHARE one
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
    CHECK(all_shared); // pointer equality, not just value equality

    // and the table really only holds a few distinct styles for this document
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
    // THIS is the step that is easy to get wrong: appending the style
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
    // The parse is cached by attribute TEXT, so this is also the check that the
    // cache is not handing every element the first one it saw.
    expect_value(f, f.find_id("a"), "color", "red", "the first element");
    expect_value(f, f.find_id("b"), "color", "blue", "the second");
    expect_value(f, f.find_id("c"), "color", "black", "and one with no attribute");
}

// INHERITANCE AS A CASCADE STAGE, and the interning that has to survive it.
//
// Before this, `resolve` produced only the declarations that MATCHED and inheritance
// was done five separate ad-hoc ways downstream - font-size, the face, text
// decoration and white-space threaded as parameters through box_builder, and `color`
// threaded through the recorder. Every one of them was a different mechanism for the
// same idea, and none of them was reachable from the cascade.
void test_inheritance() {
    {
        fixture f;
        f.load("<div><p><em id=deep></em></p></div>", "div { color: #010101 }");
        expect_value(f, f.find_id("deep"), "color", "#010101", "inherits through two levels");
    }
    {
        // A NON-inherited property does not travel.
        fixture f;
        f.load("<div><p id=child></p></div>", "div { background-color: #010101 }");
        CHECK(f.value_of(f.find_id("child"), "background-color").empty());
    }
    {
        // An element's OWN declaration beats what came down to it, whatever their
        // specificities: this is `own` before `inherited` in get(), not the cascade.
        fixture f;
        f.load("<div><p id=child></p></div>",
               "#nothing { color: #030303 } div { color: #010101 } p { color: #020202 }");
        expect_value(f, f.find_id("child"), "color", "#020202", "own beats inherited");
    }
    {
        // CUSTOM PROPERTIES INHERIT, which is the entire point of this rung: it is
        // what makes Bootstrap's 128 `--bs-*` on `:root` readable from a button.
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
        // THE SHARING INVARIANT. Four <li> that declare nothing inherited must share
        // their parent's inherited half BY POINTER - not copy it - or a 128-entry
        // object would be duplicated per element and the interning would be a
        // regression rather than an optimisation.
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
            CHECK(f.style_of(item)->inherited == first->inherited); // pointer equality
        }
        // One inheritance CONTEXT for the whole document: the root's, and the <ul>'s.
        std::printf("  4 <li> under a coloured <ul> -> %zu inherited halves\n",
                    f.styles.styles().distinct_inherited());
        CHECK(f.styles.styles().distinct_inherited() <= 2);
    }
}

// `inherit`, `initial`, `unset` and `revert`. They used to reach layout as the
// literal strings, and `display: inherit` silently became `block` because
// parse_display mapped everything it did not recognise to that.
void test_explicit_defaulting_keywords() {
    {
        fixture f;
        f.load("<div><p id=child></p></div>", "div { color: #010101 } p { color: inherit }");
        expect_value(f, f.find_id("child"), "color", "#010101", "inherit takes the parent's");
    }
    {
        // `inherit` works on a NON-inherited property too - that is what makes it
        // different from `unset`.
        fixture f;
        f.load("<div><p id=child></p></div>",
               "div { background-color: #010101 } p { background-color: inherit }");
        expect_value(f, f.find_id("child"), "background-color", "#010101",
                     "inherit works on a non-inherited property");
    }
    {
        // `initial` must BLOCK inheritance, not fall through to it.
        fixture f;
        f.load("<div><p id=child></p></div>", "div { color: #010101 } p { color: initial }");
        CHECK(f.value_of(f.find_id("child"), "color").empty());
    }
    {
        // `unset` on an INHERITED property lets the inherited value through, which is
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
}

// @media, EVALUATED. Every block used to flatten in unconditionally - the prelude was
// substring-matched for `print` and `portrait` and nothing else - so all of Bootstrap's
// breakpoints applied at once and the last in source order won. `.container` therefore
// took the xxl breakpoint's max-width at every viewport, which clamps nothing.
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
        CHECK(f.value_of(f.find_id("a"), "color").empty()); // below
        at(f, 600, 600);
        expect_value(f, f.find_id("a"), "color", "#010101", "min-width is inclusive");
    }
    {
        // max-width, and the pair of them together - which is how a breakpoint RANGE is
        // written and the case that flattening got most wrong.
        fixture f;
        f.load("<p id=a></p>", "@media (max-width: 599.98px) { p { color: #010101 } }"
                               "@media (min-width: 600px) { p { color: #020202 } }");
        at(f, 500, 600);
        expect_value(f, f.find_id("a"), "color", "#010101", "the small breakpoint");
        at(f, 800, 600);
        expect_value(f, f.find_id("a"), "color", "#020202", "the large one, exclusively");
    }
    {
        // The BOOTSTRAP SHAPE: ascending mobile-first breakpoints, where flattening made
        // the widest one win at every size.
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
        // A media TYPE. `print` must not apply on screen, and `screen` must.
        fixture f;
        f.load("<p id=a></p>", "@media print { p { color: #010101 } }"
                               "@media screen { p { background-color: #020202 } }");
        at(f, 800, 600);
        CHECK(f.value_of(f.find_id("a"), "color").empty());
        expect_value(f, f.find_id("a"), "background-color", "#020202", "screen applies");
    }
    {
        // `not`, which applies to the WHOLE query rather than per feature.
        fixture f;
        f.load("<p id=a></p>", "@media not print { p { color: #010101 } }");
        at(f, 800, 600);
        expect_value(f, f.find_id("a"), "color", "#010101", "not print, on screen");
    }
    {
        // A COMMA LIST is an OR: either arm matching is enough.
        fixture f;
        f.load("<p id=a></p>",
               "@media (min-width: 2000px), (max-width: 900px) { p { color: #010101 } }");
        at(f, 800, 600);
        expect_value(f, f.find_id("a"), "color", "#010101", "the second arm matched");
        at(f, 1200, 600);
        CHECK(f.value_of(f.find_id("a"), "color").empty()); // neither arm
    }
    {
        // `and` is a conjunction, so BOTH have to hold.
        fixture f;
        f.load("<p id=a></p>",
               "@media (min-width: 600px) and (max-width: 900px) { p { color: #010101 } }");
        at(f, 800, 600);
        expect_value(f, f.find_id("a"), "color", "#010101", "inside the range");
        at(f, 1000, 600);
        CHECK(f.value_of(f.find_id("a"), "color").empty()); // above it
    }
    {
        // orientation, which is DERIVED from the viewport rather than stored.
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
        // NESTING: the inner condition ANDs with the outer, which the parent index is
        // what makes work without flattening the tree at parse time.
        fixture f;
        f.load("<p id=a></p>",
               "@media (min-width: 600px) { @media (max-width: 900px) { p { color: #010101 } } }");
        at(f, 800, 600);
        expect_value(f, f.find_id("a"), "color", "#010101", "both hold");
        at(f, 1000, 600);
        CHECK(f.value_of(f.find_id("a"), "color").empty()); // the inner fails
        at(f, 500, 600);
        CHECK(f.value_of(f.find_id("a"), "color").empty()); // the outer fails
    }
    {
        // AN UNMODELLED FEATURE makes the query FALSE rather than being skipped.
        // Skipping would apply rules the author gated on something the engine does not
        // understand, which is the wrong direction to fail in.
        fixture f;
        f.load("<p id=a></p>", "@media (device-aspect-ratio: 16/9) { p { color: #010101 } }");
        at(f, 800, 600);
        CHECK(f.value_of(f.find_id("a"), "color").empty());
    }
    {
        // prefers-reduced-motion, which 26 of Bootstrap's blocks are gated on.
        fixture f;
        f.load(
            "<p id=a></p>",
            "@media (prefers-reduced-motion: reduce) { p { color: #010101 } }"
            "@media (prefers-reduced-motion: no-preference) { p { background-color: #020202 } }");
        at(f, 800, 600);
        CHECK(f.value_of(f.find_id("a"), "color").empty()); // not reduced by default
        expect_value(f, f.find_id("a"), "background-color", "#020202", "no-preference");
        ctbrowser::style::css::media_environment env;
        env.reduced_motion = true;
        CHECK(f.styles.set_environment(env)); // and it reports that truth FLIPPED
        const auto txn = f.doc.read();
        f.resolved = f.styles.resolve_all(txn);
        expect_value(f, f.find_id("a"), "color", "#010101", "reduce, once asked for");
    }
    {
        // set_environment reports whether anything MOVED, which is what lets a resize
        // skip the cascade. A page with no @media never re-resolves.
        fixture f;
        f.load("<p id=a></p>", "p { color: #010101 }");
        ctbrowser::style::css::media_environment env;
        env.viewport_width = 300;
        CHECK(!f.styles.set_environment(env)); // nothing to flip
    }
}

// A PAGE FONT IS FOUND BY ITS TOKENS, NOT BY SCANNING ITS TEXT.
//
// `url(` with an UNQUOTED body is its own token and a quoted one is a function
// plus a string - so by the time a declaration is reassembled into text, the
// two spellings share nothing to recognise: `url("f.ttf")` keeps its `url(`
// and `url(f.ttf)` comes back as bare `f.ttf`.
//
// The engine used to search that text for a literal `url(`, so EVERY UNQUOTED
// src produced an empty source and the face was dropped - silently, and for
// the spelling most stylesheets use. The symptom was a web font that simply
// never loaded and no diagnostic anywhere.
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
    // A LIST is about formats a browser might not support rather than about
    // different fonts, so the first url wins - and a leading local() is skipped
    // rather than taken as one.
    CHECK_EQ(only_font("@font-face { font-family: Fira; src: local(Fira), "
                       "url(f.woff2) format(\"woff2\"), url(f.ttf) }"),
             std::string{"Fira <- f.woff2"});
    // And a face this engine cannot load is not registered at all.
    CHECK_EQ(only_font("@font-face { font-family: Fira; src: local(Fira) }"),
             std::string{"<none>"});
}

} // namespace

int main() {
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
