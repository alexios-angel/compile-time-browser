#include "css_values/math.hpp"

namespace {

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
    ok("color", "#0d6efd", "rgb(13, 110, 253)");    // a hex colour is a legacy sRGB colour
    ok("width", "var(--w)", "var(--w)");            // substitution defers the answer
    ok("width", "calc(100% - 10px)", "calc(100% - 10px)");
    ok("width", "clamp(1rem, 2vw, 3rem)", "clamp(1rem, 2vw, 3rem)");
    // THE AUTHOR'S BYTES for anything the grammar does not model, spacing and
    // all. `test_valid_value` asserts the round-trip exactly, and normalising a
    // value whose grammar is UNKNOWN turned two passing `css-values` files into
    // failing ones on 2026-09-07. A math function's grammar is known, so its
    // argument list is re-serialised and `min(10px,5%)` gains its space.
    ok("width", "min(10px, 5%)", "min(10px, 5%)");
    ok("width", "min(10px,5%)", "min(10px, 5%)");
    ok("font-family", "random-item(auto ,serif)", "random-item(auto ,serif)");
    // A FONT FAMILY LOSES ITS QUOTES when the name is an identifier sequence
    // and keeps them, doubled, when it is not - CSSOM's serialize-a-string
    // rule for `font-family` in particular, and the same answer the computed
    // value gives (css/cssom's serialize-values and font-family-serialization-001).
    ok("font-family", "\"Helvetica Neue\", sans-serif", "Helvetica Neue, sans-serif");
    ok("font-family", "'Lucida Grande'", "Lucida Grande");
    ok("font-family", "Arial", "Arial");
    ok("font-family", "'34J', \"serif\", 'A  B'", "\"34J\", \"serif\", \"A  B\"");
    // A NEGATIVE ZERO IS `0` ON ITS OWN AND `-0` INSIDE A MATH FUNCTION, where
    // `1 / sign(-0)` still has to come out as -infinity (signed-zero).
    ok("scale", "-0", "0");
    ok("scale", "clamp(-1, 1 / sign(calc(-0)), 1)", "calc(-1)");
    ok("scale", "clamp(-1, 1 / sign(min(-0, 0)), 1)", "calc(-1)");
    ok("scale", "sign(-0)", "calc(0)");
    ok("width", "min(-0%, 0%)", "min(0%, 0%)"); // ...but a percentage's zero has no sign
    // `decimal` IS THE DEFAULT COUNTER STYLE and CSSOM does not write it.
    ok("content", "counter(par-num, decimal)", "counter(par-num)");
    ok("content", "counters(par-num, \".\", DECIMAL )", "counters(par-num, \".\")");
    ok("content", "counter(par-num, upper-roman)", "counter(par-num, upper-roman)");
    ok("content", "counter(par-num)", "counter(par-num)");
    // AN ARBITRARY SUBSTITUTION FUNCTION IS A VALUE FOR ANY PROPERTY, CSS
    // Values 5: what its arguments mean is decided after parsing, so a length
    // grammar has no business refusing one.
    ok("width", "random-item(auto, 1px, 2px, 3px)", "random-item(auto, 1px, 2px, 3px)");
    ok("left", "inherit(--x)", "inherit(--x)");
    ok("left", "inherit(--x,)", "inherit(--x,)");
    ok("view-transition-name", "ident( myident)", "ident( myident)"); // and NOT respaced
    ok("view-transition-name", "ident(rgb(1, 2, 3))", "ident(rgb(1, 2, 3))");
    // ...but its ARGUMENT LIST is known now, and two of them have one worth
    // checking. `ident( <declaration-value> )` takes one argument and not an
    // empty one; `inherit()` takes a custom property name and an optional
    // fallback after a comma.
    bad("view-transition-name", "ident()");
    bad("view-transition-name", "ident( )");
    bad("view-transition-name", "ident({})");
    bad("view-transition-name", "ident(a, b)");
    bad("left", "inherit(, foo)");
    bad("left", "inherit(!!, foo)");

