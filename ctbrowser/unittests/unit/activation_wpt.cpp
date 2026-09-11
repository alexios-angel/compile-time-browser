// ACTIVATION BEHAVIOUR IN DISPATCH - DOM 2.9 steps 5.5-5.11 and 11 - one case
// per rule dom/events found the other answer to: a checkbox is toggled BEFORE
// its click listeners run, a `click` that is not a MouseEvent toggles nothing,
// a parent's activation behaviour is reached only when the event bubbles and
// only the first is run, preventDefault puts the checkedness back, `input`
// and `change` fire on activation and only when connected, `element.click()`
// on a disabled control does nothing at all while dispatchEvent still reaches
// it, the event has stopped travelling by the time `change` fires, a form that
// is not connected does not submit, a button inside a link is the activation
// target rather than the link, a label's activation is a click at its control,
// and the engine's own mouse click takes the same path.
//
// Each case names the WPT file it stands in for. They are here rather than in
// events_wpt because every one is about what happens AT the activation target,
// where that file is about the path an event travels.

#include <ctbrowser.hpp>

#include "check.hpp"
#include "dom_probe.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;
using ctbrowser_test::box_of;

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><head><title>activation</title></head><body>
<div id=dump></div>
<input id=c type=checkbox>
<input id=d type=checkbox disabled>
</body></html>)";

// ONE EXPRESSION, EVALUATED AFTER THE PAGE HAS LAID OUT, with `dump` in scope
// the way Event-dispatch-click.html has it.
[[nodiscard]] std::string answer(const std::string & expression) {
    browser page{browser_options{400, 300}};
    page.load_html(page_html);
    (void)page.frame();
    const std::string source = "try { var dump = document.getElementById('dump');"
                               " console.log(String(" +
                               expression + ")); } catch (e) { console.log('threw:' + e.name); }";
    if (!page.run_script(source)) { return "<did not run: " + page.script_error() + ">"; }
    const std::vector<std::string> & logged = page.bindings().console_output();
    if (logged.empty()) { return "<nothing logged: " + page.script_error() + ">"; }
    return logged.back();
}

