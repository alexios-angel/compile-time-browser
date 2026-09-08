// THE WINDOW AS THE GLOBAL OBJECT, THE REFLECTED ATTRIBUTES, AND THE INTERFACE
// OBJECTS - the shape a wrapper has before any method on it is called: which
// prototype it hangs off, what HTML 2.6 says a reflected `width` or `href`
// answers, and that `window.foo` and a bare `foo` are one binding.
//
// One of eight files carved out of unittests/unit/bindings_basics.cpp on
// 2026-09-07, when it had reached 3,365 lines. Every case is verbatim and in
// the order it had; the helpers more than one of the eight needs are in
// page_probe.hpp beside this, and `find_id` is test/support/dom_probe.hpp's.

#include <ctbrowser/app/app.hpp>
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "page_probe.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;
using ctbrowser_test::check;
using ctbrowser_test::log_of;

namespace {

void test_window_is_the_global_object() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><script>
        // A global reached through the window, which is how a library calls a
        // host function it did not have to look up by bare name.
        console.log('raf=' + (typeof window.requestAnimationFrame));
        console.log('in=' + ('setTimeout' in window) + ',' + ('nosuch' in window));
        // A top-level declaration IS a global, so the window can see it - this
        // is how p5.js decides a sketch is in global mode.
        console.log('decl=' + (typeof window.sketchSetup));
        function sketchSetup() {}
        // ...and a write through the window defines a global, which is how p5
        // installs its ~200 drawing functions for a sketch to call bare.
        window.installed = 7;
        console.log('bare=' + installed);
        // An own property of the window keeps its own storage rather than
        // shadowing itself in the globals.
        console.log('own=' + (window.innerWidth > 0));
        console.log('same=' + (window === globalThis));
        console.log('this=' + (this === window));
        var scriptReceiver = this;
        globalThis = undefined;
        console.log('stable=' + (this === scriptReceiver) + ',' + (this === window));
    </script></body></html>)");
    check(page.script_error().empty(), "the window script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "raf=function", "a global is reachable through the window: " + log[0]);
    check(log[1] == "in=true,false", "`in` asks the globals too: " + log[1]);
    check(log[2] == "decl=function", "a hoisted declaration is on the window: " + log[2]);
    check(log[3] == "bare=7", "a write through the window defines a global: " + log[3]);
    check(log[4] == "own=true", "the window keeps its own properties: " + log[4]);
    check(log[5] == "same=true", "globalThis is the window: " + log[5]);
    check(log[6] == "this=true", "classic script this is the Window view: " + log[6]);
    check(log[7] == "stable=true,true", "replacing globalThis preserves script this: " + log[7]);
}

// The REFLECTED attributes: id, className, width, height.
//
// These are IDL attributes over content attributes - reading one reads the
// attribute, writing one writes it. As data properties they only went one way:
// a page's assignment changed the wrapper alone and the next refresh put the
// old value back. p5.js names its canvas and sizes it exactly that way, so both
// writes vanished and left a nameless 300x150 canvas.
void test_reflected_attributes() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><div id=d></div><canvas id=c></canvas><script>
        const d = document.getElementById('d');
        d.id = 'renamed';
        console.log('id=' + d.getAttribute('id') + ',' + (document.getElementById('renamed') !== null));
        d.className = 'a b';
        console.log('class=' + d.getAttribute('class') + ',' + d.classList.length);
        const c = document.getElementById('c');
        // The HTML defaults, which a page that omits the attributes relies on.
        console.log('default=' + c.width + 'x' + c.height);
        c.width = 640; c.height = 480;
        console.log('sized=' + c.width + 'x' + c.height +
                    ' attr=' + c.getAttribute('width') + 'x' + c.getAttribute('height'));
    </script></body></html>)");
    check(page.script_error().empty(), "the reflection script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "id=renamed,true", "a written id reaches the document: " + log[0]);
    check(log[1] == "class=a b,2", "className and classList agree: " + log[1]);
    check(log[2] == "default=300x150",
          "a canvas without attributes has the HTML defaults: " + log[2]);
    check(log[3] == "sized=640x480 attr=640x480",
          "a written size reaches the attribute: " + log[3]);
}

