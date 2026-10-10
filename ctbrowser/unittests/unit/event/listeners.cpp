//===- ctbrowser/unittests/unit/event/listeners.cpp - Event listener tests ===//
//
// Listener-invocation rules, one assertion apiece: the default passive value,
// the listener list copied before it runs, a throwing listener not stopping
// the dispatch, `handleEvent` on a listener object, the EventTarget methods as
// bare globals, and `window.event`. The dispatch machinery itself (the path,
// the interfaces, the options) is tested in dispatch.cpp.
//
// Shared helpers are in ../chrome_probe.hpp and test/support/dom_probe.hpp.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

#include "../chrome_probe.hpp"
#include "check.hpp"
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
// Shared probe helpers from test/support/dom_probe.hpp.
using ctbrowser_test::box_of;
using ctbrowser_test::find_id;
using ctbrowser_test::log_of;

namespace {

// --- Listener invocation rules ----------------------------------------------
//
// A golden image cannot see dispatch: a listener that never ran, ran twice, or
// cancelled something it had promised not to leaves exactly the same pixels
// behind. So every case below asks a script what happened and reads the
// answer back out of the console.

/// Runs a snippet that logs exactly one line, and returns that line. Anything
/// else (a script that would not run, a snippet that logged twice) comes back
/// as a string that fails the comparison and says what happened.
[[nodiscard]] std::string one_log(browser & page, const std::string & source) {
    const std::size_t before = log_of(page).size();
    if (!page.run_script(source)) { return "<did not run: " + page.script_error() + ">"; }
    const std::size_t after = log_of(page).size();
    if (after != before + 1) { return "<" + std::to_string(after - before) + " lines logged>"; }
    return log_of(page).back();
}

// The default passive value: https://dom.spec.whatwg.org/#default-passive-value
//
// A `touchstart`, `touchmove`, `wheel` or `mousewheel` listener on the window,
// the document, the document element or the body is passive unless the page
// said `{passive: false}`. Nowhere else, and no other type.
//
// `passive` means the canceled flag is not set while the listener runs, so the
// default decides whether `preventDefault` in these common listeners does
// anything at all (dom/events/passive-by-default.html).
void test_a_scroll_blocking_listener_is_passive_by_default() {
    browser page{browser_options{300, 200}};
    page.load_html("<body><div id=d>x</div></body>");
    page.frame();

    // Register, cancel from inside the listener, dispatch a cancelable event,
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

    // The other side of the same rule, which makes it a default rather than a
    // prohibition.
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

// The listener list is copied before any of it runs.
//
// Two rules follow from the copy: a listener registered during a dispatch is
// not in the copy and does not run, and one removed during it is skipped.
// Walking the live list cannot do the second half, because removal shifts
// every later entry down, so the loop's index then names the next listener and
// that one is silently skipped.
void test_the_listener_list_is_copied_before_it_runs() {
    browser page{browser_options{300, 200}};
    page.load_html("<body><div id=d>x</div></body>");
    page.frame();

    // `first` removes `third` and adds `late`; `second` must still run, `third`
    // must not, and `late` must not run until the next dispatch.
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
    // in registration order, adding a third listener nobody will reach, and
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

void test_native_registry_matches_reentrant_dispatch() {
    using registry = shell::listener_registry<std::string>;
    constexpr shell::listener_target target{shell::event_target_kind::object, 1};
    const auto make_listener = [&](std::uint64_t id, std::string label, bool once = false) {
        return registry::entry{.target = target,
                               .type = "x",
                               .callback_id = id,
                               .once = once,
                               .payload = std::move(label)};
    };
    browser page{browser_options{300, 200}};
    page.load_html("<body><div id=parent><span id=child></span></div></body>");

    registry readded;
    CHECK(readded.add(make_listener(1, "a")));
    CHECK(readded.add(make_listener(2, "b")));
    std::string native_readded;
    for (const auto id : readded.snapshot(target, "x", false)) {
        const auto * current = readded.begin(target, "x", id, false);
        if (!current) { continue; }
        if (!native_readded.empty()) { native_readded += ','; }
        native_readded += current->payload;
        if (id == 1) {
            readded.remove(target, "x", 2, false);
            CHECK(readded.add(make_listener(2, "b")));
        }
    }
    CHECK_EQ(native_readded, "a,b");
    CHECK_EQ(one_log(page, R"((function () {
        var e = new EventTarget(), seen = [];
        function b() { seen.push('b'); }
        function a() {
            seen.push('a'); e.removeEventListener('x', b); e.addEventListener('x', b);
        }
        e.addEventListener('x', a); e.addEventListener('x', b);
        e.dispatchEvent(new Event('x'));
        console.log(seen.join(','));
    })())"),
             native_readded);

    registry nested;
    CHECK(nested.add(make_listener(1, "a", true)));
    CHECK(nested.add(make_listener(2, "b")));
    std::string native_nested;
    for (const auto id : nested.snapshot(target, "x", false)) {
        const auto * current = nested.begin(target, "x", id, false);
        if (!current) { continue; }
        if (!native_nested.empty()) { native_nested += ','; }
        native_nested += current->payload;
        if (id == 1) {
            for (const auto inner_id : nested.snapshot(target, "x", false)) {
                const auto * inner = nested.begin(target, "x", inner_id, false);
                if (inner) { native_nested += ',' + inner->payload; }
            }
        }
    }
    nested.reap_spent();
    CHECK_EQ(native_nested, "a,b,b");
    CHECK_EQ(one_log(page, R"((function () {
        var e = new EventTarget(), seen = [], nested = false;
        function a() {
            seen.push('a');
            if (!nested) { nested = true; e.dispatchEvent(new Event('x')); }
        }
        e.addEventListener('x', a, {once: true});
        e.addEventListener('x', function () { seen.push('b'); });
        e.dispatchEvent(new Event('x'));
        console.log(seen.join(','));
    })())"),
             native_nested);

    CHECK_EQ(one_log(page, R"((function () {
        var parent = document.getElementById('parent');
        var child = document.getElementById('child'), seen = [];
        function bubble() { seen.push('bubble'); }
        parent.addEventListener('phase', function () {
            seen.push('capture'); parent.addEventListener('phase', bubble);
        }, true);
        child.dispatchEvent(new Event('phase', {bubbles: true}));
        console.log(seen.join(','));
    })())"),
             "capture,bubble");
}

