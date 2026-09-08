// calc() and the math functions - what they resolve to, when the answer is a
// <number> rather than a <length>, min()/max()/clamp(), and calc() inside the
// cascade. Carved out of unit/style_basics.cpp on 2026-09-08 -
// style_shorthands.cpp names the family, and style_fixture.hpp is the fixture
// every file in it shares.

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

// calc(), which is 134 expressions in Bootstrap and used to be 134 dropped
// declarations - parse_length gave up on the leading `c` and the property was
// silently nothing.
void test_calc() {
    using ctbrowser::style::css::evaluate_calc;
    using ctbrowser::style::css::length_context;
    using ctbrowser::style::css::math_context;
    // The fold as a LENGTH property sees it, which is what every assertion below
    // was written against: `width`, `margin-top` and friends take no bare number,
    // so a math function that answers with one is a syntax error there. The
    // number-accepting half is test_math_answers_with_a_number.
    const auto fold_calc = [](std::string_view value,
                              const ctbrowser::style::css::length_context & where) {
        return ctbrowser::style::css::fold_math(value, where, math_context::length);
    };
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
    invalid("1px 2px");      // no operator
    invalid("1px +");        // nothing after one
    invalid("5");            // a bare number is not a length
    invalid("1frobs + 2px"); // an unmodelled unit, rather than a silent zero
    // TWO THAT USED TO BE REFUSED HERE AND ARE NOT SYNTAX ERRORS AT ALL.
    // Division by zero is an infinity (CSS Values 4 10.9) - IEEE already gives
    // the right signed answer, and the guard against it was the whole bug - and
    // EOF CLOSES AN UNTERMINATED FUNCTION OR BLOCK (CSS Syntax 5.4.9), which
    // `calc(min(1em, 21px) * 2` relies on four times in one corpus file.
    CHECK(std::isinf(px("2px / 0")));
    CHECK(px("(1px") == 1.0f);
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

// CSS Values 3 §8.1: A MATH FUNCTION MAY RESOLVE TO A `<number>`, and every one
// that did was thrown away here.
//
// The evaluator answered `nullopt` for a number, the fold read that as "invalid",
// and the cascade dropped the declaration - so `opacity: calc(2 / 4)`,
// `z-index: calc(1 + 1)`, `tab-size: calc(2 * 3)` and `rgb(calc(0), calc(255),
// calc(0))` all produced nothing at all. WPT's css/css-values has whole files of
// exactly these.
//
// The reason it was written that way is real and is preserved below: a number is
// NOT a length, so `width: calc(2 * 3)` must stay invalid. What was missing was
// somewhere to ask which of the two the property wanted, which is math_context.
void test_math_answers_with_a_number() {
    using ctbrowser::style::css::evaluate_calc;
    using ctbrowser::style::css::evaluate_math;
    using ctbrowser::style::css::fold_math;
    using ctbrowser::style::css::length_context;
    using ctbrowser::style::css::math_context;
    using ctbrowser::style::css::math_context_of;
    using ctbrowser::style::css::math_outcome;

    length_context ctx;
    ctx.font_size = 20.0f;
    ctx.root_font_size = 16.0f;
    ctx.viewport_width = 1000.0f;
    ctx.viewport_height = 800.0f;
    const auto any = [&](std::string_view v) { return fold_math(v, ctx, math_context::any); };
    const auto len = [&](std::string_view v) { return fold_math(v, ctx, math_context::length); };

    // A NUMBER, WHERE A NUMBER IS A VALUE. Each of these was a dropped
    // declaration.
    CHECK(any("calc(2 * 3)").text == "6");
    CHECK(any("calc(2 * 3)").ok);
    CHECK(any("calc(2 / 4)").text == "0.5");
    CHECK(any("calc(1 + 1)").text == "2");
    CHECK(any("calc(-1 * 0)").text == "0");
    CHECK(any("calc(-1 * 0)").ok);
    // In place, inside another function - which is how css/css-values writes it:
    // `color: rgb(calc(0), calc(255 + 0), calc(140 - 139 - 1))` is green.
    CHECK(any("rgb(calc(0), calc(255 + 0), calc(140 - 139 - 1))").text == "rgb(0, 255, 0)");
    CHECK(any("\"vert\" calc(1 + 1)").text == "\"vert\" 2");
    // A number has NO UNIT. Appending `px` would be a different kind of wrong
    // from dropping it: layout would read it and believe it.
    CHECK(any("calc(2 * 3)").text.find("px") == std::string::npos);

    // AND THE GUARD, which is what makes the above safe. The same expression in a
    // property whose whole value is a length is a syntax error - it was one
    // before and it stays one. Bootstrap's `.row { margin-top: calc(-1 *
    // var(--bs-gutter-y)) }` with a gutter of `0` is exactly this shape.
    CHECK(!len("calc(2 * 3)").ok);
    CHECK(len("calc(2 * 3)").text == "calc(2 * 3)");
    CHECK(!len("calc(-1 * 0)").ok);

    // A LENGTH answer is a length in either context: nothing about the ordinary
    // case moved.
    CHECK(any("calc(1rem + 1rem)").text == "32px");
    CHECK(len("calc(1rem + 1rem)").text == "32px");
    CHECK(any("calc(100% - 12px)").text == "calc(100% - 12px)");

    // evaluate_calc is the LENGTH-ONLY view and still refuses a number, so every
    // caller that meant "is this a length" kept its meaning.
    CHECK(!evaluate_calc("2 * 3", ctx).has_value());
    CHECK(evaluate_math("2 * 3", ctx).outcome == math_outcome::resolved);
    CHECK(evaluate_math("2 * 3", ctx).value.is_number);
    CHECK(evaluate_math("2 * 3", ctx).value.px == 6.0f);
    CHECK(!evaluate_math("1px + 2", ctx).value.is_number);
    CHECK(evaluate_math("1px + 2", ctx).outcome == math_outcome::invalid);

    // The table that tells the two apart. It is deliberately short: a property
    // is on it only when EVERY component of its value is a length, because the
    // context applies to every math function in the value.
    CHECK(math_context_of("width") == math_context::length);
    CHECK(math_context_of("margin-top") == math_context::length);
    CHECK(math_context_of("MARGIN-TOP") == math_context::length); // ASCII case-insensitive
    CHECK(math_context_of("font-size") == math_context::length);
    CHECK(math_context_of("opacity") == math_context::any);
    // `z-index` and `order` are `<integer>`, which is a THIRD context: a number
    // is a value for them, but a fractional one rounds - see css_values.cpp.
    CHECK(math_context_of("z-index") == math_context::integer);
    CHECK(math_context_of("order") == math_context::integer);
    // `line-height: 1.5` is a NUMBER and is the commonest spelling of it, so the
    // property that looks most like a length is not one.
    CHECK(math_context_of("line-height") == math_context::any);
    // Compound values are `any` for the same reason: `transform: scale(calc(1 /
    // 2))` has a legitimate number inside a property nobody would call numeric.
    CHECK(math_context_of("box-shadow") == math_context::any);
    CHECK(math_context_of("background-position") == math_context::any);
}

// CSS Values 4 §10.3 - `min()`, `max()` and `clamp()`, which this front end
// could not read AT ALL: `width: clamp(1rem, 2vw, 3rem)` reached layout as text,
// parse_length gave up on the leading `c`, and the box got nothing. Bootstrap
// happens to use none of the three, which is why the gap survived a corpus of
// one.
void test_comparison_functions() {
    using ctbrowser::style::css::evaluate_math;
    using ctbrowser::style::css::fold_math;
    using ctbrowser::style::css::length_context;
    using ctbrowser::style::css::math_context;
    using ctbrowser::style::css::math_outcome;
    using ctbrowser::style::css::may_have_math;

    length_context ctx;
    ctx.font_size = 20.0f;
    ctx.root_font_size = 16.0f;
    ctx.viewport_width = 1000.0f;
    ctx.viewport_height = 800.0f;
    const auto any = [&](std::string_view v) { return fold_math(v, ctx, math_context::any); };
    const auto len = [&](std::string_view v) { return fold_math(v, ctx, math_context::length); };

    CHECK(len("min(10px, 4px)").text == "4px");
    CHECK(len("max(10px, 4px)").text == "10px");
    CHECK(len("min(3rem, 2rem, 4rem)").text == "32px"); // more than two arguments
    CHECK(len("MIN(10px, 4px)").text == "4px");         // function names fold ASCII case
    // clamp(low, value, high), with the value inside, below and above the bounds.
    CHECK(len("clamp(1rem, 2vw, 3rem)").text == "20px");
    CHECK(len("clamp(1rem, 1px, 3rem)").text == "16px");
    CHECK(len("clamp(1rem, 100px, 3rem)").text == "48px");
    // clamp IS max(low, min(value, high)), so when the bounds cross the LOW one
    // wins - the min is taken first. Getting this backwards is the classic bug.
    CHECK(len("clamp(30px, 1px, 10px)").text == "30px");
    // Nested in each other and inside a calc.
    CHECK(len("calc(1px + min(2px, 5px))").text == "3px");
    CHECK(len("max(1px, min(9px, 4px))").text == "4px");
    // A comparison of numbers is a number, and obeys the same context rule.
    CHECK(any("min(2, 5)").text == "2");

    // UNDECIDABLE IS NOT INVALID, and this is the distinction that makes the
    // whole feature safe to add. `min(10px, 5%)` is 10px on a wide containing
    // block and 5% of it on a narrow one - there is no answer at computed-value
    // time, and CSS Values 4 §10.11 says the computed value is the function as
    // written. So the text survives and the declaration lives.
    CHECK(len("min(10px, 5%)").text == "min(10px, 5%)");
    CHECK(len("min(10px, 5%)").ok);
    CHECK(evaluate_math("min(10px, 5%)", ctx).outcome == math_outcome::unresolved);
    CHECK(evaluate_math("calc(1px + min(1px, 5%))", ctx).outcome == math_outcome::unresolved);
    CHECK(len("calc(1px + min(1px, 5%))").ok);

    // A COMPARISON NEVER CONDEMNS A DECLARATION. Mismatched types, the wrong
    // arity and a malformed argument are all errors, but before this file could
    // parse the functions they were kept verbatim - so keeping them is the one
    // answer that cannot regress a page that was rendering.
    CHECK(len("min(1px, 2)").text == "min(1px, 2)");
    CHECK(len("min(1px, 2)").ok);
    CHECK(len("clamp(1px, 2px)").text == "clamp(1px, 2px)");
    CHECK(len("clamp(1px, 2px)").ok);
    CHECK(len("min()").text == "min()");
    CHECK(len("min()").ok);
    CHECK(len("min(2, 5)").text == "min(2, 5)"); // a number where a length belongs
    CHECK(len("min(2, 5)").ok);

    // `minmax()` CONTAINS `max(` three bytes in and is not one - the same
    // identifier-boundary rule that keeps `-webkit-calc(` out, and the reason
    // grid track lists are not quietly rewritten.
    CHECK(len("repeat(2, minmax(100px, 1fr))").text == "repeat(2, minmax(100px, 1fr))");
    CHECK(len("repeat(2, minmax(100px, 1fr))").ok);
    CHECK(!may_have_math("repeat(2, minmax(100px, 1fr))"));
    CHECK(!may_have_math("1px solid red"));
    CHECK(may_have_math("clamp(1px, 2px, 3px)"));
    CHECK(may_have_math("MIN(1px, 2px)"));
    CHECK(may_have_math("calc(1px)"));
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
    {
        // A NUMBER IS A VALUE for a property that takes one - CSS Values 3 §8.1 -
        // and every one of these used to reach the element as nothing at all.
        fixture f;
        f.load("<p id=a></p>", "p { opacity: calc(2 / 4); z-index: calc(1 + 1);"
                               "    tab-size: calc(2 * 3) }");
        expect_value(f, f.find_id("a"), "opacity", "0.5", "calc() of a number is a number");
        expect_value(f, f.find_id("a"), "z-index", "2", "and so is an integer one");
        expect_value(f, f.find_id("a"), "tab-size", "6", "and one nobody would call a length");
    }
    {
        // ...IN PLACE, inside another function. This is the shape css/css-values
        // uses for colours: `rgb(calc(0), calc(255 + 0), calc(140 - 139 - 1))`.
        fixture f;
        f.load("<p id=a></p>", "p { color: rgb(calc(0), calc(255 + 0), calc(140 - 139 - 1)) }");
        expect_value(f, f.find_id("a"), "color", "rgb(0, 255, 0)", "calc inside rgb()");
    }
    {
        // AND THE GUARD, at the cascade level: the same arithmetic in a length
        // property is still an invalid declaration, so the earlier one still wins.
        fixture f;
        f.load("<p id=a></p>", "p { width: 5px; width: calc(2 * 3) }");
        expect_value(f, f.find_id("a"), "width", "5px", "a number is not a length");
    }
    {
        // The comparison functions, end to end. `clamp(1rem, 2vw, 3rem)` with a
        // 1000px viewport is 20px, and used to reach layout as text it read as 0.
        fixture f;
        ctbrowser::style::css::media_environment env;
        env.viewport_width = 1000;
        env.viewport_height = 800;
        (void)f.styles.set_environment(env);
        f.load("<p id=a></p>", "p { width: clamp(1rem, 2vw, 3rem); height: max(10px, 4px);"
                               "    margin-top: min(3rem, 2rem) }");
        expect_value(f, f.find_id("a"), "width", "20px", "clamp between its bounds");
        expect_value(f, f.find_id("a"), "height", "10px", "max of two lengths");
        expect_value(f, f.find_id("a"), "margin-top", "32px", "min of two lengths");
    }
    {
        // A comparison that needs a containing block keeps its text AND its
        // declaration - CSS Values 4 §10.11. Dropping it would be a regression on
        // any page that renders today, because before this the text was all there
        // ever was.
        fixture f;
        f.load("<p id=a></p>", "p { width: min(10px, 5%) }");
        expect_value(f, f.find_id("a"), "width", "min(10px, 5%)", "unresolved, not invalid");
    }
}

} // namespace

int main() {
    test_calc();
    test_math_answers_with_a_number();
    test_comparison_functions();
    test_calc_in_the_cascade();
    REPORT("style_calc");
}
