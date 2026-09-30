// customElements: define, construct, upgrade, and WHEN the reactions run.
//
// The regression net for lib/Shell/bindings/custom_elements.cpp. What it pins
// down is the model rather than the method list: an element a definition
// covers is the author's class - `instanceof` says so and the identity a page
// already held is kept - and a reaction runs BEFORE the call that caused it
// returns, which is what every test in web-platform-tests reads first
// (`define()` then `element.click()` on the same line).
//
// The page's own `alert()` is the observation channel, as in
// unit/mutation_observer.cpp: it keeps order and survives the turn.

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

void test_define_validates() {
    CHECK_EQ(said("<html><body><script>"
                  "function threw(f){ try { f(); return 'ok'; } catch (e) { return e.name; } }"
                  "class A extends HTMLElement {}"
                  "alert(threw(function(){ customElements.define('nodash', A); }));"
                  "alert(threw(function(){ customElements.define('Upper-case', A); }));"
                  "alert(threw(function(){ customElements.define('font-face', A); }));"
                  "alert(threw(function(){ customElements.define('x-a', 5); }));"
                  "alert(threw(function(){ customElements.define('x-a', A); }));"
                  "alert(threw(function(){ customElements.define('x-a', A); }));"
                  "alert(threw(function(){ customElements.define('x-b', A); }));"
                  "alert(threw(function(){ customElements.define('x-c', class extends A {},"
                  " {extends: 'x-a'}); }));"
                  "alert(customElements.get('x-a') === A);"
                  "alert(customElements.get('x-zz'));"
                  "alert(customElements instanceof CustomElementRegistry);"
                  "</script></body></html>"),
             "SyntaxError;SyntaxError;SyntaxError;TypeError;ok;NotSupportedError;"
             "NotSupportedError;NotSupportedError;true;undefined;true");
}

// THE NAME RULE IS "valid element local name" WITH AN ASCII LOWER ALPHA IN
// FRONT, not the old PotentialCustomElementName: after the first character,
// anything the HTML tokenizer would read back as a tag name is a name, so
// punctuation a page could not use before is legal now and only the characters
// that would END a tag name - and an ASCII capital - are refused. The FIRST
// character is still [a-z] alone, which is why `_-element` and an accented
// first letter are not names however valid a local name they are.
// whatwg/html#7991, measured by
// custom-elements/registries/valid-custom-element-names.html.
void test_define_takes_the_modern_name_rule() {
    CHECK_EQ(said("<html><body><script>"
                  "function threw(f){ try { f(); return 'ok'; } catch (e) { return e.name; } }"
                  "function define(n){ return threw(function(){"
                  "  customElements.define(n, class extends HTMLElement {}); }); }"
                  "alert(define('a!-element'));"
                  "alert(define('a\\u0001-element'));"
                  "alert(define('_-element'));"
                  "alert(define('\\u00e9-element'));"
                  "alert(define('a-\\u00e9lement'));"
                  "alert(define('a -element'));"
                  "alert(define('a/-element'));"
                  "alert(define('a>-element'));"
                  "alert(define('-element'));"
                  "alert(define('.-element'));"
                  "</script></body></html>"),
             "ok;ok;SyntaxError;SyntaxError;ok;SyntaxError;SyntaxError;SyntaxError;SyntaxError;"
             "SyntaxError");
}

// A parsed element is upgraded by define(), connectedCallback has run before
// define() returns, and the wrapper a page held before is the very object
// that is now an instance of the class.
void test_define_upgrades_what_the_parser_made() {
    CHECK_EQ(
        said("<html><body><x-proxy id=a></x-proxy><script>"
             "const before = document.getElementById('a');"
             "class C extends HTMLElement { constructor() { super(); this.textContent = 'ok'; } }"
             "customElements.define('x-proxy', new Proxy(C, {}));"
             "alert(before.textContent + ',' + (before instanceof C) + ',' +"
             " (before === document.getElementById('a')));"
             "</script></body></html>"),
        "ok,true,true");
    CHECK_EQ(said("<html><body><x-foo id=a></x-foo><script>"
                  "var before = document.getElementById('a');"
                  "var log = [];"
                  "class Foo extends HTMLElement {"
                  "  constructor() { super(); log.push('ctor:' + this.id); }"
                  "  connectedCallback() { log.push('connected'); }"
                  "}"
                  "customElements.define('x-foo', Foo);"
                  "alert(log.join(','));"
                  "alert(before instanceof Foo);"
                  "alert(before === document.getElementById('a'));"
                  "</script></body></html>"),
             "ctor:a,connected;true;true");
}