// Bootstrap's transition helper filters child events, removes its own
// listener before calling the continuation, and leaves a guarded timer.
void test_transition_listener_removal_before_nested_dispatch() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<body><div id=parent><span id=child></span></div><script>
        (function () {
            var parent = document.getElementById('parent');
            var child = document.getElementById('child');
            var seen = [], complete = false;
            function listener(event) {
                if (event.target !== parent) { return; }
                complete = true;
                parent.removeEventListener('transitionend', listener);
                seen.push('complete');
                parent.dispatchEvent(new Event('transitionend'));
            }
            parent.addEventListener('transitionend', listener);
            setTimeout(function () {
                if (!complete) { parent.dispatchEvent(new Event('transitionend')); }
            }, 5);
            child.dispatchEvent(new Event('transitionend', {bubbles: true}));
            console.log('child=' + seen.join(','));
            parent.dispatchEvent(new Event('transitionend'));
            console.log('parent=' + seen.join(','));
            setTimeout(function () { console.log('later=' + seen.join(',')); }, 10);
        })();
    </script></body>)");
    page.tick(20);
    CHECK(page.script_error().empty());
    CHECK_EQ(log_of(page),
             (std::vector<std::string>{"child=", "parent=complete", "later=complete"}));
}

void test_handler_getter_preserves_selected_passive_flag() {
    browser page{browser_options{300, 200}};
    page.load_html("<body></body>");
    // The getter moves a nonpassive registration into the selected handler's
    // old vector slot. Its passive flag must already have been copied out.
    CHECK_EQ(one_log(page, R"((function () {
        var target = document.body, calls = 0, changed = false;
        function handler(event) { ++calls; event.preventDefault(); return false; }
        var setter = Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'onwheel').set;
        setter.call(target, handler);
        target.addEventListener('unused', function () {}, {passive: false});
        Object.defineProperty(target, 'onwheel', {configurable: true, get: function () {
            if (!changed) {
                changed = true;
                setter.call(target, null);
                setter.call(target, handler);
            }
            return handler;
        }});
        var event = new Event('wheel', {cancelable: true});
        var allowed = target.dispatchEvent(event);
        console.log(calls + ',' + event.defaultPrevented + ',' + allowed);
    })())"),
             "1,false,true");
}

// A listener that throws does not stop the dispatch, and the throw does not
// come back out of `dispatchEvent`: the page is told through an `error` event
// instead (dom/events/Event-dispatch-throwing.html and the last case of
// dom/events/EventTarget-dispatchEvent.html). Getting it wrong stops a page at
// the first faulting handler in a library it did not write.
void test_a_throwing_listener_is_reported_and_the_rest_still_run() {
    browser page{browser_options{300, 200}};
    page.load_html("<body><div id=d>x</div></body>");
    page.frame();

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

    // The `error` event carries the thrown value itself. (`window.onerror`, by
    // contrast, gets the message as a string: HTML's OnErrorEventHandler is
    // (message, filename, lineno, colno, error), as the case above checks.)
    check(one_log(page, R"((function () {
            var t = new EventTarget(), thrown = {name: 'mine'}, seen = null;
            window.addEventListener('error', function (e) { seen = e.error; });
            t.addEventListener('go', function () { throw thrown; });
            t.dispatchEvent(new Event('go'));
            console.log(seen === thrown);
          })())") == "true",
          "the error event carries the value that was thrown, not only its text");
}

// A listener object needs a callable `handleEvent`, and WebIDL says what
// happens when it has not got one.
//
// That `this` is the object and that the property is fetched once per dispatch
// are tested in dispatch.cpp and handlers.cpp; these are the two edges of the same clause: a
// callable listener is never asked for the method at all, and a non-callable
// one is a TypeError rather than a listener that quietly does nothing
// (dom/events/EventListener-handleEvent.html).
void test_a_listener_object_needs_a_callable_handle_event() {
    browser page{browser_options{300, 200}};
    page.load_html("<body><div id=d>x</div></body>");
    page.frame();

    // A function is never asked for one: `handleEvent` on a callable listener
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

    // A non-callable one is a TypeError, reported like any other fault in
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

// The window is the global object, so its three EventTarget methods are also
// bare global names, as the specification's own examples use them.
void test_the_event_target_methods_are_bare_globals() {
    browser page{browser_options{300, 200}};
    page.load_html("<body><div id=d>x</div></body>");
    page.frame();

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

// `window.event` is set for the whole dispatch and restored after it, including
// across a nested dispatch and across a handler that throws out of one. It is
// the oldest event API there is and pages still reach for it.
void test_window_event_is_set_and_restored() {
    browser page{browser_options{300, 200}};
    page.load_html("<body><div id=d>x</div></body>");
    page.frame();

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
    test_native_registry_matches_reentrant_dispatch();
    test_transition_listener_removal_before_nested_dispatch();
    test_handler_getter_preserves_selected_passive_flag();
    test_a_throwing_listener_is_reported_and_the_rest_still_run();
    test_a_listener_object_needs_a_callable_handle_event();
    test_the_event_target_methods_are_bare_globals();
    test_window_event_is_set_and_restored();
    REPORT("event_listeners");
}
