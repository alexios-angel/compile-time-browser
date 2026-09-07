// A SECOND DOCUMENT - `createHTMLDocument` and `createDocument`.
//
// Its own file because what it pins down is a MODEL rather than a method: a
// second Document here is a second `dom_bindings` over its own tree, sharing
// the realm, the atom table and the interface objects with the page's own. The
// block above `adopt_interfaces_of` in lib/Shell/bindings/document.cpp is the
// decision; these cases are the parts of it that would otherwise drift.
//
// TWO OF THEM ASSERT THAT SOMETHING DOES NOT WORK, and those are the important
// ones. A node of one document handed to the other is REFUSED rather than
// misread, because `pack(node_id)` is a slot into one slab and the same number
// names a different node in a different document. A test that only checked the
// happy path would let that turn back into a silent wrong answer.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><head><title>the page</title></head><body><div id=here>x</div>
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

void test_create_html_document_builds_a_whole_document() {
    is("(function () {"
       " var d = document.implementation.createHTMLDocument('made');"
       " return d.title + ',' + d.documentElement.tagName + ',' +"
       "        d.head.tagName + ',' + d.body.tagName; })()",
       "made,HTML,HEAD,BODY");
    is("document.implementation.createHTMLDocument('made').nodeType", "9");
    // THE ARGUMENT'S ABSENCE IS OBSERVABLE: with none there is no <title>
    // element at all, and with `undefined` there is one holding "undefined".
    is("document.implementation.createHTMLDocument()"
       ".getElementsByTagName('title').length",
       "0");
    is("document.implementation.createHTMLDocument(undefined).title", "undefined");
    // AND IT IS NOT THE PAGE'S DOCUMENT, which is the whole point.
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
    // ONE HTMLDivElement in a realm. The made document adopts the primary's
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
    // An XML document is CASE-SENSITIVE, so the name is not folded.
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

void test_a_node_of_one_document_is_refused_by_the_other() {
    // THE HONEST FAILURE. `pack(node_id)` is a slot and a generation into ONE
    // slab, so the same number names a different node in a different document.
    // The primary's `handle_of` now checks that a wrapper is in ITS table, so a
    // foreign node reads as no node - a method that does nothing - rather than
    // as whatever happens to occupy that slot here.
    is("(function () {"
       " var d = document.implementation.createHTMLDocument('m');"
       " var p = d.createElement('p');"
       " var before = document.body.childNodes.length;"
       " document.body.appendChild(p);"
       " return before === document.body.childNodes.length; })()",
       "true");
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
    test_a_node_of_one_document_is_refused_by_the_other();
    REPORT("second_document");
}
