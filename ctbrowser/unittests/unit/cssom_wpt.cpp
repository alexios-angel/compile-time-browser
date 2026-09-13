// WHAT css/cssom ASKS THE OBJECT MODEL, one case per behaviour that moved.
//
// The third CSSOM file beside cssom_rules.cpp and cssom_sheets.cpp: every case
// here is a string or a boolean a `css/cssom` test compares, and each one names
// the file it stands in for. `logged` is shared through cssom_probe.hpp.

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

// CSS-namespace-object-class-string, CSSStyleDeclaration-iterator, escape,
// cssom-fontfacerule-constructors: the class strings, the iterator and the
// namespace object's own tag and arities.
void test_class_strings_and_iterators() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>.a { color: red; width: 1px }</style></head><body><script>
        const rule = document.styleSheets[0].cssRules[0];
        console.log('tag=' + Object.prototype.toString.call(rule) + ',' +
                    Object.prototype.toString.call(rule.style) + ',' + CSS.toString());
        console.log('iter=' + (Symbol.iterator in CSSStyleDeclaration.prototype) + ',' +
                    rule.style[Symbol.iterator]().next().value + ',' +
                    (Symbol.iterator in CSSRuleList.prototype));
        let caught = '';
        try { CSS.escape(); } catch (e) { caught = e.name; }
        console.log('escape=' + CSS.escape.length + ',' + caught + ',' +
                    CSS.hasOwnProperty(Symbol.toStringTag));
    </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "tag="),
             std::string{"tag=[object CSSStyleRule],[object CSSStyleProperties],[object CSS]"});
    CHECK_EQ(logged(page, "iter="), std::string{"iter=true,color,true"});
    CHECK_EQ(logged(page, "escape="), std::string{"escape=1,TypeError,true"});
}

// cssom-pagerule "after rule was removed", insertRule-charset-no-index,
// CSSKeyframesRule, CSSContainerRule: the rule side.
void test_removed_rules_charset_keyframes_and_container() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>@charset "UTF-8";
        @keyframes k { 0% { top: 0px; } 100% { top: 200px; } }
        @page { margin-top: 1px }</style></head><body><script>
        const sheet = document.styleSheets[0];
        console.log('charset=' + sheet.cssRules.length + ',' + sheet.cssRules[0].name);
        let caught = '';
        try { sheet.insertRule('@charset "x";'); } catch (e) { caught = e.name; }
        console.log('charsetInsert=' + caught);
        const k = sheet.cssRules[0];
        console.log('indexed=' + k[0].cssText + '|' + k.length);
        k.name = 'none';
        console.log('named=' + k.cssText.replace(/\s/g, ''));
        const p = sheet.cssRules[1];
        sheet.deleteRule(1);
        p.selectorText = 'named';
        console.log('detached=' + p.parentStyleSheet + ',' + p.cssText);
        sheet.insertRule('@container side (min-width: 100px) {}', 0);
        const c = sheet.cssRules[0];
        console.log('container=' + (c instanceof CSSContainerRule) + ',' + c.containerName + ',' +
                    c.containerQuery + ',' + c.conditionText);
    </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    // `@charset` is consumed before the rule list: rule zero is the keyframes.
    CHECK_EQ(logged(page, "charset="), std::string{"charset=2,k"});
    CHECK_EQ(logged(page, "charsetInsert="), std::string{"charsetInsert=SyntaxError"});
    CHECK_EQ(logged(page, "indexed="), std::string{"indexed=0% { top: 0px; }|2"});
    CHECK_EQ(logged(page, "named="),
             std::string{"named=@keyframes\"none\"{0%{top:0px;}100%{top:200px;}}"});
    CHECK_EQ(logged(page, "detached="),
             std::string{"detached=null,@page named { margin-top: 1px; }"});
    CHECK_EQ(logged(page, "container="),
             std::string{"container=true,side,(min-width: 100px),side (min-width: 100px)"});
}

