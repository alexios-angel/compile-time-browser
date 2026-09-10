// THE NODE METHOD SURFACE AGAINST dom/nodes - one case per rule the corpus
// found missing, each named after the WPT file that asserts it.
//
// What these pin down is not that a method exists but WHICH of two answers it
// gives: `child.after(x, y)` where x and y already follow child, a
// `replaceChild` whose child is somebody else's, `removeChild(document)`, a
// `moveBefore` across two trees, `lookupPrefix` asked of a text node, and a
// `<template>` whose children the parser kept out of the element. Every one of
// them had the OTHER answer before, silently.
//
// The pattern is unit/element_attrs.cpp's: one expression against a fresh
// page, and whatever it logged.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><body>
<div id=box><span id=a></span><span id=b></span></div>
<template id=tpl><div id=inner><b id=deep>x</b></div></template>
<svg id=s><linearGradient id=lg></linearGradient></svg>
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

// --- ChildNode: the viable sibling, then the conversion, then one insertion --

void test_after_and_before_skip_the_arguments_when_choosing_the_sibling() {
    // ChildNode-after.html, "with all siblings of child as arguments": x, y and
    // z already follow child, so the sibling to insert before is the one AFTER
    // them - which is none - and they land after child in argument order.
    is(R"JS((function () {
        var p = document.createElement('div');
        var c = document.createElement('test'), x = document.createElement('x'),
            y = document.createElement('y'), z = document.createElement('z');
        p.append(c, x, y, z);
        c.after(x, y, z);
        return p.innerHTML; })())JS",
       "<test></test><x></x><y></y><z></z>");
    // ...and "with one sibling of child and text as arguments": the text goes
    // with the moved sibling, before the sibling that was not an argument.
    is(R"JS((function () {
        var p = document.createElement('div');
        var c = document.createElement('test'), x = document.createElement('x'),
            y = document.createElement('y');
        p.append(c, x, y);
        c.after(x, 'text');
        return p.innerHTML; })())JS",
       "<test></test><x></x>text<y></y>");
    // ChildNode-before.html, "with some siblings of child as arguments; no
    // changes in tree; viable sibling": v and x stay put, y is not an argument
    // so it is the viable previous sibling, and z goes before child.
    is(R"JS((function () {
        var p = document.createElement('div');
        var c = document.createElement('test'), v = document.createElement('v'),
            x = document.createElement('x'), y = document.createElement('y'),
            z = document.createElement('z');
        p.append(v, x, y, z, c);
        c.before(v, x, z);
        return p.innerHTML; })())JS",
       "<y></y><v></v><x></x><z></z><test></test>");
}

void test_replace_with_the_node_itself_among_the_arguments() {
    // ChildNode-replaceWith.html, "with one sibling of child and child itself":
    // converting the arguments moves child into the fragment, so it is no
    // longer this parent's and the fragment goes before the viable sibling.
    is(R"JS((function () {
        var p = document.createElement('div');
        var c = document.createElement('test'), x = document.createElement('x');
        p.append(c, x, 'text');
        c.replaceWith(x, c);
        return p.innerHTML; })())JS",
       "<x></x><test></test>text");
    // `c.replaceWith(c)` is a replacement with itself: nothing moves, nothing
    // is removed.
    is(R"JS((function () {
        var p = document.createElement('div');
        var c = document.createElement('test'), x = document.createElement('x');
        p.append(x, c);
        c.replaceWith(c);
        return p.innerHTML; })())JS",
       "<x></x><test></test>");
}

void test_append_converts_and_validates() {
    // ParentNode-append.html: a string becomes a Text node - null is the four
    // letters - and `body.append(body)` is the ancestor check, not a cycle.
    is("(function () { var d = document.createElement('div'); d.append(null);"
       " return d.firstChild.data; })()",
       "null");
    is("(function () { document.body.append(document.body); })()", "threw:HierarchyRequestError");
    is("(function () { var d = document.createElement('div'); var x = document.createElement('x');"
       " var y = document.createElement('y'); d.append(x, y, x); return d.innerHTML; })()",
       "<y></y><x></x>");
}

// --- replaceChild, removeChild and the document object --------------------

