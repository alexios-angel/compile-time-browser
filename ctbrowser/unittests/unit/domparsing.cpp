// DOMParser and XMLSerializer: the two halves of DOM Parsing.
//
// The regression net for lib/Shell/bindings/domparsing.cpp and for the
// scripting flag DOMParser turns off. What it pins down is that XML
// serialisation is NOT the HTML fragment serialiser - an empty element is
// self-closed, a namespace comes out with the element that has it - and that a
// document no script will ever run in parses `<noscript>` as elements.

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

[[nodiscard]] std::string said(std::string_view html) {
    browser page{browser_options{200, 100}};
    page.load_html(html);
    std::string out;
    for (const std::string & one : page.alerts()) {
        if (!out.empty()) { out += ';'; }
        out += one;
    }
    if (!page.script_error().empty()) { out += "|error:" + page.script_error(); }
    return out;
}

void test_serialize_to_string() {
    CHECK_EQ(said("<html><body><script>"
                  "var s = new XMLSerializer();"
                  "var d = new DOMParser().parseFromString("
                  "'<root xmlns=\"urn:x\"><a b=\"1&amp;2\"/><c>t&lt;u</c></root>',"
                  " 'application/xml');"
                  "alert(s.serializeToString(d.documentElement));"
                  "alert(s.serializeToString(d));"
                  "var made = document.createElement('div');"
                  "made.setAttribute('x', 'a\\tb');"
                  "alert(s.serializeToString(made));"
                  "alert(typeof XMLSerializer.prototype.serializeToString);"
                  "</script></body></html>"),
             "<root xmlns=\"urn:x\"><a b=\"1&amp;2\"/><c>t&lt;u</c></root>;"
             "<root xmlns=\"urn:x\"><a b=\"1&amp;2\"/><c>t&lt;u</c></root>;"
             "<div xmlns=\"http://www.w3.org/1999/xhtml\" x=\"a&#9;b\"></div>;"
             "function");
}

// XMLSerializer-serializeToString.html: the namespace prefix map. A null
// namespace root writes no xmlns; a prefix in scope for the namespace is
// reused; an attribute in a namespace nobody declared gets `ns1`; the XML
// namespace is `xml:`; a redundant declaration is dropped.
void test_serialize_namespaces() {
    CHECK_EQ(
        said("<html><body><script>"
             "var s = new XMLSerializer(), p = new DOMParser();"
             "var d = p.parseFromString('<root><child1>value1</child1></root>', 'text/xml');"
             "alert(s.serializeToString(d) + '|' + d.documentElement.namespaceURI + '|' +"
             " (d instanceof XMLDocument) + '|' + (d.URL === document.URL));"
             "var r = d.createElementNS('uri', 'p:root'); r.setAttributeNS('uri2', 'q:a', 'v');"
             "alert(s.serializeToString(r));"
             "var e = d.createElement('r'); e.setAttributeNS('http://www.w3.org/2000/xmlns/',"
             " 'xmlns:xx', 'uri'); e.setAttributeNS('uri', 'p:name', 'v');"
             "alert(s.serializeToString(e));"
             "var f = p.parseFromString('<root><xml:foo/></root>', 'text/xml');"
             "alert(s.serializeToString(f));"
             "var g = p.parseFromString('<root xmlns=\"u\"><child xmlns=\"u\"/></root>',"
             " 'text/xml');"
             "alert(s.serializeToString(g));"
             "</script></body></html>"),
        "<root><child1>value1</child1></root>|null|false|true;"
        "<p:root xmlns:p=\"uri\" xmlns:ns1=\"uri2\" ns1:a=\"v\"/>;"
        "<r xmlns:xx=\"uri\" xx:name=\"v\"/>;"
        "<root><xml:foo/></root>;"
        "<root xmlns=\"u\"><child/></root>");
}

// The scripting flag is the DOCUMENT's: a DOMParser document can never run a
// script, so `<noscript>` holds elements rather than one raw text node. The
// page's own document keeps the flag ON, which is what a `<noscript>` in the
// markup below checks.
void test_noscript_follows_the_documents_scripting_flag() {
    CHECK_EQ(said("<html><body><noscript><p id=x>one</noscript><script>"
                  "var d = new DOMParser().parseFromString("
                  "'<body><noscript><p id=t1>a<p id=t2>b</noscript>', 'text/html');"
                  "var n = d.body.firstChild;"
                  "alert(n.childNodes.length + ',' + n.childNodes[0].id + ',' "
                  "+ n.childNodes[0].localName);"
                  "var here = document.querySelector('noscript');"
                  "alert(here.childNodes.length + ',' + here.firstChild.nodeType);"
                  "</script></body></html>"),
             "2,t1,p;1,3");
}

} // namespace

int main() {
    test_serialize_to_string();
    test_serialize_namespaces();
    test_noscript_follows_the_documents_scripting_flag();
    REPORT("domparsing");
}
