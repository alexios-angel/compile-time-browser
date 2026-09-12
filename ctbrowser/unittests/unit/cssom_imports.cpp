// `@import`, THE SHEETS OF A SHADOW TREE, AND `HTMLLinkElement.disabled`.
//
// Three things the CSSOM answered wrongly or not at all: an `@import` was
// consumed by the sheet parser and reached neither the cascade nor
// `rule.styleSheet`; a `<style>` in a shadow root had no `sheet` and no list to
// be in; and a `<link disabled>` was a stylesheet like any other. Each case
// here is the shape of the css/cssom file that asserts it - cssimportrule,
// cssimportrule-parent, StyleSheetList-constructable-shadow and
// HTMLLinkElement-disabled-001 - reduced to what the engine can log.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/css/parser.hpp>
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

[[nodiscard]] std::vector<std::byte> bytes_of(std::string_view text) {
    const auto * begin = reinterpret_cast<const std::byte *>(text.data());
    return {begin, begin + text.size()};
}

// THE STATEMENTS THE FRONT END FINDS: only the leading run, the URL in any of
// its three spellings, the media list with `layer` and `supports()` removed,
// and byte spans a caller can splice.
void test_leading_imports() {
    using ctbrowser::style::css::leading_imports;
    const std::string css = "@charset \"utf-8\";\n@import url(a.css);@import \"b.css\" screen and "
                            "(min-width: 1px);\n@import url(\"c.css\") layer(x) supports(display: "
                            "grid) print;\np { color: red }\n@import url(late.css);";
    const auto found = leading_imports(css);
    CHECK_EQ(found.size(), std::size_t{3});
    CHECK_EQ(found[0].href, std::string{"a.css"});
    CHECK(found[0].media.empty());
    CHECK_EQ(css.substr(found[0].begin, found[0].end - found[0].begin),
             std::string{"@import url(a.css);"});
    CHECK_EQ(found[1].href, std::string{"b.css"});
    CHECK_EQ(found[1].media, std::string{"screen and (min-width: 1px)"});
    CHECK_EQ(found[2].href, std::string{"c.css"});
    CHECK_EQ(found[2].media, std::string{"print"});
    // A string with a `;` in it does not end the statement, and a block
    // at-rule ends the run.
    const auto tricky = leading_imports("@import \"a;b.css\";\n@layer x { }\n@import url(z);");
    CHECK_EQ(tricky.size(), std::size_t{1});
    CHECK_EQ(tricky[0].href, std::string{"a;b.css"});
}

// THE CASCADE SEES THE IMPORTED RULES - through a `<link>` whose sheet imports
// a second sheet relative to itself, and through a `<style>` importing with a
// media query that does not match, which must not apply.
void test_an_import_reaches_the_cascade() {
    browser page{browser_options{400, 200}};
    page.assets().add("css/main.css",
                      bytes_of("@import \"red.css\";\n.b { color: rgb(0, 0, 255) }"));
    page.assets().add("css/red.css", bytes_of(".r { color: rgb(255, 0, 0) }"));
    page.assets().add("print.css", bytes_of(".p { color: rgb(9, 9, 9) }"));
    page.load_html(R"(<html><head><link rel=stylesheet href="css/main.css">
    <style>@import url(print.css) print;</style></head>
    <body><p class=r id=r>r</p><p class=b id=b>b</p><p class=p id=p>p</p><script>
        const c = id => getComputedStyle(document.getElementById(id)).color;
        console.log('colors=' + c('r') + '|' + c('b') + '|' + c('p'));
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "colors="),
             std::string{"colors=rgb(255, 0, 0)|rgb(0, 0, 255)|rgb(0, 0, 0)"});
}

