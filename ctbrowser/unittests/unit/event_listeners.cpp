// THE LISTENER-INVOCATION RULES, one assertion apiece: the default passive
// value, the listener list copied before it runs, a throwing listener not
// stopping the dispatch, `handleEvent` on a listener object, the EventTarget
// methods as bare globals, and `window.event`. The dispatch machinery itself -
// the path, the interfaces, the options - is event_dispatch.cpp.
//
// One of five files carved out of unittests/unit/chrome_basics.cpp on
// 2026-09-07, when it had reached 2,299 lines. Every case is verbatim and in
// the order it had; the helpers more than one of the five needs are in
// chrome_probe.hpp beside this.

#include <ctbrowser.hpp>

#include "check.hpp"
#include "chrome_probe.hpp"
#include "dom_probe.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;
// Shared with widgets_basics and bootstrap_layout - test/support/dom_probe.hpp.
using ctbrowser_test::box_of;
using ctbrowser_test::check;
using ctbrowser_test::find_id;
using ctbrowser_test::log_of;

namespace {

// --- the event batch --------------------------------------------------------
//
// DISPATCH IS THE ONE THING HERE A GOLDEN CANNOT SEE. A listener that never
// ran, ran twice, or cancelled something it had promised not to leaves exactly
// the same pixels behind, so every case below asks a script what happened and
// reads the answer back out of the console.
//
// They are together because they are one subject and because each is two or
// three lines: dom/events is the WPT suite this engine is closest to finishing,
// and these are the rules it was getting wrong, one assertion apiece.

// Run a snippet that logs exactly one line, and return that line. Anything else
// - a script that would not run, a snippet that logged twice - comes back as a
// string that fails the comparison AND says what happened, which is worth more
// than an index into the log at the point of failure.
[[nodiscard]] std::string one_log(browser & page, const std::string & source) {
    const std::size_t before = log_of(page).size();
    if (!page.run_script(source)) { return "<did not run: " + page.script_error() + ">"; }
    const std::size_t after = log_of(page).size();
    if (after != before + 1) { return "<" + std::to_string(after - before) + " lines logged>"; }
    return log_of(page).back();
}

// THE DEFAULT PASSIVE VALUE - https://dom.spec.whatwg.org/#default-passive-value
//
// One sentence: a `touchstart`, `touchmove`, `wheel` or `mousewheel` listener on
// the WINDOW, the DOCUMENT, the DOCUMENT ELEMENT or the BODY is passive unless
// the page said `{passive: false}`. Nowhere else, and no other type.
//
// It reads like a footnote and it is not: `passive` means the canceled flag is
// not set while the listener runs, so the default decides whether
// `preventDefault` in the commonest listener on the web does anything at all.
// 32 subtests of dom/events/passive-by-default.html are this rule and only this
// rule.
void test_a_scroll_blocking_listener_is_passive_by_default() {
    browser page{browser_options{300, 200}};
    page.load_html("<body><div id=d>x</div></body>");
    check(page.frame().has_value(), "the page renders");

    // Register, cancel from inside the listener, dispatch a CANCELABLE event,
    // and report both halves of the answer: whether the flag took, and what
    // dispatchEvent said. The two must agree, which is why both are logged.
    const auto cancels = [&page](const std::string & target, const std::string & type,
                                 const std::string & options) {
        return one_log(page, "(function () { var t = " + target +
                                 "; var f = function (e) { e.preventDefault(); };" + //
                                 "t.addEventListener('" + type + "', f" + options + ");" +
                                 "var e = new Event('" + type + "', {cancelable: true});" +
                                 "var live = t.dispatchEvent(e);" +            //
                                 "t.removeEventListener('" + type + "', f);" + //
                                 "console.log(e.defaultPrevented + ',' + live); })()");
    };

    check(cancels("window", "wheel", "") == "false,true",
          "a wheel listener on the window is passive by default");
    check(cancels("document", "wheel", "") == "false,true", "and on the document");
    check(cancels("document.documentElement", "touchstart", "") == "false,true",
          "and on the document element");
    check(cancels("document.body", "touchmove", "") == "false,true", "and on the body");
    check(cancels("window", "mousewheel", "") == "false,true",
          "mousewheel is in the set as well as wheel");

    // THE OTHER SIDE OF THE SAME RULE, which is the half that makes it a
    // default rather than a prohibition.
    check(cancels("window", "wheel", ", {passive: false}") == "true,false",
          "a page that asks for {passive: false} can still cancel");
    check(cancels("window", "wheel", ", {passive: undefined}") == "false,true",
          "an undefined member is absent, so the default still applies");
    check(cancels("document.getElementById('d')", "wheel", "") == "true,false",
          "an ordinary element is NOT one of the four targets");
    check(cancels("window", "click", "") == "true,false", "and click is not one of the four types");
    check(cancels("new EventTarget()", "wheel", "") == "true,false",
          "a standalone EventTarget is in no document, so nothing scrolls for it");
}

// THE LISTENER LIST IS COPIED BEFORE ANY OF IT RUNS.
//
// Two rules fall out of the copy rather than being written anywhere: one
// REGISTERED during a dispatch is not in the copy and does not run, and one
// REMOVED during it is skipped. Walking the live list cannot do the second
// half, because removal here MOVES every later entry - so the loop's index then
// names the next listener and that one is silently skipped.
void test_the_listener_list_is_copied_before_it_runs() {
    browser page{browser_options{300, 200}};
    page.load_html("<body><div id=d>x</div></body>");
    check(page.frame().has_value(), "the page renders");

    // `first` removes `third` and adds `late`; `second` must still run, `third`
    // must not, and `late` must not run until the NEXT dispatch.
    check(one_log(page, R"((function () {
            var t = new EventTarget(), ran = [];
            var second = function () { ran.push('second'); };
            var third = function () { ran.push('third'); };
            var late = function () { ran.push('late'); };
            var first = function () {
                ran.push('first');
                t.removeEventListener('go', third);
                t.addEventListener('go', late);
            };
            t.addEventListener('go', first);
            t.addEventListener('go', second);
            t.addEventListener('go', third);
            t.dispatchEvent(new Event('go'));
            console.log(ran.join(','));
          })())") == "first,second",
          "a listener removed mid-dispatch is skipped and the one after it is not");

    // "outer,outer,late", and reading the sequence out is the only way to see
    // that it is right. The first dispatch runs `outer` alone - the listener it
    // adds is not in the copy that dispatch is walking - and adds one `late`.
    // The second walks [outer, late]: `outer` runs first because listeners fire
    // in registration order, adding a THIRD listener nobody will reach, and
    // then the `late` registered by the first dispatch runs.
    check(one_log(page, R"((function () {
            var t = new EventTarget(), ran = [];
            t.addEventListener('go', function () {
                ran.push('outer');
                t.addEventListener('go', function () { ran.push('late'); });
            });
            t.dispatchEvent(new Event('go'));
            t.dispatchEvent(new Event('go'));
            console.log(ran.join(','));
          })())") == "outer,outer,late",
          "a listener added mid-dispatch runs on the NEXT event, not on that one");

    // `{once: true}` is the same question asked from the other side: the
    // listener is spent the moment it is entered, so a nested dispatch of the
    // same type does not re-enter it.
    check(one_log(page, R"((function () {
            var t = new EventTarget(), calls = 0;
            t.addEventListener('go', function () { ++calls; }, {once: true});
            t.dispatchEvent(new Event('go'));
            t.dispatchEvent(new Event('go'));
            console.log(calls);
          })())") == "1",
          "a {once: true} listener fires exactly once");
}