    // An UNKNOWN property is stored, not refused: CSSOM lets a page set one.
    ok("-webkit-line-clamp", "3", "3");
    ok("scroll-snap-type", "x mandatory", "x mandatory");
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
    // running and failing when this said yes to everything. The substitution
    // functions are all performed now, so all six are support.
    CHECK(supports_declaration("content", "attr(data-foo)"));
    CHECK(supports_declaration("font-family", "random-item(auto, serif)"));
    CHECK(supports_declaration("color", "random-item(fixed 0, rgb(1, 0, 0), rgb(0, 1, 0))"));
    CHECK(!supports_declaration("background-image", "image-set(url(a.png) 1x)"));
    CHECK(!supports_declaration("background-color", "var(--x type(*))"));
    CHECK(supports_declaration("width", "var(--w)"));
    CHECK(supports_declaration("width", "calc(1px + 2px)"));
    CHECK(supports_declaration("background-color", "rgb(1, 2, 3)"));
    // `random()` is a math function (CSS Values 5 §random), and random-computed
    // guards a hundred and forty assertions on this answer.
    CHECK(supports_declaration("width", "random(0px, 100px)"));
    CHECK(supports_declaration("scale", "random(--foo element-scoped, 2, 12)"));
    // A `!` INSIDE A BLOCK IS A DELIM, not a priority: `if(style(--x!): a; else:
    // b)` is a value whose condition is false (if-conditionals 39, 117, 118).
    CHECK(check_declaration("--p", "if(style(--x!): 1px; else: 2px)").valid);
    CHECK(check_declaration("width", "calc(1px * var(--x, a!b))").valid);
    // ...and var()'s first argument is exactly one custom property name
    // (var-parsing.html).
    CHECK(!check_declaration("width", "var(--x!)").valid);
    CHECK(!check_declaration("width", "var()").valid);
    CHECK(!check_declaration("width", "var({})").valid);
    CHECK(!check_declaration("width", "var(, 10px)").valid);
    CHECK(!check_declaration("width", "var(--x {--y}, 10px)").valid);
    CHECK(check_declaration("width", "var(--x,)").valid);
    CHECK(check_declaration("width", "var(--x, var(--y, 1px))").valid);
    CHECK(!check_declaration("--p", "1px !").valid);
    CHECK(!check_declaration("width", "1px !").valid);
    // ...and the value is still STORED, which is the difference between the two
    // questions this file answers.
    CHECK(check_declaration("content", "attr(data-foo)").valid);
    CHECK(check_declaration("background-image", "image-set(url(a.png) 1x)").valid);
    CHECK(check_declaration("background-image", "image-set(url(a.png) 1x)").uses_unknown_function);
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
        // `font` and `all` are the two shorthands with no one initial value.
        if (one.name != "font" && one.name != "all") { CHECK(!one.initial.empty()); }
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
    // `selector()` is supported when it parses (if-conditionals 176, 177).
    CHECK(supports_condition("selector(h2 > p)"));
    CHECK(supports_condition("(selector(h2 > p))"));
    CHECK(!supports_condition("selector(h2 >)"));
    CHECK(supports_condition("not selector(h2 >)"));
    // A bare declaration with no parentheses is not a <supports-condition>.
    CHECK(!supports_condition("width: 10px"));
    CHECK(!supports_condition(""));
    // `!important` is part of a <declaration> and does not change the answer.
    CHECK(supports_condition("(width: 10px !important)"));
    // A FUNCTION THIS ENGINE CANNOT EVALUATE IS NOT SUPPORT, even though
    // `el.style` still stores the value; a substitution it performs is.
    CHECK(supports_declaration("content", "attr(data-foo)"));
    CHECK(!supports_declaration("background-image", "image-set(url(a.png) 1x)"));
    CHECK(!supports_declaration("background-color", "var(--x type(*))"));
    CHECK(supports_declaration("width", "var(--w)"));
    CHECK(supports_declaration("width", "calc(1px + 2px)"));
    CHECK(supports_declaration("background-color", "rgb(1, 2, 3)"));
    // ...and the value is still STORED, which is the difference between the two
    // questions this file answers.
    CHECK(check_declaration("background-image", "image-set(url(a.png) 1x)").valid);
    CHECK(check_declaration("background-image", "image-set(url(a.png) 1x)").uses_unknown_function);
}

