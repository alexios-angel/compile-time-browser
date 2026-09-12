// The `css/css-values` behaviours fixed against the WPT run of 2026-09-10 -
// one case per behaviour, each named after the file it moves. css_values.cpp
// asks what a specified value is worth; this asks the questions the corpus
// asked and got wrong: what a value COMPUTES to, and what the evaluator does
// at the edges of its number line.

#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "cssom_probe.hpp"
#include "style_fixture.hpp"

#include <limits>
#include <string>
#include <string_view>

using ctbrowser::style::css::computed_position;

namespace {

// position/position-computed.tentative: a `<position>`'s keywords compute to
// percentages, and the flow-relative ones through the writing mode.
void test_a_position_computes_to_percentages() {
    const auto horizontal = [](std::string_view specified) {
        return computed_position(specified, "horizontal-tb", "ltr");
    };
    CHECK_EQ(horizontal("10% center"), std::string{"10% 50%"});
    CHECK_EQ(horizontal("right 30% top 60px"), std::string{"70% 60px"});
    CHECK_EQ(horizontal("-20% -30px"), std::string{"-20% -30px"});
    CHECK_EQ(horizontal("40px top"), std::string{"40px 0%"});
    CHECK_EQ(horizontal("right bottom"), std::string{"100% 100%"});
    CHECK_EQ(horizontal("center"), std::string{"50% 50%"});
    CHECK_EQ(horizontal("bottom"), std::string{"50% 100%"});
    CHECK_EQ(horizontal("right 20px bottom 10px"),
             std::string{"calc(100% - 20px) calc(100% - 10px)"});
    CHECK_EQ(horizontal("x-end 10px top 20px"), std::string{"calc(100% - 10px) 20px"});
    CHECK_EQ(horizontal("left 10px y-end 20%"), std::string{"10px 80%"});
    // A math function is a component like any other and keeps its text -
    // getComputedStyle-calc-mixed-units-001's `background-position`.
    CHECK_EQ(horizontal("calc(0% + 320px)"), std::string{"calc(0% + 320px) 50%"});
    // The flow-relative keywords follow the writing mode.
    CHECK_EQ(computed_position("x-start", "horizontal-tb", "rtl"), std::string{"100% 50%"});
    CHECK_EQ(computed_position("y-start", "vertical-rl", "rtl"), std::string{"50% 100%"});
    CHECK_EQ(computed_position("x-start", "vertical-rl", "ltr"), std::string{"100% 50%"});
    CHECK_EQ(computed_position("y-start", "sideways-lr", "ltr"), std::string{"50% 100%"});
    CHECK_EQ(computed_position("y-start", "sideways-lr", "rtl"), std::string{"50% 0%"});
    // Not a position this reader resolves: the caller keeps what it had.
    CHECK_EQ(horizontal("left 4px top"), std::string{});
    CHECK_EQ(horizontal("garbage"), std::string{});
}

// calc-infinity-nan-computed: at computed-value time a NaN is zero and an
// infinity is the bound it overflowed. The specified value keeps its calc().
void test_infinity_and_nan_are_clamped_when_computed() {
    using ctbrowser::style::css::fold_math;
    using ctbrowser::style::css::length_context;
    using ctbrowser::style::css::math_context;
    using ctbrowser::style::css::simplify_math;
    const length_context ctx;
    CHECK_EQ(fold_math("calc(NaN * 1px)", ctx, math_context::length).text, std::string{"0px"});
    CHECK_EQ(fold_math("calc(NaN * 1%)", ctx, math_context::length).text, std::string{"0%"});
    CHECK_EQ(fold_math("calc(infinity * 1px)", ctx, math_context::length).text,
             std::string{"33554432px"});
    CHECK_EQ(fold_math("calc(-infinity * 1%)", ctx, math_context::length).text,
             std::string{"-33554432%"});
    CHECK_EQ(fold_math("calc(NaN * 1s)", ctx, math_context::any).text, std::string{"0s"});
    CHECK_EQ(fold_math("calc(infinity)", ctx, math_context::any).text, std::string{"33554432"});
    CHECK_EQ(fold_math("calc(NaN)", ctx, math_context::integer).text, std::string{"0"});
    // Two infinities that cancel for any basis are a NaN, and so zero - a
    // plain zero length, with nothing left for a basis to resolve.
    CHECK_EQ(fold_math("calc(infinity * 1px - infinity * 1%)", ctx, math_context::length).text,
             std::string{"0px"});
    CHECK_EQ(fold_math("calc(infinity * 1px + infinity * 1%)", ctx, math_context::length).text,
             std::string{"calc(33554432% + 33554432px)"});
    // ...and only when computed: `el.style` reads the calc() back.
    CHECK_EQ(simplify_math("calc(NaN * 1px)"), std::string{"calc(NaN * 1px)"});
    CHECK_EQ(simplify_math("calc(1 / 0)"), std::string{"calc(infinity)"});
}

// round-function, signed-zero and signs-abs-computed: the step's sign is
// ignored and defaults to 1, the two zeros stay distinct through min(), max(),
// clamp() and mod(), and a specified `-0em` keeps its sign.
void test_the_evaluator_at_zero_and_around_a_step() {
    using ctbrowser::style::css::evaluate_math;
    using ctbrowser::style::css::length_context;
    using ctbrowser::style::css::math_outcome;
    using ctbrowser::style::css::simplify_math;
    const length_context ctx;
    const auto number = [&](std::string_view expression) {
        const auto answer = evaluate_math(expression, ctx);
        CHECK(answer.outcome == math_outcome::resolved);
        return answer.value.px;
    };
    CHECK_EQ(number("round(15px, -10px)"), 20.0);
    CHECK_EQ(number("round(15, -10)"), 20.0);
    CHECK_EQ(number("round(1.5)"), 2.0);
    CHECK_EQ(number("round(down, 1.5)"), 1.0);
    CHECK(evaluate_math("round(1.5px)", ctx).outcome == math_outcome::invalid);
    // The sign of a zero, read the way the corpus reads it: through 1 / sign().
    CHECK_EQ(number("1 / sign(min(0, -0))"), -std::numeric_limits<double>::infinity());
    CHECK_EQ(number("1 / sign(max(-0, 0))"), std::numeric_limits<double>::infinity());
    CHECK_EQ(number("1 / sign(clamp(-0, 0, 0))"), std::numeric_limits<double>::infinity());
    CHECK_EQ(number("1 / sign(mod(-1, -1))"), -std::numeric_limits<double>::infinity());
    CHECK_EQ(number("1 / sign(mod(1, -1))"), -std::numeric_limits<double>::infinity());
    CHECK_EQ(number("1 / sign(rem(-1, 1))"), -std::numeric_limits<double>::infinity());
    CHECK_EQ(simplify_math("sign(sign(-0em))"), std::string{"sign(sign(-0em))"});
}

// clamp-partial-serialize.tentative: a clamp() with an absent bound is the
// comparison that is left.
void test_a_clamp_with_an_absent_bound_is_a_comparison() {
    using ctbrowser::style::css::simplify_math;
    CHECK_EQ(simplify_math("clamp(none, 2px, 3em)"), std::string{"min(2px, 3em)"});
    CHECK_EQ(simplify_math("calc(clamp(1em, 2px, none))"), std::string{"max(1em, 2px)"});
    CHECK_EQ(simplify_math("clamp(1px, 2px, clamp(none, 4px, 5em))"),
             std::string{"clamp(1px, 2px, min(4px, 5em))"});
    CHECK_EQ(simplify_math("clamp(clamp(none, 2em, none), 4px, clamp(none, 6em, none))"),
             std::string{"clamp(2em, 4px, 6em)"});
    // minmax-length-percent-serialize: a comparison that waits for a containing
    // block still computes its arguments against the bases it has.
    using ctbrowser::style::css::fold_math;
    using ctbrowser::style::css::length_context;
    length_context ctx;
    ctx.font_size = 16.0f;
    CHECK_EQ(fold_math("min(1em, 10%)", ctx).text, std::string{"min(16px, 10%)"});
    CHECK_EQ(fold_math("max(10% + 30px, 5em + 5%)", ctx).text,
             std::string{"max(10% + 30px, 5% + 80px)"});
    CHECK_EQ(fold_math("clamp(none, 1em, 10%)", ctx).text, std::string{"min(16px, 10%)"});
    CHECK_EQ(fold_math("min(1em, max(10%, 2em))", ctx).text,
             std::string{"min(16px, max(10%, 32px))"});
    CHECK_EQ(fold_math("min(1em, 2em)", ctx).text, std::string{"16px"});
}

// calc-numbers and calc-rounds-to-integer: a math function's result is clamped
// to the property's range at computed-value time, and an <integer> property
// refuses a <number-token> that is not one while rounding a calc() that is.
void test_the_range_of_a_property_is_applied_when_computed() {
    using ctbrowser::style::css::check_declaration;
    using ctbrowser::style::css::non_negative;
    CHECK_EQ(non_negative("-8"), std::string{"0"});
    CHECK_EQ(non_negative("-8px"), std::string{"0px"});
    CHECK_EQ(non_negative("-8%"), std::string{"0%"});
    CHECK_EQ(non_negative("8"), std::string{"8"});
    CHECK_EQ(non_negative("-8px -8px"), std::string{"-8px -8px"});
    {
        fixture f;
        f.load("<p id=a></p>", "p { tab-size: calc(2 * -4); width: calc(1px * -10) }");
        expect_value(f, f.find_id("a"), "tab-size", "0", "tab-size has a floor at zero");
        expect_value(f, f.find_id("a"), "width", "0px", "and so does width");
    }
    {
        fixture f;
        f.load("<p id=a></p>", "p { orphans: calc(10.1); widows: 3 }");
        expect_value(f, f.find_id("a"), "orphans", "10", "an integer property rounds its calc");
        expect_value(f, f.find_id("a"), "widows", "3", "widows is a property");
    }
    CHECK(!check_declaration("orphans", "1e1").valid);
    CHECK(!check_declaration("widows", "10.1").valid);
    CHECK(check_declaration("orphans", "calc(1e1)").valid);
    CHECK(!check_declaration("column-span", "10").valid);
    CHECK(check_declaration("column-span", "all").valid);
}

// typed_arithmetic and getComputedStyle-calc-mixed-units-003: two dimensions
// multiply and divide into a type of their own, and a type no property takes is
// still a syntax error as the whole answer.
void test_typed_arithmetic() {
    using ctbrowser::style::css::check_declaration;
    using ctbrowser::style::css::evaluate_math;
    using ctbrowser::style::css::fold_math;
    using ctbrowser::style::css::length_context;
    using ctbrowser::style::css::math_context;
    using ctbrowser::style::css::math_outcome;
    length_context ctx;
    ctx.font_size = 10.0f;
    ctx.root_font_size = 10.0f;
    const auto number = [&](std::string_view expression) {
        const auto answer = evaluate_math(expression, ctx);
        CHECK(answer.outcome == math_outcome::resolved);
        return answer.value.px;
    };
    CHECK_EQ(number("min(1em, 110px / 10px * 1px)"), 10.0);
    CHECK_EQ(number("max(1em + 2px, 110px / 10px * 1px)"), 12.0);
    CHECK_EQ(number("3 + sign(10px / 1rem - sign(1em + 1px))"), 3.0);
    CHECK_EQ(number("10em / 1px"), 100.0);
    CHECK_EQ(number("1px * 3deg / 1deg / 1px"), 3.0);
    // A percentage rides through a product as a percentage of the basis.
    CHECK_EQ(fold_math("calc(20% * 0.5em / 1px)", ctx, math_context::length).text,
             std::string{"100%"});
    CHECK_EQ(fold_math("calc(1px * 10em / 0em)", ctx, math_context::length).text,
             std::string{"33554432px"});
    // ...and one that would need the basis twice, or as a divisor, waits.
    CHECK(evaluate_math("10% * 10%", ctx).outcome == math_outcome::unresolved);
    CHECK(evaluate_math("52px * 1px / 10%", ctx).outcome == math_outcome::unresolved);
    CHECK(evaluate_math("10% / 1px", ctx).outcome == math_outcome::unresolved);
    // A percentage over a percentage cancels its basis.
    CHECK_EQ(number("10% / 20%"), 0.5);
    CHECK(evaluate_math("(10% + 1px) / 20%", ctx).outcome == math_outcome::unresolved);
    // The container units fall back to the small viewport without a container.
    ctx.viewport_width = 1024.0f;
    ctx.viewport_height = 768.0f;
    CHECK_EQ(number("sign(0cqi / 1px)"), 0.0);
    CHECK_EQ(number("10cqw"), 102.4);
    CHECK_EQ(number("10cqmin"), 76.8);
    // ...and the bases are applied in double: `mod(18vw, 5vw)` is exactly what
    // `3vw` is, which single-precision bases put five millionths off
    // (round-mod-rem-computed).
    CHECK_EQ(static_cast<float>(number("mod(18vw, 5vw)")), static_cast<float>(number("3vw")));
    // calc-unit-analysis: an area and an inverse length are still invalid.
    CHECK(evaluate_math("calc(2px * 1px)", ctx).outcome == math_outcome::invalid);
    CHECK(evaluate_math("calc(20 / 0.75rem)", ctx).outcome == math_outcome::invalid);
    CHECK(evaluate_math("(1% * 1deg) / 1px", ctx).outcome == math_outcome::invalid);
    CHECK_EQ(check_declaration("margin-left", "calc(110px / 10px * 1px)").serialized,
             std::string{"calc(11px)"});
    CHECK(!check_declaration("width", "calc(2px * 1px)").valid);
}

// calc-mix-serialize, calc-mix-computed: a weighted sum whose weights are
// normalised like color-mix()'s - filled equally when omitted, scaled down
// over 100%, not scaled up under - and whose specified value folds only when
// every value in it has a magnitude.
void test_calc_mix() {
    using ctbrowser::style::css::check_declaration;
    using ctbrowser::style::css::fold_math;
    using ctbrowser::style::css::length_context;
    const auto specified = [](std::string_view property, std::string_view v) {
        return check_declaration(property, v).serialized;
    };
    CHECK_EQ(specified("scale", "calc-mix(1 100%)"), std::string{"calc(1)"});
    CHECK_EQ(specified("scale", "calc-mix(1 50%, 3 50%)"), std::string{"calc(2)"});
    CHECK_EQ(specified("scale", "calc-mix(1 25%, 3 25%, 7 0%, 5 50%)"), std::string{"calc(3.5)"});
    CHECK_EQ(specified("scale", "calc-mix(1 50%, 3)"), std::string{"calc(2)"});
    CHECK_EQ(specified("scale", "calc-mix(1 75%, 3 75%, 5, 7)"), std::string{"calc(2)"});
    CHECK_EQ(specified("scale", "calc-mix(1 25%, 3 25%, 5, 7)"), std::string{"calc(4)"});
    CHECK_EQ(specified("scale", "calc-mix(1 25%, 3 25%)"), std::string{"calc(1)"});
    CHECK_EQ(specified("text-indent", "calc-mix(10px 75%, 3em 75%)"),
             std::string{"calc-mix(10px 50%, 3em 50%)"});
    CHECK_EQ(specified("text-indent", "calc-mix(10px 75%, 3em 75%, 5em, 7em)"),
             std::string{"calc-mix(10px 50%, 3em 50%)"});
    CHECK_EQ(specified("text-indent", "calc-mix(10px 25%, 3em 25%, 5em, 7em)"),
             std::string{"calc-mix(10px 25%, 3em 25%, 5em 25%, 7em 25%)"});
    CHECK_EQ(specified("text-indent", "calc(10px + calc-mix(1% 0%, 3px 0%))"),
             std::string{"calc(0% + 10px)"});
    CHECK_EQ(specified("text-indent", "calc-mix(1% 0%, 3% 0%, 5% * sibling-index() 0%)"),
             std::string{"calc(0%)"});
    CHECK_EQ(specified("scale", "calc-mix(1 * sibling-index() 50%, 3 50%, 5, 7)"),
             std::string{"calc-mix(1 * sibling-index() 50%, 3 50%)"});
    CHECK_EQ(specified("scale", "calc-mix(1 calc(50% * sibling-index()), 3)"),
             std::string{"calc-mix(1 calc(50% * sibling-index()), 3)"});
    CHECK_EQ(specified("scale", "calc-mix(1 calc(150%), 3 100%)"),
             std::string{"calc-mix(1 calc(150%), 3 100%)"});
    CHECK_EQ(specified("scale", "calc(sign(calc-mix(1 50%, 3 50%)) + 10)"),
             std::string{"calc(11)"});
    CHECK_EQ(specified("scale", "calc-mix(1px 50%, 3 50%)"), std::string{});
    CHECK_EQ(specified("scale", "calc-mix(1 150%, 3)"), std::string{});
    CHECK_EQ(specified("scale", "calc-mix(1 -50%, 3)"), std::string{});
    CHECK_EQ(specified("scale", "calc-mix(1 * sibling-index() 0%, 3 100%)"),
             std::string{"calc(3)"});

    length_context ctx;
    ctx.font_size = 10.0f;
    ctx.sibling_index = 1;
    ctx.sibling_count = 1;
    const auto fold = [&](std::string_view v) { return fold_math(v, ctx).text; };
    CHECK_EQ(fold("calc-mix(1 25%, 3 25%, 7 0%, 5 50%)"), std::string{"3.5"});
    CHECK_EQ(fold("calc-mix(10px 75%, 3em 75%)"), std::string{"20px"});
    CHECK_EQ(fold("calc-mix(10em 50%, 30px 50%)"), std::string{"65px"});
    CHECK_EQ(fold("calc-mix(1 calc(150%), 3 100%)"), std::string{"2"});
    CHECK_EQ(fold("calc-mix(1 calc(-50%), 3 100%)"), std::string{"3"});
    CHECK_EQ(fold("calc-mix(1 * sibling-index() 50%, 3 50%)"), std::string{"2"});
    CHECK_EQ(fold("calc(10px + calc-mix(10% 50%, 30% 50%))"), std::string{"calc(20% + 10px)"});
}

// random-computed: random() picks between its bounds on a base that is fixed
// per key, so the same declaration lands on the same value, and the corners
// of its range are the specification's.
void test_random() {
    using ctbrowser::style::css::fold_math;
    using ctbrowser::style::css::length_context;
    length_context ctx;
    ctx.property = "width";
    ctx.element_key = 7;
    const auto fold = [&](std::string_view v) { return fold_math(v, ctx).text; };
    CHECK_EQ(fold("random(fixed 0.5, 1px, 3px)"), std::string{"2px"});
    CHECK_EQ(fold("random(fixed 0.5, 0, 10, 5)"), std::string{"5"});
    CHECK_EQ(fold("random(fixed 0.99, 0, 10, by 5)"), std::string{"10"});
    CHECK_EQ(fold("random(100, 10)"), std::string{"100"});
    CHECK_EQ(fold("random(NaN, 100)"), std::string{"0"});
    CHECK_EQ(fold("random(infinity, 100)"), std::string{"33554432"});
    CHECK_EQ(fold("random(10, infinity)"), std::string{"0"});
    CHECK_EQ(fold("random(10, 100, infinity)"), std::string{"10"});
    CHECK_EQ(fold("random(fixed random(-2, -1), 10%, 100%)"),
             std::string{"random(fixed 0, 10%, 100%)"});
    // Deterministic per key: the same again is the same, another element or
    // another position is not, and a shared name or scope is shared.
    const std::string one = fold("random(0, 1000000)");
    CHECK_EQ(fold("random(0, 1000000)"), one);
    CHECK(fold("random(0, 1000000) random(0, 1000000)") != one + " " + one);
    CHECK_EQ(fold("random(property-index-scoped, 0, 1000000)"),
             fold("random(property-index-scoped, 0, 1000000)"));
    ctx.element_key = 8;
    CHECK(fold("random(0, 1000000)") != one);
    CHECK_EQ(fold("random(--k, 0, 1000000)"), [&] {
        length_context other = ctx;
        other.element_key = 9;
        other.property = "height";
        return fold_math("random(--k, 0, 1000000)", other).text;
    }());
    const std::string waiting = fold("random(10%, 100%)");
    CHECK(waiting.starts_with("random(fixed 0.") && waiting.ends_with(", 10%, 100%)"));
    CHECK_EQ(fold(waiting), waiting);
    // A UA ident is a key like a dashed name: shared across elements, and
    // replaced by the base it drew when the value waits (random-computed).
    const std::string ua = fold("random(ua-width-1, 10%, 100%)");
    CHECK(ua.starts_with("random(fixed 0.") && ua.ends_with(", 10%, 100%)"));
    CHECK_EQ(fold("random(ua-width-1, 0, 1000000)"), [&] {
        length_context other = ctx;
        other.element_key = 9;
        return fold_math("random(ua-width-1, 0, 1000000)", other).text;
    }());
}

// tree-counting/calc-sibling-function and the trig, exp and sqrt "computed"
// files: sibling-index() and sibling-count() resolve in the cascade, where the
// element is, and nowhere else.
void test_the_tree_counting_functions() {
    using ctbrowser::style::css::check_declaration;
    using ctbrowser::style::css::fold_math;
    using ctbrowser::style::css::length_context;
    using ctbrowser::style::css::math_context;
    using ctbrowser::style::css::simplify_math;
    length_context third;
    third.sibling_index = 3;
    third.sibling_count = 5;
    CHECK_EQ(fold_math("calc(sibling-index() * 2)", third, math_context::integer).text,
             std::string{"6"});
    CHECK_EQ(fold_math("foo calc(sibling-count())", third, math_context::any).text,
             std::string{"foo 5"});
    CHECK_EQ(fold_math("calc(10% + 100px * sibling-index())", third, math_context::length).text,
             std::string{"calc(10% + 300px)"});
    // No element, no answer: the specified value keeps the function.
    CHECK_EQ(fold_math("calc(sibling-index())", length_context{}, math_context::integer).text,
             std::string{"calc(sibling-index())"});
    CHECK_EQ(simplify_math("calc(1px * sibling-index( ))"),
             std::string{"calc(1px * sibling-index())"});
    CHECK(!check_declaration("left", "calc(1px * sibling-index(100px))").valid);
    {
        fixture f;
        f.load("<div><p></p><p id=a></p><p></p></div>",
               "#a { z-index: calc(sibling-index()); order: calc(sibling-count() * 2);"
               "     left: calc(10% + 100px * sibling-index()) }");
        expect_value(f, f.find_id("a"), "z-index", "2", "the second of three");
        expect_value(f, f.find_id("a"), "order", "6", "of three");
        expect_value(f, f.find_id("a"), "left", "calc(10% + 200px)", "inside a sum");
    }
}

// attr-all-types, attr-argument-grammar, attr-length-specified: attr() is a
// substitution like var(), judged by the type it asks for.
void test_attr_substitution() {
    using ctbrowser::style::css::attribute_lookup;
    using ctbrowser::style::css::custom_lookup;
    using ctbrowser::style::css::substitute_var;
    atom_table atoms;
    const custom_lookup none = [](atom) -> std::optional<std::string_view> { return std::nullopt; };
    const attribute_lookup attrs = [](std::string_view name) -> std::optional<std::string> {
        if (name == "data-foo") { return "10"; }
        if (name == "data-str") { return "ab\"c"; }
        if (name == "data-len") { return "3EM"; }
        if (name == "data-calc") { return "calc(1px + 3px)"; }
        if (name == "data-empty") { return ""; }
        if (name == "data-nested") { return "attr(data-foo type(*), 2px)"; }
        if (name == "data-ring") { return "attr(data-ring2 type(*), 2px)"; }
        if (name == "data-ring2") { return "attr(data-ring type(*), 3px)"; }
        return std::nullopt;
    };
    const auto sub = [&](std::string_view value) {
        return substitute_var(value, none, atoms, attrs).value_or("<invalid>");
    };
    // One attribute read through another, and a ring of them: the ring is a
    // cycle, invalid throughout, and only the outermost fallback applies
    // (attr-cycle).
    CHECK_EQ(sub("attr(data-nested type(*), 1px)"), std::string{"10"});
    CHECK_EQ(sub("attr(data-ring type(*), 1px)"), std::string{"1px"});
    CHECK_EQ(sub("attr(data-ring type(*))"), std::string{"<invalid>"});
    // No type: a string, whatever the text says.
    CHECK_EQ(sub("attr(data-foo)"), std::string{"\"10\""});
    CHECK_EQ(sub("attr(data-str)"), std::string{"\"ab\\\"c\""});
    CHECK_EQ(sub("attr(data-empty)"), std::string{"\"\""});
    CHECK_EQ(sub("attr(missing)"), std::string{"\"\""});
    CHECK_EQ(sub("attr(missing, serif)"), std::string{"serif"});
    CHECK_EQ(sub("attr(missing raw-string)"), std::string{"<invalid>"});
    // A unit: a bare number gains it.
    CHECK_EQ(sub("attr(data-foo px)"), std::string{"10px"});
    CHECK_EQ(sub("attr(data-foo %)"), std::string{"10%"});
    CHECK_EQ(sub("attr(data-foo number)"), std::string{"10"});
    CHECK_EQ(sub("calc(attr(data-foo px) + 1px)"), std::string{"calc(10px + 1px)"});
    CHECK_EQ(sub("attr(data-calc px)"), std::string{"<invalid>"});
    CHECK_EQ(sub("attr(data-foo xx, 3px)"), std::string{"3px"});
    // A syntax: the text must parse as the type.
    CHECK_EQ(sub("attr(data-foo type(<number>))"), std::string{"10"});
    CHECK_EQ(sub("attr(data-foo type(<length>), 3px)"), std::string{"3px"});
    CHECK_EQ(sub("attr(data-foo type(<length>))"), std::string{"<invalid>"});
    CHECK_EQ(sub("attr(data-len type(<length>))"), std::string{"3em"});
    CHECK_EQ(sub("attr(data-calc type(<length>))"), std::string{"calc(1px + 3px)"});
    CHECK_EQ(sub("attr(data-foo type(<number> | lighter | bold))"), std::string{"10"});
    CHECK_EQ(sub("attr(data-str type(<string>), x)"), std::string{"x"});
    CHECK_EQ(sub("attr(data-foo type(*)) 11"), std::string{"10 11"});
    // Malformed argument lists are syntax errors whatever the attribute holds.
    CHECK_EQ(sub("attr(data-foo type(< number>))"), std::string{"<invalid>"});
    CHECK_EQ(sub("attr(data-foo type(<url>))"), std::string{"<invalid>"});
    CHECK_EQ(sub("attr(data-foo type(<number>) !)"), std::string{"<invalid>"});
    CHECK_EQ(sub("attr(!)"), std::string{"<invalid>"});
    // ...and a var() whose name is followed by anything but a comma is too.
    CHECK_EQ(sub("var(--foo type(*))"), std::string{"<invalid>"});
    {
        fixture f;
        f.load("<p id=a data-foo=\"10\" data-name=\"anim\"></p>",
               "p { --x: data-foo type(<number>); font-weight: attr(var(--x));"
               "    content: attr(data-name); width: calc(attr(data-foo px) + 1px);"
               "    animation-name: attr(data-name type(<custom-ident>));"
               "    opacity: 0.5; opacity: attr(data-name type(<number>)) }");
        expect_value(f, f.find_id("a"), "font-weight", "10", "attr(var())");
        expect_value(f, f.find_id("a"), "content", "\"anim\"", "the string form");
        expect_value(f, f.find_id("a"), "width", "11px", "a unit inside a calc");
        expect_value(f, f.find_id("a"), "animation-name", "anim", "a custom ident");
        expect_value(f, f.find_id("a"), "opacity", "", "no match, no fallback: unset");
    }
}

// The getComputedStyle half, through a page: a position resolved against the
// writing mode, an opacity as a number in [0, 1], a custom property with its
// substitutions performed, and a background-position given its second half.
void test_what_a_page_reads_back() {
    using ctbrowser::shell::browser;
    using ctbrowser::shell::browser_options;
    using ctbrowser_test::logged;
    browser page{browser_options{400, 200}};
    page.load_html(R"html(<html><body>
    <div style="writing-mode: horizontal-tb; direction: rtl">
      <div id=t data-n="10" style="object-position: x-start; opacity: 90%;
           --x: attr(data-n px) 11px; background-position: calc(100% - 100% + 20em);
           tab-size: calc(2 * -4)"></div>
    </div>
    <script>
        const cs = getComputedStyle(document.getElementById('t'));
        console.log('read=' + cs.objectPosition + '|' + cs.opacity + '|' +
                    cs.getPropertyValue('--x') + '|' + cs.backgroundPosition + '|' + cs.tabSize);
        document.getElementById('t').style.opacity = 'calc(log(0))';
        console.log('clamped=' + cs.opacity);
    </script></body></html>)html");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "read="),
             std::string{"read=100% 50%|0.9|10px 11px|calc(0% + 320px) 50%|0"});
    CHECK_EQ(logged(page, "clamped="), std::string{"clamped=0"});
}

