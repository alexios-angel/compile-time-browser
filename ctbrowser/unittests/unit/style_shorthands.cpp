// Shorthands: a shorthand IS its longhands, expanded at record time so the
// cascade sees four declarations with the shorthand's source order. Carved out
// of unit/style_basics.cpp on 2026-09-08, when that file was 2,155 lines; the
// five other style_*.cpp files beside this one are the rest of it, and
// style_fixture.hpp is the document-and-resolve fixture they all share.

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

// A shorthand's parts are separated by whitespace, EXCEPT the whitespace inside
// a function or a string. That sounds like pedantry and is the single largest
// paint bug this project has had.
void test_a_shorthand_does_not_split_inside_a_function() {
    {
        // `border: 1px solid rgba(0, 0, 0, 0.175)` is THREE parts. Split on the
        // spaces inside the colour it is five, of which the third - and therefore
        // the whole `border-color` - is the string `rgba(0,`. That fails to parse,
        // so every card, alert, table and input in Bootstrap had a border in the
        // box model and NOTHING drawn.
        fixture f;
        f.load("<div id=a></div>", "#a { border: 1px solid rgba(0, 0, 0, 0.5) }");
        const node_id a = f.find("div");
        expect_value(f, a, "border-width", "1px", "the width is still the width");
        expect_value(f, a, "border-style", "solid", "and the style the style");
        expect_value(f, a, "border-color", "rgba(0, 0, 0, 0.5)",
                     "and the colour survives its own commas");
    }
    {
        // A quoted string can hold a bracket or a space too.
        fixture f;
        f.load("<div id=a></div>", "#a { margin: 1px 2px }");
        expect_value(f, f.find("div"), "margin-right", "2px", "and an ordinary split still splits");
    }
}

// The per-side form of `border`, which is how Bootstrap draws every divider it
// has - a card header's rule, a card footer's, the line under a navbar.
void test_the_per_side_border_shorthands_expand() {
    {
        fixture f;
        f.load("<div id=a></div>", "#a { border-bottom: 2px dashed #123456 }");
        const node_id a = f.find("div");
        expect_value(f, a, "border-bottom-width", "2px", "the side's width");
        expect_value(f, a, "border-bottom-style", "dashed", "its style");
        expect_value(f, a, "border-bottom-color", "#123456", "and its colour");
        // AND NOT THE UNIFORM ONES. Setting those "so it draws" was tried: a
        // `border-bottom` then inset the box on all four sides and painted a full
        // ring, which cost 8 differences on one fixture and 18 on another.
        expect_value(f, a, "border-top-width", "", "the other sides are untouched");
        expect_value(f, a, "border-width", "", "and so is the uniform property");
    }
    {
        // `border` sets all twelve longhands, so a per-side one written after it
        // can override a single edge - which it cannot do if the first
        // declaration only wrote a uniform value.
        fixture f;
        f.load("<div id=a></div>", "#a { border: 1px solid red; border-bottom-color: blue }");
        const node_id a = f.find("div");
        expect_value(f, a, "border-top-color", "red", "three sides keep the shorthand's colour");
        expect_value(f, a, "border-bottom-color", "blue", "and the fourth takes the longhand's");
    }
}

