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
        f.load("<html><body><p></p></body></html>", ":root { color: #010101 }");
        // The document element, not the body and not a paragraph.
        expect_value(f, f.find("html"), "color", "#010101", ":root is the document element");
        CHECK(f.value_of(f.find("body"), "color").empty());
        CHECK(f.value_of(f.find("p"), "color").empty());
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

// STRUCTURAL PSEUDO-CLASSES. Position, not name - so every one of them is
// arithmetic on four numbers the traversal counted on its way down.
void test_structural_pseudos() {
    {
        fixture f;
        f.load("<ul><li id=a></li><li id=b></li><li id=c></li></ul>",
               "li:first-child { color: #010101 }"
               "li:last-child { background-color: #020202 }");
        expect_value(f, f.find_id("a"), "color", "#010101", ":first-child");
        CHECK(f.value_of(f.find_id("b"), "color").empty());
        expect_value(f, f.find_id("c"), "background-color", "#020202", ":last-child");
        CHECK(f.value_of(f.find_id("b"), "background-color").empty());
    }
    {
        // :only-child needs the level TOTAL, which the traversal cannot know from the
        // siblings it has already seen - it is counted when the level is entered.
        fixture f;
        f.load("<div><p id=lonely></p></div><div><p id=x></p><p id=y></p></div>",
               "p:only-child { color: #010101 }");
        expect_value(f, f.find_id("lonely"), "color", "#010101", ":only-child");
        CHECK(f.value_of(f.find_id("x"), "color").empty());
    }
    {
        // The -of-type family counts only siblings sharing a tag, so the <em> between
        // the two <p>s does not make the second <p> stop being last-of-type.
        fixture f;
        f.load("<div><p id=p1></p><em id=e1></em><p id=p2></p></div>",
               "p:first-of-type { color: #010101 }"
               "p:last-of-type { background-color: #020202 }"
               "em:only-of-type { border-color: #030303 }");
        expect_value(f, f.find_id("p1"), "color", "#010101", ":first-of-type");
        expect_value(f, f.find_id("p2"), "background-color", "#020202", ":last-of-type");
        CHECK(f.value_of(f.find_id("p1"), "background-color").empty());
        expect_value(f, f.find_id("e1"), "border-color", "#030303", ":only-of-type");
    }
    {
        // :empty. WHITESPACE IS CONTENT per the spec, so `<p> </p>` is not empty -
        // this is the case an implementation trims and gets wrong.
        fixture f;
        f.load("<p id=none></p><p id=space> </p><p id=text>x</p><p id=child><em></em></p>",
               "p:empty { color: #010101 }");
        expect_value(f, f.find_id("none"), "color", "#010101", ":empty on nothing");
        CHECK(f.value_of(f.find_id("space"), "color").empty()); // whitespace counts
        CHECK(f.value_of(f.find_id("text"), "color").empty());
        CHECK(f.value_of(f.find_id("child"), "color").empty());
    }
    {
        // Composed in one compound: `:root` is also `:only-child` of the document.
        fixture f;
        f.load("<html><body></body></html>", ":root:only-child { color: #010101 }");
        expect_value(f, f.find("html"), "color", "#010101", ":root:only-child");
    }
}

// nth-child AND ITS THREE RELATIVES. The An+B series is where the awkwardness is:
// the tokenizer has already decided where the numbers are, so `2n+1` is a dimension
// then a signed number while `2n + 1` is a dimension, a delim and a number.
void test_nth_child() {
    const std::string_view six = "<ul><li id=n1></li><li id=n2></li><li id=n3></li>"
                                 "<li id=n4></li><li id=n5></li><li id=n6></li></ul>";
    const auto lit = [&](fixture & f, std::string_view id, std::string_view what) {
        return !f.value_of(f.find_id(id), "color").empty() ? true : (void(what), false);
    };
    {
        fixture f;
        f.load(six, "li:nth-child(2n) { color: #010101 }"); // the even ones
        CHECK(!lit(f, "n1", "1"));
        CHECK(lit(f, "n2", "2"));
        CHECK(!lit(f, "n3", "3"));
        CHECK(lit(f, "n4", "4"));
    }
    {
        fixture f;
        f.load(six, "li:nth-child(2n+1) { color: #010101 }"); // the odd ones
        CHECK(lit(f, "n1", "1"));
        CHECK(!lit(f, "n2", "2"));
        CHECK(lit(f, "n3", "3"));
    }
    {
        fixture f;
        f.load(six, "li:nth-child(odd) { color: #010101 }");
        CHECK(lit(f, "n1", "1"));
        CHECK(!lit(f, "n2", "2"));
    }
    {
        fixture f;
        f.load(six, "li:nth-child(even) { color: #010101 }");
        CHECK(!lit(f, "n1", "1"));
        CHECK(lit(f, "n2", "2"));
    }
    {
        // a == 0 is a SINGLE index, not a series.
        fixture f;
        f.load(six, "li:nth-child(3) { color: #010101 }");
        CHECK(!lit(f, "n2", "2"));
        CHECK(lit(f, "n3", "3"));
        CHECK(!lit(f, "n4", "4"));
    }
    {
        // A NEGATIVE step counts down from B, so this is the first three and nothing
        // else. An implementation that only checks divisibility matches everything.
        fixture f;
        f.load(six, "li:nth-child(-n+3) { color: #010101 }");
        CHECK(lit(f, "n1", "1"));
        CHECK(lit(f, "n3", "3"));
        CHECK(!lit(f, "n4", "4"));
        CHECK(!lit(f, "n6", "6"));
    }
    {
        // Whitespace inside the argument is allowed and means nothing.
        fixture f;
        f.load(six, "li:nth-child( 2n + 1 ) { color: #010101 }");
        CHECK(lit(f, "n1", "1"));
        CHECK(!lit(f, "n2", "2"));
    }
    {
        // nth-last-child counts from the END.
        fixture f;
        f.load(six, "li:nth-last-child(1) { color: #010101 }");
        CHECK(lit(f, "n6", "last"));
        CHECK(!lit(f, "n5", "second last"));
    }
    {
        // ...and the -of-type pair count only same-tag siblings.
        fixture f;
        f.load("<div><em></em><p id=p1></p><em></em><p id=p2></p></div>",
               "p:nth-of-type(2) { color: #010101 }");
        CHECK(f.value_of(f.find_id("p1"), "color").empty());
        expect_value(f, f.find_id("p2"), "color", "#010101", ":nth-of-type(2)");
    }
    {
        fixture f;
        f.load("<div><p id=p1></p><em></em><p id=p2></p></div>",
               "p:nth-last-of-type(1) { color: #010101 }");
        expect_value(f, f.find_id("p2"), "color", "#010101", ":nth-last-of-type(1)");
        CHECK(f.value_of(f.find_id("p1"), "color").empty());
    }
}