// exp-log-serialize, minmax-number-serialize, progress-serialize and their
// kin: the computed `transform` is the product of its functions as a
// `matrix()`, `none` is `none`, and a function this engine cannot multiply
// out keeps its text rather than guessing.
void test_a_transform_computes_to_a_matrix() {
    using ctbrowser::shell::browser;
    using ctbrowser::shell::browser_options;
    using ctbrowser_test::logged;
    browser page{browser_options{400, 200}};
    page.load_html(R"html(<html><body>
    <div id=t style="transform: scale(calc(log(1) + 0.5))"></div>
    <script>
        const t = document.getElementById('t');
        const cs = getComputedStyle(t);
        const out = [cs.transform];
        for (const v of ['none', 'matrix(1, 0, 0, 1, 0, 0)', 'scale(2, 3) translate(10px, 5px)',
                         'rotate(90deg)', 'translate(10%)', 'scale(calc(NaN))']) {
            t.style.transform = v;
            out.push(cs.transform);
        }
        console.log('matrix=' + out.join('|'));
    </script></body></html>)html");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "matrix="),
             std::string{"matrix=matrix(0.5, 0, 0, 0.5, 0, 0)|none|matrix(1, 0, 0, 1, 0, 0)|"
                         "matrix(2, 0, 0, 3, 20, 15)|matrix(0, 1, -1, 0, 0, 0)|translate(10%)|"
                         "matrix(0, 0, 0, 0, 0, 0)"});
}

