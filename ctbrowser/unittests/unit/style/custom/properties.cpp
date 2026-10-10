//===- properties.cpp - Custom property and var() tests ---------*- C++ -*-===//
//
// Custom properties: var() substitution, its fallbacks and cycles,
// `initial` as the guaranteed-invalid value, and inherit(). ../fixture.hpp is
// the shared document-and-resolve fixture.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/style/style.hpp>

#include "../fixture.hpp"
#include "check.hpp"

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using namespace ctbrowser::style;

namespace {

// `initial` on a custom property is the guaranteed-invalid value, not an empty
// one (CSS Variables §3). Bootstrap 5.3's theming layer depends on the
// difference.
void test_initial_on_a_custom_property_is_guaranteed_invalid() {
    {
        // `--x: initial` is the sentinel Bootstrap writes so that a var() reading
        // it falls through to its fallback, and `.table-striped` then overrides
        // it with a real colour. Treated as empty-but-defined, the var() would
        // substitute nothing and no table stripe would be painted.
        // A substituted value is re-spelled by the property's grammar, as
        // el.style would spell it: a hex colour reads back as rgb() (CSS
        // Color 4 §15), which is why every expectation below is rgb().
        fixture f;
        f.load("<div id=a></div>", "#a { --x: initial; color: var(--x, #123456) }");
        expect_value(f, f.find("div"), "color", "rgb(18, 52, 86)",
                     "`initial` makes var() take its fallback");
    }
    {
        // An empty custom property is a different thing and still substitutes to
        // nothing. Bootstrap ships several, and the declaration that reads one
        // is invalid at computed-value time rather than falling back.
        fixture f;
        f.load("<div id=a></div>", "#a { --x: ; color: var(--x, #123456) }");
        expect_value(f, f.find("div"), "color", "", "an EMPTY custom property is not invalid");
    }
    {
        // And it is invalid here, whatever an ancestor said - `initial` does not
        // let the inherited value show through.
        fixture f;
        f.load("<div id=outer><div id=a></div></div>",
               "#outer { --x: red } #a { --x: initial; color: var(--x, #123456) }");
        expect_value(f, f.find_id("a"), "color", "rgb(18, 52, 86)",
                     "and it beats an inherited value rather than revealing it");
    }
}

// var() substitution. Bootstrap's whole component layer is built on it: `.btn`
// declares thirty `--bs-btn-*` properties and reads every one back.
void test_var_substitution() {
    {
        fixture f;
        f.load("<p id=a></p>", ":root { --c: #010101 } p { color: var(--c) }");
        expect_value(f, f.find_id("a"), "color", "rgb(1, 1, 1)", "a plain var()");
    }
    {
        // From an inherited custom property two levels up, which is the Bootstrap
        // shape: `:root` defines, a component reads.
        fixture f;
        f.load("<html><body><div><button id=b></button></div></body></html>",
               ":root { --c: #010101 } button { color: var(--c) }");
        expect_value(f, f.find_id("b"), "color", "rgb(1, 1, 1)", "inherited through the tree");
    }
    {
        // The element's own definition wins over an inherited one - a component
        // overriding a theme.
        fixture f;
        f.load("<html><body><button id=b class=btn></button></body></html>",
               ":root { --c: #010101 } .btn { --c: #020202; color: var(--c) }");
        expect_value(f, f.find_id("b"), "color", "rgb(2, 2, 2)", "own beats inherited");
    }
    {
        // A var() surrounded by other tokens, and several in one value.
        fixture f;
        // background-position is the carrier because it is a longhand: the stored
        // value is exactly what substitution produced, with no shorthand expansion.
        f.load("<p id=a></p>", ":root { --x: 10px; --y: 20px } p { border-width: var(--x); "
                               "background-position: var(--x) var(--y) }");
        expect_value(f, f.find_id("a"), "border-width", "10px", "one var");
        expect_value(f, f.find_id("a"), "background-position", "10px 20px",
                     "two vars in one value");
    }
    {
        // A var() expanding to a comma list, which is the case that settles why
        // substitution is a token-stream operation: one argument becomes three.
        fixture f;
        f.load("<p id=a></p>", ":root { --rgb: 33, 37, 41 } p { color: rgba(var(--rgb), .5) }");
        expect_value(f, f.find_id("a"), "color", "rgba(33, 37, 41, 0.5)", "a var() comma list");
    }
    {
        // The fallback is everything after the first comma, commas included - because a
        // custom property's value may itself be a comma list.
        fixture f;
        f.load("<p id=a></p>", "p { color: var(--missing, #030303) }");
        expect_value(f, f.find_id("a"), "color", "rgb(3, 3, 3)", "the fallback is used");
    }
    {
        fixture f;
        f.load("<p id=a></p>", "p { font-family: var(--missing, Helvetica, Arial) }");
        expect_value(f, f.find_id("a"), "font-family", "Helvetica, Arial",
                     "a fallback with commas in it");
    }
    {
        // A fallback is only used when the property is absent. A present one wins even
        // if a fallback was written.
        fixture f;
        f.load("<p id=a></p>", ":root { --c: #010101 } p { color: var(--c, #030303) }");
        expect_value(f, f.find_id("a"), "color", "rgb(1, 1, 1)", "present beats fallback");
    }
    {
        // Nested var(), in the value and in the fallback.
        fixture f;
        f.load("<p id=a></p>", ":root { --a: var(--b); --b: #010101 } p { color: var(--a) }");
        expect_value(f, f.find_id("a"), "color", "rgb(1, 1, 1)", "a var() inside a var()");
    }
    {
        fixture f;
        f.load("<p id=a></p>", ":root { --b: #020202 } p { color: var(--missing, var(--b)) }");
        expect_value(f, f.find_id("a"), "color", "rgb(2, 2, 2)", "a var() inside a fallback");
    }
    {
        // An empty but valid custom property substitutes to nothing rather than
        // making the declaration invalid.
        fixture f;
        f.load("<p id=a></p>", ":root { --e: ; } p { font-family: var(--e) }");
        // The property survives with an empty value, which every consumer reads as
        // "nothing said" - not as the declaration having been thrown away.
        CHECK(f.value_of(f.find_id("a"), "font-family").empty());
    }
    {
        // Invalid at computed-value time is `unset`, not "drop it". Dropping is
        // the classic wrong implementation and it is observable: the earlier
        // declaration must not win, and the inherited value must show through.
        fixture f;
        f.load("<div><p id=a></p></div>",
               "div { color: #010101 } p { color: #020202; color: var(--missing) }");
        expect_value(f, f.find_id("a"), "color", "#010101",
                     "IACVT falls back to inherited, not to the earlier declaration");
    }
    {
        // ...and on a non-inherited property it reads as absent.
        fixture f;
        f.load("<p id=a></p>", "p { background-color: #020202; background-color: var(--nope) }");
        CHECK(f.value_of(f.find_id("a"), "background-color").empty());
    }
    {
        // A cycle makes both invalid rather than recursing to the depth limit.
        fixture f;
        f.load("<p id=a></p>", ":root { --a: var(--b); --b: var(--a) } p { color: var(--a) }");
        CHECK(f.value_of(f.find_id("a"), "color").empty());
    }
    {
        // A substituted value the property's grammar refuses is invalid at
        // computed-value time too - `width: 10` is not ten pixels - while one
        // the grammar takes, or does not model, is kept (attr-all-types).
        fixture f;
        f.load("<p id=a></p>", ":root { --n: 10; --l: 10px; --c: 33, 37, 41 } "
                               "p { width: 5px; width: var(--n); height: var(--l); "
                               "color: rgba(var(--c), .5) }");
        CHECK(f.value_of(f.find_id("a"), "width").empty());
        expect_value(f, f.find_id("a"), "height", "10px", "a length from a var()");
        expect_value(f, f.find_id("a"), "color", "rgba(33, 37, 41, 0.5)", "a colour from a var()");
    }
    {
        // `!important` on a custom property is the declaration's importance, not part
        // of its value - so it is peeled where every other declaration's is, and a
        // var() cannot smuggle it into the property that reads it. Worth a test
        // because the obvious guess is that the value keeps it.
        fixture f;
        f.load("<p id=a></p>", ":root { --bang: red !important } p { color: var(--bang) }");
        expect_value(f, f.find_id("a"), "color", "red", "!important is peeled, not substituted");
    }
    {
        // A `!` that is not `!important` stays in the custom property's value, and
        // a substituted value may not put a top-level `!` into the reading
        // property, so `color` becomes invalid at computed-value time.
        fixture f;
        f.load("<p id=a></p>", ":root { --odd: red !oops } p { color: var(--odd) }");
        CHECK(f.value_of(f.find_id("a"), "color").empty());
    }
    {
        // A custom property's own value is never substituted in place - it is stored
        // verbatim and expanded only when something reads it through var(). So the
        // stored text still says `var(--b)`.
        fixture f;
        f.load("<html><body id=a></body></html>", ":root { --a: var(--b); --b: #010101 }");
        expect_value(f, f.find_id("a"), "--a", "var(--b)", "stored verbatim");
    }
    {
        // A substituted value is spelled as `el.style` would spell it: the same
        // grammar that validates it serialises it, so a 25-digit `<integer>`
        // is the double it became (cssom/serialize-custom-props), and a
        // keyword is lowercased.
        fixture f;
        f.load("<p id=a></p>", ":root { --n: 1111111111111111111111111; --k: BLOCK }"
                               "p { z-index: var(--n); display: var(--k) }");
        expect_value(f, f.find_id("a"), "z-index", "1111111111111111092469760", "an integer");
        expect_value(f, f.find_id("a"), "display", "block", "a keyword");
    }
}

// inherit-function-basic: `inherit(--x)` is the parent's computed value of
// the custom property, and the fallback when the parent has none.
void test_inherit_function() {
    fixture f;
    f.load("<div id=p><div id=a></div></div><div id=b></div>",
           ":root { --r: 3 } #p { --z: 2 } #a { --z: 13; z-index: inherit(--z); "
           "order: inherit(--r) } #b { --z: 13; z-index: inherit(--z, 4); "
           "order: inherit(--q); --w: inherit(--z, 5) }");
    expect_value(f, f.find_id("a"), "z-index", "2", "the parent's value");
    expect_value(f, f.find_id("a"), "order", "3", "an ancestor's value");
    expect_value(f, f.find_id("b"), "z-index", "4", "the fallback");
    expect_value(f, f.find_id("b"), "order", "", "no value and no fallback");
    // In a custom property the inherit() is replaced in the cascade, so a
    // chain of them accumulates through the parent's already-computed value
    // (inherit-function-basic).
    expect_value(f, f.find_id("b"), "--w", "5", "a custom property's fallback");
    fixture chain;
    chain.load("<div id=e1><div id=e2><div id=e3></div></div></div>",
               "#e1 { --v: e1 } #e2 { --v: e2 inherit(--v) } #e3 { --v: e3 inherit(--v) }");
    expect_value(chain, chain.find_id("e3"), "--v", "e3 e2 e1", "accumulating values");
    // Two inherit()s double the value at every level: thirty levels would be
    // gigabytes. Past 64 KB the property is guaranteed-invalid, and so is every
    // descendant's.
    std::string html = "<html><body>";
    for (int i = 0; i < 30; ++i) { html += "<div id=d" + std::to_string(i) + ">"; }
    fixture doubling;
    doubling.load(html, "html { --v: x } div { --v: inherit(--v) inherit(--v) }");
    expect_value(doubling, doubling.find_id("d1"), "--v", "x x x x", "two levels doubled");
    CHECK(doubling.value_of(doubling.find_id("d29"), "--v").size() < 1024);
    CHECK(doubling.value_of(doubling.find_id("d29"), "--v") ==
          doubling.value_of(doubling.find_id("d25"), "--v"));
}

} // namespace

int main() {
    test_inherit_function();
    test_initial_on_a_custom_property_is_guaranteed_invalid();
    test_var_substitution();
    REPORT("style_custom_properties");
}
