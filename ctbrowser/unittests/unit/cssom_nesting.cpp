// The CSSOM of CSS Nesting and the cascade-layer rules: a style rule's nested
// rules as its `cssRules`, the nested declarations rule, the multi-line
// serialisation css-nesting/cssom.html asserts, and CSSLayerBlockRule /
// CSSLayerStatementRule / CSSScopeRule. Beside them, that the cascade sees
// what the object model serialises: a nested rule inserted through
// `insertRule` restyles the page.

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

#include <string>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser_test::logged;

namespace {

void test_nested_rules_are_css_rules() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        .a { color: red; .b { color: green } --x: 1; & > i { z-index: 1 } }
    </style></head><body><div class=a><span class=b>x</span></div><script>
        const rule = document.styleSheets[0].cssRules[0];
        console.log('proto=' + (CSSStyleRule.__proto__ === CSSGroupingRule));
        console.log('count=' + rule.cssRules.length);
        console.log('child0=' + rule.cssRules[0].cssText);
        console.log('child1=' + rule.cssRules[1].cssText + '|' +
                    (rule.cssRules[1] instanceof CSSNestedDeclarations) + '|' +
                    rule.cssRules[1].style.getPropertyValue('--x'));
        console.log('child2=' + rule.cssRules[2].selectorText);
        console.log('text=' + JSON.stringify(rule.cssText));
        rule.insertRule('& .c { color: blue }', 0);
        console.log('inserted=' + rule.cssRules[0].cssText + '|' +
                    (rule.cssRules[0].parentRule === rule));
        rule.deleteRule(0);
        console.log('deleted=' + rule.cssRules.length);
        console.log('green=' + getComputedStyle(document.querySelector('.b')).color);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "proto="), std::string{"proto=true"});
    CHECK_EQ(logged(page, "count="), std::string{"count=3"});
    CHECK_EQ(logged(page, "child0="), std::string{"child0=& .b { color: green; }"});
    CHECK_EQ(logged(page, "child1="), std::string{"child1=--x: 1;|true|1"});
    CHECK_EQ(logged(page, "child2="), std::string{"child2=& > i"});
    CHECK_EQ(logged(page, "text="),
             std::string{"text=\".a {\\n  color: red;\\n  & .b { color: green; }\\n  --x: "
                         "1;\\n  & > i { z-index: 1; }\\n}\""});
    CHECK_EQ(logged(page, "inserted="), std::string{"inserted=& .c { color: blue; }|true"});
    CHECK_EQ(logged(page, "deleted="), std::string{"deleted=3"});
    CHECK_EQ(logged(page, "green="), std::string{"green=rgb(0, 128, 0)"});
}

void test_layer_and_scope_rules() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        @layer a, b.c;
        @layer b { .t { color: red } }
        @layer { .t { color: green } }
        @scope (.s) to (.e) { .t { z-index: 1 } }
    </style></head><body><div class=s><div class=t>x</div></div><script>
        const rules = document.styleSheets[0].cssRules;
        console.log('names=' + JSON.stringify(rules[0].nameList) + '|' + rules[0].cssText);
        console.log('block=' + rules[1].name + '|' + (rules[1] instanceof CSSLayerBlockRule) +
                    '|' + rules[1].cssRules.length + '|' + JSON.stringify(rules[1].cssText));
        console.log('anon=' + JSON.stringify(rules[2].name));
        console.log('scope=' + rules[3].start + '|' + rules[3].end + '|' +
                    (rules[3] instanceof CSSScopeRule));
        const t = document.querySelector('.t');
        console.log('cascade=' + getComputedStyle(t).color + '|' + getComputedStyle(t).zIndex);
        // Through the object model, the cascade still sees the layers.
        document.styleSheets[0].insertRule('.t { color: blue }', 4);
        console.log('after=' + getComputedStyle(t).color);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "names="), std::string{"names=[\"a\",\"b.c\"]|@layer a, b.c;"});
    CHECK_EQ(logged(page, "block="),
             std::string{"block=b|true|1|\"@layer b {\\n  .t { color: red; }\\n}\""});
    CHECK_EQ(logged(page, "anon="), std::string{"anon=\"\""});
    CHECK_EQ(logged(page, "scope="), std::string{"scope=.s|.e|true"});
    CHECK_EQ(logged(page, "cascade="), std::string{"cascade=rgb(0, 128, 0)|1"});
    CHECK_EQ(logged(page, "after="), std::string{"after=rgb(0, 0, 255)"});
}

} // namespace

int main() {
    test_nested_rules_are_css_rules();
    test_layer_and_scope_rules();
    REPORT("cssom_nesting");
}