// createElement and `new` both build through the class, synchronously, and a
// detached element hears nothing until it is appended.
void test_create_element_and_new_construct_through_the_class() {
    CHECK_EQ(said("<html><body><script>"
                  "var log = [];"
                  "class Bar extends HTMLElement {"
                  "  constructor() { super(); log.push('ctor'); }"
                  "  connectedCallback() { log.push('in'); }"
                  "  disconnectedCallback() { log.push('out'); }"
                  "}"
                  "customElements.define('x-bar', Bar);"
                  "var a = document.createElement('x-bar');"
                  "var b = new Bar();"
                  "alert(log.join(','));"
                  "alert(a instanceof Bar);"
                  "alert(b.tagName + ':' + b.isConnected);"
                  "document.body.appendChild(a);"
                  "alert(log.join(','));"
                  "a.remove();"
                  "alert(log.join(','));"
                  "alert(a instanceof HTMLElement);"
                  "</script></body></html>"),
             "ctor,ctor;true;X-BAR:false;ctor,ctor,in;ctor,ctor,in,out;true");
}

// A CUSTOMIZED BUILT-IN constructs through its own interface's constructor -
// `super()` in `class extends HTMLScriptElement` - which is HTML's one "HTML
// element constructor" with the interface check; an interface the definition
// does not extend is a TypeError. And a script made that way runs when it
// connects, before its connectedCallback
// (Node-appendChild-cereactions-vs-script.window.js).
void test_a_customized_built_in_constructs_through_its_interface() {
    CHECK_EQ(said("<html><body><script>"
                  "var log = [];"
                  "class S extends HTMLScriptElement {"
                  "  constructor() { super(); }"
                  "  connectedCallback() { log.push('connected'); }"
                  "}"
                  "customElements.define('s-1', S, {extends: 'script'});"
                  "var s = new S();"
                  "s.textContent = 'log.push(\"ran\")';"
                  "alert(s.tagName + ':' + s.getAttribute('is') + ':' + (s instanceof S) + ':' +"
                  "      (s instanceof HTMLScriptElement));"
                  "document.body.append(s);"
                  "alert(log.join(','));"
                  "class D extends HTMLDivElement { constructor() { super(); } }"
                  "customElements.define('d-1', D, {extends: 'span'});"
                  "try { new D(); alert('constructed'); } catch (e) { alert(e.name); }"
                  "</script></body></html>"),
             "SCRIPT:s-1:true:true;ran,connected;TypeError");
}

void test_attribute_changed_follows_observed_attributes() {
    CHECK_EQ(said("<html><body><x-baz id=z lang=en data-x=1></x-baz><script>"
                  "var log = [];"
                  "class Baz extends HTMLElement {"
                  "  static get observedAttributes() { return ['lang', 'title']; }"
                  "  attributeChangedCallback(n, o, v) { log.push(n + ':' + o + '>' + v); }"
                  "}"
                  "customElements.define('x-baz', Baz);"
                  "var z = document.getElementById('z');"
                  "z.setAttribute('title', 't');"
                  "z.setAttribute('data-x', '2');"
                  "z.setAttribute('lang', 'fr');"
                  "z.removeAttribute('lang');"
                  "alert(log.join(','));"
                  "</script></body></html>"),
             "lang:null>en,title:null>t,lang:en>fr,lang:fr>null");
}

void test_when_defined_resolves_on_define() {
    CHECK_EQ(said("<html><body><script>"
                  "customElements.whenDefined('x-later').then(function (c) {"
                  "  alert('defined:' + (c === Later)); });"
                  "class Later extends HTMLElement {}"
                  "customElements.define('x-later', Later);"
                  "customElements.whenDefined('x-later').then(function (c) {"
                  "  alert('already:' + (c === Later)); });"
                  "customElements.whenDefined('bad').catch(function (e) { alert(e.name); });"
                  "</script></body></html>"),
             "defined:true;already:true;SyntaxError");
}

