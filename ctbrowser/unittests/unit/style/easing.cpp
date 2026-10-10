//===- easing.cpp - Easing and interpolation tests --------------*- C++ -*-===//
//
// Timing functions (linear, ease-*, steps(), cubic-bezier()) and the value
// interpolation, addition and accumulation that animations and transitions
// use: lengths, colours, filters, grid tracks, shadows, transforms and
// calc-size().
//
//===----------------------------------------------------------------------===//
#include <ctbrowser/style/easing.hpp>

#include "check.hpp"

#include <cmath>
#include <string>

using ctbrowser::style::composite_op;
using ctbrowser::style::composite_text;
using ctbrowser::style::easing;
using ctbrowser::style::interpolable_text;
using ctbrowser::style::interpolate_text;
using ctbrowser::style::parse_easing;
using ctbrowser::style::with_currentcolor;

namespace {

void test_easing() {
    const easing linear;
    CHECK(linear(-0.25, false) == -0.25);
    CHECK(linear(1.25, true) == 1.25);
    CHECK(parse_easing(" \tLINEAR\n").value()(0.5, false) == 0.5);
    CHECK(std::fabs(parse_easing("ease-in").value()(0.5, false) - 0.3153568) < 1e-6);
    CHECK(parse_easing("step-start").value()(0.5, false) == 1);
    CHECK(parse_easing("step-end").value()(0.5, false) == 0);

    CHECK(parse_easing("steps(2147483647, end)"));
    CHECK(parse_easing("cubic-bezier(0, 1e100, 1, -1e100)"));

    const auto maximum = parse_easing("steps(2147483647)").value();
    for (const auto large : {"steps(2147483648)", "steps(9007199254740993)",
                             "steps(+99999999999999999999999999999999)"}) {
        const auto clamped = parse_easing(large).value();
        CHECK(clamped(0.5, false) == maximum(0.5, false));
        CHECK(clamped(1, true) == maximum(1, true));
    }
    const auto beyond_double = parse_easing("steps(" + std::string(400, '9') + ")").value();
    CHECK(beyond_double(0.5, false) == maximum(0.5, false));
    CHECK(parse_easing("steps(+0004)").value()(0.3, false) == 0.25);

    const auto end = parse_easing("steps(4, end)").value();
    CHECK(end(0.5, false) == 0.5);
    CHECK(end(0.5, true) == 0.25);
    CHECK(end(0, true) == 0);
    CHECK(end(1, true) == 0.75);
    CHECK(end(-0.25, false) == -0.25);
    CHECK(end(1.25, false) == 1.25);
    CHECK(parse_easing("steps(4, jump-start)").value()(0.5, false) == 0.75);
    CHECK(parse_easing("steps(4, jump-none)").value()(0.5, false) == 2.0 / 3.0);
    CHECK(parse_easing("steps(4, jump-both)").value()(0.5, true) == 2.0 / 5.0);

    const auto overshoot = parse_easing("cubic-bezier(0, -0.5, 1, -0.5)").value();
    CHECK(overshoot(0.5, false) == -0.25);
    const auto tangent = parse_easing("cubic-bezier(0.25, 0.5, 0.75, 0.5)").value();
    CHECK(tangent(-0.5, false) == -1);
    CHECK(tangent(1.5, false) == 2);

    // The parsed value owns only numbers, independent of its source buffer.
    std::string source = "steps(4, end)";
    const auto held = parse_easing(source).value();
    source.assign("linear");
    CHECK(held(0.3, false) == 0.25);
    for (const auto invalid : {"",
                               "bogus",
                               "cubic-bezier(0, 1, 1)",
                               "cubic-bezier(-1, 0, 1, 1)",
                               "cubic-bezier(0, 0, 2, 1)",
                               "steps(0)",
                               "steps(1.5)",
                               "steps(1, jump-none)",
                               "steps(2, sideways)",
                               "steps(-2147483649)",
                               "steps(-0)",
                               "steps(1.0)",
                               "steps(1e0)",
                               "steps(1e100)",
                               "steps(.4)",
                               "steps(4.)",
                               "steps(4px)",
                               "steps(4/**/5)",
                               "steps(4/**)",
                               "steps(inf)",
                               "cubic-bezier(nan, 0, 1, 1)",
                               "cubic-bezier(0, inf, 1, 1)",
                               "cubic-bezier(0, 0, 1, -infinity)"}) {
        CHECK(!parse_easing(invalid));
    }
}

void test_interpolation() {
    ctbrowser::style::css::length_context context;
    context.font_size = 20;
    CHECK(interpolate_text("left", "0px", "100px", -0.25, context) == "-25px");
    CHECK(interpolate_text("left", "0px", "100px", 1.25, context) == "125px");
    CHECK(interpolate_text("left", "1em", "40px", 0.5, context) == "30px");
    // A lone colour is not a gap rule list, whatever commas it holds inside;
    // treating it as one recursed until the stack ran out.
    CHECK_EQ(
        interpolate_text("column-rule-color", "rgb(170, 70, 70)", "rgb(70, 170, 70)", 0.3, context),
        std::string{"rgba(140.0000, 100.0000, 70.0000, 1.0000)"});
    CHECK(interpolate_text("left", "calc(50% - 25px)", "calc(100% - 10px)", 0.5, context) ==
          "calc(75% - 17.5px)");
    CHECK(interpolate_text("opacity", "0", "1", 0.25, context) == "0.25");
    CHECK(interpolate_text("z-index", "-1", "0", 0.5, context) == "0");
    CHECK(interpolate_text("z-index", "2", "4", -2, context) == "-2");
    // Positive integer properties clamp after rounding, including extrapolation.
    for (const auto property : {"column-count", "orphans", "widows"}) {
        CHECK(interpolate_text(property, "10", "20", -1, context) == "1");
        CHECK(interpolate_text(property, "10", "20", -2, context) == "1");
        CHECK(interpolate_text(property, "10", "20", 0.05, context) == "11");
    }
    CHECK(interpolate_text("width", "0px", "100px", -0.5, context) == "0px");
    for (const auto property :
         {"width", "border-image-width", "border-image-slice", "border-top-left-radius"}) {
        CHECK(interpolate_text(property, "0%", "20%", -0.5, context) == "0%");
        CHECK(interpolate_text(property, "10%", "20%", 1.5, context) == "25%");
    }
    CHECK(interpolate_text("border-image-slice", "0% 10 20% 30 fill", "40% 50 60% 70 fill", -0.5,
                           context) == "0% 0 0% 10 fill");
    CHECK(interpolate_text("left", "0%", "20%", -0.5, context) == "-10%");
    for (const auto property : {"background-size", "mask-size"}) {
        CHECK(interpolate_text(property, "10px 10px", "20px 0px", 1.25, context) == "22.5px 0px");
        CHECK(interpolate_text(property, "0% 20%", "20% 40%", -0.5, context) == "0% 10%");
        CHECK(interpolate_text(property, "calc(100px + 10%) auto", "30% auto", 1.5, context) ==
              "calc(40% - 50px) auto");
    }
    CHECK(interpolate_text("font-weight", "100", "900", 2, context) == "1000");
    CHECK(interpolate_text("font-weight", "100", "900", -1, context) == "1");
    CHECK(interpolate_text("left", "calc(2 * 3px)", "12px", 0, context) == "6px");
    // Infinite lengths clamp to 2^25 px (Chrome's LayoutUnit maximum).
    CHECK(interpolate_text("left", "0px", "calc(infinity * 1px)", 0.5, context) == "33554432px");
    CHECK(interpolate_text("left", "0px", "calc(NaN * 1px)", 0.5, context) == "0px");
    CHECK(interpolate_text("text-align", "left", "right", 0.49, context) == "left");
    CHECK(interpolate_text("text-align", "left", "right", 0.5, context) == "right");
    CHECK(interpolate_text("left", "10px", "2s", 0.5, context) == "2s");
    CHECK(interpolate_text("left", "0", "400px", 0.5, context) == "200px");
    // A ratio interpolates as its logarithm (CSS Sizing 4 §7.1).
    CHECK(interpolate_text("aspect-ratio", "0.5", "2", 0.5, context) == "1 / 1");
    CHECK(interpolate_text("aspect-ratio", "1 / 2", "2 / 1", 1.5, context) == "4 / 1");
    CHECK(interpolate_text("aspect-ratio", "auto", "2 / 1", 0.4, context) == "auto");
    CHECK(interpolate_text("aspect-ratio", "auto 1 / 2", "auto 2 / 1", 0.5, context) ==
          "auto 1 / 1");
    CHECK(interpolate_text("border-image-width", "10%", "20px", 1, context) == "calc(0% + 20px)");
    CHECK(interpolate_text("border-left-width", "medium", "23px", -0.3, context) == "0px");
    CHECK(interpolate_text("border-top-left-radius", "10px", "20px", 0.3, context) == "13px");
    CHECK(interpolate_text("border-top-left-radius", "10px", "20px 40px", 0.5, context) ==
          "15px 25px");
    CHECK(interpolate_text("rotate", "3 6 9 360deg", "none", 0.75, context) ==
          "0.267261 0.534522 0.801784 90deg");
    CHECK(interpolate_text("rotate", "45deg", "-1 1 0 60deg", 0.125, context)
              .starts_with("-0.13645"));
    CHECK(interpolate_text("rotate", "45deg", "-1 1 0 60deg", 0.125, context)
              .ends_with(" 40.603733deg"));
    // A filter list function by function, padded with each function's lacuna
    // and clamped (Filter Effects 1 §11.2).
    CHECK(interpolate_text("filter", "hue-rotate(0deg) blur(6px)", "hue-rotate(180deg) blur(10px)",
                           0.25, context) == "hue-rotate(45deg) blur(7px)");
    CHECK(interpolate_text("filter", "none", "blur(10px)", -1, context) == "blur(0px)");
    CHECK(interpolate_text("filter", "none", "grayscale(1)", 1.5, context) == "grayscale(1)");
    CHECK(interpolate_text("filter", "brightness(0)", "none", 1.5, context) == "brightness(1.5)");
    CHECK(interpolate_text("filter", "blur(6px)", "blur(10px) hue-rotate(180deg)", 0.5, context) ==
          "blur(8px) hue-rotate(90deg)");
    CHECK(interpolate_text("filter", "blur(6px)", "grayscale(1)", 0.5, context) == "grayscale(1)");
    // Two track lists of one shape, track by track, with the nearer end's
    // line names (CSS Grid 2 §7.2.5).
    CHECK(interpolate_text("grid-template-columns", "1fr [a b] 1fr [d] 1fr", "2fr [c] auto 2fr",
                           0.4, context) == "1.4fr [a b] 1fr [d] 1.4fr");
    CHECK(interpolate_text("grid-template-columns", "minmax(10px, 1fr) minmax(20px, 2fr)",
                           "minmax(20px, 2fr) minmax(30px, auto)", 0.6,
                           context) == "minmax(16px, 1.6fr) minmax(26px, auto)");
    CHECK(interpolate_text("grid-template-rows", "1fr repeat(2, 2fr auto 30px) 1fr",
                           "2fr repeat(2, 3fr 30px 40px) 2fr", 0.4,
                           context) == "1.4fr repeat(2, 2.4fr auto 34px) 1.4fr");
    // A repeat count is raw text: this pair once spelled one shape with one
    // size against two, and the blend read past the shorter list. Discrete.
    CHECK(interpolate_text("grid-template-columns", "repeat(a(t)rb, 1px)",
                           "repeat(a, 1px) repeat(b, 2px)", 0.6,
                           context) == "repeat(a, 1px) repeat(b, 2px)");
    // An angle may go negative; a url() makes the pair discrete; accumulation
    // sums, a 1-based amount as `a + b - 1` (Filter Effects 1 §11.3).
    CHECK(interpolate_text("filter", "hue-rotate(0deg)", "hue-rotate(180deg)", -0.5, context) ==
          "hue-rotate(-90deg)");
    CHECK(interpolate_text("filter", "url(#a) blur(1px)", "url(#a) blur(3px)", 0.25, context) ==
          "url(#a) blur(1px)");
    CHECK(composite_text("filter", "blur(10px) saturate(0.75)", "blur(40px)",
                         composite_op::accumulate, context) == "blur(50px) saturate(0.75)");
    CHECK(composite_text("filter", "brightness(0.25)", "brightness(0.5)", composite_op::accumulate,
                         context) == "brightness(-0.25)");
    CHECK(composite_text("filter", "contrast(0.75)", "blur(40px)", composite_op::accumulate,
                         context) == "blur(40px)");
    // Colours premultiplied in sRGB, clamped to the gamut when extrapolated.
    CHECK(interpolate_text("color", "rgb(0, 0, 255)", "rgba(255, 0, 0, 0)", 0.5, context) ==
          "rgba(0.0000, 0.0000, 255.0000, 0.5000)");
    CHECK(interpolate_text("color", "yellow", "green", -0.3, context) ==
          "rgba(255.0000, 255.0000, 0.0000, 1.0000)");
    // A modern colour on either side interpolates in Oklab (CSS Color 4 §12.1).
    CHECK(interpolate_text("color", "rgb(0 0 0)", "color(srgb 1 1 1)", 0.3, context)
              .starts_with("oklab(0.3 "));
    CHECK(interpolate_text("color", "color(srgb 0 0 0)", "color(srgb 1 1 1)", 1.5, context)
              .starts_with("oklab(1 "));
    // Lists item by item, and a shadow list padded with transparent zeros.
    CHECK(interpolate_text("border-width", "0px 10px", "10px 30px", 0.5, context) == "5px 20px");
    CHECK(interpolate_text("box-shadow", "rgb(10, 20, 30) 1px 2px 3px 4px", "none", 0.5, context) ==
          "rgba(10.0000, 20.0000, 30.0000, 0.5000) 0.5px 1px 1.5px 2px");
    CHECK(interpolate_text("box-shadow", "rgb(10, 20, 30) 1px 2px 3px 4px", "none", 1.5, context) ==
          "rgba(0.0000, 0.0000, 0.0000, 0.0000) -0.5px -1px 0px -2px");
    // An inset against an outset shadow is discrete, in computed shape.
    CHECK(interpolate_text("box-shadow", "red 1px 1px inset", "blue 1px 1px", 0.5, context) ==
          "blue 1px 1px 0px 0px");
    // A percentage mix is not clamped before its basis exists.
    CHECK(interpolate_text("width", "calc(100px + 10%)", "30%", 1.5, context) ==
          "calc(40% - 50px)");
    CHECK(interpolable_text("width", "10px", "calc(100% - 10px)"));
    CHECK(!interpolable_text("width", "auto", "10px"));
    // A background layer list repeats to match the longer one.
    CHECK(interpolate_text("background-position", "10px 10px", "30px 30px, 50px 50px", 0.5,
                           context) == "20px 20px, 30px 30px");
    CHECK(interpolate_text("background-position-x", "10px, 20px", "100px, 200px, 300px", 0.5,
                           context) == "55px, 110px, 155px, 60px, 105px, 160px");
    CHECK(interpolate_text("background-position-x", "100px, 200px, 300px", "10px, 20px", 0.5,
                           context) == "55px, 110px, 155px, 60px, 105px, 160px");
    CHECK(composite_text("background-position-x", "10px, 20px", "100px, 200px, 300px",
                         composite_op::add, context) == "110px, 220px, 310px, 120px, 210px, 320px");
    CHECK(with_currentcolor("currentcolor 1px 1px, red 2px 2px", "rgb(1, 2, 3)") ==
          "rgb(1, 2, 3) 1px 1px, red 2px 2px");
    CHECK(with_currentcolor("CurrentColor", "blue") == "blue");
}

void test_composition() {
    ctbrowser::style::css::length_context context;
    // Border-image side lists expand before addition and fold after interpolation.
    for (const auto property :
         {"border-image-outset", "border-image-width", "border-image-slice"}) {
        const auto from =
            composite_text(property, "100 200 300 400", "100", composite_op::add, context);
        const auto to = composite_text(property, "100 200 300 400", "200 300 500",
                                       composite_op::accumulate, context);
        CHECK(from == "200 300 400 500");
        CHECK(to == "300 500 800 700");
        CHECK(interpolate_text(property, from, to, 0.5, context) == "250 400 600 600");
        CHECK(interpolate_text(property, "1 2 3 4", "101 102 103 104", -0.25, context) == "0");
        CHECK(interpolable_text(property, "1", "2 3 4"));
    }
    CHECK(composite_text("border-image-slice", "100 200 300 400 fill", "fill 100",
                         composite_op::add, context) == "200 300 400 500 fill");
    CHECK(!interpolable_text("border-image-slice", "10 fill", "20"));
    CHECK(composite_text("border-image-width", "auto 1", "2 auto", composite_op::add, context) ==
          "2 auto");
    CHECK(composite_text("width", "50px", "100px", composite_op::add, context) == "150px");
    CHECK(composite_text("width", "10%", "100px", composite_op::accumulate, context) ==
          "calc(10% + 100px)");
    CHECK(composite_text("width", "100px", "auto", composite_op::add, context) == "auto");
    CHECK(composite_text("width", "50px", "100px", composite_op::replace, context) == "100px");
    CHECK(composite_text("color", "rgb(50, 50, 50)", "rgb(100, 100, 100)", composite_op::add,
                         context) == "rgba(150.0000, 150.0000, 150.0000, 1.0000)");
    CHECK(composite_text("box-shadow", "rgb(1, 2, 3) 1px 2px", "rgb(4, 5, 6) 3px 4px inset",
                         composite_op::add, context) ==
          "rgb(1, 2, 3) 1px 2px 0px 0px, rgb(4, 5, 6) 3px 4px 0px 0px inset");
    CHECK(composite_text("box-shadow", "none", "rgb(4, 5, 6) 3px 4px", composite_op::add,
                         context) == "rgb(4, 5, 6) 3px 4px 0px 0px");
    CHECK(composite_text("border-width", "1px 2px", "10px 20px", composite_op::add, context) ==
          "11px 22px");
    CHECK(composite_text("border-image-slice", "1 2 3 4", "1 2 3 4", composite_op::add, context) ==
          "2 4 6 8");
    CHECK(composite_text("border-image-width", "auto 2", "auto 2", composite_op::accumulate,
                         context) == "auto 4");
    CHECK(composite_text("background-position", "10% 20%", "10% 20%", composite_op::add, context) ==
          "20% 40%");
    CHECK(composite_text("background-size", "40px 40px", "60px 60px, 260px 260px",
                         composite_op::add, context) == "100px 100px, 300px 300px");
    // The individual transform properties: a scale multiplies, `none` is 1 1 1.
    CHECK(composite_text("scale", "1 2 3", "4 5 6", composite_op::add, context) == "4 10 18");
    CHECK(composite_text("rotate", "1 2 3 90deg", "none", composite_op::add, context) ==
          "1 2 3 90deg");
    CHECK(composite_text("rotate", "1 2 3 90deg", "2 4 6 270deg", composite_op::add, context)
              .ends_with(" 360deg"));
    CHECK(composite_text("rotate", "x 90deg", "y 90deg", composite_op::add, context)
              .ends_with(" 120deg"));
    CHECK(interpolate_text("rotate", "3 6 9 360deg", "0 1 0 100deg", 0.25, context) ==
          "0 1 0 25deg");
    CHECK(composite_text("scale", "none", "4 5 6", composite_op::accumulate, context) == "4 5 6");
    CHECK(composite_text("scale", "2", "2", composite_op::accumulate, context) == "3 3 1");
    CHECK(interpolate_text("scale", "none", "4 3 2", 0.125, context) == "1.375 1.25 1.125");
    CHECK(interpolate_text("scale", "none", "none", 0.5, context) == "none");
    CHECK(interpolate_text("translate", "10px", "none", 0.5, context) == "5px 0px 0px");
    CHECK(interpolate_text("rotate", "none", "100deg", 0.5, context) == "0 0 1 50deg");
    // A transform list function by function while the lists match, padded
    // with identities; else as decomposed matrices (CSS Transforms 1 §12).
    CHECK(interpolate_text("transform", "rotate(30deg)", "rotate(60deg)", 0.5, context) ==
          "rotate(45deg)");
    CHECK(interpolate_text("transform", "translate(10px)", "translateX(20px) scale(2)", 0.5,
                           context) == "translate(15px, 0px) scale(1.5, 1.5)");
    CHECK(interpolate_text("transform", "none", "scale(2)", 0.5, context) == "scale(1.5, 1.5)");
    CHECK(interpolate_text("transform", "rotate(90deg)", "scale(2)", 0.5, context)
              .starts_with("matrix(1.06066"));
    CHECK(interpolate_text("transform", "none", "none", 0.5, context) == "none");
    CHECK(interpolate_text("transform", "translateY(70%)", "translateY(90%)", 1, context) ==
          "translateY(90%)");
    CHECK(interpolate_text("transform", "translateY(70%)", "translateY(90%)", 0.5, context) ==
          "translateY(80%)");
    CHECK(interpolate_text("transform", "translate(12px, 70%)", "translate(13px, 90%)", 0.25,
                           context) == "translate(12.25px, 75%)");
    // `none` pads a matrix() with the identity matrix, and the pair blends as
    // matrices; past a pair that does not match, the rest blends as matrices.
    CHECK(interpolate_text("transform", "none", "matrix(7, 0, 2, 2, 6, 0)", -1, context) ==
          "matrix(-5, 0, 0, 0, -6, 0)");
    CHECK(interpolate_text("transform", "rotate(0deg) translate(100px)",
                           "rotate(720deg) scale(2) translate(200px)", 0.25,
                           context) == "rotate(180deg) matrix(1.25, 0, 0, 1.25, 175, 0)");
    CHECK(interpolate_text("transform", "translateY(70%)", "translateY(90%) scaleZ(2)", 0.25,
                           context) == "translateY(75%) scaleZ(1.25)");
    CHECK(interpolate_text("transform-origin", "top left", "bottom right", 0.5, context) ==
          "50% 50%");
    CHECK(interpolate_text("font-variation-settings", "'bbbb' 10, 'aaaa' 0",
                           "\"aaaa\" 30, \"bbbb\" 20", 0.5, context) == "\"aaaa\" 15, \"bbbb\" 15");
    CHECK(composite_text("transform", "rotate(30deg)", "scale(2)", composite_op::add, context) ==
          "rotate(30deg) scale(2)");
    CHECK(composite_text("transform", "matrix(0, 1, -1, 0, 100, 0)", "matrix(1, 0, 0, 1, 50, 0)",
                         composite_op::accumulate, context) == "matrix(0, 1, -1, 0, 150, 0)");
    CHECK(composite_text("transform", "matrix(1, 1, 0, 0, 0, 100)", "matrix(1, 0, 0, 1, 50, 0)",
                         composite_op::accumulate, context) == "matrix(1, 0, 0, 1, 50, 0)");
    // A singular matrix makes the whole interpolation discrete, and the pair
    // is not interpolable at all - no transition starts.
    CHECK(interpolate_text("transform", "matrix(1, 1, 0, 0, 0, 100) translate(10px)",
                           "matrix(1, 1, 0, 0, 0, 100) translate(20px)", 0.25,
                           context) == "matrix(1, 1, 0, 0, 0, 100) translate(10px)");
    CHECK(!interpolable_text("transform",
                             "matrix3d(2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1)",
                             "matrix(3, 0, 0, 3, 0, 0)", context));
    CHECK(interpolable_text("transform", "matrix(2, 0, 0, 2, 0, 0)", "matrix(3, 0, 0, 3, 0, 0)",
                            context));
    CHECK(composite_text("transform", "translate(10px) scale(2)", "translate(5px) scale(3)",
                         composite_op::accumulate, context) == "translate(15px, 0px) scale(4, 4)");
    CHECK(interpolate_text("transform", "matrix(1, 0, 0, 1, 0, -6)", "matrix(0, 7, -1, 0, 6, 0)",
                           0.5, context)
              .starts_with("matrix(2.828427, 2.828427, -0.707107, 0.707107, 3, -3)"));
    // 3D (CSS Transforms 2 §15-16): a perspective interpolates its reciprocal
    // depth, `none` being zero and what an overshoot past it is; one axis
    // interpolates the angle; anything else is the decomposed 4x4 matrices.
    CHECK(interpolate_text("transform", "scaleZ(1) perspective(400px)",
                           "scaleZ(2) perspective(500px)", 0.5,
                           context) == "scaleZ(1.5) perspective(444.444444px)");
    CHECK(interpolate_text("transform", "perspective(none)", "perspective(500px)", -1, context) ==
          "perspective(none)");
    CHECK(interpolate_text("transform", "rotateX(0deg)", "rotateX(720deg)", 0.5, context) ==
          "rotate3d(1, 0, 0, 360deg)");
    CHECK(interpolate_text("transform", "rotateX(90deg)", "rotateY(90deg)", 0.5, context)
              .starts_with("matrix3d("));
    CHECK(interpolate_text("transform", "rotateX(90deg) scaleZ(1)", "scaleZ(1) rotateX(90deg)", 0.5,
                           context) == "matrix3d(1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1)");
    CHECK(composite_text("transform", "rotateX(10deg)", "rotateX(20deg)", composite_op::accumulate,
                         context) == "rotate3d(1, 0, 0, 30deg)");
}

// CSS Values 5 §calc-size: both sides canonicalized onto one basis - a length
// basis is `any`, a percentage basis `100%`, a nested calc-size() folded in -
// and the calculations blended with `size` kept as a term; a keyword meets a
// length only under `interpolate-size: allow-keywords`.
void test_calc_size_interpolation() {
    ctbrowser::style::css::length_context context;
    // As the cascade writes the same declaration, which is what reads back.
    const auto sized = [](std::string_view text) {
        return ctbrowser::style::css::calc_size_text(text, "auto min-content")
            .value_or(std::string{text});
    };
    CHECK(interpolate_text("height", "calc-size(auto, size)", "calc-size(auto, size * 2)", 0.5,
                           context) == sized("calc-size(auto, size * 1.5)"));
    CHECK(interpolate_text("height", "calc-size(20px, size)", "calc-size(60px, size)", 0.75,
                           context) == "calc-size(any, 50px)");
    CHECK(interpolate_text("height", "calc-size(50%, size)", "calc-size(50px, size)", 0, context) ==
          sized("calc-size(100%, 0px + size * 0.5)"));
    CHECK(interpolate_text("height", "auto", "calc-size(any, 50px)", 0.75, context) ==
          sized("calc-size(auto, 37.5px + size * 0.25)"));
    CHECK(interpolate_text("height", "calc-size(calc-size(auto, size / 2), size * 2)",
                           "calc-size(auto, size * 5)", 0.75,
                           context) == sized("calc-size(auto, size * 4)"));
    CHECK(!interpolable_text("height", "auto", "calc-size(50px, size)"));
    CHECK(!interpolable_text("height", "calc-size(auto, size)", "calc-size(50%, size)"));
    CHECK(!interpolable_text("height", "fit-content", "calc-size(min-content, size)"));
    CHECK(!interpolable_text("width", "auto", "10px"));
    // A nest that doubles its text at every level is refused, not expanded.
    std::string nest = "auto";
    for (int i = 0; i < 20; ++i) { nest = "calc-size(" + nest + ", size / 2 + size / 2)"; }
    CHECK(!interpolable_text("height", nest, "calc-size(auto, size * 3)"));

    // Two font palettes mix, and each end is itself.
    CHECK(interpolate_text("font-palette", "light", "dark", 0.3, context) ==
          "palette-mix(in oklab, light, dark 30%)");
    CHECK(interpolate_text("font-palette", "normal", "light", 0.5, context) ==
          "palette-mix(in oklab, normal, light)");
    CHECK(interpolate_text("font-palette", "light", "dark", -0.25, context) == "light");
    CHECK(interpolable_text("font-palette", "light", "--custom"));

    context.interpolate_keywords = true;
    CHECK(interpolable_text("width", "auto", "10px", context));
    CHECK(!interpolable_text("width", "auto", "min-content", context));
    CHECK(interpolate_text("min-height", "auto", "20px", 0.3, context) ==
          sized("calc-size(auto, 6px + size * 0.7)"));
    CHECK(interpolate_text("min-height", "10px", "min-content", -0.3, context) ==
          sized("calc-size(min-content, 13px + size * -0.3)"));
    CHECK(interpolate_text("min-height", "auto", "0px", 0, context) ==
          sized("calc-size(auto, 0px + size * 1)"));
}

// Track lists add track by track, inside repeat() too; a transform-origin
// with no z offset adds as one of zero, and prints without it.
void test_track_and_origin_composition() {
    const ctbrowser::style::css::length_context context;
    CHECK(composite_text("grid-template-columns", "0fr repeat(2, 0fr 10px) 1fr",
                         "1fr repeat(2, 1fr 20px) 1fr", composite_op::add,
                         context) == "1fr repeat(2, 1fr 30px) 2fr");
    CHECK(composite_text("grid-template-columns", "50px 10%", "100px 100px", composite_op::add,
                         context) == "150px calc(10% + 100px)");
    CHECK(composite_text("transform-origin", "40px 60px", "160px 140px 40px", composite_op::add,
                         context) == "200px 200px 40px");
    CHECK(interpolate_text("transform-origin", "100px 100px", "200px 200px 40px", 0.5, context) ==
          "150px 150px 20px");
    CHECK(interpolate_text("transform-origin", "100px 200px 20px", "200px 100px 100px", -0.25,
                           context) == "75px 225px");
    // Lists that do not pair accumulate as whole matrices; a shorter list that
    // pairs as far as it goes is padded with identities.
    CHECK(composite_text("transform", "rotate(90deg)", "translateX(100px)",
                         composite_op::accumulate, context) == "matrix(0, 1, -1, 0, 100, 0)");
    CHECK(composite_text("transform", "translateX(100px) rotate(90deg)", "translateX(100px)",
                         composite_op::accumulate,
                         context) == "translate(200px, 0px) rotate(90deg)");
}

// Lists pair at the least common multiple of their lengths: ten thousand and
// 9,999 would be a hundred million entries, so they do not pair at all.
void test_list_repetition_is_capped() {
    ctbrowser::style::css::length_context context;
    CHECK(interpolate_text("column-rule-width", "repeat(10000, 1px)", "repeat(9999, 2px)", 0.5,
                           context)
              .size() < 100000);
    std::string a = "0px 0px", b = "1px 1px";
    for (int i = 1; i < 10000; ++i) { a += ", 0px 0px"; }
    for (int i = 1; i < 9999; ++i) { b += ", 1px 1px"; }
    CHECK(interpolate_text("background-position", a, b, 0.25, context) == a);
    CHECK(interpolate_text("background-position", a, b, 0.75, context) == b);
}

} // namespace

int main() {
    test_track_and_origin_composition();
    test_easing();
    test_interpolation();
    test_composition();
    test_calc_size_interpolation();
    test_list_repetition_is_capped();
    REPORT("style_easing");
}
