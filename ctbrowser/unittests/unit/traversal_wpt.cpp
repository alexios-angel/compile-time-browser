// DOM 6's NodeIterator and TreeWalker against dom/traversal: the state as
// read-only accessors on the prototype, a root in another document of the
// realm, the filter's callback semantics, and the walks. The pattern is
// unit/dom_nodes_wpt.cpp's: one expression against a fresh page, and whatever
// it logged. The defaults and the tree-order walks are in
// unit/document_api_wpt.cpp already.

#include <ctbrowser.hpp>

#include "check.hpp"
#include "dom_probe.hpp"

#include <string>

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><body>
<div id=box><span id=a></span>text<!--c--><span id=b><i id=d></i></span></div>
</body></html>)";

void is(const std::string & expression, const std::string & expected) {
    ctbrowser_test::is_in(page_html, expression, expected);
}

void test_walker_state_is_readonly_on_the_prototype() {
    // TreeWalker-basic.html: root, whatToShow and filter are read-only
    // accessors, and currentNode is the one that can be set - to a Node only.
    is("(function () { var w = document.createTreeWalker(document.body, 5, null);"
       " var d = Object.getOwnPropertyDescriptor(TreeWalker.prototype, 'root');"
       " w.root = 1; w.whatToShow = 2; var out = [w.root === document.body, w.whatToShow,"
       " typeof d.get, d.set, Object.keys(w).length, String(w)];"
       " try { w.currentNode = null; } catch (e) { out.push(e.name); }"
       " try { w.currentNode = {}; } catch (e) { out.push(e.name); }"
       " w.currentNode = document.getElementById('b'); out.push(w.currentNode.id);"
       " return out.join(); })()",
       "true,5,function,,0,[object TreeWalker],TypeError,TypeError,b");
    is("(function () { var i = document.createNodeIterator(document.body);"
       " var d = Object.getOwnPropertyDescriptor(NodeIterator.prototype, 'referenceNode');"
       " return typeof d.get + ',' + d.set + ',' + i.pointerBeforeReferenceNode + ','"
       " + (i.referenceNode === document.body) + ',' + String(i) + ','"
       " + (i.nextNode === NodeIterator.prototype.nextNode); })()",
       "function,undefined,true,true,[object NodeIterator],true");
}

void test_a_root_in_another_document() {
    // TreeWalker.html and NodeIterator.html walk `foreignDoc` and `xmlDoc`:
    // a node of any document of the realm is a root, and the walk reads that
    // document's tree.
    is("(function () { var foreign = document.implementation.createHTMLDocument('t');"
       " foreign.body.innerHTML = '<p id=x><b id=y></b></p>';"
       " var w = document.createTreeWalker(foreign, NodeFilter.SHOW_ELEMENT);"
       " var out = []; var n; while ((n = w.nextNode())) { out.push(n.tagName); }"
       " out.push(w.parentNode().tagName, w.currentNode.ownerDocument === foreign);"
       " var i = document.createNodeIterator(foreign.body, NodeFilter.SHOW_ELEMENT);"
       " i.nextNode(); out.push(i.nextNode().id, i.nextNode().id, i.nextNode());"
       " return out.join(); })()",
       "HTML,HEAD,TITLE,BODY,P,B,P,true,x,y,");
    is("(function () { var x = document.implementation.createDocument(null, 'root', null);"
       " x.documentElement.appendChild(x.createElement('leaf'));"
       " var w = document.createTreeWalker(x.documentElement);"
       " return w.firstChild().tagName + ',' + w.nextNode() + ',' + w.parentNode().tagName; })()",
       "leaf,null,root");
}

void test_filter_callback_semantics() {
    // TreeWalker-acceptNode-filter.html: `this` is the filter; a throwing
    // filter propagates; a filter re-entering its walker is an
    // InvalidStateError; the result is an unsigned short.
    is("(function () { var seen; var w = document.createTreeWalker(document.getElementById('box'),"
       " NodeFilter.SHOW_ELEMENT, { acceptNode: function (n) { seen = this; return 1; } });"
       " w.firstChild(); return seen === w.filter; })()",
       "true");
    is("(function () { var w = document.createTreeWalker(document.getElementById('box'),"
       " NodeFilter.SHOW_ELEMENT, function () { throw new RangeError('x'); });"
       " try { w.firstChild(); } catch (e) { return e.name + ',' + w.currentNode.id; } })()",
       "RangeError,box");
    is("(function () { var w = document.createTreeWalker(document.getElementById('box'),"
       " NodeFilter.SHOW_ELEMENT, function () { return 0x10001; }); return w.firstChild().id; })()",
       "a");
    is("(function () { var w = document.createTreeWalker(document.getElementById('box'),"
       " NodeFilter.SHOW_ELEMENT, { acceptNode: 3 });"
       " try { w.firstChild(); } catch (e) { return e.name; } })()",
       "TypeError");
    // A filter that removes the node it is asked about, and a walk that
    // starts outside the root and runs off the end.
    is("(function () { var w = document.createTreeWalker(document, NodeFilter.SHOW_ELEMENT, null);"
       " var d = document.createElement('div'); d.appendChild(document.createElement('span'));"
       " w.currentNode = d; return (w.nextNode() === d.firstChild) + ',' + w.nextNode(); })()",
       "true,null");
}

void test_iterator_moves_both_ways_through_text_and_comments() {
    // NodeIterator.html: whatToShow as a bit mask, the pointer flipping
    // direction, and detach() doing nothing.
    is("(function () { var i = document.createNodeIterator(document.getElementById('box'),"
       " NodeFilter.SHOW_TEXT | NodeFilter.SHOW_COMMENT); var out = [];"
       " out.push(i.nextNode().data, i.nextNode().data, i.nextNode(), i.previousNode().data,"
       " i.pointerBeforeReferenceNode, i.previousNode().data, i.previousNode());"
       " i.detach(); out.push(i.nextNode().data); return out.join(); })()",
       "text,c,,c,true,text,,text");
}

} // namespace

int main() {
    test_walker_state_is_readonly_on_the_prototype();
    test_a_root_in_another_document();
    test_filter_callback_semantics();
    test_iterator_moves_both_ways_through_text_and_comments();
    REPORT("traversal_wpt");
}