// A page that never defines anything pays nothing: the funnel returns on its
// first line, and `new HTMLElement()` is still illegal.
void test_a_page_without_a_definition_is_untouched() {
    CHECK_EQ(said("<html><body><script>"
                  "try { new HTMLElement(); alert('made'); } catch (e) { alert(e.name); }"
                  "var d = document.createElement('x-none');"
                  "alert(d instanceof HTMLElement);"
                  "</script></body></html>"),
             "TypeError;true");
}

// A CUSTOMIZED BUILT-IN from `createElement(tag, {is})`, and a clone of one:
// DOM's "create an element" constructs through the class synchronously for
// createElement and enqueues an upgrade for a clone, detached as it is
// (builtin-coverage.html, 333 subtests at 9cd0a9e4).
void test_create_element_with_is_and_clone_node() {
    CHECK_EQ(said("<html><body><div id=c></div><script>"
                  "var log = [];"
                  "class A extends HTMLAnchorElement {"
                  "  constructor() { super(); log.push('ctor'); }"
                  "  connectedCallback() { log.push('in'); }"
                  "}"
                  "customElements.define('my-a', A, {extends: 'a'});"
                  "var a = document.createElement('a', {is: 'my-a'});"
                  "alert((a instanceof A) + ':' + a.localName + ':' + a.getAttribute('is'));"
                  "alert(a.cloneNode().constructor === A);"
                  "document.getElementById('c').innerHTML = '<a is=\"my-a\" id=x></a>';"
                  "var x = document.getElementById('x');"
                  "alert((x instanceof A) + ':' + (x.cloneNode(true).constructor === A));"
                  "var d = document.createElement('div');"
                  "d.innerHTML = '<a is=\"my-a\"></a>';"
                  "alert(d.firstChild instanceof A);"
                  "alert(log.join(','));"
                  "</script></body></html>"),
             "true:a:my-a;true;true:true;true;ctor,ctor,ctor,in,ctor,ctor");
}

// HTML 4.13.4's element definition reads the constructor in the order the
// specification lists, a prototype that is a function or a Proxy is an
// object, and an arrow function is not a constructor.
void test_define_reads_the_constructor_in_spec_order() {
    CHECK_EQ(said("<html><body><script>"
                  "function threw(f){ try { f(); return 'ok'; } catch (e) { return e.name; } }"
                  "var calls = [];"
                  "var C = function () {};"
                  "C.prototype = HTMLElement;"
                  "var P = new Proxy(C, { get: function (t, n) { calls.push(n); return t[n]; } });"
                  "alert(threw(function(){ customElements.define('x-p', P); }));"
                  "alert(calls.join(','));"
                  "alert(threw(function(){ customElements.define('x-q', () => {}); }));"
                  "alert(threw(function(){ customElements.define('x-r', class extends "
                  "HTMLElement {}, {extends: 'bgsound'}); }));"
                  "var w1 = customElements.whenDefined('x-w');"
                  "alert(w1 === customElements.whenDefined('x-w'));"
                  "</script></body></html>"),
             "ok;prototype,disabledFeatures,formAssociated;TypeError;NotSupportedError;true");
}

// AN UPGRADE THAT FAILS: the constructor's exception is reported at the
// window, the element's state is `failed`, nothing later re-upgrades it, and a
// second `super()` in one upgrade is an InvalidStateError. A `new` that has no
// definition is a TypeError even when a parent class has one.
void test_upgrade_failure_is_reported_and_final() {
    CHECK_EQ(said("<html><body><x-bad id=b></x-bad><x-two id=t></x-two><script>"
                  "window.addEventListener('error', function (e) {"
                  "  alert('reported:' + (e.error && e.error.message)); });"
                  "class Bad extends HTMLElement {"
                  "  constructor() { super(); throw new Error('boom'); }"
                  "  connectedCallback() { alert('connected'); }"
                  "}"
                  "customElements.define('x-bad', Bad);"
                  "var b = document.getElementById('b');"
                  "alert(b instanceof Bad);"
                  "b.remove(); document.body.appendChild(b);"
                  "class Two extends HTMLElement {}"
                  "customElements.define('x-two', Two);"
                  "alert(document.getElementById('t') instanceof Two);"
                  "class Sub extends Two {}"
                  "try { new Sub(); alert('made'); } catch (e) { alert(e.name); }"
                  "</script></body></html>"),
             "reported:boom;true;true;TypeError");
}