// `border-radius` and `gap`, which are shorthands over CORNERS and over AXES
// rather than over the four sides - so neither can reuse the side expansion and
// each gets the order wrong in its own way if it tries.
void test_corner_and_axis_shorthands_expand() {
    {
        // FOUR VALUES GO CLOCKWISE FROM THE TOP LEFT, not top/right/bottom/left:
        // these name corners. The two-value form is the two DIAGONALS, which has
        // no analogue at all among the edge shorthands.
        fixture f;
        f.load("<div id=a></div>", "#a { border-radius: 1px 2px 3px 4px }");
        const node_id a = f.find("div");
        expect_value(f, a, "border-top-left-radius", "1px", "clockwise: top left");
        expect_value(f, a, "border-top-right-radius", "2px", "clockwise: top right");
        expect_value(f, a, "border-bottom-right-radius", "3px", "clockwise: bottom right");
        expect_value(f, a, "border-bottom-left-radius", "4px", "clockwise: bottom left");
    }
    {
        fixture f;
        f.load("<div id=a></div>", "#a { border-radius: 5px 6px }");
        const node_id a = f.find("div");
        expect_value(f, a, "border-top-left-radius", "5px", "two values: one diagonal");
        expect_value(f, a, "border-bottom-right-radius", "5px", "...both of its corners");
        expect_value(f, a, "border-top-right-radius", "6px", "two values: the other diagonal");
        expect_value(f, a, "border-bottom-left-radius", "6px", "...both of its corners");
    }
    {
        // `.rounded-pill { border-radius: 50rem }`, which is what makes this
        // matter: the value survives as a real length and the SCALING to the box
        // happens at paint time, where the box's size is known.
        fixture f;
        f.load("<div id=a></div>", "#a { border-radius: 50rem }");
        expect_value(f, f.find("div"), "border-top-left-radius", "800px",
                     "a rem radius folds like any other length");
    }
    {
        // The elliptical `a / b` form keeps the horizontal half and makes every
        // corner circular. Bootstrap writes none; half an answer beats dropping
        // the declaration, and it is a recorded known difference.
        fixture f;
        f.load("<div id=a></div>", "#a { border-radius: 10px / 20px }");
        expect_value(f, f.find("div"), "border-top-left-radius", "10px",
                     "the horizontal group is kept");
    }
    {
        // `gap` is ROW then COLUMN - the block axis first, which is the opposite
        // of how every other two-value shorthand here reads.
        fixture f;
        f.load("<div id=a></div>", "#a { gap: 1px 2px }");
        expect_value(f, f.find("div"), "row-gap", "1px", "the FIRST value is the row gap");
        expect_value(f, f.find("div"), "column-gap", "2px", "and the second the column gap");
    }
    {
        fixture f;
        f.load("<div id=a></div>", "#a { gap: 3px }");
        expect_value(f, f.find("div"), "row-gap", "3px", "one value sets both");
        expect_value(f, f.find("div"), "column-gap", "3px", "...");
    }
}

void test_overflow_shorthand_expands_in_cascade_order() {
    {
        fixture f;
        f.load("<div id=a></div>", "#a { overflow: hidden auto }");
        const node_id a = f.find("div");
        expect_value(f, a, "overflow-x", "hidden", "overflow's first value is the x axis");
        expect_value(f, a, "overflow-y", "auto", "overflow's second value is the y axis");
    }
    {
        fixture f;
        f.load("<div id=a></div>",
               "#a { overflow: hidden; overflow-x: visible; overflow-y: visible }");
        const node_id a = f.find("div");
        expect_value(f, a, "overflow-x", "visible", "later x longhand beats shorthand");
        expect_value(f, a, "overflow-y", "visible", "later y longhand beats shorthand");
    }
    {
        fixture f;
        f.load("<div id=a></div>", "#a { overflow-x: hidden; overflow: visible }");
        const node_id a = f.find("div");
        expect_value(f, a, "overflow-x", "visible", "later shorthand resets x");
        expect_value(f, a, "overflow-y", "visible", "one shorthand value sets both axes");
    }
    {
        // Substitution happens after the cascade. A winning shorthand that
        // becomes invalid then behaves as unset for BOTH of its longhands,
        // rather than exposing an earlier axis declaration again.
        fixture f;
        f.load("<div id=a></div>",
               "#a { --bad: potato; overflow-x: hidden; overflow: var(--bad) }");
        const node_id a = f.find("div");
        expect_value(f, a, "overflow-x", "", "IACVT overflow resets its x longhand");
        expect_value(f, a, "overflow-y", "", "IACVT overflow resets its y longhand");
    }
    {
        // The same invalid grammar without var() is invalid at parse time, so it
        // is ignored and the earlier valid declaration remains the winner.
        fixture f;
        f.load("<div id=a></div>", "#a { overflow-x: hidden; overflow: potato }");
        expect_value(f, f.find("div"), "overflow-x", "hidden",
                     "parse-time invalid overflow leaves the earlier axis");
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

} // namespace

int main() {
    test_shorthands_expand();
    test_corner_and_axis_shorthands_expand();
    test_overflow_shorthand_expands_in_cascade_order();
    test_a_shorthand_does_not_split_inside_a_function();
    test_the_per_side_border_shorthands_expand();
    test_border_shorthand();
    test_flex_shorthand();
    REPORT("style_shorthands");
}
