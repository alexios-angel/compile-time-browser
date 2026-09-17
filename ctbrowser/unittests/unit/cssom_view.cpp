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

// elementScroll, scrollLeftTop, element-scroll-arguments: a scroll
// container's offset is read and written through scrollTop/scrollLeft and
// the three methods, clamped to its scrolling area; a box with nothing to
// scroll ignores the write, and the `scroll` event arrives on the next tick.
void test_element_scrolling() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<!doctype html><html><head><style>
        body { margin: 0 }
        #section { width: 300px; height: 500px; border: 3px solid gray; overflow: hidden }
        #scrollable { width: 400px; height: 700px }
        </style></head><body><section id=section><div id=scrollable></div><div id=unrelated></div></section>
        <div id=plain><div style="width: 900px; height: 10px"></div></div>
        <script>
        const section = document.getElementById('section');
        const unrelated = document.getElementById('unrelated');
        let events = 0;
        section.addEventListener('scroll', () => ++events);
        console.log('initial=' + section.scrollTop + ',' + section.scrollLeft);
        section.scrollTop = 30; section.scrollLeft = 40;
        console.log('set=' + section.scrollTop + ',' + section.scrollLeft + ',' + unrelated.scrollTop);
        section.scroll(50, 60);
        section.scroll({left: 85});
        section.scrollTo({top: 75});
        section.scrollBy({left: -15, top: 5});
        console.log('methods=' + section.scrollLeft + ',' + section.scrollTop);
        section.scrollTop = 1000; section.scrollLeft = 1000;
        console.log('max=' + section.scrollLeft + ',' + section.scrollTop);
        const plain = document.getElementById('plain');
        plain.scrollLeft = 100;
        console.log('plain=' + plain.scrollLeft + ',' + plain.scrollWidth);
        console.log('promise=' + (section.scrollTo(0, 0) instanceof Promise) + ',' + events);
        section.scrollTo(5).catch(e => console.log('rejected=' + e.name));
        setTimeout(() => console.log('events=' + events), 0);
        </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "initial="), std::string{"initial=0,0"});
    CHECK_EQ(logged(page, "set="), std::string{"set=30,40,0"});
    CHECK_EQ(logged(page, "methods="), std::string{"methods=70,80"});
    // 700 - 500 and 400 - 300: the scrolling area less the padding box.
    CHECK_EQ(logged(page, "max="), std::string{"max=100,200"});
    CHECK_EQ(logged(page, "plain="), std::string{"plain=0,900"});
    CHECK_EQ(logged(page, "promise="), std::string{"promise=true,0"});
    page.frame();
    (void)page.tick(1);
    page.frame();
    CHECK_EQ(logged(page, "rejected="), std::string{"rejected=TypeError"});
    // One event for the whole burst: the scroll steps run once per tick.
    CHECK_EQ(logged(page, "events="), std::string{"events=1"});
}

// scrollingElement, scrolling-quirks-vs-nonquirks, scrollintoview,
// elementFromPoint: the viewport's position through window.scrollTo and the
// root's scrollTop, what getBoundingClientRect and elementFromPoint answer
// once it has moved, and scrollIntoView on the viewport.
void test_viewport_scrolling_and_hit_testing() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<!doctype html><html><head><style>
        body { margin: 0 }
        #a { height: 1000px; background: red }
        #b { height: 50px; background: blue }
        </style></head><body><div id=a></div><div id=b></div>
        <script>
        const b = document.getElementById('b'), a = document.getElementById('a');
        console.log('scrolling=' + (document.scrollingElement === document.documentElement) + ',' +
                    window.scrollY + ',' + pageYOffset + ',' + document.documentElement.scrollTop);
        window.scrollTo(0, 100);
        console.log('scrolled=' + scrollY + ',' + document.documentElement.scrollTop + ',' +
                    b.getBoundingClientRect().top + ',' + document.body.getBoundingClientRect().top);
        document.documentElement.scrollTop = 20;
        scrollBy({top: 30});
        console.log('by=' + scrollY + ',' + scrollX);
        b.scrollIntoView();
        console.log('into=' + scrollY + ',' + b.getBoundingClientRect().top);
        // Off the viewport is null, whatever is there in the document.
        console.log('point=' + document.elementFromPoint(10, 10).id + ',' +
                    document.elementFromPoint(10, 260).id + ',' + document.elementFromPoint(-1, 5) +
                    ',' + document.elementFromPoint(10, 960) + ',' +
                    document.elementsFromPoint(10, 10).map(e => e.tagName || e.id).join('/'));
        b.scrollIntoView(false);
        console.log('end=' + scrollY);
        scrollTo(0, 100000);
        console.log('clamped=' + scrollY);
        </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "scrolling="), std::string{"scrolling=true,0,0,0"});
    CHECK_EQ(logged(page, "scrolled="), std::string{"scrolled=100,100,900,-100"});
    CHECK_EQ(logged(page, "by="), std::string{"by=50,0"});
    CHECK_EQ(logged(page, "point="), std::string{"point=a,b,null,null,DIV/BODY/HTML"});
    // b's top is 1000 in the document; the viewport is 300 tall, the page
    // 1050, so the most it can scroll is 750: "start" is clamped there.
    CHECK_EQ(logged(page, "into="), std::string{"into=750,250"});
    CHECK_EQ(logged(page, "end="), std::string{"end=750"});
    CHECK_EQ(logged(page, "clamped="), std::string{"clamped=750"});
    CHECK_EQ(page.scroll_y(), 750.0f);
}

// scrollingElement.html and HTMLBody-ScrollArea_quirksmode: a quirks-mode
// document scrolls through its body unless the body is potentially
// scrollable, and the root's scrollTop is then dead.
void test_quirks_mode_scrolling_element() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><head><style>body { margin: 0 } #a { height: 1000px }</style></head>
        <body><div id=a></div><script>
        console.log('quirks=' + document.compatMode + ',' + (document.scrollingElement === document.body));
        document.documentElement.scrollTop = 40;
        document.body.scrollTop = 50;
        console.log('body=' + document.body.scrollTop + ',' + document.documentElement.scrollTop +
                    ',' + scrollY + ',' + document.body.scrollHeight + ',' + document.body.clientHeight);
        document.documentElement.style.overflow = 'scroll';
        document.body.style.overflow = 'scroll';
        console.log('potentially=' + document.scrollingElement);
        </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "quirks="), std::string{"quirks=BackCompat,true"});
    CHECK_EQ(logged(page, "body="), std::string{"body=50,0,50,1000,300"});
    CHECK_EQ(logged(page, "potentially="), std::string{"potentially=null"});
}

} // namespace

int main() {
    test_scroll_and_client_metrics();
    test_offset_parent_and_offsets();
    test_element_scrolling();
    test_viewport_scrolling_and_hit_testing();
    test_quirks_mode_scrolling_element();
    REPORT("cssom_view");
}
