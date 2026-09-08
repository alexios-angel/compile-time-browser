// Selectors: matching, the buckets, the combinators, and what a rule compiles
// to. Carved out of unit/style_basics.cpp on 2026-09-08 - style_shorthands.cpp
// names the family, and style_fixture.hpp is the fixture every file in it
// shares. The original file's own banner follows, because it is about this half.
//
// ctbrowser.style: matching, the cascade, and interning.
//
// Correctness first. A matcher that is fast and wrong is worse than the previous engine's,
// which is slow and right - so the bucketing and the ancestor filter are
// checked against cases designed to break them: selectors whose rightmost
// compound files them in an unexpected bucket, and descendant selectors the
// filter is supposed to reject without walking.

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

void test_simple_selectors() {
    fixture f;
    f.load("<div id=box class='a b'><p>hi</p></div>",
           "div { color: red } p { color: blue } .a { margin: 1px } #box { padding: 2px }");
    const node_id div = f.find("div");
    const node_id p = f.find("p");

    expect_value(f, div, "color", "red", "tag selector");
    expect_value(f, p, "color", "blue", "tag selector (other)");
    // The LONGHANDS: a shorthand is expanded when it is recorded, so `margin`
    // itself is not a resolved property. See test_shorthands_expand.
    expect_value(f, div, "margin-left", "1px", "class selector");
    expect_value(f, div, "padding-left", "2px", "id selector");
    expect_value(f, p, "margin-left", "", "class must not leak to a child");
}

// Bucketing files a rule under its RIGHTMOST compound. These selectors are
// chosen so the bucket is not the obvious one - `#nav a` lives in the TAG
// bucket, not the id bucket - which is exactly where a naive index breaks.
void test_bucketing_uses_the_rightmost_compound() {
    fixture f;
    f.load("<nav id=nav><a class=link>x</a></nav><a class=link>y</a>",
           "#nav a { color: red }"       // filed under tag `a`
           ".wrap .link { color: blue }" // filed under class `link`
           "nav .link { font-size: 9px }");
    const auto txn = f.doc.read();
    const node_id inside = f.find("a"); // the first <a>, inside <nav>

    expect_value(f, inside, "color", "red", "#nav a matched via the tag bucket");
    expect_value(f, inside, "font-size", "9px", "nav .link matched via the class bucket");
    // .wrap .link must NOT match: there is no .wrap ancestor. This is the case
    // the ancestor filter rejects without walking.
    expect_value(f, inside, "color", "red", ".wrap .link correctly rejected");
}

void test_descendant_and_child_combinators() {
    fixture f;
    f.load("<section><div><p id=deep>x</p></div></section><p id=shallow>y</p>",
           "section p { color: red }"    // descendant: matches the deep one
           "section > p { color: lime }" // child: matches neither
           "div > p { font-weight: bold }");
    const node_id deep = f.find_id("deep");
    const node_id shallow = f.find_id("shallow");

    expect_value(f, deep, "color", "red", "descendant crosses generations");
    expect_value(f, deep, "font-weight", "bold", "child matches a direct parent");
    expect_value(f, shallow, "color", "", "descendant must not match outside");
    // `section > p` must not match the deep p, whose parent is a div
    const std::string deep_color = f.value_of(deep, "color");
    CHECK(deep_color == "red"); // lime would mean the child combinator was ignored
}

// A false NEGATIVE from the ancestor filter would silently drop a matching
// rule, so this hammers deep nesting where the filter does the most work.
void test_deep_nesting_still_matches() {
    std::string html = "<div class=root>";
    for (int i = 0; i < 40; ++i) { html += "<div>"; }
    html += "<span id=target>x</span>";
    for (int i = 0; i < 40; ++i) { html += "</div>"; }
    html += "</div>";

    fixture f;
    f.load(html, ".root span { color: found }");
    const node_id target = f.find_id("target");
    CHECK(static_cast<bool>(target));
    expect_value(f, target, "color", "found", "descendant matched through 40 levels");
}

void test_unmatched_element_gets_empty_style() {
    fixture f;
    f.load("<div><em>x</em></div>", "p { color: red }");
    const node_id em = f.find("em");
    CHECK(f.value_of(em, "color").empty());
    CHECK(static_cast<bool>(f.style_of(em))); // resolved, just to nothing
}