// AN `<integer>` PROPERTY ROUNDS ITS MATH, and CSS Values 4 §10.10 says which
// way. `z-index: calc(3 / 2)` is valid CSS whose computed value is 2, and this
// engine reported `1.5` - a number no `<integer>` property has ever taken.
//
// The direction is the whole of the difference: a value exactly halfway goes
// toward POSITIVE INFINITY, so `calc(-3 / 2)` is -1 and not -2. `std::round`
// would have got every negative half wrong, and
// `css/css-values/calc-z-index-fractions-001.html` is six subtests of nothing
// else. `calc-integer.html` is the other three.
void test_an_integer_property_rounds_its_math() {
    using ctbrowser::style::css::fold_math;
    using ctbrowser::style::css::length_context;
    using ctbrowser::style::css::math_context;
    using ctbrowser::style::css::math_context_of;

    const length_context ctx;
    const auto whole = [&](std::string_view value) {
        return fold_math(value, ctx, math_context::integer).text;
    };
    CHECK(math_context_of("z-index") == math_context::integer);
    CHECK(math_context_of("order") == math_context::integer);

    CHECK_EQ(whole("calc(2)"), std::string{"2"});
    CHECK_EQ(whole("calc(4 / 2)"), std::string{"2"});
    CHECK_EQ(whole("calc(1 / 2)"), std::string{"1"}); // a half rounds UP
    CHECK_EQ(whole("calc(0.5)"), std::string{"1"});
    CHECK_EQ(whole("calc(1 / 3)"), std::string{"0"}); // ...and a third rounds down
    CHECK_EQ(whole("calc(6 / 2.0)"), std::string{"3"});
    // The six of calc-z-index-fractions-001, and the four negatives are the
    // ones that say "toward positive infinity" rather than "away from zero".
    CHECK_EQ(whole("calc(2.5 / 2)"), std::string{"1"});
    CHECK_EQ(whole("calc(3 / 2)"), std::string{"2"});
    CHECK_EQ(whole("calc(3.5 / 2)"), std::string{"2"});
    CHECK_EQ(whole("calc(-2.5 / 2)"), std::string{"-1"});
    CHECK_EQ(whole("calc(-3 / 2)"), std::string{"-1"});
    CHECK_EQ(whole("calc(-3.5 / 2)"), std::string{"-2"});
    // ONLY THE FINISHED CONVERSION ROUNDS. A nested calc is an intermediate and
    // rounding it would make this 0.
    CHECK_EQ(whole("calc(calc(1 / 3) * 3)"), std::string{"1"});
    // Nothing else moves: the context is per-property, so the same expression in
    // `opacity` keeps its fraction.
    CHECK_EQ(fold_math("calc(1 / 2)", ctx, math_context::any).text, std::string{"0.5"});
    // A specified value is NOT rounded - CSS Values 4 §10.12 keeps the function
    // as written, and `el.style.zIndex` reads back the simplified form.
    ok("z-index", "calc(3 / 2)", "calc(1.5)");
    ok("z-index", "2", "2");
    bad("z-index", "1.5");
}

