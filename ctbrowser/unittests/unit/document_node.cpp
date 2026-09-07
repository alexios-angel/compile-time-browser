// THE DOCUMENT IS A NODE, and here it barely was one.
//
// Twenty-two members of `Node` and `ParentNode` read `undefined` on `document`
// before this file existed - `contains`, `compareDocumentPosition`,
// `isConnected`, `textContent`, `append`, `createAttribute` and the rest - and
// a cluster of `dom/nodes` HARNESS_ERRORs stood behind them, because a harness
// error means the test could not START and every subtest in it is lost.
//
// WHAT MAKES THIS WORTH A FILE OF ITS OWN rather than more of bindings_basics:
// this tree builder has NO DOCUMENT NODE. `txn.root()` is the `<html>` element
// and `document` is a plain script object with no handle at all, so every case
// below is asking whether a Document that does not exist is being modelled
// consistently - not whether a method returns the right number. The block above
// `install_document_as_node` in lib/Shell/bindings/document.cpp is the decision
// these cases pin down; the ones that assert a THROW pin down its limits, which
// is the half that would otherwise drift into a quiet wrong answer.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <utility>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

// `xmlns:bar` ON THE ROOT is not decoration: `lookupNamespaceURI` and
// `lookupPrefix` on a Document are defined as the element algorithm run on
// documentElement, and with no declaration anywhere the only answer they could
// give is the one the element's own namespace already provides - which would
// pass without the attribute walk being written at all.
constexpr const char * page_html = R"(<!DOCTYPE html>
<html xmlns:bar="barURI"><body>
<div id=outer>
  <p id=p1>one</p>
</div>
</body></html>)";

// One expression against that page, and whatever it logged. A fresh page per
// case, exactly as unit/selectors.cpp does it: several of these MUTATE the
// document, and a case that changed the tree for the next one would report a
// failure in the wrong place.
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

// --- the document is a node at all -----------------------------------------

void test_the_document_reports_itself_as_a_node() {
    is("document.nodeType", "9");
    is("document.nodeName", "#document");
    is("document.nodeValue === null", "true");
    is("document.ownerDocument === null", "true");
    // A Document is ALWAYS connected: its root is itself.
    is("document.isConnected", "true");
    // NULL, NOT "" - the table in DOM 4.4 gives a Document a null textContent,
    // and "" would tell a page the document is empty.
    is("document.textContent === null", "true");
    is("typeof document.textContent", "object");
    // And the assignment is defined to do NOTHING, which is why it is an
    // accessor: a data property would let `document.textContent = 'x'` stick.
    is("(function () { document.textContent = 'x'; return document.textContent === null; })()",
       "true");
    is("document.getRootNode() === document", "true");
    // `composed` is accepted and ignored: it asks for the shadow-including
    // root, and there are no shadow trees for the two answers to differ in.
    is("document.getRootNode({ composed: true }) === document", "true");
    is("document.parentNode === null", "true");
    is("document.previousSibling === null && document.nextSibling === null", "true");
}

// --- the child list this engine models -------------------------------------

void test_the_document_has_exactly_one_child() {
    is("document.hasChildNodes()", "true");
    is("document.childNodes.length", "1");
    is("document.childNodes[0] === document.documentElement", "true");
    // firstChild and documentElement MUST be the same node or the model is
    // already inconsistent with itself.
    is("document.firstChild === document.documentElement", "true");
    is("document.lastChild === document.documentElement", "true");
    // THE ONE WRONG ANSWER IN THE BLOCK, asserted so that it is a decision
    // rather than a surprise: this page begins `<!DOCTYPE html>`, so a browser
    // reports a DocumentType here. `node_kind` has no `document_type`, so
    // `document.doctype` is null and firstChild is <html> instead. When a
    // doctype node arrives, this line is what says so.
    is("document.doctype === null", "true");
    is("document.firstChild.tagName", "HTML");
}

// --- contains --------------------------------------------------------------

