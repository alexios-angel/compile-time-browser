//===- selectors.cpp - Selector matching tests ------------------*- C++ -*-===//
//
// Selector matching: rule buckets, combinators, attribute and namespace
// selectors, pseudo-elements, and what a rule compiles to. fixture.hpp is the
// fixture shared by every file in unittests/unit/style/.
//
// Correctness first: the bucketing and the ancestor filter are checked
// against cases built to break them, such as selectors whose rightmost
// compound files them in an unexpected bucket and descendant selectors the
// filter must reject without walking.
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

void test_simple_selectors() {
    fixture f;
    f.load("<div id=box class='a b'><p>hi</p></div>",
           "div { color: red } p { color: blue } .a { margin: 1px } #box { padding: 2px }");
    const node_id div = f.find("div");
    const node_id p = f.find("p");

    expect_value(f, div, "color", "red", "tag selector");
    expect_value(f, p, "color", "blue", "tag selector (other)");
    // Check longhands: a shorthand is expanded when it is recorded, so `margin`
    // itself is not a resolved property. See test_shorthands_expand in
    // shorthands.cpp.
    expect_value(f, div, "margin-left", "1px", "class selector");
    expect_value(f, div, "padding-left", "2px", "id selector");
    expect_value(f, p, "margin-left", "", "class must not leak to a child");
}

// Bucketing files a rule under its rightmost compound. These selectors are
// chosen so the bucket is not the obvious one - `#nav a` lives in the tag
// bucket, not the id bucket - which is exactly where a naive index breaks.
void test_bucketing_uses_the_rightmost_compound() {
    fixture f;
    f.load("<nav id=nav><a class=link>x</a></nav><a class=link>y</a>",
           "#nav a { color: red }"       // Filed under tag `a`.
           ".wrap .link { color: blue }" // Filed under class `link`.
           "nav .link { font-size: 9px }");
    const auto txn = f.doc.read();
    const node_id inside = f.find("a"); // The first <a>, inside <nav>.

    expect_value(f, inside, "color", "red", "#nav a matched via the tag bucket");
    expect_value(f, inside, "font-size", "9px", "nav .link matched via the class bucket");
    // `.wrap .link` must not match (there is no .wrap ancestor), so the colour
    // stays red from `#nav a` rather than blue. The ancestor filter rejects this
    // without walking.
    expect_value(f, inside, "color", "red", ".wrap .link correctly rejected");
}

void test_descendant_and_child_combinators() {
    fixture f;
    f.load("<section><div><p id=deep>x</p></div></section><p id=shallow>y</p>",
           "section p { color: red }"    // Descendant: matches the deep one.
           "section > p { color: lime }" // Child: matches neither.
           "div > p { font-weight: bold }");
    const node_id deep = f.find_id("deep");
    const node_id shallow = f.find_id("shallow");

    expect_value(f, deep, "color", "red", "descendant crosses generations");
    expect_value(f, deep, "font-weight", "bold", "child matches a direct parent");
    expect_value(f, shallow, "color", "", "descendant must not match outside");
    // `section > p` must not match the deep p, whose parent is a div.
    const std::string deep_color = f.value_of(deep, "color");
    CHECK(deep_color == "red"); // Lime would mean the child combinator was ignored.
}

// A false negative from the ancestor filter would silently drop a matching
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
    CHECK(static_cast<bool>(f.style_of(em))); // Resolved, just to nothing.
}

// One compiled selector per selector, not per declaration, and none for a
// selector that can never match. Extra copies change no rendering but make
// the matcher test the same selector repeatedly, so the count is asserted.
void test_a_selector_is_compiled_once_per_selector() {
    {
        fixture f;
        f.load("<div id=a></div>", "#a { color: #010101; background-color: #020202; width: 3px }");
        // Three declarations, one selector.
        CHECK(f.styles.selector_count() == 1);
    }
    {
        fixture f;
        f.load("<div id=a></div>", "#a, .b, div { color: #010101; width: 3px }");
        CHECK(f.styles.selector_count() == 3); // Three selectors, one compiled each.
    }
    {
        // An unsupported selector leaves nothing behind: no compiled selector and
        // no bucket entry. A vendor pseudo-element is used because it will stay
        // unsupported as other selector features are implemented.
        fixture f;
        f.load("<div id=a></div>", "::-webkit-slider-thumb { color: #010101 }");
        CHECK(f.styles.selector_count() == 0); // Not retained,
        CHECK(f.styles.rule_count() == 0);     // and files no rule.
    }
    {
        // Its siblings in the list are unaffected, which is what keeps
        // `.a, ::-webkit-x { ... }` colouring `.a`. One alternative this engine
        // cannot represent is not a reason to drop the whole rule.
        fixture f;
        f.load("<div class=a></div>", ".a, ::-webkit-slider-thumb { color: #010101 }");
        CHECK(f.styles.selector_count() == 1); // The supported alternative, alone.
        expect_value(f, f.find("div"), "color", "#010101", "and still applies");
    }
}