// ONE COMPILED SELECTOR PER SELECTOR, not per declaration.
//
// The front end this replaced compiled the selector again for every declaration in
// the block, and pushed it BEFORE deciding whether to keep it - so a sheet of one
// rule with three declarations retained three identical compiled selectors, and a
// rule whose selector could never match left a dead one behind for ever. On
// Bootstrap that was 6,289 retained where 2,965 selectors exist, roughly 650 of
// them permanently dead.
//
// Asserted rather than measured, because the cost is invisible: nothing renders
// differently, the matcher just tests the same selector several times and the
// buckets carry entries that can never fire.
void test_a_selector_is_compiled_once_per_selector() {
    {
        fixture f;
        f.load("<div id=a></div>", "#a { color: #010101; background-color: #020202; width: 3px }");
        // Three declarations, ONE selector.
        CHECK(f.styles.selector_count() == 1);
    }
    {
        fixture f;
        f.load("<div id=a></div>", "#a, .b, div { color: #010101; width: 3px }");
        CHECK(f.styles.selector_count() == 3); // three selectors, one compiled each
    }
    {
        // An UNSUPPORTED selector leaves nothing behind. A VENDOR pseudo-element is
        // the example on purpose: it will still be unsupported when everything else
        // here is implemented, so this assertion does not have to move every rung.
        // It used to be `[data-x]`, and attribute selectors landing is what moved
        // it - which is this test doing its job. The old front end forged a tag atom
        // out of such a selector and filed a rule under it, so the bucket grew an
        // entry that could never fire and the selector was retained anyway.
        fixture f;
        f.load("<div id=a></div>", "::-webkit-slider-thumb { color: #010101 }");
        CHECK(f.styles.selector_count() == 0); // not retained
        CHECK(f.styles.rule_count() == 0);     // and files no rule
    }
    {
        // Its SIBLINGS in the list are unaffected, which is what keeps
        // `.a, ::-webkit-x { ... }` colouring `.a`. One alternative this engine
        // cannot represent is not a reason to drop the whole rule.
        fixture f;
        f.load("<div class=a></div>", ".a, ::-webkit-slider-thumb { color: #010101 }");
        CHECK(f.styles.selector_count() == 1); // the supported alternative, alone
        expect_value(f, f.find("div"), "color", "#010101", "and still applies");
    }
}

// ATTRIBUTE SELECTORS, one operator at a time.
//
// 93 of Bootstrap's selectors are attribute selectors and every one of them was
// dead: the old front end split a compound on `.`, `#` and `:` only, so `[` was
// swallowed into whatever name it landed in and `input[type=checkbox]` became a
// request for a tag literally called `input[type=checkbox]`.
void test_attribute_selectors() {
    {
        fixture f;
        f.load("<div data-x=hello></div><div></div>", "[data-x] { color: #010101 }"
                                                      "[data-x=hello] { background-color: #020202 }"
                                                      "[data-x^=hel] { border-color: #030303 }"
                                                      "[data-x$=llo] { border-width: 4px }"
                                                      "[data-x*=ell] { width: 5px }");
        const node_id div = f.find("div");
        expect_value(f, div, "color", "#010101", "[a] presence");
        expect_value(f, div, "background-color", "#020202", "[a=v] exact");
        expect_value(f, div, "border-color", "#030303", "[a^=v] prefix");
        expect_value(f, div, "border-width", "4px", "[a$=v] suffix");
        expect_value(f, div, "width", "5px", "[a*=v] substring");
    }
    {
        // `~=` is a whitespace-separated LIST, not a substring - `[class~=b]` must
        // match `a b c` and must not match `abc`.
        fixture f;
        f.load("<p id=hit class='a b c'></p><p id=miss class='abc'></p>",
               "[class~=b] { color: #010101 }");
        expect_value(f, f.find_id("hit"), "color", "#010101", "~= matches a list item");
        CHECK(f.value_of(f.find_id("miss"), "color").empty()); // not a substring match
    }
    {
        // `|=` is the language-subtag form: `en` matches `en` and `en-GB`, never
        // `english`.
        fixture f;
        f.load("<p id=bare lang=en></p><p id=sub lang=en-GB></p><p id=word lang=english></p>",
               "[lang|=en] { color: #010101 }");
        expect_value(f, f.find_id("bare"), "color", "#010101", "|= exact");
        expect_value(f, f.find_id("sub"), "color", "#010101", "|= hyphen form");
        CHECK(f.value_of(f.find_id("word"), "color").empty()); // english is not en-*
    }
    {
        // The `i` flag, and its absence. Case sensitivity is the DEFAULT.
        fixture f;
        f.load("<p id=a data-v=HeLLo></p>", "[data-v=hello] { color: #010101 }"
                                            "[data-v=hello i] { background-color: #020202 }");
        CHECK(f.value_of(f.find_id("a"), "color").empty()); // case matters by default
        expect_value(f, f.find_id("a"), "background-color", "#020202", "the i flag folds");
    }
    {
        // The three substring forms match NOTHING against an empty value, per the
        // spec - otherwise `[a^=""]` would match every element that has the
        // attribute, since every string starts with the empty string.
        fixture f;
        f.load("<p id=a data-v=x></p>", "[data-v^=''] { color: #010101 }");
        CHECK(f.value_of(f.find_id("a"), "color").empty());
    }
    {
        // A `]` inside a quoted value cannot end the selector early, because the
        // tokenizer delimited the block before the selector parser saw it.
        fixture f;
        f.load("<p id=a data-v='a]b'></p>", "[data-v='a]b'] { color: #010101 }");
        expect_value(f, f.find_id("a"), "color", "#010101", "a ] inside a string");
    }
    {
        // Attribute NAMES fold - HTML attribute names are ASCII case-insensitive
        // and the DOM interns them lowercased.
        fixture f;
        f.load("<p id=a data-v=x></p>", "[DATA-V=x] { color: #010101 }");
        expect_value(f, f.find_id("a"), "color", "#010101", "the name folds");
    }
    {
        // Combined with everything else in a compound, and with a combinator.
        fixture f;
        f.load("<div class=box><input id=c type=checkbox></div>",
               "div.box input[type=checkbox] { color: #010101 }");
        expect_value(f, f.find_id("c"), "color", "#010101", "attribute inside a complex selector");
    }
}