// rule-restrictions, page-descriptors, cssstyledeclaration-cssfontrule,
// cssstyledeclaration-csstext-important, setproperty-null-undefined: the
// three declaration blocks and what each takes.
void test_declaration_blocks() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        @page { margin-top: 10px; transform: scale(1) }
        @font-face { src: url(x.ttf) }
        @keyframes k { from { top: 0px; animation-name: k } }
        .a { color: red; color: blue; padding: 1px !important; padding: 2px }
        </style></head><body><script>
        const rules = document.styleSheets[0].cssRules;
        const pg = rules[0].style;
        pg.setProperty('transform', 'scale(1)');
        pg.cssText = 'margin-bottom: 1px; transform: scale(1)';
        console.log('page=' + pg.length + ',' + pg.cssFloat + ',' + pg.marginBottom + ',' +
                    Object.prototype.toString.call(pg));
        const ff = rules[1].style;
        console.log('font=' + ff.src.includes('x.ttf') + ',' + ('unicodeRange' in ff) + ',' + ff.color + ',' +
                    Object.prototype.toString.call(ff));
        const kf = rules[2].cssRules[0].style;
        kf.setProperty('animation-name', 'k');
        console.log('keyframe=' + kf.length + ',' + kf.top);
        const st = rules[3].style;
        console.log('dup=' + st.cssText);
        st.setProperty('color', null);
        console.log('nulled=' + st.color + '|' + st.length);
        st.setProperty('color', 'green', undefined);
        st.setProperty('width', 'undefined');
        console.log('undef=' + st.color + '|' + st.width);
        // cssstyledeclaration-csstext: a name that is not a supported property
        // has no IDL setter - the write is an ordinary property, never a declaration.
        const el = document.body.style;
        el.COLOR = 'red'; el.unknown = 'unknown'; el.color = 'red'; el.fontSize = '10pt';
        console.log('expando=' + el.cssText + '|' + el.unknown + '|' + el.COLOR + '|' + el.length);
    </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    // `transform` is not a page-context property; `cssFloat` is not a page
    // descriptor - the block is a CSSPageDescriptors, not a CSSStyleProperties.
    CHECK_EQ(logged(page, "page="),
             std::string{"page=1,undefined,1px,[object CSSPageDescriptors]"});
    CHECK_EQ(logged(page, "font="),
             std::string{"font=true,true,undefined,[object CSSFontFaceDescriptors]"});
    CHECK_EQ(logged(page, "keyframe="), std::string{"keyframe=1,0px"});
    // The later declaration wins, unless the earlier was important.
    CHECK_EQ(logged(page, "dup="), std::string{"dup=color: blue; padding: 1px !important;"});
    CHECK_EQ(logged(page, "nulled="), std::string{"nulled=|4"});
    CHECK_EQ(logged(page, "undef="), std::string{"undef=green|"});
    CHECK_EQ(logged(page, "expando="),
             std::string{"expando=color: red; font-size: 10pt;|unknown|red|2"});
}

// style-sheet-interfaces-001, stylesheet-same-origin: `sheet` on the
// prototype, and a cross-origin sheet that will not show its rules.
void test_sheet_attribute_and_origin() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style id=s>.a { color: red }</style>
        <link id=far rel=stylesheet href="http://other.invalid/x.css">
        <link id=near rel=stylesheet href="missing.css"></head><body><script>
        const s = document.getElementById('s');
        console.log('proto=' + !s.hasOwnProperty('sheet') + ',' +
                    ('sheet' in HTMLStyleElement.prototype) + ',' +
                    (s.sheet === document.styleSheets[0]));
        let caught = '';
        try { document.getElementById('far').sheet.cssRules; } catch (e) { caught = e.name; }
        let inserted = '';
        try { document.getElementById('far').sheet.insertRule('a {}'); } catch (e) { inserted = e.name; }
        console.log('far=' + caught + ',' + inserted + ',' +
                    document.getElementById('near').sheet.cssRules.length);
    </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "proto="), std::string{"proto=true,true,true"});
    CHECK_EQ(logged(page, "far="), std::string{"far=SecurityError,SecurityError,0"});
}

