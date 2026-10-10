//===- parser_steps.cpp - parser and DOM steps ---------------- -*- C++ -*-===//
//
// Page-visible results of parser and DOM steps, each the smallest expression
// that fails if the behaviour regresses:
//
//   * an <option> popped off the stack of open elements is cloned into its
//     select's <selectedcontent> when it is the selected one (HTML 4.10.10);
//   * the Document's own child list is tracked like any other parent's, so a
//     live range on the Document moves when its element goes (DOM 5.5);
//   * `rootElement` is the <svg> root of an SVG document and null otherwise;
//   * NodeList's iterators are Array.prototype's own functions (WebIDL
//     3.7.10.1), live over the list;
//   * Selection.extend() into another tree collapses at the new focus;
//   * `shadowRoot` is Element's attribute, and a prefixed div may host one;
//   * an event interface's prototype names itself to Object.prototype.toString.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser.hpp>

#include "check.hpp"
#include "dom_probe.hpp"

#include <string>

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><body>
<select id=s><button><selectedcontent id=sc></selectedcontent></button><option>X<option selected>Y<b>!</b></select>
<select id=m multiple><button><selectedcontent id=msc></selectedcontent></button><option selected>Z</select>
<div id=live><b id=b1></b><b id=b2></b></div>
<p id=before>Before</p><div id=host></div>
</body></html>)";

/// Evaluates `expression` on a fresh copy of page_html and checks its string.
void is(const std::string & expression, const std::string & expected) {
    ctbrowser_test::is_in(page_html, expression, expected);
}

void test_the_selected_option_is_cloned_into_selectedcontent() {
    // The second option carries `selected`, so it is the selected one when it
    // is popped, and its children are copied, markup and all.
    is("document.getElementById('sc').innerHTML", "Y<b>!</b>");
    // A `multiple` select has no enabled selectedcontent.
    is("document.getElementById('msc').childNodes.length", "0");
}

void test_a_range_on_the_document_follows_its_element() {
    is(R"JS((function () {
        var d = document.implementation.createHTMLDocument('');
        var r = d.createRange();
        r.setStart(d, 2);
        r.setEnd(d, 2);
        d.removeChild(d.documentElement);
        return r.startOffset + ',' + r.endOffset;
    })())JS",
       "1,1");
}

void test_root_element_is_an_svg_documents_root() {
    is("new DOMParser().parseFromString(\"<svg xmlns='http://www.w3.org/2000/svg'/>\", "
       "'image/svg+xml').rootElement.localName",
       "svg");
    is("document.rootElement", "null");
}

void test_node_list_iterators_are_the_arrays() {
    is("document.body.childNodes[Symbol.iterator] === Array.prototype[Symbol.iterator] && "
       "NodeList.prototype.keys === Array.prototype.keys && "
       "NodeList.prototype.forEach === Array.prototype.forEach",
       "true");
    is(R"JS((function () {
        var live = document.getElementById('live');
        var it = live.childNodes.values(), seen = [];
        for (var step = it.next(); !step.done; step = it.next()) {
            seen.push(step.value.id);
            if (seen.length < 3) { live.appendChild(document.createElement('i')).id = 'x' + seen.length; }
        }
        return seen.join();
    })())JS",
       "b1,b2,x1,x2");
}

void test_extend_into_a_shadow_tree_collapses_at_the_focus() {
    is(R"JS((function () {
        var inside = document.getElementById('host').attachShadow({ mode: 'open' })
            .appendChild(document.createElement('span'));
        var sel = getSelection();
        sel.collapse(document.getElementById('before'), 0);
        sel.extend(inside, 0);
        var r = sel.getRangeAt(0);
        return [sel.rangeCount, r.startContainer === inside, r.collapsed].join();
    })())JS",
       "1,true,true");
}

void test_shadow_root_is_an_elements_attribute() {
    is("'shadowRoot' in Element.prototype && !('shadowRoot' in Node.prototype) && "
       "!('shadowRoot' in Text.prototype)",
       "true");
    // The local name decides whether an element may host: a prefixed HTML div
    // is a div.
    is("document.createElementNS('http://www.w3.org/1999/xhtml', 'x:div')"
       ".attachShadow({ mode: 'open' }).host.localName",
       "div");
}

void test_event_interfaces_name_themselves() {
    is("Object.prototype.toString.call(new Event('x')) + Object.prototype.toString.call("
       "new PageTransitionEvent('pageshow', { persisted: true })) + "
       "new PageTransitionEvent('pageshow', { persisted: true }).persisted",
       "[object Event][object PageTransitionEvent]true");
}

} // namespace

int main() {
    test_the_selected_option_is_cloned_into_selectedcontent();
    test_a_range_on_the_document_follows_its_element();
    test_root_element_is_an_svg_documents_root();
    test_node_list_iterators_are_the_arrays();
    test_extend_into_a_shadow_tree_collapses_at_the_focus();
    test_shadow_root_is_an_elements_attribute();
    test_event_interfaces_name_themselves();
    REPORT("dom_parser_steps");
}
