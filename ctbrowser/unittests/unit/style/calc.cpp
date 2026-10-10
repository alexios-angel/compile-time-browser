//===- calc.cpp - calc() and math function tests ----------------*- C++ -*-===//
//
// calc() and the math functions - what they resolve to, when the answer is a
// <number> rather than a <length>, min()/max()/clamp(), and calc() inside the
// cascade. Shares the test fixture in fixture.hpp with the other files in
// unittests/unit/style/.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "fixture.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using namespace ctbrowser::style;

namespace {

/// The length-only view of evaluate_math: `nullopt` for a number, for an
/// unresolved comparison, and for anything invalid.
[[nodiscard]] std::optional<ctbrowser::style::css::calc_result> evaluate_calc(
    std::string_view expression, const ctbrowser::style::css::length_context & ctx) {
    using ctbrowser::style::css::math_outcome;
    const auto answer = ctbrowser::style::css::evaluate_math(expression, ctx);
    if (answer.outcome != math_outcome::resolved || answer.value.is_number) { return std::nullopt; }
    return answer.value;
}

void test_calc() {
    using ctbrowser::style::css::length_context;
    using ctbrowser::style::css::math_context;
    // The fold as a length property sees it: `width`, `margin-top` and friends
    // take no bare number, so a math function that answers with one is a syntax
    // error there. test_math_answers_with_a_number covers properties that do
    // take a number.
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
    // The commonest shape of Bootstrap's calcs: a length scaled by a number,
    // either way round, and with the number negative.
    CHECK(px("2rem * .5") == 16.0f);
    CHECK(px(".5 * 2rem") == 16.0f);
    CHECK(px("-1 * 1.5rem") == -24.0f);
    CHECK(px("-.5 * 3rem") == -24.0f);
    CHECK(px("10px / 4") == 2.5f);
    // Units. `em` is the element's own size, `rem` the root's; the cascade
    // resolves font-size first so both are available.
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
    // Precedence, parentheses, and a nested calc, which Bootstrap also uses.
    CHECK(px("1px + 2px * 3") == 7.0f);
    CHECK(px("(1px + 2px) * 3") == 9.0f);
    CHECK(px("1em + .5rem + calc(1rem * 2)") == 20.0f + 8.0f + 32.0f);
    CHECK(px("calc(calc(1px))") == 1.0f);

    // The type rules, which are why this is more than adding numbers up.
    invalid("1px + 2");      // A length and a number.
    invalid("2px * 3px");    // An area, which calc has no type for.
    invalid("2px / 3px");    // The divisor has to be a number.
    invalid("1px 2px");      // No operator.
    invalid("1px +");        // Nothing after one.
    invalid("5");            // A bare number is not a length.
    invalid("1frobs + 2px"); // An unmodelled unit, rather than a silent zero.
    // Two cases that are not syntax errors. Division by zero is an infinity
    // (CSS Values 4 10.9), and IEEE-754 division already gives the right signed
    // answer. And EOF closes an unterminated function or block (CSS Syntax
    // 5.4.9), which real pages rely on, as in `calc(min(1em, 21px) * 2`.
    CHECK(std::isinf(px("2px / 0")));
    CHECK(px("(1px") == 1.0f);
    // `+` and `-` require surrounding whitespace, and this gets that for free: the
    // tokenizer makes `-12px` one dimension, so there is no operator between the
    // two terms. Chrome rejects it too.
    invalid("100% -12px");

    // A percentage has no answer until a containing block exists, so it survives as
    // the canonical two-term form layout::parse_length knows how to read.
    CHECK(fold_calc("calc(100% - 12px)", ctx).text == "calc(100% - 12px)");
    CHECK(fold_calc("calc(50% + 1rem)", ctx).text == "calc(50% + 16px)");
    CHECK(fold_calc("calc(100% * .5)", ctx).text == "50%");
    CHECK(fold_calc("calc(2rem)", ctx).text == "32px");

    // fold_calc leaves everything else alone and says whether it could read it.
    // The text is what a caller with no better answer carries on with; the `ok`
    // flag lets the cascade treat the declaration as invalid instead, so
    // `margin-top` takes its initial 0 rather than layout being handed a string
    // it reads as `auto`.
    CHECK(fold_calc("1px solid red", ctx).text == "1px solid red");
    CHECK(fold_calc("1px solid red", ctx).ok);
    CHECK(fold_calc("calc(1px + 2)", ctx).text == "calc(1px + 2)");
    CHECK(!fold_calc("calc(1px + 2)", ctx).ok);
    // A number is not a length, and Bootstrap writes exactly this: `.row`'s
    // `margin-top: calc(-1 * var(--bs-gutter-y))` with a gutter of `0` multiplies
    // two numbers and gets a number.
    CHECK(!fold_calc("calc(-1 * 0)", ctx).ok);
    // A vendor-prefixed one is not a calc at all, so there is nothing to fail.
    CHECK(fold_calc("-webkit-calc(1px + 1px)", ctx).text == "-webkit-calc(1px + 1px)");
    CHECK(fold_calc("-webkit-calc(1px + 1px)", ctx).ok);
    // Two of them in one value, which is how Bootstrap writes `.row` gutters.
    CHECK(fold_calc("calc(2rem * .5) calc(1rem + 1rem)", ctx).text == "16px 32px");
    // One good and one bad calc is still a bad value.
    CHECK(!fold_calc("calc(1rem) calc(1px + 2)", ctx).ok);
}

// CSS Values 3 §8.1: a math function may resolve to a <number>, which is a
// valid value for properties such as opacity, z-index and rgb() components,
// but stays invalid in a length-only property such as width. math_context
// tells the evaluator which one the property wants.
void test_math_answers_with_a_number() {
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

    // A number, where a number is a value.
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
    // A number has no unit. Appending `px` would be worse than dropping it:
    // layout would read it and believe it.
    CHECK(any("calc(2 * 3)").text.find("px") == std::string::npos);

    // The same expression in a property whose whole value is a length is a
    // syntax error. Bootstrap's `.row { margin-top: calc(-1 *
    // var(--bs-gutter-y)) }` with a gutter of `0` is exactly this shape.
    CHECK(!len("calc(2 * 3)").ok);
    CHECK(len("calc(2 * 3)").text == "calc(2 * 3)");
    CHECK(!len("calc(-1 * 0)").ok);

    // A length answer is a length in either context.
    CHECK(any("calc(1rem + 1rem)").text == "32px");
    CHECK(len("calc(1rem + 1rem)").text == "32px");
    CHECK(any("calc(100% - 12px)").text == "calc(100% - 12px)");

    // evaluate_calc is the length-only view and refuses a number, while
    // evaluate_math reports it with `is_number` set.
    CHECK(!evaluate_calc("2 * 3", ctx).has_value());
    CHECK(evaluate_math("2 * 3", ctx).outcome == math_outcome::resolved);
    CHECK(evaluate_math("2 * 3", ctx).value.is_number);
    CHECK(evaluate_math("2 * 3", ctx).value.px == 6.0f);
    CHECK(!evaluate_math("1px + 2", ctx).value.is_number);
    CHECK(evaluate_math("1px + 2", ctx).outcome == math_outcome::invalid);

    // The table that tells the two apart. It is deliberately short: a property
    // is on it only when every component of its value is a length, because the
    // context applies to every math function in the value.
    CHECK(math_context_of("width") == math_context::length);
    CHECK(math_context_of("margin-top") == math_context::length);
    CHECK(math_context_of("MARGIN-TOP") == math_context::length); // ASCII case-insensitive
    CHECK(math_context_of("font-size") == math_context::length);
    CHECK(math_context_of("opacity") == math_context::any);
    // `z-index` and `order` are `<integer>`, which is a third context: a number
    // is a value for them, but a fractional one rounds - see ../css/values.cpp.
    CHECK(math_context_of("z-index") == math_context::integer);
    CHECK(math_context_of("order") == math_context::integer);
    // `line-height: 1.5` is a number and is the commonest spelling of it, so the
    // property that looks most like a length is not one.
    CHECK(math_context_of("line-height") == math_context::any);
    // Compound values are `any` for the same reason: `transform: scale(calc(1 /
    // 2))` has a legitimate number inside a property nobody would call numeric.
    CHECK(math_context_of("box-shadow") == math_context::any);
    CHECK(math_context_of("background-position") == math_context::any);
}

// CSS Values 4 §10.3 - `min()`, `max()` and `clamp()`. They must be folded
// here: layout's parse_length cannot read `clamp(...)` text and would give the
// box nothing.
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
    CHECK(len("min(3rem, 2rem, 4rem)").text == "32px"); // More than two arguments.
    CHECK(len("MIN(10px, 4px)").text == "4px");         // Function names fold ASCII case.
    // clamp(low, value, high), with the value inside, below and above the bounds.
    CHECK(len("clamp(1rem, 2vw, 3rem)").text == "20px");
    CHECK(len("clamp(1rem, 1px, 3rem)").text == "16px");
    CHECK(len("clamp(1rem, 100px, 3rem)").text == "48px");
    // clamp is max(low, min(value, high)), so when the bounds cross the low one
    // wins - the min is taken first. Getting this backwards is the classic bug.
    CHECK(len("clamp(30px, 1px, 10px)").text == "30px");
    // Nested in each other and inside a calc.
    CHECK(len("calc(1px + min(2px, 5px))").text == "3px");
    CHECK(len("max(1px, min(9px, 4px))").text == "4px");
    // A comparison of numbers is a number, and obeys the same context rule.
    CHECK(any("min(2, 5)").text == "2");

