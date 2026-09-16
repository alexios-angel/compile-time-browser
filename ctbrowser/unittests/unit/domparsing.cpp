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
             "<div xmlns=\"http://www.w3.org/1999/xhtml\" x=\"a&#9;b\"/>;"
             "function");
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
    test_noscript_follows_the_documents_scripting_flag();
    REPORT("domparsing");
}