// A reaction is enqueued for a CUSTOM element, and only for a callback its
// class has (HTML 4.13.6). What an upgrading constructor does to its own
// element enqueues nothing - the upgrade's attributeChanged and connected are
// read when the upgrade RUNS; an element one upgrade connects is upgraded
// inside that call; and a removal with no disconnectedCallback leaves the
// child's pending connectedCallback where it was.
void test_reactions_wait_for_custom_and_a_callback() {
    CHECK_EQ(
        said("<html><body><x-up id=a title=t></x-up><x-in id=p></x-in><x-in id=q></x-in><script>"
             "var log = [];"
             "class Up extends HTMLElement {"
             "  static get observedAttributes() { return ['title', 'id']; }"
             "  constructor() { super(); this.setAttribute('title', 'u'); this.remove();"
             "    log.push('ctor'); }"
             "  attributeChangedCallback(n, o, v) { log.push('attr:' + n + '=' + v); }"
             "  connectedCallback() { log.push('connected'); }"
             "  disconnectedCallback() { log.push('disconnected'); }"
             "}"
             "customElements.define('x-up', Up);"
             "alert(log.join(','));"
             "var order = [];"
             "customElements.define('x-in', class extends HTMLElement {"
             "  constructor() { super(); order.push(this.id + ':begin');"
             "    if (this.id == 'p') { var q = document.getElementById('q');"
             "      q.remove(); document.body.appendChild(q); }"
             "    order.push(this.id + ':end'); } });"
             "alert(order.join(','));"
             "var seen = [];"
             "class Parent extends HTMLElement { connectedCallback() {"
             "  seen.push('begin'); this.firstChild.remove(); seen.push('end'); } }"
             "class Child extends HTMLElement { connectedCallback() { seen.push('connected'); } }"
             "customElements.define('x-parent', Parent);"
             "customElements.define('x-child', Child);"
             "var parent = new Parent(); parent.appendChild(new Child());"
             "document.body.appendChild(parent);"
             "alert(seen.join(','));"
             "</script></body></html>"),
        "ctor,attr:id=a,attr:title=t,connected;p:begin,q:begin,q:end,p:end;begin,end,connected");
}

// SCOPED REGISTRIES (DOM 4.9, HTML 4.13.3): a node HAS a registry, fixed at
// creation - createElement's and attachShadow's option, the parser's intended
// parent, initialize() for what had none - and a definition upgrades only
// what has ITS registry. `new C()` for a class only a scoped registry holds is
// a TypeError; createElement through that registry constructs it.
void test_scoped_registries() {
    CHECK_EQ(said("<html><body><script>"
                  "var scoped = new CustomElementRegistry();"
                  "class G extends HTMLElement {}; class S extends HTMLElement {};"
                  "customElements.define('a-b', G); scoped.define('a-b', S);"
                  "alert(document.createElement('a-b') instanceof G);"
                  "var s = document.createElement('a-b', {customElementRegistry: scoped});"
                  "alert(s instanceof S && s.customElementRegistry === scoped);"
                  "alert(document.createElement('div', {customElementRegistry: null})"
                  ".customElementRegistry);"
                  "try { new S(); alert('constructed'); } catch (e) { alert(e.name); }"
                  "var host = document.body.appendChild(document.createElement('div'));"
                  "var root = host.attachShadow({mode: 'open', customElementRegistry: scoped});"
                  "root.innerHTML = '<a-b><a-b></a-b></a-b>';"
                  "alert(root.customElementRegistry === scoped);"
                  "alert(root.firstChild instanceof S && root.firstChild.firstChild instanceof S);"
                  "var bare = document.createElement('div', {customElementRegistry: null});"
                  "bare.innerHTML = '<a-b></a-b>';"
                  "var late = new CustomElementRegistry();"
                  "class L extends HTMLElement {}; late.define('a-b', L);"
                  "late.initialize(bare);"
                  "alert(bare.firstChild.customElementRegistry === late);"
                  "alert(bare.firstChild instanceof L);"
                  "alert(document.customElementRegistry === customElements);"
                  "</script></body></html>"),
             "true;true;null;TypeError;true;true;true;true;true");
}