    // Undecidable is not invalid, and this distinction is what makes folding
    // comparisons safe. `min(10px, 5%)` is 10px on a wide containing
    // block and 5% of it on a narrow one - there is no answer at computed-value
    // time, and CSS Values 4 §10.11 says the computed value is the function as
    // written. So the text survives and the declaration lives.
    CHECK(len("min(10px, 5%)").text == "min(10px, 5%)");
    CHECK(len("min(10px, 5%)").ok);
    CHECK(evaluate_math("min(10px, 5%)", ctx).outcome == math_outcome::unresolved);
    CHECK(evaluate_math("calc(1px + min(1px, 5%))", ctx).outcome == math_outcome::unresolved);
    CHECK(len("calc(1px + min(1px, 5%))").ok);

    // A comparison never invalidates a declaration. Mismatched types, the wrong
    // arity and a malformed argument are all errors, but the text is kept
    // verbatim rather than dropped, so a page relying on that text still
    // renders as before.
    CHECK(len("min(1px, 2)").text == "min(1px, 2)");
    CHECK(len("min(1px, 2)").ok);
    CHECK(len("clamp(1px, 2px)").text == "clamp(1px, 2px)");
    CHECK(len("clamp(1px, 2px)").ok);
    CHECK(len("min()").text == "min()");
    CHECK(len("min()").ok);
    CHECK(len("min(2, 5)").text == "min(2, 5)"); // A number where a length belongs.
    CHECK(len("min(2, 5)").ok);

