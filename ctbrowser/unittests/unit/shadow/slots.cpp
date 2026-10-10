//===- slots.cpp - Shadow DOM slots and declarative roots -------*- C++ -*-===//
//
// The shadow DOM beyond the root itself: the rest of `attachShadow`'s init
// dictionary read back, slots and their assignments, and the declarative
// shadow roots `setHTMLUnsafe` and the page parser attach.
// unittests/unit/shadow/dom.cpp covers attachShadow and the root as a node.
//
// This guards lib/Shell/bindings/shadow_dom.cpp and the init-dictionary half
// of lib/Shell/bindings/element/shadow.cpp. It pins down that a slot
// assignment is computed from the two trees (move the child and the answer
// moves with it), and that `innerHTML` and `setHTMLUnsafe` differ in exactly
// one thing: whether a `<template shadowrootmode>` becomes a shadow root.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "dom_probe.hpp"
#include <string>
#include <string_view>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser_test::said;

namespace {

void test_the_init_dictionary_is_readable_back() {
    CHECK_EQ(
        said("<html><body><div id=h></div><script>"
             "function threw(f){ try { f(); return 'ok'; } catch (e) { return e.name; } }"
             "var h = document.getElementById('h');"
             "var r = h.attachShadow({mode: 'open', delegatesFocus: true,"
             " slotAssignment: 'manual', clonable: true, serializable: true});"
             "alert(r.mode + ',' + r.delegatesFocus + ',' + r.slotAssignment + ','"
             " + r.clonable + ',' + r.serializable);"
             "alert(r.host === h);"
             "alert(threw(function(){ h.attachShadow({mode: 'open'}); }));"
             "var d = document.createElement('div');"
             "alert(threw(function(){ d.attachShadow({mode: 'open',"
             " slotAssignment: 'sideways'}); }));"
             "var svg = document.createElementNS('http://www.w3.org/2000/svg', 'div');"
             "alert(threw(function(){ svg.attachShadow({mode: 'open'}); }));"
             "var made = document.createElement('div').attachShadow({mode: 'closed'});"
             "alert(made.delegatesFocus + ',' + made.slotAssignment + ',' + made.serializable);"
             "</script></body></html>"),
        "open,true,manual,true,true;true;NotSupportedError;TypeError;NotSupportedError;"
        "false,named,false");
}

// The assignment is a function of the two trees: the light child names a slot,
// the first slot of that name in the shadow tree gets it, and a closed root
// hides which slot that was from the light side.
void test_slots_assign_by_name() {
    CHECK_EQ(said("<html><body><div id=h><span id=a slot=one></span><b id=b></b></div><script>"
                  "var h = document.getElementById('h');"
                  "var a = document.getElementById('a');"
                  "var b = document.getElementById('b');"
                  "var r = h.attachShadow({mode: 'open'});"
                  "r.innerHTML = '<slot name=one id=s1></slot><slot id=s2>fallback</slot>';"
                  "var s1 = r.getElementById('s1'), s2 = r.getElementById('s2');"
                  "alert(s1.assignedNodes().length + ',' + (s1.assignedNodes()[0] === a));"
                  "alert(s2.assignedNodes().length + ',' + (s2.assignedNodes()[0] === b));"
                  "alert(a.assignedSlot === s1);"
                  "alert(s1.assignedElements().length);"
                  "a.setAttribute('slot', 'other');"
                  "alert(s1.assignedNodes().length + ',' + a.assignedSlot);"
                  "alert(s1.assignedNodes({flatten: true}).length);"
                  "alert(s1.name + ',' + s2.name);"
                  "</script></body></html>"),
             "1,true;1,true;true;1;0,null;0;one,");
}

// `slotAssignment: "manual"`: nothing is assigned by name, and what the page
// did assign stops counting as soon as the node leaves the host.
void test_manual_slot_assignment() {
    CHECK_EQ(said("<html><body><div id=h><span id=a></span><b id=b></b></div><script>"
                  "var h = document.getElementById('h');"
                  "var a = document.getElementById('a'), b = document.getElementById('b');"
                  "var r = h.attachShadow({mode: 'open', slotAssignment: 'manual'});"
                  "r.innerHTML = '<slot id=s></slot>';"
                  "var s = r.getElementById('s');"
                  "alert(s.assignedNodes().length);"
                  "s.assign(a, b);"
                  "alert(s.assignedNodes().length + ',' + (s.assignedNodes()[0] === a));"
                  "h.removeChild(b);"
                  "alert(s.assignedNodes().length);"
                  "s.assign();"
                  "alert(s.assignedNodes().length);"
                  "</script></body></html>"),
             "0;2,true;1;0");
}

void test_set_html_unsafe_attaches_a_declarative_root() {
    CHECK_EQ(said("<html><body><div id=w></div><script>"
                  "function threw(f){ try { f(); return 'ok'; } catch (e) { return e.name; } }"
                  "var w = document.getElementById('w');"
                  "w.setHTMLUnsafe('<div id=e><template shadowrootmode=open shadowrootclonable>"
                  "<slot></slot></template><span>light</span></div>');"
                  "var e = w.querySelector('#e');"
                  "alert(!!w.querySelector('template'));"
                  "alert(e.children.length + ',' + e.children[0].textContent);"
                  "alert(!!e.shadowRoot + ',' + e.shadowRoot.innerHTML);"
                  "alert(e.shadowRoot.clonable);"
                  // A matching attachShadow claims the declarative root, once,
                  // and empties it; a mismatching mode and a second call are
                  // both refused.
                  "alert(threw(function(){ e.attachShadow({mode: 'closed'}); }));"
                  "var same = e.attachShadow({mode: 'open'});"
                  "alert((same === e.shadowRoot) + ',' + same.innerHTML);"
                  "alert(threw(function(){ e.attachShadow({mode: 'open'}); }));"
                  // innerHTML leaves the template exactly where it is.
                  "var safe = document.createElement('div');"
                  "safe.innerHTML = '<div><template shadowrootmode=open></template></div>';"
                  "alert(!!safe.querySelector('template') + ',' "
                  "+ !!safe.firstChild.shadowRoot);"
                  "alert(safe.getHTML() === safe.innerHTML);"
                  // Document.parseHTMLUnsafe: a new document, the same conversion.
                  "var d = Document.parseHTMLUnsafe('<div id=p><template shadowrootmode=open>"
                  "<b>s</b></template></div><script>alert(1)</scr' + 'ipt>');"
                  "var p = d.getElementById('p');"
                  "alert((d !== document) + ',' + !!p.shadowRoot + ',' + p.shadowRoot.innerHTML "
                  "+ ',' + d.URL + ',' + d.querySelectorAll('script').length);"
                  "</script></body></html>"),
             "false;1,light;true,<slot></slot>;true;NotSupportedError;true,;NotSupportedError;"
             "true,false;true;true,true,<b>s</b>,about:blank,1");
}

// The page's own parser attaches declarative roots as it meets the template,
// not afterwards: a script inside the root already runs connected, the host keeps
// only its light children, and a template on an element that cannot host one
// - or on one that already does - stays an ordinary template. DOMParser's
// document does not allow declarative roots at all.
void test_the_page_parser_attaches_declarative_roots() {
    CHECK_EQ(said("<html><body><div id=h><template shadowrootmode=OPEN><b>in</b><script>"
                  "alert('ran:' + document.getElementById('h').shadowRoot.firstChild.textContent)"
                  "</script></template><span>light</span></div>"
                  "<progress id=g><template shadowrootmode=open>y</template></progress>"
                  "<div id=m><template shadowrootmode=closed>1</template>"
                  "<template shadowrootmode=open>2</template></div><script>"
                  "var h = document.getElementById('h');"
                  "alert((h.querySelector('template') === null) + ',' + h.children.length);"
                  "var g = document.getElementById('g');"
                  "alert(!!g.querySelector('template') + ',' + g.shadowRoot);"
                  "var m = document.getElementById('m');"
                  "alert(m.shadowRoot + ',' + m.querySelector('template').content.textContent);"
                  "var d = new DOMParser().parseFromString("
                  "'<div id=x><template shadowrootmode=open></template></div>', 'text/html');"
                  "alert(d.getElementById('x').shadowRoot);"
                  "</script></body></html>"),
             "ran:in;true,1;true,null;null,2;null");
}

// setHTMLUnsafe's options and its template target: with `runScripts` the
// parsed scripts run once the whole fragment is in, one inside a declarative
// shadow root included, and without it none does; a <template>'s markup goes
// into its contents, never under the element.
void test_set_html_unsafe_options() {
    CHECK_EQ(said("<html><body><div id=w></div><script>"
                  "var w = document.getElementById('w'); window.ran = [];"
                  "w.setHTMLUnsafe('<script>ran.push(1)</scr' + 'ipt>');"
                  "w.setHTMLUnsafe('<script>ran.push(2)</scr' + 'ipt><div>"
                  "<template shadowrootmode=open><script>ran.push(3)</scr' + 'ipt>"
                  "</template></div>', {runScripts: true});"
                  "alert(ran.join());"
                  "var t = document.createElement('template'); t.setHTMLUnsafe('<b>x</b>');"
                  "alert(t.childNodes.length + ',' + t.content.firstChild.nodeName);"
                  "</script></body></html>"),
             "2,3;0,B");
}

// The template's declarative attributes, reflected: the mode as an
// enumerated attribute limited to known values, the slot assignment with
// "named" for missing and invalid, and the booleans.
void test_template_reflects_its_declarative_attributes() {
    CHECK_EQ(said("<html><body><script>"
                  "var t = document.createElement('template');"
                  "t.setAttribute('shadowrootmode', 'OpEn');"
                  "var before = t.shadowRootMode; t.shadowRootMode = 'blah';"
                  "alert(before + ',' + JSON.stringify(t.shadowRootMode) + ','"
                  " + t.getAttribute('shadowrootmode'));"
                  "t.shadowRootClonable = true; t.shadowRootSlotAssignment = 'invalid';"
                  "alert(t.hasAttribute('shadowrootclonable') + ',' + t.shadowRootSlotAssignment"
                  " + ',' + t.shadowRootDelegatesFocus + ','"
                  " + HTMLTemplateElement.prototype.hasOwnProperty('shadowRootMode'));"
                  "</script></body></html>"),
             "open,\"\",blah;true,named,false,true");
}

} // namespace

