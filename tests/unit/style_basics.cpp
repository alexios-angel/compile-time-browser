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

// var() SUBSTITUTION. 1,370 calls in Bootstrap, and its whole component layer is
// built on them: `.btn` declares thirty `--bs-btn-*` and reads every one back.
void test_var_substitution() {
    {
        fixture f;
        f.load("<p id=a></p>", ":root { --c: #010101 } p { color: var(--c) }");
        expect_value(f, f.find_id("a"), "color", "#010101", "a plain var()");
    }
    {
        // From an INHERITED custom property two levels up, which is the Bootstrap
        // shape: `:root` defines, a component reads.
        fixture f;
        f.load("<html><body><div><button id=b></button></div></body></html>",
               ":root { --c: #010101 } button { color: var(--c) }");
        expect_value(f, f.find_id("b"), "color", "#010101", "inherited through the tree");
    }
    {
        // The element's OWN definition wins over an inherited one - a component
        // overriding a theme.
        fixture f;
        f.load("<html><body><button id=b class=btn></button></body></html>",
               ":root { --c: #010101 } .btn { --c: #020202; color: var(--c) }");
        expect_value(f, f.find_id("b"), "color", "#020202", "own beats inherited");
    }
    {
        // A var() SURROUNDED by other tokens, and several in one value.
        fixture f;
        f.load("<p id=a></p>", ":root { --w: 2px; --s: solid } p { border-width: var(--w); "
                               "border-color: var(--w) var(--s) }");
        expect_value(f, f.find_id("a"), "border-width", "2px", "one var");
        expect_value(f, f.find_id("a"), "border-color", "2px solid", "two vars in one value");
    }
    {
        // A var() EXPANDING TO A COMMA LIST, which is the case that settles why
        // substitution is a token-stream operation: one argument becomes three.
        fixture f;
        f.load("<p id=a></p>", ":root { --rgb: 33, 37, 41 } p { color: rgba(var(--rgb), .5) }");
        expect_value(f, f.find_id("a"), "color", "rgba(33, 37, 41, .5)", "a var() comma list");
    }
    {
        // THE FALLBACK is everything after the FIRST comma, commas included - because a
        // custom property's value may itself be a comma list.
        fixture f;
        f.load("<p id=a></p>", "p { color: var(--missing, #030303) }");
        expect_value(f, f.find_id("a"), "color", "#030303", "the fallback is used");
    }
    {
        fixture f;
        f.load("<p id=a></p>", "p { font-family: var(--missing, Helvetica, Arial) }");
        expect_value(f, f.find_id("a"), "font-family", "Helvetica, Arial",
                     "a fallback with commas in it");
    }
    {
        // A fallback is only used when the property is ABSENT. A present one wins even
        // if a fallback was written.
        fixture f;
        f.load("<p id=a></p>", ":root { --c: #010101 } p { color: var(--c, #030303) }");
        expect_value(f, f.find_id("a"), "color", "#010101", "present beats fallback");
    }
    {
        // NESTED var(), in the value and in the fallback.
        fixture f;
        f.load("<p id=a></p>", ":root { --a: var(--b); --b: #010101 } p { color: var(--a) }");
        expect_value(f, f.find_id("a"), "color", "#010101", "a var() inside a var()");
    }
    {
        fixture f;
        f.load("<p id=a></p>", ":root { --b: #020202 } p { color: var(--missing, var(--b)) }");
        expect_value(f, f.find_id("a"), "color", "#020202", "a var() inside a fallback");
    }
    {
        // AN EMPTY BUT VALID custom property substitutes to NOTHING rather than making
        // the declaration invalid. Bootstrap ships seventeen of these.
        fixture f;
        f.load("<p id=a></p>", ":root { --e: ; } p { font-family: var(--e) }");
        // The property survives with an empty value, which every consumer reads as
        // "nothing said" - not as the declaration having been thrown away.
        CHECK(f.value_of(f.find_id("a"), "font-family").empty());
    }
    {
        // INVALID AT COMPUTED-VALUE TIME IS `unset`, NOT "drop it". This is the classic
        // wrong implementation and it is observable: the earlier declaration must NOT
        // win, the inherited value must show through.
        fixture f;
        f.load("<div><p id=a></p></div>",
               "div { color: #010101 } p { color: #020202; color: var(--missing) }");
        expect_value(f, f.find_id("a"), "color", "#010101",
                     "IACVT falls back to inherited, not to the earlier declaration");
    }
    {
        // ...and on a NON-inherited property it reads as absent.
        fixture f;
        f.load("<p id=a></p>", "p { background-color: #020202; background-color: var(--nope) }");
        CHECK(f.value_of(f.find_id("a"), "background-color").empty());
    }
    {
        // A CYCLE makes both invalid rather than recursing to the depth limit.
        fixture f;
        f.load("<p id=a></p>", ":root { --a: var(--b); --b: var(--a) } p { color: var(--a) }");
        CHECK(f.value_of(f.find_id("a"), "color").empty());
    }
    {
        // `!important` ON A CUSTOM PROPERTY is the DECLARATION's importance, not part
        // of its value - so it is peeled where every other declaration's is, and a
        // var() cannot smuggle it into the property that reads it. Worth a test
        // because the obvious guess is that the value keeps it.
        fixture f;
        f.load("<p id=a></p>", ":root { --bang: red !important } p { color: var(--bang) }");
        expect_value(f, f.find_id("a"), "color", "red", "!important is peeled, not substituted");
    }
    {
        // The residual case the structure guard is actually for: a `!` in the value
        // that is NOT `!important` survives the peel, and a substituted value must not
        // introduce one at the top level.
        fixture f;
        f.load("<p id=a></p>", ":root { --odd: red !oops } p { color: var(--odd) }");
        CHECK(f.value_of(f.find_id("a"), "color").empty());
    }
    {
        // A CUSTOM PROPERTY'S OWN value is never substituted in place - it is stored
        // verbatim and expanded only when something reads it through var(). So the
        // stored text still says `var(--b)`.
        fixture f;
        f.load("<html><body id=a></body></html>", ":root { --a: var(--b); --b: #010101 }");
        expect_value(f, f.find_id("a"), "--a", "var(--b)", "stored verbatim");
    }
}

