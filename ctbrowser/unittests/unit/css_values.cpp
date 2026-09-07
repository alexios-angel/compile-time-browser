// The property table and the value grammar - what `el.style`, `getComputedStyle`
// and `CSS.supports` all three ask.
//
// EVERY CASE HERE IS EITHER A SPECIFICATION RULE OR A CORPUS FAILURE. The corpus
// half comes from `css/css-values`, whose `test_invalid_value` is one assertion
// per file: set the property, read it back, and expect `""`. Before this table
// existed `el.style` recorded whatever it was given and handed it back
// unchanged, so `width: round()` read back as `round()` - measured as ~600
// failing subtests in `docs/css-conformance.md` §4.
//
// The half that is NOT about refusing things is the more important one. Three
// vendored corpora and every render golden write through `el.style`, so a
// grammar that refuses a value they use turns a render into a blank box. The
// cases below marked ACCEPTED-ON-PURPOSE are that guard: a shorthand, an
// unknown property, a `var()` and a comparison function must all survive.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <string_view>

using ctbrowser::style::css::check_declaration;
using ctbrowser::style::css::css_name_of;
using ctbrowser::style::css::find_property;
using ctbrowser::style::css::idl_name_of;
using ctbrowser::style::css::known_properties;
using ctbrowser::style::css::supports_condition;
using ctbrowser::style::css::supports_declaration;

