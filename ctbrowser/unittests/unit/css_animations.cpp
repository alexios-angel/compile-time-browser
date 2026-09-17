// CSS Animations and CSS Transitions, driven from the cascade - the shape
// css/support/interpolation-testcommon.js drives every `*-interpolation.html`
// with: `animation-delay: -50s` on a `100s` animation read back synchronously
// through getComputedStyle, and a transition started by changing a property
// after a style recalc. See lib/Shell/bindings/animations/css.cpp for the
// model and lib/Style/css/keyframes.cpp for what a `@keyframes` rule becomes.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

constexpr const char * stage =
    // `animation-timing-function` is not a property-table row yet, so the
    // element's is always the initial `ease`; the keyframes say `linear`.
    "<style>@keyframes slide { from { left: 0px; animation-timing-function: linear }"
    " to { left: 100px } }"
    "@keyframes fade { from { opacity: 0; animation-timing-function: steps(1, start) }"
    " to { opacity: 1 } }"
    "@keyframes paint { from { color: rgb(0, 0, 255); animation-timing-function: linear }"
    " to { color: rgba(255, 0, 0, 0.5) } }"
    "@keyframes shadow { from { box-shadow: rgb(0, 0, 0) 0px 0px; animation-timing-function:"
    " linear } to { box-shadow: rgb(0, 0, 0) 10px 20px } }"
    "</style>"
    "<div id=c style='position:relative;width:100px;height:100px'>"
    "<div id=t style='position:absolute;left:0px;width:10px;height:10px;opacity:1'></div></div>";

[[nodiscard]] std::string last_line(browser & page) {
    const std::vector<std::string> & logged = page.bindings().console_output();
    if (logged.empty()) { return "<nothing logged: " + page.script_error() + ">"; }
    return logged.back();
}

// A page with the stage, `setup` run as its script, then `expression` logged.
[[nodiscard]] std::string answer(const std::string & setup, const std::string & expression) {
    browser page{browser_options{400, 300}};
    page.load_html(std::string{"<!DOCTYPE html><html><body>"} + stage +
                   "<script>var t = document.getElementById('t');" + setup +
                   "\ntry { console.log(String(" + expression +
                   ")); } catch (e) { console.log('threw:' + e.name + ':' + e.message); }"
                   "</script></body></html>");
    return last_line(page);
}

