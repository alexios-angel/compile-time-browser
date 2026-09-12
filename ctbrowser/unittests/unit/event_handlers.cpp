// EVENT HANDLERS: what a listener that throws does, and what an `on...`
// property is.
//
// Two things that look unrelated and are the same subject. A handler property
// is a listener registered by assignment, and both of them are CALLED by
// `fire_at` - so the questions a page can ask about them are the same
// questions: does it run, what is `this`, and where does an exception go.
//
// The exception is the reason this file exists. `context::throw_error` unwinds
// to the innermost live `try` ANYWHERE below it on the stack, and a dispatch is
// almost always reached from inside one - `test(function () { ... })` is a
// `try`, and so is every library's own error reporting. So a listener that
// threw did not stop at `dispatchEvent`: it left the dispatch entirely, the
// remaining listeners never ran, and the page was told nothing. Every case
// below that dispatches from inside a `try` is testing that containment, and
// the harness helper puts one there ON PURPOSE.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><head><title>handlers</title></head><body>
<div id=target>hello</div>
</body></html>)";

// ONE EXPRESSION, EVALUATED INSIDE A `try`. The `try` is not politeness: it is
// the outer handler a thrown exception would escape to if the fence were not
// there, so a fence regression shows up here as "threw:Error" rather than as
// the answer.
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

// --- a listener that throws ------------------------------------------------

void test_a_throwing_listener_does_not_leave_the_dispatch() {
    // dom/events/Event-dispatch-throwing.html, first test. `dispatchEvent`
    // RETURNS; the exception does not reach the `try` the page is inside.
    is("(function () {"
       " var el = document.createElement('div');"
       " el.addEventListener('x', function () { throw new Error('boom'); });"
       " el.dispatchEvent(new Event('x'));"
       " return 'returned'; })()",
       "returned");
    // ...and the same through a handler PROPERTY, which is the other way to
    // register one.
    is("(function () {"
       " var el = document.createElement('div');"
       " el.onx = function () { throw new Error('boom'); };"
       " el.dispatchEvent(new Event('x'));"
       " return 'returned'; })()",
       "returned");
}

void test_a_throwing_listener_does_not_stop_the_next_one() {
    // Event-dispatch-throwing.html's second test: "Error from first listener",
    // and the second listener still runs. It failed twice over - the throw left
    // the dispatch, and every later `cx.call` declined while the VM's failure
    // flag was up.
    is("(function () {"
       " var el = document.createElement('div');"
       " var second = false;"
       " el.addEventListener('x', function () { throw new Error('first'); });"
       " el.addEventListener('x', function () { second = true; });"
       " el.dispatchEvent(new Event('x'));"
       " return second; })()",
       "true");
}

void test_the_page_hears_about_it() {
    // `window.onerror` takes a STRING first - OnErrorEventHandler is
    // (message, filename, lineno, colno, error) and not the event.
    is("(function () {"
       " var kind = 'never called';"
       " window.onerror = function (message) { kind = typeof message; };"
       " var el = document.createElement('div');"
       " el.addEventListener('x', function () { throw new Error('boom'); });"
       " el.dispatchEvent(new Event('x'));"
       " return kind; })()",
       "string");
    // ONE report per throw, not one per dispatch and not none.
    is("(function () {"
       " var count = 0;"
       " window.onerror = function () { count++; };"
       " var el = document.createElement('div');"
       " el.addEventListener('x', function () { throw new Error('boom'); });"
       " el.dispatchEvent(new Event('x'));"
       " return count; })()",
       "1");
    // AND THE VALUE, not only the text. EventListener-handleEvent.html rethrows
    // `event.error` and asserts its identity against the object the listener
    // threw, which no string can answer.
    is("(function () {"
       " var seen = null;"
       " var thrown = { name: 'test' };"
       " window.addEventListener('error', function (e) { seen = e.error; });"
       " var el = document.createElement('div');"
       " el.addEventListener('x', function () { throw thrown; });"
       " el.dispatchEvent(new Event('x'));"
       " return seen === thrown; })()",
       "true");
}

