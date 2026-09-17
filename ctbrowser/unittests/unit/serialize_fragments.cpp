// THE FRAGMENT SERIALISERS as a page reads them back: HTML 13.3 for an HTML
// document - an element that serialises as void writes no children and has
// an empty innerHTML, a raw-text element's innerHTML is unescaped, an
// attribute is named by its NAMESPACE, a <template> serialises its contents,
// a PI is `<?target data?>` - and the XML serialisation for an element of an
// XML document, `xmlns` and all. Plus the one thing that made every
// template test fail: `div.innerHTML = "<template>..."` must carry the
// template's contents across, and `template.innerHTML = ...` fills them.

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

void test_html_fragment_serialisation() {
    CHECK_EQ(said("<script>"
                  "var k = document.createElement('keygen');"
                  "k.appendChild(document.createElement('a'));"
                  "var s = document.createElement('script');"
                  "s.appendChild(document.createTextNode('<&>'));"
                  "var g = document.createElement('svg');"
                  "g.setAttributeNS('http://www.w3.org/XML/1998/namespace', 'abc:foo', 'x');"
                  "g.setAttributeNS('http://www.w3.org/2000/xmlns/', 'xmlns', 'y');"
                  "var d = document.createElement('div');"
                  "d.appendChild(document.createProcessingInstruction('t', 'data'));"
                  "alert([k.innerHTML, k.outerHTML, s.innerHTML, s.outerHTML, g.outerHTML,"
                  " d.innerHTML].join());"
                  "</script>"),
             ",<keygen>,<&>,<script><&></script>,<svg xml:foo=\"x\" xmlns=\"y\"></svg>,"
             "<?t data?>");
}

void test_template_contents_travel_and_serialise() {
    CHECK_EQ(
        said("<div id=d></div><script>"
             "d.innerHTML = '<template id=t><table><tr><td>x</template>';"
             "var t = document.getElementById('t');"
             "alert([t.childNodes.length, t.content.childNodes.length,"
             " t.content.querySelector('td').textContent, t.innerHTML, d.innerHTML].join());"
             "t.innerHTML = '<b>y</b>';"
             "alert([t.childNodes.length, t.content.firstChild.localName, t.innerHTML].join());"
             "</script>"),
        "0,1,x,<table><tbody><tr><td>x</td></tr></tbody></table>,"
        "<template id=\"t\"><table><tbody><tr><td>x</td></tr></tbody></table></template>;"
        "0,b,<b>y</b>");
}

void test_xml_document_serialises_as_xml() {
    CHECK_EQ(said("<script>"
                  "var doc = document.implementation.createDocument(null, '');"
                  "var ns = 'http://www.w3.org/1999/xhtml';"
                  "var a = doc.createElementNS(ns, 'a');"
                  "var br = doc.createElementNS(ns, 'br');"
                  "a.appendChild(doc.createElementNS('urn:x', 'p'));"
                  "alert([a.outerHTML, br.outerHTML, a.innerHTML].join());"
                  // And the setter is the XML fragment parser: the context's
                  // namespace is the default, a declared prefix resolves, and
                  // an ill-formed fragment leaves the element alone.
                  "a.innerHTML = '<b/><q:r xmlns:q=\"urn:q\"/>';"
                  "var kept = a.innerHTML; a.innerHTML = '<b>';"
                  "alert([a.firstChild.namespaceURI === ns, a.lastChild.namespaceURI,"
                  " a.lastChild.prefix, a.innerHTML === kept].join());"
                  "</script>"),
             "<a xmlns=\"http://www.w3.org/1999/xhtml\"><p xmlns=\"urn:x\"/></a>,"
             "<br xmlns=\"http://www.w3.org/1999/xhtml\" />,<p xmlns=\"urn:x\"/>;"
             "true,urn:q,q,true");
}

} // namespace

int main() {
    test_html_fragment_serialisation();
    test_template_contents_travel_and_serialise();
    test_xml_document_serialises_as_xml();
    REPORT("serialize_fragments");
}