// A LISTENER THAT THROWS DOES NOT STOP THE DISPATCH, and the throw does not
// come back out of `dispatchEvent`: the page is told through an `error` event
// instead. dom/events/Event-dispatch-throwing.html and the last case of
// EventTarget-dispatchEvent.html are this, and getting it wrong stops a page
// dead at the first faulting handler in a library it did not write.
void test_a_throwing_listener_is_reported_and_the_rest_still_run() {
    browser page{browser_options{300, 200}};
    page.load_html("<body><div id=d>x</div></body>");
    check(page.frame().has_value(), "the page renders");

    check(one_log(page, R"((function () {
            var t = new EventTarget(), ran = [], errors = 0;
            window.onerror = function (m) { errors += (typeof m === 'string') ? 1 : 100; };
            t.addEventListener('go', function () { ran.push('first'); throw new Error('boom'); });
            t.addEventListener('go', function () { ran.push('second'); });
            var live = t.dispatchEvent(new Event('go'));
            window.onerror = null;
            console.log(ran.join(',') + ';' + live + ';' + errors);
          })())") == "first,second;true;1",
          "the second listener runs, dispatchEvent returns true, and onerror is told once");

    // AND `window.onerror` GETS THE MESSAGE AS A STRING, not the event: HTML's
    // OnErrorEventHandler is (message, filename, lineno, colno, error) and
    // twenty years of shipped code reads the first argument as text.
    check(one_log(page, R"((function () {
            var t = new EventTarget(), thrown = {name: 'mine'}, seen = null;
            window.addEventListener('error', function (e) { seen = e.error; });
            t.addEventListener('go', function () { throw thrown; });
            t.dispatchEvent(new Event('go'));
            console.log(seen === thrown);
          })())") == "true",
          "the error event carries the value that was thrown, not only its text");
}