void test_a_throwing_onerror_is_contained_too() {
    // window-event-restored-after-throwing-onerror.html: the reporting dispatch
    // is a dispatch, so a handler that throws out of it has to be contained by
    // the same fence - and the report is NOT retried, or an onerror that always
    // throws would recurse without bound.
    is("(function () {"
       " var calls = 0;"
       " window.onerror = function () { calls++; throw new Error('onerror throws'); };"
       " var el = document.createElement('div');"
       " el.addEventListener('x', function () { throw new Error('boom'); });"
       " el.dispatchEvent(new Event('x'));"
       " return calls; })()",
       "1");
}

// --- EventListener is a callback INTERFACE ---------------------------------

void test_an_object_with_handle_event_is_a_listener() {
    is("(function () {"
       " var calls = 0;"
       " var listener = { handleEvent: function () { calls++; } };"
       " var el = document.createElement('div');"
       " el.addEventListener('x', listener);"
       " el.dispatchEvent(new Event('x'));"
       " return calls; })()",
       "1");
    // `this` IS THE LISTENER OBJECT, not the target: handleEvent is a method of
    // the object and reads its own state.
    is("(function () {"
       " var listener = { seen: null, handleEvent: function () { this.seen = 'me'; } };"
       " var el = document.createElement('div');"
       " el.addEventListener('x', listener);"
       " el.dispatchEvent(new Event('x'));"
       " return listener.seen; })()",
       "me");
}

void test_handle_event_is_looked_up_on_every_dispatch() {
    // EventListener-handleEvent.html, "performs `Get` every time event is
    // dispatched": the Get is at DISPATCH time, not at registration, so an
    // accessor runs once per dispatch and a method replaced in between takes
    // effect.
    is("(function () {"
       " var calls = 0;"
       " var el = document.createElement('div');"
       " el.addEventListener('x', { get handleEvent() { calls++; return function () {}; } });"
       " el.dispatchEvent(new Event('x'));"
       " el.dispatchEvent(new Event('x'));"
       " return calls; })()",
       "2");
    is("(function () {"
       " var which = 'none';"
       " var listener = { handleEvent: function () { which = 'old'; } };"
       " var el = document.createElement('div');"
       " el.addEventListener('x', listener);"
       " listener.handleEvent = function () { which = 'new'; };"
       " el.dispatchEvent(new Event('x'));"
       " return which; })()",
       "new");
}

void test_a_function_is_never_asked_for_handle_event() {
    // The other half of the same clause, and the last two tests of
    // EventListener-handleEvent.html: a callable listener is CALLED, whatever
    // properties it happens to carry.
    is("(function () {"
       " var calls = 0;"
       " var listener = function () { calls += 1; };"
       " listener.handleEvent = function () { calls += 10; };"
       " var el = document.createElement('div');"
       " el.addEventListener('x', listener);"
       " el.dispatchEvent(new Event('x'));"
       " return calls; })()",
       "1");
}

void test_a_listener_object_without_a_callable_handle_event_is_a_type_error() {
    // ...and it is a TypeError REPORTED to the page, not a listener that quietly
    // does nothing and not an exception that escapes the dispatch. Both the
    // falsy and the truthy case, which that file tests separately.
    is("(function () {"
       " var name = 'nothing reported';"
       " window.addEventListener('error', function (e) { name = e.error && e.error.name; });"
       " var el = document.createElement('div');"
       " el.addEventListener('x', { handleEvent: null });"
       " el.dispatchEvent(new Event('x'));"
       " return name; })()",
       "TypeError");
    is("(function () {"
       " var name = 'nothing reported';"
       " window.addEventListener('error', function (e) { name = e.error && e.error.name; });"
       " var el = document.createElement('div');"
       " el.addEventListener('x', { handleEvent: 42 });"
       " el.dispatchEvent(new Event('x'));"
       " return name; })()",
       "TypeError");
}