// REFLECTION AS A TABLE, one case per rule of HTML section 2.6.
//
// Every one of these was `undefined` before the table existed, and the reason
// the file yield in web-platform-tests is low while the subtest yield is
// enormous is visible right here: it is the same six rules applied ~270 times.
void test_reflection_rules() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body>
        <a id=a href="page.html" download=x></a>
        <details id=d></details>
        <td id=c colspan=9999></td>
        <input id=i maxlength=" 42 " size=0 type=CHECKBOX>
        <ol id=o></ol>
        <label id=l for=i></label>
        <script>
        const a = document.getElementById('a');
        // DOMString: the attribute, or "" when absent - never undefined.
        console.log('string=' + a.download + ',' + a.target + ',' + (a.rel === ''));
        // boolean: PRESENCE, not value. Setting false REMOVES.
        const d = document.getElementById('d');
        console.log('bool=' + d.open);
        d.open = true;
        console.log('boolSet=' + d.open + ',' + d.getAttribute('open') + ',' +
                    d.hasAttribute('open'));
        d.open = false;
        console.log('boolClear=' + d.open + ',' + d.hasAttribute('open'));
        // clamped unsigned long: parsed, then clamped into [1, 1000].
        const c = document.getElementById('c');
        console.log('clamped=' + c.colSpan + ',' + c.rowSpan);
        // long / limited long: HTML's integer rules, leading whitespace and all.
        const i = document.getElementById('i');
        console.log('long=' + i.maxLength + ',' + i.minLength);
        // ...and the default when the attribute cannot be parsed at all.
        i.setAttribute('maxlength', 'twelve');
        console.log('longDefault=' + i.maxLength);
        // limited unsigned long: a bad attribute falls back, setting 0 throws.
        console.log('size=' + i.size);
        let threw = '';
        try { i.size = 0; } catch (e) { threw = e.name; }
        console.log('sizeThrow=' + threw + ',' + i.size);
        // enumerated: canonical keyword, and the missing/invalid defaults.
        console.log('enum=' + i.type);
        i.setAttribute('type', 'nonsense');
        console.log('enumInvalid=' + i.type);
        i.removeAttribute('type');
        console.log('enumMissing=' + i.type);
        // an IDL name is camelCase and its content attribute is not.
        console.log('case=' + document.getElementById('l').htmlFor + ',' +
                    (document.getElementById('l').getAttribute('for') === 'i'));
        // a write goes THROUGH to the attribute, in the number's shortest form.
        const o = document.getElementById('o');
        console.log('startDefault=' + o.start);
        o.start = 7.9;
        console.log('start=' + o.start + ',' + o.getAttribute('start'));
        // PER INTERFACE. `href` belongs to <a>, not to every element.
        console.log('scoped=' + (document.createElement('div').href === undefined) + ',' +
                    (typeof a.href));
        </script></body></html>)");
    check(page.script_error().empty(), "the reflection script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log.size() > 15, "every reflection case logged");
    if (log.size() <= 15) { return; }
    check(log[0] == "string=x,,true", "a DOMString is the attribute or empty: " + log[0]);
    check(log[1] == "bool=false", "an absent boolean attribute is false: " + log[1]);
    check(log[2] == "boolSet=true,,true", "setting a boolean writes an empty value: " + log[2]);
    check(log[3] == "boolClear=false,false", "setting one false removes it: " + log[3]);
    check(log[4] == "clamped=1000,1", "colSpan clamps and rowSpan defaults: " + log[4]);
    check(log[5] == "long=42,-1", "HTML integer rules skip leading whitespace: " + log[5]);
    check(log[6] == "longDefault=-1", "an unparseable limited long is -1: " + log[6]);
    check(log[7] == "size=20", "input.size falls back to 20: " + log[7]);
    check(log[8] == "sizeThrow=IndexSizeError,20", "setting size to zero throws: " + log[8]);
    check(log[9] == "enum=checkbox", "an enumerated keyword is canonicalised: " + log[9]);
    check(log[10] == "enumInvalid=text", "the invalid value default: " + log[10]);
    check(log[11] == "enumMissing=text", "the missing value default: " + log[11]);
    check(log[12] == "case=i,true", "htmlFor reflects `for`: " + log[12]);
    check(log[13] == "startDefault=1", "ol.start defaults to 1: " + log[13]);
    check(log[14] == "start=7,7", "a long is written in its shortest form: " + log[14]);
    check(log[15] == "scoped=true,string", "reflection is per interface: " + log[15]);
}

// A REFLECTED URL IS RESOLVED, which is what makes `a.href` different from
// `a.getAttribute('href')` - the first is absolute and the second is what the
// author wrote. Resolution is against the document's own address; see the note
// in element.cpp about there being no <base> support behind it.
void test_reflected_urls_resolve() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><a id=a href="sub/page.html"></a><script>
        const a = document.getElementById('a');
        console.log('raw=' + a.getAttribute('href'));
        console.log('absent=' + document.createElement('a').href);
        </script></body></html>)");
    check(page.script_error().empty(), "the URL script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log.size() > 1, "the URL cases logged");
    if (log.size() <= 1) { return; }
    check(log[0] == "raw=sub/page.html", "getAttribute is what the author wrote: " + log[0]);
    // "" rather than undefined, and rather than "null" - the specification is
    // explicit that an absent URL attribute reflects as the empty string.
    check(log[1] == "absent=", "an absent URL attribute reflects as empty: " + log[1]);
}