// THE `border` SHORTHAND, which is `<width> || <style> || <color>` in ANY order - so
// its parts are classified by WHAT THEY ARE, unlike the positional side lists.
//
// It produced nothing at all before: paint reads `border-width` and `border-color`,
// the shorthand set neither, and every card, table and list-group border was
// invisible. It could not be expanded before var() resolved either, because
// `border: var(--w) solid var(--c)` has an unknowable component count until then.
void test_border_shorthand() {
    {
        fixture f;
        f.load("<p id=a></p>", "p { border: 1px solid #dee2e6 }");
        expect_value(f, f.find_id("a"), "border-width", "1px", "width from the shorthand");
        expect_value(f, f.find_id("a"), "border-style", "solid", "style from the shorthand");
        expect_value(f, f.find_id("a"), "border-color", "#dee2e6", "colour from the shorthand");
    }
    {
        // ANY ORDER. This is the case that positional splitting gets wrong.
        fixture f;
        f.load("<p id=a></p>", "p { border: #dee2e6 1px solid }");
        expect_value(f, f.find_id("a"), "border-width", "1px", "width, written last but one");
        expect_value(f, f.find_id("a"), "border-style", "solid", "style, written last");
        expect_value(f, f.find_id("a"), "border-color", "#dee2e6", "colour, written first");
    }
    {
        // Through a var(), which is how Bootstrap writes every one of them.
        fixture f;
        f.load("<html><body><p id=a></p></body></html>",
               ":root { --w: 2px; --c: #010101 } p { border: var(--w) solid var(--c) }");
        expect_value(f, f.find_id("a"), "border-width", "2px", "width through a var");
        expect_value(f, f.find_id("a"), "border-color", "#010101", "colour through a var");
    }
    {
        // ONE var() EXPANDING TO THE WHOLE SHORTHAND - `.alert` does exactly this, via
        // `--bs-alert-border: 1px solid #9ec5fe`. Expansion has to happen AFTER
        // substitution or the single token becomes one unclassifiable part.
        fixture f;
        f.load("<html><body><p id=a></p></body></html>",
               ":root { --all: 1px solid #9ec5fe } p { border: var(--all) }");
        expect_value(f, f.find_id("a"), "border-width", "1px", "width from a whole-value var");
        expect_value(f, f.find_id("a"), "border-style", "solid", "style from a whole-value var");
        expect_value(f, f.find_id("a"), "border-color", "#9ec5fe", "colour from a whole-value var");
    }
    {
        // `border: 0` sets every longhand it governs, including the ones it did not
        // mention - which is what makes it RESET a style set elsewhere.
        fixture f;
        f.load("<p id=a></p>", "p { border-style: dashed } p { border: 0 }");
        expect_value(f, f.find_id("a"), "border-width", "0", "width zero");
        expect_value(f, f.find_id("a"), "border-style", "none", "and the style is reset");
    }
    {
        // A longhand written AFTER the shorthand still wins, which is the source-order
        // property that moving expansion into the cascade had to preserve.
        fixture f;
        f.load("<p id=a></p>", "p { border: 1px solid red; border-color: #020202 }");
        expect_value(f, f.find_id("a"), "border-color", "#020202", "a longhand after wins");
        expect_value(f, f.find_id("a"), "border-width", "1px", "and leaves the rest alone");
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

// calc(), which is 134 expressions in Bootstrap and used to be 134 dropped
// declarations - parse_length gave up on the leading `c` and the property was
// silently nothing.
void test_calc() {
    using ctbrowser::style::css::evaluate_calc;
    using ctbrowser::style::css::fold_calc;
    using ctbrowser::style::css::length_context;
    length_context ctx;
    ctx.font_size = 20.0f;
    ctx.root_font_size = 16.0f;
    ctx.viewport_width = 1000.0f;
    ctx.viewport_height = 800.0f;

    const auto px = [&](std::string_view expression) {
        const auto answer = evaluate_calc(expression, ctx);
        CHECK(answer.has_value());
        CHECK(!answer->has_percent);
        return answer ? answer->px : -1.0f;
    };
    const auto invalid = [&](std::string_view expression) {
        CHECK(!evaluate_calc(expression, ctx).has_value());
    };

    CHECK(px("1px + 2px") == 3.0f);
    CHECK(px("10px - 4px") == 6.0f);
    // The shape 34 of Bootstrap's calcs have: a length scaled by a number, either
    // way round, and with the number negative.
    CHECK(px("2rem * .5") == 16.0f);
    CHECK(px(".5 * 2rem") == 16.0f);
    CHECK(px("-1 * 1.5rem") == -24.0f);
    CHECK(px("-.5 * 3rem") == -24.0f);
    CHECK(px("10px / 4") == 2.5f);
    // Units. `em` is the element's own size, `rem` the root's - the distinction the
    // font-size pre-pass exists to make available.
    CHECK(px("2em") == 40.0f);
    CHECK(px("2rem") == 32.0f);
    CHECK(px("10vw") == 100.0f);
    CHECK(px("10vh") == 80.0f);
    CHECK(px("10vmin") == 80.0f);
    CHECK(px("10vmax") == 100.0f);
    CHECK(px("1in") == 96.0f);
    CHECK(px("72pt") == 96.0f);
    // Bootstrap's fluid heading size, mixing two different bases in one sum.
    CHECK(px("1.375rem + 1.5vw") == 22.0f + 15.0f);
    // Precedence, parentheses, and a nested calc - which Bootstrap writes eight
    // times over.
    CHECK(px("1px + 2px * 3") == 7.0f);
    CHECK(px("(1px + 2px) * 3") == 9.0f);
    CHECK(px("1em + .5rem + calc(1rem * 2)") == 20.0f + 8.0f + 32.0f);
    CHECK(px("calc(calc(1px))") == 1.0f);

    // THE TYPE RULES, which are the point of not just adding numbers up.
    invalid("1px + 2");      // a length and a number
    invalid("2px * 3px");    // an area, which calc has no type for
    invalid("2px / 3px");    // the divisor has to be a number
    invalid("2px / 0");      // and a nonzero one
    invalid("1px 2px");      // no operator
    invalid("1px +");        // nothing after one
    invalid("(1px");         // unclosed
    invalid("5");            // a bare number is not a length
    invalid("1frobs + 2px"); // an unmodelled unit, rather than a silent zero
    // `+` and `-` REQUIRE surrounding whitespace, and this gets that for free: the
    // tokenizer makes `-12px` one dimension, so there is no operator between the
    // two terms. Chrome rejects it too.
    invalid("100% -12px");

    // A percentage has no answer until a containing block exists, so it survives as
    // the canonical two-term form layout::parse_length knows how to read.
    CHECK(fold_calc("calc(100% - 12px)", ctx).text == "calc(100% - 12px)");
    CHECK(fold_calc("calc(50% + 1rem)", ctx).text == "calc(50% + 16px)");
    CHECK(fold_calc("calc(100% * .5)", ctx).text == "50%");
    CHECK(fold_calc("calc(2rem)", ctx).text == "32px");

    // fold_calc leaves everything else alone, AND SAYS WHETHER IT COULD READ IT.
    // The text is what a caller with no better answer carries on with; the flag is
    // what lets the cascade treat the declaration as invalid instead, which is the
    // difference between `margin-top` taking its initial 0 and layout being handed
    // a string it answers `auto` to.
    CHECK(fold_calc("1px solid red", ctx).text == "1px solid red");
    CHECK(fold_calc("1px solid red", ctx).ok);
    CHECK(fold_calc("calc(1px + 2)", ctx).text == "calc(1px + 2)");
    CHECK(!fold_calc("calc(1px + 2)", ctx).ok);
    // A NUMBER IS NOT A LENGTH, and this is the one Bootstrap writes: `.row`'s
    // `margin-top: calc(-1 * var(--bs-gutter-y))` with a gutter of `0` multiplies
    // two numbers and gets a number.
    CHECK(!fold_calc("calc(-1 * 0)", ctx).ok);
    // A vendor-prefixed one is not a calc at all, so there is nothing to fail.
    CHECK(fold_calc("-webkit-calc(1px + 1px)", ctx).text == "-webkit-calc(1px + 1px)");
    CHECK(fold_calc("-webkit-calc(1px + 1px)", ctx).ok);
    // Two of them in one value, which is how Bootstrap writes `.row` gutters.
    CHECK(fold_calc("calc(2rem * .5) calc(1rem + 1rem)", ctx).text == "16px 32px");
    // One good and one bad is still a bad VALUE.
    CHECK(!fold_calc("calc(1rem) calc(1px + 2)", ctx).ok);
}

// The cascade end of the same thing: a calc reaches an element as a number, an em
// resolves against the element's own font size, and a rem against the root's.
void test_calc_in_the_cascade() {
    {
        fixture f;
        f.load("<p id=a></p>", ":root { --gap: 24px } p { padding-left: calc(var(--gap) * .5) }");
        expect_value(f, f.find_id("a"), "padding-left", "12px", "a var inside a calc");
    }
    {
        // `.container`'s actual declaration, which the parity report named as the
        // cause of a +12px shift on 27 of 40 elements.
        fixture f;
        f.load("<div id=a></div>", ":root { --bs-gutter-x: 1.5rem }"
                                   "div { padding-right: calc(var(--bs-gutter-x) * .5);"
                                   "      padding-left: calc(var(--bs-gutter-x) * .5) }");
        expect_value(f, f.find_id("a"), "padding-left", "12px", "the container's gutter");
        expect_value(f, f.find_id("a"), "padding-right", "12px", "on both sides");
    }
    {
        // `em` IS THE ELEMENT'S OWN SIZE, which is the whole reason font-size is
        // folded before anything else reads it. 2em of a 32px font is 64px, not 32.
        fixture f;
        f.load("<p id=a></p>", "p { font-size: 32px; margin-top: calc(1em + 4px) }");
        expect_value(f, f.find_id("a"), "margin-top", "36px", "em against its own size");
    }
    {
        // ...and in font-size itself it is the PARENT's, which is CSS's asymmetry
        // and not a convenience: `font-size: 2em` doubles rather than recursing.
        fixture f;
        f.load("<div id=o><p id=a></p></div>", "div { font-size: 20px } p { font-size: 2em }");
        expect_value(f, f.find_id("a"), "font-size", "40px", "em in font-size is the parent's");
    }
    {
        // A REM IS THE ROOT'S SIZE, not a hardcoded 16.
        fixture f;
        f.load("<p id=a></p>", "html { font-size: 20px } p { width: calc(2rem) }");
        expect_value(f, f.find_id("a"), "width", "40px", "rem against a root that moved");
    }
    {
        // Bootstrap's fluid type, which needs the viewport as well as the root.
        fixture f;
        ctbrowser::style::css::media_environment env;
        env.viewport_width = 1000;
        env.viewport_height = 800;
        (void)f.styles.set_environment(env);
        f.load("<h1 id=a></h1>", "h1 { font-size: calc(1.375rem + 1.5vw) }");
        expect_value(f, f.find_id("a"), "font-size", "37px", "22px + 15px");
    }
    {
        // AN INVALID CALC IS AN INVALID DECLARATION, not a value nobody can read.
        //
        // This assertion used to say the opposite - that the text survived and
        // every consumer of a length would reject it - and that was wrong in a way
        // only the Chrome diff could show. layout's parse_length answers `auto` for
        // a string it cannot read, and `auto` is not the same as absent: Chrome
        // reports the property's INITIAL value, which is what an absent declaration
        // produces here. On the grid fixture the difference was 24 elements, every
        // `.row`'s `margin-top: calc(-1 * var(--bs-gutter-y))` with a gutter of 0.
        fixture f;
        f.load("<p id=a></p>", "p { width: calc(1px + 2) }");
        expect_value(f, f.find_id("a"), "width", "", "an invalid calc is dropped");
    }
    {
        // WHICH KIND of invalid decides what happens to an EARLIER declaration, and
        // the two cases are observably different. A value that went through var()
        // substitution is invalid at COMPUTED-VALUE time, and §3 spells that
        // `unset` - so it removes the earlier declaration it beat...
        fixture f;
        f.load("<p id=a></p>", "p { --g: 0; width: 5px; width: calc(-1 * var(--g)) }");
        expect_value(f, f.find_id("a"), "width", "", "IACVT is unset, not 'the earlier one wins'");
    }
    {
        // ...while one that never contained a var() is invalid at PARSE time, so
        // the earlier declaration simply wins. Getting these two the same way round
        // is the same distinction `color: red; color: var(--missing)` pins.
        fixture f;
        f.load("<p id=a></p>", "p { width: 5px; width: calc(1px + 2) }");
        expect_value(f, f.find_id("a"), "width", "5px",
                     "a parse-time invalid lets the earlier win");
    }
}

// The `flex` shorthand. Bootstrap's grid is `.col { flex: 1 0 0 }`, and a
// `.flex-grow-0` utility written after it has to win - which only works if the
// shorthand becomes longhands in the cascade rather than being read by the flex
// algorithm later.
void test_flex_shorthand() {
    const auto three = [](fixture & f, std::string_view grow, std::string_view shrink,
                          std::string_view basis, std::string_view why) {
        expect_value(f, f.find_id("a"), "flex-grow", grow, why);
        expect_value(f, f.find_id("a"), "flex-shrink", shrink, why);
        expect_value(f, f.find_id("a"), "flex-basis", basis, why);
    };
    {
        fixture f;
        f.load("<div id=a></div>", "div { flex: 1 0 0 }");
        three(f, "1", "0", "0", ".col's own declaration");
    }
    {
        // ONE NUMBER takes the shorthand's defaults, which are NOT the longhands'
        // initial values: flex-basis initial is `auto`, but `flex: 1` is `1 1 0%`.
        fixture f;
        f.load("<div id=a></div>", "div { flex: 1 }");
        three(f, "1", "1", "0%", "a bare grow factor");
    }
    {
        fixture f;
        f.load("<div id=a></div>", "div { flex: none }");
        three(f, "0", "0", "auto", "none");
    }
    {
        fixture f;
        f.load("<div id=a></div>", "div { flex: auto }");
        three(f, "1", "1", "auto", "auto");
    }
    {
        // A single WIDTH is a basis, not a grow factor - the two one-value forms are
        // told apart by whether the value carries a unit.
        fixture f;
        f.load("<div id=a></div>", "div { flex: 200px }");
        three(f, "1", "1", "200px", "a bare basis");
    }
    {
        fixture f;
        f.load("<div id=a></div>", "div { flex: 2 3 }");
        three(f, "2", "3", "0%", "grow and shrink");
    }
    {
        // ...and a second value with a unit is the BASIS, leaving shrink at 1.
        fixture f;
        f.load("<div id=a></div>", "div { flex: 2 30% }");
        three(f, "2", "1", "30%", "grow and basis");
    }
    {
        // THE POINT OF EXPANDING AT ALL: a longhand written after the shorthand
        // wins, and one written before it is overwritten. Bootstrap's `.col` plus a
        // `.flex-grow-0` utility is exactly this pair.
        fixture f;
        // Both selectors are one class, so SOURCE ORDER decides - which is the
        // property under test. A tag selector here would have lost to `.col` on
        // specificity and proved nothing.
        f.load("<div id=a class=\"col flex-grow-0\"></div>",
               ".col { flex: 1 0 0 } .flex-grow-0 { flex-grow: 0 }");
        expect_value(f, f.find_id("a"), "flex-grow", "0", "the later longhand wins");
        expect_value(f, f.find_id("a"), "flex-shrink", "0", "and the rest survive");
    }
    {
        fixture f;
        f.load("<div id=a></div>", "div { flex-grow: 7; flex: 1 0 0 }");
        expect_value(f, f.find_id("a"), "flex-grow", "1", "the shorthand overwrites");
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
    test_inheritance();
    test_explicit_defaulting_keywords();
    test_var_substitution();
    test_border_shorthand();
    test_media_queries();
    test_calc();
    test_calc_in_the_cascade();
    test_flex_shorthand();
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