// CSSImportRule: `styleSheet` is a sheet of its own with its own rules and its
// parent, `deleteRule` unlinks it, and the object model's text - the one the
// cascade is handed after a CSSOM write - carries the imported rules.
void test_the_import_rule_object() {
    browser page{browser_options{400, 200}};
    page.assets().add("a.css", bytes_of("@import 'b.css';\n.a { color: green }"));
    page.assets().add("b.css", bytes_of(".b { color: blue }"));
    page.load_html(R"(<html><head><style id=s>@import url("a.css") screen;
    .own { color: red }</style></head><body><script>
        const sheet = document.getElementById('s').sheet;
        const rule = sheet.cssRules[0];
        const child = rule.styleSheet;
        console.log('rule=' + rule.type + '|' + rule.href + '|' + rule.media.mediaText + '|' +
                    rule.cssText);
        console.log('child=' + (child instanceof CSSStyleSheet) + '|' + child.href + '|' +
                    child.cssRules.length + '|' + (child.parentStyleSheet === sheet) + '|' +
                    (child.ownerRule === rule) + '|' + (rule.styleSheet === child));
        const grandchild = child.cssRules[0].styleSheet;
        console.log('nested=' + grandchild.cssRules[0].cssText + '|' +
                    (grandchild.parentStyleSheet === child));
        sheet.deleteRule(0);
        console.log('unlinked=' + child.parentStyleSheet + '|' + child.ownerRule + '|' +
                    sheet.cssRules.length);
        // A constructed sheet drops its imports rather than fetching them.
        const made = new CSSStyleSheet();
        made.replaceSync('@import url(a.css); .z { color: red }');
        console.log('constructed=' + made.cssRules.length + '|' + made.cssRules[0].cssText);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "rule="),
             std::string{"rule=3|a.css|screen|@import url(\"a.css\") screen;"});
    CHECK_EQ(logged(page, "child="), std::string{"child=true|a.css|2|true|true|true"});
    CHECK_EQ(logged(page, "nested="), std::string{"nested=.b { color: blue; }|true"});
    CHECK_EQ(logged(page, "unlinked="), std::string{"unlinked=null|null|1"});
    CHECK_EQ(logged(page, "constructed="), std::string{"constructed=1|.z { color: red; }"});

    // The text the cascade gets after a CSSOM write: the import's rules, wrapped
    // in its media query, in place of the `@import` line.
    browser text{browser_options{400, 200}};
    text.assets().add("a.css", bytes_of(".a { color: green }"));
    text.load_html(R"(<html><head><style>@import url("a.css") screen; .own { color: red }</style>
    </head><body><script>document.styleSheets[0].insertRule('.x { color: blue }', 1);</script>
    </body></html>)");
    CHECK(text.script_error().empty());
    CHECK_EQ(text.bindings().author_style_text(),
             std::string{"@media screen {\n.a { color: green; }\n}\n.x { color: blue; }\n"
                         ".own { color: red; }\n"});
}

// A SHADOW ROOT HAS ITS OWN LIST: a `<style>` appended to one has a `sheet`,
// that sheet is in `shadowRoot.styleSheets` and not in the document's, and
// `adoptedStyleSheets` on the root takes constructed sheets only.
void test_the_sheets_of_a_shadow_tree() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>p { color: red }</style></head><body><div id=h></div>
    <script>
        const shadow = document.getElementById('h').attachShadow({mode: 'open'});
        const style = document.createElement('style');
        style.textContent = ':host { color: red }';
        shadow.appendChild(style);
        const sheet = new CSSStyleSheet();
        shadow.adoptedStyleSheets = [sheet];
        console.log('shadow=' + shadow.styleSheets.length + '|' +
                    (shadow.styleSheets[0] === style.sheet) + '|' +
                    (shadow.styleSheets === shadow.styleSheets) + '|' +
                    document.styleSheets.length + '|' + shadow.adoptedStyleSheets.length + '|' +
                    (shadow.adoptedStyleSheets[0] === sheet) + '|' +
                    style.sheet.cssRules[0].cssText + '|' + (style.sheet.ownerNode === style));
        let refused = '';
        try { shadow.adoptedStyleSheets = [document.styleSheets[0]]; } catch (e) { refused = e.name; }
        console.log('refused=' + refused + '|' + document.adoptedStyleSheets.length);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "shadow="),
             std::string{"shadow=1|true|true|1|1|true|:host { color: red; }|true"});
    CHECK_EQ(logged(page, "refused="), std::string{"refused=NotAllowedError|0"});
}