void test_a_throwing_handle_event_getter_reaches_the_page() {
    // WebIDL propagates an abrupt Get rather than swallowing it, and the value
    // the getter threw is the one the page must see - not a TypeError this
    // engine raised afterwards because the Get answered nothing.
    is("(function () {"
       " var seen = null;"
       " var thrown = { name: 'test' };"
       " window.addEventListener('error', function (e) { seen = e.error; });"
       " var el = document.createElement('div');"
       " el.addEventListener('x', { get handleEvent() { throw thrown; } });"
       " el.dispatchEvent(new Event('x'));"
       " return seen === thrown; })()",
       "true");
}

// --- the EventTarget surface ------------------------------------------------

void test_the_prototypes_methods_know_a_node_when_they_see_one() {
    // EVERY WRAPPER INHERITS THESE. The interface chain ends at EventTarget, so
    // `EventTarget.prototype.addEventListener` is reachable from a comment node
    // as much as from `new EventTarget()` - and reading `this` as a standalone
    // target would put the listener in a bucket no dispatch through the tree
    // ever visits.
    //
    // Called as a METHOD of the node rather than through
    // `Function.prototype.call`, because what is under test is which target the
    // receiver names and nothing else.
    is("(function () {"
       " var node = document.createComment('c');"
       " node.viaProto = EventTarget.prototype.addEventListener;"
       " var seen = null;"
       " node.viaProto('x', function () { seen = this; });"
       " node.dispatchEvent(new Event('x'));"
       " return seen === node; })()",
       "true");
    // ...and the document, which is a Proxy and so is neither `is_object()` nor
    // a wrapper with a handle.
    is("(function () {"
       " var count = 0;"
       " document.viaProto = EventTarget.prototype.addEventListener;"
       " document.viaProto('x', function () { count++; });"
       " document.body.dispatchEvent(new Event('x', { bubbles: true }));"
       " return count; })()",
       "1");
}

void test_a_standalone_target_is_still_its_own_whole_path() {
    is("(function () {"
       " var t = new EventTarget();"
       " var count = 0;"
       " t.addEventListener('x', function () { count++; });"
       " t.dispatchEvent(new Event('x'));"
       " return count; })()",
       "1");
    // It is not in the tree, so there is nothing above it: an event dispatched
    // at one must not reach the window's listeners even when it bubbles.
    is("(function () {"
       " var reached = 0;"
       " window.addEventListener('x', function () { reached++; });"
       " new EventTarget().dispatchEvent(new Event('x', { bubbles: true }));"
       " return reached; })()",
       "0");
}

// --- the event handler IDL attributes, HTML 8.1.7.2 -------------------------
//
// `onscroll` is the name used throughout because it is one this engine does not
// dispatch and one `install_element_methods` does not put an own null property
// on the wrapper for - so what is under test is the ACCESSOR and nothing else.
// See install_event_handler_attributes for why the 19 names that file lists are
// still data properties and what has to move for them not to be.

void test_a_handler_attribute_is_null_before_it_is_anything() {
    is("document.createElement('div').onscroll", "null");
    is("document.onscroll", "null");
    is("window.onscroll", "null");
    // PRESENT and null, which is a different thing from absent - a library
    // feature-detects with `'onscroll' in el`.
    is("'onscroll' in document.createElement('div')", "true");
}