// rem-unit-root-element and lh-rlh-on-root-001: the root element's own
// font-size and line-height read back from the cascade, whose `rem` and `lh`
// on the root resolve against the initial values, not from the viewport's box.
void test_the_root_reads_its_own_font_size_back() {
    using ctbrowser::shell::browser;
    using ctbrowser::shell::browser_options;
    using ctbrowser_test::logged;
    browser page{browser_options{400, 200}};
    page.load_html(R"html(<html><head><style>
      :root { font-size: 50px; margin-left: 2rem; line-height: 2rem }
    </style></head><body>
    <script>
        const r = document.documentElement;
        const cs = () => getComputedStyle(r);
        const a = [cs().fontSize, cs().marginLeft, cs().lineHeight];
        r.style.fontSize = '3rem';
        a.push(cs().fontSize);
        r.style.cssText = 'font-size: 2lh; line-height: 142px';
        a.push(cs().fontSize);
        console.log('root=' + a.join('|'));
    </script></body></html>)html");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "root="), std::string{"root=50px|100px|100px|48px|40px"});
}

// getComputedStyle-border-radius-001 and -003: a corner is a pair of radii and
// the shorthand puts the horizontal four before a slash and the vertical four
// after it - no slash when the two lists agree.
void test_border_radius_reassembles_its_corners() {
    using ctbrowser::shell::browser;
    using ctbrowser::shell::browser_options;
    using ctbrowser_test::logged;
    browser page{browser_options{400, 200}};
    page.load_html(R"html(<html><body>
    <div id=a style="border-top-left-radius: 1px 5px; border-top-right-radius: 2px;
         border-bottom-right-radius: 3px 7px; border-bottom-left-radius: 4px 8px"></div>
    <div id=b style="border-radius: 2px"></div>
    <script>
        const r = (id) => getComputedStyle(document.getElementById(id)).borderRadius;
        console.log('radius=' + r('a') + '|' + r('b'));
    </script></body></html>)html");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "radius="), std::string{"radius=1px 2px 3px 4px / 5px 2px 7px 8px|2px"});
}

} // namespace

int main() {
    test_a_position_computes_to_percentages();
    test_infinity_and_nan_are_clamped_when_computed();
    test_the_evaluator_at_zero_and_around_a_step();
    test_a_clamp_with_an_absent_bound_is_a_comparison();
    test_the_range_of_a_property_is_applied_when_computed();
    test_typed_arithmetic();
    test_random();
    test_calc_mix();
    test_the_tree_counting_functions();
    test_attr_substitution();
    test_what_a_page_reads_back();
    test_a_transform_computes_to_a_matrix();
    test_the_root_reads_its_own_font_size_back();
    test_border_radius_reassembles_its_corners();
    REPORT("css_values_wpt");
}
