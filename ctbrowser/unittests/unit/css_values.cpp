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
    // value whose grammar is UNKNOWN turned two passing `css-values` files into
    // failing ones on 2026-09-07. A math function's grammar is known, so its
    // argument list is re-serialised and `min(10px,5%)` gains its space.
    ok("width", "min(10px, 5%)", "min(10px, 5%)");
    ok("width", "min(10px,5%)", "min(10px, 5%)");
    ok("font-family", "random-item(auto ,serif)", "random-item(auto ,serif)");
    ok("font-family", "\"Helvetica Neue\", sans-serif", "\"Helvetica Neue\", sans-serif");
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
    // times over and expects 40px. The paren EOF added is the author's by the
    // time anything here sees it, so it belongs in the serialisation -
    // `calc-complex-unresolved-serialize` asks for it on all six of its values.
    ok("width", "calc(1px", "calc(1px)");
    ok("width", "calc(min(1em, 21px) * 2", "calc(min(1em, 21px) * 2)");

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
    bad("border-left-width", "calc(10%)"); // <length>, not <length-percentage>
    ok("text-indent", "calc(10%)", "calc(10%)");
    ok("letter-spacing", "calc(100%)", "calc(100%)"); // CSS Text 4 gave it a percentage
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
    // progress( [no-clamp]? A, B, C ), CSS Values 5. Three arguments of one type
    // and a <number> out. AN EMPTY RANGE IS NEITHER AN ERROR NOR A NaN: the
    // unclamped form keeps the numerator's sign and the clamped one is nought
    // whichever way it points, because there is no range to be anywhere in.
    ok("opacity", "progress(100px, 0px, 100px)", "calc(1)");
    ok("opacity", "progress(1%, (10% - 10%), 100%)", "calc(0.01)"); // a ratio of two of them
    ok("opacity", "progress(-100px, 0px, 100px)", "calc(0)");       // clamped into [0, 1]
    ok("opacity", "progress(no-clamp -100px, 0px, 100px)", "calc(-1)");
    ok("opacity", "progress(2rad, 1rad, 1rad)", "calc(0)");
    ok("opacity", "progress(no-clamp 2rad, 1rad, 1rad)", "calc(infinity)");
    ok("opacity", "progress(no-clamp 1rad, 1rad, 1rad)", "calc(0)");
    ok("opacity", "progress(no-clamp 0rad, 1rad, 1rad)", "calc(-infinity)");
    ok("opacity", "progress(10em, 0px, 10em)", "progress(10em, 0px, 10em)"); // no basis yet
    bad("opacity", "progress(1)");
    bad("opacity", "progress(0, 1,)");
    bad("opacity", "progress(no-clamp, 1, 0 1)");
    bad("opacity", "progress(1 no-clamp, 0, 1)");
    bad("opacity", "progress(5, 0deg, 8deg)");
    // A MIXED PERCENTAGE IS A TYPE ERROR HERE and not an undecidable comparison:
    // three arguments that do not agree on what they measure have no ratio.
    bad("opacity", "progress(5%, 0px, 10px)");
    bad("letter-spacing", "calc(1px * progress(10deg, 0, 10))");
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
    // A UNIT WITH NO BASIS KEEPS ITS TERM, AND ITS TERM KEEPS ITS PLACE. CSS
    // Values 4 §10.11 does not resolve `1em` or `5%` here, so both survive to
    // the specified value; §10.13 says what order they survive in.
    ok("width", "calc(1em + 10px)", "calc(1em + 10px)");
    ok("width", "calc(100% - 10px)", "calc(100% - 10px)");
    // ...and a unit the specification names and this engine has no basis for is
    // the same case: `1cqw` needs a container and `1lh` a line box.
    ok("width", "calc(1px + 3cqw)", "calc(3cqw + 1px)");
    ok("width", "calc(1px + 1lh)", "calc(1lh + 1px)");
    bad("width", "calc(1px + 1nonsense)"); // a typo is not a unit
}