void test_contains_and_the_document_containing_everything() {
    is("document.contains(document)", "true");
    is("document.contains(document.documentElement)", "true");
    is("document.contains(document.body)", "true");
    is("document.contains(document.getElementById('p1'))", "true");
    is("document.contains(document.getElementById('p1').firstChild)", "true");
    // NULL IS FALSE, NOT A THROW. `Node-contains.html` opens with exactly this
    // assertion for every one of its twenty-three nodes, so a throw here loses
    // the whole file.
    is("document.contains(null)", "false");
    is("document.contains(undefined)", "false");
    is("document.contains(document.createElement('div'))", "false");
    // A node that WAS in the page and is not any more.
    is("(function () { var p = document.getElementById('p1'); p.remove(); "
       "return document.contains(p); })()",
       "false");
}

// --- compareDocumentPosition, bit by bit -----------------------------------

void test_compare_document_position_reports_the_real_bitmask() {
    is("document.DOCUMENT_POSITION_DISCONNECTED", "1");
    is("document.DOCUMENT_POSITION_PRECEDING", "2");
    is("document.DOCUMENT_POSITION_FOLLOWING", "4");
    is("document.DOCUMENT_POSITION_CONTAINS", "8");
    is("document.DOCUMENT_POSITION_CONTAINED_BY", "16");
    is("document.DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC", "32");
    // "If other and reference are the same object, return zero."
    is("document.compareDocumentPosition(document)", "0");
    // Everything in the page is CONTAINED_BY the document and FOLLOWS it.
    is("document.compareDocumentPosition(document.body)", "20");
    is("document.compareDocumentPosition(document.getElementById('p1')) & "
       "document.DOCUMENT_POSITION_CONTAINED_BY",
       "16");
    is("document.compareDocumentPosition(document.getElementById('p1')) & "
       "document.DOCUMENT_POSITION_FOLLOWING",
       "4");
    // ...and never the other two bits, which are unreachable by construction:
    // nothing is an ancestor of the document and nothing precedes it.
    is("document.compareDocumentPosition(document.body) & "
       "(document.DOCUMENT_POSITION_CONTAINS | document.DOCUMENT_POSITION_PRECEDING)",
       "0");
    // A detached node is DISCONNECTED, IMPLEMENTATION_SPECIFIC and - because
    // the document is first in every order this engine could pick - FOLLOWING.
    is("document.compareDocumentPosition(document.createElement('div'))", "37");
    // CONSISTENT, which is the only thing the specification asks of the
    // direction: the same pair answers the same way twice.
    is("(function () { var d = document.createElement('div'); "
       "return document.compareDocumentPosition(d) === document.compareDocumentPosition(d); })()",
       "true");
    is("document.compareDocumentPosition(null)", "threw:TypeError");
}

// --- namespaces ------------------------------------------------------------

void test_lookup_namespace_uri_and_its_two_siblings() {
    // A Document delegates all three to documentElement, and <html> is in the
    // XHTML namespace with no prefix - so the null prefix finds it, and the
    // empty string is the null prefix.
    is("document.lookupNamespaceURI(null)", "http://www.w3.org/1999/xhtml");
    is("document.lookupNamespaceURI('')", "http://www.w3.org/1999/xhtml");
    // The declaration on the root, read off the attribute's qualified name -
    // `struct attribute` has nowhere to put a namespace, so `xmlns:bar` as
    // WRITTEN is the whole of the evidence.
    is("document.lookupNamespaceURI('bar')", "barURI");
    is("document.lookupNamespaceURI('nope') === null", "true");
    // `xml` and `xmlns` are bound at every element with no declaration saying
    // so - they are reserved, and nothing in the tree could answer them.
    is("document.lookupNamespaceURI('xml')", "http://www.w3.org/XML/1998/namespace");
    is("document.lookupNamespaceURI('xmlns')", "http://www.w3.org/2000/xmlns/");
    is("document.lookupPrefix('barURI')", "bar");
    is("document.lookupPrefix('http://www.example.org/') === null", "true");
    is("document.lookupPrefix(null) === null", "true");
    // isDefaultNamespace compares against what the NULL prefix locates, so a
    // document whose root is in the XHTML namespace answers false to null.
    is("document.isDefaultNamespace('http://www.w3.org/1999/xhtml')", "true");
    is("document.isDefaultNamespace(null)", "false");
    is("document.isDefaultNamespace('')", "false");
    is("document.isDefaultNamespace('barURI')", "false");
}