// :not, :is AND :where. The argument is a full selector list run against the same
// subject, so it may carry combinators of its own.
void test_functional_pseudos() {
    {
        fixture f;
        f.load("<p id=a class=skip></p><p id=b></p>", "p:not(.skip) { color: #010101 }");
        CHECK(f.value_of(f.find_id("a"), "color").empty());
        expect_value(f, f.find_id("b"), "color", "#010101", ":not excludes");
    }
    {
        // A selector LIST inside :not() - none of them may match. The old front end
        // split the prelude on a bare comma and fragmented this into two halves.
        fixture f;
        f.load("<p id=a class=x></p><p id=b class=y></p><p id=c></p>",
               "p:not(.x, .y) { color: #010101 }");
        CHECK(f.value_of(f.find_id("a"), "color").empty());
        CHECK(f.value_of(f.find_id("b"), "color").empty());
        expect_value(f, f.find_id("c"), "color", "#010101", ":not with a list");
    }
    {
        // Two :not()s in one compound, which is how Bootstrap writes
        // `.btn:not(:disabled):not(.disabled)`.
        fixture f;
        f.load("<p id=a class=x></p><p id=b class=y></p><p id=c></p>",
               "p:not(.x):not(.y) { color: #010101 }");
        CHECK(f.value_of(f.find_id("a"), "color").empty());
        CHECK(f.value_of(f.find_id("b"), "color").empty());
        expect_value(f, f.find_id("c"), "color", "#010101", "two :not()s");
    }
    {
        fixture f;
        f.load("<p id=a class=x></p><p id=b class=y></p><p id=c></p>",
               "p:is(.x, .y) { color: #010101 }");
        expect_value(f, f.find_id("a"), "color", "#010101", ":is matches either");
        expect_value(f, f.find_id("b"), "color", "#010101", ":is matches either");
        CHECK(f.value_of(f.find_id("c"), "color").empty());
    }
    {
        // A COMBINATOR inside the argument. The nested selector's subject is the same
        // element, so its combinators walk from the same cursor.
        fixture f;
        f.load("<div class=wrap><p id=in></p></div><p id=out></p>",
               "p:is(.wrap > p) { color: #010101 }");
        expect_value(f, f.find_id("in"), "color", "#010101", ":is with a child combinator");
        CHECK(f.value_of(f.find_id("out"), "color").empty());
    }
    {
        // :not() with a combinator, which is the same machinery negated.
        fixture f;
        f.load("<div class=wrap><p id=in></p></div><p id=out></p>",
               "p:not(.wrap p) { color: #010101 }");
        CHECK(f.value_of(f.find_id("in"), "color").empty());
        expect_value(f, f.find_id("out"), "color", "#010101", ":not with a descendant");
    }
    {
        // Nested inside each other.
        fixture f;
        f.load("<p id=a class=x></p><p id=b></p>", "p:not(:is(.x)) { color: #010101 }");
        CHECK(f.value_of(f.find_id("a"), "color").empty());
        expect_value(f, f.find_id("b"), "color", "#010101", ":not(:is(...))");
    }
    {
        // SPECIFICITY. `:is()` takes its most specific argument, so `p:is(#a)` is
        // (1,0,1) and beats `.cls` at (0,1,0).
        fixture f;
        f.load("<p id=a class=cls></p>", ".cls { color: #010101 } p:is(#a) { color: #020202 }");
        expect_value(f, f.find_id("a"), "color", "#020202", ":is takes its argument's weight");
    }
    {
        // ...and `:where()` contributes NOTHING, which is the entire reason it
        // exists. `p:where(#a)` is (0,0,1) and loses to `.cls`.
        fixture f;
        f.load("<p id=a class=cls></p>", "p:where(#a) { color: #010101 } .cls { color: #020202 }");
        expect_value(f, f.find_id("a"), "color", "#020202", ":where weighs nothing");
    }
    {
        // `:not()` weighs its argument too, so `p:not(#zz)` beats `.cls`.
        fixture f;
        f.load("<p id=a class=cls></p>", ".cls { color: #010101 } p:not(#zz) { color: #020202 }");
        expect_value(f, f.find_id("a"), "color", "#020202", ":not weighs its argument");
    }
    {
        // :has() is NOT supported and must not silently pass. It looks forward at
        // descendants the traversal has not visited, so it stays unmatchable rather
        // than wrong.
        fixture f;
        f.load("<div id=a><p></p></div>", "div:has(p) { color: #010101 }");
        CHECK(f.value_of(f.find_id("a"), "color").empty());
        CHECK(f.styles.selector_count() == 0);
    }
    {
        // An argument this engine cannot represent makes the whole thing unmatchable
        // rather than vacuously true - which is the direction that matters, because a
        // dead branch inside :not() would otherwise read as "matches nothing,
        // therefore :not passes".
        fixture f;
        f.load("<p id=a></p>", "p:not(::-webkit-slider-thumb) { color: #010101 }");
        CHECK(f.value_of(f.find_id("a"), "color").empty());
    }
}

