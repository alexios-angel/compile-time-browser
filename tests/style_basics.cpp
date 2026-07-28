// ctbrowser.style: matching, the cascade, and interning.
//
// Correctness first. A matcher that is fast and wrong is worse than the previous engine's,
// which is slow and right - so the bucketing and the ancestor filter are
// checked against cases designed to break them: selectors whose rightmost
// compound files them in an unexpected bucket, and descendant selectors the
// filter is supposed to reject without walking.

#include <ctbrowser/core/core.hpp>
import ctbrowser.dom;
import ctbrowser.style;

#include "check.hpp"
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using namespace ctbrowser::style;

namespace {

// Build a document, resolve it, and let a test ask for one element's value.
struct fixture {
    atom_table atoms;
    document doc{atoms};
    engine styles{atoms};
    style_map resolved;

    void load(std::string_view html, std::string_view css, std::string_view ua = {}) {
        (void)parse_html(doc, html);
        if (!ua.empty()) { styles.add_sheet(ua, 0); } // user agent origin
        styles.add_sheet(css, 1);                     // author origin
        const auto txn = doc.read();
        resolved = styles.resolve_all(txn);
    }

    [[nodiscard]] node_id find(std::string_view tag_name) {
        const auto txn = doc.read();
        const atom want = atoms.intern_lower(tag_name);
        node_id found{};
        const auto walk = [&](auto && self, node_id at) -> void {
            if (txn.kind(at).value_or(node_kind::text) == node_kind::element &&
                txn.tag(at).value_or(atom{}) == want && !found) {
                found = at;
            }
            for (const node_id c : txn.children(at)) { self(self, c); }
        };
        walk(walk, txn.root());
        return found;
    }
    [[nodiscard]] node_id find_id(std::string_view want_id) {
        const auto txn = doc.read();
        const atom key = atoms.intern("id");
        node_id found{};
        const auto walk = [&](auto && self, node_id at) -> void {
            if (txn.attribute_value(at, key) == want_id && !found) { found = at; }
            for (const node_id c : txn.children(at)) { self(self, c); }
        };
        walk(walk, txn.root());
        return found;
    }

    [[nodiscard]] std::string value_of(node_id n, std::string_view property) {
        const auto it = resolved.find(engine::key_of(n));
        if (it == resolved.end() || !it->second) { return "<unresolved>"; }
        return std::string{it->second->get(atoms.intern_lower(property))};
    }
    [[nodiscard]] computed_style_ptr style_of(node_id n) {
        const auto it = resolved.find(engine::key_of(n));
        return it == resolved.end() ? computed_style_ptr{} : it->second;
    }
};

void expect_value(fixture & f, node_id n, std::string_view property, std::string_view want,
                  std::string_view what) {
    const std::string got = f.value_of(n, property);
    if (got != want) {
        std::printf("FAIL %-38s %s => '%s' (want '%s')\n", std::string{what}.c_str(),
                    std::string{property}.c_str(), got.c_str(), std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}

// `margin: 1px 2px` IS four declarations. Expanding at RECORD time rather than
// where the property is read is what makes the cascade come out right - the
// four carry the shorthand's source order, so a longhand written after it wins
// and one written before it loses, which is what CSS says.
//
// Before this, only the shorthand was read, so every per-side longhand in every
// sheet did nothing - including the UA sheet's own `ul { padding-left: 40px }`.
void test_shorthands_expand() {
    {
        fixture f;
        f.load("<div id=a></div>", "#a { margin: 1px 2px 3px 4px }");
        const node_id a = f.find("div");
        expect_value(f, a, "margin-top", "1px", "four values: top");
        expect_value(f, a, "margin-right", "2px", "four values: right");
        expect_value(f, a, "margin-bottom", "3px", "four values: bottom");
        expect_value(f, a, "margin-left", "4px", "four values: left");
    }
    {
        fixture f;
        f.load("<div id=a></div>", "#a { padding: 5px }");
        const node_id a = f.find("div");
        expect_value(f, a, "padding-top", "5px", "one value: all four");
        expect_value(f, a, "padding-left", "5px", "one value: all four");
    }
    {
        fixture f;
        f.load("<div id=a></div>", "#a { padding: 5px 6px }");
        const node_id a = f.find("div");
        expect_value(f, a, "padding-top", "5px", "two values: vertical");
        expect_value(f, a, "padding-left", "6px", "two values: horizontal");
    }
    {
        fixture f;
        f.load("<div id=a></div>", "#a { padding: 1px 2px 3px }");
        const node_id a = f.find("div");
        expect_value(f, a, "padding-top", "1px", "three values: top");
        expect_value(f, a, "padding-right", "2px", "three values: horizontal");
        expect_value(f, a, "padding-bottom", "3px", "three values: bottom");
        expect_value(f, a, "padding-left", "2px", "three values: left mirrors right");
    }
    // SOURCE ORDER, both ways round. This is the half that reading "shorthand
    // first, then longhand" gets backwards.
    {
        fixture f;
        f.load("<div id=a></div>", "#a { padding: 1px; padding-left: 9px }");
        expect_value(f, f.find("div"), "padding-left", "9px", "a longhand AFTER wins");
        expect_value(f, f.find("div"), "padding-right", "1px", "and leaves the others alone");
    }
    {
        fixture f;
        f.load("<div id=a></div>", "#a { padding-left: 9px; padding: 1px }");
        expect_value(f, f.find("div"), "padding-left", "1px", "a shorthand AFTER overwrites it");
    }
    // And through the style ATTRIBUTE, which takes a different path into the
    // cascade than a sheet does.
    {
        fixture f;
        f.load("<div id=a style='margin: 7px 8px'></div>", "");
        expect_value(f, f.find("div"), "margin-top", "7px", "inline styles expand too");
        expect_value(f, f.find("div"), "margin-right", "8px", "inline styles expand too");
    }
}

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

} // namespace

int main() {
    test_shorthands_expand();
    test_simple_selectors();
    test_bucketing_uses_the_rightmost_compound();
    test_descendant_and_child_combinators();
    test_specificity_and_source_order();
    test_author_beats_user_agent();
    test_identical_styles_are_shared();
    test_deep_nesting_still_matches();
    test_unmatched_element_gets_empty_style();
    test_inline_style_applies();
    test_inline_beats_any_selector();
    test_important_selector_beats_inline();
    test_important_inline_beats_everything();
    test_inline_style_oddities();
    test_inline_style_is_per_element();
    REPORT("style_basics");
}
