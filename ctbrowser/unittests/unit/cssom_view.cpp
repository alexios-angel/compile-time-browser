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

// cssom-getBoxQuads-001, cssom-geometryutils-convertPointFromNode,
// getClientRects, DOMRectList, checkVisibility, scrollParent: the geometry
// interfaces and GeometryUtils, translation only.
void test_geometry_utils() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<!doctype html><html><head><style>
        html, body { margin: 0 }
        #target { position: absolute; left: 20px; top: 25px; width: 260px; height: 180px;
                  margin: 7px 11px 13px 17px; border: solid; border-width: 3px 5px 7px 9px;
                  padding: 2px 4px 6px 8px }
        #source { position: absolute; left: 70px; top: 55px; width: 90px; height: 45px;
                  margin: 10px 14px 18px 22px; border: solid; border-width: 1px 3px 5px 7px;
                  padding: 2px 4px 6px 8px }
        .container { width: 100px; height: 50px }
        .container span { display: block; height: 4px; width: 14px; margin: auto; border: 3px solid }
        #scroller { overflow: auto; height: 50px }
        </style></head><body>
        <div id=target><div id=source></div></div>
        <div class=container><span id=bb></span></div>
        <div id=hidden style="display: none"></div>
        <div id=vh style="visibility: hidden">x</div>
        <div id=scroller><div id=inner style="height: 200px"></div></div>
        <script>
        const target = document.getElementById('target'), source = document.getElementById('source');
        const r = new DOMRect(1, 2, 3, 4), p = new DOMPoint(5, 6);
        console.log('rect=' + r.right + ',' + r.bottom + ',' + JSON.stringify(r) + ',' +
                    (r instanceof DOMRectReadOnly) + ',' + p.w + ',' + DOMQuad.fromRect(r).p3.x);
        const q = source.getBoxQuads()[0];
        console.log('quads=' + q.p1.x + ',' + q.p1.y + ',' + q.getBounds().width + ',' +
                    source.getBoxQuads({box: 'margin', relativeTo: target})[0].p1.x + ',' +
                    (q instanceof DOMQuad) + ',' + document.getElementById('hidden').getBoxQuads().length);
        const bb = document.getElementById('bb');
        console.log('auto=' + bb.getBoxQuads({box: 'border'})[0].getBounds().width + ',' +
                    bb.getBoxQuads({box: 'margin'})[0].getBounds().width);
        const pt = target.convertPointFromNode({x: 0, y: 0}, source, {fromBox: 'content', toBox: 'padding'});
        const back = source.convertPointFromNode(target.convertPointFromNode({x: 10, y: 20}, source), target);
        console.log('convert=' + pt.x + ',' + pt.y + ',' + back.x + ',' + back.y + ',' +
                    document.convertPointFromNode({x: 0, y: 0}, source).x);
        let caught = '';
        try { target.convertPointFromNode({x: 0, y: 0}, document.getElementById('hidden')); } catch (e) { caught = e.name; }
        const rects = source.getClientRects();
        console.log('rects=' + caught + ',' + rects.length + ',' + rects.item(0).left + ',' + rects.item(5) +
                    ',' + rects[0].height);
        console.log('visible=' + document.getElementById('hidden').checkVisibility() + ',' +
                    document.getElementById('vh').checkVisibility() + ',' +
                    document.getElementById('vh').checkVisibility({checkVisibilityCSS: true}) + ',' +
                    source.checkVisibility());
        console.log('scrollParent=' + (document.getElementById('inner').scrollParent.id) + ',' +
                    (source.scrollParent === document.documentElement) + ',' +
                    document.body.scrollParent + ',' + screenX + ',' + window.screenTop);
        </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "rect="),
             std::string{"rect=4,6,{\"x\":1,\"y\":2,\"width\":3,\"height\":4,\"top\":2,"
                         "\"right\":4,\"bottom\":6,\"left\":1},true,1,4"});
    // source's border box: target's padding edge (20+17+9, 25+7+3) plus
    // left/top 70/55 plus its own margin 22/10 = 138, 100; width 90+8+4+7+3.
    // Its margin box against target's border edge (37, 32): 138-22-37.
    CHECK_EQ(logged(page, "quads="), std::string{"quads=138,100,112,79,true,0"});
    CHECK_EQ(logged(page, "auto="), std::string{"auto=20,100"});
    // content origin of source (138+7+8, 100+1+2) against target's padding
    // origin (37+9, 32+3): 107, 68.
    CHECK_EQ(logged(page, "convert="), std::string{"convert=107,68,10,20,138"});
    CHECK_EQ(logged(page, "rects="), std::string{"rects=NotFoundError,1,138,null,59"});
    CHECK_EQ(logged(page, "visible="), std::string{"visible=false,true,false,true"});
    CHECK_EQ(logged(page, "scrollParent="), std::string{"scrollParent=scroller,true,null,0,0"});
}

} // namespace

int main() {
    test_scroll_and_client_metrics();
    test_offset_parent_and_offsets();
    test_element_scrolling();
    test_viewport_scrolling_and_hit_testing();
    test_quirks_mode_scrolling_element();
    test_geometry_utils();
    REPORT("cssom_view");
}
