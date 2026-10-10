//===- ctbrowser/unittests/unit/second_document.cpp - Second documents ----===//
//
// A second document: `createHTMLDocument` and `createDocument`.
//
// This pins down a model rather than a method: a second Document is a second
// `dom_bindings` over its own tree, sharing the realm, the atom table and the
// interface objects with the page's own. The comment above
// `adopt_interfaces_of` in lib/Shell/bindings/document/second_document.cpp
// records that design; these cases are the parts of it that would otherwise
// drift.
//
// The important case is the boundary between the two documents. A node of one
// document appended into the other is adopted (cloned into the other node
// slab), never misread: `pack(node_id)` is a slot in one slab, so the same
// number names a different node in another document.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

#include "check.hpp"
#include "dom_probe.hpp"

#include <string>
#include <vector>

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><head><title>the page</title></head><body><div id=here>x</div>
</body></html>)";

/// Checks \p expression against a fresh page per case, since many of these
/// mutate the document.
void is(const std::string & expression, const std::string & expected) {
    ctbrowser_test::is_in(page_html, expression, expected);
}

void test_create_html_document_builds_a_whole_document() {
    is("(function () {"
       " var d = document.implementation.createHTMLDocument('made');"
       " return d.title + ',' + d.documentElement.tagName + ',' +"
       "        d.head.tagName + ',' + d.body.tagName; })()",
       "made,HTML,HEAD,BODY");
    is("document.implementation.createHTMLDocument('made').nodeType", "9");
    // The argument's absence is observable: with none there is no <title>
    // element at all, and `undefined` counts as absence because the argument
    // is an optional DOMString (WebIDL). DOMImplementation-createHTMLDocument.js
    // tests it beside `null`, which becomes the string "null".
    is("document.implementation.createHTMLDocument()"
       ".getElementsByTagName('title').length",
       "0");
    is("document.implementation.createHTMLDocument(undefined)"
       ".getElementsByTagName('title').length",
       "0");
    is("document.implementation.createHTMLDocument(null).title", "null");
    // And it is not the page's document, which is the whole point.
    is("document.implementation.createHTMLDocument('made') === document", "false");
    is("(function () {"
       " var d = document.implementation.createHTMLDocument('made');"
       " return d.getElementById('here') === null; })()",
       "true");
    is("(function () {"
       " document.implementation.createHTMLDocument('made');"
       " return document.title; })()",
       "the page");
}

void test_a_made_document_shares_the_realms_interfaces() {
    // One HTMLDivElement per realm. The made document adopts the primary's
    // interface prototypes rather than building a second set, so an element it
    // creates is `instanceof` the constructor the page can name.
    is("document.implementation.createHTMLDocument('m').createElement('div')"
       " instanceof HTMLDivElement",
       "true");
    is("document.implementation.createHTMLDocument('m').body instanceof HTMLBodyElement", "true");
    is("(function () {"
       " var d = document.implementation.createHTMLDocument('m');"
       " return d.createElement('div').ownerDocument === d; })()",
       "true");
}

void test_a_made_document_can_be_built_up() {
    is("(function () {"
       " var d = document.implementation.createHTMLDocument('m');"
       " var p = d.createElement('p');"
       " p.setAttribute('id', 'mine');"
       " d.body.appendChild(p);"
       " return d.getElementById('mine').tagName + ',' + d.body.childNodes.length; })()",
       "P,1");
    is("(function () {"
       " var d = document.implementation.createHTMLDocument('m');"
       " d.title = 'changed';"
       " return d.title + ',' + document.title; })()",
       "changed,the page");
}

void test_a_made_document_has_no_browsing_context() {
    is("document.implementation.createHTMLDocument('m').defaultView === null", "true");
    is("document.implementation.createHTMLDocument('m').URL", "about:blank");
}

void test_create_document_makes_an_xml_document() {
    is("(function () {"
       " var d = document.implementation.createDocument(null, 'foo', null);"
       " return d.documentElement.tagName; })()",
       "foo");
    // An XML document is case-sensitive, so the name is not folded.
    is("document.implementation.createDocument(null, 'FooBar', null).documentElement.tagName",
       "FooBar");
    // `createDocument(null, "")` is a Document with no document element at all,
    // which is what append-on-Document.html builds.
    is("document.implementation.createDocument(null, '', null).documentElement === null", "true");
    is("(function () {"
       " var d = document.implementation.createDocument("
       "     'http://www.w3.org/1999/xhtml', 'html', null);"
       " return d.documentElement.namespaceURI; })()",
       "http://www.w3.org/1999/xhtml");
}

void test_a_node_of_one_document_is_adopted_by_the_other() {
    // Adoption, as the DOM requires. `pack(node_id)` is a slot and a
    // generation into one slab, so the same number names a different node in
    // a different document. `appendChild` runs "adopt node" first: node_from
    // clone-adopts the subtree across the two slabs and rebinds the page's
    // wrapper to the copy, so the node is moved into this document.
    // Not yet pinned: `document.getElementById('moved') === p` fails, because
    // the page hands out a second wrapper for the adopted copy.
    is("(function () {"
       " var d = document.implementation.createHTMLDocument('m');"
       " var p = d.createElement('p'); p.id = 'moved';"
       " document.body.appendChild(p);"
       " return (p.ownerDocument === document) + ',' + p.parentNode.tagName"
       "   + ',' + document.body.childNodes.length; })()",
       "true,BODY,4");
    is("(function () {"
       " var d = document.implementation.createHTMLDocument('m');"
       " return document.contains(d.body); })()",
       "false");
    // And a fabricated handle names nothing either, which it always meant to.
    is("document.contains({__node: 5})", "false");
}

} // namespace

int main() {
    test_create_html_document_builds_a_whole_document();
    test_a_made_document_shares_the_realms_interfaces();
    test_a_made_document_can_be_built_up();
    test_a_made_document_has_no_browsing_context();
    test_create_document_makes_an_xml_document();
    test_a_node_of_one_document_is_adopted_by_the_other();
    REPORT("second_document");
}