void is(const std::string & expression, const std::string & expected) {
    const std::string got = answer(expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

// A checkbox appended to `dump`, as most of the file's cases begin.
constexpr const char * checkbox_setup = "var i = document.createElement('input');"
                                        " i.type = 'checkbox'; dump.appendChild(i);";

// --- the legacy-pre-activation behaviour ------------------------------------

void test_click_toggles_before_the_listeners_run() {
    // Event-dispatch-click.html "basic with click()": the click listener reads
    // the NEW checkedness. The event is a MouseEvent and, being synthetic, is
    // not trusted.
    is(std::string{"(function () {"} + checkbox_setup +
           " var seen;"
           " i.onclick = function (e) {"
           "     seen = i.checked + ',' + (e instanceof MouseEvent) + ',' + e.isTrusted; };"
           " i.click(); return seen + ',' + i.checked; })()",
       "true,true,false,true");
}

void test_a_click_that_is_not_a_mouse_event_toggles_nothing() {
    // "basic with dispatchEvent()" and "basic with wrong event class": only a
    // MouseEvent named click has activation behaviour.
    is(std::string{"(function () {"} + checkbox_setup +
           " i.dispatchEvent(new MouseEvent('click')); var a = i.checked;"
           " i.dispatchEvent(new Event('click', {bubbles: true}));"
           " return a + ',' + i.checked; })()",
       "true,true");
}

void test_a_parent_is_the_activation_target_only_when_the_event_bubbles() {
    // "look at parents only when event bubbles" / "look at parents when event
    // bubbles": a click at a text child reaches the checkbox's activation
    // behaviour only through bubbling.
    is(std::string{"(function () {"} + checkbox_setup +
           " var t = i.appendChild(document.createTextNode('x'));"
           " t.dispatchEvent(new MouseEvent('click')); var a = i.checked;"
           " t.dispatchEvent(new MouseEvent('click', {bubbles: true}));"
           " return a + ',' + i.checked; })()",
       "false,true");
}

void test_only_the_first_node_with_activation_behaviour_is_activated() {
    // "pick the first with activation behavior <input type=checkbox>": the
    // child checkbox, not the one it is inside.
    is(std::string{"(function () {"} + checkbox_setup +
           " var child = i.appendChild(document.createElement('input'));"
           " child.type = 'checkbox';"
           " child.dispatchEvent(new MouseEvent('click', {bubbles: true}));"
           " return i.checked + ',' + child.checked; })()",
       "false,true");
}

// --- the legacy-canceled-activation behaviour --------------------------------

void test_prevent_default_puts_the_checkedness_back() {
    // "disabled checkbox should get legacy-canceled-activation behavior": the
    // listener saw it checked; after the dispatch it is not.
    is(std::string{"(function () {"} + checkbox_setup +
           " var seen;"
           " i.onclick = function (e) { seen = i.checked; e.preventDefault(); };"
           " i.dispatchEvent(new MouseEvent('click', {cancelable: true}));"
           " return seen + ',' + i.checked; })()",
       "true,false");
}

void test_a_cancelled_radio_click_rechecks_the_radio_it_unchecked() {
    // Event-dispatch-click.tentative.html "radio morphed into another type
    // should not steal the existing checked state": the group's checked radio
    // comes back.
    is("(function () {"
       " var r1 = dump.appendChild(document.createElement('input'));"
       " var r2 = dump.appendChild(document.createElement('input'));"
       " r1.type = r2.type = 'radio'; r1.name = r2.name = 'g'; r2.checked = true;"
       " r1.onclick = function (e) { e.preventDefault(); };"
       " r1.dispatchEvent(new MouseEvent('click', {cancelable: true}));"
       " return r1.checked + ',' + r2.checked; })()",
       "false,true");
}

// --- the activation behaviour -----------------------------------------------

void test_input_then_change_fire_only_when_connected() {
    // Event-dispatch-detached-input-and-change.html: nothing while detached,
    // `input` (bubbles, composed) then `change` (bubbles) once in the document,
    // neither cancelable.
    is("(function () {"
       " var i = document.createElement('input'); i.type = 'checkbox'; var log = [];"
       " i.addEventListener('input', function (e) {"
       "     log.push('input:' + e.bubbles + e.composed + e.cancelable); });"
       " i.addEventListener('change', function (e) {"
       "     log.push('change:' + e.bubbles + e.composed + e.cancelable); });"
       " i.click(); var before = log.length;"
       " dump.appendChild(i); i.click();"
       " return before + ',' + log.join(' '); })()",
       "0,input:truetruefalse change:truefalsefalse");
}

void test_click_on_a_disabled_control_does_nothing_but_dispatch_event_reaches_it() {
    // "disabled checkbox still has activation behavior" and "disabled checkbox
    // should be checked from dispatchEvent(new MouseEvent("click"))": click()
    // returns before dispatching; dispatchEvent toggles and fires onclick.
    is(std::string{"(function () {"} + checkbox_setup +
           " i.disabled = true; var fired = false; i.onclick = function () { fired = true; };"
           " i.click(); var a = fired + ',' + i.checked;"
           " i.dispatchEvent(new MouseEvent('click'));"
           " return a + ',' + fired + ',' + i.checked; })()",
       "false,false,true,true");
}

void test_the_event_has_stopped_travelling_when_change_fires() {
    // "event state during post-click handling": eventPhase NONE, currentTarget
    // null, the path empty - and the target still the checkbox.
    is(std::string{"(function () {"} + checkbox_setup +
           " var ev = new MouseEvent('click'); var seen;"
           " i.onchange = function () {"
           "     seen = ev.eventPhase + ',' + ev.currentTarget + ',' + (ev.target === i) + ','"
           "            + ev.composedPath().length; };"
           " i.dispatchEvent(ev); return seen; })()",
       "0,null,true,0");
}

void test_a_form_submits_only_when_connected_and_the_button_enabled() {
    // "disconnected form should not submit", then the same form in the
    // document, then "submit button should not activate if the event listener
    // disables it".
    is("(function () {"
       " var f = document.createElement('form'); var did = false;"
       " f.onsubmit = function (e) { did = true; e.preventDefault(); };"
       " var s = f.appendChild(document.createElement('input')); s.type = 'submit';"
       " s.click(); var a = did;"
       " dump.appendChild(f); s.click(); var b = did;"
       " did = false; s.onclick = function () { s.disabled = true; }; s.click();"
       " return a + ',' + b + ',' + did; })()",
       "false,true,false");
}

void test_a_button_inside_a_link_is_the_activation_target() {
    // Event-dispatch-single-activation-behavior.html: the first node on the
    // path with activation behaviour is the button, so the link is not
    // followed. It used to be: the default action walked up from the click to
    // the nearest <a>.
    is("(function () {"
       " var a = document.createElement('a'); a.href = '#gone';"
       " var b = a.appendChild(document.createElement('button')); b.type = 'button';"
       " dump.appendChild(a); b.click(); return '[' + location.hash + ']'; })()",
       "[]");
}

void test_a_label_activation_is_a_click_at_its_control() {
    // The same file's <label><input><span class=click> row, and
    // Event-dispatch-click.tentative.html "disabled checkbox should not be
    // checked from label click": one click event at the control, and none once
    // it is disabled.
    is("(function () {"
       " var l = document.createElement('label');"
       " var c = l.appendChild(document.createElement('input')); c.type = 'checkbox';"
       " var span = l.appendChild(document.createElement('span'));"
       " dump.appendChild(l); var clicks = 0; c.onclick = function () { ++clicks; };"
       " span.click(); var a = clicks + ',' + c.checked;"
       " c.disabled = true; l.click();"
       " return a + ',' + clicks + ',' + c.checked; })()",
       "1,true,1,true");
}

// --- the engine's own click -------------------------------------------------

void test_a_mouse_click_takes_the_same_path() {
    // browser::handle's mouse_up used to dispatch a plain Event and call the
    // default action itself; now it is a MouseEvent through dispatch, so the
    // checkbox is checked when its click listener runs and `input` and `change`
    // follow. A disabled control hears no click at all.
    browser page{browser_options{400, 300}};
    page.load_html(page_html);
    (void)page.frame();
    CHECK(page.run_script(
        "var log = [];"
        "function wire(id) {"
        "    var box = document.getElementById(id);"
        "    box.onclick = function (e) {"
        "        log.push(id + ':click:' + box.checked + ':' + (e instanceof MouseEvent)); };"
        "    box.oninput = function () { log.push(id + ':input'); };"
        "    box.onchange = function () { log.push(id + ':change'); };"
        "}"
        "wire('c'); wire('d');"));
    for (const char * id : {"c", "d"}) {
        const ctbrowser::rect box = box_of(page, id);
        const float x = box.x + box.width / 2;
        const float y = box.y + box.height / 2;
        (void)page.handle(input_event::mouse_down_at(x, y));
        (void)page.handle(input_event::mouse_up_at(x, y));
    }
    CHECK(page.run_script("console.log(log.join(' ') + '|' + document.getElementById('c').checked"
                          " + document.getElementById('d').checked)"));
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK_EQ(logged.empty() ? std::string{} : logged.back(),
             std::string{"c:click:true:true c:input c:change|truefalse"});
}

} // namespace

int main() {
    test_click_toggles_before_the_listeners_run();
    test_a_click_that_is_not_a_mouse_event_toggles_nothing();
    test_a_parent_is_the_activation_target_only_when_the_event_bubbles();
    test_only_the_first_node_with_activation_behaviour_is_activated();
    test_prevent_default_puts_the_checkedness_back();
    test_a_cancelled_radio_click_rechecks_the_radio_it_unchecked();
    test_input_then_change_fire_only_when_connected();
    test_click_on_a_disabled_control_does_nothing_but_dispatch_event_reaches_it();
    test_the_event_has_stopped_travelling_when_change_fires();
    test_a_form_submits_only_when_connected_and_the_button_enabled();
    test_a_button_inside_a_link_is_the_activation_target();
    test_a_label_activation_is_a_click_at_its_control();
    test_a_mouse_click_takes_the_same_path();
    REPORT("activation_wpt");
}