void test_a_handler_attribute_stores_only_an_object() {
    // WebIDL's [LegacyTreatNonObjectAsNull]: a function round-trips, and
    // anything that is not an object is null. `el.onscroll = ""` used to read
    // back "" and is twelve of Body-FrameSet-Event-Handlers.html's assertions.
    is("(function () {"
       " var el = document.createElement('div');"
       " function nop() {}"
       " el.onscroll = nop;"
       " return el.onscroll === nop; })()",
       "true");
    is("(function () {"
       " var el = document.createElement('div');"
       " el.onscroll = function () {};"
       " el.onscroll = '';"
       " return el.onscroll; })()",
       "null");
    is("(function () {"
       " var el = document.createElement('div');"
       " el.onscroll = function () {};"
       " el.onscroll = 42;"
       " return el.onscroll; })()",
       "null");
    is("(function () {"
       " var el = document.createElement('div');"
       " el.onscroll = function () {};"
       " el.onscroll = null;"
       " return el.onscroll; })()",
       "null");
}

void test_the_prefixed_and_unprefixed_names_are_not_aliases() {
    // webkit-animation-end-event.html's first test. They are separate handlers
    // for separate event types; setting one must not be visible in the other.
    is("(function () {"
       " var el = document.createElement('div');"
       " el.onanimationend = function () {};"
       " return el.onwebkitanimationend; })()",
       "null");
    is("(function () {"
       " var el = document.createElement('div');"
       " el.onwebkitanimationend = function () {};"
       " return el.onanimationend; })()",
       "null");
}

void test_a_content_attribute_compiles_to_a_function() {
    // The attribute's value is a function BODY with one parameter named `event`
    // - which is what makes `onclick="alert(event.type)"` work at all.
    is("(function () {"
       " var el = document.createElement('div');"
       " el.setAttribute('onscroll', 'return 1');"
       " return typeof el.onscroll; })()",
       "function");
    // ONE function, not one per read: `el.onscroll === el.onscroll`.
    is("(function () {"
       " var el = document.createElement('div');"
       " el.setAttribute('onscroll', 'return 1');"
       " return el.onscroll === el.onscroll; })()",
       "true");
    // ...and it RUNS, with the event as its argument.
    is("(function () {"
       " var el = document.createElement('div');"
       " el.setAttribute('onscroll', 'window.seenType = event.type');"
       " el.dispatchEvent(new Event('scroll'));"
       " return window.seenType; })()",
       "scroll");
    // A changed attribute compiles again, and a removed one reads null.
    is("(function () {"
       " var el = document.createElement('div');"
       " el.setAttribute('onscroll', 'return 1');"
       " var first = el.onscroll;"
       " el.setAttribute('onscroll', 'return 2');"
       " return el.onscroll === first; })()",
       "false");
    is("(function () {"
       " var el = document.createElement('div');"
       " el.setAttribute('onscroll', 'return 1');"
       " el.removeAttribute('onscroll');"
       " return el.onscroll; })()",
       "null");
    // AN ASSIGNMENT DISCARDS THE MARKUP'S HANDLER - HTML's "deactivate an event
    // handler" - so a present null does NOT fall back to the attribute.
    is("(function () {"
       " var el = document.createElement('div');"
       " el.setAttribute('onscroll', 'return 1');"
       " el.onscroll = null;"
       " return el.onscroll; })()",
       "null");
}

void test_a_handler_attribute_is_a_listener() {
    is("(function () {"
       " var el = document.createElement('div');"
       " var count = 0;"
       " el.onscroll = function () { count++; };"
       " el.dispatchEvent(new Event('scroll'));"
       " return count; })()",
       "1");
    // `this` is the object the handler is on, as it is for a listener.
    is("(function () {"
       " var el = document.createElement('div');"
       " var seen = null;"
       " el.onscroll = function () { seen = this; };"
       " el.dispatchEvent(new Event('scroll'));"
       " return seen === el; })()",
       "true");
}