// `<link disabled>` IS NOT A STYLESHEET until the attribute goes, and
// `link.disabled` is what removes it - firing `load` once the sheet applies
// - and puts it back, at which point the sheet answers null for `ownerNode`
// and leaves `document.styleSheets`. An `alternate stylesheet` applies only
// once the link has been explicitly enabled that way.
void test_link_disabled() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head>
    <link id=l rel=stylesheet disabled href="data:text/css,html { color: rgb(0, 128, 0) }">
    </head><body><p id=p>p</p><script>
        const link = document.getElementById('l');
        const colour = () => getComputedStyle(document.documentElement).color;
        console.log('before=' + document.styleSheets.length + '|' + link.disabled + '|' + colour());
        link.onload = () => {
            const sheet = document.styleSheets[0];
            const applied = colour();
            const owner = sheet.ownerNode === link;
            link.disabled = true;
            console.log('loaded=' + applied + '|' + owner + '|' + sheet.ownerNode + '|' +
                        sheet.disabled + '|' + link.hasAttribute('disabled') + '|' +
                        document.styleSheets.length + '|' + colour());
        };
        link.disabled = false;
        console.log('enabled=' + link.hasAttribute('disabled') + '|' + link.disabled);
    </script></body></html>)");
    for (int i = 0; i < 3; ++i) {
        (void)page.tick(16.0);
        page.frame();
    }
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "before="), std::string{"before=0|true|rgb(0, 0, 0)"});
    CHECK_EQ(logged(page, "enabled="), std::string{"enabled=false|false"});
    CHECK_EQ(logged(page, "loaded="),
             std::string{"loaded=rgb(0, 128, 0)|true|null|false|true|0|rgb(0, 0, 0)"});

    // THE ALTERNATE: fetched (its `load` fires), not applied and not listed.
    // `disabled = false` with no attribute present changes nothing;
    // true-then-false removes the attribute, which explicitly enables it.
    browser alternate{browser_options{400, 200}};
    alternate.load_html(R"(<html><head>
    <link id=alt rel="alternate stylesheet" title=alt href="data:text/css,p { color: rgb(1, 2, 3) }">
    </head><body><p id=p>p</p><script>
        const alt = document.getElementById('alt');
        const pc = () => getComputedStyle(document.getElementById('p')).color;
        alt.onload = () => console.log('alt=fired');
        alt.disabled = false;
        console.log('noop=' + alt.hasAttribute('disabled') + '|' + pc() + '|' +
                    document.styleSheets.length);
        alt.disabled = true;
        alt.disabled = false;
        console.log('explicit=' + pc() + '|' + document.styleSheets.length + '|' +
                    (document.styleSheets[0].ownerNode === alt));
    </script></body></html>)");
    for (int i = 0; i < 3; ++i) {
        (void)alternate.tick(16.0);
        alternate.frame();
    }
    CHECK(alternate.script_error().empty());
    CHECK_EQ(logged(alternate, "noop="), std::string{"noop=false|rgb(0, 0, 0)|0"});
    CHECK_EQ(logged(alternate, "explicit="), std::string{"explicit=rgb(1, 2, 3)|1|true"});
    CHECK_EQ(logged(alternate, "alt="), std::string{"alt=fired"});
}

// THE `background` SHORTHAND REACHES THE CASCADE. It was recorded whole and
// never expanded, so `background: green` - which every HTMLLinkElement-disabled
// test asserts through the root's computed backgroundColor - painted nothing.
void test_the_background_shorthand() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        #a { background: green }
        #b { background: url(x.png) no-repeat center / cover rgb(1, 2, 3) }
        #c { background-color: red; background: none }
        #d { background: red, url(y.png) blue }
    </style></head><body><p id=a></p><p id=b></p><p id=c></p><p id=d></p><script>
        const c = id => getComputedStyle(document.getElementById(id)).backgroundColor;
        console.log('bg=' + c('a') + '|' + c('b') + '|' + c('c') + '|' + c('d'));
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "bg="),
             std::string{"bg=rgb(0, 128, 0)|rgb(1, 2, 3)|rgba(0, 0, 0, 0)|rgb(0, 0, 255)"});
}

} // namespace

int main() {
    test_the_background_shorthand();
    test_leading_imports();
    test_an_import_reaches_the_cascade();
    test_the_import_rule_object();
    test_the_sheets_of_a_shadow_tree();
    test_link_disabled();
    REPORT("cssom_imports");
}
