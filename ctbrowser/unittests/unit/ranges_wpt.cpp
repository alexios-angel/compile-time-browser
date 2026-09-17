// DOM 5's Range and StaticRange against dom/ranges - one case per algorithm
// of DOM 5.5, each on a tiny tree, named after the WPT file that asserts it.
// The pattern is unit/dom_nodes_wpt.cpp's: one expression against a fresh
// page, and whatever it logged.

#include <ctbrowser.hpp>

#include "check.hpp"
#include "dom_probe.hpp"

#include <string>

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><body>
<div id=box><p id=p1>Abcd</p><p id=p2>Efgh</p><!--c--><span id=s>ij</span></div>
</body></html>)";

void is(const std::string & expression, const std::string & expected) {
    ctbrowser_test::is_in(page_html, expression, expected);
}

// --- the attributes and the setters --------------------------------------------

void test_a_range_is_readonly_accessors_on_the_prototype() {
    // Range-attributes.html: a fresh range sits at (document, 0), collapsed.
    is("(function () { var r = document.createRange(); return (r.startContainer === document)"
       " + ',' + r.startOffset + ',' + (r.endContainer === document) + ',' + r.endOffset + ','"
       " + r.collapsed + ',' + (r instanceof Range) + ',' + (r instanceof AbstractRange)"
       " + ',' + Object.prototype.toString.call(r); })()",
       "true,0,true,0,true,true,true,[object Range]");
    // The attributes are accessors on the prototype, not own data.
    is("(function () { var r = new Range(); var d = Object.getOwnPropertyDescriptor("
       "Range.prototype, 'startContainer'); return (typeof d.get) + ',' + d.set + ','"
       " + Object.keys(r).length; })()",
       "function,undefined,0");
}

void test_set_start_and_end_follow_each_other() {
    // Range-set.html: setStart past the end drags the end along; setEnd before
    // the start drags the start; a doctype is an InvalidNodeTypeError and an
    // offset past the length an IndexSizeError.
    is("(function () { var p1 = document.getElementById('p1'), p2 = document.getElementById('p2');"
       " var r = document.createRange(); r.setStart(p1.firstChild, 1); r.setEnd(p1.firstChild, 3);"
       " r.setStart(p2.firstChild, 2); var a = r.endContainer === p2.firstChild && r.endOffset === "
       "2;"
       " r.setEnd(p1.firstChild, 0); var b = r.startContainer === p1.firstChild && r.startOffset "
       "=== 0;"
       " var out = [a, b];"
       " try { r.setStart(document.doctype, 0); } catch (e) { out.push(e.name); }"
       " try { r.setStart(p1.firstChild, 5); } catch (e) { out.push(e.name); }"
       " try { r.setStart(p1, 2); } catch (e) { out.push(e.name); }"
       " try { r.setStart({}, 0); } catch (e) { out.push(e.name); }"
       " return out.join(); })()",
       "true,true,InvalidNodeTypeError,IndexSizeError,IndexSizeError,TypeError");
    // ...and a node in another tree moves both boundaries there.
    is("(function () { var r = document.createRange(); var d = document.createElement('div');"
       " d.textContent = 'xy'; r.setStart(d.firstChild, 1);"
       " return (r.endContainer === d.firstChild) + ',' + r.endOffset + ',' + r.collapsed; })()",
       "true,1,true");
    // setStartBefore/After, setEndBefore/After: the parent and the index;
    // no parent is an InvalidNodeTypeError.
    is("(function () { var box = document.getElementById('box'), p2 = "
       "document.getElementById('p2');"
       " var r = document.createRange(); r.setStartBefore(p2); r.setEndAfter(p2);"
       " var out = [r.startContainer === box, r.startOffset, r.endOffset];"
       " r.setStartAfter(p2); r.setEndBefore(p2); out.push(r.startOffset, r.endOffset);"
       " try { r.setStartBefore(document); } catch (e) { out.push(e.name); }"
       " return out.join(); })()",
       "true,1,2,1,1,InvalidNodeTypeError");
}