// A LISTENER OBJECT NEEDS A CALLABLE `handleEvent`, and WebIDL says what
// happens when it has not got one.
//
// That `this` is the object and that the property is fetched once per dispatch
// are event_dispatch's cases; these are the two edges of the same clause it does
// not cover - a callable listener is never asked for the method at all, and a
// non-callable one is a TypeError rather than a listener that quietly does
// nothing. dom/events/EventListener-handleEvent.html is four tests about them.
void test_a_listener_object_needs_a_callable_handle_event() {
    browser page{browser_options{300, 200}};
    page.load_html("<body><div id=d>x</div></body>");
    check(page.frame().has_value(), "the page renders");

    // A FUNCTION IS NEVER ASKED FOR ONE: `handleEvent` on a callable listener
    // is an ordinary property and nothing looks at it.
    check(one_log(page, R"((function () {
            var t = new EventTarget(), calls = 0, wrong = 0;
            var f = function () { ++calls; };
            f.handleEvent = function () { ++wrong; };
            t.addEventListener('go', f);
            t.dispatchEvent(new Event('go'));
            console.log(calls + ',' + wrong);
          })())") == "1,0",
          "a callable listener is called, and its own handleEvent is left alone");

    // AND A NON-CALLABLE ONE IS A TypeError - reported, like any other fault in
    // a listener, rather than a listener that quietly does nothing.
    check(one_log(page, R"((function () {
            var t = new EventTarget(), seen = '';
            window.addEventListener('error', function (e) { seen = e.error && e.error.name; });
            t.addEventListener('go', {handleEvent: 42});
            t.dispatchEvent(new Event('go'));
            console.log(seen);
          })())") == "TypeError",
          "a listener object whose handleEvent is not callable reports a TypeError");
}

// THE WINDOW IS THE GLOBAL OBJECT, so its three EventTarget methods are three
// bare names. Two of them were globals and `dispatchEvent` was not, so a page
// written the way the specification's own examples are written registered a
// listener and then threw on the very next line.
void test_the_event_target_methods_are_bare_globals() {
    browser page{browser_options{300, 200}};
    page.load_html("<body><div id=d>x</div></body>");
    check(page.frame().has_value(), "the page renders");

    check(one_log(page, R"((function () {
            var seen = 0;
            var f = function () { ++seen; };
            addEventListener('ping', f);
            dispatchEvent(new Event('ping'));
            removeEventListener('ping', f);
            dispatchEvent(new Event('ping'));
            console.log(seen);
          })())") == "1",
          "addEventListener, dispatchEvent and removeEventListener all work unqualified");

    check(one_log(page, "console.log(dispatchEvent === window.dispatchEvent)") == "true",
          "and the bare name is the same function as window.dispatchEvent");
}

// `window.event` IS SET FOR THE WHOLE DISPATCH AND RESTORED AFTER IT, including
// across a nested dispatch and across a handler that throws out of one. It is
// the oldest event API there is and pages still reach for it.
void test_window_event_is_set_and_restored() {
    browser page{browser_options{300, 200}};
    page.load_html("<body><div id=d>x</div></body>");
    check(page.frame().has_value(), "the page renders");

    check(one_log(page, "console.log('event' in window)") == "true",
          "window.event EXISTS outside a dispatch, which is not the same as being undefined");
    check(one_log(page, "console.log(window.event === undefined)") == "true",
          "and it is undefined there");

    check(one_log(page, R"((function () {
            var outer = new EventTarget(), inner = new EventTarget(), seen = [];
            inner.addEventListener('in', function () { seen.push(window.event.type); });
            outer.addEventListener('out', function () {
                seen.push(window.event.type);
                inner.dispatchEvent(new Event('in'));
                seen.push(window.event.type);
            });
            outer.dispatchEvent(new Event('out'));
            seen.push(String(window.event));
            console.log(seen.join(','));
          })())") == "out,in,out,undefined",
          "a nested dispatch restores the outer event when it returns");

    check(one_log(page, R"((function () {
            var t = new EventTarget(), seen = '', calls = 0;
            window.onerror = function () { if (++calls === 1) { throw new Error('again'); } };
            t.addEventListener('outer', function () {
                window.dispatchEvent(new ErrorEvent('error', {error: new Error('boom')}));
                seen = window.event.type;
            });
            t.dispatchEvent(new Event('outer'));
            window.onerror = null;
            console.log(seen);
          })())") == "outer",
          "and a throwing onerror in the middle of one still restores it");
}

} // namespace

int main() {
    test_a_scroll_blocking_listener_is_passive_by_default();
    test_the_listener_list_is_copied_before_it_runs();
    test_a_throwing_listener_is_reported_and_the_rest_still_run();
    test_a_listener_object_needs_a_callable_handle_event();
    test_the_event_target_methods_are_bare_globals();
    test_window_event_is_set_and_restored();
    REPORT("event_listeners");
}