    // `minmax()` contains `max(` three bytes in and is not one - the same
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
        // Bootstrap's `.container` declaration; getting it wrong shifted most of
        // a Bootstrap page 12px compared with Chrome.
        fixture f;
        f.load("<div id=a></div>", ":root { --bs-gutter-x: 1.5rem }"
                                   "div { padding-right: calc(var(--bs-gutter-x) * .5);"
                                   "      padding-left: calc(var(--bs-gutter-x) * .5) }");
        expect_value(f, f.find_id("a"), "padding-left", "12px", "the container's gutter");
        expect_value(f, f.find_id("a"), "padding-right", "12px", "on both sides");
    }
    {
        // `em` is the element's own size, which is why font-size is folded
        // before anything else reads it. 1em of a 32px font is 32px, not 16px.
        fixture f;
        f.load("<p id=a></p>", "p { font-size: 32px; margin-top: calc(1em + 4px) }");
        expect_value(f, f.find_id("a"), "margin-top", "36px", "em against its own size");
    }
    {
        // ...and in font-size itself it is the parent's, which is CSS's asymmetry
        // and not a convenience: `font-size: 2em` doubles rather than recursing.
        fixture f;
        f.load("<div id=o><p id=a></p></div>", "div { font-size: 20px } p { font-size: 2em }");
        expect_value(f, f.find_id("a"), "font-size", "40px", "em in font-size is the parent's");
    }
    {
        // A rem is the root's size, not a hardcoded 16px.
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
        // An invalid calc() makes the whole declaration invalid, so the property
        // takes its initial value as Chrome reports. Keeping the text instead
        // would make layout read `auto`, which differs from absent.
        fixture f;
        f.load("<p id=a></p>", "p { width: calc(1px + 2) }");
        expect_value(f, f.find_id("a"), "width", "", "an invalid calc is dropped");
    }
    {
        // Which kind of invalid decides what happens to an earlier declaration,
        // and the two cases are observably different. A value that went through
        // var() substitution is invalid at computed-value time (IACVT), and CSS
        // Variables 1 §3 makes that `unset` - so it removes the earlier
        // declaration it beat...
        fixture f;
        f.load("<p id=a></p>", "p { --g: 0; width: 5px; width: calc(-1 * var(--g)) }");
        expect_value(f, f.find_id("a"), "width", "", "IACVT is unset, not 'the earlier one wins'");
    }
    {
        // ...while one that never contained a var() is invalid at parse time, so
        // the earlier declaration simply wins. `color: red; color: var(--missing)`
        // is the same distinction.
        fixture f;
        f.load("<p id=a></p>", "p { width: 5px; width: calc(1px + 2) }");
        expect_value(f, f.find_id("a"), "width", "5px",
                     "a parse-time invalid lets the earlier win");
    }
    {
        // A number is a value for a property that takes one (CSS Values 3 §8.1).
        fixture f;
        f.load("<p id=a></p>", "p { opacity: calc(2 / 4); z-index: calc(1 + 1);"
                               "    tab-size: calc(2 * 3) }");
        expect_value(f, f.find_id("a"), "opacity", "0.5", "calc() of a number is a number");
        expect_value(f, f.find_id("a"), "z-index", "2", "and so is an integer one");
        expect_value(f, f.find_id("a"), "tab-size", "6", "and one nobody would call a length");
    }
    {
        // ...in place, inside another function. This is the shape css/css-values
        // uses for colours: `rgb(calc(0), calc(255 + 0), calc(140 - 139 - 1))`.
        fixture f;
        f.load("<p id=a></p>", "p { color: rgb(calc(0), calc(255 + 0), calc(140 - 139 - 1)) }");
        expect_value(f, f.find_id("a"), "color", "rgb(0, 255, 0)", "calc inside rgb()");
    }
    {
        // At the cascade level, the same arithmetic in a length property is
        // still an invalid declaration, so the earlier one wins.
        fixture f;
        f.load("<p id=a></p>", "p { width: 5px; width: calc(2 * 3) }");
        expect_value(f, f.find_id("a"), "width", "5px", "a number is not a length");
    }
    {
        // The comparison functions, end to end. `clamp(1rem, 2vw, 3rem)` with a
        // 1000px viewport is 20px.
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
        // A comparison that needs a containing block keeps its text and its
        // declaration (CSS Values 4 §10.11); layout resolves it later.
        fixture f;
        f.load("<p id=a></p>", "p { width: min(10px, 5%) }");
        expect_value(f, f.find_id("a"), "width", "min(10px, 5%)", "unresolved, not invalid");
    }
}

