// `<image>` lists, CSS Images 3 and 4 - the gradients as `el.style` reads
// them back and as `getComputedStyle` reports them. Every expectation is one
// of css/css-images/parsing's or css/css-backgrounds/parsing's.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <string_view>

using ctbrowser::style::css::check_declaration;
using ctbrowser::style::css::color_context;
using ctbrowser::style::css::computed_image;
using ctbrowser::style::css::length_context;

namespace {

void ok(std::string_view value, std::string_view serialized) {
    const auto answer = check_declaration("background-image", value);
    CHECK(answer.valid);
    CHECK_EQ(answer.serialized, std::string{serialized});
}

void bad(std::string_view value) {
    CHECK(!check_declaration("background-image", value).valid);
}

void computed(std::string_view value, std::string_view expected) {
    length_context lengths;
    lengths.font_size = 40;
    lengths.line_height = 80;
    color_context ctx;
    ctx.lengths = &lengths;
    CHECK_EQ(computed_image(value, ctx), std::string{expected});
}

void test_the_specified_value() {
    ok("none", "none");
    ok("none, url(http://www.example.com/)", "none, url(\"http://www.example.com/\")");
    ok("linear-gradient(30deg, red, blue)", "linear-gradient(30deg, red, blue)");
    ok("linear-gradient(to bottom left, red, 50%, blue)",
       "linear-gradient(to left bottom, red, 50%, blue)");
    ok("linear-gradient(to bottom, red, blue)", "linear-gradient(red, blue)");
    // The default interpolation method is dropped: srgb over legacy colours,
    // oklab over anything else (CSS Images 4 §3.4.3).
    ok("linear-gradient(in srgb, red, blue)", "linear-gradient(red, blue)");
    ok("linear-gradient(in oklab, red, blue)", "linear-gradient(in oklab, red, blue)");
    ok("linear-gradient(in oklab, color(srgb 1 0 0), blue)",
       "linear-gradient(color(srgb 1 0 0), blue)");
    ok("linear-gradient(in hsl shorter hue 30deg, red, blue)",
       "linear-gradient(30deg in hsl, red, blue)");
    ok("linear-gradient(30deg in hsl longer hue, red, blue)",
       "linear-gradient(30deg in hsl longer hue, red, blue)");
    ok("radial-gradient(ellipse 50% 40em, red, blue)", "radial-gradient(50% 40em, red, blue)");
    ok("radial-gradient(at bottom 10% right 20%, red, blue)",
       "radial-gradient(at right 20% bottom 10%, red, blue)");
    ok("radial-gradient(at bottom right, red, blue)",
       "radial-gradient(at right bottom, red, blue)");
    ok("radial-gradient(farthest-corner at 10px 10px, red, blue)",
       "radial-gradient(at 10px 10px, red, blue)");
    ok("radial-gradient(circle farthest-side, red, blue)",
       "radial-gradient(circle farthest-side, red, blue)");
    ok("conic-gradient(from 30deg in xyz, red, blue)",
       "conic-gradient(from 30deg in xyz-d65, red, blue)");
    ok("conic-gradient(at left 10px top 50em, red, blue)",
       "conic-gradient(at left 10px top 50em, red, blue)");
    ok("conic-gradient(hsl(0,0%,75%), hsl(0,0%,25%))",
       "conic-gradient(rgb(191, 191, 191), rgb(64, 64, 64))");
    ok("image(red)", "image(red)");
    ok("cross-fade( 1% red, green)", "cross-fade(red 1%, green)");
    ok("light-dark(image(blue), url(\"b.png\"))", "light-dark(image(blue), url(\"b.png\"))");
    bad("linear-gradient(, red, blue)");
    bad("linear-gradient(red, blue, lab)");
    bad("linear-gradient(lab lab, red, blue)");
    bad("linear-gradient(hsl hue, red, blue)");
    bad("linear-gradient(90deg in hsl longer, black, transparent)");
    bad("linear-gradient(lab shorter hue, red, blue)");
    bad("radial-gradient(at top 0px, red, blue)");
    bad("radial-gradient(at bottom 7% left, red, blue)");
    bad("radial-gradient(circle -10px at center, red, blue)");
    bad("radial-gradient(20px -30px at center, red, blue)");
    bad("image()");
    bad("image(red, blue)");
    bad("none, auto");
}

void test_the_computed_value() {
    computed("linear-gradient(to left bottom, red, blue)",
             "linear-gradient(to left bottom, rgb(255, 0, 0), rgb(0, 0, 255))");
    computed("radial-gradient(at center, red, blue)",
             "radial-gradient(rgb(255, 0, 0), rgb(0, 0, 255))");
    computed("radial-gradient(farthest-corner at 50%, red, blue)",
             "radial-gradient(rgb(255, 0, 0), rgb(0, 0, 255))");
    computed("radial-gradient(farthest-side at 10px 10px, rgb(255, 0, 0), rgb(0, 0, 255))",
             "radial-gradient(farthest-side at 10px 10px, rgb(255, 0, 0), rgb(0, 0, 255))");
    computed("radial-gradient(circle calc(-0.5em + 10px) at calc(-1em + 10px) calc(-2em + 10px), "
             "red, blue)",
             "radial-gradient(0px at -30px -70px, rgb(255, 0, 0), rgb(0, 0, 255))");
    computed("radial-gradient(10px at 1lh 1lh, red, blue)",
             "radial-gradient(10px at 80px 80px, rgb(255, 0, 0), rgb(0, 0, 255))");
    computed("radial-gradient(ellipse 50% 40em, red, blue)",
             "radial-gradient(50% 1600px, rgb(255, 0, 0), rgb(0, 0, 255))");
    computed("radial-gradient(at right center, red, blue)",
             "radial-gradient(at 100% 50%, rgb(255, 0, 0), rgb(0, 0, 255))");
    computed("conic-gradient(from 0deg at 50%, red, blue)",
             "conic-gradient(rgb(255, 0, 0), rgb(0, 0, 255))");
    computed("conic-gradient(from 45deg at 10px 10px, red, blue)",
             "conic-gradient(from 45deg at 10px 10px, rgb(255, 0, 0), rgb(0, 0, 255))");
    computed("conic-gradient(red 0deg, gold 1turn)",
             "conic-gradient(rgb(255, 0, 0) 0deg, rgb(255, 215, 0) 360deg)");
    computed("conic-gradient(at bottom 20px left 30px, red, blue)",
             "conic-gradient(at 30px calc(100% - 20px), rgb(255, 0, 0), rgb(0, 0, 255))");
    computed("linear-gradient(in srgb, color(srgb 1 0 0), blue)",
             "linear-gradient(in srgb, color(srgb 1 0 0), rgb(0, 0, 255))");
    computed("image(red)", "image(rgb(255, 0, 0))");
    computed("light-dark(image(blue), url(\"b.png\"))", "image(rgb(0, 0, 255))");
    computed("none, url(\"http://x/\")", "none, url(\"http://x/\")");
}

} // namespace

int main() {
    test_the_specified_value();
    test_the_computed_value();
    REPORT("css_grammar_image");
}
