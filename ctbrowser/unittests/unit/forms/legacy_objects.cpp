//===- legacy_objects.cpp - Legacy form object tests ------------*- C++ -*-===//
//
// HTMLFormElement and HTMLSelectElement as legacy platform objects (WebIDL
// 3.9, lib/Shell/bindings/element/control_methods/legacy_objects.cpp): a
// form's indexed and named properties and its past names map, a select's
// indexed getter and setter, and that the proxy the page holds is still the
// node every binding and every identity comparison expects.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

#include "check.hpp"
#include "dom_probe.hpp"

namespace {

const char * const page = R"(<!DOCTYPE html><body>
<form id=f><input name=a id=first><input type=radio name=r value=1><input type=radio name=r value=2>
<input type=image name=img><input name=submit><img name=pic></form>
<select id=s><option>x</option><option>y</option></select></body>)";

void test_form() {
    // Indices are `elements`, and an image button is not among them.
    ctbrowser_test::is_in(
        page,
        "(function () { var f = document.getElementById('f');"
        " return [f[0] === f.elements[0], f[3].name, f[4], f.length].join(); })()",
        "true,submit,,4");
    // A named control overrides the interface ([LegacyOverrideBuiltIns]); two
    // of one name are one RadioNodeList; an owned img answers too.
    ctbrowser_test::is_in(page,
                          "(function () { var f = document.getElementById('f');"
                          " return [f.submit === f[3], f.r.length, f.r === f.r, f.r.value,"
                          " f.pic.localName, f.img].join(); })()",
                          "true,2,true,,img,");
    // The descriptor: read-only, configurable, not enumerable - and the page
    // can neither define nor delete over it, but may add a name of its own.
    ctbrowser_test::is_in(
        page,
        "(function () { 'use strict'; var f = document.getElementById('f');"
        " var d = Object.getOwnPropertyDescriptor(f, 'a'); var out = [d.writable,"
        " d.enumerable, d.configurable, d.value.id];"
        " try { delete f.a; out.push('deleted'); } catch (e) { out.push(e.name); }"
        " f.mine = 1; out.push(f.mine, Object.keys(f).indexOf('0') >= 0);"
        " return out.join(); })()",
        "false,false,true,first,TypeError,1,true");
    // [[Set]] ignores named properties: an accessor's setter still runs for a
    // name a control answers to, and any other write of it is refused.
    ctbrowser_test::is_in(R"(<!DOCTYPE html><body><form id=f><input name=action>
<input name=submit></form></body>)",
                          "(function () { var f = document.getElementById('f');"
                          " f.action = 'x.html'; f.submit = 5;"
                          " return [f.action === f[0], f.getAttribute('action'),"
                          " f.submit === f[1]].join(); })()",
                          "true,x.html,true");
    // The past names map: a renamed control still answers to the old name
    // until it leaves the form.
    ctbrowser_test::is_in(page,
                          "(function () { var f = document.getElementById('f');"
                          " var i = document.getElementById('first'); var o = [f.a === i];"
                          " i.name = 'b'; o.push(f.a === i, f.b === i); i.remove();"
                          " o.push(f.a, f.b); return o.join(); })()",
                          "true,true,true,,");
    // A manual slot keeps the object behind the proxy, and hands back what
    // the page holds.
    ctbrowser_test::is_in(
        R"(<!DOCTYPE html><body><div id=host><form id=g></form><select id=t></select></div></body>)",
        "(function () { var root = host.attachShadow({mode: 'open', slotAssignment: 'manual'});"
        " var slot = root.appendChild(document.createElement('slot')); slot.assign(g, t);"
        " var n = slot.assignedNodes(); return [n.length, n[0] === g, n[1] === t,"
        " g.assignedSlot === slot].join(); })()",
        "2,true,true,true");
    // Still the node: identity, instanceof, and the methods on `this`.
    ctbrowser_test::is_in(page,
                          "(function () { var f = document.getElementById('f');"
                          " return [f === document.forms[0], f instanceof HTMLFormElement,"
                          " f.getAttribute('id'), f.first === f.a,"
                          " f.contains(f.a), f[0].form === f].join(); })()",
                          "true,true,f,true,true,true");
}

void test_select() {
    ctbrowser_test::is_in(page,
                          "(function () { var s = document.getElementById('s');"
                          " var o = [s[1].text, s[2]]; var n = new Option('z');"
                          " s[3] = n; o.push(s.length, s[2].text, s[3] === n);"
                          " s[0] = null; o.push(s.length, s[0].text); return o.join(); })()",
                          "y,,4,,true,3,y");
}

} // namespace

int main() {
    test_form();
    test_select();
    REPORT("forms_legacy_objects");
}
