// Web Animations - `element.animate`, seeked, read back through getComputedStyle.
//
// The shape every case takes is the one css/support/interpolation-testcommon.js
// takes: make an animation, pause it, set `currentTime`, read the property.
// See lib/Shell/bindings/animations.cpp for what the model is and what it
// deliberately leaves out; these cases pin down the parts a page can observe,
// and two of them - `fill: none` past the end, and `cancel()` - assert that an
// animation STOPS having an effect, which is the half a stub gets wrong.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <cstdio>
#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

// A positioned 100px-wide container with an absolutely positioned target in it,
// so `left` has a used value and a percentage has a containing block.
constexpr const char * stage =
    "<div id=c style='position:relative;width:100px;height:100px'>"
    "<div id=t style='position:absolute;left:0px;width:10px;height:10px;opacity:1'></div></div>";

// Run `setup` at load and log `expression` on the first timer after it, once
// the page has ticked a few times - a running animation's clock has moved by
// then, and a promise settled by `finish()` has delivered.
[[nodiscard]] std::string answer(const std::string & setup, const std::string & expression) {
    browser page{browser_options{400, 300}};
    const std::string html =
        std::string{"<!DOCTYPE html><html><body>"} + stage +
        "<script>var t = document.getElementById('t');" + setup +
        "\nwindow.addEventListener('load', function () { setTimeout(function () {"
        " try { console.log(String(" +
        expression +
        ")); } catch (e) { console.log('threw:' + e.name); } }, 0); });</script></body></html>";
    page.load_html(html);
    for (int i = 0; i < 4; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    if (logged.empty()) { return "<nothing logged: " + page.script_error() + ">"; }
    return logged.back();
}

void is(const std::string & setup, const std::string & expression, const std::string & expected) {
    const std::string got = answer(setup, expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

constexpr const char * seeked =
    "var a = t.animate([{left: '0px'}, {left: '100px'}], {duration: 1000, fill: 'forwards'});"
    "a.pause(); a.currentTime = 250;";

void test_the_surface_exists() {
    is("", "'animate' in Element.prototype", "true");
    is("", "typeof document.getAnimations", "function");
    is("", "typeof document.timeline.currentTime", "number");
    is(seeked, "a instanceof Animation", "true");
    is(seeked, "a.effect instanceof KeyframeEffect", "true");
    is(seeked, "a.timeline === document.timeline", "true");
}

void test_a_seeked_animation_is_what_getComputedStyle_reports() {
    is(seeked, "getComputedStyle(t).left", "25px");
    is(seeked, "a.currentTime", "250");
    // A percentage interpolates as a percentage and resolves against the
    // containing block afterwards: 62.5% of 100px less 21.25px.
    is("var a = t.animate([{left: 'calc(50% - 25px)'}, {left: 'calc(100% - 10px)'}],"
       " {duration: 1000, fill: 'forwards'}); a.pause(); a.currentTime = 250;",
       "getComputedStyle(t).left", "41.25px");
    // A number is a number.
    is("var a = t.animate([{opacity: 0}, {opacity: 1}], {duration: 1000, fill: 'forwards'});"
       " a.pause(); a.currentTime = 250;",
       "getComputedStyle(t).opacity", "0.25");
    // The property-indexed form, and a one-value list whose missing endpoint
    // is the underlying value - `left: 0px` from the style attribute.
    is("var a = t.animate({left: ['0px', '100px']}, {duration: 1000, fill: 'forwards'});"
       " a.pause(); a.currentTime = 500;",
       "getComputedStyle(t).left", "50px");
    is("var a = t.animate({left: '100px'}, {duration: 1000, fill: 'forwards'});"
       " a.pause(); a.currentTime = 500;",
       "getComputedStyle(t).left", "50px");
    // A keyword flips at the midpoint.
    is("var a = t.animate([{textAlign: 'left'}, {textAlign: 'right'}], {duration: 1000,"
       " fill: 'forwards'}); a.pause(); a.currentTime = 250;",
       "getComputedStyle(t).textAlign", "left");
    is("var a = t.animate([{textAlign: 'left'}, {textAlign: 'right'}], {duration: 1000,"
       " fill: 'forwards'}); a.pause(); a.currentTime = 750;",
       "getComputedStyle(t).textAlign", "right");
}

void test_easing_is_applied_and_may_overshoot() {
    // The three shapes interpolation-testcommon.js builds for progress 0, 1 and
    // an arbitrary point: steps(1, end), steps(1, start), and a cubic-bezier
    // whose control points sit at the progress wanted - here -0.25.
    const std::string at_half = "{duration: 1000, fill: 'forwards', easing: '";
    const std::string seek = "'}); a.pause(); a.currentTime = 500;";
    is("var a = t.animate([{left: '0px'}, {left: '100px'}], " + at_half + "steps(1, end)" + seek,
       "getComputedStyle(t).left", "0px");
    is("var a = t.animate([{left: '0px'}, {left: '100px'}], " + at_half + "steps(1, start)" + seek,
       "getComputedStyle(t).left", "100px");
    is("var a = t.animate([{left: '0px'}, {left: '100px'}], " + at_half +
           "cubic-bezier(0, -0.5, 1, -0.5)" + seek,
       "getComputedStyle(t).left", "-25px");
    is("var a = t.animate([{left: '0px'}, {left: '100px'}], " + at_half + "ease-in" + seek,
       "Math.round(parseFloat(getComputedStyle(t).left) * 100)", "3154");
    is("",
       "(function () { try { t.animate([{left: '0px'}], {easing: 'bogus'}); return 'no'; }"
       " catch (e) { return e.name; } })()",
       "TypeError");
}

void test_the_play_state_follows_the_specification() {
    is("var a = t.animate([{left: '0px'}, {left: '100px'}], 1000);", "a.playState", "running");
    is("var a = t.animate([{left: '0px'}, {left: '100px'}], 1000);", "a.currentTime > 0", "true");
    is(seeked, "a.playState", "paused");
    // Paused and seeked past the end is still paused, not finished.
    is("var a = t.animate([{left: '0px'}, {left: '100px'}], 1000); a.pause(); a.currentTime = "
       "5000;",
       "a.playState", "paused");
    is("var a = t.animate([{left: '0px'}, {left: '100px'}], 1000); a.finish();", "a.playState",
       "finished");
    is("var a = t.animate([{left: '0px'}, {left: '100px'}], 1000); a.finish();", "a.currentTime",
       "1000");
    is("var a = t.animate([{left: '0px'}, {left: '100px'}], 1000); a.cancel();", "a.playState",
       "idle");
    is("var a = t.animate([{left: '0px'}, {left: '100px'}], 1000); a.cancel();", "a.currentTime",
       "null");
    // The finished promise resolves with the animation.
    is("var a = t.animate([{left: '0px'}, {left: '100px'}], 1000); a.finish();"
       " a.finished.then(function (x) { window.__done = x === a; });",
       "window.__done", "true");
    // ...and a seek back out of the finished state hands out a fresh one.
    is("var a = t.animate([{left: '0px'}, {left: '100px'}], 1000); a.finish();"
       " var p = a.finished; a.currentTime = 100;",
       "a.finished === p", "false");
}

void test_an_animation_can_stop_having_an_effect() {
    // No fill: past the end the underlying value is back.
    is("var a = t.animate([{left: '0px'}, {left: '100px'}], 100); a.pause(); a.currentTime = 500;",
       "getComputedStyle(t).left", "0px");
    is("var a = t.animate([{left: '0px'}, {left: '100px'}], {duration: 100, fill: 'forwards'});"
       " a.pause(); a.currentTime = 500;",
       "getComputedStyle(t).left", "100px");
    is(std::string{seeked} + " a.cancel();", "getComputedStyle(t).left", "0px");
    // A live computed style object sees the seek that happened after it was made.
    is(std::string{seeked} + " var cs = getComputedStyle(t); var before = cs.left;"
                             " a.currentTime = 750;",
       "before + ' ' + cs.left", "25px 75px");
}

void test_getAnimations_and_the_effect() {
    is(seeked, "t.getAnimations().length", "1");
    is(seeked, "t.getAnimations()[0] === a", "true");
    is(seeked, "document.getAnimations()[0] === a", "true");
    is(seeked, "document.getElementById('c').getAnimations().length", "0");
    is(seeked, "document.getElementById('c').getAnimations({subtree: true}).length", "1");
    is(std::string{seeked} + " a.cancel();", "document.getAnimations().length", "0");
    is(seeked, "a.effect.target === t", "true");
    is(seeked, "a.effect.getKeyframes().length", "2");
    is(seeked, "a.effect.getKeyframes()[1].computedOffset", "1");
    is(seeked, "a.effect.getKeyframes()[0].offset", "null");
    is(seeked, "a.effect.getKeyframes()[1].left", "100px");
    is(seeked, "a.effect.getTiming().duration", "1000");
    is(seeked, "a.effect.getTiming().fill", "forwards");
    is(seeked, "a.effect.getComputedTiming().endTime", "1000");
}

void test_the_constructors_build_the_same_thing() {
    is("var e = new KeyframeEffect(t, {left: ['0px', '100px']}, 1000); var a = new Animation(e);"
       " a.play(); a.pause(); a.currentTime = 500;",
       "getComputedStyle(t).left", "50px");
    is("var e = new KeyframeEffect(t, {left: ['0px', '100px']}, 1000); var a = new Animation(e);",
       "a.playState", "idle");
    is("", "(function () { try { Animation(); return 'no'; } catch (e) { return e.name; } })()",
       "TypeError");
}

} // namespace

int main() {
    test_the_surface_exists();
    test_a_seeked_animation_is_what_getComputedStyle_reports();
    test_easing_is_applied_and_may_overshoot();
    test_the_play_state_follows_the_specification();
    test_an_animation_can_stop_having_an_effect();
    test_getAnimations_and_the_effect();
    test_the_constructors_build_the_same_thing();
    REPORT("web_animations");
}