// THE INTERFACE OBJECTS, and the chain a wrapper is linked into.
//
// `assert_class_string`, `e instanceof HTMLBodyElement` and
// `eventTarget.constructor.name` are three ways of asking the same question, and
// a wrapper that was a plain object could answer none of them.
void test_dom_interface_objects() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><div id=d></div><script>
        const d = document.getElementById('d');
        // The whole chain, one link at a time.
        console.log('chain=' + (d instanceof HTMLDivElement) + ',' +
                    (d instanceof HTMLElement) + ',' + (d instanceof Element) + ',' +
                    (d instanceof Node) + ',' + (d instanceof EventTarget));
        // ...and what it is NOT.
        console.log('not=' + (d instanceof HTMLAnchorElement) + ',' +
                    (d instanceof HTMLBodyElement));
        console.log('ctor=' + d.constructor.name + ',' + document.body.constructor.name +
                    ',' + document.documentElement.constructor.name);
        console.log('proto=' + (Object.getPrototypeOf(d) === HTMLDivElement.prototype));
        // A node that is not an element has interfaces of its own.
        const t = document.createTextNode('x');
        console.log('text=' + (t instanceof Text) + ',' + (t instanceof CharacterData) + ',' +
                    (t instanceof Node) + ',' + (t instanceof Element));
        // A foreign-namespace element is an Element and NOT an HTMLElement,
        // which is the assertion Body-FrameSet-Event-Handlers.html opens with.
        const foreign = document.createElementNS('http://example.com/', 'example');
        console.log('foreign=' + (foreign instanceof Element) + ',' +
                    (foreign instanceof HTMLElement));
        // The document and the window are EventTargets with names of their own.
        console.log('hosts=' + document.constructor.name + ',' + window.constructor.name);
        // NOT CONSTRUCTIBLE, which is what a browser says too.
        let threw = '';
        try { new HTMLDivElement(); } catch (e) { threw = e.name; }
        console.log('illegal=' + threw);
        // The four interfaces the engine already had keep working, on the same
        // chain rather than beside it.
        const c = document.createElement('canvas');
        console.log('canvas=' + (c instanceof HTMLCanvasElement) + ',' +
                    (c instanceof HTMLElement) + ',' + c.constructor.name);
        // A tag nobody has heard of is an HTMLUnknownElement; one with a hyphen
        // in its name is a valid custom element name and so an HTMLElement.
        console.log('unknown=' + document.createElement('blink').constructor.name + ',' +
                    document.createElement('my-widget').constructor.name + ',' +
                    document.createElement('section').constructor.name);
        </script></body></html>)");
    check(page.script_error().empty(), "the interface script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log.size() > 8, "every interface case logged");
    if (log.size() <= 8) { return; }
    check(log[0] == "chain=true,true,true,true,true", "the whole prototype chain: " + log[0]);
    check(log[1] == "not=false,false", "an interface it is not: " + log[1]);
    check(log[2] == "ctor=HTMLDivElement,HTMLBodyElement,HTMLHtmlElement",
          "constructor.name per tag: " + log[2]);
    check(log[3] == "proto=true", "the wrapper is linked to its own prototype: " + log[3]);
    check(log[4] == "text=true,true,true,false", "a Text is not an Element: " + log[4]);
    check(log[5] == "foreign=true,false", "a foreign element is not an HTMLElement: " + log[5]);
    check(log[6] == "hosts=Document,Window", "the document and the window: " + log[6]);
    check(log[7] == "illegal=TypeError", "an interface object is not constructible: " + log[7]);
    check(log[8] == "canvas=true,true,HTMLCanvasElement",
          "the interfaces that already existed join the chain: " + log[8]);
    check(log.size() > 9 && log[9] == "unknown=HTMLUnknownElement,HTMLElement,HTMLElement",
          "an unrecognised tag is HTMLUnknownElement: " + (log.size() > 9 ? log[9] : ""));
}

} // namespace

int main() {
    test_reflected_attributes();
    test_reflection_rules();
    test_reflected_urls_resolve();
    test_dom_interface_objects();
    test_window_is_the_global_object();
    REPORT("dom_interfaces");
}
