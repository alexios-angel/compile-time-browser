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

// A parsed element is upgraded by define(), connectedCallback has run before
// define() returns, and the wrapper a page held before is the very object
// that is now an instance of the class.
void test_define_upgrades_what_the_parser_made() {
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

} // namespace

int main() {
    test_define_validates();
    test_define_upgrades_what_the_parser_made();
    test_create_element_and_new_construct_through_the_class();
    test_attribute_changed_follows_observed_attributes();
    test_when_defined_resolves_on_define();
    test_a_page_without_a_definition_is_untouched();
    REPORT("custom_elements");
}