// `random-item( <declaration-value>, [ <declaration-value>? ]# )`, CSS Values 5.
// The function is an arbitrary substitution one, so only its ARGUMENT LIST is
// decided at parse time - but it is decided, and `random-item-invalid` is
// fourteen assertions of exactly that.
void test_the_random_item_argument_list() {
    // The key is required, and so is the comma after it.
    bad("font-family", "random-item()");
    bad("font-family", "random-item( )");
    bad("font-family", "random-item(auto)");
    bad("font-family", "random-item(, serif, sans-serif)");
    // `<declaration-value>` forbids a top-level `;` or `!`.
    bad("font-family", "random-item(auto, !)");
    bad("font-family", "random-item(auto, ;)");
    // AN UNMATCHED BRACKET, which is why this MATCHES brackets rather than
    // counting them: `{serif)` closes a brace with a paren, and a depth counter
    // reads that as balanced.
    bad("font-family", "random-item(auto, })");
    bad("font-family", "random-item(auto, ])");
    bad("font-family", "random-item(auto, {serif)");
    bad("font-family", "random-item(auto, serif})");
    bad("font-family", "random-item(auto, {Times, serif)");
    bad("font-family", "random-item({auto, serif, sans-serif)");
    // A `{}` block is how an ITEM containing a comma is written, so it is the
    // whole item or it is not an item at all.
    bad("font-family", "random-item(auto, {Times, serif} extra)");
    bad("font-family", "random-item(auto, extra {Times, serif})");

    // ...and every one of these must still SURVIVE, spacing and all.
    ok("font-family", "random-item(auto ,serif)", "random-item(auto ,serif)");
    ok("width", "random-item(auto, 1px, 2px, 3px)", "random-item(auto, 1px, 2px, 3px)");
    ok("font-family", "random-item(auto,)", "random-item(auto,)"); // an item may be EMPTY
    ok("font-family", "random-item(fixed 0, rgb(4, 5, 6), blue)",
       "random-item(fixed 0, rgb(4, 5, 6), blue)");
    ok("font-family", "random-item(auto, {Times, serif}, sans-serif)",
       "random-item(auto, {Times, serif}, sans-serif)");
    // EOF CLOSES EVERY OPEN BLOCK, CSS Syntax 3 §5.4.9, so an unterminated one
    // is a value and not a parse error.
    ok("font-family", "random-item(auto, serif", "random-item(auto, serif)");
}

// `interpolate-size` is a real property with a real two-keyword grammar. As an
// UNKNOWN one `el.style` stored `interpolate-size: 100%` and `getComputedStyle`
// did not publish the property at all - which is the two assertions of
// `calc-size/interpolate-size-computed.html` and three of `-parsing.html`.
// calc-size-parsing: the basis is judged against the property, the
// calculation is a length over `size`, and both are written canonically.
void test_calc_size() {
    ok("width", "calc-size(auto, size)", "calc-size(auto, size)");
    ok("min-height", "calc-size(auto, size)", "calc-size(auto, size)");
    bad("max-width", "calc-size(auto, size)");
    bad("width", "calc-size(none, size)");
    bad("max-width", "calc-size(none, size)");
    ok("max-height", "calc-size(max-content, size)", "calc-size(max-content, size)");
    ok("height", "calc-size(min-content, size * 2)", "calc-size(min-content, 2 * size)");
    ok("max-width", "calc-size(max-content, size / 2)", "calc-size(max-content, 0.5 * size)");
    ok("max-height", "calc-size(fit-content, 30px + size / 2)",
       "calc-size(fit-content, 30px + (0.5 * size))");
    ok("width", "calc-size(fit-content, 50% + size / 2)",
       "calc-size(fit-content, 50% + (0.5 * size))");
    ok("width", "calc-size(any, 25em)", "calc-size(any, 25em)");
    ok("width", "calc-size(any, 40%)", "calc-size(any, 40%)");
    ok("width", "calc-size(any, 50px + 30%)", "calc-size(any, 30% + 50px)");
    ok("width", "calc-size(calc-size(any, 30px), size)", "calc-size(calc-size(any, 30px), size)");
    bad("width", "calc-size(any, size)");
    bad("width", "calc-size(any, fit-content)");
    ok("width", "calc-size(10px, sign(size) * size)", "calc-size(10px, sign(size) * size)");
    bad("width", "size");
    bad("width", "calc-size(any, calc-size(10px, sign(size) * size))");
    bad("width", "calc(calc-size(auto, size))");
    ok("width", "calc-size(calc-size(2in, 30px), 25em)", "calc-size(calc-size(192px, 30px), 25em)");
    ok("width", "calc-size(calc-size(min-content, size), size)",
       "calc-size(calc-size(min-content, size), size)");
    bad("width", "calc-size(30px)");
    bad("width", "calc-size(any)");
    bad("width", "calc-size(calc-size(fit-content, size * 2))");
    ok("flex-basis", "calc-size(content, size)", "calc-size(content, size)");
    bad("width", "calc-size(content, size)");
    ok("width", "calc-size(0px, 0px)", "calc-size(0px, 0px)");
    bad("width", "calc-size(0, 0px)");
    bad("width", "calc-size(0px, 0)");
}