// THE PARSER CONSTRUCTS a defined custom element SYNCHRONOUSLY (HTML 13.2.6.1),
// with the throw-on-dynamic-markup-insertion counter up: document.write from
// its constructor throws, and its connectedCallback runs before the parser
// reads its children.
void test_the_parser_constructs_synchronously() {
    CHECK_EQ(said("<html><body><script>"
                  "var log = [];"
                  "customElements.define('x-p', class extends HTMLElement {"
                  "  constructor() { super(); log.push('ctor:' + this.attributes.length);"
                  "    try { document.write('x'); } catch (e) { log.push(e.name); } }"
                  "  connectedCallback() { log.push('connected:' + this.childNodes.length); } });"
                  "</script><x-p a=1><i></i></x-p><script>alert(log.join(','));</script>"
                  "</body></html>"),
             "ctor:0,InvalidStateError,connected:0");
}

// PAGE-CONTROLLED RECEIVERS AND COLLECTIONS DURING define(). HTMLElement
// called on an array or proxy made to look like an instance, or on an element
// with no upgrade running, is a TypeError - not a write through the wrong
// object type. And define() reads the callbacks and observedAttributes through
// page getters that may collect and mutate: what it read earlier must survive,
// and a list that grows while it is converted is converted as it was.
constexpr const char * churn = "function churn(){ var a = []; for (var i = 0; i < 200000; ++i)"
                               " a.push({i: i}); return a.length; }";

void test_page_controlled_receivers_are_refused() {
    CHECK_EQ(said(std::string{"<html><body><script>"} +
                  "function threw(f){ try { f(); return 'ok'; } catch (e) { return e.name; } }"
                  "class C extends HTMLElement {}"
                  "customElements.define('x-recv', C);"
                  "var arr = []; Object.setPrototypeOf(arr, C.prototype);"
                  "alert(threw(function(){ HTMLElement.call(arr); }));"
                  "var px = new Proxy({}, {}); Object.setPrototypeOf(px, C.prototype);"
                  "alert(threw(function(){ HTMLElement.call(px); }));"
                  "var el = document.createElement('x-recv');"
                  "alert(threw(function(){ HTMLElement.call(el); }));"
                  "alert(el instanceof C);"
                  "</script></body></html>"),
             "TypeError;TypeError;TypeError;true");
}

void test_define_survives_collection_in_its_getters() {
    CHECK_EQ(said(std::string{"<html><body><script>"} + churn +
                  "var list = null;"
                  "class D extends HTMLElement {"
                  "  static get observedAttributes() {"
                  "    list = [{toString() { for (var i = 0; i < 64; ++i) list.push('x' + i);"
                  "      churn(); return 'a'; }}, 'b'];"
                  "    return list; }"
                  "  get connectedCallback() { return function() { alert('connected'); }; }"
                  "  get adoptedCallback() { churn(); return undefined; }"
                  "  get attributeChangedCallback() {"
                  "    return function(n) { alert('changed:' + n); }; }"
                  "}"
                  "customElements.define('x-churn', D);"
                  "churn();"
                  "var el = document.createElement('x-churn');"
                  "document.body.appendChild(el);"
                  "el.setAttribute('x5', '1'); el.setAttribute('b', '1');"
                  "</script></body></html>"),
             "connected;changed:b");
}

} // namespace

int main() {
    test_define_validates();
    test_define_takes_the_modern_name_rule();
    test_define_upgrades_what_the_parser_made();
    test_create_element_and_new_construct_through_the_class();
    test_a_customized_built_in_constructs_through_its_interface();
    test_attribute_changed_follows_observed_attributes();
    test_when_defined_resolves_on_define();
    test_a_page_without_a_definition_is_untouched();
    test_create_element_with_is_and_clone_node();
    test_define_reads_the_constructor_in_spec_order();
    test_upgrade_failure_is_reported_and_final();
    test_reactions_wait_for_custom_and_a_callback();
    test_scoped_registries();
    test_the_parser_constructs_synchronously();
    test_page_controlled_receivers_are_refused();
    test_define_survives_collection_in_its_getters();
    REPORT("custom_elements");
}