// :disabled, :enabled, :checked, :link - facts about the element, not UI state.
//
// These were state bits beside `:hover` and NOTHING EVER SET THEM. That was
// invisible while `:not()` was unsupported, because the selector simply never
// matched; implementing `:not()` turned it into a wrong render, since
// `.btn:not(:disabled)` then matched disabled buttons too - and Bootstrap writes
// exactly that eight times.
//
// It also shows what the coverage census cannot see: those selectors ALWAYS counted
// as "can match", because they parsed. Countable is not the same as correct.
void test_element_state_pseudos() {
    {
        fixture f;
        f.load("<button id=off disabled></button><button id=on></button>",
               "button:disabled { color: #010101 }"
               "button:enabled { background-color: #020202 }");
        expect_value(f, f.find_id("off"), "color", "#010101", ":disabled");
        CHECK(f.value_of(f.find_id("on"), "color").empty());
        expect_value(f, f.find_id("on"), "background-color", "#020202", ":enabled");
        CHECK(f.value_of(f.find_id("off"), "background-color").empty());
    }
    {
        // THE BUG THIS RUNG WOULD OTHERWISE HAVE SHIPPED: `:not(:disabled)` must
        // exclude a disabled control. With :disabled unset it matched everything.
        fixture f;
        f.load("<button id=off disabled></button><button id=on></button>",
               "button:not(:disabled) { color: #010101 }");
        CHECK(f.value_of(f.find_id("off"), "color").empty());
        expect_value(f, f.find_id("on"), "color", "#010101", ":not(:disabled) keeps the enabled");
    }
    {
        // `:enabled` is NOT the negation of `:disabled`: it applies only to elements
        // that could be disabled, so a <div> is neither.
        fixture f;
        f.load("<div id=d></div>", "div:enabled { color: #010101 }");
        CHECK(f.value_of(f.find_id("d"), "color").empty());
    }
    {
        fixture f;
        f.load("<input id=on type=checkbox checked><input id=off type=checkbox>",
               "input:checked { color: #010101 }");
        expect_value(f, f.find_id("on"), "color", "#010101", ":checked from the attribute");
        CHECK(f.value_of(f.find_id("off"), "color").empty());
    }
    {
        fixture f;
        f.load("<a id=linked href=x></a><a id=bare></a>", "a:link { color: #010101 }");
        expect_value(f, f.find_id("linked"), "color", "#010101", ":link needs an href");
        CHECK(f.value_of(f.find_id("bare"), "color").empty());
    }
    {
        // `:visited` never matches, and that is the honest answer rather than a gap:
        // Chrome restricts it to colour for privacy reasons.
        fixture f;
        f.load("<a id=a href=x></a>", "a:visited { color: #010101 }");
        CHECK(f.value_of(f.find_id("a"), "color").empty());
    }
}

int main() {
    test_shorthands_expand();
    test_simple_selectors();
    test_bucketing_uses_the_rightmost_compound();
    test_descendant_and_child_combinators();
    test_specificity_and_source_order();
    test_author_beats_user_agent();
    test_identical_styles_are_shared();
    test_a_selector_is_compiled_once_per_selector();
    test_attribute_selectors();
    test_root_selector();
    test_sibling_combinators();
    test_resolving_twice_does_not_leak_siblings();
    test_structural_pseudos();
    test_nth_child();
    test_functional_pseudos();
    test_element_state_pseudos();
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