void test_replace_child_checks_before_it_swaps() {
    // Node-replaceChild.html, in the order its subtests come.
    is("(function () { var a = document.createElement('div'); a.replaceChild(null, null); })()",
       "threw:TypeError");
    is("(function () { var a = document.createElement('div'), b = document.createElement('div'),"
       " c = document.createElement('div'); a.replaceChild(b, c); })()",
       "threw:NotFoundError");
    is("(function () { var a = document.createElement('div'); a.replaceChild(a, a); })()",
       "threw:HierarchyRequestError");
    // "Replacing a node with itself should not move the node".
    is("(function () { var a = document.createElement('div'), b = document.createElement('b'),"
       " c = document.createElement('c'); a.append(b, c); a.replaceChild(b, b);"
       " return a.innerHTML; })()",
       "<b></b><c></c>");
    is("(function () { var a = document.createElement('div'), b = document.createElement('b'),"
       " c = document.createElement('c'); a.append(b); return a.replaceChild(c, b) === b &&"
       " a.innerHTML; })()",
       "<c></c>");
}

void test_the_document_is_a_node_for_the_purpose_of_a_refusal() {
    // Node-removeChild.html: `s.removeChild(document)` is NOT_FOUND_ERR, and
    // Node-insertBefore.html: `el.insertBefore(document, a)` is
    // HIERARCHY_REQUEST_ERR. Both were TypeErrors, because the document object
    // has no node handle.
    is("(function () { var s = document.createElement('div'); s.removeChild(document); })()",
       "threw:NotFoundError");
    is("(function () { var el = document.createElement('div'), a = document.createElement('a');"
       " el.appendChild(a); el.insertBefore(document, a); })()",
       "threw:HierarchyRequestError");
    // ...and something that is not a Node at all is still a TypeError.
    is("(function () { document.body.removeChild({a: 'b'}); })()", "threw:TypeError");
}

void test_two_required_arguments() {
    // Node-insertBefore.html: "Calling insertBefore with second argument
    // missing ... must throw TypeError".
    is("(function () { document.body.insertBefore(document.createTextNode('child')); })()",
       "threw:TypeError");
    is("(function () { document.body.moveBefore(document.createTextNode('child')); })()",
       "threw:TypeError");
}

void test_move_before() {
    // Node-moveBefore.html: undefined, not the node; not on a Text; and a move
    // between two trees is refused.
    is("(function () { var a = document.body.appendChild(document.createElement('div'));"
       " var b = document.createElement('b'), c = document.createElement('c'); a.append(b, c);"
       " return a.moveBefore(c, b) === undefined && a.innerHTML; })()",
       "<c></c><b></b>");
    is("'moveBefore' in document.createTextNode('x')", "false");
    is("'moveBefore' in document.createElement('div')", "true");
    is("(function () { var target = document.body.appendChild(document.createElement('div'));"
       " document.createElement('div').moveBefore(target, null); })()",
       "threw:HierarchyRequestError");
    is("(function () { var dest = document.body.appendChild(document.createElement('div'));"
       " dest.moveBefore(document.createElement('div'), null); })()",
       "threw:HierarchyRequestError");
}

// --- namespaces, normalize, live tag lists ---------------------------------

void test_namespace_lookups_on_every_node() {
    // Node-lookupPrefix.xhtml asks a text node, a comment and a fragment; the
    // element algorithm runs at the parent element, or at nothing.
    is("(function () { var x = document.createElement('x'); x.setAttribute('xmlns:t', 'test');"
       " x.append('TEST'); return x.firstChild.lookupPrefix('test'); })()",
       "t");
    is("(function () { var x = document.createElement('x'); x.setAttribute('xmlns:t', 'test');"
       " x.append('TEST'); return x.firstChild.lookupNamespaceURI('t'); })()",
       "test");
    is("document.createDocumentFragment().lookupNamespaceURI('xml')", "null");
    is("document.createDocumentFragment().isDefaultNamespace(null)", "true");
    is("document.createDocumentFragment().isDefaultNamespace('foo')", "false");
    is("document.createElement('div').lookupNamespaceURI('xmlns')",
       "http://www.w3.org/2000/xmlns/");
}