// The line-height units, CSS Values 4 §6.1.1. `lh` is the element's own line
// height everywhere but in `line-height` and `font-size`, where it is the
// parent's - the same asymmetry as `em` - and `rlh` is the root's, which on
// the root itself resolves against the initial values. `normal` is 1.25 times
// the font size, layout's factor. lh-unit-001, rlh-on-root-lengths,
// rlh-invalidation and lh-rlh-on-root-001 are the corpus files.
void test_the_line_height_units() {
    {
        fixture f;
        f.load("<div id=o><aside id=a></aside></div>",
               "div { font-size: 50px; line-height: 1 } aside { font-size: 42px; line-height: 2lh;"
               " width: 1lh; padding-left: 1rlh }");
        expect_value(f, f.find_id("a"), "line-height", "100px",
                     "lh in line-height is the parent's");
        expect_value(f, f.find_id("a"), "width", "100px", "lh elsewhere is the element's own");
        expect_value(f, f.find_id("a"), "padding-left", "20px", "rlh is the root's normal");
    }
    {
        fixture f;
        f.load("<div id=a></div>", ":root { line-height: 73px; padding-left: 2rlh }"
                                   "div { width: 10rlh; height: 10ric; margin-left: 1ic }");
        expect_value(f, f.find("html"), "padding-left", "146px", "rlh on the root");
        expect_value(f, f.find_id("a"), "width", "730px", "rlh below the root");
        expect_value(f, f.find_id("a"), "height", "160px", "ric falls back to 1rem");
        expect_value(f, f.find_id("a"), "margin-left", "16px", "ic falls back to 1em");
    }
    {
        // A unitless line-height inherits as a factor, so the child's lh is
        // against its own font size; and lh in font-size is the parent's.
        fixture f;
        f.load("<div id=o><p id=a></p></div>",
               "div { font-size: 20px; line-height: 2 } p { font-size: 1lh; width: 1lh }");
        expect_value(f, f.find_id("a"), "font-size", "40px", "lh in font-size is the parent's");
        expect_value(f, f.find_id("a"), "width", "80px", "the factor against the new size");
    }
    {
        // The root's own font-size and line-height resolve lh and rlh against
        // the initial values, whatever the root declares.
        fixture f;
        f.load("<p id=a></p>", "html { font-size: 1lh; line-height: 1rlh } p { width: 1rlh }");
        expect_value(f, f.find("html"), "font-size", "20px", "lh on the root's font-size");
        expect_value(f, f.find("html"), "line-height", "20px", "rlh on the root's line-height");
        expect_value(f, f.find_id("a"), "width", "20px", "and rlh below it");
    }
    {
        // A percentage in line-height is of the font size, and a font-size's is
        // of the parent's - so typed arithmetic over one has an answer here:
        // `calc(10% / 1px)` is the number 1 (typed_arithmetic).
        fixture f;
        f.load("<div id=o><p id=a></p><p id=b></p></div>",
               "div { font-size: 10px; line-height: 20px } p { font-size: calc(50% + 5px) }"
               "#a { line-height: calc(10% / 1px) } #b { line-height: calc(1lh / 1px) }");
        expect_value(f, f.find_id("a"), "font-size", "10px", "percent of the parent's size");
        expect_value(f, f.find_id("a"), "line-height", "1", "a ratio of the font size");
        expect_value(f, f.find_id("b"), "line-height", "20", "the parent's lh in pixels");
    }
    {
        // An `em` in any other font-* property is the parent's size too.
        fixture f;
        f.load("<div id=o><p id=a></p></div>",
               "div { font-size: 10px } p { font-size: 100px; font-weight: calc(1em / 1px);"
               " font-style: oblique calc(1em / 1px * 1deg); letter-spacing: 1em }");
        expect_value(f, f.find_id("a"), "font-weight", "10", "em in font-weight is the parent's");
        expect_value(f, f.find_id("a"), "font-style", "oblique 10deg", "and in font-style");
        expect_value(f, f.find_id("a"), "letter-spacing", "100px", "but not elsewhere");
    }
    {
        // The viewport's other spellings: no dynamic toolbar and a horizontal
        // writing mode make all six of each axis one number.
        fixture f;
        ctbrowser::style::css::media_environment env;
        env.viewport_width = 200;
        env.viewport_height = 100;
        (void)f.styles.set_environment(env);
        f.load("<p id=a></p>", "p { width: calc(1dvw + 1svb); height: 100lvi; top: 1vmax }");
        expect_value(f, f.find_id("a"), "width", "3px", "dvw and svb");
        expect_value(f, f.find_id("a"), "height", "200px", "lvi is the width");
        expect_value(f, f.find_id("a"), "top", "2px", "vmax");
    }
}