void is(const std::string & setup, const std::string & expression, const std::string & expected) {
    const std::string got = answer(setup, expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

// The harness's CSS Animations row: a name, 100s, -50s.
constexpr const char * midway =
    "t.style.animationName = 'slide'; t.style.animationDuration = '100s';"
    " t.style.animationDelay = '-50s';";
// ...and its CSS Transitions row: recalc, then the transition, then the change.
constexpr const char * transitioned =
    "getComputedStyle(t).left; t.style.transitionProperty = 'left';"
    " t.style.transitionDuration = '100s'; t.style.transitionDelay = '-50s';"
    " t.style.transitionTimingFunction = 'linear'; t.style.left = '100px';";

void test_keyframes_are_captured() {
    ctbrowser::atom_table atoms;
    ctbrowser::style::engine engine{atoms};
    engine.add_sheet("@keyframes k { from { left: 0px } 50%, to { left: 100px; margin: 1px 2px;"
                     " animation-timing-function: linear } }"
                     "@-webkit-keyframes k2 { to { opacity: 0 } }"
                     "@keyframes none { to { opacity: 0 } }"
                     "@keyframes k { from { top: 1px } }"
                     "@media (min-width: 5000px) { @keyframes k { from { top: 9px } } }");
    const ctbrowser::style::keyframes_rule * k = engine.keyframes_of("k");
    CHECK(k != nullptr);
    // The LAST unconditional rule of the name wins; the one behind a false
    // media query does not.
    CHECK(k != nullptr && k->keyframes.size() == 1 && k->keyframes[0].values.size() == 1 &&
          k->keyframes[0].values[0].first == "top" && k->keyframes[0].values[0].second == "1px");
    engine.clear_origin(1);
    CHECK(engine.keyframes_of("k") == nullptr);
    engine.add_sheet("@keyframes k { from { left: 0px } 50%, to { left: 100px; margin: 1px 2px;"
                     " animation-timing-function: linear } }"
                     "@-webkit-keyframes k2 { to { opacity: 0 } }"
                     "@keyframes none { to { opacity: 0 } }");
    k = engine.keyframes_of("k");
    CHECK(k != nullptr);
    if (k != nullptr) {
        CHECK_EQ(k->keyframes.size(), std::size_t{3});
        CHECK_EQ(k->keyframes[0].offset, 0.0);
        CHECK_EQ(k->keyframes[1].offset, 0.5);
        CHECK_EQ(k->keyframes[2].offset, 1.0);
        CHECK_EQ(k->keyframes[1].easing, std::string{"linear"});
        CHECK(k->keyframes[0].easing.empty());
        // The shorthand arrived as its longhands.
        CHECK_EQ(k->keyframes[2].values.size(), std::size_t{5});
        CHECK_EQ(k->keyframes[2].values[1].first, std::string{"margin-top"});
        CHECK_EQ(k->keyframes[2].values[1].second, std::string{"1px"});
    }
    CHECK(engine.keyframes_of("k2") != nullptr);
    CHECK(engine.keyframes_of("K") == nullptr);
    CHECK(engine.keyframes_of("none") == nullptr);
}

void test_a_css_animation_is_sampled_at_the_flush() {
    is(midway, "getComputedStyle(t).left", "50px");
    is(midway, "t.getAnimations().length", "1");
    is(midway, "t.getAnimations()[0] instanceof CSSAnimation", "true");
    is(midway, "t.getAnimations()[0] instanceof Animation", "true");
    is(midway, "t.getAnimations()[0].animationName", "slide");
    is(midway, "t.getAnimations()[0].playState", "running");
    is(midway, "t.getAnimations()[0].effect.getKeyframes().length", "2");
    // The same object survives a restyle that keeps the name.
    is(std::string{midway} + " var a = t.getAnimations()[0]; t.style.animationDuration = '200s';",
       "t.getAnimations()[0] === a && getComputedStyle(t).left", "25px");
    // A keyframe's own timing function; the element's initial `ease` is
    // 0.8024 at the midpoint, `steps(1, start)` is 1.
    is("t.style.animationName = 'fade'; t.style.animationDuration = '100s';"
       " t.style.animationDelay = '-50s';",
       "getComputedStyle(t).opacity", "1");
    // Two names, two animations, in list order.
    is("t.style.animationName = 'slide, fade'; t.style.animationDuration = '100s';"
       " t.style.animationDelay = '-50s';",
       "t.getAnimations().map(function (a) { return a.animationName; }).join()", "slide,fade");
    // Removing the name cancels it.
    is(std::string{midway} + " getComputedStyle(t).left; t.style.animationName = 'none';",
       "t.getAnimations().length + ' ' + getComputedStyle(t).left", "0 0px");
    // A name no rule defines is no animation.
    is("t.style.animationName = 'nope'; t.style.animationDuration = '100s';",
       "t.getAnimations().length", "0");
    // Colours interpolate in premultiplied sRGB: blue to half-transparent red
    // at the midpoint is rgba(85, 0, 170, 0.75) - the opaque endpoint weighs
    // twice the translucent one.
    is("t.style.animationName = 'paint'; t.style.animationDuration = '100s';"
       " t.style.animationDelay = '-50s';",
       "getComputedStyle(t).color", "rgba(85, 0, 170, 0.75)");
    // A list interpolates item by item when the shapes agree.
    is("t.style.animationName = 'shadow'; t.style.animationDuration = '100s';"
       " t.style.animationDelay = '-50s';",
       "getComputedStyle(t).boxShadow", "rgb(0, 0, 0) 5px 10px 0px 0px");
    // The constructor is not callable.
    is("",
       "(function () { try { new CSSAnimation(); return 'no'; } catch (e) { return e.name; } })()",
       "TypeError");
}

void test_a_css_transition_starts_from_the_before_change_style() {
    is(transitioned, "getComputedStyle(t).left", "50px");
    is(transitioned, "t.getAnimations()[0] instanceof CSSTransition", "true");
    is(transitioned, "t.getAnimations()[0].transitionProperty", "left");
    is(transitioned, "t.getAnimations()[0].effect.getTiming().fill", "backwards");
    // `all` covers it; `none` and an unrelated name do not.
    is("getComputedStyle(t).left; t.style.transition = 'all 100s -50s linear'; t.style.left = "
       "'100px';",
       "getComputedStyle(t).left", "50px");
    is("getComputedStyle(t).left; t.style.transitionProperty = 'none';"
       " t.style.transitionDuration = '100s'; t.style.left = '100px';",
       "getComputedStyle(t).left", "100px");
    is("getComputedStyle(t).left; t.style.transitionProperty = 'top';"
       " t.style.transitionDuration = '100s'; t.style.transitionDelay = '-50s';"
       " t.style.left = '100px';",
       "getComputedStyle(t).left", "100px");
    // No recalc between the two values: the first is never a before-change
    // style, so nothing transitions.
    is("t.style.transition = 'left 100s -50s linear'; t.style.left = '100px';",
       "getComputedStyle(t).left", "100px");
    // A pair that cannot interpolate does not transition...
    is("t.style.zIndex = '1'; getComputedStyle(t).zIndex;"
       " t.style.transition = 'z-index 100s -50s linear'; t.style.zIndex = 'auto';",
       "getComputedStyle(t).zIndex + ' ' + t.getAnimations().length", "auto 0");
    // ...and a colour does.
    is("t.style.color = 'rgb(0, 0, 255)'; getComputedStyle(t).color;"
       " t.style.transition = 'color 100s -50s linear'; t.style.color = 'rgb(255, 0, 0)';",
       "getComputedStyle(t).color", "rgb(128, 0, 128)");
    // A shorthand names its longhands, each with a transition of its own.
    is("t.style.padding = '10px 20px'; getComputedStyle(t).padding;"
       " t.style.transition = 'padding 100s -50s linear'; t.style.padding = '30px 40px';",
       "getComputedStyle(t).padding + ' ' + t.getAnimations().length + ' ' +"
       " t.getAnimations()[0].transitionProperty",
       "20px 30px 4 padding-top");
    // Setting the property back to the running transition's end does nothing;
    // setting it somewhere else replaces the transition from its current value.
    is(std::string{transitioned} + " var a = t.getAnimations()[0]; t.style.left = '100px';",
       "t.getAnimations()[0] === a", "true");
    is(std::string{transitioned} + " getComputedStyle(t).left; t.style.left = '200px';",
       "getComputedStyle(t).left", "125px");
}

void test_reversing_shortens_the_way_back() {
    browser page{browser_options{400, 300}};
    page.load_html(std::string{"<!DOCTYPE html><html><body>"} + stage +
                   "<script>var t = document.getElementById('t');"
                   "getComputedStyle(t).left; t.style.transition = 'left 1000ms linear';"
                   " t.style.left = '100px'; getComputedStyle(t).left;</script></body></html>");
    // CSS Transitions §3.1: a quarter of the way there, sent back: the return
    // trip is a quarter as long, from where it is.
    (void)page.tick(250);
    CHECK(page.run_script("console.log(getComputedStyle(t).left)"));
    CHECK_EQ(last_line(page), std::string{"25px"});
    CHECK(page.run_script(
        "t.style.left = '0px'; var a = t.getAnimations()[0];"
        "console.log(getComputedStyle(t).left + ' ' + a.effect.getTiming().duration)"));
    CHECK_EQ(last_line(page), std::string{"25px 250"});
    (void)page.tick(125);
    CHECK(page.run_script("console.log(getComputedStyle(t).left)"));
    CHECK_EQ(last_line(page), std::string{"12.5px"});
    (void)page.tick(200);
    // Over: the after-change value, and no relevant animation left.
    CHECK(
        page.run_script("console.log(getComputedStyle(t).left + ' ' + t.getAnimations().length)"));
    CHECK_EQ(last_line(page), std::string{"0px 0"});
}

void test_events_fire_as_the_clock_moves() {
    browser page{browser_options{400, 300}};
    page.load_html(
        std::string{"<!DOCTYPE html><html><body>"} + stage +
        "<script>var t = document.getElementById('t'); var log = [];"
        "for (const type of ['animationstart', 'animationiteration', 'animationend',"
        " 'animationcancel', 'transitionrun', 'transitionstart', 'transitionend', "
        "'transitioncancel'])"
        " t.addEventListener(type, function (e) { log.push(e.type + ':' + (e.animationName ||"
        " e.propertyName) + ':' + e.elapsedTime + ':' + (e instanceof AnimationEvent ? 'A' : "
        "'T')); });"
        "t.style.animationName = 'slide'; t.style.animationDuration = '100ms';"
        " t.style.animationDelay = '50ms'; t.style.animationIterationCount = '2';"
        "getComputedStyle(t).left; t.style.transition = 'opacity 100ms 20ms';"
        " t.style.opacity = '0'; getComputedStyle(t).opacity;</script></body></html>");
    const auto logged = [&page] {
        CHECK(page.run_script("console.log(log.join(' ')); log = [];"));
        return last_line(page);
    };
    (void)page.tick(10);
    CHECK_EQ(logged(), std::string{"transitionrun:opacity:0:T"});
    (void)page.tick(50); // 60: the animation's delay is over, and so is the transition's
    CHECK_EQ(logged(), std::string{"transitionstart:opacity:0:T animationstart:slide:0:A"});
    (void)page.tick(100); // 160: second iteration; transition done at 120
    CHECK_EQ(logged(), std::string{"transitionend:opacity:0.1:T animationiteration:slide:0.1:A"});
    (void)page.tick(100); // 260: done at 250
    CHECK_EQ(logged(), std::string{"animationend:slide:0.2:A"});
    CHECK(page.run_script("console.log(t.getAnimations().length)"));
    CHECK_EQ(last_line(page), std::string{"0"});
    // A cancelled one says so, with the time it had run.
    CHECK(page.run_script("t.style.animationName = 'fade'; t.style.animationDelay = '0ms';"
                          " t.style.animationIterationCount = '1'; getComputedStyle(t).opacity;"));
    (void)page.tick(30);
    (void)logged();
    CHECK(page.run_script("t.style.animationName = 'none'; getComputedStyle(t).opacity;"));
    (void)page.tick(1);
    CHECK_EQ(logged(), std::string{"animationcancel:fade:0.03:A"});
    // Nothing left to wake up for.
    CHECK(page.next_wakeup_ms() == std::numeric_limits<double>::infinity());

    // THE LEGACY TYPES (DOM "invoke" step 6): a trusted `animationend` with
    // no listener for that name reaches a `webkitAnimationEnd` listener and
    // the `onwebkitanimationend` handler, under the prefixed type; one that
    // has an unprefixed listener does not; a synthetic event is never mapped.
    CHECK(page.run_script(
        "var u = document.createElement('div'); document.body.appendChild(u);"
        "u.addEventListener('webkitAnimationEnd', function (e) { log.push('legacy:' + e.type); });"
        "u.onwebkitanimationend = function (e) { log.push('handler:' + e.type); };"
        "u.style.animation = 'fade 20ms';"
        "u.dispatchEvent(new AnimationEvent('animationend'));"
        "getComputedStyle(u).opacity;"));
    (void)page.tick(40);
    CHECK_EQ(logged(), std::string{"legacy:webkitAnimationEnd handler:webkitAnimationEnd"});
    CHECK(page.run_script(
        "u.addEventListener('animationend', function (e) { log.push('plain:' + e.type); });"
        "u.style.animation = 'none'; getComputedStyle(u).opacity; u.style.animation = 'fade 20ms';"
        " getComputedStyle(u).opacity;"));
    (void)page.tick(40);
    CHECK_EQ(logged(), std::string{"plain:animationend"});
}

} // namespace

int main() {
    test_keyframes_are_captured();
    test_a_css_animation_is_sampled_at_the_flush();
    test_a_css_transition_starts_from_the_before_change_style();
    test_reversing_shortens_the_way_back();
    test_events_fire_as_the_clock_moves();
    REPORT("css_animations");
}