void test_select_collapse_and_common_ancestor() {
    // Range-selectNode.html, Range-collapse.html, Range-commonAncestorContainer.html.
    is("(function () { var box = document.getElementById('box'), p2 = "
       "document.getElementById('p2');"
       " var r = document.createRange(); r.selectNode(p2);"
       " var out = [r.startContainer === box, r.startOffset, r.endOffset];"
       " r.selectNodeContents(p2.firstChild); out.push(r.startContainer === p2.firstChild,"
       " r.startOffset, r.endOffset);"
       " r.setStart(document.getElementById('p1').firstChild, 1); "
       "out.push(r.commonAncestorContainer.id);"
       " r.collapse(false); out.push(r.collapsed, r.startOffset);"
       " r.selectNodeContents(box); r.collapse(true); out.push(r.endOffset);"
       " try { r.selectNodeContents(document.doctype); } catch (e) { out.push(e.name); }"
       " var c = r.cloneRange(); out.push(c.startContainer === box, c !== r);"
       " return out.join(); })()",
       "true,1,2,true,0,4,box,true,4,0,InvalidNodeTypeError,true,true");
}

// --- the four comparisons -------------------------------------------------------

void test_comparisons_over_two_documents_and_an_attr() {
    // Range-comparePoint.html, Range-isPointInRange.html, Range-intersectsNode.html,
    // Range-compareBoundaryPoints.html: the answers and the errors.
    is("(function () { var box = document.getElementById('box');"
       " var p1 = document.getElementById('p1'), p2 = document.getElementById('p2');"
       " var r = document.createRange(); r.setStart(p1.firstChild, 1); r.setEnd(p2.firstChild, 1);"
       " var o = [r.comparePoint(box, 0), r.comparePoint(p1.firstChild, 2), r.comparePoint(box, 3),"
       "   r.isPointInRange(p2, 0), r.isPointInRange(box, 0), r.intersectsNode(p1),"
       "   r.intersectsNode(document.getElementById('s')), r.intersectsNode(box)];"
       " var s = document.createRange(); s.selectNodeContents(box);"
       " o.push(r.compareBoundaryPoints(Range.START_TO_START, s),"
       "   r.compareBoundaryPoints(Range.END_TO_START, s), s.compareBoundaryPoints(3, r));"
       " try { r.compareBoundaryPoints(4, s); } catch (e) { o.push(e.name); }"
       " try { r.compareBoundaryPoints(0, {}); } catch (e) { o.push(e.name); }"
       " return o.join(); })()",
       "-1,0,1,true,false,true,false,true,1,-1,-1,NotSupportedError,TypeError");
    // Another document's node is another tree: false, WrongDocumentError, false.
    is("(function () { var foreign = document.implementation.createHTMLDocument('x');"
       " var r = document.createRange(); r.selectNodeContents(document.body);"
       " var o = [r.isPointInRange(foreign.body, 0), r.intersectsNode(foreign.body)];"
       " try { r.comparePoint(foreign.body, 0); } catch (e) { o.push(e.name); }"
       " var f = foreign.createRange(); f.selectNodeContents(foreign.body);"
       " try { r.compareBoundaryPoints(0, f); } catch (e) { o.push(e.name); }"
       " o.push(f.startContainer === foreign.body, f.endOffset);"
       " return o.join(); })()",
       "false,false,WrongDocumentError,WrongDocumentError,true,0");
    // Range-attribute-nodes.html: an Attr is a node of length 0 rooted at itself.
    is("(function () { var a = document.getElementById('box').getAttributeNode('id');"
       " var r = document.createRange(); r.selectNodeContents(a);"
       " var o = [r.startContainer === a, r.endOffset, r.collapsed, r.toString() === ''];"
       " try { r.setStart(a, 1); } catch (e) { o.push(e.name); }"
       " var d = document.createRange(); d.selectNodeContents(document.body);"
       " o.push(d.isPointInRange(a, 0), d.intersectsNode(a));"
       " try { d.comparePoint(a, 0); } catch (e) { o.push(e.name); }"
       " try { r.insertNode(document.createElement('b')); } catch (e) { o.push(e.name); }"
       " o.push(r.extractContents().childNodes.length, r.cloneContents().nodeType);"
       " return o.join(); })()",
       "true,0,true,true,IndexSizeError,false,false,WrongDocumentError,HierarchyRequestError,0,11");
}