void test_the_four_prefixed_handlers_answer_to_a_name_no_fold_produces() {
    // `onwebkitanimationend` handles `webkitAnimationEnd`, so "on" + type is the
    // wrong property by four capital letters - see handler_property_name.
    is("(function () {"
       " var el = document.createElement('div');"
       " var count = 0;"
       " el.onwebkitanimationend = function () { count++; };"
       " el.dispatchEvent(new Event('webkitAnimationEnd'));"
       " return count; })()",
       "1");
    // ...and the unprefixed handler does NOT hear the prefixed event, which is
    // three of the four assertions webkit-animation-end-event.html makes about
    // the pair.
    is("(function () {"
       " var el = document.createElement('div');"
       " var count = 0;"
       " el.onanimationend = function () { count++; };"
       " el.dispatchEvent(new Event('webkitAnimationEnd'));"
       " return count; })()",
       "0");
}

void test_an_animation_event_can_be_constructed() {
    // The four webkit-*-event.html files synthesise one to ask whether the
    // prefixed and unprefixed handlers are separate; without the constructor
    // the question cannot be put. The interface existing is not a claim that
    // this engine runs animations.
    is("(new AnimationEvent('webkitAnimationEnd')).type", "webkitAnimationEnd");
    is("(new AnimationEvent('animationend') instanceof Event)", "true");
    is("(new AnimationEvent('animationend', { animationName: 'anim', elapsedTime: 2 })"
       ".animationName)",
       "anim");
    is("(new AnimationEvent('animationend')).elapsedTime", "0");
    is("(new TransitionEvent('transitionend', { propertyName: 'width' })).propertyName", "width");
    is("(function () {"
       " var el = document.createElement('div');"
       " var count = 0;"
       " el.onwebkitanimationend = function () { count++; };"
       " el.addEventListener('webkitAnimationEnd', function () { count++; });"
       " el.dispatchEvent(new AnimationEvent('webkitAnimationEnd'));"
       " return count; })()",
       "2");
}

void test_a_synthetic_touch_event_is_not_cancelable() {
    // The interface exists on a machine with no touchscreen, exactly as it does
    // in every desktop browser - and `ontouchstart` deliberately does not, which
    // is the name a capability check is supposed to ask about.
    is("'TouchEvent' in window", "true");
    is("'ontouchstart' in window", "false");
    is("(new TouchEvent('touchstart')).cancelable", "false");
    is("(new TouchEvent('touchstart')).touches.length", "0");
    is("(new TouchEvent('touchstart') instanceof UIEvent)", "true");
    // ...so preventDefault does nothing to one, which is what
    // synthetic-events-cancelable.html asks four times.
    is("(function () {"
       " var seen = null;"
       " window.addEventListener('touchstart', function (e) {"
       "   e.preventDefault(); seen = e.defaultPrevented; });"
       " var returned = window.dispatchEvent(new TouchEvent('touchstart'));"
       " return seen + ',' + returned; })()",
       "false,true");
}

void test_the_window_keeps_both_spellings() {
    // `window` IS the global object in HTML, but the globals table and the
    // window object are separate storage here - so an own accessor on the
    // window would otherwise make the bare form unreachable.
    is("(function () {"
       " window.onscroll = function () { return 7; };"
       " return typeof onscroll; })()",
       "function");
    is("(function () {"
       " var count = 0;"
       " window.onerror = function () { count++; };"
       " var el = document.createElement('div');"
       " el.addEventListener('x', function () { throw new Error('boom'); });"
       " el.dispatchEvent(new Event('x'));"
       " return count; })()",
       "1");
}

void test_a_sheets_onload_attribute_has_run_by_the_windows_load() {
    // css/cssom/HTMLStyleElement-load-event: three `<style onload="++N">`,
    // counted from a `window` load listener. HTML delays the document's load
    // until its subresources have settled, so the count is three and not - as
    // it was with the sheets queued beside the timers - zero.
    browser page{browser_options{400, 300}};
    page.load_html("<!DOCTYPE html><html><head><script>var N = 0;</script>"
                   "<style onload='++N'></style><style onload='++N'>p{}</style>"
                   "<link rel=stylesheet href='data:text/css,*{}' onload='++N'>"
                   "<script>console.log('sync=' + N);"
                   "window.addEventListener('load', function () { console.log('load=' + N); });"
                   "</script></head><body></body></html>");
    (void)page.tick(16.0);
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK_EQ(logged.size(), std::size_t{2});
    if (logged.size() == 2) {
        CHECK_EQ(logged[0], std::string{"sync=0"});
        CHECK_EQ(logged[1], std::string{"load=3"});
    }
}