// CSS Values 4 §10.12: A MATH FUNCTION'S SPECIFIED VALUE IS ITS SIMPLIFIED FORM,
// wherever the function sits.
//
// "Wherever" is the half that was missing, and it is not a corner: the corpus
// tests every one of the sixteen functions through `transform`,
// `background-image` and `scale`, which are properties whose grammar this table
// does not model at all - so it asked "is the WHOLE value one math function",
// answered no, and handed back the author's text. That is ~290 assertions across
// `acos-asin-atan-atan2-serialize`, `sin-cos-tan-serialize`, `exp-log-serialize`,
// `hypot-pow-sqrt-serialize`, `round-mod-rem-serialize`, `signs-abs-serialize`,
// `minmax-number-serialize` and three of the `calc-infinity-nan-serialize-*`.
void test_a_math_function_is_simplified_wherever_it_sits() {
    // Inside a function of a property with no grammar here at all.
    ok("transform", "rotate(acos(1))", "rotate(calc(0deg))");
    ok("transform", "rotate(calc(1deg * NaN))", "rotate(calc(NaN * 1deg))");
    ok("transform", "scale(min(.3, .2, .1))", "scale(calc(0.1))");
    ok("transform", "translate(calc(1px + 2px), calc(2px * 2))", "translate(calc(3px), calc(4px))");
    ok("scale", "calc(sin(30deg) + cos(60deg))", "calc(1)");
    // Beside other values, and beside a quoted string whose parentheses must not
    // end the expression early.
    ok("border", "calc(calc(10px)) solid pink", "calc(10px) solid pink");
    ok("background-image", "image-set(url(\"a)b\") calc(1x * 2))",
       "image-set(url(\"a)b\") calc(2dppx))");

    // A calc() AROUND ONE OTHER calc() IS REDUNDANT and loses exactly one layer,
    // which is what §10.12's simplification does with a lone child.
    ok("margin-top", "calc(calc(0px + clamp(1px, 1em, 1vh)))", "calc(0px + clamp(1px, 1em, 1vh))");
    ok("width", "calc(calc(calc(10px)))", "calc(10px)");
    // ...AND A calc() AROUND ANY OTHER MATH FUNCTION IS NOT. That generalisation
    // was read out of `clamp-length-serialize`, which only ever writes a calc in
    // a calc; `calc-complex-unresolved-serialize` writes the other case six
    // times and wants the outer function back on every one of them.
    ok("orphans", "calc(pow(2, sign(1em - 18px)))", "calc(pow(2, sign(1em - 18px)))");
    ok("orphans", "calc(pow(2, sibling-index())", "calc(pow(2, sibling-index()))");
    ok("margin-top", "calc(clamp(1px, 1em, 1vh))", "calc(clamp(1px, 1em, 1vh))");

    // ...AND EVERYTHING ELSE KEEPS THE AUTHOR'S BYTES. A function with no answer
    // until layout, one whose units have no basis yet, and one this file cannot
    // evaluate are all left exactly as written - which is where they were before
    // this rule existed, so nothing that works today can start failing.
    ok("transform", "translate(min(10px, 5%))", "translate(min(10px, 5%))");
    ok("transform", "rotate(calc(1deg + 1cqw))", "rotate(calc(1deg + 1cqw))");
    ok("transform", "scale(calc(1 * sibling-index()))", "scale(calc(1 * sibling-index()))");
    ok("width", "calc-size(10px, sign(size) * size)", "calc-size(10px, sign(size) * size)");
    ok("font-family", "\"calc(1px + 1px)\"", "\"calc(1px + 1px)\""); // inside a string
}

// A PERCENTAGE SIMPLIFIES; COMPARING TWO OF THEM DOES NOT. The two halves are
// separate rules and they used to be one over-cautious rule ("no percentage
// anywhere, ever").
void test_the_percentage_half_of_simplification() {
    // The value model carries a percentage BESIDE the pixels rather than
    // resolving it, so these need no basis and nothing is guessed.
    ok("left", "calc(50px + calc(40%))", "calc(40% + 50px)");
    ok("width", "calc(100% * 0.5)", "calc(50%)");
    ok("text-indent", "min(1% + 1px)", "calc(1% + 1px)"); // one argument is not a comparison
    // AN ABSENT COMPONENT IS NOT A ZERO ONE. `10%` has no length in it, and IEEE
    // makes `0 * infinity` a NaN, so scaling one by an infinity used to invent a
    // NaN length beside the right answer. A zero the AUTHOR wrote still does.
    ok("width", "calc(1% * infinity)", "calc(infinity * 1%)");
    ok("width", "calc(1% * NaN)", "calc(NaN * 1%)");
    ok("width", "calc(1% / 0)", "calc(infinity * 1%)");
    ok("width", "calc(0px * infinity)", "calc(NaN * 1px)");

    // ...AND THE COMPARISON HALF. `min(1%, 2%)` looks decidable and is not: a
    // percentage resolves against a basis that may be NEGATIVE, and then 2% is
    // the smaller. `minmax-percentage-serialize` asks for both functions back.
    ok("text-indent", "min(1%, 2%)", "min(1%, 2%)");
    ok("text-indent", "max(3%, 4%)", "max(3%, 4%)");
    ok("text-indent", "clamp(1%, 2%, 3%)", "clamp(1%, 2%, 3%)");
    ok("text-indent", "min(10px, 5%)", "min(10px, 5%)"); // and the mixed case, as before
}

