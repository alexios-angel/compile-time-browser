// The `select` event of a text control (HTML 4.10.19.7 "set the selection
// range"): queued once per CHANGE of the selection, never for a repeat of
// the same range, and never synchronously. Stands in for
// html/semantics/forms/textfieldselection/select-event.html.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

void test_select_fires_once_per_change() {
    browser page{browser_options{400, 300}};
    page.load_html(R"html(<!doctype html><html><body>
        <input id=i type=text value=foobar><textarea id=t>foobar</textarea>
        <script>
        const seen = [];
        for (const el of [document.getElementById('i'), document.getElementById('t')]) {
            el.addEventListener('select', e => seen.push(el.id + ':' + e.type + ',' + e.bubbles +
                                                          ',' + e.isTrusted));
            el.onselect = () => seen.push(el.id + ':handler');
        }
        const i = document.getElementById('i'), t = document.getElementById('t');
        i.select(); i.select();
        t.setSelectionRange(1, 1); t.selectionStart = 1;
        seen.push('sync:' + seen.length);
        window.step2 = () => { i.select(); t.selectionStart = 1; t.setSelectionRange(1, 1);
                              t.selectionDirection = 'backward'; t.selectionDirection = 'backward';
                              t.setRangeText('x', 0, 0, 'select'); t.setRangeText('x', 0, 1, 'select'); };
        window.report = () => console.log('seen=' + seen.join('|'));
        </script></body></html>)html");
    CHECK_EQ(page.script_error(), std::string{});
    for (int i = 0; i < 3; ++i) { (void)page.tick(16.0); }
    (void)page.run_script("report()");
    CHECK_EQ(page.bindings().console_output().back(),
             std::string{"seen=sync:0|i:select,true,true|i:handler|t:select,true,true|t:handler"});
    (void)page.run_script("step2()");
    for (int i = 0; i < 3; ++i) { (void)page.tick(16.0); }
    (void)page.run_script("report()");
    // step2: i.select() repeats (nothing); t: selectionStart=1 repeats, the
    // range (1,1) repeats. Changing the direction and selecting "x" each
    // queue one event; repeating either operation queues nothing.
    CHECK_EQ(page.bindings().console_output().back(),
             std::string{"seen=sync:0|i:select,true,true|i:handler|t:select,true,true|t:handler|"
                         "t:select,true,true|t:handler|t:select,true,true|t:handler"});
}

} // namespace

int main() {
    test_select_fires_once_per_change();
    REPORT("select_event");
}
