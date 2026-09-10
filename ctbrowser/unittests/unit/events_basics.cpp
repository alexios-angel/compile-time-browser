// THE EVENT INTERFACES AND THE HANDLER PROPERTY, from a page.
//
// Its own file rather than more of bindings_basics because everything in it is
// one question asked twice: what a page can CONSTRUCT, and what the engine does
// with it when it is dispatched. Those are the two halves dom/events measures,
// and the two that a wrapper-shaped test in bindings_basics cannot see - a
// missing interface object is a name that is not there, and a handler property
// that is never called says nothing at all.
//
// THE CASES THAT ASSERT SOMETHING DOES NOT HAPPEN ARE THE IMPORTANT ONES. A
// passive listener may not cancel, a handler property named in the event's own
// mixed case is not a handler, and `return 0` is not `return false`. Each of
// those is a rule whose only evidence is an event that came back UNcancelled,
// which is indistinguishable from a rule nobody implemented unless the
// cancelling case is asserted beside it.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><head><title>events</title></head><body><div id=here>x</div>
</body></html>)";

[[nodiscard]] std::string answer(const std::string & expression) {
    browser page{browser_options{400, 300}};
    std::string html{page_html};
    const std::string tail = "<script>try { console.log(String(" + expression +
                             ")); } catch (e) { console.log('threw:' + e.name); }</script>";
    html.insert(html.find("</body>"), tail);
    page.load_html(html);
    const std::vector<std::string> & logged = page.bindings().console_output();
    if (logged.empty()) { return "<nothing logged: " + page.script_error() + ">"; }
    return logged.back();
}