// --- the ParentNode insertion methods, every one of which refuses ----------

void test_append_and_prepend_on_the_document() {
    // "A Document may have at most one element child", and this one already has
    // <html>. Both of these are the SPECIFIED answer rather than this engine's
    // limitation, which is why they name a DOMException the DOM does put here.
    is("document.append(document.createElement('x'))", "threw:HierarchyRequestError");
    is("document.prepend(document.createElement('x'))", "threw:HierarchyRequestError");
    // A string argument becomes a Text node - which a Document may never have.
    is("document.append('text')", "threw:HierarchyRequestError");
    is("document.prepend('text')", "threw:HierarchyRequestError");
    // No arguments is a documented no-op, and it is the one insertion on this
    // document that succeeds.
    is("String(document.append()) + ',' + document.childNodes.length", "undefined,1");
    // EVERY ARGUMENT IS CHECKED BEFORE ANYTHING IS INSERTED, which is what
    // `append-on-Document.html` measures: the child list is untouched after.
    is("(function () { try { document.append(document.createElement('x'), "
       "document.createElement('y')); } catch (e) { return e.name + ',' + "
       "document.childNodes.length; } return 'no throw'; })()",
       "HierarchyRequestError,1");
    is("document.appendChild(document.createElement('x'))", "threw:HierarchyRequestError");
    // A Comment is the one child the DOM permits here and this engine cannot
    // hold - there is no node above <html> for a sibling of it to hang from.
    // NotSupportedError, not HierarchyRequestError: no specification puts one
    // here, so the name cannot be read as a claim about the hierarchy.
    is("document.appendChild(document.createComment('c'))", "threw:NotSupportedError");
    is("document.replaceChildren()", "threw:NotSupportedError");
    is("document.insertBefore(document.createElement('x'), document.documentElement)",
       "threw:HierarchyRequestError");
    // "If child is non-null and its parent is not parent, throw NotFoundError"
    // - and it comes BEFORE the check on what is being inserted.
    is("document.insertBefore(document.createElement('x'), document.body)", "threw:NotFoundError");
    is("document.removeChild(document.body)", "threw:NotFoundError");
    is("document.removeChild(document.documentElement)", "threw:NotSupportedError");
    is("document.replaceChild(document.createElement('x'), document.body)", "threw:NotFoundError");
    is("document.cloneNode()", "threw:NotSupportedError");
    is("document.isSameNode(document)", "true");
    is("document.isSameNode(document.body)", "false");
    is("document.isEqualNode(document)", "true");
    is("document.isEqualNode(document.body)", "false");
}

// --- normalize --------------------------------------------------------------

void test_normalize_merges_the_text_nodes_below_the_document() {
    // Three text nodes and an empty one, which is the case the specification
    // orders: the empty node is removed FIRST, so it does not stop the two
    // around it being joined.
    is("(function () { var d = document.createElement('div'); "
       "d.appendChild(document.createTextNode('a')); "
       "d.appendChild(document.createTextNode('')); "
       "d.appendChild(document.createTextNode('b')); "
       "document.body.appendChild(d); document.normalize(); "
       "return d.childNodes.length + ',' + d.textContent; })()",
       "1,ab");
    // A run broken by an element is two runs, not one.
    is("(function () { var d = document.createElement('div'); "
       "d.appendChild(document.createTextNode('a')); "
       "d.appendChild(document.createElement('br')); "
       "d.appendChild(document.createTextNode('b')); "
       "document.body.appendChild(d); document.normalize(); "
       "return d.childNodes.length; })()",
       "3");
}

// --- createAttribute, and the name rule that is worth more than the object --

