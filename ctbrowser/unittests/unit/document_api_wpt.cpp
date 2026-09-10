// THE DOCUMENT BINDING AGAINST dom/nodes - one case per rule the corpus found
// the other answer to, each named after the WPT file that asserts it.
//
// `new Document()`, importNode and adoptNode, the two DOM 6 walkers, and
// three one-line rules a clone, a textContent setter and getElementById had
// wrong. The pattern is unit/dom_nodes_wpt.cpp's: one expression against a
// fresh page, and whatever it logged.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><body>
<div id=box><span id=a></span>text<!--c--><span id=b></span></div>
<template id=tpl><b id=deep>x</b></template>
<svg xmlns:xlink='http://www.w3.org/1999/xlink'><use xlink:href='#t'></use></svg>
<p id=blank-id><i id=""></i></p>
</body></html>)";

[[nodiscard]] std::string answer(const std::string & expression) {
    browser page{browser_options{400, 300}};
    std::string html{page_html};
    const std::string tail = "<script>try { console.log(String(" + expression +
                             ")); } catch (e) { console.log('threw:' + e.name); }</script>";
    html.insert(html.find("</body>"), tail);
    page.load_html(html);
    const std::vector<std::string> & logged = page.bindings().console_output();
    if (logged.empty()) { return "<nothing logged: " + page.script_error() + ">"; }
    return logged.back();
}

