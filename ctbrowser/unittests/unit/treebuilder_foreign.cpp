// FOREIGN CONTENT, HTML 13.2.6.5, as the page sees it: `<math>` is MathML -
// `node_ns::other` with its URI on the document - with the text integration
// points handing children back to HTML; the tokenizer lowercases every name
// and the adjustment tables put `viewBox`, `foreignObject` and
// `definitionURL` back; only the ten listed foreign attributes get a
// namespace; and a fragment parses in ITS ELEMENT'S context, so
// `table.innerHTML = "<tr>"` is a row and `svg.innerHTML` makes SVG.
//
// The html5lib fixtures (html5lib_fixtures) cover the tree shapes case by
// case; this is the handful of facts a script observes on top of them.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include <string>
#include <string_view>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

[[nodiscard]] std::string said(std::string_view body_script) {
    browser page{browser_options{200, 100}};
    page.load_html("<!DOCTYPE html><body>" + std::string{body_script});
    std::string out;
    for (const std::string & one : page.alerts()) {
        if (!out.empty()) { out += ';'; }
        out += one;
    }
    if (!page.script_error().empty()) { out += "|error:" + page.script_error(); }
    return out;
}

void test_mathml_is_a_namespace() {
    CHECK_EQ(said("<math xml:lang=en definitionurl=d><mi>a</mi><mrow><b>x</b></mrow></math>"
                  "<script>"
                  "var m = document.querySelector('math');"
                  "var xl = m.getAttributeNodeNS('http://www.w3.org/XML/1998/namespace', 'lang');"
                  "alert([m.namespaceURI, m.nodeName, m.firstChild.namespaceURI,"
                  " m.getAttribute('definitionURL'), xl.prefix + ':' + xl.localName,"
                  " m.childNodes.length, m.nextSibling.localName,"
                  " m.cloneNode(true).namespaceURI === m.namespaceURI].join());"
                  "</script>"),
             "http://www.w3.org/1998/Math/MathML,math,http://www.w3.org/1998/Math/MathML,d,"
             "xml:lang,2,b,true");
}

void test_svg_names_are_adjusted_from_any_case() {
    CHECK_EQ(said("<SVG VIEWBOX='0 0 1 1'><FOREIGNOBJECT><p>hi</p></FOREIGNOBJECT>"
                  "<lineargradient gradientunits=x xml:base=b xlink:href=h/></SVG>"
                  "<script>"
                  "var s = document.querySelector('svg');"
                  "var g = s.lastChild;"
                  "var base = g.getAttributeNode('xml:base');"
                  "var href = g.getAttributeNode('xlink:href');"
                  "alert([s.hasAttribute('viewBox'), s.firstChild.localName,"
                  " s.firstChild.firstChild.namespaceURI, g.localName,"
                  " g.hasAttribute('gradientUnits'), base.namespaceURI, href.namespaceURI,"
                  " document.getElementsByTagName('linearGradient').length].join());"
                  "</script>"),
             "true,foreignObject,http://www.w3.org/1999/xhtml,linearGradient,true,,"
             "http://www.w3.org/1999/xlink,1");
}

void test_a_fragment_parses_in_its_elements_context() {
    CHECK_EQ(said("<table id=t></table><svg id=s></svg><title id=ti></title>"
                  "<script>"
                  "t.innerHTML = '<tr><td>x';"
                  "s.innerHTML = '<circle/>x<![CDATA[y]]>';"
                  "ti.innerHTML = 'a</title>b';"
                  "var mi = document.createElementNS('http://www.w3.org/1998/Math/MathML', 'mi');"
                  "mi.innerHTML = '<b>x</b><![CDATA[y]]>';"
                  "alert([t.firstChild.localName, t.getElementsByTagName('tr').length, "
                  "s.firstChild.namespaceURI,"
                  " s.textContent, ti.textContent, mi.firstChild.namespaceURI,"
                  " mi.textContent].join());"
                  "</script>"),
             "tbody,1,http://www.w3.org/2000/svg,xy,a</title>b,http://www.w3.org/1999/xhtml,xy");
}

} // namespace

int main() {
    test_mathml_is_a_namespace();
    test_svg_names_are_adjusted_from_any_case();
    test_a_fragment_parses_in_its_elements_context();
    REPORT("treebuilder_foreign");
}