// getComputedStyle-resolved-colors: a system colour resolves to an rgb(),
// and a logical border longhand answers as its physical one.
void test_resolved_colours() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        #t { background-color: Menu; border: 1px solid Menu; color: Menu; caret-color: Menu;
             box-shadow: 1px 1px Menu, inset 2px 3px 4px; text-shadow: 1em 0 red }
        #u { color: blue; box-shadow: 0 0 0 1px currentcolor }
        </style></head><body><div id=t></div><div id=u></div><script>
        const cs = getComputedStyle(document.getElementById('t'));
        console.log('sys=' + ['background-color', 'border-top-color', 'border-block-end-color',
                              'border-inline-start-color', 'color', 'caret-color',
                              'border-block-start-width', 'border-inline-end-style']
                        .map(p => cs.getPropertyValue(p)).join('|'));
        console.log('shadow=' + cs.boxShadow + '|' + cs.textShadow + '|' +
                    getComputedStyle(document.getElementById('u')).boxShadow);
    </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "sys="),
             std::string{"sys=rgb(247, 247, 247)|rgb(247, 247, 247)|rgb(247, 247, 247)|"
                         "rgb(247, 247, 247)|rgb(247, 247, 247)|rgb(247, 247, 247)|1px|solid"});
    // A shadow's colour first and resolved - the system colour, an omitted
    // one as the element's own `color` - and its lengths in px, padded.
    CHECK_EQ(logged(page, "shadow="),
             std::string{"shadow=rgb(247, 247, 247) 1px 1px 0px 0px, "
                         "rgb(247, 247, 247) 2px 3px 4px 0px inset|"
                         "rgb(255, 0, 0) 16px 0px 0px|rgb(0, 0, 255) 0px 0px 0px 1px"});
}

// getComputedStyle-detached-subtree, getComputedStyle-pseudo-with-argument: an
// element outside the flat tree - a light child of a shadow host that no
// `<slot>` takes - has no computed style, while a slotted one keeps its own;
// a node of another document answers the same empty declaration; and a
// functional pseudo-element argument parses as the selector grammar does.
void test_flat_tree_and_pseudo_arguments() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body>
        <div id=host><div id=unslotted></div></div>
        <div id=slotting><div id=slotted></div><div id=named slot=n></div></div>
        <iframe id=f srcdoc="<html></html>" style="display: none"></iframe>
        <script>
        host.attachShadow({mode: 'open'});
        slotting.attachShadow({mode: 'open'}).innerHTML = '<slot></slot>';
        const empty = el => getComputedStyle(el).length == 0 && getComputedStyle(el).color === '';
        console.log('flat=' + empty(unslotted) + ',' + empty(slotted) + ',' + empty(named) + ',' +
                    empty(host) + ',' + empty(f.contentDocument.documentElement));
        const parses = p => getComputedStyle(host, p).length != 0;
        console.log('fn=' + ['::highlight(name)', '::highlight( n\\61me ', '::picker(select)',
                             '::view-transition-old(x)']
                        .map(parses).join() + '|' +
                    ['::highlight()', '::highlight(1)', '::highlight(name)a', ':highlight(name)',
                     '::picker(div)', '::before(x)', '::highlight (name)']
                        .map(parses).join());
        // getComputedStyle-pseudo: a rule for `::highlight(name)` IS the
        // answer for that pseudo-element, and not for another argument.
        const sheet = document.createElement('style');
        sheet.textContent = '#host::highlight(name) { color: rgb(0, 128, 0) }'
            + ' #host::view-transition-old(x) { color: rgb(0, 0, 128) }';
        document.head.appendChild(sheet);
        console.log('hl=' + getComputedStyle(host, '::highlight(name)').color + '|' +
                    getComputedStyle(host, '::highlight(other)').color + '|' +
                    getComputedStyle(host, '::view-transition-old(x)').color + '|' +
                    document.styleSheets[0].cssRules[0].selectorText);
        </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "flat="), std::string{"flat=true,false,true,false,true"});
    CHECK_EQ(logged(page, "hl="),
             std::string{"hl=rgb(0, 128, 0)|rgb(0, 0, 0)|rgb(0, 0, 128)|#host::highlight(name)"});
    CHECK_EQ(logged(page, "fn="),
             std::string{"fn=true,true,true,true|false,false,false,false,false,false,false"});
}