// A PERCENTAGE HAS TO BE A PERCENTAGE OF SOMETHING, CSS Values 4 §10.11. The
// only calculation context this engine ever supplies is a length - no property
// resolves a percentage into an angle or a time - so an expression that answers
// with one of those and mentions a percentage is a syntax error, not a value
// waiting for layout. `percentage-without-context` is twelve of these and every
// one folded here by reading the percentage's own digits as its magnitude.
void test_a_percentage_needs_a_context() {
    bad("transform", "rotate(calc(sign(50%) * 1deg))");
    bad("filter", "hue-rotate(calc(sign(50%) * 1deg))");
    bad("font-style", "oblique calc(sign(50%) * 1deg)");
    bad("color", "hsl(calc(sign(50%) * 1deg) 82% 43%)");
    bad("animation-duration", "calc(sign(50%) * 1s)");
    bad("transition-delay", "calc(sign(50%) * 1s)");
    // ...INCLUDING FOR A PROPERTY THIS TABLE HAS NEVER HEARD OF. An unknown name
    // is stored rather than refused, but the math in it is still math: these two
    // are `percentage-without-context`'s last pair and both used to be kept
    // because `offset-rotate` is not in the table.
    bad("offset-rotate", "calc(sign(50%) * 1deg)");
    bad("offset-path", "ray(calc(sign(50%) * 1deg))");
    ok("offset-rotate", "calc(45deg + 45deg)", "calc(90deg)"); // ...and simplified, too

    // A <number> ANSWER IS NOT COVERED and must not be: there the percentage
    // sits in a length context that the property does supply.
    ok("width", "calc(1px * pow(tan(atan2(50%, 1px)), 1))",
       "calc(1px * pow(tan(atan2(50%, 1px)), 1))");
    ok("width", "calc(50% + 1px)", "calc(50% + 1px)");
}

// A MATH FUNCTION IS TYPED BY WHERE IT SITS, and `rotate()` is the one position
// this file can say so from without a grammar for `transform` or `filter`.
// `minmax-angle-invalid`, `sin-cos-tan-invalid` and `acos-asin-atan-atan2-invalid`
// end on these sixteen assertions between them, one shape each: a rotation by a
// length, by a number, and by a ratio with a percentage in it.
void test_the_angle_functions_take_an_angle() {
    bad("transform", "rotate(min(0px))");
    bad("transform", "rotate(min(0))");
    bad("transform", "rotate(max(0fr))");
    bad("transform", "rotate(tan(45deg ))"); // tan() answers with a <number>
    bad("transform", "rotate(atan2(90px, 100%))");
    bad("transform", "skew(min(1px), 45deg)");
    bad("filter", "hue-rotate(min(1px))");

    // <zero> IS WHY ONLY A MATH FUNCTION IS JUDGED. `rotate( [ <angle> | <zero> ] )`
    // is CSS Transforms 1's own spelling, so a literal `0` is a rotation.
    ok("transform", "rotate(0)", "rotate(0)");
    ok("transform", "rotate(45deg)", "rotate(45deg)");
    ok("transform", "rotate(calc(45deg + 45deg))", "rotate(calc(90deg))");
    ok("transform", "rotate(atan2(1, 1))", "rotate(calc(45deg))");
    ok("filter", "hue-rotate(90deg)", "hue-rotate(90deg)");
    // ...and a function with no answer here is not a function with a wrong type.
    ok("transform", "rotate(calc(1deg + 1cqw))", "rotate(calc(1deg + 1cqw))");
    // Nothing outside the angle-only functions is touched by the rule.
    ok("transform", "translate(min(10px, 5%))", "translate(min(10px, 5%))");
}

