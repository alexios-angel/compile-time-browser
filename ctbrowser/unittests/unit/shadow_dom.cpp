// `Element.attachShadow` AND THE SHADOW ROOT.
//
// DOM 4.8, far enough that a test which merely USES a shadow tree can run -
// which is what thirty-one files across `dom/nodes`, `dom/events`, `css/cssom`
// and `css/css-values` do. Most of them are not ABOUT shadow DOM: they attach a
// tree as scaffolding and then assert something else, and every one of them died
// on `attachShadow is undefined, not a function` before one assertion ran.
//
// TWO THINGS HERE ARE NOT CONFORMANCE CHECKS AND ARE THE POINT.
//
// The first is that a shadow tree is INVISIBLE TO THE LIGHT DOM. It is a
// detached DocumentFragment, so `document.querySelector` must not find anything
// in it and the host's `childNodes` must not grow - and if a later rung ever
// grafts the fragment into the tree to make it render, these are the assertions
// that catch the light DOM changing shape underneath every other test.
//
// The second is that `shadowRoot.querySelector` answers AT ALL. The selector
// engine walks from the document root, so a detached fragment is unreachable
// from it; the cases below run a real selector against a subtree nothing else in
// the engine can see. See subtree_matcher in lib/Shell/bindings/element/shadow.cpp for
// what that costs and for the change to lib/Style that would retire it.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><head><title>shadow</title></head><body>
<div id=host></div>
<table id=nothost></table>
<my-widget id=custom></my-widget>
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

// An expression run after `h` is a host and `r` its open shadow root, so that
// every case below is one line rather than four.
[[nodiscard]] std::string with_root(const std::string & body) {
    return "(function () { var h = document.getElementById('host');"
           " var r = h.attachShadow({mode: 'open'}); " +
           body + " })()";
}

// --- attachShadow itself ----------------------------------------------------

void test_attach_shadow_makes_a_shadow_root() {
    // A DocumentFragment by node type - 11 - because that is what a shadow root
    // IS. The interface is a second name for the same kind of node, which is why
    // both instanceof questions have to answer true.
    is(with_root("return r.nodeType;"), "11");
    is(with_root("return r.nodeName;"), "#document-fragment");
    is(with_root("return r instanceof ShadowRoot;"), "true");
    is(with_root("return r instanceof DocumentFragment;"), "true");
    is(with_root("return r instanceof Node;"), "true");
    // ...and a fragment a page made itself is NOT one, which is the half that
    // makes the distinction worth storing.
    is("document.createDocumentFragment() instanceof ShadowRoot", "false");
    is("document.createDocumentFragment() instanceof DocumentFragment", "true");
    // `mode` and `host` read back what attachShadow was told and who it was
    // called on.
    is(with_root("return r.mode;"), "open");
    is(with_root("return r.host === h;"), "true");
    // THE SAME OBJECT EVERY TIME. A wrapper is cached per node, and a page that
    // stashes `el.shadowRoot` and compares it later relies on that.
    is(with_root("return h.shadowRoot === r;"), "true");
}

void test_the_mode_is_required_and_is_one_of_two_words() {
    // `mode` is a required member of a required dictionary, so WebIDL's argument
    // conversion throws a plain TypeError - NOT a DOMException - and it throws
    // before one thing about the element has been looked at.
    is("(function () { try { document.getElementById('host').attachShadow({}); }"
       " catch (e) { return e.name; } return 'no throw'; })()",
       "TypeError");
    is("(function () { try { document.getElementById('host').attachShadow(); }"
       " catch (e) { return e.name; } return 'no throw'; })()",
       "TypeError");
    is("(function () { try {"
       " document.getElementById('host').attachShadow({mode: 'ajar'}); }"
       " catch (e) { return e.name; } return 'no throw'; })()",
       "TypeError");
    // A CLOSED ROOT IS A REAL ROOT. It is only `element.shadowRoot` that refuses
    // to hand it over - everything else about it works, which is what makes the
    // mode a privacy measure rather than a feature switch.
    is("document.getElementById('host').attachShadow({mode: 'closed'}).mode", "closed");
    is("(function () { var h = document.getElementById('host');"
       " h.attachShadow({mode: 'closed'}); return h.shadowRoot; })()",
       "null");
    is("(function () { var h = document.getElementById('host');"
       " var r = h.attachShadow({mode: 'closed'}); return r.host === h; })()",
       "true");
    // An element with no shadow tree at all answers null too, which is not the
    // same as answering undefined: a page tests `if (el.shadowRoot)`.
    is("document.getElementById('nothost').shadowRoot", "null");
}

