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
#include "dom_probe.hpp"
#include <string>
#include <string_view>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser_test::said;

namespace {

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

// XMLSerializer-serializeToString.html's three: ONE prefix index for the
// whole serialisation (a sibling's generated prefix continues the count), a
// null-namespace `xmlns` attribute read as the default namespace declaration
// it spells, and an Attr serialised as the empty string.
void test_serialize_prefixes_and_declarations() {
    CHECK_EQ(said("<html><body><script>"
                  "var s = new XMLSerializer();"
                  "var r = new DOMParser().parseFromString('<root><c1/><c2/></root>',"
                  " 'application/xml').documentElement;"
                  "r.firstChild.setAttributeNS('u1', 'a', '1');"
                  "r.lastChild.setAttributeNS('u2', 'b', '2');"
                  "var p = new DOMParser().parseFromString('<package/>', 'application/xml')"
                  ".documentElement;"
                  "p.setAttribute('xmlns', 'urn:o');"
                  "alert(s.serializeToString(r));"
                  "alert(s.serializeToString(p) + '|' + s.serializeToString("
                  "document.createAttribute('a')) + '|');"
                  "</script></body></html>"),
             "<root><c1 xmlns:ns1=\"u1\" ns1:a=\"1\"/><c2 xmlns:ns2=\"u2\" ns2:b=\"2\"/></root>;"
             "<package/>||");
}

// XML 1.0 [75]: a PUBLIC identifier with no system literal after it is
// ill-formed, so the document carries the parser's error element; with one -
// even an empty one - it parses.
void test_an_external_id_needs_its_system_literal() {
    CHECK_EQ(said("<html><body><script>"
                  "var p = new DOMParser();"
                  "var bad = p.parseFromString('<!DOCTYPE html PUBLIC \"x\"><html/>',"
                  " 'application/xml');"
                  "var good = p.parseFromString('<!DOCTYPE html PUBLIC \"x\" \"\"><html/>',"
                  " 'application/xml');"
                  "alert(bad.getElementsByTagName('parsererror').length + ','"
                  " + good.getElementsByTagName('parsererror').length);"
                  "</script></body></html>"),
             "1,0");
}

// createContextualFragment on a range in a DETACHED Text node: no parent
// element, so the context is a new <body> - and the fragment's script runs
// when the fragment is inserted, not before.
void test_a_contextual_fragment_without_a_context_element() {
    CHECK_EQ(
        said("<html><body><script>"
             "var r = document.createRange();"
             "r.selectNodeContents(document.createTextNode('?'));"
             "var f = r.createContextualFragment('<b>x</b><script>window.ran = 1</scr' + 'ipt>');"
             "alert(f.firstChild.nodeName + ',' + (typeof window.ran));"
             "document.body.appendChild(f);"
             "alert(window.ran);"
             "</script></body></html>"),
        "B,undefined;1");
}

// THE HTML PARTIAL UPDATES: each position lands where its name says, the safe
// spelling drops the script and an event handler the Unsafe one keeps (and
// runs only with runScripts), a sanitizer's removeElements takes the element
// and its text, a template's markup goes into its contents, and a node whose
// parent gives no context element - a plain fragment, a Document - refuses.
void test_positional_html_methods() {
    CHECK_EQ(
        said("<html><body><div id=t><i id=r>r</i></div><script>"
             "function threw(f){ try { f(); return 'ok'; } catch (e) { return e.name; } }"
             "var t = document.getElementById('t'), r = document.getElementById('r');"
             "window.ran = 0;"
             "r.beforeHTML('<b>1</b>'); r.afterHTML('<b>2</b>');"
             "t.prependHTML('<b>0</b>'); t.appendHTMLUnsafe('<b>3</b>');"
             "alert(t.textContent);"
             "t.appendHTML('<s onclick=x>4</s><script>ran++</scr' + 'ipt>', {runScripts: true});"
             "t.appendHTMLUnsafe('<u onclick=x>5</u><script>ran++</scr' + 'ipt>');"
             "t.appendHTMLUnsafe('<script>ran++</scr' + 'ipt>', {runScripts: true});"
             "alert(t.querySelectorAll('script').length + ',' + t.querySelector('s')"
             ".hasAttribute('onclick') + ',' + t.querySelector('u').hasAttribute('onclick')"
             " + ',' + ran);"
             "r.replaceWithHTML('<em>x</em><p>gone</p>', {sanitizer: {removeElements: ['p']}});"
             "alert(t.textContent.replace(/[^0-9x]/g, ''));"
             "var tp = document.createElement('template'); tp.appendHTML('<b>c</b>');"
             "alert(tp.childNodes.length + ',' + tp.content.textContent);"
             "var f = document.createDocumentFragment(); var k = f.appendChild("
             "document.createElement('i'));"
             "alert(threw(function(){ k.afterHTML('<b></b>'); }));"
             "</script></body></html>"),
        "01r23;2,false,true,1;01x2345;0,c;HierarchyRequestError");
}

} // namespace

int main() {
    test_positional_html_methods();
    test_serialize_prefixes_and_declarations();
    test_an_external_id_needs_its_system_literal();
    test_a_contextual_fragment_without_a_context_element();
    test_serialize_to_string();
    test_serialize_namespaces();
    test_noscript_follows_the_documents_scripting_flag();
    REPORT("domparsing");
}