// A SUM THAT COULD NOT BE FOLDED IS STILL SIMPLIFIED. Its terms have no single
// magnitude before there is a font size, a viewport and a containing block, and
// §10.12's simplified form is not the author's bytes but one term per unit in
// §10.13's order: the percentage first, then the units sorted ASCII
// case-insensitively - `px` among them in its alphabetical place, not first for
// being the canonical one. `calc-serialization` is six of these and
// `calc-dimension-serialization-order` walks all forty-four relative units.
void test_a_sum_that_cannot_fold_still_has_an_order() {
    ok("width", "calc(10px + 1vmin + 10%)", "calc(10% + 10px + 1vmin)");
    ok("width", "calc(10px + 1vmin)", "calc(10px + 1vmin)");
    ok("width", "calc(10px + 1em)", "calc(1em + 10px)");
    ok("width", "calc(1vmin - 10px)", "calc(-10px + 1vmin)");
    ok("width", "calc(-10px + 1em)", "calc(1em - 10px)");
    ok("height", "calc(1ch + 1cap)", "calc(1cap + 1ch)");
    ok("height", "calc(1rcap + 1px)", "calc(1px + 1rcap)"); // px sorts, it does not lead
    ok("height", "calc(1lvw + 1px)", "calc(1lvw + 1px)");
    ok("width", "calc(3 * (1em + 1px))", "calc(3em + 3px)"); // a coefficient scales every term
    // A UNIT WITH NO BASIS IS NOT A UNIT WITH NO ARITHMETIC. `fr` converts to
    // nothing and never will, which is a different fact from `1fr + 1fr`.
    ok("grid-template-rows", "calc(1fr + 1fr)", "calc(2fr)");

    // ...AND A COMPARISON STILL CANNOT BE DECIDED. `min(1em, 1px)` has no order
    // before a font size, exactly as `min(10px, 5%)` has none before a
    // containing block - but each SIDE of it is a calculation like any other and
    // simplifies like any other, which is what `minmax-length-percent-serialize`
    // and `calc-infinity-nan-serialize-length` end on.
    ok("width", "min(1em, 1px)", "min(1em, 1px)");
    ok("width", "min(10% + 30px, 5em + 5%)", "min(10% + 30px, 5% + 5em)");
    ok("width", "calc(1 * min(NaN * 2px, NaN * 4em))", "calc(1 * min(NaN * 1px, NaN * 1em))");
    ok("width", "clamp(1rem, 2vw, 3rem)", "clamp(1rem, 2vw, 3rem)");
    ok("width", "min(1em)", "calc(1em)"); // ...one argument is not a comparison
}

// ...AND THE OTHER HALF OF §10.11'S CALCULATION CONTEXT IS THE PROPERTY'S.
// `text-indent: min(1px, 0%)` resolves against a containing block and
// `border-left-width: min(1px, 0%)` has nothing to resolve against, so the two
// look alike and only one of them is a value. These are the last failures of
// `minmax-length-invalid` and `signs-abs-invalid`.
void test_a_percentage_needs_a_property_that_takes_one() {
    bad("border-left-width", "min(1px, 0%)");
    bad("border-left-width", "max(1px, 0%)");
    bad("font-weight", "sign(10%)");
    bad("outline-width", "calc(10%)");
    ok("text-indent", "min(1px, 0%)", "min(1px, 0%)");
    ok("width", "calc(50% - 10px)", "calc(50% - 10px)");
    // A FREEFORM PROPERTY ANSWERS YES and has to: the grammar is not modelled,
    // so a guess would lose two values that are perfectly ordinary.
    ok("transform", "translate(50%)", "translate(50%)");
    ok("background-position", "calc(50% - 1px)", "calc(50% - 1px)");
    // ...and a percentage OUTSIDE a math function is not this rule's business.
    ok("color", "hsl(calc(1deg) 82% 43%)", "hsl(calc(1deg) 82% 43%)");

    // `line-height` and `tab-size` are why the two <number>-and-<length> kinds
    // are two: `line-height: 50%` is half the font size and `tab-size: 50%` is
    // nothing at all. CSS Text 4 writes `<number [0,inf]> | <length [0,inf]>`.
    ok("line-height", "50%", "50%");
    ok("line-height", "calc(50% + 1px)", "calc(50% + 1px)");
    bad("tab-size", "50%");
    bad("tab-size", "abs(10%)");
    ok("tab-size", "4", "4");
    ok("tab-size", "10px", "10px");
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
    ok("font-family", "random-item(auto, serif", "random-item(auto, serif");
}

// `interpolate-size` is a real property with a real two-keyword grammar. As an
// UNKNOWN one `el.style` stored `interpolate-size: 100%` and `getComputedStyle`
// did not publish the property at all - which is the two assertions of
// `calc-size/interpolate-size-computed.html` and three of `-parsing.html`.
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
    test_interpolate_size_is_a_property();
    REPORT("css_values");
}