// --- the mutations --------------------------------------------------------------

void test_delete_extract_and_clone_contents() {
    // Range-deleteContents.html: across two text nodes the partial ends are
    // cut, the contained nodes go, and the range collapses to the new point.
    is("(function () { var box = document.getElementById('box');"
       " var p1 = document.getElementById('p1'), p2 = document.getElementById('p2');"
       " var r = document.createRange(); r.setStart(p1.firstChild, 1); r.setEnd(p2.firstChild, 1);"
       " r.deleteContents(); return box.innerHTML + '|' + (r.startContainer === box) + "
       "r.startOffset"
       " + r.collapsed; })()",
       "<p id=\"p1\">A</p><p id=\"p2\">fgh</p><!--c--><span id=\"s\">ij</span>|true1true");
    // Range-extractContents.html: the same as a fragment, partial nodes
    // cloned shallowly around their cut text.
    is("(function () { var box = document.getElementById('box');"
       " var p1 = document.getElementById('p1'), p2 = document.getElementById('p2');"
       " var r = document.createRange(); r.setStart(p1.firstChild, 1); r.setEnd(p2.firstChild, 1);"
       " var f = r.extractContents(); var d = document.createElement('div'); d.appendChild(f);"
       " return d.innerHTML + '|' + box.innerHTML + '|' + r.collapsed + r.startOffset; })()",
       "<p id=\"p1\">bcd</p><p id=\"p2\">E</p>|<p id=\"p1\">A</p><p id=\"p2\">fgh</p><!--c-->"
       "<span id=\"s\">ij</span>|true1");
    // Range-cloneContents.html: copies, and the tree and range are untouched.
    is("(function () { var box = document.getElementById('box');"
       " var p1 = document.getElementById('p1'), p2 = document.getElementById('p2');"
       " var r = document.createRange(); r.setStart(p1.firstChild, 1); r.setEnd(box, 3);"
       " var f = r.cloneContents(); var d = document.createElement('div'); d.appendChild(f);"
       " return d.innerHTML + '|' + box.childNodes.length + '|' + r.startOffset + r.endOffset; "
       "})()",
       "<p id=\"p1\">bcd</p><p id=\"p2\">Efgh</p><!--c-->|4|13");
    // Inside one text node, and a doctype in the way.
    is("(function () { var p1 = document.getElementById('p1'); var r = document.createRange();"
       " r.setStart(p1.firstChild, 1); r.setEnd(p1.firstChild, 3); var f = r.cloneContents();"
       " var s = f.firstChild.data; r.deleteContents();"
       " var o = [s, p1.textContent, r.startOffset];"
       " var d = document.createRange(); d.selectNodeContents(document);"
       " try { d.extractContents(); } catch (e) { o.push(e.name); }"
       " return o.join(); })()",
       "bc,Ad,1,HierarchyRequestError");
    // Range-stringifier.html.
    is("(function () { var r = document.createRange();"
       " r.setStart(document.getElementById('p1').firstChild, 2);"
       " r.setEnd(document.getElementById('s').firstChild, 1); return r.toString(); })()",
       "cdEfghi");
    // DOM Parsing's createContextualFragment: parsed as the start node's
    // element's children, in a fragment of its document.
    is("(function () { var r = document.createRange();"
       " r.setStart(document.getElementById('p1').firstChild, 1);"
       " var f = r.createContextualFragment('<b>x</b>y'); return f.nodeType + ',' +"
       " f.childNodes.length + ',' + f.firstChild.tagName + ',' + f.lastChild.data + ','"
       " + (f.ownerDocument === document); })()",
       "11,2,B,y,true");
}

