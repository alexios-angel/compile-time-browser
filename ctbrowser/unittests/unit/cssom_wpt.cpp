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
    CHECK(page.script_error().empty());
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
    CHECK(page.script_error().empty());
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
    </script></body></html>)");
    CHECK(page.script_error().empty());
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
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "proto="), std::string{"proto=true,true,true"});
    CHECK_EQ(logged(page, "far="), std::string{"far=SecurityError,SecurityError,0"});
}

// getComputedStyle-resolved-colors: a system colour resolves to an rgb(),
// and a logical border longhand answers as its physical one.
void test_resolved_colours() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        #t { background-color: Menu; border: 1px solid Menu; color: Menu; caret-color: Menu }
        </style></head><body><div id=t></div><script>
        const cs = getComputedStyle(document.getElementById('t'));
        console.log('sys=' + ['background-color', 'border-top-color', 'border-block-end-color',
                              'border-inline-start-color', 'color', 'caret-color',
                              'border-block-start-width', 'border-inline-end-style']
                        .map(p => cs.getPropertyValue(p)).join('|'));
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "sys="),
             std::string{"sys=rgb(247, 247, 247)|rgb(247, 247, 247)|rgb(247, 247, 247)|"
                         "rgb(247, 247, 247)|rgb(247, 247, 247)|rgb(247, 247, 247)|1px|solid"});
}

} // namespace

int main() {
    test_resolved_colours();
    test_class_strings_and_iterators();
    test_removed_rules_charset_keyframes_and_container();
    test_declaration_blocks();
    test_sheet_attribute_and_origin();
    REPORT("cssom_wpt");
}
