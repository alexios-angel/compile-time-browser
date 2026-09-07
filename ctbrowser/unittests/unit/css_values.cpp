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
    bad("width", "round()");             // the corpus case, verbatim
    bad("width", "calc(1px");            // unbalanced
    bad("width", "calc(1px) calc(2px)"); // two values where one belongs
    bad("width", "notafunction(1px)");
    bad("display", "calc(1px)"); // a keyword-only property takes no math at all
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
    test_important_and_the_empty_value();
    test_a_custom_property_takes_anything_that_tokenises();
    test_the_two_spellings_of_one_property();
    test_the_property_table_itself();
    test_css_supports();
    REPORT("css_values");
}