namespace {

void ok(std::string_view property, std::string_view value, std::string_view serialized) {
    const auto answer = check_declaration(property, value);
    CHECK(answer.valid);
    CHECK_EQ(answer.serialized, std::string{serialized});
}

void bad(std::string_view property, std::string_view value) {
    const auto answer = check_declaration(property, value);
    CHECK(!answer.valid);
    CHECK_EQ(answer.serialized, std::string{});
}

void test_lengths_and_the_unitless_zero() {
    ok("width", "10px", "10px");
    ok("width", "10PX", "10px");     // units are ASCII case-insensitive
    ok("width", "  10px  ", "10px"); // and whitespace is not part of the value
    ok("width", "50%", "50%");
    ok("width", "10.500px", "10.5px"); // shortest form, CSSOM 6.7.2
    // A UNITLESS ZERO IS A LENGTH AND NOTHING ELSE IS. `width: 0` is valid and
    // serialises as `0px`; `width: 1` is not a length at all, and answering
    // `1px` for it is the single most common way a permissive parser is wrong.
    ok("width", "0", "0px");
    bad("width", "1");
    bad("width", "10");
    bad("width", "-1px"); // width is non-negative; margin-left is not
    ok("margin-left", "-1px", "-1px");
    ok("margin-left", "auto", "auto");
    bad("width", "10 px"); // a space makes it two values
    bad("width", "px");
    bad("width", "10pxx");
    ok("width", "10em", "10em");
    ok("width", "10vmin", "10vmin");
    ok("width", "10q", "10q");
}

void test_numbers_integers_and_keywords() {
    ok("z-index", "3", "3");
    ok("z-index", "-3", "-3");
    ok("z-index", "auto", "auto");
    bad("z-index", "3.5"); // <integer>, and 3.5 is not one
    bad("z-index", "3px");
    ok("opacity", "0.5", "0.5");
    ok("opacity", ".5", "0.5");
    ok("opacity", "50%", "50%");
    ok("flex-grow", "2", "2");
    bad("flex-grow", "-2"); // non-negative
    // A unitless number IS a line-height, which is the case that makes
    // `number_length` a kind of its own: `line-height: 2` is not `2px`.
    ok("line-height", "2", "2");
    ok("line-height", "2px", "2px");
    ok("line-height", "normal", "normal");
    ok("display", "BLOCK", "block"); // keywords fold to lowercase
    bad("display", "blocks");
    bad("display", "10px");
    ok("position", "absolute", "absolute");
    bad("position", "absolute relative");
}

void test_the_css_wide_keywords_apply_to_everything() {
    ok("width", "inherit", "inherit");
    ok("display", "INITIAL", "initial");
    ok("z-index", "unset", "unset");
    ok("margin", "revert", "revert");
    // ...including a property this table has never heard of.
    ok("scroll-snap-type", "inherit", "inherit");
}

void test_the_things_that_must_survive() {
    // ACCEPTED-ON-PURPOSE. Each of these is a value a page in the corpora
    // actually writes, and refusing any of them is how this file could break a
    // render that works today.
    ok("margin", "10px 20px", "10px 20px");         // a shorthand is freeform
    ok("border", "1px solid red", "1px solid red"); // ...and so is this one
    ok("color", "#0d6efd", "#0d6efd");              // colours are not modelled yet
    ok("width", "var(--w)", "var(--w)");            // substitution defers the answer
    ok("width", "calc(100% - 10px)", "calc(100% - 10px)");
    ok("width", "clamp(1rem, 2vw, 3rem)", "clamp(1rem, 2vw, 3rem)");
    // THE AUTHOR'S BYTES for anything the grammar does not model, spacing and
    // all. `test_valid_value` asserts the round-trip exactly, and normalising a
    // value whose grammar is unknown turned two passing `css-values` files into
    // failing ones on 2026-09-07.
    ok("width", "min(10px,5%)", "min(10px,5%)");
    ok("font-family", "random-item(auto ,serif)", "random-item(auto ,serif)");
    ok("font-family", "\"Helvetica Neue\", sans-serif", "\"Helvetica Neue\", sans-serif");
    // An UNKNOWN property is stored, not refused: CSSOM lets a page set one.
    ok("-webkit-line-clamp", "3", "3");
    ok("scroll-snap-type", "x mandatory", "x mandatory");
}

void test_what_a_math_function_may_not_be() {
    bad("width", "round()"); // the corpus case, verbatim
    bad("width", "min()");   // ...and its two siblings
    bad("width", "calc()");
    bad("width", "calc(1px) calc(2px)"); // two values where one belongs
    bad("width", "notafunction(1px)");
    bad("display", "calc(1px)"); // a keyword-only property takes no math at all
    // AN UNTERMINATED FUNCTION IS CLOSED BY EOF, CSS Syntax 3 §5.4.9, so these
    // are VALUES and refusing them deleted the declaration. It is not a corner
    // case: `css/css-values/minmax-length-computed` writes the second one four
    // times over and expects 40px.
    ok("width", "calc(1px", "calc(1px)");
    ok("width", "calc(min(1em, 21px) * 2", "calc(min(1em, 21px) * 2");

    // THE ARITY IS PART OF THE GRAMMAR. `round-mod-rem-invalid` and
    // `calc-invalid-parsing` are one assertion per line and this is what they
    // say: a step is required and a fourth argument is not a thing.
    bad("width", "round(nearest, 1px)");
    bad("width", "round(nearest, 1px, 1px, 1px)");
    bad("width", "clamp(1px, 2px)");
    bad("width", "mod(1px)");
    ok("width", "round(nearest, 10px, 6px)", "calc(12px)");

    // EITHER OF clamp()'S BOUNDS MAY BE `none`, CSS Values 4 §10.3, and an
    // absent bound is an unbounded side rather than a missing argument:
    // `clamp(none, 33px, 30px)` is `min(33px, 30px)`. It was a syntax error here
    // and the declaration went with it - `clamp-length-serialize` is six
    // assertions of exactly this shape. The MIDDLE argument is still required.
    ok("width", "clamp(none, 33px, 30px)", "calc(30px)");
    ok("width", "clamp(33px, 30px, none)", "calc(33px)");
    ok("width", "clamp(none, 30px, none)", "calc(30px)");
    bad("width", "clamp(none, none, none)");
    bad("width", "clamp(1px, none, 2px)");

    // ...AND SO IS THE TYPE ALGEBRA, CSS Values 4 §10.2. Every one of these is
    // `calc-unit-analysis` verbatim.
    bad("margin-left", "calc(0)");       // a unitless zero in a calc is a NUMBER
    bad("margin-left", "calc(1px + 2)"); // a length plus a number
    bad("margin-left", "calc(2 + 1px)");
    bad("margin-left", "calc(1px - 2)");
    bad("margin-left", "calc(2 - 1px)");
    bad("margin-left", "calc(2px * 1px)");    // an area, which no property takes
    bad("margin-left", "calc(20 / 0.75rem)"); // division by a non-number
    ok("margin-left", "calc(0px)", "calc(0px)");
    ok("margin-left", "calc(2px * 2)", "calc(4px)");
    ok("margin-left", "calc(2 * 2px)", "calc(4px)");
    // The same rule the other way round: a math function is refused where its
    // RESOLVED TYPE is not a value. `width: calc(2 * 3)` was already invalid in
    // the cascade and is now invalid in the CSSOM, which is where the corpus
    // looks.
    bad("width", "calc(2 * 3)");
    bad("rotate", "calc(1s)");
    bad("transition-duration", "calc(1px)");
    bad("letter-spacing", "calc(10%)"); // <length>, not <length-percentage>
    ok("text-indent", "calc(10%)", "calc(10%)");
    ok("rotate", "calc(45deg + 45deg)", "calc(90deg)");
    ok("transition-duration", "calc(1s / 2)", "calc(0.5s)");
    ok("opacity", "calc(2 / 4)", "calc(0.5)");
    ok("z-index", "calc(1 + 1)", "calc(2)");

    // A `<flex>` IS A TYPE, NOT AN UNRESOLVED LENGTH. `fr` has no basis here and
    // never will - a flex is sized by grid track resolution and by nothing else -
    // but treating it as "a unit I cannot resolve" made `min(1px, 0fr)` a value
    // that survived, where it is `1px + 2` with different spelling. Six corpus
    // files say so at once: `minmax-{length,number,percentage,time}-invalid`,
    // `minmax-length-percent-invalid` and `exp-log-invalid`.
    bad("width", "min(0fr)");
    bad("width", "min(1px, 0fr)");
    bad("opacity", "max(1, 0fr)");
    bad("opacity", "exp(0fr)");
    bad("transition-delay", "min(1s, 0fr)");
    bad("margin-left", "calc(1px + 1fr)");
    // ...and two flexes add up perfectly well - the family was what was missing,
    // not the arithmetic - but their sum is still not a length.
    bad("margin-left", "calc(1fr + 1fr)");

    // A BAD CALC ANYWHERE IN THE VALUE, not only when it is the whole of it.
    // `transform` has no grammar in this table at all, and the corpus still
    // expects the declaration refused.
    bad("transform", "rotate(calc((0.25turn error)))");
    bad("width", "calc([])");
    bad("width", "calc(7px * up)");
}

// CSS Values 4 §10.9's numeric constants, and §10.4-§10.8's function set. None
// of these parsed before: `infinity` and `NaN` are keywords rather than tokens,
// and fourteen of the functions did not exist here at all.
void test_the_rest_of_the_math_functions() {
    ok("opacity", "calc(infinity)", "calc(infinity)");
    ok("opacity", "calc(NaN)", "calc(NaN)");
    ok("opacity", "calc(pi)", "calc(3.141593)");
    ok("opacity", "calc(e)", "calc(2.718282)");
    // A DIVISION BY ZERO IS AN INFINITY, not a syntax error - §10.9 - and an
    // infinity keeps its calc() and moves the unit to a multiplier, because
    // `infinitypx` is not a token.
    ok("width", "calc(100px / 0)", "calc(infinity * 1px)");
    ok("opacity", "calc(0 / 0)", "calc(NaN)");
    ok("width", "abs(-10px)", "calc(10px)");
    ok("z-index", "sign(1em - 1px)", "sign(1em - 1px)"); // a length's sign is a NUMBER
    ok("width", "hypot(3px, 4px)", "calc(5px)");
    ok("width", "calc(1px * pow(2, 3))", "calc(8px)");
    ok("opacity", "sqrt(4)", "calc(2)");
    ok("opacity", "log(10, 10)", "calc(1)");
    ok("opacity", "exp(0)", "calc(1)");
    ok("opacity", "cos(0)", "calc(1)");          // an angle in, a number out
    ok("rotate", "atan2(1, 1)", "calc(45deg)");  // ...and a number in, an angle out
    ok("rotate", "calc(1turn)", "calc(360deg)"); // canonical units, CSS Values 4 §6.5
    ok("transition-duration", "calc(250ms)", "calc(0.25s)");
    ok("width", "mod(10px, 6px)", "calc(4px)");
    ok("width", "rem(10px, 6px)", "calc(4px)");
    ok("width", "round(up, 101px, 10px)", "calc(110px)");
    ok("width", "round(down, 106px, 10px)", "calc(100px)");
    ok("width", "round(to-zero, 105px, 10px)", "calc(100px)");
    // `mod` takes the sign of the DIVISOR and `rem` the sign of the dividend,
    // which is the whole difference between them - §10.6.
    ok("opacity", "mod(-18, 5)", "calc(2)");
    ok("opacity", "rem(-18, 5)", "calc(-3)");
    // A CONSTANT IS NOT A VALUE ON ITS OWN. `infinity` and `NaN` are not
    // <number-token>s, which is the whole reason §10.9 spells them as keywords
    // usable only inside a math function. It is asked of `opacity` rather than
    // of `scale` because `scale` is `freeform` and a freeform property accepts
    // any value at all - which is the table being conservative, not a bug.
    bad("opacity", "infinity");
    bad("width", "sign(1px)"); // a number where the property's value is a length

    // A FUNCTION THIS FILE CANNOT EVALUATE IS NOT AN ERROR. Every one of these
    // is `test_valid_value` in the corpus, and calling them invalid deleted 34
    // declarations across `css/css-values/tree-counting/`.
    ok("left", "calc(1px * sibling-index())", "calc(1px * sibling-index())");
    ok("left", "calc(inherit(--x) + 1px)", "calc(inherit(--x) + 1px)");
    ok("width", "calc-size(10px, sign(size) * size)", "calc-size(10px, sign(size) * size)");
    // A SIMPLIFIED SPECIFIED VALUE ONLY WHERE EVERY UNIT IS CONTEXT-FREE. CSS
    // Values 4 §10.11 keeps `1em` and `5%` as written because neither has a
    // basis yet, and this file has one evaluator rather than two, so it declines
    // to simplify at all when either appears. `calc(1px + 2px)` above is the
    // case that DOES simplify.
    ok("width", "calc(1em + 10px)", "calc(1em + 10px)");
    ok("width", "calc(100% - 10px)", "calc(100% - 10px)");
    // ...and neither is a unit the specification names and this engine has no
    // basis for. `1cqw` needs a container and `1lh` a line box; both are values.
    ok("width", "calc(1px + 3cqw)", "calc(1px + 3cqw)");
    ok("width", "calc(1px + 1lh)", "calc(1px + 1lh)");
    bad("width", "calc(1px + 1nonsense)"); // a typo is not a unit
}

void test_important_and_the_empty_value() {
    // `!important` is a DECLARATION's business, never a value's. CSSOM's
    // setProperty takes the priority as its own argument, and the IDL setter
    // (`el.style.width = "1px !important"`) has to refuse it - which is what
    // the default `allow_important = false` says.
    bad("color", "red !important");
    bad("width", "10px !important");
    // ...but a `style` ATTRIBUTE may carry one, and there refusing it would
    // DROP the declaration: an inline `width: 100px !important` that stops
    // applying is far worse than a mis-reported priority.
    {
        const auto answer = check_declaration("width", "100px !important", true);
        CHECK(answer.valid);
        CHECK(answer.important);
        CHECK_EQ(answer.serialized, std::string{"100px"});
    }
    {
        const auto answer = check_declaration("width", "100px", true);
        CHECK(answer.valid);
        CHECK(!answer.important);
    }
    // The value is still validated with the priority removed, not before it.
    CHECK(!check_declaration("width", "100 !important", true).valid);
    CHECK(supports_condition("(width: 10px !important)"));
    // A FUNCTION THIS ENGINE CANNOT EVALUATE IS NOT SUPPORT, even though
    // `el.style` still stores the value. Five `css/css-values` files guard
    // their assertions on `CSS.supports` and went from passing vacuously to
    // running and failing when this said yes to everything.
    CHECK(!supports_declaration("content", "attr(data-foo)"));
    CHECK(!supports_declaration("font-family", "random-item(auto, serif)"));
    CHECK(!supports_declaration("background-color", "var(--x type(*))"));
    CHECK(supports_declaration("width", "var(--w)"));
    CHECK(supports_declaration("width", "calc(1px + 2px)"));
    CHECK(supports_declaration("background-color", "rgb(1, 2, 3)"));
    // ...and the value is still STORED, which is the difference between the two
    // questions this file answers.
    CHECK(check_declaration("content", "attr(data-foo)").valid);
    CHECK(check_declaration("content", "attr(data-foo)").uses_unknown_function);
    // An empty value is reported invalid because both callers want the same
    // thing from it - store nothing - and `el.style.width = ""` is how a page
    // removes a declaration.
    bad("width", "");
    bad("width", "   ");
}

void test_a_custom_property_takes_anything_that_tokenises() {
    ok("--x", "anything at all", "anything at all");
    ok("--x", "10px", "10px");
    // ...and its NAME is case-sensitive, unlike every other property, which is
    // why `css_name_of` leaves it alone.
    CHECK_EQ(css_name_of("--myVar"), std::string{"--myVar"});
    CHECK_EQ(idl_name_of("--myVar"), std::string{"--myVar"});
}

void test_the_two_spellings_of_one_property() {
    CHECK_EQ(css_name_of("backgroundColor"), std::string{"background-color"});
    CHECK_EQ(css_name_of("background-color"), std::string{"background-color"});
    CHECK_EQ(css_name_of("zIndex"), std::string{"z-index"});
    CHECK_EQ(idl_name_of("background-color"), std::string{"backgroundColor"});
    CHECK_EQ(idl_name_of("z-index"), std::string{"zIndex"});
    // THE TWO EXCEPTIONS CSSOM NAMES BY HAND, and both had been wrong in both
    // copies of this conversion: a prefixed property's IDL name drops the
    // leading dash, and `float` is a reserved word in the IDL.
    CHECK_EQ(css_name_of("webkitTransform"), std::string{"-webkit-transform"});
    CHECK_EQ(idl_name_of("-webkit-transform"), std::string{"webkitTransform"});
    CHECK_EQ(css_name_of("cssFloat"), std::string{"float"});
    CHECK_EQ(idl_name_of("float"), std::string{"cssFloat"});
}

void test_the_property_table_itself() {
    CHECK(find_property("width") != nullptr);
    CHECK(find_property("WIDTH") != nullptr); // property names fold
    CHECK(find_property("wdith") == nullptr);
    CHECK(known_properties().size() > 100);
    // The shorthands are marked, because CSSOM enumerates the LONGHANDS and
    // answers `in` for both. This used to be a second list in
    // `computed_style.cpp`, which is one list too many.
    CHECK(find_property("margin")->shorthand);
    CHECK(find_property("border-top")->shorthand);
    CHECK(!find_property("margin-top")->shorthand);
    CHECK(!find_property("width")->shorthand);
    // Every entry has an initial value, because `getComputedStyle` answers it
    // for a property nothing declared - and an empty one there reads as
    // `undefined` to a page, which is the failure this table exists to end.
    for (const auto & one : known_properties()) {
        CHECK(!one.name.empty());
        if (one.name != "font") { CHECK(!one.initial.empty()); }
    }
}

void test_css_supports() {
    CHECK(supports_declaration("width", "10px"));
    CHECK(!supports_declaration("width", "10"));
    // An UNKNOWN property is false here even though `el.style` stores it: §5 of
    // CSS Conditional 3 asks whether the declaration would be DROPPED, and one
    // naming a property the engine does not implement would be.
    CHECK(!supports_declaration("nonsense-property", "1px"));
    CHECK(supports_declaration("--x", "anything"));

    CHECK(supports_condition("(width: 10px)"));
    CHECK(!supports_condition("(width: 10)"));
    CHECK(supports_condition("not (width: 10)"));
    CHECK(supports_condition("(width: 10px) and (display: flex)"));
    CHECK(!supports_condition("(width: 10px) and (display: flexx)"));
    CHECK(supports_condition("(width: 10px) or (display: flexx)"));
    CHECK(supports_condition("((width: 10px))"));
    // A bare declaration with no parentheses is not a <supports-condition>.
    CHECK(!supports_condition("width: 10px"));
    CHECK(!supports_condition(""));
    // `!important` is part of a <declaration> and does not change the answer.
    CHECK(supports_condition("(width: 10px !important)"));
    // A FUNCTION THIS ENGINE CANNOT EVALUATE IS NOT SUPPORT, even though
    // `el.style` still stores the value. Five `css/css-values` files guard
    // their assertions on `CSS.supports` and went from passing vacuously to
    // running and failing when this said yes to everything.
    CHECK(!supports_declaration("content", "attr(data-foo)"));
    CHECK(!supports_declaration("font-family", "random-item(auto, serif)"));
    CHECK(!supports_declaration("background-color", "var(--x type(*))"));
    CHECK(supports_declaration("width", "var(--w)"));
    CHECK(supports_declaration("width", "calc(1px + 2px)"));
    CHECK(supports_declaration("background-color", "rgb(1, 2, 3)"));
    // ...and the value is still STORED, which is the difference between the two
    // questions this file answers.
    CHECK(check_declaration("content", "attr(data-foo)").valid);
    CHECK(check_declaration("content", "attr(data-foo)").uses_unknown_function);
}

} // namespace

int main() {
    test_lengths_and_the_unitless_zero();
    test_numbers_integers_and_keywords();
    test_the_css_wide_keywords_apply_to_everything();
    test_the_things_that_must_survive();
    test_what_a_math_function_may_not_be();
    test_the_rest_of_the_math_functions();
    test_important_and_the_empty_value();
    test_a_custom_property_takes_anything_that_tokenises();
    test_the_two_spellings_of_one_property();
    test_the_property_table_itself();
    test_css_supports();
    REPORT("css_values");
}