// slotchange-event.html: slotchange is not synchronous, fires once per slot
// however many mutations changed it (including at a slot removed after its
// assignment changed), runs after the mutation observers' callbacks, and is
// not fired for a slot whose assignment did not change.
void test_slotchange_events() {
    browser page{browser_options{200, 100}};
    page.load_html(
        "<html><body><div id=h></div><script>"
        "var seen = [];"
        "var h = document.getElementById('h');"
        "var r = h.attachShadow({mode: 'open'});"
        "var s = document.createElement('slot'); s.name = 'x';"
        "s.addEventListener('slotchange', function (e) { seen.push('s:' + (e.target === s) + ','"
        " + e.bubbles + ',' + e.cancelable + ',' + e.composed); });"
        "var d = document.createElement('slot');"
        "d.addEventListener('slotchange', function () { seen.push('d'); });"
        "r.appendChild(s); r.appendChild(d);"
        "new MutationObserver(function () { seen.push('mo'); }).observe(h, {childList: true});"
        "var a = document.createElement('span'); a.slot = 'x';"
        "h.appendChild(a); h.appendChild(document.createElement('b'));"
        "h.appendChild(document.createElement('i'));"
        "r.removeChild(s);"
        "seen.push('sync:' + seen.length);"
        "queueMicrotask(function () { seen.push('micro'); });"
        "setTimeout(function () { h.removeChild(a); alert(seen.join('|')); }, 0);"
        "setTimeout(function () { alert(seen.filter(function (x) { return x !== 'mo'; })"
        ".join('|')); }, 5);"
        "</script></body></html>");
    for (int i = 0; i < 4; ++i) { (void)page.tick(16.0); }
    std::string out;
    for (const std::string & one : page.alerts()) { out += one + ";"; }
    // The second alert: no slotchange for a slot nothing moved on (and the
    // observer's own records are not this test's question).
    CHECK_EQ(out, std::string{"sync:0|mo|s:true,true,false,false|d|micro;"
                              "sync:0|s:true,true,false,false|d|micro;"});
}

int main() {
    test_the_init_dictionary_is_readable_back();
    test_slots_assign_by_name();
    test_manual_slot_assignment();
    test_set_html_unsafe_attaches_a_declarative_root();
    test_the_page_parser_attaches_declarative_roots();
    test_set_html_unsafe_options();
    test_template_reflects_its_declarative_attributes();
    test_slotchange_events();
    REPORT("shadow_slots");
}