// Attribute selectors, one operator at a time. A compound must split on `[`
// as well as `.`, `#` and `:`, or `input[type=checkbox]` is read as a tag
// literally named `input[type=checkbox]`.
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
        // `~=` is a whitespace-separated list, not a substring - `[class~=b]` must
        // match `a b c` and must not match `abc`.
        fixture f;
        f.load("<p id=hit class='a b c'></p><p id=miss class='abc'></p>",
               "[class~=b] { color: #010101 }");
        expect_value(f, f.find_id("hit"), "color", "#010101", "~= matches a list item");
        CHECK(f.value_of(f.find_id("miss"), "color").empty()); // Not a substring match.
    }
    {
        // `|=` is the language-subtag form: `en` matches `en` and `en-GB`, never
        // `english`.
        fixture f;
        f.load("<p id=bare lang=en></p><p id=sub lang=en-GB></p><p id=word lang=english></p>",
               "[lang|=en] { color: #010101 }");
        expect_value(f, f.find_id("bare"), "color", "#010101", "|= exact");
        expect_value(f, f.find_id("sub"), "color", "#010101", "|= hyphen form");
        CHECK(f.value_of(f.find_id("word"), "color").empty()); // "english" is not en-*.
    }
    {
        // The `i` flag, and its absence. Case sensitivity is the default.
        fixture f;
        f.load("<p id=a data-v=HeLLo></p>", "[data-v=hello] { color: #010101 }"
                                            "[data-v=hello i] { background-color: #020202 }");
        CHECK(f.value_of(f.find_id("a"), "color").empty()); // Case matters by default.
        expect_value(f, f.find_id("a"), "background-color", "#020202", "the i flag folds");
    }
    {
        // The three substring forms match nothing against an empty value, per the
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
        // Attribute names fold - HTML attribute names are ASCII case-insensitive
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

// `:root`: Bootstrap declares its global custom properties on
// `:root, [data-bs-theme=light]`, so both alternatives must match.
void test_root_selector() {
    {
        fixture f;
        f.load("<html><body><p></p></body></html>",
               ":root { color: #010101; background-color: #020202 }");
        // Matching and inheriting are different: `:root` matches only the document
        // element, but `color` inherits so the body and paragraph see it, while
        // `background-color` does not inherit and stops at <html>.
        expect_value(f, f.find("html"), "color", "#010101", ":root is the document element");
        expect_value(f, f.find("body"), "color", "#010101", "and colour inherits from it");
        expect_value(f, f.find("p"), "color", "#010101", "all the way down");
        expect_value(f, f.find("html"), "background-color", "#020202", "a non-inherited one");
        CHECK(f.value_of(f.find("body"), "background-color").empty()); // Stops at the root.
        CHECK(f.value_of(f.find("p"), "background-color").empty());
    }
    {
        // Specificity: `:root` is class-level, so it beats a bare type selector.
        fixture f;
        f.load("<html><body></body></html>", "html { color: #010101 } :root { color: #020202 }");
        expect_value(f, f.find("html"), "color", "#020202", ":root outranks html");
    }
    {
        // A custom property on :root is stored like any other declaration, so
        // `var(--bs-blue)` anywhere below it has a value to substitute.
        fixture f;
        f.load("<html><body></body></html>", ":root { --bs-blue: #0d6efd }");
        expect_value(f, f.find("html"), "--bs-blue", "#0d6efd", "a custom property arrives");
    }
}

// Sibling combinators. There is no previous-sibling link in the tree, so
// matching reads the siblings the traversal has already visited at that depth.
void test_sibling_combinators() {
    {
        // `+` is the immediately preceding element sibling, and only it.
        fixture f;
        f.load("<p class=a></p><p id=hit class=b></p><p id=miss class=b></p>",
               ".a + .b { color: #010101 }");
        expect_value(f, f.find_id("hit"), "color", "#010101", "+ matches the next sibling");
        CHECK(f.value_of(f.find_id("miss"), "color").empty()); // Not the one after that.
    }
    {
        // `~` is any following sibling, however far.
        fixture f;
        f.load("<p class=a></p><p id=one class=b></p><em></em><p id=two class=b></p>",
               ".a ~ .b { color: #010101 }");
        expect_value(f, f.find_id("one"), "color", "#010101", "~ matches the next");
        expect_value(f, f.find_id("two"), "color", "#010101", "~ matches a later one too");
    }
    {
        // A text node between two elements is not a sibling as far as `+` is
        // concerned - the combinator is about elements. This is the case a
        // node-walking implementation gets wrong first.
        fixture f;
        f.load("<p class=a></p> some text <p id=hit class=b></p>", ".a + .b { color: #010101 }");
        expect_value(f, f.find_id("hit"), "color", "#010101", "+ steps over a text node");
    }
    {
        // An intervening element breaks `+` and does not break `~`.
        fixture f;
        f.load("<p class=a></p><em></em><p id=x class=b></p>",
               ".a + .b { color: #010101 } .a ~ .b { background-color: #020202 }");
        CHECK(f.value_of(f.find_id("x"), "color").empty()); // `+` needs adjacency.
        expect_value(f, f.find_id("x"), "background-color", "#020202", "~ does not");
    }
    {
        // The first element has no previous sibling, so a sibling combinator must
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
        // the walk continues from the sibling - so `.wrap .a + .b` has to find
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
        // A combinator adds no specificity, but each compound counts: `.a + .b` is
        // (0,2,0) and `.b` is (0,1,0), so the two-compound selector wins.
        fixture f;
        f.load("<p class=a></p><p id=x class=b></p>",
               ".b { color: #010101 } .a + .b { color: #020202 }");
        expect_value(f, f.find_id("x"), "color", "#020202", "two compounds outrank one");
    }
}

// A second resolve_all must not see the first pass's elements as siblings.
// style::engine's levels_ is reused for its capacity, so its contents have to
// be cleared; otherwise the new <html> lands at index 1 behind the old one and
// `html ~ x` matches across two passes.
void test_resolving_twice_does_not_leak_siblings() {
    fixture f;
    f.load("<p class=a></p><p id=x class=b></p>", ".a + .b { color: #010101 }");
    expect_value(f, f.find_id("x"), "color", "#010101", "first pass");
    const auto txn = f.doc.read();
    f.resolved = f.styles.resolve_all(txn);
    expect_value(f, f.find_id("x"), "color", "#010101", "second pass agrees");
    // And the first element still has no previous sibling on the second pass.
    fixture g;
    g.load("<p id=first class=b></p><p class=a></p>", ".a + .b { color: #010101 }");
    const auto gtxn = g.doc.read();
    g.resolved = g.styles.resolve_all(gtxn);
    CHECK(g.value_of(g.find_id("first"), "color").empty());
}

// A namespaced type selector matches by URI, and a default `@namespace` puts
// every unprefixed compound in it - the implied `*` of `.c` included (Selectors
// 4 §6.1.1). cssom/CSSStyleRule-set-selectorText-namespace asks both.
void test_namespaces() {
    fixture f;
    f.load("<p id=p class=c></p><svg id=s class=c></svg>",
           "@namespace url(http://www.w3.org/1999/xhtml);"
           "@namespace svg url(http://www.w3.org/2000/svg);"
           "svg|*.c { color: #010101 }"
           ".c { background-color: #020202 }"
           "*|*.c { outline-color: #030303 }"
           "|*.c { caret-color: #040404 }");
    expect_value(f, f.find_id("s"), "color", "#010101", "svg|* names the <svg>");
    expect_value(f, f.find_id("p"), "color", "", "...and not the <p>");
    expect_value(f, f.find_id("p"), "background-color", "#020202", ".c is xhtml|*.c");
    expect_value(f, f.find_id("s"), "background-color", "", "...so not the <svg>");
    expect_value(f, f.find_id("s"), "outline-color", "#030303", "*|* is any");
    expect_value(f, f.find_id("p"), "outline-color", "#030303", "*|* is any");
    expect_value(f, f.find_id("p"), "caret-color", "", "|* is the null namespace");
}

// getComputedStyle-pseudo: a `::before` rule matches no element, and
// resolve_pseudo cascades it for the element, inheriting from the element.
void test_pseudo_elements() {
    fixture f;
    f.load("<div id=a style=\"padding-top: 3px\"><p id=b></p></div>",
           "#a { color: #010101 } #a::before { content: \"x\"; width: 50%; color: #020202 }"
           "div:after { height: 2px } p::before { width: 1px }");
    const node_id a = f.find_id("a");
    expect_value(f, a, "width", "", "the ::before rule is not the element's");
    expect_value(f, a, "content", "", "nor its content");
    const auto txn = f.doc.read();
    const computed_style_ptr before =
        f.styles.resolve_pseudo(txn, a, f.atoms.intern("before"), f.style_of(a));
    CHECK(before);
    CHECK_EQ(std::string{before->get(f.atoms.intern("width"))}, std::string{"50%"});
    CHECK_EQ(std::string{before->get(f.atoms.intern("content"))}, std::string{"\"x\""});
    CHECK_EQ(std::string{before->get(f.atoms.intern("color"))}, std::string{"#020202"});
    // The element's style attribute is not the pseudo-element's.
    CHECK_EQ(std::string{before->get(f.atoms.intern("padding-top"))}, std::string{});
    const computed_style_ptr after =
        f.styles.resolve_pseudo(txn, a, f.atoms.intern("after"), f.style_of(a));
    CHECK(after);
    CHECK_EQ(std::string{after->get(f.atoms.intern("height"))}, std::string{"2px"});
    CHECK_EQ(std::string{after->get(f.atoms.intern("width"))}, std::string{});
    // ...inheriting from the element: the colour it did not set itself.
    CHECK_EQ(std::string{after->get(f.atoms.intern("color"))}, std::string{"#010101"});
    // A different element's ::before is not this one's.
    const computed_style_ptr b_before = f.styles.resolve_pseudo(
        txn, f.find_id("b"), f.atoms.intern("before"), f.style_of(f.find_id("b")));
    CHECK_EQ(std::string{b_before->get(f.atoms.intern("width"))}, std::string{"1px"});
    // The ordinary cascade still resolves the element afterwards.
    f.resolved = f.styles.resolve_all(txn);
    expect_value(f, a, "width", "", "still not the element's");
}

// HTML's legacy case-insensitive attribute values, :is()'s forgiving list,
// and what may follow a pseudo-element.
void test_selector_grammar_edges() {
    {
        fixture f;
        f.load("<p id=a lang=ES></p><p id=b data-x=ES></p>",
               "[lang|=es] { color: #010101 } [data-x=es] { color: #020202 }"
               ":is(:total-nonsense, #b) { width: 1px } :is() { height: 1px }");
        expect_value(f, f.find_id("a"), "color", "#010101", "lang compares case-insensitively");
        CHECK(f.value_of(f.find_id("b"), "color").empty()); // data-x does not.
        expect_value(f, f.find_id("b"), "width", "1px", ":is() drops what does not parse");
        CHECK(f.value_of(f.find_id("a"), "height").empty()); // `:is()` matches nothing.
    }
    {
        fixture f;
        f.load("<p id=a></p>",
               "#a::before.x, #a { color: #010101 }"
               "#a::before div, #a { width: 1px } #a, :not(::before) { height: 1px }"
               "#a::before:hover, #a { border-width: 1px }");
        // A selector list with one invalid selector is invalid whole.
        CHECK(f.value_of(f.find_id("a"), "color").empty());
        CHECK(f.value_of(f.find_id("a"), "width").empty());
        CHECK(f.value_of(f.find_id("a"), "height").empty());
        expect_value(f, f.find_id("a"), "border-width", "1px", "::before:hover is valid");
    }
    {
        // A type selector opens its compound: `[x]p`, `#a*` and `.c|p` are
        // syntax errors, not a tag added after the fact.
        fixture f;
        f.load("<p id=a x class=c></p>", "[x]p, #a { color: #010101 } #a*, #a { width: 1px }"
                                         ".c|p, #a { height: 1px } p[x], #a { border-width: 1px }");
        CHECK(f.value_of(f.find_id("a"), "color").empty());
        CHECK(f.value_of(f.find_id("a"), "width").empty());
        CHECK(f.value_of(f.find_id("a"), "height").empty());
        expect_value(f, f.find_id("a"), "border-width", "1px", "p[x] is valid");
    }
}

} // namespace

int main() {
    test_selector_grammar_edges();
    test_simple_selectors();
    test_namespaces();
    test_pseudo_elements();
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