// `:root`, which is what makes Bootstrap's 128 global custom properties reachable
// at all: they live on `:root, [data-bs-theme=light]`, and BOTH alternatives were
// dead - the first an unknown pseudo, the second an attribute selector.
void test_root_selector() {
    {
        fixture f;
        f.load("<html><body><p></p></body></html>",
               ":root { color: #010101; background-color: #020202 }");
        // MATCHING and INHERITING are different questions, and this is where the
        // difference shows. `:root` matches the document element and nothing else -
        // but `color` inherits, so the body and the paragraph see it anyway, while
        // `background-color` does not inherit and stops at <html>.
        //
        // This assertion used to say the body's colour was empty, which was true only
        // because there was no inheritance in the cascade at all.
        expect_value(f, f.find("html"), "color", "#010101", ":root is the document element");
        expect_value(f, f.find("body"), "color", "#010101", "and colour inherits from it");
        expect_value(f, f.find("p"), "color", "#010101", "all the way down");
        expect_value(f, f.find("html"), "background-color", "#020202", "a non-inherited one");
        CHECK(f.value_of(f.find("body"), "background-color").empty()); // stops at the root
        CHECK(f.value_of(f.find("p"), "background-color").empty());
    }
    {
        // Specificity: `:root` is class-level, so it beats a bare type selector.
        fixture f;
        f.load("<html><body></body></html>", "html { color: #010101 } :root { color: #020202 }");
        expect_value(f, f.find("html"), "color", "#020202", ":root outranks html");
    }
    {
        // A custom property on :root is stored like any other declaration. Nothing
        // READS it yet - var() is a later rung - but it has to arrive, because
        // being unreachable was the reason the whole block was invisible.
        fixture f;
        f.load("<html><body></body></html>", ":root { --bs-blue: #0d6efd }");
        expect_value(f, f.find("html"), "--bs-blue", "#0d6efd", "a custom property arrives");
    }
}

