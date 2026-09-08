// Custom properties: var() substitution, its fallbacks and cycles, and
// `initial` on a custom property being the guaranteed-invalid value. Carved out
// of unit/style_basics.cpp on 2026-09-08 - style_shorthands.cpp names the
// family, and style_fixture.hpp is the fixture every file in it shares.

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

// `initial` on a CUSTOM property is the guaranteed-invalid value, not an empty
// one - CSS Variables §3 - and the difference is the whole of Bootstrap 5.3's
// theming layer.
void test_initial_on_a_custom_property_is_guaranteed_invalid() {
    {
        // `--x: initial` is the SENTINEL Bootstrap writes so that a var() reading
        // it falls through to its fallback, and `.table-striped` then overrides
        // it with a real colour. Treating it as empty-but-defined made the var()
        // substitute NOTHING - so every table cell's `box-shadow` lost its colour
        // and no stripe was ever painted.
        fixture f;
        f.load("<div id=a></div>", "#a { --x: initial; color: var(--x, #123456) }");
        expect_value(f, f.find("div"), "color", "#123456",
                     "`initial` makes var() take its fallback");
    }
    {
        // AN EMPTY CUSTOM PROPERTY IS A DIFFERENT THING and still substitutes to
        // nothing. Bootstrap ships seventeen of those, and the declaration that
        // reads one is invalid at computed-value time rather than falling back.
        fixture f;
        f.load("<div id=a></div>", "#a { --x: ; color: var(--x, #123456) }");
        expect_value(f, f.find("div"), "color", "", "an EMPTY custom property is not invalid");
    }
    {
        // And it is invalid HERE, whatever an ancestor said - `initial` does not
        // let the inherited value show through.
        fixture f;
        f.load("<div id=outer><div id=a></div></div>",
               "#outer { --x: red } #a { --x: initial; color: var(--x, #123456) }");
        expect_value(f, f.find_id("a"), "color", "#123456",
                     "and it beats an inherited value rather than revealing it");
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
        // THE CARRIER IS NOT A SHORTHAND, deliberately. This used to use
        // `border-color`, which was fine while nothing expanded it and wrong the
        // moment it became the four-side shorthand it really is - the test would
        // then have been asserting an expansion it does not care about.
        // `background-position` takes two lengths, is expanded by nothing, and is
        // read by nothing, so it holds exactly what the substitution produced.
        f.load("<p id=a></p>", ":root { --x: 10px; --y: 20px } p { border-width: var(--x); "
                               "background-position: var(--x) var(--y) }");
        expect_value(f, f.find_id("a"), "border-width", "10px", "one var");
        expect_value(f, f.find_id("a"), "background-position", "10px 20px",
                     "two vars in one value");
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

} // namespace

int main() {
    test_initial_on_a_custom_property_is_guaranteed_invalid();
    test_var_substitution();
    REPORT("style_custom_properties");
}