void test_insert_node_and_surround_contents() {
    // Range-insertNode.html: a start inside text splits it, the range's own
    // points follow the split and the insertion; a collapsed range stretches.
    is("(function () { var p1 = document.getElementById('p1'); var r = document.createRange();"
       " r.setStart(p1.firstChild, 1); r.setEnd(p1.firstChild, 3);"
       " r.insertNode(document.createElement('br'));"
       " return p1.innerHTML + '|' + (r.startContainer === p1.firstChild) + r.startOffset + '|'"
       " + (r.endContainer === p1.lastChild) + r.endOffset; })()",
       "A<br>bcd|true1|true2");
    is("(function () { var box = document.getElementById('box'); var r = document.createRange();"
       " r.setStart(box, 1); r.collapse(true); var f = document.createDocumentFragment();"
       " f.appendChild(document.createElement('u')); f.appendChild(document.createElement('i'));"
       " r.insertNode(f); return box.childNodes.length + '|' + r.startOffset + r.endOffset; })()",
       "6|13");
    // Moving a node already in the parent, ahead of the range.
    is("(function () { var box = document.getElementById('box'); var r = document.createRange();"
       " r.setStart(box, 2); r.setEnd(box, 3); r.insertNode(document.getElementById('p1'));"
       " return box.firstChild.id + ',' + r.startOffset + ',' + r.endOffset; })()",
       "p2,1,3");
    // The errors: a comment start, a text with no parent, the node itself.
    is("(function () { var r = document.createRange(); var o = [];"
       " r.setStart(document.getElementById('box').childNodes[2], 0);"
       " try { r.insertNode(document.createElement('b')); } catch (e) { o.push(e.name); }"
       " var t = document.createTextNode('x'); r.setStart(t, 0);"
       " try { r.insertNode(document.createElement('b')); } catch (e) { o.push(e.name); }"
       " return o.join(); })()",
       "HierarchyRequestError,HierarchyRequestError");
    // Range-surroundContents.html.
    is("(function () { var p1 = document.getElementById('p1'); var r = document.createRange();"
       " r.setStart(p1.firstChild, 1); r.setEnd(p1.firstChild, 3);"
       " var b = document.createElement('b'); b.textContent = 'old'; r.surroundContents(b);"
       " return p1.innerHTML + '|' + (r.startContainer === p1) + r.startOffset + r.endOffset; })()",
       "A<b>bc</b>d|true12");
    is("(function () { var r = document.createRange(); var o = [];"
       " r.setStart(document.getElementById('p1').firstChild, 1);"
       " r.setEnd(document.getElementById('p2').firstChild, 1);"
       " try { r.surroundContents(document.createElement('b')); } catch (e) { o.push(e.name); }"
       " r.selectNodeContents(document.getElementById('p1'));"
       " try { r.surroundContents(document.createDocumentFragment()); } catch (e) { "
       "o.push(e.name); }"
       " return o.join(); })()",
       "InvalidStateError,InvalidNodeTypeError");
}

// --- StaticRange ------------------------------------------------------------------

// --- the live range steps, DOM 5.5 --------------------------------------------

// Range-mutations-{appendChild,insertBefore,removeChild}.html: a boundary on
// the parent past the edit moves with it; one inside a removed node lands
// where the node was.
void test_a_range_follows_tree_edits() {
    is("(function () { var box = document.getElementById('box'), p1 = "
       "document.getElementById('p1'),"
       " p2 = document.getElementById('p2');"
       " var r = document.createRange(); r.setStart(box, 1); r.setEnd(box, 3);"
       " var out = [];"
       " box.insertBefore(document.createElement('i'), p1); out.push(r.startOffset + '-' + "
       "r.endOffset);"
       " box.appendChild(document.createElement('b')); out.push(r.startOffset + '-' + r.endOffset);"
       " box.removeChild(box.firstChild); out.push(r.startOffset + '-' + r.endOffset);"
       " r.setEnd(p2.firstChild, 2); box.removeChild(p2);"
       " out.push((r.endContainer === box) + ':' + r.endOffset);"
       " box.insertBefore(p2, box.firstChild); out.push(r.startOffset + '-' + r.endOffset + ':' +"
       " (r.endContainer === box));"
       " return out.join('|'); })()",
       "2-4|2-4|1-3|true:1|2-2:true");
}