// getComputedStyle-sticky-pos-percent: a sticky inset's percentage resolves
// against the nearest scroll container's content box, `auto` stays `auto`,
// and a calc() folds against the same basis.
void test_sticky_insets() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body style="margin: 0">
        <div style="height: 500px; overflow: hidden">
          <div style="height: 400px">
            <div id="t" style="height: 100px; position: sticky; left: 0; top: 50%;
                               bottom: calc(10% - 1px);"></div>
          </div>
        </div>
        <script>
        const cs = getComputedStyle(document.getElementById('t'));
        console.log('sticky=' + cs.top + ',' + cs.bottom + ',' + cs.left + ',' + cs.right);
        </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "sticky="), std::string{"sticky=250px,49px,0px,auto"});
}

// mediaquery-sort-dedup: matchMedia serialises the list as written - not
// sorted, not deduplicated - and answers `matches` from the cascade's own
// environment.
void test_match_media() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><script>
        console.log('mm=' + matchMedia('(min-width: 10px) and (min-height: 10px)').media + '|' +
                    window.matchMedia('(color) and (color)').media + '|' +
                    matchMedia('(min-width: 300px)').matches + ',' +
                    matchMedia('(min-width: 500px)').matches + ',' +
                    matchMedia('screen').matches + ',' + matchMedia('print').matches + '|' +
                    Object.prototype.toString.call(matchMedia('all')));
        // Media Queries 4: a length may be any unit (em is the initial 16px,
        // the viewport units are the viewport's) or a math function of them.
        console.log('units=' + [matchMedia('(width: 100vw)').matches,
                                matchMedia('(height: 100vh)').matches,
                                matchMedia('(width: calc(50vw + 200px))').matches,
                                matchMedia('(width: calc(200vh + 5em))').matches,
                                matchMedia('(min-width: 25em)').matches,
                                matchMedia('(min-width: 26em)').matches,
                                matchMedia('(width >= calc(100vh * 2))').matches].join());
        </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "mm="),
             std::string{"mm=(min-width: 10px) and (min-height: 10px)|(color) and (color)|"
                         "true,false,true,false|[object MediaQueryList]"});
    // 400 x 200: 200vh + 5em is 480 (false), 100vh * 2 is 400.
    CHECK_EQ(logged(page, "units="), std::string{"units=true,true,true,false,true,false,true"});
}

// ttwf-cssom-doc-ext-load-count: a StyleSheetList held in a variable is live -
// its `length` follows a removed <style> - and it still iterates, indexes and
// answers `item()`, and is one object.
void test_live_sheet_list() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>a { color: red }</style><style></style></head><body>
        <script>
        const list = document.styleSheets;
        const before = list.length + ',' + [...list].length + ',' + Array.from(list).length;
        list.item(0).ownerNode.remove();
        console.log('live=' + before + '|' + list.length + ',' + [...list].length + ',' +
                    (list.item(1) === null) + ',' + (list[0] === document.styleSheets[0]) + ',' +
                    (list === document.styleSheets) + ',' + (list instanceof StyleSheetList) + ',' +
                    Object.prototype.toString.call(list));
        </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "live="),
             std::string{"live=2,2,2|1,1,true,true,true,true,[object StyleSheetList]"});
}

// tree-counting/sibling-function-descriptors: a descriptor is on no element,
// so a tree-counting function in one is invalid and the earlier value stays.
void test_descriptors_refuse_tree_counting() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        @page { margin-top: 10px; margin-top: calc(0px * sibling-index()) }
        @font-face { font-weight: 300; font-weight: calc(max(0 * sibling-count(), 400)) }
        </style></head><body><script>
        const rules = document.styleSheets[0].cssRules;
        rules[0].style.setProperty('margin-bottom', 'calc(1px * sibling-index())');
        console.log('desc=' + rules[0].style.marginTop + '|' + rules[0].style.marginBottom + '|' +
                    rules[1].style.fontWeight);
    </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "desc="), std::string{"desc=10px||300"});
}

} // namespace

int main() {
    test_resolved_colours();
    test_flat_tree_and_pseudo_arguments();
    test_live_sheet_list();
    test_sticky_insets();
    test_match_media();
    test_descriptors_refuse_tree_counting();
    test_class_strings_and_iterators();
    test_removed_rules_charset_keyframes_and_container();
    test_declaration_blocks();
    test_sheet_attribute_and_origin();
    REPORT("cssom_wpt");
}