void test_only_some_elements_may_host_one() {
    // Sixteen names, and `<table>` is not one of them.
    is("(function () { try { document.getElementById('nothost').attachShadow({mode:'open'}); }"
       " catch (e) { return e.name; } return 'no throw'; })()",
       "NotSupportedError");
    // ...but ANY valid custom element name is, and there can be no list of those.
    is("document.getElementById('custom').attachShadow({mode: 'open'}).mode", "open");
    is("document.createElement('section').attachShadow({mode: 'open'}).nodeType", "11");
    is("(function () { try { document.createElement('input').attachShadow({mode:'open'}); }"
       " catch (e) { return e.name; } return 'no throw'; })()",
       "NotSupportedError");
    // TWICE IS A REFUSAL, not a second tree and not the first one handed back.
    is(with_root("try { h.attachShadow({mode: 'open'}); }"
                 " catch (e) { return e.name; } return 'no throw';"),
       "NotSupportedError");
}

// --- the tree inside it -----------------------------------------------------

void test_a_shadow_root_holds_a_tree() {
    // `innerHTML` PARSES, on a fragment exactly as on an element - the same
    // function, because a shadow root has children like anything else.
    is(with_root("r.innerHTML = '<div class=\"c\">x</div>'; return r.childNodes.length;"), "1");
    is(with_root("r.innerHTML = '<div class=\"c\">x</div>'; return r.firstChild.tagName;"), "DIV");
    is(with_root("r.innerHTML = '<b>x</b><i>y</i>'; return r.children.length;"), "2");
    is(with_root("r.innerHTML = '<b>x</b>'; return r.textContent;"), "x");
    // appendChild, append and replaceChildren, which is how a page that is not
    // using markup builds one.
    is(with_root("r.appendChild(document.createElement('span')); return r.childNodes.length;"),
       "1");
    is(with_root("r.append('text', document.createElement('span'));"
                 " return r.childNodes.length + ':' + r.children.length;"),
       "2:1");
    is(with_root("r.innerHTML = '<b>1</b><b>2</b>';"
                 " r.replaceChildren(document.createElement('i'));"
                 " return r.children.length + ':' + r.firstChild.tagName;"),
       "1:I");
    // A node inside the tree knows where it is.
    is(with_root("r.innerHTML = '<b>x</b>'; return r.firstChild.parentNode === r;"), "true");
}

void test_the_shadow_tree_is_invisible_to_the_light_dom() {
    // THE ASSERTION THIS FILE EXISTS FOR. A shadow root is detached, so nothing
    // reached from the document can see into it: the host has no children, and
    // the document's own query finds nothing.
    is(with_root("r.innerHTML = '<div class=\"c\">x</div>'; return h.childNodes.length;"), "0");
    is(with_root("r.innerHTML = '<div class=\"c\">x</div>';"
                 " return document.querySelectorAll('.c').length;"),
       "0");
    is(with_root("r.innerHTML = '<div id=\"inside\">x</div>';"
                 " return String(document.getElementById('inside'));"),
       "null");
    // ...and the id inside it IS findable through the root, which is what makes
    // the two scopes different scopes rather than one broken one.
    is(with_root("r.innerHTML = '<div id=\"inside\">x</div>';"
                 " return r.getElementById('inside').id;"),
       "inside");
    is(with_root("r.innerHTML = '<div id=\"inside\">x</div>';"
                 " return String(r.getElementById('nosuch'));"),
       "null");
}

void test_query_selector_searches_the_shadow_tree() {
    // A real selector against a subtree the selector engine cannot reach. Every
    // one of these is a shape the corpus actually uses on a shadow root.
    is(with_root("r.innerHTML = '<div class=\"shadowChild\">x</div>';"
                 " return r.querySelector('.shadowChild').className;"),
       "shadowChild");
    is(with_root("r.innerHTML = '<p>one</p><p>two</p>'; return r.querySelector('p').textContent;"),
       "one");
    is(with_root("r.innerHTML = '<p>one</p><p>two</p>'; return r.querySelectorAll('p').length;"),
       "2");
    is(with_root("r.innerHTML = '<div id=\"target\">x</div>';"
                 " return r.querySelector('#target').id;"),
       "target");
    is(with_root("r.innerHTML = '<div dir=\"rtl\">x</div><div dir=\"ltr\">y</div>';"
                 " return r.querySelector('div[dir=rtl]').getAttribute('dir');"),
       "rtl");
    is(with_root("r.innerHTML = '<slot name=\"outer\"></slot>';"
                 " return r.querySelector('slot[name=\"outer\"]').getAttribute('name');"),
       "outer");
    // THE COMBINATORS, which the subtree matcher walks through parent and
    // sibling links of its own rather than through the cascade's cursor.
    is(with_root("r.innerHTML = '<div><span>a</span></div>';"
                 " return r.querySelector('div > span').textContent;"),
       "a");
    is(with_root("r.innerHTML = '<div><p><span>a</span></p></div>';"
                 " return r.querySelector('div span').textContent;"),
       "a");
    is(with_root("r.innerHTML = '<b>1</b><i>2</i>'; return r.querySelector('b + i').textContent;"),
       "2");
    is(with_root("r.innerHTML = '<p>1</p><p>2</p><p>3</p>';"
                 " return r.querySelector('p:nth-child(2)').textContent;"),
       "2");
    is(with_root("r.innerHTML = '<p>1</p><p>2</p>';"
                 " return r.querySelector('p:last-child').textContent;"),
       "2");
    is(with_root("r.innerHTML = '<p class=\"a\">1</p><p>2</p>';"
                 " return r.querySelector('p:not(.a)').textContent;"),
       "2");
    // A DESCENDANT SEARCH NEVER RETURNS THE ROOT and never leaves the tree: a
    // selector naming the HOST's tag must not match the host from inside.
    is(with_root("r.innerHTML = '<b>x</b>'; return String(r.querySelector('div'));"), "null");
    is(with_root("r.innerHTML = '<b>x</b>'; return r.querySelectorAll('*').length;"), "1");
    // Not a selector at all is a SyntaxError, exactly as on the document.
    is(with_root("try { r.querySelector('div >'); }"
                 " catch (e) { return e.name; } return 'no throw';"),
       "SyntaxError");
}

