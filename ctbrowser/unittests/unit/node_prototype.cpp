// THE METHODS ARE ON THE PROTOTYPES, not on every wrapper: what
// `Node.prototype.appendChild.call(x, y)`, `"insertBefore" in Node.prototype`,
// `appendChild.length` and `"moveBefore" in textNode` answer, and that a node
// of a SECOND document edits its own tree when its method runs through the
// shared prototype - see define_operation in lib/Shell/bindings/element/methods.cpp.
//
// The pattern is unit/dom_nodes_wpt.cpp's: one expression against a fresh
// page, and whatever it logged.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

[[nodiscard]] std::string answer(const std::string & expression) {
    browser page{browser_options{400, 300}};
    page.load_html("<!DOCTYPE html><html><body><div id=box><span id=a></span>text</div>"
                   "<canvas id=cv></canvas><script>try { console.log(String(" +
                   expression +
                   ")); } catch (e) { console.log('threw:' + e.name); }</script>"
                   "</body></html>");
    const std::vector<std::string> & logged = page.bindings().console_output();
    if (logged.empty()) { return "<nothing logged: " + page.script_error() + ">"; }
    return logged.back();
}

void is(const std::string & expression, const std::string & expected) {
    const std::string got = answer(expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

void test_operations_are_on_the_interface_prototypes() {
    is("typeof Node.prototype.insertBefore + ',' + typeof Element.prototype.querySelector + ',' +"
       " typeof Element.prototype.setAttribute + ',' + typeof CharacterData.prototype.remove",
       "function,function,function,function");
    // Own of the prototype, not of the wrapper, and not enumerable.
    is("(function () { var a = document.getElementById('a');"
       " return a.hasOwnProperty('appendChild') + ',' +"
       " Node.prototype.hasOwnProperty('appendChild') + ',' +"
       " Object.keys(Node.prototype).indexOf('appendChild'); })()",
       "false,true,-1");
    // The `.call` idiom the corpus writes everywhere.
    is("(function () { var box = document.getElementById('box');"
       " var made = document.createElement('i');"
       " Node.prototype.appendChild.call(box, made);"
       " return made.parentNode === box && box.lastChild === made; })()",
       "true");
}

void test_length_is_the_required_argument_count() {
    is("Node.prototype.appendChild.length + ',' + Node.prototype.insertBefore.length + ',' +"
       " Element.prototype.querySelector.length + ',' + Element.prototype.setAttributeNS.length"
       " + ',' + Element.prototype.append.length",
       "1,2,1,3,0");
}

void test_a_method_is_only_where_its_interface_is() {
    // ParentNode is not a mixin of CharacterData; ChildNode is.
    is("(function () { var t = document.getElementById('box').lastChild;"
       " return ('moveBefore' in t) + ',' + ('querySelector' in t) + ',' +"
       " ('getAttribute' in t) + ',' + ('remove' in t) + ',' + ('appendChild' in t); })()",
       "false,false,false,true,true");
    // The canvas three are HTMLCanvasElement's alone.
    is("('getContext' in document.getElementById('cv')) + ',' +"
       " ('getContext' in document.getElementById('a'))",
       "true,false");
    // The EventTarget trio is inherited, not own.
    is("(function () { var a = document.getElementById('a'); var hit = 0;"
       " a.addEventListener('x', function () { hit++; });"
       " a.dispatchEvent(new Event('x'));"
       " return hit + ',' + a.hasOwnProperty('addEventListener'); })()",
       "1,false");
}

void test_node_constants_and_compare_document_position() {
    is("Node.ELEMENT_NODE + ',' + Node.prototype.TEXT_NODE + ',' +"
       " document.getElementById('a').DOCUMENT_POSITION_CONTAINED_BY",
       "1,3,16");
    is("(function () { var box = document.getElementById('box'), a = box.firstChild,"
       " t = box.lastChild; return box.compareDocumentPosition(a) + ',' +"
       " a.compareDocumentPosition(box) + ',' + a.compareDocumentPosition(t) + ',' +"
       " t.compareDocumentPosition(a) + ',' + a.compareDocumentPosition(a) + ',' +"
       " box.hasChildNodes() + ',' + a.hasChildNodes(); })()",
       "20,10,4,2,0,true,false");
    is("(function () { var a = document.getElementById('a');"
       " var d = document.createElement('div');"
       " var r = a.compareDocumentPosition(d);"
       " return ((r & 1) !== 0) + ',' + ((r & 32) !== 0) + ',' +"
       " (a.compareDocumentPosition(document) & 10); })()",
       "true,true,10");
}

void test_a_second_document_edits_its_own_tree() {
    // The prototype natives are shared; the receiver decides which document
    // runs. Before, the primary's copy ran with the other tree's node ids.
    is("(function () { var doc = document.implementation.createHTMLDocument('t');"
       " var p = doc.createElement('p'); p.setAttribute('id', 'made');"
       " doc.body.appendChild(p); p.append('hi');"
       " return doc.body.innerHTML + '|' + doc.getElementById('made').textContent + '|' +"
       " (document.getElementById('made') === null) + '|' + doc.body.querySelector('p').id +"
       " '|' + doc.body.firstChild.getAttribute('id'); })()",
       "<p id=\"made\">hi</p>|hi|true|made|made");
}

void test_document_operations_are_on_document_prototype() {
    // node-creation-realm.html: `Document.prototype.createTextNode.apply(doc,
    // ...)` runs the receiver's own method; a receiver that is no document is
    // an illegal invocation; Node's operations are not duplicated onto it.
    is(R"JS((function () {
        var doc = document.implementation.createHTMLDocument('t');
        var t = Document.prototype.createTextNode.apply(doc, ['x']);
        var d = DOMImplementation.prototype.createHTMLDocument.call(document.implementation, 'u');
        var threw = false;
        try { Document.prototype.createElement.call({}, 'div'); } catch (e) { threw = e.name; }
        return [t.ownerDocument === doc, t.nodeType, d.title, threw,
                Object.getOwnPropertyNames(Document.prototype).includes('appendChild'),
                typeof Document.prototype.getElementById].join();
    })())JS",
       "true,3,u,TypeError,false,function");
}

} // namespace

int main() {
    test_operations_are_on_the_interface_prototypes();
    test_length_is_the_required_argument_count();
    test_a_method_is_only_where_its_interface_is();
    test_node_constants_and_compare_document_position();
    test_a_second_document_edits_its_own_tree();
    test_document_operations_are_on_document_prototype();
    REPORT("node_prototype");
}