void test_normalize() {
    // Node-normalize.html: the run becomes its FIRST member, and an empty first
    // member goes rather than absorbing the run (bug 19837).
    is("(function () { var df = document.createDocumentFragment();"
       " var t1 = document.createTextNode('1'), t2 = document.createTextNode('2');"
       " df.append(t1, t2); df.normalize(); return df.childNodes.length + ',' +"
       " (df.firstChild === t1) + ',' + t1.data + ',' + t2.data; })()",
       "1,true,12,2");
    is("(function () { var d = document.createElement('div');"
       " var t1 = d.appendChild(document.createTextNode('')),"
       " t2 = d.appendChild(document.createTextNode('a')),"
       " t3 = d.appendChild(document.createTextNode('')); d.normalize();"
       " return d.childNodes.length + ',' + (d.firstChild === t2); })()",
       "1,true");
    is("(function () { var d = document.createElement('div');"
       " d.append(document.createTextNode(''), document.createTextNode('')); d.normalize();"
       " return d.childNodes.length; })()",
       "0");
}

void test_get_elements_by_tag_name_on_an_element() {
    // Element-getElementsByTagName.html: live, an HTMLCollection, and a foreign
    // element is found by its own spelling only.
    is("(function () { var l = document.getElementById('box').getElementsByTagName('span');"
       " var n = l.length; "
       "document.getElementById('box').appendChild(document.createElement('span'));"
       " return n + ',' + l.length + ',' + (l instanceof HTMLCollection); })()",
       "2,3,true");
    is("document.getElementById('s').getElementsByTagName('linearGradient').length", "1");
    is("document.getElementById('s').getElementsByTagName('lineargradient').length", "0");
    is("document.getElementById('box').getElementsByTagName('SPAN').length", "2");
    is("document.getElementById('box').getElementsByTagName('div').length", "0");
}

// --- fragments and templates ------------------------------------------------

void test_template_content() {
    // svg-template-querySelector.html and DocumentFragment-getElementById.html:
    // a parsed template's children are in its CONTENT fragment and not in the
    // element, the fragment is the same object on every read, and a template a
    // script makes has one too.
    is("document.getElementById('tpl').childNodes.length", "0");
    is("document.getElementById('tpl').content.childNodes.length", "1");
    is("document.getElementById('tpl').content === document.getElementById('tpl').content", "true");
    is("document.getElementById('tpl').content.querySelector('b').id", "deep");
    is("document.getElementById('tpl').content.getElementById('deep').textContent", "x");
    is("document.getElementById('inner')", "null");
    is("(function () { var t = document.createElement('template');"
       " return t.content.nodeType + ',' + t.content.childNodes.length; })()",
       "11,0");
}

void test_get_element_by_id_on_a_fragment() {
    is("(function () { var f = document.createDocumentFragment();"
       " f.append(document.createElement('div'), document.createElement('span'));"
       " f.childNodes[0].id = 'foo'; f.childNodes[1].id = 'foo';"
       " return f.getElementById('foo') === f.childNodes[0]; })()",
       "true");
    is("(function () { var f = document.createDocumentFragment();"
       " f.appendChild(document.createElement('div')).setAttribute('id', '');"
       " return f.getElementById(''); })()",
       "null");
}

void test_a_named_attribute_does_not_shadow_the_prototype_chain() {
    // attributes-namednodemap.html: `toString` stays the inherited function.
    is("(function () { var e = document.createElement('div');"
       " e.setAttributeNS('foo', 'toString', 'first');"
       " return e.attributes.length + ',' + typeof e.attributes.toString; })()",
       "1,function");
}

} // namespace

int main() {
    test_after_and_before_skip_the_arguments_when_choosing_the_sibling();
    test_replace_with_the_node_itself_among_the_arguments();
    test_append_converts_and_validates();
    test_replace_child_checks_before_it_swaps();
    test_the_document_is_a_node_for_the_purpose_of_a_refusal();
    test_two_required_arguments();
    test_move_before();
    test_namespace_lookups_on_every_node();
    test_normalize();
    test_get_elements_by_tag_name_on_an_element();
    test_template_content();
    test_get_element_by_id_on_a_fragment();
    test_a_named_attribute_does_not_shadow_the_prototype_chain();
    REPORT("dom_nodes_wpt");
}