// random-serialize: a specified random() spells its key - the dashed name,
// `element-scoped`, and the UA ident the scoping words mean - and its bounds
// in canonical units.
void test_random_spells_its_key() {
    ok("width", "random(0px, 100px)", "random(element-scoped ua-width-1, 0px, 100px)");
    ok("height", "random(auto, 0px, 100px)", "random(element-scoped ua-height-1, 0px, 100px)");
    ok("width", "random(fixed 0.5, 0px, 100px)", "random(fixed 0.5, 0px, 100px)");
    ok("width", "random(--foo, 0px, 100px)", "random(--foo, 0px, 100px)");
    ok("width", "random(--foo element-scoped, 0px, 100px)",
       "random(--foo element-scoped, 0px, 100px)");
    ok("width", "random(element-scoped, 0px, 100px)", "random(element-scoped, 0px, 100px)");
    ok("font-size", "random(property-scoped, 0px, 100px)", "random(ua-font-size, 0px, 100px)");
    ok("width", "random(--foo property-index-scoped, 0px, 100px)",
       "random(--foo ua-width-1, 0px, 100px)");
    ok("height", "random(property-scoped element-scoped, 0px, 100px)",
       "random(element-scoped ua-height, 0px, 100px)");
    ok("width", "random(ua-height-1 element-scoped, 10px, 20%)",
       "random(element-scoped ua-height-1, 10px, 20%)");
    ok("width", "random(10 * 100px, 200em / 2)",
       "random(element-scoped ua-width-1, 1000px, 100em)");
    ok("width", "random(fixed calc(2 / 4), 0px, 100px)", "random(fixed calc(0.5), 0px, 100px)");
    ok("rotate", "random(25deg, 1turn)", "random(element-scoped ua-rotate-1, 25deg, 360deg)");
    ok("transition-delay", "random(--foo, 25ms, 50s, 5s)", "random(--foo, 0.025s, 50s, 5s)");
    ok("margin", "random(0px, 1px) random(0px, 1px)",
       "random(element-scoped ua-margin-1, 0px, 1px) random(element-scoped ua-margin-2, 0px, 1px)");
    ok("width", "calc(2 * random(--foo, 0px, 100px))", "calc(2 * random(--foo, 0px, 100px))");
    // ...and the sharing grammar (random-invalid).
    ok("width", "random(--foo ua-width-1, 10px, 20%)", "random(--foo ua-width-1, 10px, 20%)");
    ok("width", "random(--foo ua-x element-scoped, 10px, 20%)",
       "random(--foo element-scoped ua-x, 10px, 20%)");
    bad("width", "random(--foo --bar, 1px, 2px)");
    bad("width", "random(fixed 0.5 auto, 1px, 2px)");
    bad("width", "random(fixed -1, 1px, 2px)");
    bad("width", "random(--foo element-scoped element-scoped, 1px, 2px)");
    bad("width", "random(property-scoped ua-width-1, 1px, 2px)");
    bad("width", "random(property-scoped property-index-scoped, 1px, 2px)");
    bad("width", "random(foo, 1px, 2px)");
    bad("width", "random(1px)");
}

