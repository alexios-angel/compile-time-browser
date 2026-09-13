#include <ctbrowser/style/easing.hpp>

#include "check.hpp"

#include <cmath>
#include <string>

using ctbrowser::style::easing;
using ctbrowser::style::interpolate_text;
using ctbrowser::style::parse_easing;

namespace {

void test_easing() {
    const easing linear;
    CHECK(linear(-0.25, false) == -0.25);
    CHECK(linear(1.25, true) == 1.25);
    CHECK(parse_easing(" \tLINEAR\n").value()(0.5, false) == 0.5);
    CHECK(std::fabs(parse_easing("ease-in").value()(0.5, false) - 0.3153568) < 1e-6);
    CHECK(parse_easing("step-start").value()(0.5, false) == 1);
    CHECK(parse_easing("step-end").value()(0.5, false) == 0);

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
    for (const auto invalid : {"", "bogus", "cubic-bezier(0, 1, 1)", "cubic-bezier(-1, 0, 1, 1)",
                               "cubic-bezier(0, 0, 2, 1)", "steps(0)", "steps(1.5)",
                               "steps(1, jump-none)", "steps(2, sideways)"}) {
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
}

} // namespace

int main() {
    test_easing();
    test_interpolation();
    REPORT("style_easing");
}