// --- getRootNode, and the boundary it stops at ------------------------------

void test_get_root_node_stops_at_the_boundary() {
    // `rootNode.html`'s first case, in one line: the default and `composed:
    // false` answer with the ShadowRoot, and `composed: true` keeps going
    // through the host to the document.
    is(with_root("r.innerHTML = '<div class=\"shadowChild\">x</div>';"
                 " var c = r.querySelector('.shadowChild');"
                 " return c.getRootNode() === r;"),
       "true");
    is(with_root("r.innerHTML = '<div class=\"shadowChild\">x</div>';"
                 " var c = r.querySelector('.shadowChild');"
                 " return c.getRootNode({composed: false}) === r;"),
       "true");
    is(with_root("r.innerHTML = '<div class=\"shadowChild\">x</div>';"
                 " var c = r.querySelector('.shadowChild');"
                 " return c.getRootNode({composed: true}) === document;"),
       "true");
    // A CLOSED ROOT IS STILL A ROOT. `getRootNode` does not consult the mode -
    // only `element.shadowRoot` does.
    is("(function () { var h = document.getElementById('host');"
       " var r = h.attachShadow({mode: 'closed'}); r.innerHTML = '<b>x</b>';"
       " return r.firstChild.getRootNode() === r; })()",
       "true");
    // And the ordinary cases, which have nothing to do with shadow DOM and had
    // no method to answer them at all: `rootNode.html` is four more tests of
    // exactly this.
    is("document.body.getRootNode() === document", "true");
    is("document.createElement('div').getRootNode().tagName", "DIV");
    is("(function () { var p = document.createElement('div');"
       " var e = document.createElement('span'); p.appendChild(e);"
       " return e.getRootNode() === p; })()",
       "true");
    is("(function () { var f = document.createDocumentFragment();"
       " var e = document.createElement('span'); f.appendChild(e);"
       " return e.getRootNode() === f; })()",
       "true");
    is("(function () { var t = document.createTextNode('');"
       " return t.getRootNode() === t; })()",
       "true");
    is("document.getRootNode() === document", "true");
}

void test_is_connected_crosses_the_boundary() {
    // "Shadow-including root is a document" - so a node inside a shadow tree
    // whose HOST is in the document is connected, and the same tree on a
    // detached host is not. That difference is `Node-isConnected-shadow-dom`.
    is("document.body.isConnected", "true");
    is("document.createElement('div').isConnected", "false");
    is(with_root("r.innerHTML = '<b>x</b>'; return r.firstChild.isConnected;"), "true");
    is(with_root("return r.isConnected;"), "true");
    is("(function () { var h = document.createElement('div');"
       " var r = h.attachShadow({mode: 'open'}); r.innerHTML = '<b>x</b>';"
       " return r.firstChild.isConnected; })()",
       "false");
    // ...and it follows the host being appended, which is the whole reason it is
    // an accessor rather than a property written when the wrapper was made.
    is("(function () { var h = document.createElement('div');"
       " var r = h.attachShadow({mode: 'open'}); r.innerHTML = '<b>x</b>';"
       " document.body.appendChild(h); return r.firstChild.isConnected; })()",
       "true");
}

} // namespace

int main() {
    test_attach_shadow_makes_a_shadow_root();
    test_the_mode_is_required_and_is_one_of_two_words();
    test_only_some_elements_may_host_one();
    test_a_shadow_root_holds_a_tree();
    test_the_shadow_tree_is_invisible_to_the_light_dom();
    test_query_selector_searches_the_shadow_tree();
    test_get_root_node_stops_at_the_boundary();
    test_is_connected_crosses_the_boundary();
    REPORT("shadow_dom");
}