// urls/url-request-modifiers-*: the modifiers in one order, the unknown ones
// dropped, duplicates and anything that is not an ident or a function refused.
void test_url_request_modifiers() {
    const std::string u = "url(\"a.png\"";
    ok("background-image", u + " cross-origin(anonymous))", u + " cross-origin(anonymous))");
    ok("background-image", u + " integrity(\"sha384-x\") cross-origin(anonymous))",
       u + " cross-origin(anonymous) integrity(\"sha384-x\"))");
    ok("background-image",
       u + " referrer-policy(no-referrer) integrity(\"sha384-x\") cross-origin(anonymous))",
       u + " cross-origin(anonymous) integrity(\"sha384-x\") referrer-policy(no-referrer))");
    ok("background-image", u + " integrity(\"\"))", u + " integrity(\"\"))");
    ok("background-image", u + " foobar(baz))", u + ")");
    ok("background-image", u + " foobar cross-origin(anonymous))", u + " cross-origin(anonymous))");
    ok("background-image", u + " foobar([brackets {braces}]) referrer-policy(same-origin))",
       u + " referrer-policy(same-origin))");
    ok("background-image", u + " crossorigin(anonymous))", u + ")");
    ok("background-image", u + " foobar foobar(42))", u + ")");
    bad("background-image", u + " cross-origin())");
    bad("background-image", u + " cross-origin(,))");
    bad("background-image", u + " cross-origin(anonymous,))");
    bad("background-image", u + " cross-origin(anonymous foobar))");
    bad("background-image", u + " cross-origin(anonymous) cross-origin(use-credentials))");
    bad("background-image", u + " integrity(sha384-x))");
    bad("background-image", u + " referrer-policy(no-referrer same-origin))");
    bad("background-image", u + " foobar(baz) foobar(qux))");
    bad("background-image", u + " FooBar foobar)");
    bad("background-image", u + " 42)");
    bad("background-image", u + " \"foobar\")");
    bad("background-image", "url(a.png cross-origin(anonymous))");
    CHECK(supports_declaration("background-image", u + " cross-origin(anonymous))"));
}

// calc-rounds-to-integer: `steps()` takes an <integer>, which `1e1` is not.
void test_steps_takes_an_integer() {
    ok("animation-timing-function", "steps(10)", "steps(10)");
    ok("transition-timing-function", "steps(calc(10.1))", "steps(calc(10.1))");
    ok("max-lines", "10", "10");
    ok("max-lines", "calc(10.1)", "calc(10.1)");
    bad("max-lines", "1e1");
    bad("hyphenate-limit-lines", "10.1");
    bad("animation-timing-function", "steps(1e1)");
    bad("animation-timing-function", "steps(10.1)");
    bad("transition-timing-function", "steps(1.1e1, start)");
    // ...and so do the <integer> slots of the freeform properties: a counter's
    // step, a grid line, repeat()'s count, a feature tag's value, and the
    // second value of initial-letter - but not its first, which is a <number>.
    ok("counter-increment", "foo 10", "foo 10");
    CHECK(check_declaration("counter-increment", "foo calc(1e1)").valid);
    bad("counter-increment", "foo 1e1");
    bad("counter-reset", "foo 10.1");
    bad("font-feature-settings", "\"liga\" 1.1e1");
    bad("grid-row", "1e1");
    ok("grid-template-rows", "repeat(10, 10px)", "repeat(10, 10px)");
    bad("grid-template-rows", "repeat(10.1, 10px)");
    ok("initial-letter", "1.1 10", "1.1 10");
    bad("initial-letter", "1.1 10.1");
    bad("text-combine-upright", "digits 1e1");
}

// A three-channel colour function takes three or four components.
void test_colour_function_arity() {
    bad("background-color", "rgb(0)");
    bad("color", "rgb(1, 2)");
    bad("color", "hsl(1 2 3 4 5)");
    ok("color", "rgb(1, 2, 3)", "rgb(1, 2, 3)");
    ok("color", "rgb(1 2 3 / 0.5)", "rgba(1, 2, 3, 0.5)");
    ok("color", "rgba(1, 2, 3, 0.5)", "rgba(1, 2, 3, 0.5)");
    ok("color", "rgb(calc(1 + 1) 2 3)", "rgb(2, 2, 3)");
    ok("color", "rgb(from red r g b)", "rgb(from red r g b)");
    ok("color", "color(srgb 1 0 0)", "color(srgb 1 0 0)");
}