// `cap` takes the ascent when no cap height is known (CSS Values 4 §6.1.1),
// which is what makes `-0cap` a signed zero rather than an unresolved term
// (typed_arithmetic); an infinite length beside an opposite infinite
// percentage keeps both terms specified and is zero computed
// (calc-infinity-nan-computed); and random() draws by ordinal - the first
// random() of a value shares with the first of another whatever component it
// sits in, and two inside one calc() are two (random-computed, random-in-if).
void test_cap_infinities_and_random_ordinals() {
    using ctbrowser::style::css::check_declaration;
    using ctbrowser::style::css::fold_math;
    using ctbrowser::style::css::length_context;
    length_context ctx;
    ctx.font_size = 10.0f;
    CHECK_EQ(fold_math("calc(1cap + 1px)", ctx).text, std::string{"9px"});
    CHECK_EQ(fold_math("clamp(-1, 1 / sign(atan2(-0cap / 1px, 0em / 1px)), 1)", ctx).text,
             std::string{"-1"});
    CHECK_EQ(check_declaration("width", "calc(infinity * 1px - infinity * 1%)").serialized,
             std::string{"calc(-infinity * 1% + infinity * 1px)"});
    CHECK_EQ(fold_math("calc(-infinity * 1% + infinity * 1px)", ctx).text, std::string{"0px"});

    ctx.property = "animation-iteration-count";
    ctx.element_key = 1;
    const std::string two = fold_math("random(property-index-scoped, 0, 300000), "
                                      "random(property-index-scoped, 0, 300000)",
                                      ctx)
                                .text;
    const std::string one = fold_math("300, random(property-index-scoped, 0, 300000)", ctx).text;
    const std::size_t comma = two.find(", ");
    CHECK(comma != std::string::npos);
    CHECK_EQ(one, "300, " + two.substr(0, comma));
    CHECK(two.substr(0, comma) != two.substr(comma + 2));
    // Two inside one calc() draw differently - the sum is not double either.
    ctx.property = "scale";
    const std::string sum = fold_math("calc(random(property-index-scoped, 0, 1) + "
                                      "random(property-index-scoped, 0, 1))",
                                      ctx)
                                .text;
    const std::string first = fold_math("random(property-index-scoped, 0, 1)", ctx).text;
    CHECK(std::fabs(std::stod(sum) - 2 * std::stod(first)) > 1e-9);
    // The bounds are bounds even when they are identifiers: `infinity` is
    // the first bound and A wins an out-of-order range.
    CHECK_EQ(check_declaration("scale", "random(infinity, 100)").serialized,
             std::string{"random(element-scoped ua-scale-1, infinity, 100)"});
    CHECK_EQ(fold_math("random(NaN, 100)", ctx).text, std::string{"0"});
}

