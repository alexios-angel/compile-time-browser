// `matchMedia` and MediaQueryList, CSSOM View §4.2: a list is an EventTarget
// whose `matches` is live, the page's window resize and a frame's box change
// each report a `change` MediaQueryListEvent, and `addListener` is
// `addEventListener("change")`. Each case names the css/cssom-view file it
// stands in for.

#include <ctbrowser.hpp>

#include "check.hpp"
#include "cssom_probe.hpp"

#include <string>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser_test::logged;

namespace {

// matchMedia.html, MediaQueryListEvent.html: the interface, the
// serialisation ("::" is "not all", "all and" drops), the event constructor.
void test_interface_and_serialisation() {
    browser page{browser_options{400, 300}};
    page.load_html(R"html(<!doctype html><html><body><script>
        const all = matchMedia("all"), empty = window.matchMedia(""), bad = matchMedia("::");
        console.log('all=' + all.media + ',' + all.matches + ',' + (all instanceof MediaQueryList) +
                    ',' + (all instanceof EventTarget) + ',' + typeof all.addListener);
        console.log('empty=' + JSON.stringify(empty.media) + ',' + empty.matches);
        console.log('bad=' + bad.media + ',' + bad.matches);
        console.log('list=' + matchMedia("(max-width: 199px), all and (min-width: 200px)").media);
        const e = new MediaQueryListEvent("change", {media: "test", matches: true});
        const d = new MediaQueryListEvent("x");
        console.log('event=' + e.type + ',' + e.media + ',' + e.matches + ',' + e.bubbles + '|' +
                    d.media + ',' + d.matches + ',' + (e instanceof Event));
        let threw = 'no'; try { new MediaQueryList(); } catch (x) { threw = x.name; }
        console.log('ctor=' + threw);
        </script></body></html>)html");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "all="), std::string{"all=all,true,true,true,function"});
    CHECK_EQ(logged(page, "empty="), std::string{"empty=\"\",true"});
    CHECK_EQ(logged(page, "bad="), std::string{"bad=not all,false"});
    CHECK_EQ(logged(page, "list="), std::string{"list=(max-width: 199px), (min-width: 200px)"});
    CHECK_EQ(logged(page, "event="), std::string{"event=change,test,true,false|,false,true"});
    CHECK_EQ(logged(page, "ctor="), std::string{"ctor=TypeError"});
}

// MediaQueryList-change-event-matches-value, -extends-EventTarget,
// -addListener-removeListener: a resize across the breakpoint fires `change`
// once, at every kind of listener, with the list's new answer; `matches` is
// live before the event; a removed listener does not hear it.
void test_change_events_on_the_page() {
    browser page{browser_options{400, 300}};
    page.load_html(R"html(<!doctype html><html><body><script>
        const mql = matchMedia("(max-width: 300px)");
        let heard = [];
        mql.onchange = e => heard.push('handler:' + e.matches + ',' + e.media + ',' + e.type +
                                       ',' + (e instanceof MediaQueryListEvent) + ',' +
                                       (e.target === mql) + ',' + e.isTrusted);
        mql.addEventListener("change", () => heard.push('listener'));
        const gone = () => heard.push('gone');
        mql.addListener(gone);
        mql.removeEventListener("change", gone);
        const other = matchMedia("(min-width: 100px)");
        other.addListener(() => heard.push('other'));
        console.log('before=' + mql.matches);
        window.report = () => console.log('after=' + mql.matches + '|' + heard.join(';'));
        </script></body></html>)html");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "before="), std::string{"before=false"});
    page.resize(250, 300);
    // `matches` is live the moment the environment moved; the event is a
    // tick later, and `other` never flipped.
    (void)page.run_script("report()");
    (void)page.tick(1);
    (void)page.run_script("report()");
    CHECK_EQ(logged(page, "after="), std::string{"after=true|"});
    CHECK_EQ(
        page.bindings().console_output().back(),
        std::string{"after=true|handler:true,(max-width: 300px),change,true,true,true;listener"});
}

// matchMedia.html's iframe cases and MediaQueryList-extends-EventTarget: a
// frame's list reads the FRAME's viewport, and the frame's box changing
// reports the change.
void test_a_frame_has_its_own_lists() {
    browser page{browser_options{400, 300}};
    page.load_html(R"html(<!doctype html><html><body>
        <iframe id=f srcdoc="" width=200 height=100 style="border:none"></iframe>
        <script>
        const f = document.getElementById('f');
        f.addEventListener('load', () => {
            f.contentDocument.body.offsetWidth;
            const w = f.contentWindow;
            const mql = w.matchMedia("(max-width: 200px)");
            console.log('frame=' + mql.matches + ',' + w.matchMedia("(max-height: 50px)").matches +
                        ',' + w.matchMedia("(min-width: 150px)").matches + ',' +
                        matchMedia("(max-width: 200px)").matches + ',' +
                        (mql instanceof w.MediaQueryList));
            mql.addEventListener('change', e => console.log('changed=' + e.matches + ',' +
                (e instanceof w.MediaQueryListEvent) + ',' + mql.matches));
            f.width = "250";
        });
        </script></body></html>)html");
    for (int i = 0; i < 4; ++i) { (void)page.tick(16.0); }
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "frame="), std::string{"frame=true,false,true,false,true"});
    CHECK_EQ(logged(page, "changed="), std::string{"changed=false,true,false"});
}

} // namespace

int main() {
    test_interface_and_serialisation();
    test_change_events_on_the_page();
    test_a_frame_has_its_own_lists();
    REPORT("media_query_list");
}
