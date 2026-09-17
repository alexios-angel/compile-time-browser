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
    CHECK(interpolate_text("left", "calc(50% - 25px)", "calc(100% - 10px)", 0.5, context) ==
          "calc(75% - 17.5px)");
    CHECK(interpolate_text("opacity", "0", "1", 0.25, context) == "0.25");
    CHECK(interpolate_text("z-index", "-1", "0", 0.5, context) == "0");
    CHECK(interpolate_text("width", "0px", "100px", -0.5, context) == "0px");
    CHECK(interpolate_text("font-weight", "100", "900", 2, context) == "1000");
    CHECK(interpolate_text("font-weight", "100", "900", -1, context) == "1");
    CHECK(interpolate_text("left", "calc(2 * 3px)", "12px", 0, context) == "6px");
    CHECK(interpolate_text("left", "0px", "calc(infinity * 1px)", 0.5, context) == "33554432px");
    CHECK(interpolate_text("left", "0px", "calc(NaN * 1px)", 0.5, context) == "0px");
    CHECK(interpolate_text("text-align", "left", "right", 0.49, context) == "left");
    CHECK(interpolate_text("text-align", "left", "right", 0.5, context) == "right");
    CHECK(interpolate_text("left", "10px", "2s", 0.5, context) == "2s");
    // Colours premultiplied in sRGB, clamped to the gamut when extrapolated.
    CHECK(interpolate_text("color", "rgb(0, 0, 255)", "rgba(255, 0, 0, 0)", 0.5, context) ==
          "rgba(0.0000, 0.0000, 255.0000, 0.5000)");
    CHECK(interpolate_text("color", "yellow", "green", -0.3, context) ==
          "rgba(255.0000, 255.0000, 0.0000, 1.0000)");
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
    CHECK(with_currentcolor("currentcolor 1px 1px, red 2px 2px", "rgb(1, 2, 3)") ==
          "rgb(1, 2, 3) 1px 1px, red 2px 2px");
    CHECK(with_currentcolor("CurrentColor", "blue") == "blue");
}

void test_composition() {
    ctbrowser::style::css::length_context context;
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
    CHECK(composite_text("background-size", "40px 40px", "60px 60px, 260px 260px",
                         composite_op::add, context) == "100px 100px, 300px 300px");
    // The individual transform properties: a scale multiplies, `none` is 1 1 1.
    CHECK(composite_text("scale", "1 2 3", "4 5 6", composite_op::add, context) == "4 10 18");
    CHECK(composite_text("scale", "none", "4 5 6", composite_op::accumulate, context) == "4 5 6");
    CHECK(composite_text("scale", "2", "2", composite_op::accumulate, context) == "3 3 1");
    CHECK(interpolate_text("scale", "none", "4 3 2", 0.125, context) == "1.375 1.25 1.125");
    CHECK(interpolate_text("scale", "none", "none", 0.5, context) == "none");
    CHECK(interpolate_text("translate", "10px", "none", 0.5, context) == "5px 0px 0px");
    CHECK(interpolate_text("rotate", "none", "100deg", 0.5, context) == "50deg");
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
    CHECK(interpolate_text("transform", "translate(12px, 70%)", "translate(13px, 90%)", 0.25,
                           context) == "translate(12.25px, 75%)");
    CHECK(composite_text("transform", "rotate(30deg)", "scale(2)", composite_op::add, context) ==
          "rotate(30deg) scale(2)");
}

} // namespace

int main() {
    test_easing();
    test_interpolation();
    test_composition();
    REPORT("style_easing");
}
