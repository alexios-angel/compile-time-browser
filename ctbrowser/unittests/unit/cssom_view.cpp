// CSSOM VIEW through the bindings: the metrics on an element (§7, §8), the
// scroll APIs (§6) and the document's geometry methods (§5). Each case names
// the css/cssom-view file whose assertion it stands in for.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "cssom_probe.hpp"

#include <string>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser_test::logged;

namespace {

// scrollWidthHeight-negative-margin-002, table-client-props, client-props-root:
// the scrolling area, the padding box and the border edges.
void test_scroll_and_client_metrics() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<!doctype html><html><head><style>
        body { margin: 0 }
        #w { width: 80px; height: 80px; padding: 1px 4px 8px 16px; border-style: solid;
             border-width: 1px 50px 40px 4px; overflow: hidden }
        #i { margin: -100px; height: 300px; width: 300px }
        #tall { height: 900px }
        </style></head><body><div id=w><div id=i></div></div><span id=s>x</span><div id=tall></div>
        <script>
        const w = document.getElementById('w'), s = document.getElementById('s');
        console.log('scroll=' + w.scrollWidth + ',' + w.scrollHeight);
        console.log('client=' + w.clientWidth + ',' + w.clientHeight + ',' + w.clientLeft + ',' +
                    w.clientTop);
        console.log('inline=' + s.clientWidth + ',' + s.clientHeight + ',' + s.clientLeft);
        const root = document.documentElement;
        console.log('root=' + root.clientWidth + ',' + root.clientHeight + ',' + root.scrollWidth +
                    ',' + (root.scrollHeight === document.body.offsetHeight) + ',' +
                    (root.scrollHeight > 1000) + ',' + (document.body.scrollHeight === root.scrollHeight));
        console.log('detached=' + document.createElement('div').scrollWidth);
        </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "scroll="), std::string{"scroll=216,201"});
    CHECK_EQ(logged(page, "client="), std::string{"client=100,89,4,1"});
    CHECK_EQ(logged(page, "inline="), std::string{"inline=0,0,0"});
    // The root's client rectangle is the viewport - less the scrollbar the
    // tall page brought - and its scrolling area is the document's.
    CHECK_EQ(logged(page, "root="), std::string{"root=385,300,385,true,true,true"});
    CHECK_EQ(logged(page, "detached="), std::string{"detached=0"});
}

// offsetTopLeft-border-box, offsetParent-body-and-html, offsetParent-fixed:
// offsetLeft is against the offsetParent's PADDING edge, and who the
// offsetParent is follows §8.
void test_offset_parent_and_offsets() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<!doctype html><html><head><style>
        body { margin: 0 }
        #outer { position: relative; margin: 10px; border: 5px solid; padding: 7px; width: 200px }
        #inner { width: 20px; height: 20px; margin-left: 3px }
        #fixed { position: fixed; top: 0; left: 0 }
        table { border-spacing: 0 }
        </style></head><body>
        <div id=outer><div id=inner></div></div>
        <div id=fixed></div>
        <table><tr><td id=cell><div id=incell></div></td></tr></table>
        <script>
        const inner = document.getElementById('inner'), outer = document.getElementById('outer');
        console.log('parent=' + (inner.offsetParent === outer) + ',' +
                    (outer.offsetParent === document.body) + ',' +
                    document.body.offsetParent + ',' + document.documentElement.offsetParent +
                    ',' + document.getElementById('fixed').offsetParent + ',' +
                    (document.getElementById('incell').offsetParent.id));
        console.log('offset=' + inner.offsetLeft + ',' + inner.offsetTop + ',' + outer.offsetLeft +
                    ',' + document.body.offsetTop);
        </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "parent="), std::string{"parent=true,true,null,null,null,cell"});
    // inner: outer's 7px of padding and its own 3px of margin past outer's
    // padding edge; outer: its 10px margin from the body's padding edge.
    CHECK_EQ(logged(page, "offset="), std::string{"offset=10,7,10,0"});
}

} // namespace

int main() {
    test_scroll_and_client_metrics();
    test_offset_parent_and_offsets();
    REPORT("cssom_view");
}
