// Unhandled promise rejections, HTML 8.1.7.x: `unhandledrejection` at the
// window for a promise nobody handled by the time the notification task
// runs, `rejectionhandled` for one handled after that, and the
// PromiseRejectionEvent interface. Stands in for html/webappapis/scripting/
// processing-model-2/unhandled-promise-rejections/promise-rejection-events.html.

#include <ctbrowser.hpp>

#include "check.hpp"
#include "cssom_probe.hpp"

#include <string>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser_test::logged;

namespace {

void test_events() {
    browser page{browser_options{400, 300}};
    page.load_html(R"html(<!doctype html><html><body><script>
        const seen = [];
        const e1 = new Error('one'), e2 = new Error('two'), e3 = new Error('three');
        let p1, p2, p3;
        addEventListener('unhandledrejection', ev => {
            seen.push('unhandled:' + (ev.promise === p1 ? 'p1' : ev.promise === p2 ? 'p2' : ev.promise === p3 ? 'p3' : '?') +
                      ',' + ev.reason.message + ',' + ev.cancelable + ',' + (ev instanceof PromiseRejectionEvent) +
                      ',' + ev.isTrusted);
            if (ev.promise === p1) { ev.preventDefault(); }
            // Handled AFTER the notification: rejectionhandled follows. (A
            // handler attached during the event itself would mean neither.)
            if (ev.promise === p3) { setTimeout(() => p3.catch(() => {}), 0); }
        });
        onrejectionhandled = ev => seen.push('handled:' + (ev.promise === p3 ? 'p3' : '?') + ',' + ev.cancelable);
        p1 = Promise.reject(e1);
        // Handled one microtask later: never reported.
        p2 = Promise.reject(e2);
        Promise.resolve().then(() => p2.catch(() => {}));
        p3 = new Promise((_, reject) => setTimeout(() => reject(e3), 1));
        // Handled before it rejects: never tracked at all.
        const p4 = Promise.reject(e1); p4.catch(() => {});
        let threw = 'no'; try { new PromiseRejectionEvent('x'); } catch (x) { threw = x.name; }
        const made = new PromiseRejectionEvent('x', {promise: p4, reason: 7});
        seen.push('ctor:' + threw + ',' + (made.promise === p4) + ',' + made.reason);
        window.report = () => console.log('seen=' + seen.join('|'));
        </script></body></html>)html");
    CHECK_EQ(page.script_error(), std::string{});
    for (int i = 0; i < 6; ++i) { (void)page.tick(16.0); }
    (void)page.run_script("report()");
    CHECK_EQ(page.bindings().console_output().back(),
             std::string{"seen=ctor:TypeError,true,7|unhandled:p1,one,true,true,true|"
                         "unhandled:p3,three,true,true,true|handled:p3,false"});
}

} // namespace

int main() {
    test_events();
    REPORT("promise_rejection_events");
}