// Range-mutations-{insertData,deleteData,replaceData,dataChange}.html,
// Range-mutations-splitText.html, Node-normalize: the replace data steps, the
// split steps and the absorption steps.
void test_a_range_follows_data_edits() {
    is("(function () { var t = document.getElementById('p1').firstChild;"
       " var r = document.createRange(); r.setStart(t, 1); r.setEnd(t, 3); var out = [];"
       " t.insertData(0, 'xy'); out.push(r.startOffset + '-' + r.endOffset);"
       " t.deleteData(0, 1); out.push(r.startOffset + '-' + r.endOffset);"
       " t.replaceData(2, 3, 'Q'); out.push(r.startOffset + '-' + r.endOffset);"
       " t.data = 'hello'; out.push(r.startOffset + '-' + r.endOffset);"
       " r.setStart(t, 1); r.setEnd(t, 4); var made = t.splitText(2);"
       " out.push((r.startContainer === t) + ':' + r.startOffset + ',' + (r.endContainer === made)"
       " + ':' + r.endOffset);"
       " var p = t.parentNode; var q = document.createRange(); q.setStart(p, 1); q.setEnd(p, 2);"
       " p.normalize(); out.push(r.startOffset + '-' + r.endOffset + ',' + (r.endContainer === t)"
       " + ',' + (q.startContainer === t) + ':' + q.startOffset + ',' + q.endOffset);"
       " return out.join('|'); })()",
       "3-5|2-4|2-2|0-0|true:1,true:2|1-4,true,true:2,1");
}

void test_static_range_is_a_snapshot() {
    // StaticRange-constructor.html: four required members, a doctype or an
    // Attr refused, an inverted or too-long pair allowed, nothing live.
    is("(function () { var p1 = document.getElementById('p1');"
       " var s = new StaticRange({startContainer: p1, startOffset: 0, endContainer: p1.firstChild,"
       " endOffset: 9}); var o = [s.startContainer === p1, s.endOffset, s.collapsed,"
       " s instanceof StaticRange, s instanceof AbstractRange, s instanceof Range,"
       " Object.prototype.toString.call(s)];"
       " p1.textContent = ''; o.push(s.endOffset);"
       " try { new StaticRange(); } catch (e) { o.push(e.name); }"
       " try { new StaticRange({startContainer: p1, startOffset: 0, endContainer: p1}); }"
       " catch (e) { o.push(e.name); }"
       " try { new StaticRange({startContainer: document.doctype, startOffset: 0,"
       " endContainer: p1, endOffset: 0}); } catch (e) { o.push(e.name); }"
       " try { new StaticRange({startContainer: null, startOffset: 0, endContainer: p1,"
       " endOffset: 0}); } catch (e) { o.push(e.name); }"
       " return o.join(); })()",
       "true,9,false,true,true,false,[object StaticRange],9,TypeError,TypeError,"
       "InvalidNodeTypeError,TypeError");
}

} // namespace

int main() {
    test_a_range_is_readonly_accessors_on_the_prototype();
    test_set_start_and_end_follow_each_other();
    test_select_collapse_and_common_ancestor();
    test_comparisons_over_two_documents_and_an_attr();
    test_delete_extract_and_clone_contents();
    test_insert_node_and_surround_contents();
    test_a_range_follows_tree_edits();
    test_a_range_follows_data_edits();
    test_static_range_is_a_snapshot();
    REPORT("ranges_wpt");
}
