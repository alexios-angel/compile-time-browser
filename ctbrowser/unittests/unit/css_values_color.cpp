// `<color>`, CSS Color 4 and 5 - the grammar `el.style` validates against,
// the specified serialisation it reads back, and the computed value
// `getComputedStyle` reports.
//
// Every expectation here is one of css/css-color/parsing's, quoted: those
// files compare the serialisation byte for byte (or, for the arithmetic ones,
// to a hundredth), and this is the same reading in a form that runs in a
// second. The out-of-gamut ones are the matrix check: a wrong constant in the
// conversions is a fourth-decimal error, invisible to every other test.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

using ctbrowser::style::css::check_declaration;
using ctbrowser::style::css::color_context;
using ctbrowser::style::css::computed_color;
using ctbrowser::style::css::length_context;
using ctbrowser::style::css::serialize_color;

namespace {

void ok(std::string_view value, std::string_view serialized) {
    const auto answer = check_declaration("color", value);
    CHECK(answer.valid);
    CHECK_EQ(answer.serialized, std::string{serialized});
}

void bad(std::string_view value) {
    const auto answer = check_declaration("color", value);
    CHECK(!answer.valid);
}

void computed(std::string_view value, std::string_view expected) {
    color_context ctx;
    ctx.current_color = "rgb(255, 0, 0)";
    CHECK_EQ(computed_color(value, ctx), std::string{expected});
}

// The numbers of two colour strings within `epsilon`, and the text without
// them equal - color-testcommon.js's fuzzy comparison.
void computed_near(std::string_view value, std::string_view expected, double epsilon = 0.01) {
    color_context ctx;
    ctx.current_color = "rgb(255, 0, 0)";
    const std::string actual = computed_color(value, ctx);
    const auto numbers = [](std::string_view text) {
        std::vector<double> out;
        std::string shape;
        for (std::size_t i = 0; i < text.size();) {
            const bool numeric =
                (text[i] >= '0' && text[i] <= '9') || text[i] == '.' ||
                (text[i] == '-' && i + 1 < text.size() &&
                 ((text[i + 1] >= '0' && text[i + 1] <= '9') || text[i + 1] == '.'));
            if (!numeric) {
                shape += text[i++];
                continue;
            }
            std::size_t end = i + 1;
            while (end < text.size() &&
                   ((text[end] >= '0' && text[end] <= '9') || text[end] == '.')) {
                ++end;
            }
            out.push_back(std::stod(std::string{text.substr(i, end - i)}));
            i = end;
        }
        return std::pair{out, shape};
    };
    const auto [a, a_shape] = numbers(actual);
    const auto [b, b_shape] = numbers(expected);
    bool same = a.size() == b.size() && a_shape == b_shape;
    for (std::size_t i = 0; same && i < a.size(); ++i) { same = std::fabs(a[i] - b[i]) <= epsilon; }
    if (!same) {
        std::printf("  %s -> %s, wanted %s\n", value.data(), actual.c_str(), expected.data());
    }
    CHECK(same);
}

void test_the_legacy_syntax() {
    ok("red", "red");
    ok("RED", "red");
    ok("transparent", "transparent");
    ok("currentColor", "currentcolor");
    ok("ActiveText", "activetext");
    ok("#234", "rgb(34, 51, 68)");
    ok("#FEDCBA", "rgb(254, 220, 186)");
    ok("#ff000080", "rgba(255, 0, 0, 0.5)");
    ok("rgb(100%, 0%, 0%)", "rgb(255, 0, 0)");
    ok("rgba(2, 3, 4, 50%)", "rgba(2, 3, 4, 0.5)");
    ok("rgb(-2, 3, 4)", "rgb(0, 3, 4)");
    ok("rgb(100, 200, 300)", "rgb(100, 200, 255)");
    ok("rgb(20, 10, 0, -10)", "rgba(20, 10, 0, 0)");
    ok("rgb(2.5, 3.4, 4.6)", "rgb(3, 3, 5)");
    ok("rgba(0, 51, 255, 0.42)", "rgba(0, 51, 255, 0.42)");
    ok("hsl(120, 100%, 50%)", "rgb(0, 255, 0)");
    ok("hsla(120, 100%, 50%, 0.25)", "rgba(0, 255, 0, 0.25)");
    ok("hsl(120 30% 50%)", "rgb(89, 166, 89)");
    ok("hsl(0 -50% 40%)", "rgb(102, 102, 102)");
    ok("hwb(120 30% 50%)", "rgb(77, 128, 77)");
    ok("hwb(90 50% 50%)", "rgb(128, 128, 128)");
    ok("rgb(none none none / none)", "rgba(0, 0, 0, 0)");
    ok("rgb(20% none none)", "rgb(51, 0, 0)");
    ok("rgb(calc(infinity), 0, 0)", "rgb(255, 0, 0)");
    ok("rgb(calc(NaN), 0, 0)", "rgb(0, 0, 0)");
    ok("hsl(calc(infinity) 100% 50%)", "rgb(255, 0, 0)");
    // The modern form stays when a channel is missing or cannot be answered yet.
    ok("hsl(120 80% none)", "hsl(120 80 none)");
    ok("hsl(none none none / none)", "hsl(none none none / none)");
    ok("rgb(calc(50% + (sign(1em - 10px) * 10%)), 0%, 0%, 50%)",
       "rgb(calc(50% + (10% * sign(1em - 10px))) 0 0 / 0.5)");
    ok("hsla(calc(50deg + (sign(1em - 10px) * 10deg)) -100% 300% / 0.5)",
       "hsl(calc(50deg + (10deg * sign(1em - 10px))) 0 300 / 0.5)");

    bad("auto");
    bad("#12");
    bad("#ffg");
    bad("blak");
    bad("rgb(1)");
    bad("rgb(1,2,3,4,5)");
    bad("rgb(10%, 20, 30%)");
    bad("rgb(none, none, none)");
    bad("rgb(0 0, 0)");
    bad("rgb(0, 0, 0,)");
    bad("rgb(0, 0, 0deg)");
    bad("rgb(257, 0, 5 / 0)");
    bad("hsl(10, 50%, 0)");
    bad("hsl(50%, 50%, 0%)");
    bad("hsl(0, 0% 0%)");
    bad("hwb(90deg, 50%, 50%)");
    bad("hwba(120 30% 50%)");
    bad("hsl(calc(0.56turn * -0.43turn), 47%, 4884.6%)");
}

void test_lab_lch_and_color() {
    ok("lab(0 0 0 / 1)", "lab(0 0 0)");
    ok("lab(400 0 10/50%)", "lab(100 0 10 / 0.5)");
    ok("lab(50% 50% -20%)", "lab(50 62.5 -25)");
    ok("lab(calc(50 * 3) calc(0.5 - 1) calc(1.5) / calc(-0.5 + 1))",
       "lab(calc(150) calc(-0.5) calc(1.5) / calc(0.5))");
    ok("lab(calc(0 / 0) 0 0)", "lab(calc(NaN) 0 0)");
    ok("lch(10 20 1.28rad)", "lch(10 20 73.3386)");
    ok("lch(10 20 -700deg)", "lch(10 20 20)");
    ok("lch(0.5 -20% -20)", "lch(0.5 0 340)");
    ok("oklab(50% 50% -20%)", "oklab(0.5 0.2 -0.08)");
    ok("oklch(20% 60% 10/0.5)", "oklch(0.2 0.24 10 / 0.5)");
    ok("color(srgb 20% 0 10/50%)", "color(srgb 0.2 0 10 / 0.5)");
    ok("color(xyz 0 calc(infinity) 0)", "color(xyz-d65 0 calc(infinity) 0)");
    ok("color(display-p3 none none none / 0.5)", "color(display-p3 none none none / 0.5)");
    bad("lab(0% 0 0deg)");
    bad("color(srgb 1 1)");
    bad("color(srgb 0, 0, 0)");
    bad("color(banana 1 1 1)");
    bad("color(srgb 1 1 1 / bacon)");
    bad("lch(20% 10 10deg 10)");
}

void test_relative_colours_and_mixing() {
    ok("rgb(from rgb(20%, 40%, 60%, 80%) r g b / alpha)",
       "rgb(from rgba(51, 102, 153, 0.8) r g b / alpha)");
    ok("rgb(from rebeccapurple b calc(r * .5) 10)", "rgb(from rebeccapurple b calc(0.5 * r) 10)");
    ok("hsl(from hsl(120deg none 50% / .5) h s l)", "hsl(from rgba(128, 128, 128, 0.5) h s l)");
    ok("alpha(from hsl(120 50% 50%) / 0.5)", "alpha(from rgb(64, 191, 64) / 0.5)");
    ok("alpha(from currentcolor / calc(alpha + 0.1))",
       "alpha(from currentcolor / calc(0.1 + alpha))");
    ok("color-mix(in hsl, 25% hsl(120deg 10% 20%), hsl(30deg 30% 40%))",
       "color-mix(in hsl, rgb(46, 56, 46) 25%, rgb(133, 102, 71) 75%)");
    ok("color-mix(in oklab, oklab(0.1 0.2 0.3), 25% oklab(0.5 0.6 0.7))",
       "color-mix(oklab(0.1 0.2 0.3) 75%, oklab(0.5 0.6 0.7) 25%)");
    ok("color-mix(in lch shorter hue, lch(100 0 20deg), lch(100 0 320deg))",
       "color-mix(in lch, lch(100 0 20), lch(100 0 320))");
    ok("light-dark(black, white)", "light-dark(black, white)");
    ok("color-layers(normal, red, blue)", "color-layers(red, blue)");
    bad("hsl(from rebeccapurple calc(h + 1deg) s l)");
    bad("rgb(from rebeccapurple r 10deg 10)");
    bad("rgb(from rebeccapurple h g b)");
    bad("rgb(0 0 0 / alpha)");
    bad("color-mix(in hsl, hsl(120deg 10% 20%) 150%, hsl(30deg 30% 40%))");
    bad("color-mix(in hsl hue, hsl(120deg 10% 20%), hsl(30deg 30% 40%))");
    bad("color-mix(in lab longer hue, lab(10 20 30), lab(50 60 70))");
    bad("color-mix(in srgb, red, blue blue)");
}

void test_the_computed_value() {
    computed("red", "rgb(255, 0, 0)");
    computed("transparent", "rgba(0, 0, 0, 0)");
    computed("currentcolor", "rgb(255, 0, 0)");
    computed("Highlight", "rgb(30, 144, 255)");
    computed("hsl(120 80% none)", "hsl(120 80% none)");
    computed("rgb(128 none none)", "color(srgb 0.501961 none none)");
    computed("rgb(calc(50% + (sign(1em - 10px) * 10%)), 0%, 0%, 50%)", "rgba(153, 0, 0, 0.5)");
    computed("lab(calc(0 / 0) 0 0)", "lab(0 0 0)");
    computed("color(srgb calc(50% + (sign(1em - 10px) * 10%)) 0 0 / 0.5)",
             "color(srgb 0.6 0 0 / 0.5)");
    computed("rgb(from rebeccapurple r 25 b / alpha)", "color(srgb 0.4 0.0980392 0.6)");
    computed("hsl(from rebeccapurple h s none)", "hsl(270 50% none)");
    computed("alpha(from red / 0.5)", "color(srgb 1 0 0 / 0.5)");
    computed("light-dark(black, white)", "rgb(0, 0, 0)");
    // color-mix, to a hundredth: the interpolation and the hue methods.
    computed_near("color-mix(in hsl, hsl(120deg 10% 20% / .4), hsl(30deg 30% 40% / .8))",
                  "color(srgb 0.372222 0.411111 0.255556 / 0.6)");
    computed_near("color-mix(in hsl longer hue, hsl(40deg 50% 50%), hsl(60deg 50% 50%))",
                  "color(srgb 0.25 0.333333 0.75)");
    computed_near("color-mix(in hsl, hsl(120deg 40% 40% / none), hsl(0deg 40% 40% / none))",
                  "hsl(60 40% 40% / none)");
    computed_near("color-mix(in hwb, hwb(120deg 10% 20%), hwb(30deg 30% 40%))",
                  "color(srgb 0.575 0.7 0.2)");
    // Powerless and missing components across a conversion.
    computed_near("hsl(from hwb(180 100% 25%) h s l)", "hsl(none 0% 80%)");
    computed_near("hsl(from hsl(180 0 50%) h s l)", "color(srgb 0.5 0.5 0.5)");
    computed_near("hsl(from lab(50 none none) h s l)", "hsl(none none 46.63%)");
    computed_near("lch(from hsl(180 0.001% 50%) l c h)", "lch(53.389 0 none)");
    computed_near("color-mix(in hsl, lch(none 20 180), hsl(11 33 44))",
                  "color(srgb 1.09909 -0.21909 -0.11806)");
    // Out of gamut, to a ten-thousandth: the matrices.
    computed_near("rgb(from color(display-p3 0 1 0) r g b / alpha)",
                  "color(srgb -0.5116 1.01827 -0.31067)", 0.0001);
    computed_near("rgb(from lab(100 104.3 -50.9) r g b)", "color(srgb 1.5935 0.58776 1.40555)",
                  0.0001);
    computed_near("rgb(from oklch(0 0.399 336.3) r g b)", "color(srgb 0.07651 -0.04579 0.0937)",
                  0.0001);
    computed_near("hsl(from lch(100 116 334) h s l)", "color(srgb 1.59336 0.58802 1.40517)",
                  0.0001);
    // `serialize_color` is the specified form on its own, for a gradient stop.
    CHECK_EQ(serialize_color(" #f00 "), std::string{"rgb(255, 0, 0)"});
    CHECK_EQ(serialize_color("12px"), std::string{});
}

} // namespace

int main() {
    test_the_legacy_syntax();
    test_lab_lch_and_color();
    test_relative_colours_and_mixing();
    test_the_computed_value();
    REPORT("css_values_color");
}