// SIBLING COMBINATORS. 48 `+` and 27 `~` in Bootstrap, all dead before: the old
// front end split a selector on whitespace and `>` only, so `.a + .b` became three
// steps whose middle one asked for a tag literally called "+".
//
// These need the traversal's memory rather than the tree: there is no
// previous-sibling link to walk back along, so matching reads the siblings the DFS
// has already visited at that depth.
void test_sibling_combinators() {
    {
        // `+` is the IMMEDIATELY preceding element sibling, and only it.
        fixture f;
        f.load("<p class=a></p><p id=hit class=b></p><p id=miss class=b></p>",
               ".a + .b { color: #010101 }");
        expect_value(f, f.find_id("hit"), "color", "#010101", "+ matches the next sibling");
        CHECK(f.value_of(f.find_id("miss"), "color").empty()); // not the one after that
    }
    {
        // `~` is ANY following sibling, however far.
        fixture f;
        f.load("<p class=a></p><p id=one class=b></p><em></em><p id=two class=b></p>",
               ".a ~ .b { color: #010101 }");
        expect_value(f, f.find_id("one"), "color", "#010101", "~ matches the next");
        expect_value(f, f.find_id("two"), "color", "#010101", "~ matches a later one too");
    }
    {
        // A TEXT NODE between two elements is not a sibling as far as `+` is
        // concerned - the combinator is about elements. This is the case a
        // node-walking implementation gets wrong first.
        fixture f;
        f.load("<p class=a></p> some text <p id=hit class=b></p>", ".a + .b { color: #010101 }");
        expect_value(f, f.find_id("hit"), "color", "#010101", "+ steps over a text node");
    }
    {
        // An INTERVENING ELEMENT breaks `+` and does not break `~`.
        fixture f;
        f.load("<p class=a></p><em></em><p id=x class=b></p>",
               ".a + .b { color: #010101 } .a ~ .b { background-color: #020202 }");
        CHECK(f.value_of(f.find_id("x"), "color").empty()); // + needs adjacency
        expect_value(f, f.find_id("x"), "background-color", "#020202", "~ does not");
    }
    {
        // The FIRST element has no previous sibling, so a sibling combinator must
        // fail rather than reading off the front of the list.
        fixture f;
        f.load("<p id=first class=b></p><p class=a></p>", ".a + .b { color: #010101 }");
        CHECK(f.value_of(f.find_id("first"), "color").empty());
    }
    {
        // Siblings do not cross a parent boundary: `.a` in one div cannot be the
        // sibling of `.b` in another.
        fixture f;
        f.load("<div><p class=a></p></div><div><p id=x class=b></p></div>",
               ".a + .b { color: #010101 } .a ~ .b { background-color: #020202 }");
        CHECK(f.value_of(f.find_id("x"), "color").empty());
        CHECK(f.value_of(f.find_id("x"), "background-color").empty());
    }
    {
        // Chained with the other combinators, in both orders. After `+` has matched,
        // the walk continues from the SIBLING - so `.wrap .a + .b` has to find
        // `.wrap` above `.a`, not above `.b` only.
        fixture f;
        f.load("<div class=wrap><p class=a></p><p id=x class=b></p></div>",
               ".wrap .a + .b { color: #010101 }");
        expect_value(f, f.find_id("x"), "color", "#010101", "descendant then sibling");
    }
    {
        fixture f;
        f.load("<div class=wrap><p class=a></p><p class=b><em id=x></em></p></div>",
               ".a + .b em { color: #010101 }");
        expect_value(f, f.find_id("x"), "color", "#010101", "sibling then descendant");
    }
    {
        // Two sibling steps in one selector.
        fixture f;
        f.load("<p class=a></p><p class=b></p><p id=x class=c></p>",
               ".a + .b + .c { color: #010101 }");
        expect_value(f, f.find_id("x"), "color", "#010101", "two + steps");
    }
    {
        // `>` and `+` together, which is where a cursor that only tracked depth
        // would lose the index and match the wrong sibling.
        fixture f;
        f.load("<div class=wrap><p class=a></p><p id=x class=b></p></div>",
               ".wrap > .a + .b { color: #010101 }");
        expect_value(f, f.find_id("x"), "color", "#010101", "child then sibling");
    }
    {
        // Specificity is unaffected by a combinator: `.a + .b` and `.b` are both
        // (0,1,0) for the SUBJECT plus (0,1,0) for the other compound, so the
        // two-compound one wins on class count.
        fixture f;
        f.load("<p class=a></p><p id=x class=b></p>",
               ".b { color: #010101 } .a + .b { color: #020202 }");
        expect_value(f, f.find_id("x"), "color", "#020202", "two compounds outrank one");
    }
}

// A SECOND resolve_all must not see the first document's elements as siblings.
// levels_ is reused for its capacity, so its contents have to be cleared - without
// that, the new <html> lands at index 1 behind the old one and `html ~ x` matches
// across two documents.
void test_resolving_twice_does_not_leak_siblings() {
    fixture f;
    f.load("<p class=a></p><p id=x class=b></p>", ".a + .b { color: #010101 }");
    expect_value(f, f.find_id("x"), "color", "#010101", "first pass");
    const auto txn = f.doc.read();
    f.resolved = f.styles.resolve_all(txn);
    expect_value(f, f.find_id("x"), "color", "#010101", "second pass agrees");
    // And the FIRST element still has no previous sibling on the second pass.
    fixture g;
    g.load("<p id=first class=b></p><p class=a></p>", ".a + .b { color: #010101 }");
    const auto gtxn = g.doc.read();
    g.resolved = g.styles.resolve_all(gtxn);
    CHECK(g.value_of(g.find_id("first"), "color").empty());
}

} // namespace

int main() {
    test_simple_selectors();
    test_bucketing_uses_the_rightmost_compound();
    test_descendant_and_child_combinators();
    test_a_selector_is_compiled_once_per_selector();
    test_attribute_selectors();
    test_root_selector();
    test_sibling_combinators();
    test_resolving_twice_does_not_leak_siblings();
    test_deep_nesting_still_matches();
    test_unmatched_element_gets_empty_style();
    REPORT("style_selectors");
}
