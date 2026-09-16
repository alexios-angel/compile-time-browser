// The shadow DOM beyond `attachShadow`: the init dictionary read back off the
// root, slots and their assignments, and the declarative shadow roots
// `setHTMLUnsafe` attaches.
//
// The regression net for lib/Shell/bindings/shadow_dom.cpp and the
// attachShadow half of element/shadow.cpp. What it pins down is that an
// assignment is COMPUTED from the two trees - move the child, and the answer
// moves with it - and that `innerHTML` and `setHTMLUnsafe` differ in exactly
// one thing: whether a `<template shadowrootmode>` becomes a shadow root.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include <string>
#include <string_view>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

[[nodiscard]] std::string said(std::string_view html) {
    browser page{browser_options{200, 100}};
    page.load_html(html);
    std::string out;
    for (const std::string & one : page.alerts()) {
        if (!out.empty()) { out += ';'; }
        out += one;
    }
    if (!page.script_error().empty()) { out += "|error:" + page.script_error(); }
    return out;
}

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
// the FIRST slot of that name in the shadow tree gets it, and a closed root
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
                  // The declarative root is CLAIMED by a matching attachShadow,
                  // once, and emptied; the mismatching mode and the second call
                  // are both refused.
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
                  "</script></body></html>"),
             "false;1,light;true,<slot></slot>;true;NotSupportedError;true,;NotSupportedError;"
             "true,false;true");
}

} // namespace

int main() {
    test_the_init_dictionary_is_readable_back();
    test_slots_assign_by_name();
    test_set_html_unsafe_attaches_a_declarative_root();
    REPORT("shadow_dom");
}