void test_create_attribute_and_its_name_rule() {
    // THE EMPTY STRING IS THE ONE NAME THAT THROWS. `dom/nodes/productions.js`
    // lists exactly one invalid name and thirteen valid ones, and every one of
    // the thirteen is refused by the XML `Name` production - an attribute name
    // is measured by whether it survives being written into a start tag, and
    // nothing below would fail to.
    is("document.createAttribute('')", "threw:InvalidCharacterError");
    is("document.createAttribute('a b')", "threw:InvalidCharacterError");
    is("document.createAttribute('a>b')", "threw:InvalidCharacterError");
    is("document.createAttribute('a=b')", "threw:InvalidCharacterError");
    // productions.js' own list, minus the two whose quoting says nothing about
    // the rule. The expected answer is the name ASCII-lowercased, which is what
    // an HTML document does to it.
    for (const auto & [written, expected] :
         std::vector<std::pair<const char *, const char *>>{{"x", "x"},
                                                            {"X", "x"},
                                                            {":", ":"},
                                                            {"a:0", "a:0"},
                                                            {"invalid^Name", "invalid^name"},
                                                            {"0", "0"},
                                                            {"0:a", "0:a"},
                                                            {":a", ":a"},
                                                            {"x:y:x", "x:y:x"},
                                                            {"~", "~"},
                                                            {"'", "'"}}) {
        is(std::string{"document.createAttribute(\""} + written + "\").name", expected);
    }
    // AN HTML DOCUMENT LOWERCASES, and name, nodeName and localName are the
    // same string for a createAttribute attribute - there is no prefix split.
    is("document.createAttribute('TITLE').name", "title");
    is("document.createAttribute('TITLE').nodeName", "title");
    is("document.createAttribute('TITLE').localName", "title");
    // A DOMString: null asks for an attribute called "null".
    is("document.createAttribute(null).name", "null");
    is("document.createAttribute(undefined).name", "undefined");
    // The rest of what `dom/nodes/attributes.js`'s attr_is reads off one.
    is("document.createAttribute('x').value", "");
    is("document.createAttribute('x').nodeValue", "");
    is("document.createAttribute('x').textContent", "");
    is("document.createAttribute('x').nodeType", "2");
    is("document.createAttribute('x').specified", "true");
    is("document.createAttribute('x').prefix === null", "true");
    is("document.createAttribute('x').namespaceURI === null", "true");
    is("document.createAttribute('x').ownerElement === null", "true");
    // ONE STRING BEHIND THREE SPELLINGS: writing `value` has to be visible
    // through `nodeValue` and `textContent`, which three data properties would
    // not have been.
    is("(function () { var a = document.createAttribute('x'); a.value = 'v'; "
       "return a.nodeValue + ',' + a.textContent; })()",
       "v,v");
    // createAttributeNS keeps its case, splits the prefix, and enforces the
    // three namespace rules createElementNS does.
    is("document.createAttributeNS('http://www.example.org/', 'p:Local').localName", "Local");
    is("document.createAttributeNS('http://www.example.org/', 'p:Local').prefix", "p");
    is("document.createAttributeNS('http://www.example.org/', 'p:Local').name", "p:Local");
    is("document.createAttributeNS('http://www.example.org/', 'p:Local').namespaceURI",
       "http://www.example.org/");
    is("document.createAttributeNS(null, 'p:Local')", "threw:NamespaceError");
    is("document.createAttributeNS('http://www.example.org/', 'xmlns')", "threw:NamespaceError");
    is("document.createAttributeNS('http://www.w3.org/2000/xmlns/', 'xmlns').name", "xmlns");
    // The NAME is validated before the namespace, so a bad name in the XMLNS
    // namespace is an InvalidCharacterError rather than the NamespaceError its
    // namespace would otherwise earn.
    is("document.createAttributeNS('http://www.w3.org/2000/xmlns/', 'a b')",
       "threw:InvalidCharacterError");
}

} // namespace

int main() {
    test_the_document_reports_itself_as_a_node();
    test_the_document_has_exactly_one_child();
    test_contains_and_the_document_containing_everything();
    test_compare_document_position_reports_the_real_bitmask();
    test_lookup_namespace_uri_and_its_two_siblings();
    test_append_and_prepend_on_the_document();
    test_normalize_merges_the_text_nodes_below_the_document();
    test_create_attribute_and_its_name_rule();
    REPORT("document_node");
}