void is(const std::string & expression, const std::string & expected) {
    const std::string got = answer(expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

// --- clone, textContent, getElementById ------------------------------------

void test_a_clone_keeps_attribute_namespaces_and_template_contents() {
    // Node-cloneNode-svg.html: the cloned <use>'s xlink:href is still in the
    // XLink namespace, and the <svg>'s xmlns:xlink in the XMLNS one.
    is("(function () { var c = document.querySelector('svg').cloneNode(true);"
       " return c.attributes[0].namespaceURI + '|' + c.firstElementChild.attributes[0].namespaceURI"
       " + '|' + c.firstElementChild.attributes[0].prefix; })()",
       "http://www.w3.org/2000/xmlns/|http://www.w3.org/1999/xlink|xlink");
    // HTML 4.12.3: a deep clone of a <template> clones its contents; a shallow
    // one does not.
    is("document.getElementById('tpl').cloneNode(true).content.childNodes.length", "1");
    is("document.getElementById('tpl').cloneNode(true).content.getElementById('deep').textContent",
       "x");
    is("document.getElementById('tpl').cloneNode(false).content.childNodes.length", "0");
}

void test_text_content_set_to_the_empty_string_makes_no_text_node() {
    // Node-textContent.html, "Element with children set to """: no child at all.
    is("(function () { var b = document.getElementById('box'); b.textContent = '';"
       " return b.firstChild + ',' + b.childNodes.length; })()",
       "null,0");
    is("(function () { var b = document.getElementById('box'); b.textContent = 'y';"
       " return b.childNodes.length + ',' + b.firstChild.data; })()",
       "1,y");
}

void test_get_element_by_id_with_the_empty_string_is_null() {
    // Document-getElementById.html: an element with id="" is not the answer.
    is("document.getElementById('')", "null");
    is("document.getElementById('blank-id').tagName", "P");
}

// --- new Document() ----------------------------------------------------------

void test_new_document_is_an_xml_document_with_no_browsing_context() {
    // Document-constructor.html, all five subtests.
    is("(function () { var d = new Document(); return (d instanceof Node) + ',' +"
       " (d instanceof Document) + ',' + (d instanceof XMLDocument) + ',' +"
       " (d.constructor === Document); })()",
       "true,true,false,true");
    is("(function () { var d = new Document(); return d.firstChild + ',' + d.lastChild + ',' +"
       " d.doctype + ',' + d.documentElement + ',' + d.childNodes.length; })()",
       "null,null,null,null,0");
    is("(function () { var d = new Document(); return d.location + ',' + d.URL + ',' +"
       " d.compatMode + ',' + d.characterSet + ',' + d.contentType; })()",
       "null,about:blank,CSS1Compat,UTF-8,application/xml");
    // createElement in an XML document: the name as written, the null
    // namespace, and so a plain Element.
    is("(function () { var d = new Document(); var e = d.createElement('DIV');"
       " return e.localName + ',' + e.namespaceURI + ',' + (e.constructor === Element); })()",
       "DIV,null,true");
    is("new Document().createElementNS('http://www.w3.org/1999/xhtml', 'a').constructor ==="
       " HTMLAnchorElement",
       "true");
    // ...and createDocument's is an XMLDocument, DOM 4.5.1.
    is("document.implementation.createDocument(null, 'x') instanceof XMLDocument", "true");
}

void test_an_element_appended_to_an_empty_document_becomes_its_root() {
    // Document-doctype.html "new Document()" and dom/common.js's setup.
    is("(function () { var d = new Document(); var h = d.createElement('html');"
       " var r = d.appendChild(h); return (r === h) + ',' + (d.documentElement === h) + ',' +"
       " (d.firstChild === h) + ',' + d.childNodes.length + ',' + d.doctype; })()",
       "true,true,true,1,null");
    // A second element is one too many - and a Text child never.
    is("(function () { var d = new Document(); d.appendChild(d.createElement('a'));"
       " try { d.appendChild(d.createElement('b')); } catch (e) { return e.name; } })()",
       "HierarchyRequestError");
    is("(function () { var d = new Document();"
       " try { d.append('text'); } catch (e) { return e.name; } })()",
       "HierarchyRequestError");
}

// --- importNode and adoptNode ------------------------------------------------

void test_import_node_copies_into_this_document() {
    // Document-importNode.html: the copy is ours, the original stays theirs,
    // and `deep` decides whether the children come.
    is("(function () { var doc = document.implementation.createHTMLDocument('T');"
       " var div = doc.body.appendChild(doc.createElement('div'));"
       " div.appendChild(doc.createElement('span'));"
       " var made = document.importNode(div);"
       " return (div.ownerDocument === doc) + ',' + (made.ownerDocument === document) + ',' +"
       " made.firstChild + ',' + (made !== div); })()",
       "true,true,null,true");
    is("(function () { var doc = document.implementation.createHTMLDocument('T');"
       " var div = doc.body.appendChild(doc.createElement('div'));"
       " div.appendChild(doc.createElement('span'));"
       " var made = document.importNode(div, true);"
       " return (made.firstChild.ownerDocument === document) + ',' + made.firstChild.tagName; })()",
       "true,SPAN");
    // The other direction works too, and a Document is refused.
    is("(function () { var doc = document.implementation.createHTMLDocument('T');"
       " return doc.importNode(document.getElementById('a')).ownerDocument === doc; })()",
       "true");
    is("(function () { try { document.importNode(document.implementation"
       ".createHTMLDocument('T')); } catch (e) { return e.name; } })()",
       "NotSupportedError");
    is("(function () { try { document.importNode(42); } catch (e) { return e.name; } })()",
       "TypeError");
}

void test_adopt_node_within_one_document_removes_and_returns() {
    // Document-adoptNode.html, the same-document half: the node is detached
    // and handed back, children intact.
    is("(function () { var a = document.getElementById('a');"
       " var r = document.adoptNode(a); return (r === a) + ',' + a.parentNode + ',' +"
       " (a.ownerDocument === document); })()",
       "true,null,true");
    is("(function () { var doc = document.implementation.createDocument(null, null, null);"
       " try { document.adoptNode(doc); } catch (e) { return e.name; } })()",
       "NotSupportedError");
    // Document-adoptNode-DocumentFragment-with-host.window.js: a shadow root.
    is("(function () { var root = document.createElement('div').attachShadow({mode: 'closed'});"
       " try { document.adoptNode(root); } catch (e) { return e.name; } })()",
       "HierarchyRequestError");
    // ACROSS DOCUMENTS IT IS REFUSED, by name: see install.cpp.
    is("(function () { var doc = document.implementation.createHTMLDocument('T');"
       " try { doc.adoptNode(document.getElementById('a')); } catch (e) { return e.name; } })()",
       "NotSupportedError");
}

// --- the two walkers -----------------------------------------------------------

void test_tree_walker_defaults_and_moves() {
    // Document-createTreeWalker.html: the optional arguments.
    is("(function () { try { document.createTreeWalker(); } catch (e) { return e.name; } })()",
       "TypeError");
    is("(function () { var w = document.createTreeWalker(document.body);"
       " return (w.root === document.body) + ',' + (w.currentNode === document.body) + ',' +"
       " w.whatToShow + ',' + w.filter + ',' + (w instanceof TreeWalker); })()",
       "true,true,4294967295,null,true");
    is("(function () { var f = function () {}; var w = document.createTreeWalker(document.body,"
       " 42, f); return w.whatToShow + ',' + (w.filter === f); })()",
       "42,true");
    // TreeWalker-basic.html's shape: elements only, in tree order, and back.
    is("(function () { var w = document.createTreeWalker(document.getElementById('box'),"
       " NodeFilter.SHOW_ELEMENT); var out = []; var n;"
       " while ((n = w.nextNode())) { out.push(n.id); }"
       " out.push(w.previousNode().id); out.push(w.parentNode().id); out.push(w.parentNode());"
       " return out.join(','); })()",
       "a,b,a,box,null");
    // Text and comments are one bit each in whatToShow.
    is("(function () { var w = document.createTreeWalker(document.getElementById('box'),"
       " NodeFilter.SHOW_TEXT | NodeFilter.SHOW_COMMENT);"
       " return w.firstChild().data + ',' + w.nextSibling().data + ',' + w.nextSibling() +"
       " ',' + w.lastChild(); })()",
       "text,c,null,null");
    // A filter: FILTER_SKIP looks through the node, FILTER_REJECT does not.
    is("(function () { var w = document.createTreeWalker(document.body, NodeFilter.SHOW_ELEMENT,"
       " function (n) { return n.id === 'box' ? NodeFilter.FILTER_SKIP : NodeFilter.FILTER_ACCEPT; "
       "});"
       " return w.firstChild().id; })()",
       "a");
    is("(function () { var w = document.createTreeWalker(document.body, NodeFilter.SHOW_ELEMENT,"
       " { acceptNode: function (n) { return n.id === 'box' ? 2 : 1; } });"
       " return w.firstChild().id; })()",
       "tpl");
    // Rooted at the document itself: the first node is <html> and its parent
    // is the document.
    is("(function () { var w = document.createTreeWalker(document);"
       " var first = w.nextNode(); return first.tagName + ',' + (w.parentNode() === document) +"
       " ',' + w.parentNode(); })()",
       "HTML,true,null");
    // A filter that re-enters its own walker is an InvalidStateError.
    is("(function () { var w; w = document.createTreeWalker(document.body, 0xFFFFFFFF,"
       " function () { w.nextNode(); return 1; });"
       " try { w.nextNode(); } catch (e) { return e.name; } })()",
       "InvalidStateError");
}

void test_node_iterator_walks_both_ways() {
    is("(function () { var i = document.createNodeIterator(document.getElementById('box'),"
       " NodeFilter.SHOW_ELEMENT); var out = []; var n;"
       " out.push(i.pointerBeforeReferenceNode, i.referenceNode.id);"
       " while ((n = i.nextNode())) { out.push(n.id); }"
       " out.push(i.pointerBeforeReferenceNode, i.previousNode().id, i.previousNode().id,"
       " i.previousNode().id, i.previousNode(), i instanceof NodeIterator);"
       " return out.join(','); })()",
       "true,box,box,a,b,false,b,a,box,null,true");
    is("NodeFilter.SHOW_ALL + ',' + NodeFilter.FILTER_REJECT + ',' + NodeFilter.SHOW_DOCUMENT",
       "4294967295,2,256");
}

} // namespace

int main() {
    test_a_clone_keeps_attribute_namespaces_and_template_contents();
    test_text_content_set_to_the_empty_string_makes_no_text_node();
    test_get_element_by_id_with_the_empty_string_is_null();
    test_new_document_is_an_xml_document_with_no_browsing_context();
    test_an_element_appended_to_an_empty_document_becomes_its_root();
    test_import_node_copies_into_this_document();
    test_adopt_node_within_one_document_removes_and_returns();
    test_tree_walker_defaults_and_moves();
    test_node_iterator_walks_both_ways();
    REPORT("document_api_wpt");
}