void test_a_body_handler_is_the_windows() {
    // HTML's window-reflecting body element event handler set, as
    // dom/events/Body-FrameSet-Event-Handlers.html reads it: `body.onload` IS
    // `window.onload`, both ways, and a `<body onload>` attribute is the
    // window's handler - so the load event runs it.
    is("(function () { function f() {} document.body.onresize = f;"
       " return window.onresize === f && document.body.onresize === f; })()",
       "true");
    is("(function () { window.onblur = null; document.body.setAttribute('onblur', 'return 1');"
       " var a = typeof window.onblur === 'function' && window.onblur === document.body.onblur;"
       " document.body.removeAttribute('onblur'); return a && window.onblur === null; })()",
       "true");
    // A DETACHED body's content attribute reaches the window too - the
    // file's "Forward" cases set it on a `createElement("body")`.
    is("(function () { window.onscroll = null; var b = document.createElement('body');"
       " b.setAttribute('onscroll', 'return 2');"
       " return typeof window.onscroll === 'function' && window.onscroll === b.onscroll; })()",
       "true");
    // A frameset made by script forwards too; a div does not.
    is("(function () { var fs = document.createElement('frameset'); function f() {}"
       " fs.onfocus = f; var d = document.createElement('div'); d.onload = f;"
       " return window.onfocus === f && window.onload === null; })()",
       "true");
    browser page{browser_options{400, 300}};
    page.load_html("<!DOCTYPE html><html><head><script>var N = 0;</script></head>"
                   "<body onload='console.log(\"body-load=\" + (++N))'></body></html>");
    (void)page.tick(16.0);
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK_EQ(logged.size(), std::size_t{1});
    if (!logged.empty()) { CHECK_EQ(logged.back(), std::string{"body-load=1"}); }
}

void test_a_hash_change_event_can_be_constructed() {
    // html/dom/historical.html asks HashChangeEvent.prototype a question.
    is("(function () { var e = new HashChangeEvent('hashchange', {newURL: 'a#b'});"
       " return e.oldURL + '|' + e.newURL + '|' + ('initHashChangeEvent' in e); })()",
       "|a#b|false");
}

} // namespace

int main() {
    test_a_throwing_listener_does_not_leave_the_dispatch();
    test_a_throwing_listener_does_not_stop_the_next_one();
    test_the_page_hears_about_it();
    test_a_throwing_onerror_is_contained_too();
    test_an_object_with_handle_event_is_a_listener();
    test_handle_event_is_looked_up_on_every_dispatch();
    test_a_function_is_never_asked_for_handle_event();
    test_a_listener_object_without_a_callable_handle_event_is_a_type_error();
    test_a_throwing_handle_event_getter_reaches_the_page();
    test_the_prototypes_methods_know_a_node_when_they_see_one();
    test_a_standalone_target_is_still_its_own_whole_path();
    test_a_handler_attribute_is_null_before_it_is_anything();
    test_a_handler_attribute_stores_only_an_object();
    test_the_prefixed_and_unprefixed_names_are_not_aliases();
    test_a_content_attribute_compiles_to_a_function();
    test_a_handler_attribute_is_a_listener();
    test_the_four_prefixed_handlers_answer_to_a_name_no_fold_produces();
    test_an_animation_event_can_be_constructed();
    test_a_synthetic_touch_event_is_not_cancelable();
    test_the_window_keeps_both_spellings();
    test_a_sheets_onload_attribute_has_run_by_the_windows_load();
    test_a_body_handler_is_the_windows();
    test_a_hash_change_event_can_be_constructed();
    REPORT("event_handlers");
}
