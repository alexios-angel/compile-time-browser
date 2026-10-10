// Pseudo-classes: the structural ones, :nth-child() and its arithmetic, the
// functional ones (:is, :where, :not, :has) and the element-state ones. Carved
// out of unit/style_basics.cpp on 2026-09-08 - shorthands.cpp names the
// family, and fixture.hpp is the fixture every file in it shares.

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
        // :has() looks forward at descendants the traversal has not visited, so
        // it is answered by a scoped query from the subject - a second walker,
        // because the first is mid-traversal. It weighs its argument, as :is does.
        fixture f;
        f.load("<div id=a><p></p></div><div id=b></div>",
               "div:has(p) { color: #010101 } div:has(> span) { color: #020202 }");
        expect_value(f, f.find_id("a"), "color", "#010101", ":has matches a descendant");
        CHECK(f.value_of(f.find_id("b"), "color").empty());
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

// `:focus-within` is `:focus` on the element or on anything under it, derived
// from the one bit; `:focus-visible` is a bit of its own the shell sets with
// the focus when the heuristic says to show it (browser/focus.cpp).
void test_focus_within_and_visible() {
    fixture f;
    (void)parse_html(f.doc, "<div id=outer><div id=inner><input id=i></div></div><p id=p></p>");
    // z-index rather than an inherited property: `html` and `body` are
    // :focus-within too, and `p` would inherit their colour.
    f.styles.add_sheet(":focus-within { z-index: 1 } :focus-visible { z-index: 2 }", 1);
    (void)f.styles.set_state(f.find_id("i"), engine::state_focus, true);
    f.resolved = f.styles.resolve_all(f.doc.read());
    expect_value(f, f.find_id("i"), "z-index", "1", ":focus alone is not :focus-visible");
    (void)f.styles.set_state(f.find_id("i"), style::state_focus_visible, true);
    f.resolved = f.styles.resolve_all(f.doc.read());
    expect_value(f, f.find_id("i"), "z-index", "2", ":focus-visible on the focused element");
    expect_value(f, f.find_id("inner"), "z-index", "1", "its parent");
    expect_value(f, f.find_id("outer"), "z-index", "1", "its grandparent");
    CHECK(f.value_of(f.find_id("p"), "z-index").empty());
}

// `dir=auto` on a text control reads its VALUE - the attribute until the form
// store says it was edited - and a date control reads nothing (HTML §3.2.6.4,
// dir-auto-form-associated).
void test_dir_auto_reads_a_controls_value() {
    fixture f;
    f.load("<input id=t dir=auto value='א'><input id=d type=date dir=auto value='א'>"
           "<input id=e dir=auto>",
           "");
    const auto txn = f.doc.read();
    CHECK(f.styles.direction_is_rtl(txn, f.find_id("t")));
    CHECK(!f.styles.direction_is_rtl(txn, f.find_id("d")));
    CHECK(!f.styles.direction_is_rtl(txn, f.find_id("e")));
    const node_id edited = f.find_id("e");
    f.styles.set_control_value([edited](node_id id) -> std::optional<std::string> {
        if (id == edited) { return std::string{"א"}; }
        return std::nullopt;
    });
    CHECK(f.styles.direction_is_rtl(txn, edited));
}

} // namespace

// `:placeholder-shown`: a placeholder attribute - empty or not - and an empty
// value (placeholder-shown).
void test_placeholder_shown() {
    fixture f;
    f.load("<input id=a placeholder><input id=b placeholder value=x><input id=c>"
           "<textarea id=d placeholder=p></textarea>",
           ":placeholder-shown { color: #010101 }");
    expect_value(f, f.find_id("a"), "color", "#010101", "an empty placeholder is shown");
    CHECK(f.value_of(f.find_id("b"), "color").empty());
    CHECK(f.value_of(f.find_id("c"), "color").empty());
    expect_value(f, f.find_id("d"), "color", "#010101", "an empty textarea shows it");
}

// `:valid` and `:invalid` are the validity hook's answer, and neither matches
// an element it says nothing about - nor anything at all without a hook.
void test_valid_invalid() {
    fixture f;
    f.load("<input id=a><input id=b><div id=c></div>",
           ":valid { color: #020202 } :invalid { color: #030303 }");
    CHECK(f.value_of(f.find_id("a"), "color").empty());
    const node_id a = f.find_id("a"), b = f.find_id("b");
    f.styles.set_validity([a, b](node_id id) -> std::optional<bool> {
        if (id == a) { return true; }
        if (id == b) { return false; }
        return std::nullopt;
    });
    f.resolved = f.styles.resolve_all(f.doc.read());
    expect_value(f, a, "color", "#020202", "a control that satisfies its constraints");
    expect_value(f, b, "color", "#030303", "a control that does not");
    CHECK(f.value_of(f.find_id("c"), "color").empty());
}

int main() {
    test_valid_invalid();
    test_placeholder_shown();
    test_dir_auto_reads_a_controls_value();
    test_structural_pseudos();
    test_nth_child();
    test_functional_pseudos();
    test_element_state_pseudos();
    test_focus_within_and_visible();
    REPORT("style_pseudo_classes");
}