void is(const std::string & expression, const std::string & expected) {
    const std::string got = answer(expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

// The four names dom/events feature-detects before it asserts anything.
void test_the_touch_interfaces_exist() {
    is("typeof Touch + ',' + typeof TouchList + ',' + typeof TouchEvent",
       "function,function,function");
    is("'TouchEvent' in self", "true");
    // A TouchEvent is a UIEvent, which is what puts `view` and `detail` on it.
    is("(function () { var e = new TouchEvent('touchstart');"
       " return (e instanceof TouchEvent) + ',' + (e instanceof UIEvent) + ',' +"
       "        (e instanceof Event) + ',' + e.constructor.name; })()",
       "true,true,true,TouchEvent");
    // A TouchList has no constructor in the IDL and this one says so rather
    // than handing back an empty object that is not one.
    is("(function () { try { new TouchList(); return 'made'; }"
       " catch (e) { return e.name; } })()",
       "TypeError");
}

// TouchInit's two REQUIRED members, which are the only rule in the family.
void test_a_touch_needs_an_identifier_and_a_target() {
    is("(function () { var t = new Touch({identifier: 7, target: document.body, clientX: 3});"
       " return t.identifier + ',' + (t.target === document.body) + ',' + t.clientX + ',' +"
       "        t.touchType; })()",
       "7,true,3,direct");
    is("(function () { try { new Touch({target: document.body}); return 'made'; }"
       " catch (e) { return e.name; } })()",
       "TypeError");
    is("(function () { try { new Touch({identifier: 1}); return 'made'; }"
       " catch (e) { return e.name; } })()",
       "TypeError");
    is("(function () { try { new Touch(); return 'made'; } catch (e) { return e.name; } })()",
       "TypeError");
}

// `sequence<Touch>` in, TouchList out - both spellings of reading one, and the
// out-of-range answer, which is `null` rather than undefined.
void test_a_touch_event_carries_touch_lists() {
    is("(function () { var e = new TouchEvent('touchstart');"
       " return e.touches.length + ',' + e.targetTouches.length + ',' +"
       "        e.changedTouches.length + ',' + e.touches.item(0); })()",
       "0,0,0,null");
    is("(function () {"
       " var t = new Touch({identifier: 1, target: document.body});"
       " var e = new TouchEvent('touchstart', {touches: [t], changedTouches: [t]});"
       " return e.touches.length + ',' + (e.touches[0] === t) + ',' +"
       "        (e.touches.item(0) === t) + ',' + e.touches.item(1) + ',' +"
       "        (e.touches instanceof TouchList) + ',' + e.targetTouches.length; })()",
       "1,true,true,null,true,0");
    // EventModifierInit is TouchEventInit's parent, so the four booleans are
    // read from the same dictionary the touches were.
    is("(function () { var e = new TouchEvent('touchmove', {ctrlKey: true, shiftKey: true});"
       " return e.ctrlKey + ',' + e.shiftKey + ',' + e.altKey + ',' + e.metaKey; })()",
       "true,true,false,false");
}

// WHAT THE INTERFACE WAS THE GATE IN FRONT OF. `touchstart` on the window is
// passive by default, so a listener that calls preventDefault is refused - and
// the refusal is only visible on an event that COULD have been cancelled.
void test_a_touchstart_listener_on_the_window_cannot_cancel() {
    is("(function () {"
       " addEventListener('touchstart', function (e) { e.preventDefault(); });"
       " var e = new TouchEvent('touchstart', {cancelable: true});"
       " return dispatchEvent(e) + ',' + e.defaultPrevented; })()",
       "true,false");
    // The same listener on an ordinary target is NOT passive by default, so
    // there the cancel stands. Without this the case above would pass on an
    // engine that had simply lost preventDefault.
    is("(function () {"
       " var target = new EventTarget();"
       " target.addEventListener('touchstart', function (e) { e.preventDefault(); });"
       " var e = new TouchEvent('touchstart', {cancelable: true});"
       " return target.dispatchEvent(e) + ',' + e.defaultPrevented; })()",
       "false,true");
    // And a constructed event is not cancelable unless the page asked, which is
    // the other half of what synthetic-events-cancelable.html checks.
    is("new TouchEvent('touchstart').cancelable", "false");
}

// From `Event` and not from `UIEvent` - they carry no view and no detail.
void test_the_css_event_interfaces() {
    is("(function () { var e = new AnimationEvent('animationend',"
       "     {animationName: 'slide', elapsedTime: 0.5});"
       " return e.animationName + ',' + e.elapsedTime + ',' + e.pseudoElement.length + ',' +"
       "        (e instanceof Event) + ',' + (e instanceof UIEvent); })()",
       "slide,0.5,0,true,false");
    is("(function () { var e = new TransitionEvent('transitionend', {propertyName: 'width'});"
       " return e.propertyName + ',' + e.elapsedTime + ',' + e.constructor.name; })()",
       "width,0,TransitionEvent");
}

// A HANDLER PROPERTY IS NAMED IN LOWERCASE AND AN EVENT TYPE IS NOT.
void test_a_handler_property_is_found_by_the_lowercased_type() {
    // The four prefixed CSS families are the reason: `onwebkitanimationend`
    // listens for `webkitAnimationEnd`, so concatenating found nothing.
    is("(function () { var seen = 'no';"
       " var d = document.createElement('div');"
       " d.onwebkitanimationend = function () { seen = 'yes'; };"
       " d.dispatchEvent(new AnimationEvent('webkitAnimationEnd'));"
       " return seen; })()",
       "yes");
    // And the invention that goes away with it: only the attributes the IDL
    // declares are handlers, so a property spelled in the event's own case is
    // not one. `onmyevent` is not one either in a browser, but this engine
    // looks up `on` + the type and that is the deviation it is admitting to -
    // what matters here is that the two spellings no longer BOTH fire.
    is("(function () { var seen = [];"
       " var d = document.createElement('div');"
       " d.onMyEvent = function () { seen.push('mixed'); };"
       " d.onmyevent = function () { seen.push('lower'); };"
       " d.dispatchEvent(new Event('MyEvent'));"
       " return seen.join('+') || 'none'; })()",
       "lower");
}

// HTML, "processing the return value".
void test_what_a_handler_property_returns_can_cancel() {
    is("(function () { var d = document.createElement('div');"
       " d.onclick = function () { return false; };"
       " var e = new MouseEvent('click', {cancelable: true});"
       " return d.dispatchEvent(e) + ',' + e.defaultPrevented; })()",
       "false,true");
    // EXACTLY `false`, not merely falsy - the return type is `any` and the rule
    // names the boolean, so neither of these cancels in any browser.
    is("(function () { var d = document.createElement('div');"
       " d.onclick = function () { return 0; };"
       " var e = new MouseEvent('click', {cancelable: true});"
       " return d.dispatchEvent(e) + ',' + e.defaultPrevented; })()",
       "true,false");
    is("(function () { var d = document.createElement('div');"
       " d.onclick = function () { return ''; };"
       " var e = new MouseEvent('click', {cancelable: true});"
       " return d.dispatchEvent(e); })()",
       "true");
    // A handler that returns nothing is the common case and must stay a no-op.
    is("(function () { var d = document.createElement('div');"
       " d.onclick = function () {};"
       " var e = new MouseEvent('click', {cancelable: true});"
       " return d.dispatchEvent(e); })()",
       "true");
    // Cancelling here is the DOM's set-the-canceled-flag, so an event that is
    // not cancelable is not cancelled by a `false` either.
    is("(function () { var d = document.createElement('div');"
       " d.onclick = function () { return false; };"
       " var e = new MouseEvent('click');"
       " return d.dispatchEvent(e) + ',' + e.defaultPrevented; })()",
       "true,false");
}

} // namespace

int main() {
    test_the_touch_interfaces_exist();
    test_a_touch_needs_an_identifier_and_a_target();
    test_a_touch_event_carries_touch_lists();
    test_a_touchstart_listener_on_the_window_cannot_cancel();
    test_the_css_event_interfaces();
    test_a_handler_property_is_found_by_the_lowercased_type();
    test_what_a_handler_property_returns_can_cancel();
    REPORT("events_basics");
}
