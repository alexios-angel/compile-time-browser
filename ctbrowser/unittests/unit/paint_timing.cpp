// PAINT TIMING, AND THE `load` A STYLESHEET OR SCRIPT OWES ITS ELEMENT.
//
// html/dom/render-blocking measures render-blocking through time: it waits for
// the element's `load` (LoadObserver), then polls
// `performance.getEntriesByType('paint')` and asserts every paint entry came
// after that load. Both halves were missing - `performance` was `now` alone,
// and only <img> and <iframe> ever fired `load`. See
// lib/Shell/bindings/performance.cpp and browser/styles.cpp.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "cssom_probe.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser_test::logged;

namespace {

// The two paint entries appear with the first frame and not before, and they
// are PerformancePaintTiming objects a page can feature-detect and filter.
void test_paint_entries_are_recorded_by_the_first_frame() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><p>text</p><script>
        requestAnimationFrame(() => {
            console.log('before=' + performance.getEntriesByType('paint').length + ',' +
                        typeof PerformancePaintTiming + ',' + typeof PerformanceEntry);
            setTimeout(() => {
                const paints = performance.getEntriesByType('paint');
                console.log('after=' + paints.map(e => e.name + '@' + e.entryType + '/' +
                    e.duration + '/' + (e.startTime > 0) + '/' +
                    (e instanceof PerformancePaintTiming) + '/' +
                    (e instanceof PerformanceEntry)).join(';'));
                console.log('lookup=' + performance.getEntries().length + ',' +
                            performance.getEntriesByName('first-paint').length + ',' +
                            performance.getEntriesByName('first-paint', 'mark').length + ',' +
                            Object.keys(paints[0].toJSON()).join('+'));
            }, 0);
        });
    </script></body></html>)");
    (void)page.tick(16.0);
    CHECK_EQ(logged(page, "before="), std::string{"before=0,function,function"});
    page.frame();
    (void)page.tick(16.0);
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "after="), std::string{"after=first-paint@paint/0/true/true/true;"
                                                 "first-contentful-paint@paint/0/true/true/true"});
    CHECK_EQ(logged(page, "lookup="),
             std::string{"lookup=2,1,0,name+entryType+startTime+duration"});
}

// Parser-inserted: `load` at a <link rel=stylesheet>, a <style> and a
// <script src>, `error` where the bytes were not there, nothing at an inline
// classic script - and all of it a tick AFTER the page's script registered the
// listeners, with the sheet applied and the script run - and BEFORE the
// window's own `load`, which HTML delays until the subresources have settled.
void test_parsed_sheets_and_scripts_fire_load_or_error_at_the_element() {
    browser page{browser_options{400, 200}};
    page.assets().add("red.css", bytes_of(".target { color: rgb(255, 0, 0); }"));
    page.assets().add("dummy.js", bytes_of("window.dummy = 1;"));
    page.load_html(R"(<html><head>
        <link id=l rel=stylesheet href=red.css>
        <link id=m rel=stylesheet href=missing.css>
        <style id=s>.other { color: blue; }</style>
        <script id=j src=dummy.js></script>
        <script id=k src=missing.js></script>
        <script id=i>/* inline: no load event */</script>
        <script>
        const seen = [];
        const colour = () => getComputedStyle(document.querySelector('.target')).color;
        for (const id of ['l', 'm', 's', 'j', 'k', 'i']) {
            const el = document.getElementById(id);
            el.addEventListener('load', e => seen.push(id + ':load:' + (e.timeStamp > 0) + ':' +
                                                       (e.target === el) + ':' + e.bubbles));
            el.addEventListener('error', () => seen.push(id + ':error'));
        }
        document.getElementById('j').addEventListener('load', () => seen.push('dummy=' + window.dummy));
        document.getElementById('l').addEventListener('load', () => seen.push(colour()));
        window.addEventListener('load', () => seen.push('window'));
        setTimeout(() => console.log('events=' + seen.join(',')), 0);
        </script></head><body><div class=target>red</div></body></html>)");
    (void)page.tick(16.0);
    CHECK_EQ(logged(page, "events="),
             std::string{"events=l:load:true:true:false,rgb(255, 0, 0),m:error,"
                         "s:load:true:true:false,j:load:true:true:false,dummy=1,k:error,window"});
}

// Script-inserted: a <link> and a <style> appended after load are announced
// once their sheet has applied, which is the restyle the insertion scheduled.
void test_inserted_sheets_fire_load_once_applied() {
    browser page{browser_options{400, 200}};
    page.assets().add("red.css", bytes_of(".target { color: rgb(255, 0, 0); }"));
    page.load_html(R"(<html><head><script>
        const colour = () => getComputedStyle(document.querySelector('.target')).color;
        window.addEventListener('load', () => {
            const link = document.createElement('link');
            link.setAttribute('rel', 'stylesheet');
            link.setAttribute('href', 'red.css');
            link.addEventListener('load', () => console.log('link=' + colour()));
            document.head.appendChild(link);
            const style = document.createElement('style');
            style.textContent = '.target { font-weight: bold; }';
            style.addEventListener('load', () => console.log('style=' + style.sheet.cssRules.length));
            document.head.appendChild(style);
        });
        </script></head><body><div class=target>red</div></body></html>)");
    for (int i = 0; i < 3; ++i) {
        (void)page.tick(16.0);
        page.frame();
    }
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "link="), std::string{"link=rgb(255, 0, 0)"});
    CHECK_EQ(logged(page, "style="), std::string{"style=1"});
}

} // namespace

int main() {
    test_paint_entries_are_recorded_by_the_first_frame();
    test_parsed_sheets_and_scripts_fire_load_or_error_at_the_element();
    test_inserted_sheets_fire_load_once_applied();
    REPORT("paint_timing");
}