void test_interpolate_size_is_a_property() {
    ok("interpolate-size", "numeric-only", "numeric-only");
    ok("interpolate-size", "allow-keywords", "allow-keywords");
    bad("interpolate-size", "auto");
    bad("interpolate-size", "none");
    bad("interpolate-size", "100%");
    CHECK(find_property("interpolate-size") != nullptr);
    CHECK(supports_declaration("interpolate-size", "numeric-only"));
    CHECK(!supports_declaration("interpolate-size", "auto"));
}

// `<position>`, CSS Values 5 - the one multi-component value this table models,
// and the only one whose canonical form REORDERS what the author wrote.
void test_the_position_grammar() {
    // ONE COMPONENT names one axis and the other is `center`, which is why a
    // per-token matcher cannot produce this: `top` is `center top` and `10%` is
    // `10% center`.
    ok("object-position", "10%", "10% center");
    ok("object-position", "left", "left center");
    ok("object-position", "top", "center top");
    ok("object-position", "center", "center center");
    ok("object-position", "x-start", "x-start center"); // level 5's logical keywords
    ok("object-position", "y-start", "center y-start");
    // TWO COMPONENTS are horizontal then vertical, and the `&&` branch lets two
    // KEYWORDS arrive the other way round - but only keywords, so `bottom right`
    // is a position and `10px right` is not.
    ok("object-position", "30px center", "30px center");
    ok("object-position", "40px top", "40px top");
    ok("object-position", "bottom right", "right bottom");
    ok("object-position", "center left", "left center");
    ok("object-position", "top center", "center top");
    ok("object-position", "10px y-start", "10px y-start");
    bad("object-position", "left right"); // two horizontals
    bad("object-position", "bottom 10%"); // a vertical keyword in the first slot
    // FOUR COMPONENTS are two `<side> <offset>` pairs, one per axis, in either
    // order - and `center` takes no offset, so it cannot appear in this form.
    ok("object-position", "right 30% top 60px", "right 30% top 60px");
    ok("object-position", "bottom 10% right 20%", "right 20% bottom 10%");
    ok("object-position", "y-end 20% left 10px", "left 10px y-end 20%");
    bad("object-position", "bottom 10% top 20%"); // both pairs vertical
    // THE THREE-VALUE FORM IS GONE in level 5. `left 4px top` is still a valid
    // `background-position` and is no longer a `<position>`, which is eight of
    // `position/position-invalid.tentative`'s twenty-one assertions.
    bad("object-position", "left 4px top");
    bad("object-position", "center left 1px");
    bad("object-position", "right 3% center");
    bad("object-position", "bottom right 8%");
    bad("object-position", "1px 2px 3px");
    bad("object-position", "auto");
    bad("object-position", "garbage left top");
    bad("object-position", "left 10px top 10px garbage");
    // ...and a math function anywhere in it keeps the author's bytes rather than
    // being refused: this reader does not evaluate the components.
    ok("object-position", "calc(50% - 1px) center", "calc(50% - 1px) center");
}

} // namespace

int main() {
    test_lengths_and_the_unitless_zero();
    test_numbers_integers_and_keywords();
    test_the_css_wide_keywords_apply_to_everything();
    test_the_things_that_must_survive();
    test_what_a_math_function_may_not_be();
    test_the_rest_of_the_math_functions();
    test_a_math_function_is_simplified_wherever_it_sits();
    test_the_percentage_half_of_simplification();
    test_a_percentage_needs_a_context();
    test_the_angle_functions_take_an_angle();
    test_a_sum_that_cannot_fold_still_has_an_order();
    test_a_percentage_needs_a_property_that_takes_one();
    test_important_and_the_empty_value();
    test_a_custom_property_takes_anything_that_tokenises();
    test_the_two_spellings_of_one_property();
    test_the_property_table_itself();
    test_css_supports();
    test_an_integer_property_rounds_its_math();
    test_the_random_item_argument_list();
    test_calc_size();
    test_random_spells_its_key();
    test_url_request_modifiers();
    test_steps_takes_an_integer();
    test_colour_function_arity();
    test_interpolate_size_is_a_property();
    test_the_position_grammar();
    REPORT("css_values");
}