// Nesting and blow-up are capped, and past a cap the math is invalid or left
// as written - never a stack overflow and never billions of steps.
void test_math_nesting_and_budgets() {
    using ctbrowser::style::css::check_declaration;
    using ctbrowser::style::css::evaluate_math;
    using ctbrowser::style::css::length_context;
    using ctbrowser::style::css::math_outcome;
    using ctbrowser::style::css::simplify_math;
    const auto nest = [](std::string_view open, std::string_view inner, std::string_view close,
                         int levels) {
        std::string out;
        for (int i = 0; i < levels; ++i) { out += open; }
        out += inner;
        for (int i = 0; i < levels; ++i) { out += close; }
        return out;
    };
    const length_context ctx;
    // Five thousand `(` inside one calc(): the evaluator recurses per level, so
    // this must hit the depth cap.
    CHECK(evaluate_math(nest("(", "1px", ")", 5000), ctx).outcome == math_outcome::invalid);
    CHECK(!check_declaration("width", "calc(" + nest("(", "1px", ")", 5000) + ")").valid);
    // Within the cap it is still arithmetic.
    CHECK(evaluate_math(nest("(", "1px", ")", 50), ctx).outcome == math_outcome::resolved);
    // A 369-byte nest of calc()s around a sum that will not fold.
    const std::string calcs = nest("calc(", "1px + 1em", " + 1vw)", 30);
    CHECK(calcs.size() > 300);
    CHECK(check_declaration("width", calcs).valid);
    // Comparisons thirty deep simplify each argument more than one way: 2^30
    // simplifications without the step budget.
    const std::string mins = nest("min(", "1em", ", 2vw)", 30);
    CHECK(!simplify_math(mins).empty());
    CHECK(check_declaration("width", mins).valid);
    // A function symbol times a sum of units, repeatedly, squares its symbols.
    std::string product = "calc(sibling-index() * 1px";
    for (int i = 0; i < 24; ++i) { product += " * (1em + 1vw) / 1px"; }
    product += ")";
    (void)check_declaration("width", product);
    (void)simplify_math(product);
}

} // namespace

int main() {
    test_calc();
    test_math_answers_with_a_number();
    test_comparison_functions();
    test_calc_in_the_cascade();
    test_the_line_height_units();
    test_cap_infinities_and_random_ordinals();
    test_math_nesting_and_budgets();
    REPORT("style_calc");
}
